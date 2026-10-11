.pragma library

// Geometry for screenshot annotations. Everything is in source-image pixels; an annotation
// is { kind, points: [[x, y], ...], color, size, fill, text, pill, id }.

// Kinds drawn with two points, and those that can be resized by their handles.
const pairs = ["box", "ellipse", "arrow", "line", "redact"];
const corners = ["box", "ellipse", "redact"];

function isVector(a) {
    return ["box", "ellipse", "arrow", "line", "pen", "marker"].includes(a.kind);
}

// The rectangle spanned by a two-point annotation's corners.
function span(a) {
    const [p, q] = a.points;
    return Qt.rect(Math.min(p[0], q[0]), Math.min(p[1], q[1]), Math.abs(q[0] - p[0]), Math.abs(q[1] - p[1]));
}

// Arrowheads grow with the stroke so thick arrows keep their shape.
function headLength(a) {
    return Math.max(a.size * 4, 10);
}

// The outline of a vector annotation as an SVG path. Arrows leave their head to head().
function path(a) {
    const p = a.points;
    switch (a.kind) {
    case "box": {
        const r = span(a), c = Math.min(a.size * 2, r.width / 2, r.height / 2);
        if (c < 0.5)
            return `M${r.x} ${r.y}h${r.width}v${r.height}h${-r.width}Z`;
        const arc = (x, y) => `A${c} ${c} 0 0 1 ${x} ${y}`;
        return `M${r.x + c} ${r.y}H${r.x + r.width - c}` + arc(r.x + r.width, r.y + c) + `V${r.y + r.height - c}` + arc(r.x + r.width - c, r.y + r.height) + `H${r.x + c}` + arc(r.x, r.y + r.height - c) + `V${r.y + c}` + arc(r.x + c, r.y) + "Z";
    }
    case "ellipse": {
        const r = span(a), rx = Math.max(r.width / 2, 0.5), ry = Math.max(r.height / 2, 0.5);
        const cx = r.x + r.width / 2, cy = r.y + r.height / 2;
        return `M${cx - rx} ${cy}A${rx} ${ry} 0 1 0 ${cx + rx} ${cy}A${rx} ${ry} 0 1 0 ${cx - rx} ${cy}Z`;
    }
    case "line":
        return `M${p[0][0]} ${p[0][1]}L${p[1][0]} ${p[1][1]}`;
    case "arrow": {
        // The shaft stops inside the head so its round cap never pokes out of the tip.
        const dx = p[1][0] - p[0][0], dy = p[1][1] - p[0][1], length = Math.hypot(dx, dy);
        const back = Math.min(length, headLength(a) * 0.7);
        if (length < 0.5)
            return "";
        return `M${p[0][0]} ${p[0][1]}L${p[1][0] - dx / length * back} ${p[1][1] - dy / length * back}`;
    }
    case "pen":
    case "marker": {
        // Quadratic curves through the midpoints smooth the pointer's corners.
        let d = `M${p[0][0]} ${p[0][1]}`;
        for (let i = 1; i < p.length - 1; i++)
            d += `Q${p[i][0]} ${p[i][1]} ${(p[i][0] + p[i + 1][0]) / 2} ${(p[i][1] + p[i + 1][1]) / 2}`;
        const last = p[p.length - 1];
        return d + `L${last[0]} ${last[1]}`;
    }
    }
    return "";
}

// A filled arrowhead with its tip on the end point.
function head(a) {
    if (a.kind !== "arrow")
        return "";
    const [p, q] = a.points, dx = q[0] - p[0], dy = q[1] - p[1], length = Math.hypot(dx, dy);
    if (length < 0.5)
        return "";
    const h = headLength(a), ux = dx / length, uy = dy / length, w = h * 0.55;
    const bx = q[0] - ux * h, by = q[1] - uy * h;
    return `M${q[0]} ${q[1]}L${bx - uy * w} ${by + ux * w}L${bx + uy * w} ${by - ux * w}Z`;
}

// Steps count up in drawing order, so deleting one renumbers the rest.
function stepNumber(list, index) {
    let n = 0;
    for (let i = 0; i <= index && i < list.length; i++)
        if (list[i].kind === "step")
            n++;
    return n;
}

// The area an annotation covers. Text and steps are measured by their drawn items.
function bounds(a, drawn) {
    if (!pairs.includes(a.kind) && !["pen", "marker"].includes(a.kind))
        return drawn;
    const xs = a.points.map(p => p[0]), ys = a.points.map(p => p[1]);
    const grow = a.kind === "redact" ? 0 : a.kind === "arrow" ? headLength(a) * 0.55 : a.size / 2;
    const x = Math.min(...xs) - grow, y = Math.min(...ys) - grow;
    return Qt.rect(x, y, Math.max(...xs) + grow - x, Math.max(...ys) + grow - y);
}

function distanceToSegment(x, y, p, q) {
    const dx = q[0] - p[0], dy = q[1] - p[1], squared = dx * dx + dy * dy;
    const t = squared ? Math.max(0, Math.min(1, ((x - p[0]) * dx + (y - p[1]) * dy) / squared)) : 0;
    return Math.hypot(x - p[0] - t * dx, y - p[1] - t * dy);
}

// Whether (x, y) touches an annotation, within `slack` source pixels.
function hit(a, drawn, x, y, slack) {
    if (["line", "arrow", "pen", "marker"].includes(a.kind)) {
        const reach = a.size / 2 + slack + (a.kind === "arrow" ? headLength(a) * 0.3 : 0);
        for (let i = 1; i < a.points.length; i++)
            if (distanceToSegment(x, y, a.points[i - 1], a.points[i]) <= reach)
                return true;
        return false;
    }
    const r = bounds(a, drawn);
    return !!r && x >= r.x - slack && x <= r.x + r.width + slack && y >= r.y - slack && y <= r.y + r.height + slack;
}

function moved(a, dx, dy) {
    return Object.assign({}, a, {
        "points": a.points.map(p => [p[0] + dx, p[1] + dy])
    });
}

// Resize handles: the corners of boxes, ellipses and redactions, and the ends of lines
// and arrows. Other kinds only move.
function handles(a) {
    if (corners.includes(a.kind)) {
        const r = span(a);
        return [[r.x, r.y], [r.x + r.width, r.y], [r.x + r.width, r.y + r.height], [r.x, r.y + r.height]];
    }
    return ["line", "arrow"].includes(a.kind) ? a.points : [];
}

// Holding Shift draws squares and circles, and snaps lines to 45°.
function constrained(kind, from, to) {
    const dx = to[0] - from[0], dy = to[1] - from[1];
    if (corners.includes(kind)) {
        const side = Math.max(Math.abs(dx), Math.abs(dy));
        return [from[0] + Math.sign(dx || 1) * side, from[1] + Math.sign(dy || 1) * side];
    }
    const angle = Math.round(Math.atan2(dy, dx) / (Math.PI / 4)) * (Math.PI / 4), length = Math.hypot(dx, dy);
    return [from[0] + Math.cos(angle) * length, from[1] + Math.sin(angle) * length];
}

// Drags handle `index` to `to`. Corners keep the opposite corner fixed.
function resized(a, index, to, square) {
    let points;
    if (corners.includes(a.kind)) {
        const fixed = handles(a)[(index + 2) % 4];
        points = [fixed, square ? constrained(a.kind, fixed, to) : to];
    } else {
        const fixed = a.points[1 - index], end = square ? constrained(a.kind, fixed, to) : to;
        points = index ? [fixed, end] : [end, fixed];
    }
    return Object.assign({}, a, {
        "points": points
    });
}
