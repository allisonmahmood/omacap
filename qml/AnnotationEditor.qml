import QtQuick
import "annotations.js" as Geometry

// Draws and edits screenshot annotations over the preview. Pointer positions map through
// `composition` into source pixels. A drag is drawn live here and committed to the
// backend once, on release, so each one is a single undo step.
Item {
    id: editor

    required property Item composition
    // select, box, ellipse, arrow, line, text, step, pen, marker or redact.
    property string tool: "select"
    property color color: "#ff375f"
    // Small, medium or large: 0, 1 or 2.
    property int size: 1
    property bool textPill: true
    property string selectedId: ""
    // The live copy of the annotation being drawn, moved or resized.
    property var draft: null
    // Text being typed: its anchor and style, plus the id when it edits existing text.
    property var typing: null
    readonly property var annotations: backend.edit.annotations || []
    readonly property int selectedIndex: annotations.findIndex((a) => a.id === selectedId)
    readonly property var selected: selectedIndex >= 0 ? annotations[selectedIndex] : null
    readonly property string hiddenId: draft?.id ?? typing?.id ?? ""
    // Preview pixels per source pixel, and where source (0, 0) lands in the preview.
    readonly property real k: composition.markScale * composition.scale
    readonly property real originX: composition.markX * composition.scale
    readonly property real originY: composition.markY * composition.scale
    // Sizes follow the screenshot's resolution, so marks read the same on any display.
    readonly property real unit: Math.max(1, Math.min(backend.sourceSize.width, backend.sourceSize.height) / 900)
    // Annotation::fromJson accepts at most this many points in a stroke.
    property int maxPoints: 5000

    function sizeFor(kind, index) {
        const steps = kind === "text" ? [18, 26, 38] : kind === "step" ? [28, 40, 56] : kind === "marker" ? [12, 18, 28] : [2, 4, 8];
        return steps[index] * unit;
    }

    function toSource(x, y) {
        return [(x - originX) / k, (y - originY) / k];
    }

    // The topmost annotation under source point `p`, or -1.
    function indexAt(p) {
        for (let i = annotations.length - 1; i >= 0; i--)
            if (Geometry.hit(annotations[i], composition.markBox(i), p[0], p[1], 6 / k))
                return i;
        return -1;
    }

    // The selection's resize handle under preview point (x, y), or -1.
    function handleAt(x, y) {
        const points = selected ? Geometry.handles(selected) : [];
        for (let i = 0; i < points.length; i++)
            if (Math.abs(originX + points[i][0] * k - x) <= 7 && Math.abs(originY + points[i][1] * k - y) <= 7)
                return i;
        return -1;
    }

    // The backend stores what it's given; a live copy's step number is display-only.
    function stored(a) {
        const copy = Object.assign({}, a);
        delete copy.number;
        return copy;
    }

    // Applies a style change to the selection, if there is one.
    function restyle(change) {
        if (selected)
            backend.updateAnnotation(selectedId, Object.assign({}, selected, change));
    }

    function remove() {
        if (!selected)
            return;
        backend.deleteAnnotation(selectedId);
        selectedId = "";
    }

    function nudge(dx, dy) {
        if (selected)
            backend.updateAnnotation(selectedId, Geometry.moved(selected, dx, dy));
    }

    function editText(anchor, existing) {
        selectedId = "";
        typing = existing ? {
            "id": existing.id,
            "anchor": existing.points[0],
            "size": existing.size,
            "color": existing.color,
            "pill": existing.pill
        } : {
            "anchor": anchor,
            "size": sizeFor("text", size),
            "color": color.toString(),
            "pill": textPill
        };
        input.text = existing ? existing.text : "";
        input.cursorPosition = input.length;
        input.forceActiveFocus();
    }

    // Empty text removes the annotation it was editing.
    function commitText() {
        const t = typing;
        if (!t)
            return;
        typing = null;
        input.focus = false;
        const text = input.text.replace(/\s+$/, "");
        const a = {
            "kind": "text",
            "points": [t.anchor],
            "text": text,
            "size": t.size,
            "color": t.color,
            "pill": t.pill
        };
        if (t.id && text)
            backend.updateAnnotation(t.id, a);
        else if (t.id)
            backend.deleteAnnotation(t.id);
        else if (text)
            backend.addAnnotation(a);
        tool = "select";
    }

    // Escape finishes typing, cancels a drag, leaves a drawing tool, then clears the selection.
    // A cancelled drag stays cancelled until the next press.
    function cancel() {
        if (typing) {
            commitText();
        } else if (draft || pointer.start) {
            draft = null;
            pointer.start = null;
            pointer.handle = -1;
        } else if (tool !== "select") {
            tool = "select";
        } else {
            selectedId = "";
        }
    }

    clip: true

    Connections {
        function onChanged() {
            if (backend.phase !== "editor" && backend.phase !== "exporting") {
                editor.draft = null;
                editor.typing = null;
                editor.selectedId = "";
            }
        }

        target: backend
    }

    MouseArea {
        id: pointer

        // Where the drag started, in source pixels; the annotation before it; the handle.
        property var origin: null
        property var start: null
        property int handle: -1
        property bool hovering: false

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        cursorShape: editor.tool === "text" ? Qt.IBeamCursor : editor.tool !== "select" ? Qt.CrossCursor : hovering ? Qt.SizeAllCursor : Qt.ArrowCursor
        onPressed: (mouse) => {
            editor.commitText();
            if (mouse.button === Qt.RightButton) {
                editor.draft = null;
                start = null;
                editor.tool = "select";
                return ;
            }
            const p = editor.toSource(mouse.x, mouse.y);
            origin = p;
            if (editor.tool === "select") {
                handle = editor.handleAt(mouse.x, mouse.y);
                if (handle < 0) {
                    const i = editor.indexAt(p);
                    editor.selectedId = i >= 0 ? editor.annotations[i].id : "";
                }
                start = editor.selected;
                return ;
            }
            if (editor.tool === "text") {
                editor.editText(p, null);
                return ;
            }
            editor.selectedId = "";
            const kind = editor.tool;
            editor.draft = {
                "kind": kind,
                "points": kind === "step" || kind === "pen" || kind === "marker" ? [p] : [p, p],
                "color": editor.color.toString(),
                "size": editor.sizeFor(kind, editor.size),
                "fill": "outline",
                "number": Geometry.stepNumber(editor.annotations, editor.annotations.length) + 1
            };
        }
        onPositionChanged: (mouse) => {
            if (!pressed) {
                hovering = editor.tool === "select" && (editor.handleAt(mouse.x, mouse.y) >= 0 || editor.indexAt(editor.toSource(mouse.x, mouse.y)) >= 0);
                return ;
            }
            const p = editor.toSource(mouse.x, mouse.y), square = mouse.modifiers & Qt.ShiftModifier;
            if (editor.tool === "select") {
                // A click that wobbles a pixel selects without moving.
                if (!start || (!editor.draft && Math.hypot(p[0] - origin[0], p[1] - origin[1]) * editor.k < 3))
                    return ;
                const next = handle >= 0 ? Geometry.resized(start, handle, p, square) : Geometry.moved(start, p[0] - origin[0], p[1] - origin[1]);
                if (start.kind === "step")
                    next.number = Geometry.stepNumber(editor.annotations, editor.selectedIndex);
                editor.draft = next;
                return ;
            }
            const d = editor.draft;
            if (!d)
                return ;
            if (d.kind === "step") {
                editor.draft = Object.assign({}, d, {
                    "points": [p]
                });
            } else if (d.kind === "pen" || d.kind === "marker") {
                const last = d.points[d.points.length - 1];
                if (d.points.length < editor.maxPoints && Math.hypot(p[0] - last[0], p[1] - last[1]) * editor.k >= 2)
                    editor.draft = Object.assign({}, d, {
                        "points": d.points.concat([p])
                    });
            } else {
                editor.draft = Object.assign({}, d, {
                    "points": [d.points[0], square ? Geometry.constrained(d.kind, d.points[0], p) : p]
                });
            }
        }
        onReleased: {
            const d = editor.draft;
            if (editor.tool === "select") {
                if (d && start)
                    backend.updateAnnotation(start.id, editor.stored(d));
                editor.draft = null;
                start = null;
                handle = -1;
                return ;
            }
            if (!d)
                return ;
            let a = editor.stored(d);
            // A click places a step or a pen dot; other kinds need a drag.
            if (a.points.length === 1 && a.kind !== "step")
                a.points = [a.points[0], a.points[0]];
            const first = a.points[0], last = a.points[a.points.length - 1];
            if (a.kind === "step" || a.kind === "pen" || a.kind === "marker" || Math.hypot(last[0] - first[0], last[1] - first[1]) * editor.k >= 4)
                backend.addAnnotation(a);
            editor.draft = null;
            editor.tool = "select";
        }
        onDoubleClicked: (mouse) => {
            if (editor.tool === "select" && editor.selected?.kind === "text")
                editor.editText(null, editor.selected);
        }
    }

    // The live copy, drawn exactly as the composition will draw it once committed.
    Annotations {
        id: live

        annotations: editor.draft ? [editor.draft] : []
        x: editor.originX
        y: editor.originY
        scale: editor.k
        transformOrigin: Item.TopLeft
    }

    Item {
        id: selection

        readonly property var shown: editor.draft?.id ? editor.draft : editor.selected
        readonly property rect area: shown ? Geometry.bounds(shown, editor.draft ? live.boxOf(0) : editor.composition.markBox(editor.selectedIndex)) ?? Qt.rect(0, 0, 0, 0) : Qt.rect(0, 0, 0, 0)

        visible: shown !== null && editor.typing === null

        Rectangle {
            x: editor.originX + selection.area.x * editor.k - 4
            y: editor.originY + selection.area.y * editor.k - 4
            width: selection.area.width * editor.k + 8
            height: selection.area.height * editor.k + 8
            color: "transparent"
            border.color: theme.colors.accent
            border.width: 1
        }

        Repeater {
            model: selection.shown ? Geometry.handles(selection.shown) : []

            Rectangle {
                required property var modelData

                x: editor.originX + modelData[0] * editor.k - 5
                y: editor.originY + modelData[1] * editor.k - 5
                width: 10
                height: 10
                color: "#ffffff"
                border.color: theme.colors.accent
                border.width: 2
            }

        }

    }

    // Text is typed in place, scaled and styled like the annotation it becomes.
    Item {
        visible: editor.typing !== null
        x: editor.originX + (editor.typing?.anchor[0] ?? 0) * editor.k
        y: editor.originY + (editor.typing?.anchor[1] ?? 0) * editor.k
        scale: editor.k
        transformOrigin: Item.TopLeft

        Rectangle {
            id: field

            readonly property real fontSize: editor.typing?.size ?? 20
            readonly property bool pill: editor.typing?.pill ?? true
            readonly property color tone: editor.typing?.color ?? editor.color
            readonly property real padX: fontSize * 0.4
            readonly property real padY: fontSize * 0.18

            width: Math.max(input.contentWidth, fontSize * 0.5) + 2 * padX
            height: input.contentHeight + 2 * padY
            radius: Math.min(height / 2, fontSize * 0.7)
            color: pill ? tone : "transparent"
            border.color: pill ? "transparent" : theme.colors.accent
            border.width: 1 / editor.k

            TextEdit {
                id: input

                x: field.padX
                y: field.padY
                font.pixelSize: field.fontSize
                font.bold: true
                color: !field.pill ? field.tone : 0.299 * field.tone.r + 0.587 * field.tone.g + 0.114 * field.tone.b > 0.6 ? "#1d1b16" : "#ffffff"
                selectByMouse: true
                // Enter finishes; Shift+Enter starts a new line.
                Keys.onPressed: (event) => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                        editor.commitText();
                        event.accepted = true;
                    }
                }

                cursorDelegate: Rectangle {
                    width: Math.max(2, 2 / editor.k)
                    color: input.color
                }

            }

        }

    }

}
