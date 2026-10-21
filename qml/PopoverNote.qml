import QtQuick
import QtQuick.Layouts

// A line of help in a popover.
Text {
    Layout.fillWidth: true
    font: theme.font(11)
    color: theme.alpha(theme.foreground, 150)
    wrapMode: Text.Wrap
}
