import QtQuick

// Omasnap's hotkey legend: a faint, backgroundless column of `key  action` rows along the
// bottom left, growing upward. Rows show from the first until one would reach `ceiling` or
// come within 12 px of `avoid`, so it never runs into the chrome around it.
Column {
    id: legend

    // [key, action] pairs; the first is the bottom row.
    property var entries: []
    // Where the bottom row ends, how high it may grow, and a rect to its right to keep clear
    // of, all in the parent's coordinates.
    property real floor
    property real ceiling: 0
    property rect avoid
    readonly property real rowHeight: 17
    readonly property real keyWidth: widest(0)
    readonly property int rows: {
        let shown = 0;
        for (; shown < entries.length; ++shown) {
            const top = floor - (shown + 1) * rowHeight;
            const below = top >= avoid.y + avoid.height + 12;
            const beside = x + keyWidth + 10 + metrics.advanceWidth(entries[shown][1]) + 12 <= avoid.x;
            if (top < ceiling || !(below || beside))
                break;
        }
        return shown;
    }

    function widest(column) {
        let widest = 0;
        for (const entry of entries)
            widest = Math.max(widest, metrics.advanceWidth(entry[column]));
        return widest;
    }

    // As wide as every row, however many show.
    width: keyWidth + 10 + widest(1)
    y: floor - height
    spacing: 0

    FontMetrics {
        id: metrics

        font: theme.font(11)
    }

    Repeater {
        model: legend.entries.slice(0, legend.rows).reverse()

        Row {
            required property var modelData

            height: legend.rowHeight
            spacing: 10

            Text {
                width: legend.keyWidth
                height: 15
                verticalAlignment: Text.AlignVCenter
                text: parent.modelData[0]
                font: theme.font(11)
                color: theme.alpha(theme.muted, 165)
            }

            Text {
                height: 15
                verticalAlignment: Text.AlignVCenter
                text: parent.modelData[1]
                font: theme.font(11)
                color: theme.alpha(theme.foreground, 130)
            }

        }

    }

}
