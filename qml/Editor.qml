import QtQuick
import QtQuick.Layouts
import OmaCap

// The video editor, drawn the way Omasnap draws its editor: over the dimmed desktop (or
// filling a window), a centred toolbar, small popovers for the look, the preview, a floating
// timeline dock, a status pill and a faint hotkey legend. VideoStage hosts it.
FocusScope {
    id: editor

    // The open popover: "backdrop", "frame", "glass", "camera", "zoom", "export", or "".
    property string popover: ""
    property bool cropping: false
    // What the discard dialog asks about: "close" before closing an edit with unexported
    // changes, "replace" before a new recording or video replaces it, or "".
    property string discarding: ""
    readonly property bool editable: backend.phase === "editor"
    readonly property int selectedZoom: timeline.selectedZoom
    readonly property var zoom: selectedZoom >= 0 ? backend.edit.zooms[selectedZoom] ?? null : null
    // Omasnap's toolbar sits 39 px down on the overlay; a window has no margin to keep.
    readonly property real toolbarTop: stage.windowed ? 14 : 39
    readonly property string status: {
        if (backend.phase === "exporting")
            return "Exporting · " + Math.round(Math.min(1, backend.progress / 0.9) * 100) + "%";
        if (cropping)
            return "Drag the corners to resize · drag inside to move";
        if (backend.message !== "")
            return backend.message;
        if (timeline.cutting)
            return "Cut · click a track to split it · V selects";
        return "";
    }

    // Opens a popover, or closes it when it is already open.
    function toggle(name) {
        popover = popover === name ? "" : name;
    }

    // Toolbar buttons and hotkeys both land here.
    function act(action) {
        if (!editable && !["export", "close", "window"].includes(action))
            return;
        switch (action) {
        case "undo":
            preview.settle();
            backend.undo();
            break;
        case "redo":
            preview.settle();
            backend.redo();
            break;
        case "backdrop":
        case "frame":
        case "glass":
        case "export":
            cropping = false;
            toggle(action);
            break;
        case "camera":
            if (backend.cameraSource !== "") {
                cropping = false;
                toggle(action);
            }
            break;
        case "zoom":
            cropping = false;
            toggle("zoom");
            break;
        case "addZoom":
            cropping = false;
            timeline.addZoom(backend.position);
            break;
        case "select":
            cropping = false;
            timeline.cutting = false;
            break;
        case "cut":
            cropping = false;
            timeline.cutting = !timeline.cutting;
            break;
        case "cutMode":
            cropping = false;
            timeline.cutting = true;
            break;
        case "split":
            timeline.splitAtPlayhead();
            break;
        case "crop":
            if (cropping)
                cropping = false;
            else
                startCrop();
            break;
        case "window":
            stage.setWindowed(!stage.windowed);
            break;
        case "close":
            requestClose();
            break;
        }
    }

    function startCrop() {
        backend.pause();
        popover = "";
        timeline.selection = "";
        timeline.cutting = false;
        cropping = true;
        cropView.begin(backend.edit.crop);
    }

    // Escape steps back one level at a time: a popover, then the selection or cut tool,
    // then the crop view, then the editor itself.
    function stepBack() {
        backend.endEdit();
        if (discarding !== "")
            answer(false);
        else if (popover !== "")
            popover = "";
        else if (timeline.selection !== "" || timeline.cutting) {
            timeline.selection = "";
            timeline.cutting = false;
        } else if (cropping)
            cropping = false;
        else
            requestClose();
    }

    // Closes the session, asking first when it has unexported edits. The recording itself
    // always stays where it was saved.
    function requestClose() {
        if (backend.phase === "exporting") {
            popover = "export";
            return;
        }
        if (discarding !== "")
            return;
        if (backend.dirty)
            discarding = "close";
        else
            backend.discard();
    }

    // VideoStage asks this before a new recording or video replaces unexported edits.
    function askReplace() {
        backend.endEdit();
        popover = "";
        discarding = "replace";
    }

    // Answers the discard dialog: discard the edit, or keep editing.
    function answer(discard) {
        const question = discarding;
        discarding = "";
        if (question === "replace")
            backend.answerReplace(discard);
        else if (discard)
            backend.discard();
    }

    function handleKey(event) {
        const plain = (event.modifiers & ~Qt.KeypadModifier) === Qt.NoModifier;
        const control = event.modifiers & Qt.ControlModifier;
        const shift = event.modifiers & Qt.ShiftModifier;
        const open = !cropping && discarding === "";
        let handled = true;
        if (event.key === Qt.Key_Escape)
            stepBack();
        else if (discarding !== "" && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter))
            answer(true);
        else if (cropping && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter))
            cropView.confirm();
        else if (control && event.key === Qt.Key_Z && open && editable)
            act(shift ? "redo" : "undo");
        else if (control && event.key === Qt.Key_Y && open && editable)
            act("redo");
        else if (!plain)
            handled = false;
        else if (event.key === Qt.Key_Space && open && editable)
            backend.togglePlay();
        else if (event.key === Qt.Key_Left && open && editable)
            backend.seek(backend.position - 1 / 30);
        else if (event.key === Qt.Key_Right && open && editable)
            backend.seek(backend.position + 1 / 30);
        else if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) && open && editable && (popover === "" || popover === "zoom"))
            timeline.removeSelection();
        else if (!open)
            handled = false;
        else {
            const action = {
                [Qt.Key_B]: "backdrop",
                [Qt.Key_F]: "frame",
                [Qt.Key_G]: "glass",
                [Qt.Key_P]: "camera",
                [Qt.Key_E]: "export",
                [Qt.Key_K]: "crop",
                [Qt.Key_C]: "cutMode",
                [Qt.Key_V]: "select",
                [Qt.Key_S]: "split",
                [Qt.Key_Z]: "addZoom",
                [Qt.Key_W]: "window"
            }[event.key];
            if (action)
                act(action);
            else
                handled = false;
        }
        event.accepted = handled;
    }

    objectName: "editor"
    anchors.fill: parent
    focus: true
    Keys.onPressed: (event) => handleKey(event)
    onSelectedZoomChanged: {
        if (selectedZoom >= 0)
            cropping = false;
    }

    // Omasnap dims the frozen desktop with its theme background at alpha 143. A window has
    // nothing behind it to show.
    Rectangle {
        anchors.fill: parent
        color: stage.windowed ? theme.scrim : theme.alpha(theme.scrim, 143)

        MouseArea {
            anchors.fill: parent
            onClicked: editor.popover = ""
        }

    }

    HotkeyLegend {
        visible: !stage.windowed
        x: 14
        y: parent.height - 14 - height
        entries: [["Esc", "Back · close"], ["W", "Window"], ["E", "Export"], ["B F G P", "Backdrop · frame · glass · camera"], ["K", "Crop"], ["S", "Split at playhead"], ["Z", "Zoom at playhead"], ["C V", "Cut · select"], ["Del", "Delete selection"], ["← →", "Step a frame"], ["Space", "Play · pause"], ["Ctrl+Z", "Undo · Shift redoes"]]
    }

    Preview {
        id: preview

        visible: !editor.cropping
        x: 30
        y: toolbar.y + toolbar.height + 18
        width: parent.width - 60
        height: dock.y - 18 - y
        zoomIndex: editor.selectedZoom
        onClicked: {
            editor.popover = "";
            timeline.selection = "";
        }
        onAimPressed: {
            editor.popover = "";
            timeline.selectZoom(editor.selectedZoom, true);
            editor.forceActiveFocus();
        }
    }

    CropView {
        id: cropView

        function confirm() {
            accepted([selection.x, selection.y, selection.width, selection.height]);
        }

        objectName: "cropView"
        visible: editor.cropping && backend.phase === "editor"
        x: 30
        y: toolbar.y + toolbar.height + 18
        width: parent.width - 60
        // The status pill sits below.
        height: parent.height - y - 60
        source: backend.screenSource
        sourceAspect: backend.aspect
        onAccepted: (crop) => {
            backend.setValue("crop", crop);
            editor.cropping = false;
        }
        onCanceled: editor.cropping = false
    }

    ChromePanel {
        id: dock

        visible: !editor.cropping
        width: Math.min(parent.width - 28, Math.max(860, parent.width * 0.6))
        height: timeline.implicitHeight + 24
        x: (parent.width - width) / 2
        y: parent.height - height - 14
        radius: 12

        Timeline {
            id: timeline

            x: 14
            y: 12
            width: parent.width - 28
            enabled: editor.editable
        }

    }

    StatusPill {
        x: (parent.width - width) / 2
        y: (editor.cropping ? parent.height : dock.y) - height - 12
        text: editor.status
    }

    Toolbar {
        id: toolbar

        z: 2
        x: (parent.width - width) / 2
        y: editor.toolbarTop
        popover: editor.popover
        cropping: editor.cropping
        cutting: timeline.cutting
        zoomSelected: editor.selectedZoom >= 0
        hasCamera: backend.cameraSource !== ""
        editable: editor.editable
        onTriggered: (action) => editor.act(action)
    }

    BackdropPopover {
        z: 1
        visible: editor.popover === "backdrop"
        anchorItem: toolbar.button("backdrop")
    }

    FramePopover {
        z: 1
        visible: editor.popover === "frame"
        anchorItem: toolbar.button("frame")
    }

    GlassPopover {
        z: 1
        visible: editor.popover === "glass"
        anchorItem: toolbar.button("glass")
    }

    CameraPopover {
        z: 1
        visible: editor.popover === "camera"
        anchorItem: toolbar.button("camera")
        cameraHeight: preview.composition.cameraH
    }

    ZoomPopover {
        z: 1
        visible: editor.popover === "zoom"
        anchorItem: toolbar.button("zoom")
        index: editor.selectedZoom
        zoom: editor.zoom
        onAdd: timeline.addZoom(backend.position)
        onRemove: timeline.removeSelection()
    }

    ExportPopover {
        z: 1
        visible: editor.popover === "export"
        anchorItem: toolbar.button("export")
    }

    // Asks before closing an edit with unexported changes, or replacing it.
    Item {
        objectName: "discardDialog"
        anchors.fill: parent
        visible: editor.discarding !== ""
        z: 3

        Rectangle {
            anchors.fill: parent
            color: theme.alpha(theme.scrim, 120)

            MouseArea {
                anchors.fill: parent
                onClicked: editor.answer(false)
            }

        }

        ChromePanel {
            anchors.centerIn: parent
            width: 420
            height: question.implicitHeight + 32

            ColumnLayout {
                id: question

                x: 16
                y: 16
                width: parent.width - 32
                spacing: 12

                Text {
                    text: "Discard this edit?"
                    font: theme.font(14, true)
                    color: theme.foreground
                }

                Text {
                    Layout.fillWidth: true
                    text: (editor.discarding === "replace" ? "A new recording or video replaces it, and its changes haven't been exported. " : "It has changes that haven't been exported. ") + (backend.original ? "The recording stays at " + backend.original + "." : "Discarding removes its temporary media.")
                    font: theme.font(12)
                    color: theme.alpha(theme.foreground, 190)
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 6

                    ChromeChip {
                        objectName: "keepEditing"
                        text: "Keep editing"
                        key: "Esc"
                        onClicked: editor.answer(false)
                    }

                    ChromeChip {
                        objectName: "confirmDiscard"
                        accent: true
                        text: "Discard"
                        key: "Enter"
                        onClicked: editor.answer(true)
                    }

                }

            }

        }

    }

}
