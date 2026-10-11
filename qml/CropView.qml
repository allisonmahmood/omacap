import QtQuick
import QtQuick.Layouts
import OmaCap

// Crops the recording on its full source, so reopening it can widen an earlier crop. Drag
// the corners to resize or inside to move; nothing changes until Confirm crop.
ColumnLayout {
    id: root

    property string source: ""
    property real sourceAspect: 16 / 9
    property rect selection: Qt.rect(0, 0, 1, 1)

    readonly property color shade: theme.alpha(theme.scrim, 153)

    signal accepted(var crop)
    signal canceled()

    function begin(crop) {
        selection = Qt.rect(crop[0], crop[1], crop[2], crop[3]);
        forceActiveFocus();
    }

    function clamp(value, min, max) {
        return Math.max(min, Math.min(max, value));
    }

    // Deltas are measured against the full source, never the moving handle.
    function adjust(corner, initial, dx, dy) {
        if (corner < 0) {
            selection = Qt.rect(clamp(initial.x + dx, 0, 1 - initial.width), clamp(initial.y + dy, 0, 1 - initial.height), initial.width, initial.height);
            return ;
        }
        let left = initial.x, top = initial.y;
        let right = left + initial.width, bottom = top + initial.height;
        if (corner % 2 === 0)
            left = clamp(left + dx, 0, right - 0.05);
        else
            right = clamp(right + dx, left + 0.05, 1);
        if (corner < 2)
            top = clamp(top + dy, 0, bottom - 0.05);
        else
            bottom = clamp(bottom + dy, top + 0.05, 1);
        selection = Qt.rect(left, top, right - left, bottom - top);
    }

    spacing: 12

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

        Image {
            id: picture

            objectName: "cropImage"
            anchors.centerIn: parent
            width: Math.max(0, Math.min(parent.width - 32, (parent.height - 32) * root.sourceAspect))
            height: width / root.sourceAspect
            source: root.visible ? root.source : ""
            cache: false

            // Four shaded strips leave the selected pixels unobscured.
            Rectangle {
                width: picture.width
                height: selected.y
                color: root.shade
            }

            Rectangle {
                y: selected.y + selected.height
                width: picture.width
                height: picture.height - y
                color: root.shade
            }

            Rectangle {
                y: selected.y
                width: selected.x
                height: selected.height
                color: root.shade
            }

            Rectangle {
                x: selected.x + selected.width
                y: selected.y
                width: picture.width - x
                height: selected.height
                color: root.shade
            }

            Rectangle {
                id: selected

                objectName: "cropSelection"
                x: root.selection.x * picture.width
                y: root.selection.y * picture.height
                width: root.selection.width * picture.width
                height: root.selection.height * picture.height
                color: "transparent"

                ChromeBox {
                    anchors.fill: parent
                    anchors.margins: -1
                    radius: 0
                    borderWidth: 2
                    border: "selection"
                }

                MouseArea {
                    property point origin
                    property rect initial

                    anchors.fill: parent
                    cursorShape: Qt.SizeAllCursor
                    onPressed: (mouse) => {
                        origin = mapToItem(picture, mouse.x, mouse.y);
                        initial = root.selection;
                    }
                    onPositionChanged: (mouse) => {
                        if (pressed) {
                            const point = mapToItem(picture, mouse.x, mouse.y);
                            root.adjust(-1, initial, (point.x - origin.x) / picture.width, (point.y - origin.y) / picture.height);
                        }
                    }
                }

            }

            Repeater {
                model: 4

                MouseArea {
                    required property int index
                    property point origin
                    property rect initial

                    objectName: "cropCorner" + index
                    width: 28
                    height: 28
                    x: selected.x + (index % 2 ? selected.width : 0) - width / 2
                    y: selected.y + (index >= 2 ? selected.height : 0) - height / 2
                    cursorShape: index === 0 || index === 3 ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                    onPressed: (mouse) => {
                        origin = mapToItem(picture, mouse.x, mouse.y);
                        initial = root.selection;
                    }
                    onPositionChanged: (mouse) => {
                        if (pressed) {
                            const point = mapToItem(picture, mouse.x, mouse.y);
                            root.adjust(index, initial, (point.x - origin.x) / picture.width, (point.y - origin.y) / picture.height);
                        }
                    }

                    // Omasnap's crop handle.
                    ChromeBox {
                        anchors.centerIn: parent
                        width: 12
                        height: 12
                        radius: 3
                        fill: theme.foreground
                        borderWidth: 2
                        border: "selection"
                    }

                }

            }

        }

    }

    ChromePanel {
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: actions.implicitWidth + 16
        implicitHeight: actions.implicitHeight + 16

        Row {
            id: actions

            anchors.centerIn: parent
            spacing: 6

            ChromeChip {
                icon: "undo"
                text: "Reset"
                tooltip: "Select the whole recording"
                onClicked: root.selection = Qt.rect(0, 0, 1, 1)
            }

            ChromeChip {
                text: "Cancel"
                key: "Esc"
                onClicked: root.canceled()
            }

            ChromeChip {
                accent: true
                icon: "check"
                text: "Confirm crop"
                key: "Enter"
                onClicked: root.accepted([root.selection.x, root.selection.y, root.selection.width, root.selection.height])
            }

        }

    }

}
