.pragma library

// The export choices, shared by the editor's Export popover and the card's Export. Choices
// are remembered as indices into these lists, per format, under export/*.
const formats = ["MP4", "GIF"];
const widths = [["1920", "1280", "2560", "3840"], ["640", "960", "1280", "480"]];
const rates = [["30", "60"], ["15", "10", "20", "30"]];
const qualities = ["High", "Balanced", "Smaller file"];
const crf = [18, 23, 28];

function remembered(backend, key, count) {
    const index = Number(backend.preference(key, 0));
    return index >= 0 && index < count ? index : 0;
}

// Each format remembers its own width and frame rate.
function widthIndex(backend, format) {
    return remembered(backend, "export/widthIndex" + format, widths[format].length);
}

function rateIndex(backend, format) {
    return remembered(backend, "export/fpsIndex" + format, rates[format].length);
}

// The remembered settings, as VideoStage's chooseExport and exportNow take them.
function current(backend) {
    const format = remembered(backend, "export/format", formats.length);
    return {
        "gif": format === 1,
        "width": Number(widths[format][widthIndex(backend, format)]),
        "fps": Number(rates[format][rateIndex(backend, format)]),
        "quality": crf[remembered(backend, "export/quality", qualities.length)]
    };
}
