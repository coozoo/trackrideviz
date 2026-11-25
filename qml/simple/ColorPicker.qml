import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A color chosen with a finger: swatches, how see-through, or typed as #RRGGBB / #AARRGGBB.
Dialog {
    id: picker
    property color color: "white"
    property int touch: 48
    signal picked(color color)

    modal: true
    anchors.centerIn: parent
    standardButtons: Dialog.Ok | Dialog.Cancel
    onAccepted: picked(color)

    // bright ones round the hue circle, the same darker, and greys from white to black
    readonly property var swatches: {
        const list = []
        for (let i = 0; i < 12; ++i)
            list.push(Qt.hsva(i / 12, 0.85, 1, 1))
        for (let i = 0; i < 12; ++i)
            list.push(Qt.hsva(i / 12, 0.9, 0.55, 1))
        for (let i = 0; i < 6; ++i)
            list.push(Qt.hsva(0, 0, 1 - i / 5, 1))
        return list
    }

    function hexOf(c) {
        const two = (v) => ("0" + Math.round(v * 255).toString(16)).slice(-2)
        return "#" + (c.a < 1 ? two(c.a) : "") + two(c.r) + two(c.g) + two(c.b)
    }
    function withAlpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }
    onOpened: hex.text = hexOf(color)
    onColorChanged: if (!hex.activeFocus) hex.text = hexOf(color)

    ColumnLayout {
        spacing: 12
        GridLayout {
            columns: 6
            rowSpacing: 4
            columnSpacing: 4
            Repeater {
                model: picker.swatches
                Rectangle {
                    required property color modelData
                    width: picker.touch * 0.8
                    height: picker.touch * 0.8
                    radius: 6
                    color: modelData
                    border.width: Qt.colorEqual(picker.withAlpha(modelData, 1), picker.withAlpha(picker.color, 1)) ? 3 : 1
                    border.color: border.width > 1 ? "#ffb300" : "#808080"
                    TapHandler {
                        onTapped: picker.color = picker.withAlpha(parent.modelData, picker.color.a)
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Opacity") }
            Slider {
                Layout.fillWidth: true
                from: 0
                to: 1
                value: picker.color.a
                onMoved: picker.color = picker.withAlpha(picker.color, value)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            // what it is now, over a light and a dark ground: see-through shows
            Row {
                Rectangle { width: picker.touch / 2; height: picker.touch * 0.7; color: "white"
                    Rectangle { anchors.fill: parent; color: picker.color } }
                Rectangle { width: picker.touch / 2; height: picker.touch * 0.7; color: "black"
                    Rectangle { anchors.fill: parent; color: picker.color } }
            }
            TextField {
                id: hex
                Layout.fillWidth: true
                font.family: "monospace"
                inputMethodHints: Qt.ImhNoPredictiveText
                validator: RegularExpressionValidator { regularExpression: /#?([0-9a-fA-F]{6}|[0-9a-fA-F]{8})?/ }
                onTextEdited: {
                    const t = text.startsWith("#") ? text : "#" + text
                    if (t.length === 7 || t.length === 9)
                        picker.color = t
                }
            }
        }
    }
}
