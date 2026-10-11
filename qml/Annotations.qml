import QtQuick
import QtQuick.Shapes
import "annotations.js" as Geometry

// Draws screenshot annotations in source-image pixels. Position and scale this item to
// map that space onto the frame.
Item {
    id: drawing

    property var annotations: []
    // Not drawn while the editor shows a live copy of it.
    property string hiddenId: ""

    // The area annotation `index` covers, in source pixels.
    function boxOf(index) {
        const mark = marks.itemAt(index);
        return mark ? mark.box : null;
    }

    Repeater {
        id: marks

        model: drawing.annotations

        delegate: Item {
            id: mark

            required property var modelData
            required property int index
            readonly property var a: modelData
            readonly property color tone: a.color
            // A live copy carries the number it will get once committed.
            readonly property int number: a.number ?? Geometry.stepNumber(drawing.annotations, index)
            readonly property bool fillable: a.kind === "box" || a.kind === "ellipse"
            readonly property rect box: a.kind === "text" ? Qt.rect(label.x, label.y, label.width, label.height) : a.kind === "step" ? Qt.rect(step.x, step.y, step.width, step.height) : Geometry.bounds(a)

            visible: !a.id || a.id !== drawing.hiddenId

            Shape {
                visible: Geometry.isVector(mark.a)
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: mark.a.kind === "marker" ? Qt.rgba(mark.tone.r, mark.tone.g, mark.tone.b, 0.4) : mark.tone
                    strokeWidth: mark.a.size
                    fillColor: !mark.fillable || mark.a.fill === "outline" ? "transparent" : mark.a.fill === "solid" ? mark.tone : Qt.rgba(mark.tone.r, mark.tone.g, mark.tone.b, 0.25)
                    capStyle: mark.a.kind === "marker" ? ShapePath.FlatCap : ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin

                    PathSvg {
                        path: Geometry.path(mark.a)
                    }

                }

                ShapePath {
                    strokeColor: "transparent"
                    strokeWidth: 0
                    fillColor: mark.tone

                    PathSvg {
                        path: Geometry.head(mark.a)
                    }

                }

            }

            // Redactions are opaque so nothing of the screenshot survives under them.
            Rectangle {
                readonly property rect area: mark.a.kind === "redact" ? Geometry.span(mark.a) : Qt.rect(0, 0, 0, 0)

                visible: mark.a.kind === "redact"
                x: area.x
                y: area.y
                width: area.width
                height: area.height
                color: "#000000"
            }

            Rectangle {
                id: step

                visible: mark.a.kind === "step"
                width: mark.a.size
                height: mark.a.size
                x: mark.a.points[0][0] - width / 2
                y: mark.a.points[0][1] - height / 2
                radius: width / 2
                color: mark.tone
                border.color: "#ffffff"
                border.width: mark.a.size * 0.07

                Text {
                    anchors.centerIn: parent
                    text: mark.number
                    color: "#ffffff"
                    font.pixelSize: mark.a.size * 0.55
                    font.bold: true
                }

            }

            // Text sits on a pill of its colour, or plain with a dark outline.
            Rectangle {
                id: label

                readonly property real padX: mark.a.size * 0.4
                readonly property real padY: mark.a.size * 0.18

                visible: mark.a.kind === "text"
                x: mark.a.points[0][0]
                y: mark.a.points[0][1]
                width: words.implicitWidth + 2 * padX
                height: words.implicitHeight + 2 * padY
                radius: Math.min(height / 2, mark.a.size * 0.7)
                color: mark.a.pill ? mark.tone : "transparent"

                Text {
                    id: words

                    x: label.padX
                    y: label.padY
                    text: mark.a.text ?? ""
                    font.pixelSize: mark.a.size
                    font.bold: true
                    color: !mark.a.pill ? mark.tone : 0.299 * mark.tone.r + 0.587 * mark.tone.g + 0.114 * mark.tone.b > 0.6 ? "#1d1b16" : "#ffffff"
                    style: mark.a.pill ? Text.Normal : Text.Outline
                    styleColor: "#b0000000"
                }

            }

        }

    }

}
