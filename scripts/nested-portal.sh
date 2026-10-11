#!/usr/bin/env bash
# A private screen-cast portal for the nested Hyprland, so recordings can be tested without
# touching your session's portal, restore tokens or settings.
#
#   scripts/nested-hyprland.sh start STATE_DIR
#   scripts/nested-portal.sh start STATE_DIR   # prints the env to export
#   scripts/nested-portal.sh stop STATE_DIR
#
# Runs its own D-Bus session bus with xdg-desktop-portal and xdg-desktop-portal-hyprland
# connected to the nested display. XDPH's picker is a script that picks the monitor named in
# STATE_DIR/portal/pick (default: the nested display's first monitor) and logs each run to
# STATE_DIR/portal/picker.log, so no human click is ever needed. XDG config, data, cache and
# state live under STATE_DIR/home; PipeWire is your session's.
set -euo pipefail
command=${1:?start or stop}
state=$(realpath -m "${2:?state directory}")
portal=$state/portal
case $command in
start)
  [[ -f $state/env ]] || { echo "start the nested Hyprland first: scripts/nested-hyprland.sh start $state" >&2; exit 1; }
  # shellcheck disable=SC1091
  source "$state/env"
  export WAYLAND_DISPLAY HYPRLAND_INSTANCE_SIGNATURE
  rm -rf "$portal"
  mkdir -p "$portal"/{services,portals} "$state"/home/{config/hypr,data,cache,state}
  cat >"$portal/bus.conf" <<CONF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:path=$portal/bus</listen>
  <servicedir>$portal/services</servicedir>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
CONF
  # Only the permission store is activatable: it keeps restore tokens, in the isolated data
  # dir. The document portal stays off; it would mount over your session's.
  printf '[D-BUS Service]\nName=org.freedesktop.impl.portal.PermissionStore\nExec=/usr/lib/xdg-permission-store\n' \
    >"$portal/services/org.freedesktop.impl.portal.PermissionStore.service"
  cp /usr/share/xdg-desktop-portal/portals/hyprland.portal "$portal/portals/"
  monitor=$(hyprctl monitors -j | python -c 'import json,sys; print(json.load(sys.stdin)[0]["name"])')
  echo "$monitor" >"$portal/pick"
  cat >"$portal/picker.sh" <<PICKER
#!/bin/sh
echo "\$(date +%s.%N) \$*" >>"$portal/picker.log"
echo "[SELECTION]r/screen:\$(cat "$portal/pick")"
PICKER
  chmod +x "$portal/picker.sh"
  printf 'screencopy {\n  custom_picker_binary = %s\n  allow_token_by_default = true\n}\n' \
    "$portal/picker.sh" >"$state/home/config/hypr/xdph.conf"
  cat >"$portal/env" <<ENV
DBUS_SESSION_BUS_ADDRESS=unix:path=$portal/bus
WAYLAND_DISPLAY=$WAYLAND_DISPLAY
HYPRLAND_INSTANCE_SIGNATURE=$HYPRLAND_INSTANCE_SIGNATURE
XDG_CURRENT_DESKTOP=Hyprland
XDG_CONFIG_HOME=$state/home/config
XDG_DATA_HOME=$state/home/data
XDG_CACHE_HOME=$state/home/cache
XDG_STATE_HOME=$state/home/state
ENV
  set -a
  # shellcheck disable=SC1091
  source "$portal/env"
  set +a
  unset DISPLAY
  export XDG_DESKTOP_PORTAL_DIR=$portal/portals
  dbus-daemon --config-file="$portal/bus.conf" --fork --print-pid >"$portal/pids"
  for _ in $(seq 50); do [[ -S $portal/bus ]] && break; sleep 0.1; done
  /usr/lib/xdg-desktop-portal-hyprland >"$portal/xdph.log" 2>&1 &
  echo $! >>"$portal/pids"
  /usr/lib/xdg-desktop-portal --replace >"$portal/xdp.log" 2>&1 &
  echo $! >>"$portal/pids"
  has() { busctl --address="$DBUS_SESSION_BUS_ADDRESS" list --no-legend 2>/dev/null | grep -q "^$1 "; }
  for _ in $(seq 100); do
    has org.freedesktop.portal.Desktop && has org.freedesktop.impl.portal.desktop.hyprland && break
    sleep 0.1
  done
  has org.freedesktop.portal.Desktop || { echo "the portal did not start; see $portal/*.log" >&2; exit 1; }
  cat "$portal/env"
  ;;
stop)
  [[ -f $portal/pids ]] || exit 0
  # Only the processes this script started; the permission store exits with the bus.
  while read -r pid; do kill "$pid" 2>/dev/null || true; done <"$portal/pids"
  rm -f "$portal/pids"
  ;;
*) echo "usage: $0 start|stop STATE_DIR" >&2; exit 2 ;;
esac
