import QtQuick
import OmaCap

// A labelled action: icon, label and its key, as on the timeline dock and the card.
// `accent` fills it as the primary action; `solid` sits on an image, as the card's.
Item {
    id: chip

    property string icon
    property string text
    property string key
    property string tooltip
    property bool accent: false
    property bool solid: false
    property bool selected: false
    readonly property bool hovered: mouse.containsMouse
    readonly property color ink: accent ? theme.accentText : selected ? theme.buttonSelectedText : hovered ? theme.buttonHoverText : theme.buttonText

    signal clicked()

    implicitWidth: row.implicitWidth + 20
    implicitHeight: 28
    opacity: enabled ? 1 : 0.4

    ChromeBox {
        anchors.fill: parent
        radius: 7
        fill: chip.accent ? theme.accent : chip.solid ? theme.surface : chip.selected ? theme.buttonSelected : chip.hovered ? theme.buttonHover : theme.button
        border: chip.solid ? "active" : chip.selected ? "buttonSelected" : chip.hovered ? "buttonHover" : "button"
    }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: 6

        ChromeIcon {
            visible: chip.icon !== ""
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            size: 15
            name: chip.icon
            color: chip.ink
        }

        Text {
            visible: chip.text !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            font: theme.font(12, chip.accent)
            color: chip.ink
        }

        Text {
            visible: chip.key !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: chip.key
            font: theme.font(10)
            color: theme.alpha(chip.ink, 128)
        }

    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        hoverEnabled: true
        onClicked: chip.clicked()
    }

    ChromeTooltip {
        text: chip.tooltip
        shown: chip.hovered && !mouse.pressed
    }

}
