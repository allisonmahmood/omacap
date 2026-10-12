// A notification daemon for tests/record-nested.py, run with `quickshell -p` on the harness's
// private bus: every notification is a magenta toast in the middle of the screen for
// OMACAP_TEST_TOAST_MS (default 5000; 0 keeps it up, as Omarchy's shell keeps a hovered toast).
// A toast its sender closes goes at once, unless OMACAP_TEST_KEEP_CLOSED=1: then it stays until
// it expires, as Omarchy's shell keeps it. As in Omarchy's shell, `notifications dismiss NEEDLE`
// over quickshell's IPC takes down every toast whose summary holds NEEDLE, answering ok or none.
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Services.Notifications

ShellRoot {
    id: root

    readonly property bool keepClosed: Quickshell.env("OMACAP_TEST_KEEP_CLOSED") === "1"
    readonly property int lifetime: Number(Quickshell.env("OMACAP_TEST_TOAST_MS") || 5000)
    // The toasts on screen: {id, summary}.
    property var shown: []

    function hide(id) {
        shown = shown.filter(toast => toast.id !== id);
    }

    NotificationServer {
        onNotification: notification => {
            notification.tracked = true;
            const id = notification.id;
            root.shown = root.shown.concat([{
                    id: id,
                    summary: notification.summary
                }]);
            notification.closed.connect(() => {
                if (!root.keepClosed)
                    root.hide(id);
            });
            if (root.lifetime > 0)
                expiry.createObject(root, {
                    toast: id
                });
        }
    }

    IpcHandler {
        target: "notifications"

        function dismiss(needle: string): string {
            const hit = needle !== "" && root.shown.some(toast => toast.summary.includes(needle));
            if (hit)
                root.shown = root.shown.filter(toast => !toast.summary.includes(needle));
            return hit ? "ok" : "none";
        }
    }

    Component {
        id: expiry

        Timer {
            property int toast
            interval: root.lifetime
            running: true
            onTriggered: {
                root.hide(toast);
                destroy();
            }
        }
    }

    PanelWindow {
        visible: root.shown.length > 0
        implicitWidth: 400
        implicitHeight: 200
        color: "#ff00ff"
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.namespace: "omacap-test-notification"
    }
}
