import QtQuick
import QtQuick.Layouts

// The camera overlay: shown or hidden, size, shape, corners, shadow and position.
Popover {
    id: popover

    // Its height on the 1080-tall preview, for the corner positions.
    property real cameraHeight: 0

    function moveTo(corner) {
        backend.beginEdit();
        backend.setValue("cameraX", corner % 2 === 0 ? 0.03 : 0.97 - backend.edit.cameraSize);
        backend.setValue("cameraY", corner < 2 ? 0.04 : 0.96 - cameraHeight / 1080);
        backend.endEdit();
    }

    title: "Camera"
    key: "P"

    PopoverRow {
        label: "Camera"

        ChromeSegmented {
            objectName: "cameraShown"
            options: ["Shown", "Hidden"]
            current: (backend.edit.camera ?? true) ? "Shown" : "Hidden"
            onPicked: (option) => backend.setValue("camera", option === "Shown")
        }

    }

    ChromeSetting {
        label: "Size"
        fieldName: "cameraSize"
        from: 0.08
        to: 0.4
        current: backend.edit.cameraSize || 0.18
    }

    PopoverRow {
        label: "Shape"

        ChromeSegmented {
            options: ["Rectangle", "Circle"]
            current: backend.edit.cameraShape === "circle" ? "Circle" : "Rectangle"
            onPicked: (option) => backend.setValue("cameraShape", option.toLowerCase())
        }

    }

    ChromeSetting {
        enabled: backend.edit.cameraShape !== "circle"
        opacity: enabled ? 1 : 0.4
        label: "Corners"
        fieldName: "cameraCorners"
        to: 0.5
        current: backend.edit.cameraCorners ?? 0.12
    }

    ChromeSetting {
        label: "Shadow"
        fieldName: "cameraShadow"
        current: backend.edit.cameraShadow ?? 0.45
    }

    PopoverRow {
        label: "Position"

        Repeater {
            model: [["corner-tl", "Top left"], ["corner-tr", "Top right"], ["corner-bl", "Bottom left"], ["corner-br", "Bottom right"]]

            ChromeButton {
                required property var modelData
                required property int index

                implicitWidth: 30
                implicitHeight: 30
                radius: 7
                icon: modelData[0]
                tooltip: modelData[1]
                onClicked: popover.moveTo(index)
            }

        }

    }

    PopoverNote {
        text: "Or drag the camera in the preview."
    }

}
