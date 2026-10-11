import QtQuick

// Omasnap's hotkey legend: a faint, backgroundless column of `key  action` rows along the
// bottom left, growing upward. Draw it early; chrome painted later simply covers it.
Column {
    id: legend

    // [key, action] pairs; the first is the bottom row.
    property var entries: []
    readonly property real keyWidth: {
        let widest = 0;
        for (const entry of entries)
            widest = Math.max(widest, metrics.advanceWidth(entry[0]));
        return widest;
    }

    spacing: 0

    FontMetrics {
        id: metrics

        font: theme.font(11)
    }

    Repeater {
        model: legend.entries.slice().reverse()

        Row {
            required property var modelData

            height: 17
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
