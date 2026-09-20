import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: field
    property string label
    property string fieldName
    property real from: 0
    property real to: 1
    property real current: 0
    property real stepSize: 0
    property string suffix: ""
    spacing: 2
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: field.label
            Layout.fillWidth: true
        }
        Label {
            text: (field.current * 100).toFixed(0) + field.suffix
            color: theme.colors.dark_foreground
        }
    }
    FlatSlider {
        Layout.fillWidth: true
        from: field.from
        to: field.to
        value: field.current
        stepSize: field.stepSize
        onPressedChanged: pressed ? backend.beginEdit() : backend.endEdit()
        onMoved: backend.setValue(field.fieldName, value)
    }
}
