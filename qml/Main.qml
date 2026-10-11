import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: win

    property bool recorderMode: ["recorder", "selecting", "countdown", "recording", "stopping"].includes(backend.phase)
    property int selectedZoom: timeline.selectedZoom
    property bool cropping: false
    property bool allowClose: false
    property bool closeAfterDiscard: false
    property bool captureAfterDiscard: false
    property bool editing: backend.phase === "editor" || backend.phase === "exporting"
    // A screenshot session: no timeline, camera or audio, and Copy/Save instead of Export.
    property bool still: backend.kind === "image"
    property bool outputReady: still && backend.phase === "editor" && !cropping && !discardDialog.opened && !editor.typing
    // Single-key annotation shortcuts, off while typing.
    property bool markKeys: outputReady
    // The screenshot tool row, as in Omasnap: name, key, and the editor tool it selects.
    readonly property var tools: [{
        "id": "select",
        "label": "Select",
        "key": "V"
    }, {
        "id": "crop",
        "label": "Crop",
        "key": "K"
    }, {
        "id": "box",
        "label": "Box",
        "key": "R"
    }, {
        "id": "ellipse",
        "label": "Ellipse",
        "key": "E"
    }, {
        "id": "arrow",
        "label": "Arrow",
        "key": "A"
    }, {
        "id": "line",
        "label": "Line",
        "key": "L"
    }, {
        "id": "text",
        "label": "Text",
        "key": "T"
    }, {
        "id": "step",
        "label": "Step",
        "key": "C"
    }, {
        "id": "pen",
        "label": "Pen",
        "key": "F"
    }, {
        "id": "marker",
        "label": "Marker",
        "key": "H"
    }, {
        "id": "redact",
        "label": "Redact",
        "key": "D"
    }]
    readonly property var markColors: ["#ff375f", "#ff9f0a", "#ffd60a", "#30d158", "#0a84ff", "#bf5af2", "#000000", "#ffffff"]

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

    // The buttons can be clicked mid-typing; the shortcuts are off then.
    function output(copy, save) {
        editor.commitText();
        backend.outputImage(copy, save);
    }

    function startCrop() {
        backend.pause();
        timeline.selection = "";
        win.cropping = true;
        cropView.begin(backend.edit.crop);
    }

    function useTool(id) {
        if (id === "crop") {
            editor.cancel();
            startCrop();
            return ;
        }
        editor.commitText();
        editor.tool = id;
        if (id !== "select")
            editor.selectedId = "";
    }

    // Colour and size apply to the selection, and to whatever is drawn next.
    function useColor(color) {
        editor.color = color;
        editor.restyle({
            "color": color
        });
    }

    function useSize(index) {
        editor.size = index;
        if (editor.selected && editor.selected.kind !== "redact")
            editor.restyle({
                "size": editor.sizeFor(editor.selected.kind, index)
            });
    }

    // Hidden while the region picker runs, so OmaCap stays out of its own screenshot.
    visible: backend.phase !== "capturing"
    title: "OmaCap"
    flags: recorderMode ? Qt.Dialog : Qt.Window
    width: recorderMode ? 520 : 1280
    height: recorderMode ? 640 : 850
    minimumWidth: recorderMode ? 520 : 1000
    minimumHeight: recorderMode ? 640 : 680
    maximumWidth: recorderMode ? 520 : 1.67772e+07
    maximumHeight: recorderMode ? 640 : 1.67772e+07
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
        if (selectedZoom >= 0)
            cropping = false;

        if (appearanceScroll.contentItem)
            appearanceScroll.contentItem.contentY = 0;

    }
    onClosing: (event) => {
        if (allowClose)
            return ;

        // Text still being typed counts as an unexported change.
        editor.commitText();

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

    Shortcut {
        sequence: "Space"
        enabled: !win.still && backend.phase === "editor" && !win.cropping && !exportDialog.opened && !discardDialog.opened && !focusPicker.pressed
        onActivated: backend.togglePlay()
    }

    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: backend.phase === "editor" && !win.cropping && !focusPicker.pressed && !editor.typing
        onActivated: backend.undo()
    }

    Shortcut {
        sequences: [StandardKey.Redo]
        enabled: backend.phase === "editor" && !win.cropping && !focusPicker.pressed && !editor.typing
        onActivated: backend.redo()
    }

    Shortcut {
        sequence: "Left"
        enabled: !win.still && backend.phase === "editor" && !win.cropping && !focusPicker.pressed && !exportDialog.opened && !discardDialog.opened
        onActivated: backend.seek(backend.position - 1 / 30)
    }

    Shortcut {
        sequence: "Right"
        enabled: !win.still && backend.phase === "editor" && !win.cropping && !focusPicker.pressed && !exportDialog.opened && !discardDialog.opened
        onActivated: backend.seek(backend.position + 1 / 30)
    }

    // `omacap --screenshot` while OmaCap is open. An unexported edit asks first.
    Connections {
        function onScreenshotRequested() {
            // The new shot replaces this session, and its dialogs go with it. Text still
            // being typed counts as an unexported change.
            if (backend.phase === "editor") {
                editor.commitText();
                exportDialog.close();
                win.cropping = false;
            }
            if (backend.phase === "recorder") {
                backend.takeScreenshot();
            } else if (backend.phase === "editor" && !backend.dirty) {
                backend.discard();
                backend.takeScreenshot();
            } else if (backend.phase === "editor") {
                win.closeAfterDiscard = false;
                win.captureAfterDiscard = true;
                discardDialog.open();
                win.raise();
                win.requestActivate();
            }
        }

        target: backend
    }

    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: win.outputReady
        onActivated: backend.outputImage(true, false)
    }

    Shortcut {
        sequences: [StandardKey.Save]
        enabled: win.outputReady
        onActivated: backend.outputImage(false, true)
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: win.outputReady
        onActivated: backend.outputImage(true, true)
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            win.cropping = false;
            backend.endEdit();
            timeline.selection = "";
            timeline.cutting = false;
            editor.cancel();
        }
    }

    Instantiator {
        model: win.tools

        delegate: Shortcut {
            required property var modelData

            sequence: modelData.key
            enabled: win.markKeys
            onActivated: win.useTool(modelData.id)
        }

    }

    Instantiator {
        model: win.markColors

        delegate: Shortcut {
            required property int index
            required property string modelData

            sequence: String(index + 1)
            enabled: win.markKeys
            onActivated: win.useColor(modelData)
        }

    }

    Shortcut {
        sequences: [StandardKey.Delete, "Backspace"]
        enabled: win.markKeys && editor.selected !== null
        onActivated: editor.remove()
    }

    // Arrow keys nudge the selected annotation 1 px, or 10 px with Shift.
    Instantiator {
        model: [["Left", -1, 0], ["Right", 1, 0], ["Up", 0, -1], ["Down", 0, 1], ["Shift+Left", -10, 0], ["Shift+Right", 10, 0], ["Shift+Up", 0, -10], ["Shift+Down", 0, 10]]

        delegate: Shortcut {
            required property var modelData

            sequence: modelData[0]
            enabled: win.markKeys && editor.selected !== null
            onActivated: editor.nudge(modelData[1], modelData[2])
        }

    }

    CropView {
        id: cropView
        objectName: "cropView"
        anchors.fill: parent
        anchors.margins: 16
        visible: win.cropping && backend.phase === "editor"
        source: backend.screenSource
        sourceAspect: backend.aspect
        onAccepted: (crop) => {
            backend.setValue("crop", crop);
            win.cropping = false;
        }
        onCanceled: win.cropping = false
    }

    ColumnLayout {
        visible: !win.cropping
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
                objectName: "undo"
                text: "Undo"
                // Text being typed is committed first, so Undo acts on it rather than under it.
                onClicked: {
                    editor.commitText();
                    backend.undo();
                }
                enabled: backend.phase === "editor"
            }

            FlatButton {
                visible: win.editing
                text: "Redo"
                onClicked: {
                    editor.commitText();
                    backend.redo();
                }
                enabled: backend.phase === "editor"
            }

            FlatButton {
                visible: win.editing
                text: win.still ? "New capture" : "New recording"
                enabled: backend.phase === "editor"
                onClicked: {
                    editor.commitText();
                    if (backend.dirty) {
                        win.closeAfterDiscard = false;
                        discardDialog.open();
                    } else {
                        backend.discard();
                    }
                }
            }

            FlatButton {
                visible: win.editing && win.still
                objectName: "copyImage"
                text: "Copy"
                enabled: backend.phase === "editor"
                onClicked: win.output(true, false)
            }

            FlatButton {
                visible: win.editing && win.still
                objectName: "saveImage"
                text: "Save"
                primary: true
                enabled: backend.phase === "editor"
                onClicked: win.output(false, true)
            }

            FlatButton {
                visible: win.editing && !win.still
                text: "Export"
                primary: true
                enabled: backend.phase === "editor" && backend.duration > 0 && !focusPicker.pressed
                onClicked: exportDialog.open()
            }

        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: theme.colors.muted
        }

        RowLayout {
            objectName: "toolRow"
            visible: win.still && win.editing
            enabled: backend.phase === "editor"
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: win.tools

                FlatButton {
                    required property var modelData
                    required property int index

                    objectName: "tool-" + modelData.id
                    // Shortcut hints when the window has room for them.
                    text: modelData.label + (win.width >= 1260 ? " <font color='" + theme.colors.muted + "'>" + modelData.key + "</font>" : "")
                    padding: 8
                    primary: editor.tool === modelData.id
                    Layout.leftMargin: index === 2 ? 10 : 0
                    onClicked: win.useTool(modelData.id)
                }

            }

            Item {
                Layout.fillWidth: true
            }

            Repeater {
                model: win.markColors

                Rectangle {
                    required property string modelData

                    implicitWidth: 18
                    implicitHeight: 18
                    color: modelData
                    border.width: Qt.colorEqual(editor.color, modelData) ? 2 : 1
                    border.color: Qt.colorEqual(editor.color, modelData) ? theme.colors.foreground : theme.colors.muted

                    MouseArea {
                        anchors.fill: parent
                        onClicked: win.useColor(parent.modelData)
                    }

                }

            }

            Repeater {
                model: ["S", "M", "L"]

                FlatButton {
                    required property int index
                    required property string modelData

                    text: modelData
                    padding: 8
                    primary: editor.size === index
                    Layout.leftMargin: index === 0 ? 10 : 0
                    onClicked: win.useSize(index)
                }

            }

        }


        ColumnLayout {
            visible: !win.editing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            ColumnLayout {
                visible: backend.phase === "recorder"
                Layout.fillWidth: true
                spacing: 12

                Label {
                    text: "Take a screenshot"
                    font.pixelSize: 20
                }

                Label {
                    text: "Drag a region, click a window, or click empty space for the whole screen."
                    color: theme.colors.dark_foreground
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                FlatButton {
                    objectName: "takeScreenshot"
                    text: "Take screenshot"
                    primary: true
                    Layout.fillWidth: true
                    onClicked: backend.takeScreenshot()
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    height: 1
                    color: theme.colors.lighter_background
                }

            }

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
                        text: "Open image or video"
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
                    visible: !win.still

                    FlatButton {
                        text: "Crop"
                        enabled: backend.duration > 0
                        onClicked: win.startCrop()
                    }

                    FlatButton {
                        text: "Reset crop"
                        onClicked: backend.setValue("crop", [0, 0, 1, 1])
                    }

                    Label {
                        text: (win.still ? (backend.edit.canvas === "auto" ? "Auto" : backend.edit.canvas) : "16:9") + " · styled preview"
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

                        property real aspect: win.still ? backend.canvasAspect : 16 / 9

                        width: Math.min(parent.width, parent.height * aspect)
                        height: width / aspect
                        anchors.centerIn: parent

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                timeline.selection = "";
                            }
                        }

                        Composition {
                            id: composition

                            objectName: "composition"
                            // At most 1920 px on its longer side, however tall the shot.
                            width: fit.aspect >= 1 ? 1920 : 1920 * fit.aspect
                            height: width / fit.aspect
                            scale: fit.width / width
                            transformOrigin: Item.TopLeft
                            edit: backend.edit
                            sourcePixels: backend.sourceSize
                            hiddenAnnotation: editor.hiddenId
                            screenSource: win.still || backend.duration > 0 ? backend.screenSource : ""
                            cameraSource: backend.duration > 0 ? backend.cameraSource : ""
                            sourceAspect: backend.aspect
                            zoom: backend.zoom
                            focusX: backend.focusX
                            focusY: backend.focusY
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: !win.still && backend.duration === 0
                            text: "No clips left. Undo to restore them."
                            padding: 16

                            background: Rectangle {
                                color: theme.colors.background
                            }

                        }

                        AnnotationEditor {
                            id: editor

                            objectName: "annotationEditor"
                            anchors.fill: parent
                            visible: win.still
                            composition: composition
                        }

                        MouseArea {
                            property real px
                            property real py

                            visible: backend.edit.camera && backend.cameraSource !== "" && !focusPicker.pressed
                            x: composition.cameraX * fit.width / 1920
                            y: composition.cameraY * fit.width / 1920
                            width: composition.cameraW * fit.width / 1920
                            height: composition.cameraH * fit.width / 1920
                            cursorShape: Qt.SizeAllCursor
                            onPressed: (mouse) => {
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
                        visible: win.selectedZoom >= 0
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
                                    if (!z)
                                        return;
                                    const x = c[0] + Math.max(0, Math.min(1, mx / width)) * c[2];
                                    const y = c[1] + Math.max(0, Math.min(1, my / height)) * c[3];
                                    win.changeZoom(z.start, z.end, x, y, z.amount);
                                }

                                objectName: "zoomFocusPicker"
                                x: focusPreview.frameX * parent.width / 1920
                                y: focusPreview.frameY * parent.width / 1920
                                width: focusPreview.frameW * parent.width / 1920
                                height: focusPreview.frameH * parent.width / 1920
                                cursorShape: Qt.CrossCursor
                                onPressed: (mouse) => {
                                    timeline.selectZoom(win.selectedZoom, true);
                                    win.cropping = false;
                                    forceActiveFocus();
                                    backend.beginEdit();
                                    choose(mouse.x, mouse.y);
                                }
                                onReleased: backend.endEdit()
                                onCanceled: backend.endEdit()
                                onPositionChanged: (mouse) => {
                                    if (pressed)
                                        choose(mouse.x, mouse.y);

                                }

                                Rectangle {
                                    property var focusZoom: win.zoomValue()

                                    x: focusZoom ? Math.max(0, Math.min(1, (focusZoom.x - backend.edit.crop[0]) / backend.edit.crop[2])) * parent.width - 7 : 0
                                    y: focusZoom ? Math.max(0, Math.min(1, (focusZoom.y - backend.edit.crop[1]) / backend.edit.crop[3])) * parent.height - 7 : 0
                                    width: 14
                                    height: 14
                                    radius: 7
                                    color: "transparent"
                                    border.color: "white"
                                    border.width: 2
                                }

                            }

                        }

                        Label {
                            text: "Drag to move the focus."
                            color: theme.colors.dark_foreground
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

                        FlatButton {
                            text: "Delete zoom"
                            onClicked: {
                                backend.deleteZoom(win.selectedZoom);
                                timeline.selection = "";
                            }
                        }

                    }

                    // The selected annotation's own options; colour and size are in the tool row.
                    ColumnLayout {
                        objectName: "annotationControls"
                        visible: win.still && editor.selected !== null
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            text: (win.tools.find((t) => t.id === editor.selected?.kind)?.label ?? "").toUpperCase()
                            font.bold: true
                        }

                        RowLayout {
                            visible: ["box", "ellipse"].includes(editor.selected?.kind)
                            spacing: 0

                            Repeater {
                                model: ["Outline", "Solid", "Tint"]

                                FlatButton {
                                    required property string modelData

                                    text: modelData
                                    primary: editor.selected?.fill === modelData.toLowerCase()
                                    onClicked: editor.restyle({
                                        "fill": modelData.toLowerCase()
                                    })
                                }

                            }

                        }

                        RowLayout {
                            visible: editor.selected?.kind === "text"
                            spacing: 0

                            Repeater {
                                model: ["Pill", "Plain"]

                                FlatButton {
                                    required property string modelData

                                    text: modelData
                                    primary: (editor.selected?.pill ?? true) === (modelData === "Pill")
                                    onClicked: {
                                        editor.textPill = modelData === "Pill";
                                        editor.restyle({
                                            "pill": editor.textPill
                                        });
                                    }
                                }

                            }

                        }

                        Label {
                            visible: editor.selected?.kind === "text"
                            text: "Double-click the text to edit it."
                            color: theme.colors.dark_foreground
                        }

                        FlatButton {
                            text: "Delete"
                            onClicked: editor.remove()
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: theme.colors.lighter_background
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
                            visible: !win.still
                            text: "Recorded window only"
                            color: theme.colors.dark_foreground
                        }

                        Label {
                            visible: win.still
                            text: "Canvas"
                        }

                        RowLayout {
                            visible: win.still
                            spacing: 0

                            Repeater {
                                model: ["Auto", "16:9", "4:3", "1:1"]

                                FlatButton {
                                    required property string modelData
                                    objectName: "canvas" + modelData
                                    text: modelData
                                    primary: (backend.edit.canvas ?? "auto") === modelData.toLowerCase()
                                    onClicked: backend.setValue("canvas", modelData.toLowerCase())
                                }
                            }
                        }

                        Label {
                            visible: win.still
                            text: "Auto fits the shot plus padding."
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            color: theme.colors.dark_foreground
                        }

                        ColumnLayout {
                            visible: !win.still
                            Layout.fillWidth: true
                            spacing: 10

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

        }

        Timeline {
            id: timeline

            visible: win.editing && !win.still
            enabled: backend.phase === "editor"
            Layout.fillWidth: true
        }

        RowLayout {
            visible: backend.phase === "exporting" && !win.still
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
            visible: backend.message !== "" || backend.lastExport !== "" || (win.still && win.editing)

            Label {
                objectName: "stillSize"
                visible: win.still && win.editing
                text: "Source " + backend.sourceSize.width + "×" + backend.sourceSize.height + " · PNG " + backend.stillSize.width + "×" + backend.stillSize.height
                color: theme.colors.dark_foreground
            }

            Label {
                text: backend.message
                horizontalAlignment: win.still ? Text.AlignRight : Text.AlignLeft
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

        objectName: "discardDialog"

        anchors.centerIn: parent
        modal: true
        title: "Discard this session?"
        width: 420
        onClosed: win.captureAfterDiscard = false

        contentItem: Label {
            text: "This session has unexported changes. " + (backend.original ? (win.still ? "The image" : "The recording") + " stays at " + backend.original + "." : "Discard removes its temporary media.")
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
                    const capture = win.captureAfterDiscard;
                    discardDialog.close();
                    backend.discard();
                    if (win.closeAfterDiscard) {
                        win.allowClose = true;
                        win.close();
                    } else if (capture) {
                        backend.takeScreenshot();
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
                visible: format.currentIndex === 0
                text: "Quality"
            }

            ComboBox {
                id: quality

                visible: format.currentIndex === 0
                Layout.fillWidth: true
                model: ["High", "Balanced", "Smaller file"]
                Component.onCompleted: currentIndex = backend.preference("export/quality", 0)
                onActivated: backend.remember("export/quality", currentIndex)
            }

            Label {
                text: format.currentIndex === 1 ? "Silent loop of the whole edit. Busy motion makes large files." : "H.264 video + AAC audio"
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
                    backend.exportVideo(format.currentIndex === 1, parseInt(outputWidth.currentText), parseInt(outputFps.currentText), [18, 23, 28][quality.currentIndex]);
                }
            }

        }

    }

}
