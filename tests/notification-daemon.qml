// A notification daemon for tests/record-nested.py, run with `quickshell -p` on the harness's
// private bus: every notification is a magenta toast in the middle of the screen for
// OMACAP_TEST_TOAST_MS (default 5000). A toast its sender closes goes at once, unless
// OMACAP_TEST_KEEP_CLOSED=1: then it stays until it expires, as Omarchy's shell keeps it.
import QtQuick
import Quickshell
import Quickshell.Wayland
import Quickshell.Services.Notifications

ShellRoot {
    id: root

    readonly property bool keepClosed: Quickshell.env("OMACAP_TEST_KEEP_CLOSED") === "1"
    readonly property int lifetime: Number(Quickshell.env("OMACAP_TEST_TOAST_MS") || 5000)
    property var shown: []

    function hide(id) {
        shown = shown.filter(other => other !== id);
    }

    NotificationServer {
        onNotification: notification => {
            notification.tracked = true;
            const id = notification.id;
            root.shown = root.shown.concat([id]);
            notification.closed.connect(() => {
                if (!root.keepClosed)
                    root.hide(id);
            });
            expiry.createObject(root, {
                toast: id
            });
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
