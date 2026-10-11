import QtQuick
import OmaCap

// Aims the selected zoom on the preview: the area it zooms to is lit and the rest dimmed.
// Drag the area, or click to centre it; the wheel or a corner changes the amount. Each drag,
// and each run of wheel notches, is one undo step.
Item {
    id: aim

    property int index: -1
    // The recording's frame on the unzoomed preview.
    property rect frame
    readonly property var zoom: index >= 0 ? backend.edit.zooms[index] ?? null : null
    readonly property var crop: backend.edit.crop || [0, 0, 1, 1]
    readonly property real amount: zoom ? zoom.amount : 1
    // The lit area, as Composition places a zoomed view: the focus centred where it can be,
    // the view kept inside the frame.
    readonly property rect view: {
        const w = frame.width / amount, h = frame.height / amount;
        const fx = zoom ? (zoom.x - crop[0]) / crop[2] : 0.5;
        const fy = zoom ? (zoom.y - crop[1]) / crop[3] : 0.5;
        return Qt.rect(frame.x + Math.max(0, Math.min(frame.width - w, fx * frame.width - w / 2)), frame.y + Math.max(0, Math.min(frame.height - h, fy * frame.height - h / 2)), w, h);
    }

    signal pressed()

    // Centres the view on (cx, cy) in this item, keeping it inside the frame.
    function aimAt(cx, cy, amount) {
        const z = zoom;
        if (!z)
            return;
        const w = frame.width / amount, h = frame.height / amount;
        const left = Math.max(0, Math.min(frame.width - w, cx - frame.x - w / 2));
        const top = Math.max(0, Math.min(frame.height - h, cy - frame.y - h / 2));
        backend.updateZoom(index, z.start, z.end, crop[0] + crop[2] * (left + w / 2) / frame.width, crop[1] + crop[3] * (top + h / 2) / frame.height, amount);
    }

    function changeAmount(next) {
        aimAt(view.x + view.width / 2, view.y + view.height / 2, Math.max(1, Math.min(4, next)));
    }

    // Ends a run of wheel notches now, before undo or redo steps past it, so its timer can't
    // end whatever edit comes next.
    function settle() {
        if (!wheelRun.running)
            return;
        wheelRun.stop();
        backend.endEdit();
    }

    // Dims everything around the view.
    Repeater {
        model: [Qt.rect(0, 0, aim.width, aim.view.y), Qt.rect(0, aim.view.y + aim.view.height, aim.width, aim.height - aim.view.y - aim.view.height), Qt.rect(0, aim.view.y, aim.view.x, aim.view.height), Qt.rect(aim.view.x + aim.view.width, aim.view.y, aim.width - aim.view.x - aim.view.width, aim.view.height)]

        Rectangle {
            required property rect modelData

            x: modelData.x
            y: modelData.y
            width: Math.max(0, modelData.width)
            height: Math.max(0, modelData.height)
            color: theme.alpha(theme.scrim, 148)
        }

    }

    MouseArea {
        id: drag

        property point grab

        objectName: "zoomAim"
        anchors.fill: parent
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: (mouse) => {
            aim.pressed();
            backend.beginEdit();
            const inside = mouse.x >= aim.view.x && mouse.x <= aim.view.x + aim.view.width && mouse.y >= aim.view.y && mouse.y <= aim.view.y + aim.view.height;
            if (!inside)
                aim.aimAt(mouse.x, mouse.y, aim.amount);
            grab = Qt.point(mouse.x - aim.view.x - aim.view.width / 2, mouse.y - aim.view.y - aim.view.height / 2);
        }
        onPositionChanged: (mouse) => {
            if (pressed)
                aim.aimAt(mouse.x - grab.x, mouse.y - grab.y, aim.amount);
        }
        onReleased: backend.endEdit()
        onCanceled: backend.endEdit()
        onWheel: (wheel) => {
            backend.beginEdit();
            wheelRun.restart();
            aim.changeAmount(Math.round((aim.amount + wheel.angleDelta.y / 1200) * 10) / 10);
        }
    }

    Timer {
        id: wheelRun

        interval: 400
        onTriggered: backend.endEdit()
    }

    ChromeBox {
        x: aim.view.x - 1
        y: aim.view.y - 1
        width: aim.view.width + 2
        height: aim.view.height + 2
        radius: 3
        borderWidth: 2
        border: "active"
    }

    // Corner handles resize the view about its centre, which sets the amount.
    Repeater {
        model: 4

        Rectangle {
            id: handle

            required property int index

            x: aim.view.x + (index % 2 ? aim.view.width : 0) - width / 2
            y: aim.view.y + (index >= 2 ? aim.view.height : 0) - height / 2
            width: 11
            height: 11
            radius: 5.5
            color: theme.foreground
            border.width: 2
            border.color: theme.surface

            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: handle.index === 0 || handle.index === 3 ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                onPressed: {
                    aim.pressed();
                    backend.beginEdit();
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return;
                    const point = mapToItem(aim, mouse.x, mouse.y);
                    const half = Math.max(Math.abs(point.x - aim.view.x - aim.view.width / 2), Math.abs(point.y - aim.view.y - aim.view.height / 2) * aim.frame.width / aim.frame.height);
                    aim.changeAmount(aim.frame.width / Math.max(1, 2 * half));
                }
                onReleased: backend.endEdit()
                onCanceled: backend.endEdit()
            }

        }

    }

    // The focus point.
    Rectangle {
        x: aim.view.x + aim.view.width / 2 - 8
        y: aim.view.y + aim.view.height / 2 - 8
        width: 16
        height: 16
        radius: 8
        color: "transparent"
        border.width: 2
        border.color: theme.foreground

        Rectangle {
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: 3
            color: theme.accent
        }

    }

    ChromeBox {
        x: aim.view.x + aim.view.width / 2 - width / 2
        y: aim.view.y + aim.view.height - height - 10
        width: tag.implicitWidth + 18
        height: 24
        radius: 7
        fill: theme.surface
        border: "panel"
        visible: aim.view.height > 60

        Text {
            id: tag

            anchors.centerIn: parent
            text: "Zoom " + (aim.index + 1) + " · " + aim.amount.toFixed(1) + "× · drag to aim, wheel to change"
            font: theme.font(11)
            color: theme.foreground
        }

    }

}
