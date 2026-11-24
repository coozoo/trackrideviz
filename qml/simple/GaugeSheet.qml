import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The properties of one gauge, as the full window's dock has them: what it shows, how big it
// is, its letters, how see-through, where its scale ends; and taking it off.
Drawer {
    id: sheet
    property int index: -1
    property var props: ({})
    property int touch: 48
    edge: Qt.RightEdge

    function refresh() {
        props = index >= 0 ? simple.gaugeProperties(index) : ({})
    }
    onOpened: refresh()
    Connections {
        target: simple
        function onOverlayChanged() {
            if (sheet.opened)
                sheet.refresh()
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 8
            Label {
                Layout.fillWidth: true
                Layout.margins: 16
                font.bold: true
                elide: Text.ElideRight
                text: sheet.props.name || ""
            }
            Label { Layout.leftMargin: 16; text: qsTr("Shows") }
            ComboBox {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.preferredHeight: sheet.touch
                model: sheet.props.channelNames || []
                currentIndex: (sheet.props.channelIds || []).indexOf(sheet.props.channel)
                onActivated: (row) => simple.setGaugeChannel(sheet.index, sheet.props.channelIds[row])
            }
            Label { Layout.leftMargin: 16; text: qsTr("Size") }
            Slider {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                from: 0.05
                to: 0.8
                value: sheet.props.height || 0.25
                onMoved: simple.resizeGauge(sheet.index, value)
            }
            Label { Layout.leftMargin: 16; text: qsTr("Letters") }
            Slider {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                from: 0.5
                to: 2.5
                value: sheet.props.textScale || 1
                onMoved: simple.setGaugeTextScale(sheet.index, value)
            }
            Label { Layout.leftMargin: 16; text: qsTr("Opacity") }
            Slider {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                from: 0.1
                to: 1
                value: sheet.props.opacity || 1
                onMoved: simple.setGaugeOpacity(sheet.index, value)
            }
            Label {
                Layout.leftMargin: 16
                visible: sheet.props.hasScale === true
                text: qsTr("Scale ends at (0: by the ride)") + (sheet.props.unit ? ", " + sheet.props.unit : "")
            }
            SpinBox {
                Layout.leftMargin: 16
                Layout.preferredHeight: sheet.touch
                visible: sheet.props.hasScale === true
                from: 0
                to: 100000
                stepSize: 10
                editable: true
                value: Math.round(sheet.props.scaleMax || 0)
                onValueModified: simple.setGaugeScaleMax(sheet.index, value)
            }
            Button {
                Layout.fillWidth: true
                Layout.margins: 16
                Layout.preferredHeight: sheet.touch
                text: qsTr("Remove this gauge")
                onClicked: {
                    simple.removeGauge(sheet.index)
                    sheet.close()
                }
            }
        }
    }
}
