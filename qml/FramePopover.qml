import QtQuick

// The frame around the recording: padding, corners and shadow.
Popover {
    title: "Frame"
    key: "F"

    ChromeSetting {
        objectName: "paddingSetting"
        label: "Padding"
        fieldName: "padding"
        to: 0.25
        current: backend.edit.padding || 0
    }

    ChromeSetting {
        objectName: "cornersSetting"
        label: "Corners"
        fieldName: "corners"
        to: 0.08
        current: backend.edit.corners || 0
    }

    ChromeSetting {
        objectName: "shadowSetting"
        label: "Shadow"
        fieldName: "shadow"
        current: backend.edit.shadow || 0
    }

}
