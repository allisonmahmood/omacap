import QtQuick
import OmaCap

// Omasnap's instant tooltip for its parent: a pill centred above it, or below when there is
// no room above, kept inside the window.
Item {
    id: tip

    property string text
    property bool shown: false
    readonly property Item anchor: parent

    function place(shown) {
        // `shown` makes the binding re-evaluate where the anchor is each time it appears.
        if (!shown || !anchor || !Window.window)
            return Qt.point(0, 0);
        const origin = anchor.mapToItem(null, 0, 0);
        const left = Math.max(8, Math.min(origin.x + anchor.width / 2 - width / 2, Window.width - width - 8));
        const above = origin.y - height - 7;
        return Qt.point(left - origin.x, (above < 6 ? origin.y + anchor.height + 7 : above) - origin.y);
    }

    visible: shown && text !== ""
    z: 1000
    width: label.implicitWidth + 20
    height: 28
    x: place(shown).x
    y: place(shown).y

    ChromeBox {
        anchors.fill: parent
        radius: 7
        fill: theme.tooltipBackground
        border: "tooltip"
    }

    Text {
        id: label

        anchors.centerIn: parent
        text: tip.text
        font: theme.font(12)
        color: theme.tooltipText
    }

}
