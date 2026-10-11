import QtQuick
import OmaCap

// Omasnap's 36 px toolbar button: rest, hover, selected and primary (accent) faces, a
// Lucide icon or a colour dot, and an instant tooltip.
Item {
    id: button

    property string icon
    property string tooltip
    property bool selected: false
    property bool primary: false
    // A colour swatch in place of the icon, as Omasnap's annotation colour button.
    property color dot: "transparent"
    property real radius: 8
    readonly property bool hovered: mouse.containsMouse

    signal clicked()

    implicitWidth: primary ? 40 : 36
    implicitHeight: 36
    opacity: enabled ? 1 : 0.4

    ChromeBox {
        anchors.fill: parent
        radius: button.radius
        fill: button.primary ? theme.accent : button.selected ? theme.buttonSelected : button.hovered ? theme.buttonHover : theme.button
        border: button.selected ? "buttonSelected" : button.hovered ? "buttonHover" : "button"
    }

    ChromeIcon {
        anchors.fill: parent
        visible: button.icon !== ""
        name: button.icon
        size: button.height < 36 ? 16 : 19
        color: button.primary ? theme.accentText : button.selected ? theme.buttonSelectedText : button.hovered ? theme.buttonHoverText : theme.buttonText
    }

    Rectangle {
        visible: button.dot.a > 0
        anchors.centerIn: parent
        width: 14
        height: 14
        radius: 7
        color: button.dot
        border.width: button.selected ? 2 : 1
        border.color: button.selected ? theme.foreground : theme.alpha(theme.foreground, 80)
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        hoverEnabled: true
        onClicked: button.clicked()
    }

    ChromeTooltip {
        text: button.tooltip
        shown: button.hovered && !mouse.pressed
    }

}
