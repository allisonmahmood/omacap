import QtQuick
import QtQuick.Layouts
import OmaCap

// The backdrop behind the recording: the Omarchy wallpaper, an image, or a colour.
Popover {
    id: popover

    // Backend keeps the wallpaper and a chosen image as session copies, named for their source.
    readonly property string background: backend.edit.background || ""
    readonly property string kind: background.indexOf("/wallpaper-") >= 0 ? "wallpaper" : background !== "" ? "image" : "color"

    title: "Backdrop"
    key: "B"

    component Swatch: Item {
        id: swatch

        property string kind
        property string tooltip
        property alias picture: picture.source
        property color fill: theme.button
        property string icon
        readonly property bool chosen: popover.kind === kind

        signal clicked()

        objectName: "backdrop-" + kind
        implicitWidth: 44
        implicitHeight: 44

        Rectangle {
            anchors.fill: parent
            radius: 9
            color: "transparent"
            border.width: 2
            border.color: swatch.chosen ? theme.accent : "transparent"
        }

        Rectangle {
            id: face

            anchors.fill: parent
            anchors.margins: 4
            radius: 6
            color: swatch.fill
            border.color: theme.alpha(theme.foreground, 64)
            clip: true

            Image {
                id: picture

                anchors.fill: parent
                anchors.margins: 1
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(96, 96)
                asynchronous: true
            }

            ChromeIcon {
                visible: swatch.icon !== ""
                anchors.fill: parent
                size: 15
                name: swatch.icon
                color: theme.buttonText
            }

        }

        MouseArea {
            id: mouse

            anchors.fill: parent
            hoverEnabled: true
            onClicked: swatch.clicked()
        }

        ChromeTooltip {
            text: swatch.tooltip
            shown: mouse.containsMouse
        }

    }

    RowLayout {
        spacing: 6

        Swatch {
            kind: "wallpaper"
            tooltip: "Omarchy wallpaper"
            picture: popover.kind === "wallpaper" ? "file:" + popover.background : ""
            icon: popover.kind === "wallpaper" ? "" : "background"
            onClicked: backend.useWallpaper()
        }

        Swatch {
            kind: "image"
            tooltip: "Image…"
            picture: popover.kind === "image" ? "file:" + popover.background : ""
            icon: popover.kind === "image" ? "" : "plus"
            onClicked: stage.chooseBackground()
        }

        Swatch {
            kind: "color"
            tooltip: "Colour…"
            fill: backend.edit.color || "#182638"
            onClicked: stage.chooseColor()
        }

        Text {
            text: popover.kind === "wallpaper" ? "Wallpaper" : popover.kind === "image" ? "Image" : "Colour " + (backend.edit.color || "")
            font: theme.font(12)
            color: theme.alpha(theme.foreground, 180)
            Layout.leftMargin: 6
        }

    }

}
