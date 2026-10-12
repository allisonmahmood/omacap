import QtQuick
import OmaCap

// Omasnap's status pill: one line saying what is happening or what to do next. Narrower
// than its text, the text is elided.
ChromeBox {
    property alias text: label.text

    visible: label.text !== ""
    implicitWidth: label.implicitWidth + 28
    height: 30
    radius: 10
    fill: theme.surface
    border: "panel"

    Text {
        id: label

        anchors.centerIn: parent
        width: Math.min(implicitWidth, parent.width - 28)
        elide: Text.ElideRight
        font: theme.font(13)
        color: theme.foreground
    }

}
