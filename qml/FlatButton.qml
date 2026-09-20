import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool primary: false
    implicitHeight: 32
    padding: 10
    font.pixelSize: 12
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? (control.primary ? theme.colors.background : theme.colors.foreground) : theme.colors.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 0
        color: control.primary ? theme.colors.accent : (control.down || control.hovered ? theme.colors.selection : theme.colors.lighter_background)
        border.width: 1
        border.color: control.activeFocus ? theme.colors.accent : theme.colors.muted
        opacity: control.enabled ? 1 : .5
    }
}
