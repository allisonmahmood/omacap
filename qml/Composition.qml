import QtQuick
import QtQuick.Effects

Item {
    id: scene
    width: 1920
    height: 1080
    property var edit: ({})
    property string screenSource: ""
    property string cameraSource: ""
    property real sourceAspect: 16 / 9
    property real zoom: 1
    property real focusX: .5
    property real focusY: .5
    property var crop: edit.crop || [0, 0, 1, 1]
    property real pad: (edit.padding ?? .07) * width
    property real ratio: sourceAspect * crop[2] / crop[3]
    property real frameW: Math.min(width - 2 * pad, (height - 2 * pad) * ratio)
    property real frameH: frameW / ratio
    property real frameX: (width - frameW) / 2
    property real frameY: (height - frameH) / 2
    Rectangle {
        anchors.fill: parent
        color: scene.edit.color || "#182638"
    }
    Image {
        anchors.fill: parent
        source: scene.edit.background ? "file:" + scene.edit.background : ""
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: 1920
        asynchronous: false
    }
    Rectangle {
        id: shape
        x: scene.frameX
        y: scene.frameY
        width: scene.frameW
        height: scene.frameH
        radius: (scene.edit.corners ?? .015) * scene.width
        color: "white"
        visible: false
    }
    MultiEffect {
        source: shape
        anchors.fill: shape
        shadowEnabled: true
        shadowBlur: .6
        shadowOpacity: scene.edit.shadow ?? .45
        shadowVerticalOffset: scene.width * .01
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
        maskEnabled: true
        maskSource: mask
        autoPaddingEnabled: false
    }
    Image {
        id: cam
        source: scene.cameraSource
        cache: false
        visible: false
        fillMode: Image.PreserveAspectCrop
        width: (scene.edit.cameraSize ?? .18) * scene.width
        height: width * .75
        x: Math.min(scene.width - width, Math.max(0, (scene.edit.cameraX ?? .79) * scene.width))
        y: Math.min(scene.height - height, Math.max(0, (scene.edit.cameraY ?? .75) * scene.height))
    }
    Rectangle {
        id: cmask
        width: cam.width
        height: cam.height
        radius: shape.radius
        color: "white"
        visible: false
        layer.enabled: true
    }
    MultiEffect {
        source: cam
        anchors.fill: cam
        maskEnabled: true
        maskSource: cmask
        visible: (scene.edit.camera ?? true) && scene.cameraSource !== ""
        shadowEnabled: true
        shadowBlur: .4
        shadowOpacity: .3
    }
}
