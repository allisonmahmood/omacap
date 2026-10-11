import QtQuick
import OmaCap

// Omasnap's status pill: one line saying what is happening or what to do next.
ChromeBox {
    property alias text: label.text

    visible: label.text !== ""
    width: label.implicitWidth + 28
    height: 30
    radius: 10
    fill: theme.surface
    border: "panel"

    Text {
        id: label

        anchors.centerIn: parent
        font: theme.font(13)
        color: theme.foreground
    }

}
