import QtQuick
import QtQuick.Layouts

// A named row in a popover; its content follows the 74 px name column.
RowLayout {
    property alias label: name.text

    Layout.fillWidth: true
    spacing: 10

    Text {
        id: name

        font: theme.font(12)
        color: theme.alpha(theme.foreground, 180)
        Layout.preferredWidth: 74
    }

}
