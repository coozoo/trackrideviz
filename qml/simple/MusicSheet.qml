import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// The music under the video, as the full window's dock has it: tracks one after another from
// the start, and how loud they and the camera's own sound are. With the data alone, the music
// is the only sound.
Drawer {
    id: sheet
    property int touch: 48
    property bool hasVideo: true
    edge: Qt.RightEdge

    FileDialog {
        id: musicDialog
        title: qsTr("Choose music")
        nameFilters: [qsTr("Music (*.mp3 *.m4a *.aac *.ogg *.opus *.flac *.wav)"), qsTr("All files (*)")]
        onAccepted: simple.addMusic(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Label {
            Layout.margins: 16
            font.bold: true
            text: qsTr("Music")
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: simple.musicNames
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                width: ListView.view.width
                height: sheet.touch
                text: (index + 1) + ". " + modelData
                ToolButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: sheet.touch
                    height: sheet.touch
                    icon.source: "icons/close.svg"
                    display: AbstractButton.IconOnly
                    text: qsTr("Remove")
                    onClicked: simple.removeMusic(index)
                }
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: simple.musicNames.length === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: sheet.hasVideo ? qsTr("No music: the video keeps its own sound.") : qsTr("No music: the export is silent.")
            }
        }
        Button {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.preferredHeight: sheet.touch
            text: qsTr("Add music")
            onClicked: musicDialog.open()
        }
        Label { Layout.leftMargin: 16; visible: sheet.hasVideo; text: qsTr("The video's sound: %1 %").arg(simple.videoVolume) }
        Slider {
            visible: sheet.hasVideo
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            from: 0
            to: 200
            stepSize: 5
            value: simple.videoVolume
            onMoved: simple.videoVolume = value
        }
        Label { Layout.leftMargin: 16; text: qsTr("The music: %1 %").arg(simple.musicVolume) }
        Slider {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 16
            from: 0
            to: 200
            stepSize: 5
            value: simple.musicVolume
            onMoved: simple.musicVolume = value
        }
    }
}
