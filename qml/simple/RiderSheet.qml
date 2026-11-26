import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Who rides what, for the power a ride took when no power meter recorded it: as the full
// window's settings have it (the same settings). Each change is kept at once and the data
// worked out again.
Drawer {
    id: sheet
    property int touch: 48
    edge: Qt.RightEdge

    readonly property var rider: simple.rider
    readonly property var tireChoices: [
        { value: "road-fast", text: qsTr("Road, fast") },
        { value: "road-typical", text: qsTr("Road, typical") },
        { value: "road-slow", text: qsTr("Road, slow") },
        { value: "gravel-fast", text: qsTr("Gravel, fast") },
        { value: "gravel-typical", text: qsTr("Gravel, typical") },
        { value: "gravel-slow", text: qsTr("Gravel, slow") },
        { value: "mtb-fast", text: qsTr("Mountain bike, fast") },
        { value: "mtb-typical", text: qsTr("Mountain bike, typical") },
        { value: "mtb-slow", text: qsTr("Mountain bike, slow") }
    ]
    readonly property var positionChoices: [
        { value: "tops", text: qsTr("Road bike, hands on the tops") },
        { value: "hoods", text: qsTr("Road bike, hands on the hoods") },
        { value: "drops", text: qsTr("Road bike, in the drops") },
        { value: "aerobars", text: qsTr("On aero bars") },
        { value: "upright", text: qsTr("Upright: mountain or city bike") }
    ]
    // a value of the user's own, set in the full window, stays one to choose
    function withOwn(choices, current) {
        return current === "own" ? choices.concat([{ value: "own", text: qsTr("My own value") }]) : choices
    }
    function indexOf(choices, value) {
        for (let i = 0; i < choices.length; ++i) {
            if (choices[i].value === value)
                return i
        }
        return -1
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 8
            Label {
                Layout.margins: 16
                Layout.bottomMargin: 0
                font.bold: true
                text: qsTr("Rider and power")
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("For rides without a power meter: the power is worked out from the speed, the climb and who rides what. With the weather on, the wind is counted too.")
            }
            Switch {
                Layout.leftMargin: 8
                text: qsTr("Estimate the power")
                checked: sheet.rider.enabled
                onToggled: simple.setRider({ enabled: checked })
            }

            Label { Layout.leftMargin: 16; text: qsTr("Rider's weight, kg") }
            SpinBox {
                Layout.leftMargin: 16
                Layout.preferredHeight: sheet.touch
                from: 20
                to: 250
                editable: true
                value: Math.round(sheet.rider.riderKg)
                onValueModified: simple.setRider({ riderKg: value })
            }
            Label { Layout.leftMargin: 16; text: qsTr("Rider's height, cm") }
            SpinBox {
                Layout.leftMargin: 16
                Layout.preferredHeight: sheet.touch
                from: 100
                to: 230
                editable: true
                value: Math.round(sheet.rider.heightCm)
                onValueModified: simple.setRider({ heightCm: value })
            }
            Label { Layout.leftMargin: 16; text: qsTr("Bike's weight, kg") }
            SpinBox {
                Layout.leftMargin: 16
                Layout.preferredHeight: sheet.touch
                from: 1
                to: 60
                editable: true
                value: Math.round(sheet.rider.bikeKg)
                onValueModified: simple.setRider({ bikeKg: value })
            }

            Label { Layout.leftMargin: 16; text: qsTr("Tires") }
            ComboBox {
                readonly property var choices: sheet.withOwn(sheet.tireChoices, sheet.rider.tires)
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.preferredHeight: sheet.touch
                model: choices
                textRole: "text"
                currentIndex: sheet.indexOf(choices, sheet.rider.tires)
                onActivated: (index) => simple.setRider({ tires: choices[index].value })
            }
            Label { Layout.leftMargin: 16; text: qsTr("Riding position") }
            ComboBox {
                readonly property var choices: sheet.withOwn(sheet.positionChoices, sheet.rider.position)
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16
                Layout.preferredHeight: sheet.touch
                model: choices
                textRole: "text"
                currentIndex: sheet.indexOf(choices, sheet.rider.position)
                onActivated: (index) => simple.setRider({ position: choices[index].value })
            }
        }
    }
}
