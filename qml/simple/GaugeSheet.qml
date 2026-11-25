import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The properties of one gauge, as the full window's dock and its menu of a gauge have them: what
// it shows and in which units, its size and shape, its letters, how see-through, where its scale
// ends; a graph along the distance; a route's map, close-up, 3D flyover and ghost; taking it off.
Drawer {
    id: sheet
    property int index: -1
    property var props: ({})
    property int touch: 48
    // the close-up chosen as "other" or "by speed" before any is set: shown as chosen
    property int closeChoice: -1
    property bool speedsWrong: false
    edge: Qt.RightEdge

    readonly property var closeMeters: [0, 300, 600, 1000, 2000, 5000]
    readonly property int otherChoice: closeMeters.length
    readonly property int speedChoice: closeMeters.length + 1

    function refresh() {
        props = index >= 0 ? simple.gaugeProperties(index) : ({})
    }
    function closeIndex() {
        if (closeChoice >= 0)
            return closeChoice
        if (props.bySpeed)
            return speedChoice
        const fixed = closeMeters.indexOf(Math.round(props.closeUp || 0))
        return fixed >= 0 ? fixed : (props.closeUp > 0 ? otherChoice : 0)
    }
    function applySpeeds() {
        speedsWrong = !simple.setGaugeCloseUp(index, near.value, far.value, slow.value, fast.value)
    }
    function applyCamera() {
        simple.setGaugeFlyoverCamera(index, tilt.value, cameraHeight.value, hills.value)
    }
    onOpened: {
        closeChoice = -1
        speedsWrong = false
        refresh()
    }
    Connections {
        target: simple
        function onOverlayChanged() {
            if (sheet.opened)
                sheet.refresh()
        }
    }

    // a heading of a group, a line above it
    component Heading: Label {
        Layout.fillWidth: true
        Layout.topMargin: 12
        Layout.leftMargin: 16
        font.bold: true
        opacity: 0.8
    }
    component Caption: Label {
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
    component Wide: Slider {
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 8
    }
    component Choice: ComboBox {
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.preferredHeight: sheet.touch
    }
    component Toggle: Switch {
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 8
    }

    ColorPicker {
        id: colorPicker
        property int role: 0
        parent: Overlay.overlay
        touch: sheet.touch
        title: qsTr("Color")
        onPicked: (color) => simple.setGaugeColor(sheet.index, role, color)
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 6
            Label {
                Layout.fillWidth: true
                Layout.margins: 16
                font.bold: true
                elide: Text.ElideRight
                text: sheet.props.name || ""
            }

            Caption { text: qsTr("Shows") }
            Choice {
                model: sheet.props.channelNames || []
                currentIndex: (sheet.props.channelIds || []).indexOf(sheet.props.channel)
                onActivated: (row) => simple.setGaugeChannel(sheet.index, sheet.props.channelIds[row])
            }
            Caption { text: qsTr("Units") }
            Choice {
                model: [qsTr("As in the settings"), qsTr("Metric (km/h, m, °C)"), qsTr("Imperial (mph, ft, °F)")]
                currentIndex: sheet.props.units || 0
                onActivated: (row) => simple.setGaugeUnits(sheet.index, row)
            }

            Heading { text: qsTr("Size") }
            Caption { text: sheet.props.keepShape === false ? qsTr("Height") : qsTr("Size") }
            Wide {
                from: 0.05
                // the route in 3D under a dash is more than the picture
                to: Math.max(0.8, sheet.props.height || 0)
                value: sheet.props.height || 0.25
                onMoved: simple.resizeGauge(sheet.index, value)
            }
            Toggle {
                text: qsTr("Keep the gauge's shape")
                checked: sheet.props.keepShape !== false
                onToggled: simple.setGaugeKeepShape(sheet.index, checked)
            }
            Caption {
                visible: sheet.props.keepShape === false
                opacity: 0.7
                text: qsTr("Set free, its width is its own. Round parts become oval.")
            }
            Caption { visible: sheet.props.keepShape === false; text: qsTr("Width") }
            Wide {
                visible: sheet.props.keepShape === false
                from: 0.02
                to: Math.max(1, sheet.props.width || 0)
                value: sheet.props.width || 0.25
                onMoved: simple.setGaugeWidth(sheet.index, value)
            }
            Caption { text: qsTr("Letters") }
            Wide {
                from: 0.5
                to: 2.5
                value: sheet.props.textScale || 1
                onMoved: simple.setGaugeTextScale(sheet.index, value)
            }
            Caption { text: qsTr("Opacity") }
            Wide {
                from: 0.1
                to: 1
                value: sheet.props.opacity || 1
                onMoved: simple.setGaugeOpacity(sheet.index, value)
            }
            // its colors, or those of all gauges
            Heading { text: qsTr("Colors") }
            Toggle {
                text: qsTr("Own colors")
                checked: sheet.props.ownColors === true
                onToggled: simple.setGaugeOwnColors(sheet.index, checked)
            }
            Caption {
                opacity: 0.7
                text: sheet.props.ownColors === true ? qsTr("These colors are this gauge's alone.")
                                                     : qsTr("These colors are those of all gauges.")
            }
            GridLayout {
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                columns: 2
                rowSpacing: 4
                Repeater {
                    model: [qsTr("Text and numbers"), qsTr("Plates behind the gauges"), qsTr("Needles, bars and highlights"),
                            qsTr("Scales, units and labels")]
                    Label {
                        required property string modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: modelData
                    }
                }
                Repeater {
                    model: 4
                    // over a checkered ground: a see-through color shows as such
                    Rectangle {
                        required property int index
                        readonly property color shown: (sheet.props.colors || [])[index] || "transparent"
                        Layout.row: index
                        Layout.column: 1
                        Layout.preferredWidth: sheet.touch * 1.2
                        Layout.preferredHeight: sheet.touch * 0.75
                        radius: 6
                        color: "#9e9e9e"
                        border.color: "#808080"
                        clip: true
                        Grid {
                            anchors.fill: parent
                            columns: 6
                            Repeater {
                                model: 12
                                Rectangle {
                                    required property int index
                                    width: parent.width / 6
                                    height: parent.height / 2
                                    color: (index + Math.floor(index / 6)) % 2 ? "#e0e0e0" : "#9e9e9e"
                                }
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            color: parent.shown
                        }
                        TapHandler {
                            onTapped: {
                                colorPicker.role = parent.index
                                colorPicker.color = parent.shown
                                colorPicker.open()
                            }
                        }
                    }
                }
            }

            Caption {
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

            Toggle {
                visible: sheet.props.hasGraph === true
                text: qsTr("Graph along the distance")
                checked: sheet.props.byDistance === true
                onToggled: simple.setGaugeByDistance(sheet.index, checked)
            }
            Caption {
                visible: sheet.props.hasGraph === true
                opacity: 0.7
                text: qsTr("Across the distance ridden, stops taking no room; otherwise across the time")
            }

            // a route: its map, how much of it, in 3D
            Heading { visible: sheet.props.hasRoute === true; text: qsTr("Route") }
            Toggle {
                visible: sheet.props.hasRoute === true
                text: qsTr("Street map")
                checked: sheet.props.streets === true
                onToggled: simple.setGaugeStreets(sheet.index, checked)
            }
            Caption { visible: sheet.props.hasRoute === true; text: qsTr("Map") }
            Choice {
                visible: sheet.props.hasRoute === true
                model: sheet.props.mapNames || []
                currentIndex: Math.max(0, (sheet.props.mapIds || []).indexOf(sheet.props.map || ""))
                onActivated: (row) => simple.setGaugeMap(sheet.index, sheet.props.mapIds[row])
            }
            Caption { visible: sheet.props.hasRoute === true; text: qsTr("Close-up") }
            Choice {
                visible: sheet.props.hasRoute === true
                model: [qsTr("Off: the whole route"), qsTr("300 m"), qsTr("600 m"), qsTr("1 km"), qsTr("2 km"),
                        qsTr("5 km"), qsTr("Other"), qsTr("Dynamic, by speed")]
                currentIndex: sheet.closeIndex()
                onActivated: (row) => {
                    sheet.speedsWrong = false
                    if (row < sheet.closeMeters.length) {
                        sheet.closeChoice = -1
                        simple.setGaugeCloseUp(sheet.index, sheet.closeMeters[row], 0, 0, 0)
                    } else if (row === sheet.otherChoice) {
                        sheet.closeChoice = row
                        simple.setGaugeCloseUp(sheet.index, near.value, 0, 0, 0)
                    } else {
                        sheet.closeChoice = row
                        sheet.applySpeeds()
                    }
                }
            }
            // what "other" and "by speed" ask for
            GridLayout {
                readonly property int mode: sheet.closeIndex()
                visible: sheet.props.hasRoute === true && mode >= sheet.otherChoice
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                columns: 2
                Label {
                    visible: parent.mode === sheet.speedChoice
                    text: qsTr("At this speed or slower:")
                }
                SpinBox {
                    id: slow
                    visible: parent.mode === sheet.speedChoice
                    from: 0
                    to: 1000
                    editable: true
                    value: sheet.props.slow || 10
                    textFromValue: (v) => v + " " + (sheet.props.speedUnit || "")
                    valueFromText: (text) => parseInt(text) || 0
                    onValueModified: sheet.applySpeeds()
                }
                Label { text: qsTr("Shows across:") }
                SpinBox {
                    id: near
                    from: 50
                    to: 100000
                    stepSize: 100
                    editable: true
                    value: sheet.props.closeUp > 0 ? Math.round(sheet.props.closeUp) : 300
                    textFromValue: (v) => v + " m"
                    valueFromText: (text) => parseInt(text) || 50
                    onValueModified: parent.mode === sheet.speedChoice ? sheet.applySpeeds()
                                                                       : simple.setGaugeCloseUp(sheet.index, value, 0, 0, 0)
                }
                Label {
                    visible: parent.mode === sheet.speedChoice
                    text: qsTr("At this speed or faster:")
                }
                SpinBox {
                    id: fast
                    visible: parent.mode === sheet.speedChoice
                    from: 0
                    to: 1000
                    editable: true
                    value: sheet.props.fast || 40
                    textFromValue: (v) => v + " " + (sheet.props.speedUnit || "")
                    valueFromText: (text) => parseInt(text) || 0
                    onValueModified: sheet.applySpeeds()
                }
                Label {
                    visible: parent.mode === sheet.speedChoice
                    text: qsTr("Shows across:")
                }
                SpinBox {
                    id: far
                    visible: parent.mode === sheet.speedChoice
                    from: 50
                    to: 100000
                    stepSize: 100
                    editable: true
                    value: Math.round(sheet.props.far || 1500)
                    textFromValue: (v) => v + " m"
                    valueFromText: (text) => parseInt(text) || 50
                    onValueModified: sheet.applySpeeds()
                }
            }
            Caption {
                visible: sheet.props.hasRoute === true && sheet.closeIndex() === sheet.speedChoice
                opacity: 0.7
                text: sheet.speedsWrong ? qsTr("The second speed has to be above the first.")
                                        : sheet.props.canBySpeed === false ? qsTr("The data has no speed.")
                                        : qsTr("Closer when slow, wider when fast")
            }
            Toggle {
                visible: sheet.props.hasRoute === true
                enabled: sheet.props.closeUp > 0
                text: qsTr("The way ahead up")
                checked: sheet.props.headingUp === true
                onToggled: simple.setGaugeHeadingUp(sheet.index, checked)
            }
            Toggle {
                visible: sheet.props.hasRoute === true
                text: qsTr("3D flyover")
                checked: sheet.props.flyover === true
                onToggled: simple.setGaugeFlyover(sheet.index, checked)
            }
            Caption {
                visible: sheet.props.hasRoute === true
                opacity: 0.7
                text: qsTr("The land in 3D, seen from behind the rider; the close-up sets how much of it")
            }
            // the flyover's camera
            Caption {
                visible: sheet.props.flyover === true
                text: qsTr("Tilt: %1°").arg(Math.round(tilt.value))
            }
            Wide {
                id: tilt
                visible: sheet.props.flyover === true
                from: 10
                to: 85
                stepSize: 1
                value: sheet.props.tilt || 60
                onMoved: sheet.applyCamera()
            }
            Caption {
                visible: sheet.props.flyover === true
                text: qsTr("Height above the rider")
            }
            SpinBox {
                id: cameraHeight
                visible: sheet.props.flyover === true
                Layout.leftMargin: 16
                Layout.preferredHeight: sheet.touch
                from: 10
                to: 20000
                stepSize: 25
                editable: true
                value: Math.round(sheet.props.cameraHeight || 100)
                textFromValue: (v) => v + " m"
                valueFromText: (text) => parseInt(text) || 10
                onValueModified: sheet.applyCamera()
            }
            Caption {
                visible: sheet.props.flyover === true
                text: qsTr("Hills: x %1").arg(hills.value.toFixed(1))
            }
            Wide {
                id: hills
                visible: sheet.props.flyover === true
                from: 1
                to: 5
                stepSize: 0.5
                value: sheet.props.hills || 1.5
                onMoved: sheet.applyCamera()
            }
            Button {
                visible: sheet.props.flyover === true
                Layout.leftMargin: 16
                Layout.preferredHeight: sheet.touch
                text: qsTr("As the gauge has it")
                onClicked: simple.setGaugeFlyoverCamera(sheet.index, 0, 0, 0)
            }
            Toggle {
                visible: sheet.props.hasRoute === true
                enabled: sheet.props.flyover !== true
                text: qsTr("Ghost of the reference lap")
                checked: sheet.props.ghost === true
                onToggled: simple.setGaugeGhost(sheet.index, checked)
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
