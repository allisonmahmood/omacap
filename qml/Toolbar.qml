import QtQuick

// The editor's toolbar, laid out as Omasnap's: 36 px buttons 2.5 px apart, in groups split
// by dividers. History | look (backdrop, frame, transparency, camera) | timeline tools |
// export and close. Buttons report actions; the editor decides what they do.
Row {
    id: toolbar

    // The editor state the buttons show.
    property string popover
    property bool cropping
    property bool cutting
    property bool zoomSelected
    property bool hasCamera
    property bool editable

    signal triggered(string action)

    // The button for `action`, for anchoring its popover.
    function button(action) {
        for (let i = 0; i < children.length; ++i)
            if (children[i].objectName === action + "Button")
                return children[i];
        return null;
    }

    spacing: 2.5

    ChromeButton {
        objectName: "undoButton"
        icon: "undo"
        tooltip: "Undo · Ctrl+Z"
        enabled: toolbar.editable && !toolbar.cropping
        onClicked: toolbar.triggered("undo")
    }

    ChromeButton {
        objectName: "redoButton"
        icon: "redo"
        tooltip: "Redo · Ctrl+Shift+Z"
        enabled: toolbar.editable && !toolbar.cropping
        onClicked: toolbar.triggered("redo")
    }

    ChromeDivider {
    }

    ChromeButton {
        objectName: "backdropButton"
        icon: "background"
        tooltip: toolbar.popover === "backdrop" ? "" : "Backdrop · B"
        selected: toolbar.popover === "backdrop"
        enabled: toolbar.editable
        onClicked: toolbar.triggered("backdrop")
    }

    ChromeButton {
        objectName: "frameButton"
        icon: "frame"
        tooltip: toolbar.popover === "frame" ? "" : "Frame: padding, corners, shadow · F"
        selected: toolbar.popover === "frame"
        enabled: toolbar.editable
        onClicked: toolbar.triggered("frame")
    }

    ChromeButton {
        objectName: "glassButton"
        icon: "glass"
        tooltip: toolbar.popover === "glass" ? "" : "Window transparency · G"
        selected: toolbar.popover === "glass"
        enabled: toolbar.editable
        onClicked: toolbar.triggered("glass")
    }

    ChromeButton {
        objectName: "cameraButton"
        icon: "person"
        tooltip: toolbar.popover === "camera" ? "" : toolbar.hasCamera ? "Camera · P" : "Camera · this recording has none"
        selected: toolbar.popover === "camera"
        enabled: toolbar.editable && toolbar.hasCamera
        onClicked: toolbar.triggered("camera")
    }

    ChromeDivider {
    }

    ChromeButton {
        objectName: "selectButton"
        icon: "tool-select"
        tooltip: "Select clips and ranges · V"
        selected: !toolbar.cutting && !toolbar.cropping
        enabled: toolbar.editable
        onClicked: toolbar.triggered("select")
    }

    ChromeButton {
        objectName: "cropButton"
        icon: "crop"
        tooltip: "Crop · K"
        selected: toolbar.cropping
        enabled: toolbar.editable
        onClicked: toolbar.triggered("crop")
    }

    ChromeButton {
        objectName: "zoomButton"
        icon: "zoom"
        tooltip: toolbar.popover === "zoom" ? "" : "Zoom · Z adds one at the playhead"
        selected: toolbar.zoomSelected || toolbar.popover === "zoom"
        enabled: toolbar.editable
        onClicked: toolbar.triggered("zoom")
    }

    ChromeButton {
        objectName: "cutButton"
        icon: "scissors"
        tooltip: "Cut · C · click a track to split it"
        selected: toolbar.cutting
        enabled: toolbar.editable
        onClicked: toolbar.triggered("cut")
    }

    ChromeDivider {
    }

    ChromeButton {
        objectName: "exportButton"
        icon: "export"
        tooltip: toolbar.popover === "export" ? "" : "Export · E"
        primary: true
        selected: toolbar.popover === "export"
        onClicked: toolbar.triggered("export")
    }

    ChromeButton {
        objectName: "closeButton"
        icon: "close"
        tooltip: "Close · Esc"
        onClicked: toolbar.triggered("close")
    }

}
