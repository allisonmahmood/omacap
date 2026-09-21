import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: timeline

    property string selection: ""
    property string clipId: ""
    property string zoomId: ""
    property real rangeA: 0
    property real rangeB: 0
    property bool cutting: false
    property int selectedZoom: {
        const zs = backend.edit.zooms || [];
        return selection === "zoom" ? zs.findIndex((z) => {
            return z.id === zoomId;
        }) : -1;
    }

    function selectZoom(index, reveal) {
        const z = backend.edit.zooms[index];
        if (!z)
            return ;

        selection = "zoom";
        zoomId = z.id;
        forceActiveFocus();
        if (reveal) {
            backend.pause();
            backend.seek((backend.toEditedTime(z.start) + backend.toEditedTime(z.end)) / 2);
        }
    }

    function addZoom(at) {
        const index = backend.addZoom(at);
        if (index < 0)
            return ;

        selectZoom(index, true);
    }

    function selectClip(at) {
        let pos = 0;
        for (const s of backend.edit.spans) {
            const end = pos + s[1] - s[0];
            if (at >= pos && at < end) {
                selection = "clip";
                clipId = s[2];
                rangeA = pos;
                rangeB = end;
                return ;
            }
            pos = end;
        }
    }

    function removeSelection() {
        if (selection === "zoom")
            backend.deleteZoom(selectedZoom);
        else if (selection === "clip" || selection === "range")
            backend.removeRange(rangeA, rangeB);
        selection = "";
    }

    function time(t) {
        return Math.floor(t / 60) + ":" + (t % 60).toFixed(1).padStart(4, "0");
    }

    objectName: "timeline"
    implicitHeight: contents.implicitHeight
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            removeSelection();
            event.accepted = true;
        } else if (event.key === Qt.Key_C) {
            cutting = true;
            event.accepted = true;
        } else if (event.key === Qt.Key_V || event.key === Qt.Key_Escape) {
            cutting = false;
            event.accepted = true;
        }
    }

    Connections {
        function onEditChanged() {
            if (timeline.selection === "clip") {
                let pos = 0, found = false;
                for (const s of backend.edit.spans) {
                    if (s[2] === timeline.clipId) {
                        timeline.rangeA = pos;
                        timeline.rangeB = pos + s[1] - s[0];
                        found = true;
                    }
                    pos += s[1] - s[0];
                }
                if (!found)
                    timeline.selection = "";

            } else if (timeline.selection === "zoom" && timeline.selectedZoom < 0) {
                timeline.selection = "";
            }
            timeline.rangeA = Math.min(timeline.rangeA, backend.duration);
            timeline.rangeB = Math.min(timeline.rangeB, backend.duration);
        }

        target: backend
    }

    ColumnLayout {
        id: contents

        width: parent.width
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
                enabled: backend.duration > 0
                onClicked: backend.togglePlay()
            }

            Label {
                text: timeline.time(backend.position) + " / " + timeline.time(backend.duration)
                Layout.fillWidth: true
            }

            FlatButton {
                text: "Add zoom"
                enabled: backend.duration > 0
                onClicked: timeline.addZoom(backend.position)
            }

            FlatButton {
                objectName: "cutButton"
                text: "✂ Cut"
                primary: timeline.cutting
                onClicked: {
                    timeline.cutting = !timeline.cutting;
                    timeline.forceActiveFocus();
                }
            }

            FlatButton {
                text: "Select"
                primary: !timeline.cutting
                onClicked: {
                    timeline.cutting = false;
                    timeline.forceActiveFocus();
                }
            }

            FlatButton {
                text: "Keep range"
                enabled: timeline.selection === "range" || timeline.selection === "clip"
                onClicked: {
                    backend.trim(timeline.rangeA, timeline.rangeB);
                    timeline.selection = "";
                }
            }

            FlatButton {
                text: "Delete selection"
                enabled: timeline.selection !== ""
                onClicked: timeline.removeSelection()
            }

        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.preferredWidth: 80
                text: timeline.cutting ? "Cut mode" : "Time"
                color: theme.colors.dark_foreground
            }

            Item {
                id: ruler

                Layout.fillWidth: true
                height: 22

                Repeater {
                    model: 6

                    Label {
                        required property int index

                        x: index / 5 * (ruler.width - width)
                        text: timeline.time(index / 5 * backend.duration)
                        color: theme.colors.dark_foreground
                    }

                }

                MouseArea {
                    objectName: "timelineRuler"
                    anchors.fill: parent
                    onPressed: (mouse) => {
                        timeline.selection = "";
                        timeline.forceActiveFocus();
                        backend.pause();
                        backend.seek(mouse.x / width * backend.duration);
                    }
                    onPositionChanged: (mouse) => {
                        if (pressed)
                            backend.seek(Math.max(0, Math.min(1, mouse.x / width)) * backend.duration);

                    }
                }

            }

        }

        Item {
            Layout.fillWidth: true
            implicitHeight: lanes.implicitHeight

            ColumnLayout {
                id: lanes

                width: parent.width
                spacing: 3

                Repeater {
                    model: ["recording", "mic", "desktop"]

                    RowLayout {
                        id: lane

                        required property string modelData
                        property var wave: backend.waveforms[modelData]

                        visible: modelData === "recording" || wave !== undefined
                        Layout.fillWidth: true

                        Label {
                            Layout.preferredWidth: 80
                            text: lane.modelData === "recording" ? "Recording" : lane.modelData === "mic" ? "Mic" : "Desktop"
                        }

                        Rectangle {
                            id: body

                            Layout.fillWidth: true
                            height: lane.modelData === "recording" ? 36 : 30
                            color: theme.colors.lighter_background
                            border.color: theme.colors.muted

                            Repeater {
                                model: backend.edit.spans || []

                                Rectangle {
                                    required property var modelData

                                    x: backend.toEditedTime(modelData[0]) / Math.max(0.001, backend.duration) * body.width
                                    width: (modelData[1] - modelData[0]) / Math.max(0.001, backend.duration) * body.width
                                    height: body.height
                                    color: timeline.selection === "clip" && timeline.clipId === modelData[2] ? theme.colors.selection : "transparent"
                                    border.color: timeline.selection === "clip" && timeline.clipId === modelData[2] ? theme.colors.accent : theme.colors.muted
                                }

                            }

                            Canvas {
                                id: waveform

                                property var audio: lane.wave
                                property var spans: backend.edit.spans
                                property color ink: theme.colors.accent

                                anchors.fill: parent
                                visible: lane.modelData !== "recording"
                                opacity: backend.edit[lane.modelData] ? 1 : 0.3
                                onAudioChanged: requestPaint()
                                onSpansChanged: requestPaint()
                                onInkChanged: requestPaint()
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                                onPaint: {
                                    const ctx = getContext("2d");
                                    ctx.clearRect(0, 0, width, height);
                                    if (!audio || audio.status !== "ready")
                                        return ;

                                    ctx.strokeStyle = ink;
                                    ctx.lineWidth = 1;
                                    ctx.beginPath();
                                    let pos = 0;
                                    for (const s of spans) {
                                        const start = pos;
                                        pos += s[1] - s[0];
                                        const left = Math.ceil(start / backend.duration * width), right = Math.min(width, Math.ceil(pos / backend.duration * width));
                                        for (let px = left; px < right; ++px) {
                                            const a = Math.max(0, Math.floor((s[0] + px / width * backend.duration - start) / audio.step));
                                            const b = Math.min(audio.peaks.length - 1, Math.floor((s[0] + (px + 1) / width * backend.duration - start) / audio.step));
                                            let lo = 0, hi = 0;
                                            for (let i = a; i <= b; ++i) {
                                                lo = Math.min(lo, audio.peaks[i][0]);
                                                hi = Math.max(hi, audio.peaks[i][1]);
                                            }
                                            ctx.moveTo(px + 0.5, height / 2 - Math.max(0.5, hi * (height / 2 - 3)));
                                            ctx.lineTo(px + 0.5, height / 2 - Math.min(-0.5, lo * (height / 2 - 3)));
                                        }
                                    }
                                    ctx.stroke();
                                }
                            }

                            Label {
                                anchors.centerIn: parent
                                visible: lane.modelData !== "recording" && !!lane.wave && lane.wave.status !== "ready"
                                text: lane.wave && lane.wave.status === "loading" ? "Loading waveform…" : "Waveform unavailable"
                                color: theme.colors.dark_foreground
                            }

                            Rectangle {
                                visible: timeline.selection === "range"
                                x: timeline.rangeA / Math.max(0.001, backend.duration) * body.width
                                width: (timeline.rangeB - timeline.rangeA) / Math.max(0.001, backend.duration) * body.width
                                height: parent.height
                                color: theme.colors.accent
                                opacity: 0.25
                            }

                            MouseArea {
                                id: clipMouse

                                property real startX
                                property bool dragging: false

                                function at(x) {
                                    return Math.max(0, Math.min(1, x / width)) * backend.duration;
                                }

                                objectName: lane.modelData + "Timeline"
                                anchors.fill: parent
                                hoverEnabled: timeline.cutting
                                cursorShape: timeline.cutting ? Qt.CrossCursor : Qt.ArrowCursor
                                onPressed: (mouse) => {
                                    timeline.forceActiveFocus();
                                    startX = mouse.x;
                                    dragging = false;
                                }
                                onPositionChanged: (mouse) => {
                                    if (pressed && !timeline.cutting && (dragging || Math.abs(mouse.x - startX) > 5)) {
                                        if (!dragging)
                                            backend.pause();

                                        dragging = true;
                                        timeline.selection = "range";
                                        timeline.rangeA = at(Math.min(mouse.x, startX));
                                        timeline.rangeB = at(Math.max(mouse.x, startX));
                                    }
                                }
                                onReleased: (mouse) => {
                                    if (timeline.cutting) {
                                        backend.split(at(mouse.x));
                                    } else if (!dragging) {
                                        timeline.selectClip(at(mouse.x));
                                        backend.pause();
                                        backend.seek(at(mouse.x));
                                    }
                                }
                            }

                            Rectangle {
                                visible: timeline.cutting && clipMouse.containsMouse
                                x: clipMouse.mouseX
                                width: 1
                                height: parent.height
                                color: theme.colors.accent
                            }

                            Repeater {
                                model: timeline.selection === "range" ? 2 : 0

                                Rectangle {
                                    required property int index

                                    x: (index === 0 ? timeline.rangeA : timeline.rangeB) / Math.max(0.001, backend.duration) * body.width - width / 2
                                    width: 6
                                    height: parent.height
                                    color: theme.colors.accent

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.SizeHorCursor
                                        onPositionChanged: (mouse) => {
                                            if (!pressed)
                                                return ;

                                            const at = Math.max(0, Math.min(1, mapToItem(body, mouse.x, 0).x / body.width)) * backend.duration;
                                            if (parent.index === 0)
                                                timeline.rangeA = Math.min(at, timeline.rangeB);
                                            else
                                                timeline.rangeB = Math.max(at, timeline.rangeA);
                                        }
                                    }

                                }

                            }

                        }

                    }

                }

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.preferredWidth: 80
                        text: "Zooms"
                    }

                    Rectangle {
                        id: zoomTrack

                        Layout.fillWidth: true
                        height: 30
                        color: theme.colors.background
                        border.color: theme.colors.muted

                        MouseArea {
                            objectName: "zoomLane"
                            anchors.fill: parent
                            onClicked: (mouse) => {
                                return timeline.addZoom(mouse.x / width * backend.duration);
                            }
                        }

                        Repeater {
                            model: (backend.edit.zooms || []).length

                            Rectangle {
                                id: block

                                required property int index
                                property var zoomData: backend.edit.zooms[index]
                                property real start: backend.toEditedTime(zoomData.start)
                                property real end: backend.toEditedTime(zoomData.end)

                                objectName: "zoomBlock" + index
                                x: start / Math.max(0.001, backend.duration) * zoomTrack.width
                                width: Math.max(2, (end - start) / Math.max(0.001, backend.duration) * zoomTrack.width)
                                height: parent.height
                                visible: end > start
                                color: timeline.selectedZoom === index ? theme.colors.accent : theme.colors.selection
                                border.color: theme.colors.accent

                                Label {
                                    anchors.centerIn: parent
                                    text: block.zoomData.amount.toFixed(1) + "×"
                                    color: timeline.selectedZoom === block.index ? theme.colors.background : theme.colors.foreground
                                }

                                MouseArea {
                                    property real pressX
                                    property real originalStart
                                    property real originalEnd
                                    property bool dragging: false

                                    anchors.fill: parent
                                    objectName: "zoomDrag" + block.index
                                    cursorShape: Qt.SizeAllCursor
                                    onPressed: (mouse) => {
                                        timeline.selectZoom(block.index, false);
                                        pressX = mapToItem(zoomTrack, mouse.x, 0).x;
                                        originalStart = block.start;
                                        originalEnd = block.end;
                                        dragging = false;
                                    }
                                    onPositionChanged: (mouse) => {
                                        const dx = mapToItem(zoomTrack, mouse.x, 0).x - pressX;
                                        if (!pressed || (!dragging && Math.abs(dx) < 5))
                                            return ;

                                        if (!dragging)
                                            backend.beginEdit();

                                        dragging = true;
                                        const a = Math.max(0, Math.min(backend.duration - (originalEnd - originalStart), originalStart + dx / zoomTrack.width * backend.duration));
                                        const z = block.zoomData;
                                        backend.updateZoom(block.index, backend.toSourceTime(a), backend.toSourceTime(a + originalEnd - originalStart), z.x, z.y, z.amount);
                                    }
                                    onReleased: {
                                        if (dragging)
                                            backend.endEdit();
                                        else
                                            timeline.selectZoom(block.index, true);
                                    }
                                    onCanceled: {
                                        if (dragging)
                                            backend.cancelEdit();

                                    }
                                }

                                Repeater {
                                    model: 2

                                    Rectangle {
                                        required property int index

                                        width: 7
                                        height: parent.height
                                        x: index === 0 ? 0 : parent.width - width
                                        color: theme.colors.foreground
                                        opacity: 0.7

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.SizeHorCursor
                                            onPressed: {
                                                timeline.selectZoom(block.index, false);
                                                backend.beginEdit();
                                            }
                                            onPositionChanged: (mouse) => {
                                                if (!pressed)
                                                    return ;

                                                const t = backend.toSourceTime(Math.max(0, Math.min(1, mapToItem(zoomTrack, mouse.x, 0).x / zoomTrack.width)) * backend.duration);
                                                const z = block.zoomData;
                                                backend.updateZoom(block.index, parent.index === 0 ? t : z.start, parent.index === 1 ? t : z.end, z.x, z.y, z.amount);
                                            }
                                            onReleased: backend.endEdit()
                                            onCanceled: backend.cancelEdit()
                                        }

                                    }

                                }

                            }

                        }

                    }

                }

            }

            Rectangle {
                x: 85 + backend.displayPosition / Math.max(0.001, backend.duration) * (parent.width - 85)
                width: 1
                height: parent.height
                color: theme.colors.foreground
                visible: backend.duration > 0
            }

        }

    }

}
