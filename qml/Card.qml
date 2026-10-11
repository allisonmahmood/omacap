import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import OmaCap
import "exports.js" as Exports

// The card in the corner after a recording, as Omasnap's capture preview: the styled first
// frame and its length; hover for Edit and Export, keep, reveal and close. Keys: Enter exports
// with the last settings, E edits, P keeps, X or Esc closes. It fades after ten seconds unless
// it is hovered, kept or exporting, and can't close while exporting. Closing it ends the
// session; the recording stays in Videos. The same card offers an earlier session's unfinished
// edit for recovery: E or Enter recovers it, D or Delete discards it, X or Esc leaves it for
// later.
FocusScope {
    id: card

    readonly property bool recovery: stage.card === "recovery"
    readonly property bool exporting: backend.phase === "exporting"
    readonly property bool exported: backend.lastExport !== "" && backend.message.startsWith("Export complete.")
    readonly property bool hovered: hover.hovered
    property bool kept: false
    property string toast: ""

    function edit() {
        if (recovery)
            backend.recover();
        else
            stage.openEditor();
    }

    // Deletes the offered edit and closes the card. Its recording stays in Videos.
    function discard() {
        if (!recovery || backend.phase !== "idle")
            return;
        backend.discardRecovery();
        stage.closeCard();
    }

    function exportNow() {
        if (recovery || backend.phase !== "editor")
            return;
        const settings = Exports.current(backend);
        stage.exportNow(settings.gif, settings.width, settings.fps, settings.quality);
    }

    function keep() {
        kept = !kept;
        toast = kept ? "Kept" : "Fades after 10 seconds";
        toastTimer.restart();
    }

    function time(t) {
        const seconds = Math.round(t);
        return Math.floor(seconds / 60) + ":" + (seconds % 60).toString().padStart(2, "0");
    }

    objectName: "card"
    implicitWidth: 200
    implicitHeight: 113
    focus: true
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_E || (recovery && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)))
            edit();
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            exportNow();
        else if (event.key === Qt.Key_P && !recovery)
            keep();
        else if ((event.key === Qt.Key_D || event.key === Qt.Key_Delete) && recovery)
            discard();
        else if (event.key === Qt.Key_X || event.key === Qt.Key_Escape)
            stage.closeCard();
        else
            return ;
        event.accepted = true;
    }
    onHoveredChanged: {
        if (hovered) {
            fade.stop();
            opacity = 1;
        }
    }

    HoverHandler {
        id: hover
    }

    // Ten seconds without a hover, a kept pin or an export, then a short fade.
    Timer {
        interval: 10000
        running: !card.kept && !card.hovered && !card.exporting && !fade.running
        onTriggered: fade.start()
    }

    NumberAnimation {
        id: fade

        target: card
        property: "opacity"
        to: 0
        duration: 300
        onFinished: stage.closeCard()
    }

    Timer {
        id: toastTimer

        interval: 1200
        onTriggered: card.toast = ""
    }

    ChromeBox {
        anchors.fill: parent
        radius: 7
        fill: theme.surface
    }

    // The styled first frame, rounded like the card.
    Item {
        id: thumbnail

        anchors.fill: parent
        visible: false
        layer.enabled: true

        Composition {
            width: 1920
            height: 1080
            scale: thumbnail.width / 1920
            transformOrigin: Item.TopLeft
            edit: backend.edit
            screenSource: card.recovery ? "" : backend.screenSource
            cameraSource: card.recovery ? "" : backend.cameraSource
            sourceAspect: backend.aspect
            zoom: backend.zoom
            focusX: backend.focusX
            focusY: backend.focusY
        }

    }

    Rectangle {
        id: rounded

        anchors.fill: parent
        radius: 7
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: parent
        visible: !card.recovery
        source: thumbnail
        maskEnabled: true
        maskSource: rounded
    }

    ColumnLayout {
        visible: card.recovery
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 4

        Text {
            text: "Unfinished edit"
            font: theme.font(12, true)
            color: theme.foreground
        }

        Text {
            Layout.fillWidth: true
            text: backend.message !== "" ? backend.message : "OmaCap closed before it was exported."
            font: theme.font(11)
            color: theme.alpha(theme.foreground, 170)
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

    }

    Rectangle {
        anchors.fill: parent
        radius: 7
        visible: card.hovered && !card.recovery
        color: theme.alpha(theme.surface, 90)
    }

    // The length, the export's progress, or the exported size.
    Rectangle {
        visible: !card.recovery
        x: 7
        y: parent.height - height - 7
        width: badge.implicitWidth + 12
        height: 18
        radius: 5
        color: theme.alpha(theme.surface, 217)

        Text {
            id: badge

            anchors.centerIn: parent
            // "Export complete. 1.2 MB." gives the size; a GIF over 10 MB is marked.
            text: card.exporting ? "Exporting " + Math.round(Math.min(1, backend.progress / 0.9) * 100) + "%" : card.exported ? (backend.message.indexOf("10 MB") >= 0 ? "⚠ " : "✓ ") + backend.message.split(". ")[1].replace(/\.$/, "") : "▶ " + card.time(backend.duration)
            font: theme.mono(10, true)
            color: card.exported && backend.message.indexOf("10 MB") >= 0 ? theme.warning : theme.foreground
        }

    }

    // Progress moves in the exporter's steps; nothing animates in between.
    Rectangle {
        visible: card.exporting
        x: 7
        y: parent.height - 4
        width: (parent.width - 14) * Math.min(1, backend.progress / 0.9)
        height: 2
        radius: 1
        color: theme.accent
    }

    MouseArea {
        anchors.fill: parent
        onDoubleClicked: card.edit()
    }

    Column {
        anchors.centerIn: parent
        visible: card.hovered && !card.recovery
        spacing: 6

        ChromeChip {
            objectName: "cardEdit"
            anchors.horizontalCenter: parent.horizontalCenter
            solid: true
            icon: "edit"
            text: "Edit"
            key: "E"
            onClicked: card.edit()
        }

        ChromeChip {
            objectName: "cardExport"
            anchors.horizontalCenter: parent.horizontalCenter
            solid: true
            icon: card.exporting ? "close" : "export"
            text: card.exporting ? "Cancel export" : "Export"
            key: card.exporting ? "" : "Enter"
            enabled: backend.duration > 0
            onClicked: card.exporting ? backend.cancelExport() : card.exportNow()
        }

    }

    Row {
        visible: card.recovery
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - height - 12
        spacing: 6

        ChromeChip {
            objectName: "cardRecover"
            accent: true
            text: "Recover"
            key: "E"
            enabled: backend.phase === "idle"
            onClicked: card.edit()
        }

        ChromeChip {
            objectName: "cardDiscard"
            text: "Discard"
            key: "D"
            enabled: backend.phase === "idle"
            onClicked: card.discard()
        }

    }

    Row {
        x: parent.width - width - 6
        y: 6
        visible: card.hovered || card.recovery
        spacing: 4

        ChromeButton {
            visible: !card.recovery
            width: 24
            height: 24
            radius: 6
            icon: "pin"
            selected: card.kept
            tooltip: card.kept ? "Kept · P" : "Keep · P"
            onClicked: card.keep()
        }

        ChromeButton {
            visible: !card.recovery
            width: 24
            height: 24
            radius: 6
            icon: "folder"
            tooltip: "Show in folder"
            onClicked: stage.reveal(backend.lastExport || backend.original)
        }

        ChromeButton {
            objectName: "cardClose"
            visible: !card.exporting
            width: 24
            height: 24
            radius: 6
            icon: "close"
            tooltip: card.recovery ? "Not now · X" : "Close · X"
            onClicked: stage.closeCard()
        }

    }

    ChromeBox {
        anchors.fill: parent
        radius: 7
        borderWidth: 1.5
        border: card.hovered ? "active" : "inactive"
    }

    Rectangle {
        visible: card.toast !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - 34
        width: toastText.implicitWidth + 24
        height: 24
        radius: 12
        color: theme.toastBackground

        Text {
            id: toastText

            anchors.centerIn: parent
            text: card.toast
            font: theme.font(11)
            color: theme.toastText
        }

    }

}
