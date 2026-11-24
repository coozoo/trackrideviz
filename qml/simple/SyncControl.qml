import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The data in step with the picture: - and + by a tenth of a second (held: again and again),
// a wheel dragged for anything between, and the offset itself, tapped to type it.
RowLayout {
    id: sync
    spacing: 2
    property int touch: 48

    function offsetText(seconds) {
        return (seconds > 0 ? "+" : seconds < 0 ? "−" : "") + Math.abs(seconds).toFixed(2) + " s"
    }

    Label {
        text: qsTr("Sync")
        opacity: 0.7
        Layout.rightMargin: 4
    }
    ToolButton {
        Layout.preferredWidth: sync.touch
        Layout.preferredHeight: sync.touch
        text: "−"
        font.pixelSize: 20
        autoRepeat: true
        onClicked: simple.nudgeSync(-0.1)
    }
    // the wheel: its lines run under the finger, a hundredth of a second for each pixel
    Rectangle {
        id: wheel
        Layout.preferredWidth: 120
        Layout.preferredHeight: sync.touch * 0.7
        radius: height / 2
        color: palette.base
        border.color: palette.mid
        clip: true
        readonly property real spacing: 10
        // where the lines stand: they move with the offset
        readonly property real phase: ((simple.syncOffset * 100) % spacing + spacing) % spacing
        Repeater {
            model: Math.ceil(wheel.width / wheel.spacing) + 1
            Rectangle {
                required property int index
                x: index * wheel.spacing + wheel.phase - wheel.spacing
                y: wheel.height * 0.2
                width: 1
                height: wheel.height * 0.6
                color: palette.text
                // fainter towards the edges, as on a wheel seen from the side
                opacity: 0.15 + 0.6 * (1 - Math.abs(x - wheel.width / 2) / (wheel.width / 2))
            }
        }
        DragHandler {
            id: drag
            target: null
            xAxis.enabled: true
            yAxis.enabled: false
            property real last: 0
            onActiveChanged: last = 0
            // only while dragged: the translation goes back to 0 at the release
            onTranslationChanged: {
                if (active) {
                    simple.nudgeSync((translation.x - last) * 0.01)
                    last = translation.x
                }
            }
        }
    }
    ToolButton {
        Layout.preferredWidth: sync.touch
        Layout.preferredHeight: sync.touch
        text: "+"
        font.pixelSize: 20
        autoRepeat: true
        onClicked: simple.nudgeSync(0.1)
    }
    ToolButton {
        Layout.preferredHeight: sync.touch
        text: sync.offsetText(simple.syncOffset)
        font.family: "monospace"
        ToolTip.visible: hovered
        ToolTip.text: simple.syncText
        onClicked: {
            typed.text = simple.syncOffset.toFixed(2)
            typing.open()
        }
    }

    Dialog {
        id: typing
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Data offset, seconds")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: typed.forceActiveFocus()
        onAccepted: {
            const seconds = Number(typed.text.replace(",", "."))
            if (!isNaN(seconds))
                simple.setSyncOffset(seconds)
        }
        ColumnLayout {
            anchors.fill: parent
            TextField {
                id: typed
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: RegularExpressionValidator { regularExpression: /-?[0-9]*([.,][0-9]*)?/ }
                onAccepted: typing.accept()
            }
            Label {
                Layout.fillWidth: true
                Layout.preferredWidth: 280
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("More when the gauges are late against the picture, less when they are early.")
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("By the clocks")
                onClicked: {
                    simple.syncByClocks()
                    typing.close()
                }
            }
        }
    }
}
