import QtQuick
import OmaCap

// A segmented choice: one of `options`, the chosen one filled with the accent. Each segment
// is named `namePrefix + option`.
ChromeBox {
    id: control

    property var options: []
    property string current
    property string namePrefix

    signal picked(string option)

    implicitWidth: row.implicitWidth + 6
    implicitHeight: 30
    radius: 8
    fill: theme.button
    border: "button"

    Row {
        id: row

        anchors.centerIn: parent
        spacing: 2

        Repeater {
            model: control.options

            Rectangle {
                required property string modelData
                readonly property bool chosen: control.current === modelData

                objectName: control.namePrefix + modelData
                width: label.implicitWidth + 18
                height: 24
                radius: 6
                color: chosen ? theme.accent : segment.containsMouse ? theme.buttonHover : "transparent"

                Text {
                    id: label

                    anchors.centerIn: parent
                    text: parent.modelData
                    font: theme.font(12, parent.chosen)
                    color: parent.chosen ? theme.accentText : theme.alpha(theme.foreground, 190)
                }

                MouseArea {
                    id: segment

                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: control.picked(parent.modelData)
                }

            }

        }

    }

}
