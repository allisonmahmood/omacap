import QtQuick
import QtQuick.Controls

Slider {
    id: control
    implicitWidth: 150
    implicitHeight: 26
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - 1
        width: control.availableWidth
        height: 2
        color: theme.colors.muted
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            color: theme.colors.accent
        }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 8
        height: 18
        color: control.pressed ? theme.colors.foreground : theme.colors.accent
    }
}
