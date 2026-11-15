#!/usr/bin/env bash
# A hidden nested Hyprland for testing OmaCap's overlays without touching your session.
#
#   scripts/nested-hyprland.sh start STATE_DIR   # prints the env to export
#   scripts/nested-hyprland.sh stop STATE_DIR
#
# The nested compositor opens as a window on a hidden special workspace of the running
# Hyprland and keeps rendering there (render_unfocused). Point clients at it with the
# printed WAYLAND_DISPLAY and HYPRLAND_INSTANCE_SIGNATURE; `grim` captures it.
set -euo pipefail
command=${1:?start or stop}
state=${2:?state directory}
mkdir -p "$state"
case $command in
start)
  cat >"$state/hyprland.lua" <<'CONF'
hl.monitor({ output = "", mode = "1600x1000", position = "auto", scale = 1 })
hl.config({
    misc = { disable_hyprland_logo = true, disable_splash_rendering = true },
    animations = { enabled = false },
})
CONF
  displays() { ls "$XDG_RUNTIME_DIR" | grep -E '^wayland-[0-9]+$' | sort; }
  instances() { ls "$XDG_RUNTIME_DIR/hypr" | sort; }
  before_display=$(displays)
  before_instance=$(instances)
  hyprctl dispatch "hl.dsp.exec_cmd(\"env -u HYPRLAND_INSTANCE_SIGNATURE Hyprland --config $state/hyprland.lua > $state/hyprland.log 2>&1\", { workspace = \"special:omacaptest silent\", float = true, size = \"1600 1000\", render_unfocused = true })" >/dev/null
  display='' instance=''
  for _ in $(seq 100); do
    display=$(comm -13 <(echo "$before_display") <(displays) | head -1)
    instance=$(comm -13 <(echo "$before_instance") <(instances) | head -1)
    [[ -n $display && -n $instance && -S $XDG_RUNTIME_DIR/hypr/$instance/.socket.sock ]] && break
    sleep 0.1
  done
  [[ -n $display && -n $instance ]] || { echo "nested Hyprland did not start; see $state/hyprland.log" >&2; exit 1; }
  printf 'WAYLAND_DISPLAY=%s\nHYPRLAND_INSTANCE_SIGNATURE=%s\n' "$display" "$instance" | tee "$state/env"
  ;;
stop)
  [[ -f $state/env ]] || exit 0
  # shellcheck disable=SC1091
  instance=$(sed -n 's/^HYPRLAND_INSTANCE_SIGNATURE=//p' "$state/env")
  # Everything below acts on this name. An empty one would address the live session's Hyprland
  # and its runtime directory.
  if [[ ! $instance =~ ^[[:alnum:]]+_[0-9]+_[0-9]+$ ]]; then
    echo "$state/env names no Hyprland instance; nothing was stopped" >&2
    exit 1
  fi
  # Hyprland removes its lock, which holds its pid, when it exits.
  lock=$XDG_RUNTIME_DIR/hypr/$instance/hyprland.lock
  pid=$(head -1 "$lock" 2>/dev/null || true)
  # Hyprland's Lua configs take `hl.dsp.exit()`; older ones the plain `exit`.
  HYPRLAND_INSTANCE_SIGNATURE=$instance hyprctl dispatch 'hl.dsp.exit()' >/dev/null 2>&1 ||
    HYPRLAND_INSTANCE_SIGNATURE=$instance hyprctl dispatch exit >/dev/null 2>&1 || true
  running() { [[ -e $lock ]] || { [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null; }; }
  for _ in $(seq 50); do
    running || break
    sleep 0.1
  done
  if running; then
    echo "the nested Hyprland${pid:+ (pid $pid)} is still running; $state/env is kept" >&2
    exit 1
  fi
  rm -rf -- "$XDG_RUNTIME_DIR/hypr/$instance"
  rm -f "$state/env"
  ;;
*) echo "usage: $0 start|stop STATE_DIR" >&2; exit 2 ;;
esac
