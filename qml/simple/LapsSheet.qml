import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TrackRideViz

// The laps, as the full window's laps dock has them: the route on a map (zoomed with two fingers
// or the wheel, moved with one) with the start/finish line, placed with a tap on it or where the
// picture is; each lap's time against the reference, tapped to see it on the map and go to it;
// the reference (the ghost) chosen; a lap exported on its own. The bar under the map makes it
// bigger or smaller.
Drawer {
    id: sheet
    property int touch: 48
    // the moment shown, ms of the video: where "here" is
    property real moment: 0
    signal jump(real ms)
    signal exportStarted()
    // what the last button could not do
    property string note: ""
    // the lap seen on the map; 0: none
    property int picked: 0
    // a tap on the map places the start/finish line
    property bool placing: false
    property real mapHeight: height * 0.45
    edge: Qt.RightEdge
    // a drag across it moves the map: it closes with a tap beside it, not with a swipe
    interactive: false
    onOpened: {
        note = ""
        placing = false
    }
    onClosed: placing = false

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Label {
            Layout.margins: 12
            font.bold: true
            text: qsTr("Laps")
        }

        // the map
        RouteView {
            id: map
            Layout.fillWidth: true
            Layout.preferredHeight: sheet.mapHeight
            controller: simple
            position: sheet.moment
            highlightLap: sheet.picked
            streets: streetsButton.checked
            hint: sheet.placing ? qsTr("Tap the route where the start/finish line goes") : ""
            baseColor: sheet.palette.base
            textColor: sheet.palette.text
            highlightColor: sheet.palette.highlight

            PinchHandler {
                target: null
                property real last: 1
                onActiveChanged: last = 1
                onActiveScaleChanged: {
                    map.zoomAround(centroid.position.x, centroid.position.y, activeScale / last)
                    last = activeScale
                }
            }
            WheelHandler {
                target: null
                onWheel: (event) => map.zoomAround(event.x, event.y, Math.pow(1.25, event.angleDelta.y / 120))
            }
            DragHandler {
                target: null
                property point last
                onActiveChanged: last = Qt.point(0, 0)
                // only while dragged: the translation goes back to 0 at the release
                onTranslationChanged: {
                    if (active) {
                        map.moveBy(translation.x - last.x, translation.y - last.y)
                        last = Qt.point(translation.x, translation.y)
                    }
                }
            }
            TapHandler {
                onTapped: (point) => {
                    if (!sheet.placing)
                        return
                    const place = map.placeAt(point.position.x, point.position.y)
                    if (simple.placeLine(place.y, place.x)) {
                        sheet.note = ""
                        sheet.placing = false
                    } else {
                        sheet.note = qsTr("Tap on the route itself.")
                    }
                }
            }
            Column {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 6
                spacing: 4
                RoundButton { text: "+"; onClicked: map.zoomAround(map.width / 2, map.height / 2, 1.5) }
                RoundButton { text: "⤢"; onClicked: map.fit() }
                RoundButton { text: "−"; onClicked: map.zoomAround(map.width / 2, map.height / 2, 1 / 1.5) }
            }
            RoundButton {
                id: streetsButton
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 6
                checkable: true
                checked: true
                text: qsTr("Map")
            }
        }
        // dragged: the map bigger or smaller
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            color: sheet.palette.button
            Rectangle {
                anchors.centerIn: parent
                width: 48
                height: 4
                radius: 2
                color: sheet.palette.mid
            }
            DragHandler {
                target: null
                yAxis.enabled: true
                xAxis.enabled: false
                property real from: 0
                onActiveChanged: if (active) from = sheet.mapHeight
                onTranslationChanged: {
                    if (active)
                        sheet.mapHeight = Math.max(100, Math.min(sheet.height - 180, from + translation.y))
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 6
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: simple.hasLine ? qsTr("Laps between crossings of the start/finish line.")
                                 : qsTr("The laps the device recorded. For your own: place the start/finish line on the map, or where the picture is.")
        }
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 4
            Button {
                height: sheet.touch
                checkable: true
                checked: sheet.placing
                text: qsTr("Place on the map")
                onClicked: sheet.placing = checked
            }
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
            Layout.leftMargin: 12
            Layout.rightMargin: 12
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
                height: sheet.touch + 4
                highlighted: modelData.number === sheet.picked
                // seen on the map, and gone to
                onClicked: {
                    sheet.picked = modelData.number
                    sheet.jump(modelData.start)
                }
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
                        color: row.modelData.best ? "#81c784" : sheet.palette.text
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
            Layout.margins: 12
            wrapMode: Text.WordWrap
            opacity: 0.7
            font.pixelSize: 12
            text: qsTr("Ghost: the lap the others are measured against, ridden on route maps that show the ghost (Properties of a map). None chosen: the fastest.")
        }
    }
}
