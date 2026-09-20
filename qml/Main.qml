import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: win

    property bool recorderMode: ["recorder", "selecting", "countdown", "recording", "stopping"].includes(backend.phase)
    property int selectedZoom: timeline.selectedZoom
    property bool choosingFocus: false
    property real draftFocusX: 0.5
    property real draftFocusY: 0.5
    property bool cropping: false
    property bool allowClose: false
    property bool closeAfterDiscard: false
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

    function beginFocus() {
        if (!zoomValue())
            return ;

        timeline.selectZoom(selectedZoom, true);
        draftFocusX = zoomValue().x;
        draftFocusY = zoomValue().y;
        choosingFocus = true;
        cropping = false;
    }

    function finishFocus(confirm) {
        if (!choosingFocus)
            return ;

        const z = zoomValue();
        choosingFocus = false;
        if (confirm && z)
            changeZoom(z.start, z.end, draftFocusX, draftFocusY, z.amount);

    }

    visible: true
    title: "OmaCap"
    flags: recorderMode ? Qt.Dialog : Qt.Window
    width: recorderMode ? 520 : 1280
    height: recorderMode ? 540 : 850
    minimumWidth: recorderMode ? 520 : 1000
    minimumHeight: recorderMode ? 540 : 680
    maximumWidth: recorderMode ? 520 : 1.67772e+07
    maximumHeight: recorderMode ? 540 : 1.67772e+07
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
    onSelectedZoomChanged: {
        if (choosingFocus)
            finishFocus(false);

        if (appearanceScroll.contentItem)
            appearanceScroll.contentItem.contentY = 0;

    }
    onClosing: (event) => {
        if (allowClose)
            return ;

        if (backend.phase === "exporting") {
            event.accepted = false;
            return ;
        }
        if (!editing && backend.phase !== "recorder") {
            event.accepted = false;
            backend.stop();
            return ;
        }
        if (backend.dirty) {
            event.accepted = false;
            closeAfterDiscard = true;
            discardDialog.open();
        } else {
            backend.discard();
        }
    }

    Connections {
        function onChanged() {
            if (backend.phase !== "editor")
                win.finishFocus(false);

        }

        target: backend
    }

    Shortcut {
        sequence: "Space"
        enabled: backend.phase === "editor" && !exportDialog.opened && !discardDialog.opened && !win.choosingFocus
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
        enabled: backend.phase === "editor" && !win.choosingFocus && !exportDialog.opened && !discardDialog.opened
        onActivated: backend.seek(backend.position - 1 / 30)
    }

    Shortcut {
        sequence: "Right"
        enabled: backend.phase === "editor" && !win.choosingFocus && !exportDialog.opened && !discardDialog.opened
        onActivated: backend.seek(backend.position + 1 / 30)
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            win.cropping = false;
            win.finishFocus(false);
            timeline.cutting = false;
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
                    } else {
                        backend.discard();
                    }
                }
            }

            FlatButton {
                visible: win.editing
                text: "Export"
                primary: true
                enabled: backend.phase === "editor" && backend.duration > 0 && !win.choosingFocus
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
                    model: [{
                        "label": "Off",
                        "id": ""
                    }].concat(backend.microphones)
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
                    model: [{
                        "label": "Off",
                        "id": ""
                    }].concat(backend.cameras)
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
                onRunningChanged: {
                    if (running)
                        seconds = 0;

                }
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
                            win.finishFocus(false);
                            timeline.selection = "";
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

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                win.finishFocus(false);
                                timeline.selection = "";
                            }
                        }

                        Composition {
                            id: composition

                            objectName: "composition"
                            width: 1920
                            height: 1080
                            scale: fit.width / 1920
                            transformOrigin: Item.TopLeft
                            edit: backend.edit
                            screenSource: backend.duration > 0 ? backend.screenSource : ""
                            cameraSource: backend.duration > 0 ? backend.cameraSource : ""
                            sourceAspect: backend.aspect
                            zoom: win.cropping ? 1 : backend.zoom
                            focusX: win.choosingFocus ? win.draftFocusX : backend.focusX
                            focusY: win.choosingFocus ? win.draftFocusY : backend.focusY
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: backend.duration === 0
                            text: "No clips left. Undo to restore them."
                            padding: 16

                            background: Rectangle {
                                color: theme.colors.background
                            }

                        }

                        MouseArea {
                            id: picture

                            property real sx: 0
                            property real sy: 0
                            property real ex: 0
                            property real ey: 0

                            x: composition.frameX * fit.width / 1920
                            y: composition.frameY * fit.width / 1920
                            width: composition.frameW * fit.width / 1920
                            height: composition.frameH * fit.width / 1920
                            enabled: win.cropping
                            cursorShape: Qt.CrossCursor
                            onPressed: (mouse) => {
                                sx = ex = mouse.x;
                                sy = ey = mouse.y;
                            }
                            onPositionChanged: (mouse) => {
                                if (pressed) {
                                    ex = Math.max(0, Math.min(width, mouse.x));
                                    ey = Math.max(0, Math.min(height, mouse.y));
                                }
                            }
                            onReleased: (mouse) => {
                                let c = backend.edit.crop;
                                if (win.cropping && Math.abs(ex - sx) > 8 && Math.abs(ey - sy) > 8) {
                                    backend.setValue("crop", [c[0] + Math.min(sx, ex) / width * c[2], c[1] + Math.min(sy, ey) / height * c[3], Math.abs(ex - sx) / width * c[2], Math.abs(ey - sy) / height * c[3]]);
                                    win.cropping = false;
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

                        }

                        MouseArea {
                            property real px
                            property real py

                            visible: backend.edit.camera && backend.cameraSource !== "" && !win.choosingFocus && !win.cropping
                            x: composition.cameraX * fit.width / 1920
                            y: composition.cameraY * fit.width / 1920
                            width: composition.cameraW * fit.width / 1920
                            height: composition.cameraH * fit.width / 1920
                            cursorShape: Qt.SizeAllCursor
                            onPressed: (mouse) => {
                                win.finishFocus(false);
                                timeline.selection = "";
                                px = mouse.x;
                                py = mouse.y;
                                backend.beginEdit();
                            }
                            onPositionChanged: (mouse) => {
                                if (pressed) {
                                    backend.setValue("cameraX", (x + mouse.x - px) / fit.width);
                                    backend.setValue("cameraY", (y + mouse.y - py) / fit.height);
                                }
                            }
                            onReleased: backend.endEdit()
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
                id: appearanceScroll

                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: appearanceScroll.availableWidth - 10
                    spacing: 10

                    ColumnLayout {
                        visible: win.choosingFocus
                        Layout.fillWidth: true

                        Label {
                            text: "ZOOM FOCUS"
                            font.bold: true
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: width * 9 / 16

                            Composition {
                                id: focusPreview

                                width: 1920
                                height: 1080
                                scale: parent.width / width
                                transformOrigin: Item.TopLeft
                                edit: backend.edit
                                screenSource: backend.duration > 0 ? backend.screenSource : ""
                                sourceAspect: backend.aspect
                                cameraSource: ""
                                zoom: 1
                            }

                            MouseArea {
                                id: focusPicker

                                function choose(mx, my) {
                                    const z = win.zoomValue(), c = backend.edit.crop;
                                    win.draftFocusX = c[0] + Math.max(0, Math.min(1, mx / width)) * c[2];
                                    win.draftFocusY = c[1] + Math.max(0, Math.min(1, my / height)) * c[3];
                                }

                                objectName: "zoomFocusPicker"
                                x: focusPreview.frameX * parent.width / 1920
                                y: focusPreview.frameY * parent.width / 1920
                                width: focusPreview.frameW * parent.width / 1920
                                height: focusPreview.frameH * parent.width / 1920
                                cursorShape: Qt.CrossCursor
                                onPressed: (mouse) => {
                                    forceActiveFocus();
                                    choose(mouse.x, mouse.y);
                                }
                                Keys.onEscapePressed: win.finishFocus(false)
                                onPositionChanged: (mouse) => {
                                    if (pressed)
                                        choose(mouse.x, mouse.y);

                                }

                                Rectangle {
                                    property var focusZoom: win.zoomValue()

                                    x: focusZoom ? (win.draftFocusX - backend.edit.crop[0]) / backend.edit.crop[2] * parent.width - 7 : 0
                                    y: focusZoom ? (win.draftFocusY - backend.edit.crop[1]) / backend.edit.crop[3] * parent.height - 7 : 0
                                    width: 14
                                    height: 14
                                    radius: 7
                                    color: "transparent"
                                    border.color: "white"
                                    border.width: 2
                                }

                            }

                        }

                        RowLayout {
                            FlatButton {
                                objectName: "confirmFocus"
                                text: "Confirm"
                                onClicked: win.finishFocus(true)
                            }

                            FlatButton {
                                text: "Cancel"
                                onClicked: win.finishFocus(false)
                            }

                        }

                    }

                    ColumnLayout {
                        objectName: "zoomControls"
                        visible: win.selectedZoom >= 0
                        Layout.fillWidth: true

                        Label {
                            text: "ZOOM"
                            Layout.fillWidth: true
                        }

                        FlatSlider {
                            id: amountSlider

                            Layout.fillWidth: true
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

                        RowLayout {
                            FlatButton {
                                objectName: "chooseFocus"
                                text: "Choose focus"
                                enabled: !win.choosingFocus
                                onClicked: win.beginFocus()
                            }

                            FlatButton {
                                text: "Delete zoom"
                                enabled: !win.choosingFocus
                                onClicked: {
                                    win.finishFocus(true);
                                    backend.deleteZoom(win.selectedZoom);
                                    timeline.selection = "";
                                    win.finishFocus(false);
                                }
                            }

                        }

                    }

                    ColumnLayout {
                        objectName: "appearanceControls"
                        visible: win.selectedZoom < 0
                        Layout.fillWidth: true
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
                            to: 0.25
                            current: backend.edit.padding || 0
                            suffix: "%"
                        }

                        Setting {
                            Layout.fillWidth: true
                            label: "Corners"
                            fieldName: "corners"
                            to: 0.08
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
                            text: "Window transparency"
                        }

                        RowLayout {
                            spacing: 0

                            Repeater {
                                model: ["Off", "Light", "Custom"]

                                FlatButton {
                                    required property string modelData
                                    objectName: "transparency" + modelData
                                    text: modelData
                                    primary: (backend.edit.windowTransparency ?? "off") === modelData.toLowerCase()
                                    onClicked: backend.setValue("windowTransparency", modelData.toLowerCase())
                                }
                            }
                        }

                        Setting {
                            objectName: "windowOpacitySetting"
                            Layout.fillWidth: true
                            visible: backend.edit.windowTransparency === "custom"
                            label: "Opacity"
                            fieldName: "windowOpacity"
                            stepSize: 0.01
                            current: backend.edit.windowOpacity ?? 0.96
                            suffix: "%"
                        }

                        Label {
                            text: "Recorded window only"
                            color: theme.colors.dark_foreground
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
                            from: 0.08
                            to: 0.4
                            current: backend.edit.cameraSize || 0.18
                            suffix: "%"
                        }

                        RowLayout {
                            FlatButton {
                                text: "Rectangle"
                                primary: backend.edit.cameraShape === "rectangle"
                                onClicked: backend.setValue("cameraShape", "rectangle")
                            }

                            FlatButton {
                                text: "Circle"
                                primary: backend.edit.cameraShape === "circle"
                                onClicked: backend.setValue("cameraShape", "circle")
                            }

                        }

                        Setting {
                            Layout.fillWidth: true
                            label: "Camera corners"
                            fieldName: "cameraCorners"
                            to: 0.5
                            current: backend.edit.cameraCorners ?? 0.12
                            suffix: "%"
                            enabled: backend.edit.cameraShape !== "circle"
                        }

                        Setting {
                            Layout.fillWidth: true
                            label: "Camera shadow"
                            fieldName: "cameraShadow"
                            current: backend.edit.cameraShadow ?? 0.45
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
                                        backend.setValue("cameraX", index % 2 === 0 ? 0.03 : 0.97 - backend.edit.cameraSize);
                                        backend.setValue("cameraY", index < 2 ? 0.04 : 0.96 - composition.cameraH / 1080);
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

        }

        Timeline {
            id: timeline

            visible: win.editing
            enabled: backend.phase === "editor"
            Layout.fillWidth: true
            onInteracting: win.finishFocus(false)
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

        objectName: "exportDialog"
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
                text: format.currentIndex === 1 ? "Silent loop. Rough estimate: " + (parseInt(outputWidth.currentText) * parseInt(outputWidth.currentText) * 9 / 16 * parseInt(outputFps.currentText) * gifLength.value * 0.03 / 1e+06).toFixed(1) + "–" + (parseInt(outputWidth.currentText) * parseInt(outputWidth.currentText) * 9 / 16 * parseInt(outputFps.currentText) * gifLength.value * 0.2 / 1e+06).toFixed(1) + " MB. Motion and detail affect the final size." : "H.264 video + AAC audio"
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
