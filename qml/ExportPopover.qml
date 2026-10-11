import QtQuick
import QtQuick.Layouts
import "exports.js" as Exports

// Export: format, size, frame rate and quality, remembered for next time and for the card;
// then progress with Cancel, and the finished file.
Popover {
    id: popover

    readonly property bool exporting: backend.phase === "exporting"
    property int format: Exports.remembered(backend, "export/format", Exports.formats.length)
    property int widthIndex: Exports.remembered(backend, "export/widthIndex" + format, Exports.widths[format].length)
    property int rateIndex: Exports.remembered(backend, "export/fpsIndex" + format, Exports.rates[format].length)
    property int quality: Exports.remembered(backend, "export/quality", Exports.qualities.length)

    title: "Export"
    key: "E"

    PopoverRow {
        visible: !popover.exporting
        label: "Format"

        ChromeSegmented {
            namePrefix: "format"
            options: Exports.formats
            current: Exports.formats[popover.format]
            onPicked: (option) => {
                popover.format = Exports.formats.indexOf(option);
                backend.remember("export/format", popover.format);
            }
        }

    }

    PopoverRow {
        visible: !popover.exporting
        label: "Width"

        ChromeSegmented {
            options: Exports.widths[popover.format]
            current: options[popover.widthIndex]
            onPicked: (option) => {
                popover.widthIndex = options.indexOf(option);
                backend.remember("export/widthIndex" + popover.format, popover.widthIndex);
            }
        }

    }

    PopoverRow {
        visible: !popover.exporting
        label: "Frame rate"

        ChromeSegmented {
            options: Exports.rates[popover.format]
            current: options[popover.rateIndex]
            onPicked: (option) => {
                popover.rateIndex = options.indexOf(option);
                backend.remember("export/fpsIndex" + popover.format, popover.rateIndex);
            }
        }

    }

    PopoverRow {
        visible: !popover.exporting && popover.format === 0
        label: "Quality"

        ChromeSegmented {
            options: Exports.qualities
            current: options[popover.quality]
            onPicked: (option) => {
                popover.quality = options.indexOf(option);
                backend.remember("export/quality", popover.quality);
            }
        }

    }

    PopoverNote {
        visible: !popover.exporting
        text: popover.format === 1 ? "A silent 16:9 loop of the whole edit. Busy motion makes large files." : "16:9 H.264 video with AAC audio."
    }

    ChromeChip {
        visible: !popover.exporting
        objectName: "chooseExport"
        accent: true
        icon: "export"
        text: "Choose file and export"
        enabled: backend.phase === "editor" && backend.duration > 0
        Layout.fillWidth: true
        onClicked: {
            const settings = Exports.current(backend);
            stage.chooseExport(settings.gif, settings.width, settings.fps, settings.quality);
        }
    }

    // Progress moves in steps as the exporter reports it; nothing animates in between.
    RowLayout {
        visible: popover.exporting
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 4
            radius: 2
            color: theme.alpha(theme.foreground, 41)

            Rectangle {
                width: parent.width * Math.min(1, backend.progress / 0.9)
                height: parent.height
                radius: 2
                color: theme.accent
            }

        }

        Text {
            text: Math.round(Math.min(1, backend.progress / 0.9) * 100) + "%"
            font: theme.mono(11)
            color: theme.foreground
        }

        ChromeChip {
            objectName: "cancelExport"
            text: "Cancel"
            onClicked: backend.cancelExport()
        }

    }

    PopoverNote {
        visible: !popover.exporting && backend.message !== ""
        text: backend.message
        color: backend.message.indexOf("10 MB") >= 0 ? theme.warning : theme.alpha(theme.foreground, 150)
    }

    RowLayout {
        visible: !popover.exporting && backend.lastExport !== ""

        ChromeChip {
            icon: "open"
            text: "Open file"
            onClicked: backend.openExport()
        }

        ChromeChip {
            icon: "folder"
            text: "Show in folder"
            onClicked: backend.revealExport()
        }

    }

}
