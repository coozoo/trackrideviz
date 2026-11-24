import QtQuick
import QtQuick.Controls

// Pictures to choose from, in a grid that fills the width: the dashes, or gauges to add.
GridView {
    id: grid
    property string kind: "template"        // "template" or "gauge": which pictures
    property var ids: []
    property var names: []
    property int current: -1
    // a picture this wide at least, more where the width allows
    property real smallest: kind === "template" ? 200 : 130
    signal chosen(int index)

    clip: true
    model: ids.length
    readonly property int columns: Math.max(1, Math.floor(width / smallest))
    cellWidth: width / columns
    cellHeight: cellWidth * (kind === "template" ? 9 / 16 : 1) + 36
    ScrollIndicator.vertical: ScrollIndicator {}

    delegate: ItemDelegate {
        required property int index
        width: grid.cellWidth
        height: grid.cellHeight
        highlighted: index === grid.current
        onClicked: grid.chosen(index)
        contentItem: Column {
            spacing: 4
            Image {
                width: parent.width
                height: width * (grid.kind === "template" ? 9 / 16 : 1)
                asynchronous: true
                fillMode: Image.PreserveAspectFit
                sourceSize.width: width
                sourceSize.height: height
                source: "image://previews/" + grid.kind + "/" + encodeURIComponent(grid.ids[index])
                BusyIndicator {
                    anchors.centerIn: parent
                    running: parent.status === Image.Loading
                    visible: running
                }
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: grid.names[index] || ""
            }
        }
    }
}
