import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import TrackRideViz

// The simplified interface: a video, its data, a template, an export. Made for fingers first;
// the same layout on a phone, a tablet and the desktop.
ApplicationWindow {
    id: window
    width: 1100
    height: 700
    visible: true
    title: qsTr("Race Track Visualizer")

    readonly property bool hasVideo: simple.videoUrl.toString() !== ""
    // a finger is about this wide
    readonly property int touch: 48

    // opened from the full window: closing it goes back there
    onClosing: (close) => {
        if (simple.canGoBack) {
            close.accepted = false
            player.stop()
            simple.goBack()
        }
    }

    function timeText(ms) {
        const s = Math.max(0, Math.floor(ms / 1000))
        const m = Math.floor(s / 60)
        const h = Math.floor(m / 60)
        const two = (n) => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + two(m % 60) + ":" + two(s % 60) : m + ":" + two(s % 60)
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 4
            ToolButton {
                visible: simple.canGoBack
                text: qsTr("Full interface")
                icon.name: "go-previous"
                onClicked: {
                    player.stop()
                    simple.goBack()
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: simple.canGoBack ? 0 : 12
                elide: Label.ElideRight
                text: window.hasVideo ? simple.videoName + (simple.hasData ? "  +  " + simple.dataName : "")
                                      : qsTr("Race Track Visualizer")
            }
            BusyIndicator {
                running: simple.busy
                visible: running
                Layout.preferredHeight: 32
                Layout.preferredWidth: 32
            }
            ToolButton {
                text: qsTr("Video")
                onClicked: videoDialog.open()
            }
            ToolButton {
                text: qsTr("Data")
                onClicked: dataDialog.open()
            }
            ToolButton {
                text: qsTr("Look")
                enabled: simple.hasData
                onClicked: templates.open()
            }
            ToolButton {
                text: qsTr("Export")
                enabled: simple.hasData && window.hasVideo
                highlighted: enabled
                onClicked: exporter.open()
            }
        }
    }

    FileDialog {
        id: videoDialog
        title: qsTr("Choose the video")
        nameFilters: [qsTr("Videos (*.mp4 *.MP4 *.mov *.MOV *.mkv *.avi *.insv *.lrv)"), qsTr("All files (*)")]
        onAccepted: simple.openVideo(selectedFile)
    }
    FileDialog {
        id: dataDialog
        title: qsTr("Choose the data of the ride")
        nameFilters: [qsTr("Ride data (*.fit *.FIT *.gpx *.tcx *.csv *.nmea *.vbo *.mp4 *.MP4)"), qsTr("All files (*)")]
        onAccepted: simple.openData(selectedFile)
    }

    // the start: what to open
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420)
        visible: !window.hasVideo
        spacing: 16
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 20
            text: qsTr("Gauges over your ride video")
        }
        Button {
            Layout.fillWidth: true
            Layout.preferredHeight: window.touch * 1.3
            text: qsTr("Choose a video")
            highlighted: true
            onClicked: videoDialog.open()
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: qsTr("A GoPro keeps its data in the video; for other cameras open the file of the ride too (FIT, GPX...).")
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: text !== ""
            text: simple.message
        }
    }

    // the preview: the video, the gauges over it, and under it a timeline and the step
    ColumnLayout {
        anchors.fill: parent
        visible: window.hasVideo
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"

            MediaPlayer {
                id: player
                source: simple.videoUrl
                videoOutput: video
                audioOutput: AudioOutput {}
                onSourceChanged: {
                    // the first picture, not black
                    play()
                    pause()
                }
            }
            VideoOutput {
                id: video
                anchors.fill: parent
            }
            GaugeOverlay {
                controller: simple
                position: player.position
                x: video.contentRect.x
                y: video.contentRect.y
                width: video.contentRect.width
                height: video.contentRect.height
            }
            Label {
                anchors.centerIn: parent
                visible: simple.message !== ""
                width: parent.width * 0.8
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: "white"
                style: Text.Outline
                styleColor: "black"
                font.pixelSize: 18
                text: simple.message
            }
            // a tap plays or pauses; a long one leads to the look
            TapHandler {
                onTapped: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
                onLongPressed: if (simple.hasData) templates.open()
            }
        }

        // the timeline, as small as it can be
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8
            RoundButton {
                Layout.preferredWidth: window.touch
                Layout.preferredHeight: window.touch
                text: player.playbackState === MediaPlayer.PlayingState ? "❚❚" : "▶"
                onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
            }
            Slider {
                id: timeline
                Layout.fillWidth: true
                Layout.preferredHeight: window.touch
                from: 0
                to: Math.max(1, player.duration)
                value: player.position
                onMoved: player.position = value
            }
            Label {
                text: window.timeText(player.position) + " / " + window.timeText(player.duration)
            }
        }

        // in step: the data moved against the picture until the gauges match it
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 4
            visible: simple.hasData
            spacing: 4
            Label {
                Layout.fillWidth: true
                elide: Label.ElideRight
                text: simple.syncText
                opacity: 0.8
            }
            Repeater {
                model: [-1, -0.1, 0.1, 1]
                Button {
                    required property real modelData
                    Layout.preferredHeight: window.touch
                    text: (modelData > 0 ? "+" : "−") + Math.abs(modelData) + " s"
                    onClicked: simple.nudgeSync(modelData)
                }
            }
            Button {
                Layout.preferredHeight: window.touch
                text: qsTr("By the clocks")
                onClicked: simple.syncByClocks()
            }
        }
    }

    // the look: one of the templates
    Drawer {
        id: templates
        edge: Qt.RightEdge
        width: Math.min(window.width * 0.85, 360)
        height: window.height
        ListView {
            anchors.fill: parent
            clip: true
            model: simple.templateNames
            header: Label {
                padding: 16
                font.bold: true
                text: qsTr("Look")
            }
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                width: ListView.view.width
                height: window.touch * 1.2
                text: modelData
                highlighted: index === simple.templateIndex
                onClicked: {
                    simple.chooseTemplate(index)
                    templates.close()
                }
            }
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }

    // the export: a profile, then go
    Dialog {
        id: exporter
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 460)
        modal: true
        title: qsTr("Export the video")
        onOpened: player.pause()
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            ComboBox {
                Layout.fillWidth: true
                Layout.preferredHeight: window.touch
                model: simple.profileNames
                currentIndex: simple.profileIndex
                enabled: !simple.exporting
                onActivated: (index) => simple.profileIndex = index
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: simple.exporting
                value: simple.exportProgress
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: text !== ""
                text: simple.exportText
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: window.touch
                    visible: !simple.exporting
                    highlighted: true
                    text: qsTr("Export")
                    onClicked: simple.startExport()
                }
                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: window.touch
                    visible: simple.exporting
                    text: qsTr("Stop, keep what is done")
                    onClicked: simple.stopExport()
                }
                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: window.touch
                    visible: !simple.exporting && simple.exportedUrl.toString() !== ""
                    text: qsTr("Play it")
                    onClicked: Qt.openUrlExternally(simple.exportedUrl)
                }
            }
        }
    }
}
