import QtQuick
import QtQuick.Layouts

// A small panel hanging below its toolbar button, as Omasnap's submenus: a title, the key
// that opens it, and its rows.
ChromePanel {
    id: popover

    property string title
    property string key
    // The toolbar button it belongs to.
    property Item anchorItem
    default property alias content: rows.data

    function place(shown) {
        // `shown` makes the binding re-evaluate where the button is each time it opens.
        if (!shown || !anchorItem || !parent)
            return Qt.point(0, 0);
        const button = anchorItem.mapToItem(parent, 0, 0);
        return Qt.point(Math.max(14, Math.min(button.x + anchorItem.width / 2 - width / 2, parent.width - width - 14)), button.y + anchorItem.height + 8);
    }

    width: Math.max(280, rows.implicitWidth + 24)
    height: rows.implicitHeight + 24
    x: place(visible).x
    y: place(visible).y

    ColumnLayout {
        id: rows

        x: 12
        y: 12
        width: popover.width - 24
        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: popover.title
                font: theme.font(12, true)
                color: theme.foreground
                Layout.fillWidth: true
            }

            Text {
                visible: popover.key !== ""
                text: popover.key
                font: theme.font(11)
                color: theme.alpha(theme.foreground, 140)
            }

        }

    }

}
