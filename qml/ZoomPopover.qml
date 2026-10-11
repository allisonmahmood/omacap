import QtQuick
import QtQuick.Layouts

// The selected zoom's amount, or how to add one.
Popover {
    id: popover

    // The selected zoom's index and value, or -1 and null.
    property int index: -1
    property var zoom: null

    signal add()
    signal remove()

    title: popover.zoom ? "Zoom " + (popover.index + 1) : "Zoom"
    key: "Z"

    PopoverRow {
        visible: popover.zoom !== null
        label: "Amount"

        ChromeSlider {
            id: amount

            objectName: "zoomAmount"
            Layout.fillWidth: true
            from: 1
            to: 4
            value: popover.zoom ? popover.zoom.amount : 1.8
            onPressedChanged: pressed ? backend.beginEdit() : backend.endEdit()
            onMoved: {
                const z = popover.zoom;
                if (z)
                    backend.updateZoom(popover.index, z.start, z.end, z.x, z.y, value);
            }
        }

        Text {
            text: amount.value.toFixed(1) + "×"
            font: theme.mono(11)
            color: theme.foreground
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 34
        }

    }

    PopoverNote {
        text: popover.zoom ? "Drag the lit area on the preview to aim it. The wheel changes the amount." : "Add a zoom at the playhead, or click an empty part of the Zooms lane."
    }

    RowLayout {
        ChromeChip {
            visible: popover.zoom === null
            icon: "plus"
            text: "Add zoom"
            key: "Z"
            onClicked: popover.add()
        }

        ChromeChip {
            visible: popover.zoom !== null
            objectName: "deleteZoom"
            icon: "trash"
            text: "Delete zoom"
            key: "Del"
            onClicked: popover.remove()
        }

    }

}
