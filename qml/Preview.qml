import QtQuick

// The styled recording, fitted into the editor. Drag the camera to move it. While a zoom is
// selected and paused, the preview shows the whole frame with the zoom's view lit, to aim.
Item {
    id: preview

    // The selected zoom's index, or -1.
    property int zoomIndex: -1
    readonly property bool aiming: zoomIndex >= 0 && !backend.playing
    readonly property alias composition: composition

    // A click on the recording itself, outside the camera and the aim.
    signal clicked()
    // A press on the zoom aim, which keeps that zoom selected.
    signal aimPressed()

    Item {
        id: fit

        // The composition is laid out at 1920 px wide, as the exporter lays it out.
        readonly property real scale: width / 1920

        width: Math.min(parent.width, parent.height * 16 / 9)
        height: width * 9 / 16
        anchors.centerIn: parent

        MouseArea {
            anchors.fill: parent
            onClicked: preview.clicked()
        }

        Composition {
            id: composition

            objectName: "composition"
            width: 1920
            height: 1080
            scale: fit.scale
            transformOrigin: Item.TopLeft
            edit: backend.edit
            screenSource: backend.duration > 0 ? backend.screenSource : ""
            cameraSource: backend.duration > 0 ? backend.cameraSource : ""
            sourceAspect: backend.aspect
            zoom: preview.aiming ? 1 : backend.zoom
            focusX: backend.focusX
            focusY: backend.focusY
        }

        StatusPill {
            anchors.centerIn: parent
            visible: backend.duration === 0 && backend.phase === "editor"
            text: "No clips left. Undo to restore them."
        }

        MouseArea {
            property real px
            property real py

            objectName: "cameraDrag"
            visible: backend.edit.camera && backend.cameraSource !== "" && !preview.aiming
            x: composition.cameraX * fit.scale
            y: composition.cameraY * fit.scale
            width: composition.cameraW * fit.scale
            height: composition.cameraH * fit.scale
            cursorShape: Qt.SizeAllCursor
            onPressed: (mouse) => {
                preview.clicked();
                px = mouse.x;
                py = mouse.y;
                backend.beginEdit();
            }
            onPositionChanged: (mouse) => {
                if (pressed) {
                    backend.setValue("cameraX", (x + mouse.x - px) / fit.width);
                    backend.setValue("cameraY", (y + mouse.y - py) / fit.height);
                }
            }
            onReleased: backend.endEdit()
            onCanceled: backend.endEdit()
        }

        ZoomAim {
            anchors.fill: parent
            visible: preview.aiming
            index: preview.zoomIndex
            frame: Qt.rect(composition.frameX * fit.scale, composition.frameY * fit.scale, composition.frameW * fit.scale, composition.frameH * fit.scale)
            onPressed: preview.aimPressed()
        }

    }

}
