import QtQuick

// Window transparency: the backdrop shows through the recorded window.
Popover {
    title: "Window transparency"
    key: "G"

    PopoverRow {
        label: "Window"

        ChromeSegmented {
            namePrefix: "transparency"
            options: ["Off", "Light", "Custom"]
            current: {
                const mode = backend.edit.windowTransparency ?? "off";
                return mode.charAt(0).toUpperCase() + mode.slice(1);
            }
            onPicked: (option) => backend.setValue("windowTransparency", option.toLowerCase())
        }

    }

    ChromeSetting {
        objectName: "windowOpacitySetting"
        visible: backend.edit.windowTransparency === "custom"
        label: "Opacity"
        fieldName: "windowOpacity"
        stepSize: 0.01
        current: backend.edit.windowOpacity ?? 0.96
    }

    PopoverNote {
        text: "Light is 96% opaque. The recorded window only; the camera stays opaque."
    }

}
