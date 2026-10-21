import QtQuick

// The divider between toolbar groups. With the row's 2.5 px spacing on either side it makes
// Omasnap's 10 px group gap.
Item {
    implicitWidth: 7.5
    implicitHeight: 36

    Rectangle {
        x: parent.width / 2 - 0.5
        y: 6
        width: 1
        height: parent.height - 12
        color: theme.alpha(theme.foreground, 30)
    }

}
