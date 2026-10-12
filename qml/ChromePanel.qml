import QtQuick
import OmaCap

// A floating panel on the overlay: popovers, the timeline dock, dialogs and the card. It
// takes the clicks that land on it, so they never reach the preview underneath.
ChromeBox {
    radius: 11
    fill: theme.surface
    border: "panel"

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

}
