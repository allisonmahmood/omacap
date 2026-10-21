import QtQuick
import QtQuick.Layouts

// The timeline in the editor's dock: transport and edit actions, the time ruler, the
// recording and audio lanes with waveforms, and the zooms lane. Drag across a lane to select
// a range; click to select a clip (or split one, in cut mode); click the zooms lane to add a
// zoom, and drag a zoom or its ends to time it.
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

    // Splits every track at the playhead.
    function splitAtPlayhead() {
        backend.split(backend.position);
    }

    function keepRange() {
        backend.trim(rangeA, rangeB);
        selection = "";
    }

    // The width of the lane names, and the gap after them.
    readonly property real labelWidth: 62
    readonly property real gap: 8

    objectName: "timeline"
    implicitHeight: contents.implicitHeight

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
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            ChromeButton {
                objectName: "playButton"
                icon: backend.playing ? "pause" : "play"
                tooltip: (backend.playing ? "Pause" : "Play") + " · Space"
                enabled: backend.duration > 0
                onClicked: backend.togglePlay()
            }

            Text {
                text: timeline.time(backend.position) + " / " + timeline.time(backend.duration)
                font: theme.mono(12)
                color: theme.alpha(theme.foreground, 217)
                Layout.leftMargin: 8
                Layout.fillWidth: true
            }

            ChromeChip {
                icon: "scissors"
                text: "Split"
                key: "S"
                tooltip: "Split at the playhead"
                enabled: backend.duration > 0
                onClicked: timeline.splitAtPlayhead()
            }

            ChromeChip {
                icon: "keep"
                text: "Keep range"
                tooltip: "Trim to the selected range or clip"
                enabled: timeline.selection === "range" || timeline.selection === "clip"
                onClicked: timeline.keepRange()
            }

            ChromeChip {
                icon: "trash"
                text: "Delete"
                key: "Del"
                tooltip: "Delete the selected clip, range or zoom"
                enabled: timeline.selection !== ""
                onClicked: timeline.removeSelection()
            }

            ChromeDivider {
                implicitHeight: 28
            }

            ChromeChip {
                icon: "plus"
                text: "Zoom"
                key: "Z"
                tooltip: "Add a zoom at the playhead"
                enabled: backend.duration > 0
                onClicked: timeline.addZoom(backend.position)
            }

            ChromeDivider {
                visible: mute.visible
                implicitHeight: 28
            }

            Row {
                id: mute

                visible: backend.waveforms.mic !== undefined || backend.waveforms.desktop !== undefined
                spacing: 4

                ChromeButton {
                    objectName: "micToggle"
                    visible: backend.waveforms.mic !== undefined
                    width: 30
                    height: 30
                    radius: 7
                    icon: (backend.edit.mic ?? true) ? "mic" : "mic-off"
                    selected: backend.edit.mic ?? true
                    tooltip: (backend.edit.mic ?? true) ? "Microphone on · click to mute" : "Microphone muted"
                    onClicked: backend.setValue("mic", !(backend.edit.mic ?? true))
                }

                ChromeButton {
                    objectName: "desktopToggle"
                    visible: backend.waveforms.desktop !== undefined
                    width: 30
                    height: 30
                    radius: 7
                    icon: (backend.edit.desktop ?? true) ? "speaker" : "speaker-off"
                    selected: backend.edit.desktop ?? true
                    tooltip: (backend.edit.desktop ?? true) ? "Desktop audio on · click to mute" : "Desktop audio muted"
                    onClicked: backend.setValue("desktop", !(backend.edit.desktop ?? true))
                }

            }

        }

        RowLayout {
            Layout.fillWidth: true
            spacing: timeline.gap

            Text {
                Layout.preferredWidth: timeline.labelWidth
                text: timeline.cutting ? "Cut mode" : ""
                font: theme.font(11, true)
                color: theme.accent
            }

            Item {
                id: ruler

                // The roundest interval that gives at most six ticks.
                readonly property real tick: [0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600].find((step) => {
                    return backend.duration / step <= 6;
                }) ?? 1200

                Layout.fillWidth: true
                height: 18

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: theme.alpha(theme.foreground, 30)
                }

                Repeater {
                    model: backend.duration > 0 ? Math.floor(backend.duration / ruler.tick + 1e-6) + 1 : 0

                    Text {
                        required property int index
                        readonly property real at: index * ruler.tick

                        x: Math.max(0, Math.min(ruler.width - width, at / backend.duration * ruler.width - width / 2))
                        text: Math.floor(at / 60) + ":" + (ruler.tick < 1 ? (at % 60).toFixed(1).padStart(4, "0") : Math.floor(at % 60).toString().padStart(2, "0"))
                        font: theme.mono(10)
                        color: theme.alpha(theme.foreground, 128)
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
                spacing: 6

                Repeater {
                    model: ["recording", "mic", "desktop"]

                    RowLayout {
                        id: lane

                        required property string modelData
                        property var wave: backend.waveforms[modelData]

                        visible: modelData === "recording" || wave !== undefined
                        Layout.fillWidth: true
                        spacing: timeline.gap

                        Text {
                            Layout.preferredWidth: timeline.labelWidth
                            text: lane.modelData === "recording" ? "Video" : lane.modelData === "mic" ? "Mic" : "Desktop"
                            font: theme.font(12)
                            color: theme.alpha(theme.foreground, 180)
                        }

                        Rectangle {
                            id: body

                            Layout.fillWidth: true
                            height: lane.modelData === "recording" ? 26 : 22
                            radius: 4
                            color: theme.alpha(theme.foreground, 10)
                            border.color: theme.alpha(theme.foreground, 20)

                            Repeater {
                                model: lane.modelData === "recording" ? backend.edit.spans || [] : []

                                Rectangle {
                                    required property var modelData
                                    readonly property bool chosen: timeline.selection === "clip" && timeline.clipId === modelData[2]

                                    x: backend.toEditedTime(modelData[0]) / Math.max(0.001, backend.duration) * body.width + 1
                                    width: (modelData[1] - modelData[0]) / Math.max(0.001, backend.duration) * body.width - 2
                                    height: body.height
                                    radius: 4
                                    color: chosen ? theme.buttonSelected : theme.button
                                    border.color: chosen ? theme.accent : theme.alpha(theme.foreground, 64)
                                }

                            }

                            // Clip boundaries on the audio lanes, which stay linked to the video.
                            Repeater {
                                model: lane.modelData === "recording" ? [] : (backend.edit.spans || []).slice(1)

                                Rectangle {
                                    required property var modelData

                                    x: backend.toEditedTime(modelData[0]) / Math.max(0.001, backend.duration) * body.width
                                    width: 1
                                    height: body.height
                                    color: theme.alpha(theme.foreground, 64)
                                }

                            }

                            Canvas {
                                id: waveform

                                property var audio: lane.wave
                                property var spans: backend.edit.spans
                                // Muted tracks draw grey.
                                property color ink: (backend.edit[lane.modelData] ?? true) ? theme.accent : theme.alpha(theme.foreground, 64)

                                anchors.fill: parent
                                visible: lane.modelData !== "recording"
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

                            Text {
                                anchors.centerIn: parent
                                visible: lane.modelData !== "recording" && !!lane.wave && lane.wave.status !== "ready"
                                text: lane.wave && lane.wave.status === "loading" ? "Loading waveform…" : "Waveform unavailable"
                                font: theme.font(11)
                                color: theme.alpha(theme.foreground, 128)
                            }

                            Rectangle {
                                visible: timeline.selection === "range"
                                x: timeline.rangeA / Math.max(0.001, backend.duration) * body.width
                                width: (timeline.rangeB - timeline.rangeA) / Math.max(0.001, backend.duration) * body.width
                                height: parent.height
                                color: theme.alpha(theme.accent, 64)
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
                                color: theme.accent
                            }

                            Repeater {
                                model: timeline.selection === "range" ? 2 : 0

                                Rectangle {
                                    required property int index

                                    x: (index === 0 ? timeline.rangeA : timeline.rangeB) / Math.max(0.001, backend.duration) * body.width - width / 2
                                    width: 6
                                    height: parent.height
                                    radius: 2
                                    color: theme.accent

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
                    spacing: timeline.gap

                    Text {
                        Layout.preferredWidth: timeline.labelWidth
                        text: "Zooms"
                        font: theme.font(12)
                        color: theme.alpha(theme.foreground, 180)
                    }

                    Rectangle {
                        id: zoomTrack

                        Layout.fillWidth: true
                        height: 22
                        radius: 4
                        color: theme.alpha(theme.foreground, 10)
                        border.color: theme.alpha(theme.foreground, 20)

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

                                readonly property bool chosen: timeline.selectedZoom === index

                                objectName: "zoomBlock" + index
                                x: start / Math.max(0.001, backend.duration) * zoomTrack.width
                                width: Math.max(2, (end - start) / Math.max(0.001, backend.duration) * zoomTrack.width)
                                height: parent.height
                                visible: end > start
                                radius: 4
                                color: chosen ? theme.buttonSelected : theme.button
                                border.color: chosen ? theme.accent : theme.alpha(theme.foreground, 77)

                                Text {
                                    anchors.centerIn: parent
                                    text: block.zoomData.amount.toFixed(1) + "×"
                                    font: theme.font(10, true)
                                    color: block.chosen ? theme.accent : theme.alpha(theme.foreground, 204)
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
                                        color: "transparent"

                                        // Resize grips: accent bars on the selected zoom, faint otherwise.
                                        Rectangle {
                                            x: parent.parent.index === 0 ? 1 : parent.width - width - 1
                                            y: 3
                                            width: 3
                                            height: parent.height - 6
                                            radius: 1.5
                                            color: block.chosen ? theme.accent : theme.alpha(theme.foreground, 64)
                                        }

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

            // The playhead, from the ruler down through the lanes.
            Rectangle {
                x: timeline.labelWidth + timeline.gap + backend.displayPosition / Math.max(0.001, backend.duration) * (parent.width - timeline.labelWidth - timeline.gap)
                y: -ruler.height - contents.spacing
                width: 1.5
                height: parent.height - y
                color: theme.foreground
                visible: backend.duration > 0

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 10
                    height: 8
                    radius: 2.5
                    color: theme.foreground
                }

            }

        }

    }

}
