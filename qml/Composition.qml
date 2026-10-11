import QtQuick
import QtQuick.Effects

Item {
    id: scene

    property var edit: ({
    })
    property string screenSource: ""
    property string cameraSource: ""
    property real sourceAspect: 16 / 9
    property real zoom: 1
    property real focusX: 0.5
    property real focusY: 0.5
    property var crop: edit.crop || [0, 0, 1, 1]
    property real pad: (edit.padding ?? 0.07) * width
    property real ratio: sourceAspect * crop[2] / crop[3]
    property real frameW: Math.min(width - 2 * pad, (height - 2 * pad) * ratio)
    property real frameH: frameW / ratio
    property real frameX: (width - frameW) / 2
    property real frameY: (height - frameH) / 2
    property real windowOpacity: edit.windowTransparency === "light" ? 0.96 : edit.windowTransparency === "custom" ? (edit.windowOpacity ?? 0.96) : 1
    property real cameraW: (edit.cameraSize ?? 0.18) * width
    property real cameraH: cameraW * (edit.cameraShape === "circle" ? 1 : 0.75)
    property real cameraX: Math.min(width - cameraW, Math.max(0, (edit.cameraX ?? 0.79) * width))
    property real cameraY: Math.min(height - cameraH, Math.max(0, (edit.cameraY ?? 0.75) * height))
    property real cameraRadius: edit.cameraShape === "circle" ? cameraW / 2 : Math.min(cameraW, cameraH) * (edit.cameraCorners ?? 0.12)
    // Annotations are stored in source pixels; these map them onto the frame.
    property size sourcePixels: Qt.size(1920, 1080)
    property real markScale: frameW / (crop[2] * sourcePixels.width)
    property real markX: frameX - crop[0] * sourcePixels.width * markScale
    property real markY: frameY - crop[1] * sourcePixels.height * markScale
    property alias hiddenAnnotation: marks.hiddenId

    // The area annotation `index` covers, in source pixels.
    function markBox(index) {
        return marks.boxOf(index);
    }

    width: 1920
    height: 1080

    Rectangle {
        anchors.fill: parent
        color: scene.edit.color || "#182638"
    }

    Image {
        anchors.fill: parent
        source: scene.edit.background ? "file:" + scene.edit.background : ""
        fillMode: Image.PreserveAspectCrop
        // Cover screenshot canvases larger than 1080p, but cap the decode at 4K: a very
        // tall or wide canvas would otherwise demand an enormous image.
        sourceSize: Qt.size(Math.min(3840, Math.max(1920, scene.width)), Math.min(2160, Math.max(1080, scene.height)))
        asynchronous: false
    }

    // A padded, inverse mask keeps the shadow outside the window. No opaque
    // backing may sit behind the recording when its opacity is reduced.
    Item {
        id: shadowCaster
        property real margin: 32 + scene.width * 0.01
        x: scene.frameX - margin
        y: scene.frameY - margin
        width: scene.frameW + 2 * margin
        height: scene.frameH + 2 * margin
        visible: false
        layer.enabled: true

        Rectangle {
            id: shape
            x: shadowCaster.margin
            y: shadowCaster.margin
            width: scene.frameW
            height: scene.frameH
            radius: (scene.edit.corners ?? 0.015) * scene.width
            color: "white"
        }
    }

    MultiEffect {
        source: shadowCaster
        anchors.fill: shadowCaster
        autoPaddingEnabled: false
        maskEnabled: true
        maskSource: shadowCaster
        maskInverted: true
        shadowEnabled: true
        shadowBlur: 0.6
        shadowOpacity: scene.edit.shadow ?? 0.45
        shadowVerticalOffset: scene.width * 0.01
    }

    Item {
        id: viewport

        x: scene.frameX
        y: scene.frameY
        width: scene.frameW
        height: scene.frameH
        clip: true
        visible: false

        Image {
            source: scene.screenSource
            cache: false
            width: viewport.width / scene.crop[2] * scene.zoom
            height: viewport.height / scene.crop[3] * scene.zoom
            x: -scene.crop[0] * width - Math.min(viewport.width * (scene.zoom - 1), Math.max(0, ((scene.focusX - scene.crop[0]) / scene.crop[2]) * viewport.width * scene.zoom - viewport.width / 2))
            y: -scene.crop[1] * height - Math.min(viewport.height * (scene.zoom - 1), Math.max(0, ((scene.focusY - scene.crop[1]) / scene.crop[3]) * viewport.height * scene.zoom - viewport.height / 2))
        }

    }

    Rectangle {
        id: mask

        width: viewport.width
        height: viewport.height
        radius: shape.radius
        color: "white"
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        source: viewport
        anchors.fill: viewport
        opacity: scene.windowOpacity
        maskEnabled: true
        maskSource: mask
        autoPaddingEnabled: false
    }

    // Above the window's opacity, so annotations stay fully opaque. They may overhang the
    // padding, up to the canvas edge.
    Item {
        anchors.fill: parent
        clip: true

        Annotations {
            id: marks

            annotations: scene.edit.annotations || []
            x: scene.markX
            y: scene.markY
            scale: scene.markScale
            transformOrigin: Item.TopLeft
        }

    }

    Image {
        id: cam

        source: scene.cameraSource
        cache: false
        visible: false
        fillMode: Image.PreserveAspectCrop
        width: scene.cameraW
        height: scene.cameraH
        x: scene.cameraX
        y: scene.cameraY
    }

    Rectangle {
        id: cmask

        width: cam.width
        height: cam.height
        radius: scene.cameraRadius
        color: "white"
        visible: false
        layer.enabled: true
    }

    Rectangle {
        id: cameraShadowShape

        x: cam.x
        y: cam.y
        width: cam.width
        height: cam.height
        radius: scene.cameraRadius
        color: "white"
        visible: false
    }

    MultiEffect {
        source: cameraShadowShape
        anchors.fill: cameraShadowShape
        visible: (scene.edit.camera ?? true) && scene.cameraSource !== ""
        shadowEnabled: true
        shadowBlur: 0.5
        shadowOpacity: scene.edit.cameraShadow ?? 0.45
        shadowVerticalOffset: scene.width * 0.004
    }

    MultiEffect {
        source: cam
        anchors.fill: cam
        maskEnabled: true
        maskSource: cmask
        visible: (scene.edit.camera ?? true) && scene.cameraSource !== ""
        autoPaddingEnabled: false
    }

}
