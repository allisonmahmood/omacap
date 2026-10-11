import QtQuick
import QtQuick.Controls

// A slider in the chrome's style: a 4 px track filled with the accent, and a round handle.
// It never takes the keyboard, so the arrow keys keep stepping through the recording.
Slider {
    id: control

    focusPolicy: Qt.NoFocus
    implicitWidth: 150
    implicitHeight: 22
    padding: 0
    leftPadding: 7
    rightPadding: 7

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: theme.alpha(theme.foreground, 41)

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: theme.accent
        }

    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * control.availableWidth - width / 2
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 14
        height: 14
        radius: 7
        color: control.pressed ? theme.accent : theme.foreground
        border.width: 1
        border.color: theme.alpha(theme.surface, 160)
    }

}
