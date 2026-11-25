import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The laps: where the start/finish line is, each lap's time against the reference, a tap to go
// to it, the reference (the ghost) chosen, a lap exported on its own.
Drawer {
    id: sheet
    property int touch: 48
    // the moment shown, ms of the video: where "here" is
    property real moment: 0
    signal jump(real ms)
    signal exportStarted()
    // what the last button could not do
    property string note: ""
    edge: Qt.RightEdge
    onOpened: note = ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Label {
            Layout.margins: 16
            Layout.bottomMargin: 0
            font.bold: true
            text: qsTr("Laps")
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: simple.hasLine ? qsTr("Laps between crossings of the start/finish line.")
                                 : qsTr("The laps the device recorded. For your own: stop the video where the start/finish is, then tap below.")
        }
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            spacing: 4
            Button {
                height: sheet.touch
                text: qsTr("Start/finish here")
                onClicked: sheet.note = simple.lineHere(sheet.moment) ? "" : qsTr("The direction of travel is not known there.")
            }
            Button {
                height: sheet.touch
                enabled: simple.hasLine
                text: qsTr("Best place nearby")
                onClicked: sheet.note = simple.findBestLine() ? "" : qsTr("No place nearby gives a lap.")
            }
            Button {
                height: sheet.touch
                enabled: simple.hasLine
                flat: true
                text: qsTr("No line")
                onClicked: {
                    sheet.note = ""
                    simple.clearLine()
                }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: sheet.note !== ""
            wrapMode: Text.WordWrap
            color: "#e57373"
            text: sheet.note
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: simple.laps
            ScrollIndicator.vertical: ScrollIndicator {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                width: ListView.view.width
                height: sheet.touch + 8
                onClicked: sheet.jump(modelData.start)
                contentItem: RowLayout {
                    spacing: 8
                    Label {
                        Layout.preferredWidth: 56
                        text: qsTr("Lap %1").arg(row.modelData.number)
                    }
                    Label {
                        Layout.preferredWidth: 80
                        font.family: "monospace"
                        font.bold: row.modelData.best
                        color: row.modelData.best ? "#81c784" : palette.text
                        text: row.modelData.time
                    }
                    Label {
                        Layout.fillWidth: true
                        font.family: "monospace"
                        opacity: 0.8
                        text: row.modelData.delta
                    }
                    // the reference: what the others are measured against, the ghost on the maps
                    ToolButton {
                        Layout.preferredHeight: sheet.touch
                        checkable: true
                        checked: row.modelData.reference
                        text: qsTr("Ghost")
                        onClicked: simple.setReferenceLap(checked ? row.modelData.number : 0)
                    }
                    ToolButton {
                        Layout.preferredHeight: sheet.touch
                        enabled: !simple.exporting
                        text: qsTr("Export")
                        onClicked: {
                            simple.exportLap(row.modelData.number)
                            sheet.exportStarted()
                        }
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("No laps.")
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.margins: 16
            wrapMode: Text.WordWrap
            opacity: 0.7
            font.pixelSize: 12
            text: qsTr("Ghost: the lap the others are measured against, ridden on route maps that show the ghost (Properties of a map). None chosen: the fastest.")
        }
    }
}
