import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import TrackRideViz

// The simplified interface: a video, its data, a dash of gauges, music, an export. Made for
// fingers first; the same layout on a phone, a tablet and the desktop.
ApplicationWindow {
    id: window
    width: 1100
    height: 720
    visible: true
    title: qsTr("Race Track Visualizer")

    readonly property bool hasVideo: simple.videoUrl.toString() !== ""
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    // a finger is about this wide
    readonly property int touch: 48
    // room for every button in the bar, and for the step beside the timeline
    readonly property bool wide: width >= 760
    // the gauge being moved by the finger; -1: none
    property int moving: -1
    // drawn again: where the gauges are has changed
    property int revision: 0

    Connections {
        target: simple
        function onOverlayChanged() { window.revision++ }
    }

    // opened from the full window: closing it goes back there
    onClosing: (close) => {
        if (simple.canGoBack) {
            close.accepted = false
            window.leave()
        }
    }
    function leave() {
        player.stop()
        music.stop()
        simple.goBack()
    }

    function timeText(ms) {
        const s = Math.max(0, Math.floor(ms / 1000))
        const m = Math.floor(s / 60)
        const h = Math.floor(m / 60)
        const two = (n) => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + two(m % 60) + ":" + two(s % 60) : m + ":" + two(s % 60)
    }
    function togglePlay() {
        playing ? player.pause() : player.play()
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 0
            ToolButton {
                visible: simple.canGoBack
                text: window.wide ? qsTr("Full interface") : "←"
                onClicked: window.leave()
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 12
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
            ToolButton { visible: window.wide; text: qsTr("Video"); onClicked: videoDialog.open() }
            ToolButton { visible: window.wide; text: qsTr("Data"); onClicked: dataDialog.open() }
            ToolButton { text: qsTr("Dash"); enabled: simple.hasData; onClicked: dashes.open() }
            ToolButton { visible: window.wide; text: qsTr("Add gauge"); enabled: simple.hasData; onClicked: gauges.open() }
            ToolButton { visible: window.wide; text: qsTr("Music"); enabled: window.hasVideo; onClicked: musicSheet.open() }
            ToolButton {
                text: qsTr("Export")
                enabled: simple.hasData && window.hasVideo
                highlighted: enabled
                onClicked: exporter.open()
            }
            // on a phone the rest is behind this
            ToolButton {
                visible: !window.wide
                text: "⋮"
                onClicked: moreMenu.popup()
                Menu {
                    id: moreMenu
                    MenuItem { text: qsTr("Video"); onTriggered: videoDialog.open() }
                    MenuItem { text: qsTr("Data"); onTriggered: dataDialog.open() }
                    MenuItem { text: qsTr("Add gauge"); enabled: simple.hasData; onTriggered: gauges.open() }
                    MenuItem { text: qsTr("Music"); enabled: window.hasVideo; onTriggered: musicSheet.open() }
                }
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

    // the preview, and under it the timeline with the step beside it
    ColumnLayout {
        anchors.fill: parent
        visible: window.hasVideo
        spacing: 0

        Rectangle {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"

            MediaPlayer {
                id: player
                source: simple.videoUrl
                videoOutput: video
                // a player plays no louder than the file: a boost is heard in the export only
                audioOutput: AudioOutput { volume: Math.min(1, simple.videoVolume / 100) }
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
            // the picture's own rectangle: the gauges are placed in it
            Item {
                id: picture
                x: video.contentRect.x
                y: video.contentRect.y
                width: video.contentRect.width
                height: video.contentRect.height

                GaugeOverlay {
                    anchors.fill: parent
                    controller: simple
                    position: player.position
                }

                // the gauge being moved, outlined
                Rectangle {
                    readonly property rect area: window.revision, window.moving >= 0
                                                 ? simple.gaugeRect(window.moving, picture.width, picture.height) : Qt.rect(0, 0, 0, 0)
                    visible: window.moving >= 0
                    x: area.x
                    y: area.y
                    width: area.width
                    height: area.height
                    color: "transparent"
                    border.color: "#ffb300"
                    border.width: 2
                    radius: 4
                }

                // moving: the gauge follows the finger, two fingers make it bigger or smaller
                DragHandler {
                    id: mover
                    enabled: window.moving >= 0
                    target: null
                    property point from
                    // where it was taken last: at the release the translation is 0 again
                    property point at
                    function place(done) {
                        simple.moveGauge(window.moving, at.x / picture.width, at.y / picture.height, done)
                    }
                    onActiveChanged: {
                        if (active) {
                            const r = simple.gaugeRect(window.moving, picture.width, picture.height)
                            from = Qt.point(r.x, r.y)
                            at = from
                        } else if (window.moving >= 0) {
                            place(true)
                        }
                    }
                    onTranslationChanged: {
                        if (active) {
                            at = Qt.point(from.x + translation.x, from.y + translation.y)
                            place(false)
                        }
                    }
                }
                PinchHandler {
                    enabled: window.moving >= 0
                    target: null
                    property real from: 0.25
                    onActiveChanged: if (active) from = simple.gaugeProperties(window.moving).height
                    onActiveScaleChanged: if (active) simple.resizeGauge(window.moving, from * activeScale)
                }
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

            // a tap plays or pauses; a long one on a gauge offers what to do with it, elsewhere what to add
            TapHandler {
                enabled: window.moving < 0
                onTapped: window.togglePlay()
                onLongPressed: {
                    const p = point.position
                    const index = simple.hasData
                            ? simple.gaugeAt(p.x - picture.x, p.y - picture.y, picture.width, picture.height) : -1
                    if (index >= 0) {
                        gaugeMenu.index = index
                        gaugeMenu.popup(p.x, p.y)
                    } else if (simple.hasData) {
                        stageMenu.popup(p.x, p.y)
                    }
                }
            }
            Menu {
                id: gaugeMenu
                property int index: -1
                MenuItem {
                    text: qsTr("Move")
                    onTriggered: {
                        player.pause()
                        window.moving = gaugeMenu.index
                    }
                }
                MenuItem {
                    text: qsTr("Properties")
                    onTriggered: {
                        properties.index = gaugeMenu.index
                        properties.open()
                    }
                }
                MenuItem { text: qsTr("Remove"); onTriggered: simple.removeGauge(gaugeMenu.index) }
            }
            Menu {
                id: stageMenu
                MenuItem { text: qsTr("Dash"); onTriggered: dashes.open() }
                MenuItem { text: qsTr("Add gauge"); onTriggered: gauges.open() }
                MenuItem { text: qsTr("Music"); onTriggered: musicSheet.open() }
            }

            // while one is moved: bigger, smaller, done
            Pane {
                visible: window.moving >= 0
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                padding: 4
                RowLayout {
                    Label { Layout.leftMargin: 8; text: qsTr("Drag it where it goes") }
                    ToolButton {
                        Layout.preferredWidth: window.touch
                        text: "−"
                        font.pixelSize: 20
                        onClicked: simple.resizeGauge(window.moving, simple.gaugeProperties(window.moving).height / 1.1)
                    }
                    ToolButton {
                        Layout.preferredWidth: window.touch
                        text: "+"
                        font.pixelSize: 20
                        onClicked: simple.resizeGauge(window.moving, simple.gaugeProperties(window.moving).height * 1.1)
                    }
                    Button {
                        text: qsTr("Done")
                        highlighted: true
                        onClicked: window.moving = -1
                    }
                }
            }
        }

        // the timeline, and the step beside it (under it where there is no room)
        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            columns: window.wide ? 4 : 3
            columnSpacing: 8
            rowSpacing: 0
            RoundButton {
                Layout.preferredWidth: window.touch
                Layout.preferredHeight: window.touch
                text: window.playing ? "❚❚" : "▶"
                onClicked: window.togglePlay()
            }
            // a press anywhere on it goes there at once
            Slider {
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
            SyncControl {
                visible: simple.hasData
                touch: window.touch
                Layout.columnSpan: window.wide ? 1 : 3
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }

    // the music in the preview, following the video as the export will have it
    MediaPlayer {
        id: music
        audioOutput: AudioOutput { id: musicOutput }
    }
    Timer {
        interval: 250
        repeat: true
        running: window.hasVideo && simple.musicNames.length > 0
        onRunningChanged: if (!running) music.pause()
        onTriggered: {
            const now = simple.musicAt(player.position)
            if (now.url.toString() === "") {
                music.pause()
                return
            }
            if (music.source.toString() !== now.url.toString())
                music.source = now.url
            musicOutput.volume = now.volume
            if (!window.playing) {
                music.pause()
                return
            }
            // only a jump is put right: each seek is heard
            if (music.playbackState !== MediaPlayer.PlayingState) {
                music.position = now.position
                music.play()
            } else if (Math.abs(music.position - now.position) > 1000) {
                music.position = now.position
            }
        }
    }

    // the dashes: the gauges of a template laid over the video
    Drawer {
        id: dashes
        edge: Qt.BottomEdge
        width: window.width
        height: Math.min(window.height * 0.7, 560)
        ColumnLayout {
            anchors.fill: parent
            Label { Layout.margins: 12; font.bold: true; text: qsTr("Dash") }
            PreviewGrid {
                Layout.fillWidth: true
                Layout.fillHeight: true
                kind: "template"
                ids: simple.templateIds
                names: simple.templateNames
                current: simple.templateIndex
                onChosen: (index) => {
                    simple.chooseTemplate(index)
                    dashes.close()
                }
            }
        }
    }

    // one gauge more
    Drawer {
        id: gauges
        edge: Qt.BottomEdge
        width: window.width
        height: Math.min(window.height * 0.7, 560)
        ColumnLayout {
            anchors.fill: parent
            Label { Layout.margins: 12; font.bold: true; text: qsTr("Add gauge") }
            PreviewGrid {
                Layout.fillWidth: true
                Layout.fillHeight: true
                kind: "gauge"
                ids: simple.gaugeIds
                names: simple.gaugeNames
                onChosen: (index) => {
                    simple.addGauge(simple.gaugeIds[index])
                    gauges.close()
                    // straight to where it goes
                    player.pause()
                    window.moving = simple.gaugeCount - 1
                }
            }
        }
    }

    GaugeSheet {
        id: properties
        touch: window.touch
        width: Math.min(window.width * 0.9, 380)
        height: window.height
    }

    MusicSheet {
        id: musicSheet
        touch: window.touch
        width: Math.min(window.width * 0.9, 380)
        height: window.height
    }

    // the export: a profile, then go
    Dialog {
        id: exporter
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 460)
        modal: true
        title: qsTr("Export the video")
        onOpened: {
            player.pause()
            window.moving = -1
        }
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
