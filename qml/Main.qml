import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: win
    visible: true
    title: "OmaCap"
    property bool recorderMode: ["recorder", "selecting", "countdown", "recording", "stopping"].includes(backend.phase)
    flags: recorderMode ? Qt.Dialog : Qt.Window
    width: recorderMode ? 520 : 1280
    height: recorderMode ? 540 : 850
    minimumWidth: recorderMode ? 520 : 1000
    minimumHeight: recorderMode ? 540 : 680
    maximumWidth: recorderMode ? 520 : 16777215
    maximumHeight: recorderMode ? 540 : 16777215
    color: theme.colors.background
    font.family: "monospace"
    font.pixelSize: 12
    palette.window: theme.colors.background
    palette.windowText: theme.colors.foreground
    palette.base: theme.colors.background
    palette.text: theme.colors.foreground
    palette.button: theme.colors.lighter_background
    palette.buttonText: theme.colors.foreground
    palette.highlight: theme.colors.accent
    palette.highlightedText: theme.colors.background
    palette.mid: theme.colors.muted
    property int selectedZoom: -1
    property bool choosingFocus: false
    property bool cropping: false
    property bool allowClose: false
    property bool closeAfterDiscard: false
    property real rangeA: 0
    property real rangeB: backend.duration
    property bool editing: backend.phase === "editor" || backend.phase === "exporting"
    function time(t) {
        return Math.floor(t / 60).toString().padStart(2, "0") + ":" + (t % 60).toFixed(1).padStart(4, "0");
    }
    function editedTime(t) {
        return backend.toEditedTime(t);
    }
    function sourceTime(t) {
        return backend.toSourceTime(t);
    }
    function zoomValue() {
        return selectedZoom >= 0 && selectedZoom < backend.edit.zooms.length ? backend.edit.zooms[selectedZoom] : null;
    }
    function changeZoom(a, b, x, y, amount) {
        backend.updateZoom(selectedZoom, a, b, x, y, amount);
    }
    onClosing: event => {
        if (allowClose)
            return;
        if (backend.phase === "exporting") {
            event.accepted = false;
            return;
        }
        if (!editing && backend.phase !== "recorder") {
            event.accepted = false;
            backend.stop();
            return;
        }
        if (backend.dirty) {
            event.accepted = false;
            closeAfterDiscard = true;
            discardDialog.open();
        } else
            backend.discard();
    }
    Shortcut {
        sequence: "Space"
        enabled: backend.phase === "editor" && !exportDialog.opened
        onActivated: backend.togglePlay()
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: backend.phase === "editor"
        onActivated: backend.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        enabled: backend.phase === "editor"
        onActivated: backend.redo()
    }
    Shortcut {
        sequence: "Left"
        enabled: backend.phase === "editor"
        onActivated: backend.seek(backend.position - 1 / 30)
    }
    Shortcut {
        sequence: "Right"
        enabled: backend.phase === "editor"
        onActivated: backend.seek(backend.position + 1 / 30)
    }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            win.cropping = false;
            win.choosingFocus = false;
        }
    }
    Connections {
        target: backend
        function onEditChanged() {
            win.rangeA = 0;
            win.rangeB = backend.duration;
            if (win.selectedZoom >= backend.edit.zooms.length)
                win.selectedZoom = -1;
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "O M A C A P"
                font.bold: true
                font.pixelSize: 14
                Layout.fillWidth: true
            }
            FlatButton {
                visible: win.editing
                text: "Undo"
                onClicked: backend.undo()
                enabled: backend.phase === "editor"
            }
            FlatButton {
                visible: win.editing
                text: "Redo"
                onClicked: backend.redo()
                enabled: backend.phase === "editor"
            }
            FlatButton {
                visible: win.editing
                text: "New recording"
                enabled: backend.phase === "editor"
                onClicked: {
                    if (backend.dirty) {
                        win.closeAfterDiscard = false;
                        discardDialog.open();
                    } else
                        backend.discard();
                }
            }
            FlatButton {
                visible: win.editing
                text: "Export"
                primary: true
                enabled: backend.phase === "editor"
                onClicked: exportDialog.open()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: theme.colors.muted
        }
        ColumnLayout {
            visible: !win.editing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14
            Label {
                text: backend.phase === "recorder" ? "Record a demo" : backend.phase === "selecting" ? "Select a window or display" : backend.phase === "countdown" ? "Starting in " + backend.countdown : backend.phase === "recording" ? "Recording" : backend.phase === "stopping" ? "Finishing recording" : "Preparing video"
                font.pixelSize: 20
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            ColumnLayout {
                visible: backend.phase === "recorder"
                Layout.fillWidth: true
                spacing: 12
                Label {
                    text: "Microphone"
                }
                ComboBox {
                    id: micChoice
                    Layout.fillWidth: true
                    model: [
                        {
                            label: "Off",
                            id: ""
                        }
                    ].concat(backend.microphones)
                    textRole: "label"
                    valueRole: "id"
                    Component.onCompleted: currentIndex = Math.min(backend.preference("micIndex", 1), count - 1)
                    onActivated: backend.remember("micIndex", currentIndex)
                }
                Label {
                    text: "Camera"
                }
                ComboBox {
                    id: cameraChoice
                    Layout.fillWidth: true
                    model: [
                        {
                            label: "Off",
                            id: ""
                        }
                    ].concat(backend.cameras)
                    textRole: "label"
                    valueRole: "id"
                    Component.onCompleted: currentIndex = Math.min(backend.preference("cameraIndex", 0), count - 1)
                    onActivated: backend.remember("cameraIndex", currentIndex)
                }
                CheckBox {
                    id: desktopAudio
                    text: "Record desktop audio"
                    checked: backend.preference("desktopAudio", false)
                    onToggled: backend.remember("desktopAudio", checked)
                }
                Label {
                    text: "Choose the source in the system picker.\nA three-second countdown follows."
                    color: theme.colors.dark_foreground
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                FlatButton {
                    text: "Select source and record"
                    primary: true
                    Layout.fillWidth: true
                    onClicked: backend.record(micChoice.currentValue || "", cameraChoice.currentValue || "", desktopAudio.checked)
                }
                RowLayout {
                    FlatButton {
                        text: "Open existing video"
                        onClicked: backend.importVideo()
                    }
                    FlatButton {
                        text: "Recover session"
                        visible: backend.recoverable
                        onClicked: backend.recover()
                    }
                }
            }
            Label {
                visible: backend.phase === "recording"
                text: win.time(recordClock.seconds)
                font.pixelSize: 40
            }
            Timer {
                id: recordClock
                property int seconds: 0
                interval: 1000
                repeat: true
                running: backend.phase === "recording"
                onRunningChanged: if (running)
                    seconds = 0
                onTriggered: seconds++
            }
            FlatButton {
                visible: ["selecting", "countdown", "recording"].includes(backend.phase)
                text: backend.phase === "recording" ? "Stop and edit" : "Cancel"
                primary: backend.phase === "recording"
                Layout.fillWidth: true
                onClicked: backend.stop()
            }
            Item {
                Layout.fillHeight: true
            }
        }
        RowLayout {
            visible: win.editing
            enabled: backend.phase === "editor"
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                RowLayout {
                    FlatButton {
                        text: win.cropping ? "Cancel crop" : "Crop"
                        onClicked: {
                            win.cropping = !win.cropping;
                            win.choosingFocus = false;
                        }
                    }
                    FlatButton {
                        text: "Reset crop"
                        onClicked: backend.setValue("crop", [0, 0, 1, 1])
                    }
                    Label {
                        text: win.cropping ? "Drag over the picture to crop" : "16:9 · styled preview"
                        color: theme.colors.dark_foreground
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                    }
                }
                Item {
                    id: preview
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Item {
                        id: fit
                        width: Math.min(parent.width, parent.height * 16 / 9)
                        height: width * 9 / 16
                        anchors.centerIn: parent
                        Composition {
                            id: composition
                            width: 1920
                            height: 1080
                            scale: fit.width / 1920
                            transformOrigin: Item.TopLeft
                            edit: backend.edit
                            screenSource: backend.screenSource
                            cameraSource: backend.cameraSource
                            sourceAspect: backend.aspect
                            zoom: win.choosingFocus || win.cropping ? 1 : backend.zoom
                            focusX: backend.focusX
                            focusY: backend.focusY
                        }
                        MouseArea {
                            id: picture
                            x: composition.frameX * fit.width / 1920
                            y: composition.frameY * fit.width / 1920
                            width: composition.frameW * fit.width / 1920
                            height: composition.frameH * fit.width / 1920
                            enabled: win.cropping || win.choosingFocus
                            cursorShape: Qt.CrossCursor
                            property real sx: 0
                            property real sy: 0
                            property real ex: 0
                            property real ey: 0
                            onPressed: mouse => {
                                sx = ex = mouse.x;
                                sy = ey = mouse.y;
                            }
                            onPositionChanged: mouse => {
                                if (pressed) {
                                    ex = Math.max(0, Math.min(width, mouse.x));
                                    ey = Math.max(0, Math.min(height, mouse.y));
                                }
                            }
                            onReleased: mouse => {
                                let c = backend.edit.crop;
                                if (win.cropping && Math.abs(ex - sx) > 8 && Math.abs(ey - sy) > 8) {
                                    backend.setValue("crop", [c[0] + Math.min(sx, ex) / width * c[2], c[1] + Math.min(sy, ey) / height * c[3], Math.abs(ex - sx) / width * c[2], Math.abs(ey - sy) / height * c[3]]);
                                    win.cropping = false;
                                } else if (win.choosingFocus) {
                                    let z = win.zoomValue();
                                    if (z)
                                        win.changeZoom(z.start, z.end, c[0] + Math.max(0, Math.min(1, mouse.x / width)) * c[2], c[1] + Math.max(0, Math.min(1, mouse.y / height)) * c[3], z.amount);
                                }
                            }
                            Rectangle {
                                visible: picture.pressed && win.cropping
                                x: Math.min(picture.sx, picture.ex)
                                y: Math.min(picture.sy, picture.ey)
                                width: Math.abs(picture.ex - picture.sx)
                                height: Math.abs(picture.ey - picture.sy)
                                color: "#304694e4"
                                border.width: 1
                                border.color: "white"
                            }
                            Rectangle {
                                visible: win.choosingFocus && win.zoomValue() !== null
                                property var zoomData: win.zoomValue()
                                x: zoomData ? (zoomData.x - backend.edit.crop[0]) / backend.edit.crop[2] * parent.width - 12 : 0
                                y: zoomData ? (zoomData.y - backend.edit.crop[1]) / backend.edit.crop[3] * parent.height - 12 : 0
                                width: 24
                                height: 24
                                radius: 12
                                color: "transparent"
                                border.color: "white"
                                border.width: 2
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 4
                                    height: 4
                                    color: "white"
                                }
                            }
                        }
                        MouseArea {
                            visible: backend.edit.camera && backend.cameraSource !== "" && !win.choosingFocus && !win.cropping
                            x: Math.min(1 - backend.edit.cameraSize, backend.edit.cameraX) * parent.width
                            y: Math.min(1 - backend.edit.cameraSize * 4 / 3, backend.edit.cameraY) * parent.height
                            width: backend.edit.cameraSize * parent.width
                            height: width * .75
                            cursorShape: Qt.SizeAllCursor
                            property real px
                            property real py
                            onPressed: mouse => {
                                px = mouse.x;
                                py = mouse.y;
                                backend.beginEdit();
                            }
                            onPositionChanged: mouse => {
                                if (pressed) {
                                    backend.setValue("cameraX", (x + mouse.x - px) / fit.width);
                                    backend.setValue("cameraY", (y + mouse.y - py) / fit.height);
                                }
                            }
                            onReleased: backend.endEdit()
                        }
                    }
                }
                RowLayout {
                    visible: win.selectedZoom >= 0
                    Layout.fillWidth: true
                    Label {
                        text: "Zoom focus"
                        Layout.fillWidth: true
                    }
                    FlatSlider {
                        id: amountSlider
                        from: 1
                        to: 4
                        value: win.zoomValue() ? win.zoomValue().amount : 1.8
                        onPressedChanged: pressed ? backend.beginEdit() : backend.endEdit()
                        onMoved: {
                            let z = win.zoomValue();
                            if (z)
                                win.changeZoom(z.start, z.end, z.x, z.y, value);
                        }
                    }
                    Label {
                        text: amountSlider.value.toFixed(1) + "×"
                    }
                    FlatButton {
                        text: win.choosingFocus ? "Confirm focus" : "Choose focus"
                        onClicked: win.choosingFocus = !win.choosingFocus
                    }
                    FlatButton {
                        text: "Delete zoom"
                        onClicked: {
                            backend.deleteZoom(win.selectedZoom);
                            win.selectedZoom = -1;
                            win.choosingFocus = false;
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillHeight: true
                width: 1
                color: theme.colors.muted
            }
            ScrollView {
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                clip: true
                ColumnLayout {
                    width: 250
                    spacing: 10
                    Label {
                        text: "APPEARANCE"
                        font.bold: true
                    }
                    RowLayout {
                        FlatButton {
                            text: "Wallpaper"
                            onClicked: backend.useWallpaper()
                        }
                        FlatButton {
                            text: "Image…"
                            onClicked: backend.chooseBackground()
                        }
                        FlatButton {
                            text: "Color"
                            onClicked: backend.chooseColor()
                        }
                    }
                    Setting {
                        Layout.fillWidth: true
                        label: "Padding"
                        fieldName: "padding"
                        to: .25
                        current: backend.edit.padding || 0
                        suffix: "%"
                    }
                    Setting {
                        Layout.fillWidth: true
                        label: "Corners"
                        fieldName: "corners"
                        to: .08
                        current: backend.edit.corners || 0
                        suffix: "%"
                    }
                    Setting {
                        Layout.fillWidth: true
                        label: "Shadow"
                        fieldName: "shadow"
                        current: backend.edit.shadow || 0
                        suffix: "%"
                    }
                    Label {
                        text: "CAMERA"
                        font.bold: true
                    }
                    CheckBox {
                        text: "Show camera"
                        checked: backend.edit.camera ?? true
                        enabled: backend.cameraSource !== ""
                        onToggled: backend.setValue("camera", checked)
                    }
                    Setting {
                        Layout.fillWidth: true
                        label: "Size"
                        fieldName: "cameraSize"
                        from: .08
                        to: .4
                        current: backend.edit.cameraSize || .18
                        suffix: "%"
                    }
                    Label {
                        text: "Drag the camera in the preview."
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        color: theme.colors.dark_foreground
                    }
                    RowLayout {
                        Repeater {
                            model: ["↖", "↗", "↙", "↘"]
                            FlatButton {
                                required property int index
                                required property string modelData
                                text: modelData
                                onClicked: {
                                    backend.beginEdit();
                                    backend.setValue("cameraX", index % 2 === 0 ? .03 : .97 - backend.edit.cameraSize);
                                    backend.setValue("cameraY", index < 2 ? .04 : .96 - backend.edit.cameraSize * 4 / 3);
                                    backend.endEdit();
                                }
                            }
                        }
                    }
                    Label {
                        text: "AUDIO"
                        font.bold: true
                    }
                    CheckBox {
                        text: "Microphone / source audio"
                        checked: backend.edit.mic ?? true
                        onToggled: backend.setValue("mic", checked)
                    }
                    CheckBox {
                        text: "Desktop audio"
                        checked: backend.edit.desktop ?? true
                        onToggled: backend.setValue("desktop", checked)
                    }
                }
            }
        }
        ColumnLayout {
            visible: win.editing
            enabled: backend.phase === "editor"
            Layout.fillWidth: true
            spacing: 4
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: theme.colors.muted
            }
            RowLayout {
                Layout.fillWidth: true
                FlatButton {
                    text: backend.playing ? "Pause" : "Play"
                    onClicked: backend.togglePlay()
                }
                Label {
                    text: win.time(backend.position) + " / " + win.time(backend.duration)
                    Layout.fillWidth: true
                }
                FlatButton {
                    text: "Add zoom"
                    onClicked: {
                        win.selectedZoom = backend.addZoom(backend.position);
                        win.choosingFocus = win.selectedZoom >= 0;
                        win.cropping = false;
                    }
                }
                FlatButton {
                    text: "Keep selection"
                    onClicked: backend.trim(win.rangeA, win.rangeB)
                }
                FlatButton {
                    text: "Delete selection"
                    enabled: win.rangeB - win.rangeA < backend.duration - .05
                    onClicked: backend.removeRange(win.rangeA, win.rangeB)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Selection"
                    Layout.preferredWidth: 80
                }
                RangeSlider {
                    id: range
                    implicitHeight: 28
                    first.handle: Rectangle {
                        x: range.leftPadding + range.first.visualPosition * (range.availableWidth - width)
                        y: range.topPadding + range.availableHeight / 2 - height / 2
                        width: 8
                        height: 20
                        color: theme.colors.accent
                    }
                    second.handle: Rectangle {
                        x: range.leftPadding + range.second.visualPosition * (range.availableWidth - width)
                        y: range.topPadding + range.availableHeight / 2 - height / 2
                        width: 8
                        height: 20
                        color: theme.colors.accent
                    }
                    background: Rectangle {
                        x: range.leftPadding
                        y: range.topPadding + range.availableHeight / 2 - 1
                        width: range.availableWidth
                        height: 2
                        color: theme.colors.muted
                        Rectangle {
                            x: range.first.visualPosition * parent.width
                            width: (range.second.visualPosition - range.first.visualPosition) * parent.width
                            height: parent.height
                            color: theme.colors.accent
                        }
                    }
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(.1, backend.duration)
                    first.value: win.rangeA
                    second.value: win.rangeB
                    first.onMoved: win.rangeA = first.value
                    second.onMoved: win.rangeB = second.value
                }
                Label {
                    text: win.time(win.rangeA) + " – " + win.time(win.rangeB)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Recording"
                    Layout.preferredWidth: 80
                }
                Rectangle {
                    id: track
                    Layout.fillWidth: true
                    height: 36
                    color: theme.colors.selection
                    border.color: theme.colors.muted
                    Repeater {
                        model: backend.edit.spans || []
                        Rectangle {
                            required property var modelData
                            x: win.editedTime(modelData[0]) / Math.max(.1, backend.duration) * track.width
                            width: (modelData[1] - modelData[0]) / Math.max(.1, backend.duration) * track.width
                            height: parent.height
                            color: "transparent"
                            border.color: theme.colors.muted
                            Label {
                                anchors.centerIn: parent
                                text: win.time(modelData[0])
                                color: theme.colors.dark_foreground
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: mouse => backend.seek(mouse.x / width * backend.duration)
                        onPositionChanged: mouse => {
                            if (pressed)
                                backend.seek(mouse.x / width * backend.duration);
                        }
                    }
                    Rectangle {
                        x: backend.position / Math.max(.1, backend.duration) * parent.width
                        width: 2
                        height: parent.height
                        color: theme.colors.accent
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Zooms"
                    Layout.preferredWidth: 80
                }
                Rectangle {
                    id: zoomTrack
                    Layout.fillWidth: true
                    height: 28
                    color: theme.colors.background
                    border.color: theme.colors.muted
                    Repeater {
                        model: (backend.edit.zooms || []).length
                        Rectangle {
                            id: block
                            objectName: "zoomBlock" + index
                            property var zoomData: backend.edit.zooms[index]
                            required property int index
                            property real start: win.editedTime(zoomData.start)
                            property real end: win.editedTime(zoomData.end)
                            x: start / Math.max(.1, backend.duration) * zoomTrack.width
                            width: Math.max(2, (end - start) / Math.max(.1, backend.duration) * zoomTrack.width)
                            height: parent.height
                            color: win.selectedZoom === index ? theme.colors.accent : theme.colors.selection
                            border.color: theme.colors.accent
                            visible: end > start
                            Label {
                                anchors.centerIn: parent
                                text: block.zoomData.amount.toFixed(1) + "×"
                                color: win.selectedZoom === block.index ? theme.colors.background : theme.colors.foreground
                            }
                            MouseArea {
                                anchors.fill: parent
                                objectName: "zoomDrag" + block.index
                                cursorShape: Qt.SizeAllCursor
                                property real pressX
                                property real originalStart
                                property real originalEnd
                                onPressed: mouse => {
                                    win.selectedZoom = block.index;
                                    pressX = mapToItem(zoomTrack, mouse.x, 0).x;
                                    originalStart = block.start;
                                    originalEnd = block.end;
                                    backend.beginEdit();
                                }
                                onPositionChanged: mouse => {
                                    if (pressed) {
                                        let delta = (mapToItem(zoomTrack, mouse.x, 0).x - pressX) / zoomTrack.width * backend.duration;
                                        let a = Math.max(0, Math.min(backend.duration - (originalEnd - originalStart), originalStart + delta));
                                        let z = block.zoomData;
                                        win.changeZoom(win.sourceTime(a), win.sourceTime(a + originalEnd - originalStart), z.x, z.y, z.amount);
                                    }
                                }
                                onReleased: backend.endEdit()
                            }
                            Repeater {
                                model: 2
                                Rectangle {
                                    required property int index
                                    width: 7
                                    height: parent.height
                                    x: index === 0 ? 0 : parent.width - width
                                    color: theme.colors.foreground
                                    opacity: .7
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.SizeHorCursor
                                        onPressed: {
                                            win.selectedZoom = block.index;
                                            backend.beginEdit();
                                        }
                                        onPositionChanged: mouse => {
                                            if (pressed) {
                                                let t = win.sourceTime(Math.max(0, Math.min(1, mapToItem(zoomTrack, mouse.x, 0).x / zoomTrack.width)) * backend.duration);
                                                let z = block.zoomData;
                                                win.changeZoom(parent.index === 0 ? t : z.start, parent.index === 1 ? t : z.end, z.x, z.y, z.amount);
                                            }
                                        }
                                        onReleased: backend.endEdit()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            visible: backend.phase === "exporting"
            Layout.fillWidth: true
            Label {
                text: "Exporting"
            }
            ProgressBar {
                Layout.fillWidth: true
                value: backend.progress
            }
            FlatButton {
                text: "Cancel"
                onClicked: backend.cancelExport()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: backend.message !== "" || backend.lastExport !== ""
            Label {
                text: backend.message
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: theme.colors.dark_foreground
            }
            FlatButton {
                text: "Open file"
                visible: backend.lastExport !== ""
                onClicked: backend.openExport()
            }
            FlatButton {
                text: "Show in folder"
                visible: backend.lastExport !== ""
                onClicked: backend.revealExport()
            }
        }
    }
    Dialog {
        id: discardDialog
        anchors.centerIn: parent
        modal: true
        title: "Discard this session?"
        width: 420
        contentItem: Label {
            text: "This session has unexported changes. Discard removes its temporary media."
            wrapMode: Text.Wrap
        }
        footer: RowLayout {
            FlatButton {
                text: "Keep editing"
                onClicked: discardDialog.close()
            }
            FlatButton {
                text: "Discard"
                onClicked: {
                    discardDialog.close();
                    backend.discard();
                    if (win.closeAfterDiscard) {
                        win.allowClose = true;
                        win.close();
                    }
                }
            }
        }
    }
    Dialog {
        id: exportDialog
        anchors.centerIn: parent
        modal: true
        title: "Export locally"
        width: 420
        contentItem: ColumnLayout {
            spacing: 12
            ComboBox {
                id: format
                Layout.fillWidth: true
                model: ["MP4", "GIF"]
                Component.onCompleted: currentIndex = backend.preference("export/format", 0)
                onActivated: backend.remember("export/format", currentIndex)
            }
            Label {
                text: "Output width · 16:9"
            }
            ComboBox {
                id: outputWidth
                Layout.fillWidth: true
                model: format.currentIndex === 0 ? ["1920", "1280", "2560", "3840"] : ["640", "960", "1280", "480"]
                onModelChanged: currentIndex = backend.preference("export/widthIndex" + format.currentIndex, 0)
                Component.onCompleted: currentIndex = backend.preference("export/widthIndex" + format.currentIndex, 0)
                onActivated: backend.remember("export/widthIndex" + format.currentIndex, currentIndex)
            }
            Label {
                text: "Frame rate"
            }
            ComboBox {
                id: outputFps
                Layout.fillWidth: true
                model: format.currentIndex === 0 ? ["30", "60"] : ["15", "10", "20", "30"]
                onModelChanged: currentIndex = backend.preference("export/fpsIndex" + format.currentIndex, 0)
                Component.onCompleted: currentIndex = backend.preference("export/fpsIndex" + format.currentIndex, 0)
                onActivated: backend.remember("export/fpsIndex" + format.currentIndex, currentIndex)
            }
            Label {
                text: format.currentIndex === 0 ? "Quality" : "Duration in seconds"
            }
            ComboBox {
                id: quality
                visible: format.currentIndex === 0
                Layout.fillWidth: true
                model: ["High", "Balanced", "Smaller file"]
                Component.onCompleted: currentIndex = backend.preference("export/quality", 0)
                onActivated: backend.remember("export/quality", currentIndex)
            }
            SpinBox {
                id: gifLength
                visible: format.currentIndex === 1
                from: 1
                to: Math.max(1, Math.ceil(backend.duration))
                value: Math.min(backend.preference("export/gifSeconds", 10), to)
                onValueModified: backend.remember("export/gifSeconds", value)
                editable: true
            }
            Label {
                text: format.currentIndex === 1 ? "Silent loop. Rough estimate: " + (parseInt(outputWidth.currentText) * parseInt(outputWidth.currentText) * 9 / 16 * parseInt(outputFps.currentText) * gifLength.value * .03 / 1000000).toFixed(1) + "–" + (parseInt(outputWidth.currentText) * parseInt(outputWidth.currentText) * 9 / 16 * parseInt(outputFps.currentText) * gifLength.value * .2 / 1000000).toFixed(1) + " MB. Motion and detail affect the final size." : "H.264 video + AAC audio"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: theme.colors.dark_foreground
            }
        }
        footer: RowLayout {
            FlatButton {
                text: "Cancel"
                onClicked: exportDialog.close()
            }
            FlatButton {
                text: "Choose file and export"
                primary: true
                onClicked: {
                    exportDialog.close();
                    backend.exportVideo(format.currentIndex === 1, parseInt(outputWidth.currentText), parseInt(outputFps.currentText), [18, 23, 28][quality.currentIndex], gifLength.value);
                }
            }
        }
    }
}
