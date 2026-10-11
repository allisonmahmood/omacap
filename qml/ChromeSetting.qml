import QtQuick
import QtQuick.Layouts

// One look setting as a popover row: name, slider and value. A drag is one undo step.
RowLayout {
    id: setting

    property string label
    // The edit field the slider sets, as Backend::setValue takes it.
    property string fieldName
    property real from: 0
    property real to: 1
    property real current: 0
    property real stepSize: 0
    property string suffix: "%"
    readonly property alias slider: slider

    spacing: 10

    Text {
        text: setting.label
        font: theme.font(12)
        color: theme.alpha(theme.foreground, 180)
        Layout.preferredWidth: 74
    }

    ChromeSlider {
        id: slider

        Layout.fillWidth: true
        from: setting.from
        to: setting.to
        value: setting.current
        stepSize: setting.stepSize
        onPressedChanged: pressed ? backend.beginEdit() : backend.endEdit()
        onMoved: backend.setValue(setting.fieldName, value)
    }

    Text {
        text: (setting.current * 100).toFixed(0) + setting.suffix
        font: theme.mono(11)
        color: theme.foreground
        horizontalAlignment: Text.AlignRight
        Layout.preferredWidth: 34
    }

}
