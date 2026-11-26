import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import TrackRideViz

// The simplified interface: a video, its data, a dash of gauges, laps, music, an export; without
// a video the route in 3D. Made for fingers first; the same layout on a phone, a tablet and the
// desktop.
ApplicationWindow {
    id: window
    width: 1100
    height: 720
    visible: true
    title: qsTr("Race Track Visualizer")

    readonly property bool hasVideo: simple.videoUrl.toString() !== ""
    // something to show: a video, or data drawn over a plain picture
    readonly property bool hasPicture: hasVideo || simple.hasData
    readonly property bool playing: playback.playing
    // changes that would be lost
    readonly property bool unsaved: simple.modified && simple.canSave
    // closing asked once, and answered
    property bool leaving: false
    // a finger is about this wide
    readonly property int touch: 48
    // room for every button in the bar, and for the step beside the timeline
    readonly property bool wide: width >= 760
    // the gauge being moved by the finger; -1: none
    property int moving: -1
    // drawn again: where the gauges are has changed
    property int revision: 0
    // a corner of it dragged
    property bool resizing: false
    // a sheet or a dialog over the picture: taps are theirs
    readonly property bool covered: dashes.visible || gauges.visible || properties.visible || musicSheet.visible
                                    || lapsSheet.visible || exporter.visible || discardDialog.visible || joinDialog.visible
    // the graph under the video, and what it shows
    property bool graphShown: true
    property string graphChannel: ""
    function graphDefault() {
        const ids = simple.channelIds
        if (ids.indexOf(graphChannel) >= 0)
            return
        graphChannel = ids.indexOf("speed") >= 0 ? "speed" : (ids.length > 0 ? ids[0] : "")
    }
    // what the gauge being moved is like
    readonly property var movingProps: revision, moving >= 0 ? simple.gaugeProperties(moving) : ({})

    Connections {
        target: simple
        function onOverlayChanged() { window.revision++ }
    }

    // opened from the full window: closing it goes back there; otherwise the changes are asked about.
    // Android's Back comes here too: a gauge being moved or resized is done with first, as with Done.
    onClosing: (close) => {
        if (window.moving >= 0) {
            close.accepted = false
            window.moving = -1
        } else if (simple.canGoBack) {
            close.accepted = false
            window.leave()
        } else if (window.unsaved && !window.leaving) {
            close.accepted = false
            window.whenSaved(() => {
                window.leaving = true
                window.close()
            })
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
        playing ? playback.pause() : playback.play()
    }

    // `action` once the changes are saved or let go, or at once when there are none
    property var afterSaving: null
    function whenSaved(action) {
        if (!unsaved) {
            action()
            return
        }
        afterSaving = action
        discardDialog.open()
    }
    function saveThen(action) {
        if (simple.save()) {
            if (action)
                action()
            return
        }
        afterSaving = action
        saveDialog.open()
    }
    function openChosen(urls) {
        const project = urls.some((url) => url.toString().toLowerCase().endsWith(".rtvproj"))
        const open = () => {
            moving = -1
            simple.openFiles(urls)
        }
        project ? whenSaved(open) : open()
    }

    // where the picture is: the video's player, or a clock over the data
    QtObject {
        id: playback
        property real clock: 0
        property bool clockRuns: false
        readonly property real position: window.hasVideo ? player.position : clock
        readonly property real duration: window.hasVideo ? player.duration : simple.dataDuration
        readonly property bool playing: window.hasVideo ? player.playbackState === MediaPlayer.PlayingState : clockRuns
        function play() {
            if (window.hasVideo) {
                player.play()
            } else {
                if (clock >= duration)
                    clock = 0
                clockRuns = true
            }
        }
        function pause() {
            if (window.hasVideo)
                player.pause()
            else
                clockRuns = false
        }
        function seek(ms) {
            if (window.hasVideo)
                player.position = ms
            else
                clock = Math.max(0, Math.min(duration, ms))
        }
    }
    Timer {
        interval: 33
        repeat: true
        running: playback.clockRuns && !window.hasVideo
        property real last: 0
        onRunningChanged: last = Date.now()
        onTriggered: {
            const now = Date.now()
            playback.clock = Math.min(playback.duration, playback.clock + now - last)
            last = now
            if (playback.clock >= playback.duration)
                playback.clockRuns = false
        }
    }
    Connections {
        target: simple
        function onDataChanged() {
            playback.clockRuns = false
            playback.clock = Math.min(playback.clock, simple.dataDuration)
        }
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 0
            ToolButton {
                visible: simple.canGoBack
                // drawn, not a character: a phone's fonts may not have it
                icon.source: window.wide ? "" : "icons/back.svg"
                display: window.wide ? AbstractButton.TextOnly : AbstractButton.IconOnly
                text: qsTr("Full interface")
                onClicked: window.leave()
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                elide: Label.ElideRight
                text: (window.unsaved ? "• " : "")
                      + (simple.projectName !== "" ? simple.projectName
                         : window.hasVideo ? simple.videoName + (simple.hasData ? "  +  " + simple.dataName : "")
                         : simple.hasData ? simple.dataName : qsTr("Race Track Visualizer"))
            }
            BusyIndicator {
                running: simple.busy
                visible: running
                Layout.preferredHeight: 32
                Layout.preferredWidth: 32
            }
            ToolButton { text: qsTr("Open"); onClicked: openDialog.open() }
            ToolButton { visible: window.wide; text: qsTr("Save"); enabled: window.unsaved; onClicked: window.saveThen(null) }
            ToolButton { text: qsTr("Dash"); enabled: simple.hasData; onClicked: dashes.open() }
            ToolButton { visible: window.wide; text: qsTr("Add gauge"); enabled: simple.hasData; onClicked: gauges.open() }
            ToolButton { visible: window.wide; text: qsTr("Laps"); enabled: simple.hasData; onClicked: lapsSheet.open() }
            ToolButton { visible: window.wide; text: qsTr("Music"); enabled: window.hasVideo; onClicked: musicSheet.open() }
            ToolButton {
                text: qsTr("Export")
                enabled: simple.hasData
                highlighted: enabled
                onClicked: exporter.open()
            }
            // the rarer ones; on a phone all but the first few
            ToolButton {
                icon.source: "icons/more.svg"
                display: AbstractButton.IconOnly
                text: qsTr("More")
                onClicked: moreMenu.popup()
                Menu {
                    id: moreMenu
                    MenuItem { visible: !window.wide; height: visible ? implicitHeight : 0; text: qsTr("Save"); enabled: window.unsaved; onTriggered: window.saveThen(null) }
                    MenuItem { text: qsTr("Save as..."); enabled: simple.canSave; onTriggered: saveDialog.open() }
                    Menu {
                        id: recentMenu
                        title: qsTr("Recent")
                        enabled: simple.recentFiles.length > 0
                        Instantiator {
                            model: simple.recentFiles
                            delegate: MenuItem {
                                required property var modelData
                                text: modelData.name
                                onTriggered: window.openChosen([modelData.url])
                            }
                            onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                        }
                    }
                    // how long painting and downloading take, over the picture (on Android, where
                    // they are measured)
                    MenuItem {
                        visible: Qt.platform.os === "android"
                        height: visible ? implicitHeight : 0
                        text: qsTr("Timings")
                        checkable: true
                        checked: timingsLabel.visible
                        onTriggered: timingsLabel.visible = checked
                    }
                    MenuItem {
                        visible: Qt.platform.os === "android"
                        height: visible ? implicitHeight : 0
                        text: qsTr("Copy log")
                        onTriggered: simple.copyLog()
                    }
                    MenuItem {
                        text: qsTr("Graph under the video")
                        checkable: true
                        checked: window.graphShown
                        onTriggered: window.graphShown = checked
                    }
                    MenuItem { visible: !window.wide; height: visible ? implicitHeight : 0; text: qsTr("Add gauge"); enabled: simple.hasData; onTriggered: gauges.open() }
                    MenuItem { visible: !window.wide; height: visible ? implicitHeight : 0; text: qsTr("Laps"); enabled: simple.hasData; onTriggered: lapsSheet.open() }
                    MenuItem { visible: !window.wide; height: visible ? implicitHeight : 0; text: qsTr("Music"); enabled: window.hasVideo; onTriggered: musicSheet.open() }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("New")
                        enabled: window.hasPicture && !simple.exporting
                        onTriggered: window.whenSaved(() => {
                            window.moving = -1
                            playback.pause()
                            simple.newProject()
                        })
                    }
                }
            }
        }
    }

    // one for all: a video, the ride's data, music, a project; several at once
    FileDialog {
        id: openDialog
        title: qsTr("Open")
        fileMode: FileDialog.OpenFiles
        currentFolder: simple.projectFolder
        nameFilters: [qsTr("Videos, ride data, music, projects (*.mp4 *.MP4 *.mov *.MOV *.mkv *.avi *.insv *.lrv *.fit *.FIT *.gpx *.tcx *.csv *.nmea *.vbo *.mp3 *.m4a *.aac *.ogg *.opus *.flac *.wav *.rtvproj)"),
                      qsTr("All files (*)")]
        onAccepted: window.openChosen(selectedFiles)
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save the project")
        fileMode: FileDialog.SaveFile
        currentFolder: simple.projectFolder
        nameFilters: [qsTr("Projects (*.rtvproj)")]
        onAccepted: {
            if (simple.saveAs(selectedFile) && window.afterSaving)
                window.afterSaving()
            window.afterSaving = null
        }
        onRejected: window.afterSaving = null
    }
    Dialog {
        id: discardDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Save the changes?")
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        Label {
            width: Math.min(window.width - 96, 360)
            wrapMode: Text.WordWrap
            text: qsTr("The project has changes that are not saved yet.")
        }
        onAccepted: window.saveThen(window.afterSaving)
        onDiscarded: {
            close()
            const action = window.afterSaving
            window.afterSaving = null
            if (action)
                action()
        }
        onRejected: window.afterSaving = null
    }

    // the start: what to open, or a look at the demo
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420)
        visible: !window.hasPicture
        spacing: 16
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 20
            text: qsTr("Gauges over your ride video")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                Layout.fillWidth: true
                Layout.preferredHeight: window.touch * 1.3
                text: qsTr("Open")
                highlighted: true
                onClicked: openDialog.open()
            }
            Button {
                Layout.preferredHeight: window.touch * 1.3
                flat: true
                text: qsTr("Demo")
                onClicked: simple.openDemo()
            }
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: qsTr("A video, the ride's data (FIT, GPX...), or both at once; a GoPro keeps its data in the video. With data alone the route is shown in 3D.")
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: text !== ""
            text: simple.message
        }
        // opened lately: projects, videos, data
        Label {
            Layout.topMargin: 8
            visible: simple.recentFiles.length > 0
            font.bold: true
            text: qsTr("Recent")
        }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, window.height * 0.4)
            visible: count > 0
            clip: true
            model: simple.recentFiles
            ScrollIndicator.vertical: ScrollIndicator {}
            delegate: ItemDelegate {
                id: recent
                required property var modelData
                width: ListView.view.width
                height: window.touch
                onClicked: window.openChosen([modelData.url])
                contentItem: ColumnLayout {
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: (recent.modelData.kind === "project" ? qsTr("Project") : recent.modelData.kind === "video" ? qsTr("Video") : qsTr("Data"))
                              + ": " + recent.modelData.name
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        opacity: 0.6
                        font.pixelSize: 11
                        text: recent.modelData.folder
                    }
                }
            }
        }
    }

    // the preview, and under it the timeline with the step beside it
    ColumnLayout {
        anchors.fill: parent
        visible: window.hasPicture
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
                visible: window.hasVideo
            }
            // the picture's own rectangle: the gauges are placed in it (without a video, its shape fitted)
            Item {
                id: picture
                readonly property real fitted: Math.min(stage.width, stage.height * simple.aspect)
                x: window.hasVideo ? video.contentRect.x : (stage.width - fitted) / 2
                y: window.hasVideo ? video.contentRect.y : (stage.height - fitted / simple.aspect) / 2
                width: window.hasVideo ? video.contentRect.width : fitted
                height: window.hasVideo ? video.contentRect.height : fitted / simple.aspect

                GaugeOverlay {
                    anchors.fill: parent
                    controller: simple
                    position: playback.position
                }

                Label {
                    id: timingsLabel
                    visible: false
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 4
                    padding: 4
                    font.pixelSize: 11
                    color: "white"
                    background: Rectangle { color: "#a0000000" }
                    Timer {
                        interval: 1000
                        repeat: true
                        running: timingsLabel.visible
                        triggeredOnStart: true
                        onTriggered: timingsLabel.text = simple.timings() || qsTr("Nothing measured yet.")
                    }
                }

                // the gauge being moved, outlined
                Rectangle {
                    id: outline
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
                    // not while a corner is dragged, which it would take over
                    enabled: window.moving >= 0 && !window.resizing
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

                // its corners: dragged, the opposite one stays; with its ratio kept it grows as a whole
                Repeater {
                    model: window.moving >= 0 ? 4 : 0
                    Item {
                        id: corner
                        required property int index
                        readonly property bool atRight: index % 2 === 1
                        readonly property bool atBottom: index >= 2
                        width: window.touch
                        height: window.touch
                        x: outline.area.x + (atRight ? outline.area.width : 0) - width / 2
                        y: outline.area.y + (atBottom ? outline.area.height : 0) - height / 2
                        Rectangle {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            radius: 9
                            color: "#ffb300"
                            border.color: "black"
                        }
                        DragHandler {
                            target: null
                            // the drag is the corner's to the end: the move under it does not take it
                            grabPermissions: PointerHandler.TakeOverForbidden
                            property rect start
                            // where it was taken last: at the release the translation is 0 again
                            property rect at
                            function place(done) {
                                simple.setGaugeRect(window.moving, at.x / picture.width, at.y / picture.height,
                                                    at.width / picture.width, at.height / picture.height, done)
                            }
                            onActiveChanged: {
                                window.resizing = active
                                if (active) {
                                    start = simple.gaugeRect(window.moving, picture.width, picture.height)
                                    at = start
                                } else if (window.moving >= 0) {
                                    place(true)
                                }
                            }
                            onTranslationChanged: {
                                if (!active)
                                    return
                                let w = Math.max(16, start.width + (corner.atRight ? translation.x : -translation.x))
                                let h = Math.max(16, start.height + (corner.atBottom ? translation.y : -translation.y))
                                if (window.movingProps.keepShape !== false) {
                                    const scale = (w / start.width + h / start.height) / 2
                                    w = start.width * scale
                                    h = start.height * scale
                                }
                                at = Qt.rect(corner.atRight ? start.x : start.x + start.width - w,
                                             corner.atBottom ? start.y : start.y + start.height - h, w, h)
                                place(false)
                            }
                        }
                    }
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
                // not through a sheet or a dialog over it
                enabled: window.moving < 0 && !window.covered
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
                    text: qsTr("Move/resize")
                    onTriggered: {
                        playback.pause()
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
                MenuItem { text: qsTr("Laps"); onTriggered: lapsSheet.open() }
                MenuItem { text: qsTr("Music"); enabled: window.hasVideo; onTriggered: musicSheet.open() }
            }

            // while one is moved: its ratio kept or not, bigger, smaller, done
            Pane {
                visible: window.moving >= 0
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                padding: 4
                RowLayout {
                    // off: the corners change the width and the height each; on: back to its own shape
                    ToolButton {
                        checkable: true
                        checked: window.movingProps.keepShape !== false
                        text: qsTr("Ratio")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Kept: the gauge grows and shrinks as a whole. Off: the corners change its width and height each.")
                        onToggled: simple.setGaugeKeepShape(window.moving, checked)
                    }
                    ToolButton {
                        Layout.preferredWidth: window.touch
                        enabled: window.movingProps.keepShape !== false
                        text: "−"
                        font.pixelSize: 20
                        onClicked: simple.resizeGauge(window.moving, window.movingProps.height / 1.1)
                    }
                    ToolButton {
                        Layout.preferredWidth: window.touch
                        enabled: window.movingProps.keepShape !== false
                        text: "+"
                        font.pixelSize: 20
                        onClicked: simple.resizeGauge(window.moving, window.movingProps.height * 1.1)
                    }
                    Button {
                        text: qsTr("Done")
                        highlighted: true
                        onClicked: window.moving = -1
                    }
                }
            }
        }

        // one channel along the video, a thin strip: tapped or dragged, the video goes there
        RowLayout {
            Layout.fillWidth: true
            // a layout in a layout fills by default: this one stays thin
            Layout.fillHeight: false
            Layout.preferredHeight: 36
            Layout.maximumHeight: 36
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            visible: window.graphShown && simple.hasData
            spacing: 4
            // what it shows, tapped to choose another
            Label {
                Layout.preferredWidth: window.wide ? 120 : 84
                Layout.fillHeight: true
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                font.pixelSize: 12
                font.underline: true
                text: simple.channelNames[simple.channelIds.indexOf(window.graphChannel)] || ""
                TapHandler { onTapped: channelMenu.popup() }
                Menu {
                    id: channelMenu
                    Instantiator {
                        model: simple.channelNames
                        delegate: MenuItem {
                            required property string modelData
                            required property int index
                            text: modelData
                            checkable: true
                            checked: simple.channelIds[index] === window.graphChannel
                            onTriggered: window.graphChannel = simple.channelIds[index]
                        }
                        onObjectAdded: (index, object) => channelMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => channelMenu.removeItem(object)
                    }
                }
                Connections {
                    target: simple
                    function onDataChanged() { window.graphDefault() }
                }
                Component.onCompleted: window.graphDefault()
            }
            ChannelGraph {
                id: graph
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: simple
                channel: window.graphChannel
                position: playback.position
                duration: playback.duration
                lineColor: window.palette.highlight
                textColor: window.palette.text
                MouseArea {
                    anchors.fill: parent
                    function seekTo(x) {
                        playback.seek(Math.max(0, Math.min(1, x / width)) * playback.duration)
                    }
                    onPressed: (mouse) => seekTo(mouse.x)
                    onPositionChanged: (mouse) => seekTo(mouse.x)
                }
            }
        }

        // the timeline, and the step beside it (under it where there is no room)
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            columns: window.wide ? 4 : 3
            columnSpacing: 8
            rowSpacing: 0
            RoundButton {
                Layout.preferredWidth: window.touch
                Layout.preferredHeight: window.touch
                icon.source: window.playing ? "icons/pause.svg" : "icons/play.svg"
                display: AbstractButton.IconOnly
                text: window.playing ? qsTr("Pause") : qsTr("Play")
                onClicked: window.togglePlay()
            }
            // a press anywhere on it goes there at once
            Slider {
                Layout.fillWidth: true
                Layout.preferredHeight: window.touch
                from: 0
                to: Math.max(1, playback.duration)
                value: playback.position
                onMoved: playback.seek(value)
            }
            Label {
                text: window.timeText(playback.position) + " / " + window.timeText(playback.duration)
            }
            // without a video the data runs on its own time: nothing to put in step
            SyncControl {
                visible: simple.hasData && window.hasVideo
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
            const now = simple.musicAt(playback.position)
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
                    playback.pause()
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

    LapsSheet {
        id: lapsSheet
        touch: window.touch
        width: Math.min(window.width, 600)
        height: window.height
        moment: playback.position
        onJump: (ms) => playback.seek(ms)
        onExportStarted: {
            close()
            exporter.open()
        }
    }

    MusicSheet {
        id: musicSheet
        touch: window.touch
        width: Math.min(window.width * 0.9, 380)
        height: window.height
    }

    // several videos, or a camera's chapters, made one before they are opened
    Dialog {
        id: joinDialog
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 480)
        modal: true
        closePolicy: simple.joining ? Popup.NoAutoClose : Popup.CloseOnEscape
        title: simple.joinChapters ? qsTr("Join the chapters of the recording?") : qsTr("Join the videos?")
        Connections {
            target: simple
            function onJoinOffered() { joinDialog.open() }
            // joined and opened: done
            function onJoinChanged() {
                if (joinDialog.opened && simple.joinNames.length === 0 && !simple.joining)
                    joinDialog.close()
            }
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            // in the order they are joined: moved up or down where the names tell it wrong
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, window.height * 0.35)
                clip: true
                interactive: contentHeight > height
                model: simple.joinNames
                ScrollIndicator.vertical: ScrollIndicator {}
                delegate: RowLayout {
                    id: part
                    required property string modelData
                    required property int index
                    width: ListView.view.width
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        font.family: "monospace"
                        font.pixelSize: 12
                        text: (part.index + 1) + ". " + part.modelData
                    }
                    ToolButton {
                        Layout.preferredWidth: window.touch
                        enabled: part.index > 0 && simple.joinReady && !simple.joining
                        icon.source: "icons/up.svg"
                        display: AbstractButton.IconOnly
                        text: qsTr("Move up")
                        onClicked: simple.moveJoinPart(part.index, -1)
                    }
                    ToolButton {
                        Layout.preferredWidth: window.touch
                        enabled: part.index < simple.joinNames.length - 1 && simple.joinReady && !simple.joining
                        icon.source: "icons/down.svg"
                        display: AbstractButton.IconOnly
                        text: qsTr("Move down")
                        onClicked: simple.moveJoinPart(part.index, 1)
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                font.pixelSize: 12
                text: qsTr("In order of their names. The breaks between them come from when each was recorded (the time in its name, or the file's own); if the data does not follow after joining, move it with Sync.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: simple.joinText
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: simple.joining
                value: simple.joinProgress
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    height: window.touch
                    visible: !simple.joining
                    enabled: simple.joinReady
                    highlighted: true
                    text: qsTr("Join")
                    onClicked: simple.startJoin()
                }
                Button {
                    height: window.touch
                    visible: simple.joinChapters && !simple.joining
                    text: qsTr("Only this one")
                    onClicked: {
                        const first = simple.joinFirst
                        simple.cancelJoin()
                        joinDialog.close()
                        simple.openVideo(first)
                    }
                }
                Button {
                    height: window.touch
                    flat: true
                    text: simple.joining ? qsTr("Stop") : qsTr("Cancel")
                    onClicked: {
                        simple.cancelJoin()
                        joinDialog.close()
                    }
                }
            }
        }
    }

    // the export: a profile, then go
    Dialog {
        id: exporter
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 460)
        modal: true
        title: qsTr("Export the video")
        onOpened: {
            playback.pause()
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
                enabled: !simple.exporting && !simple.gaugesOnly
                onActivated: (index) => simple.profileIndex = index
            }
            // the quickest of all: the video is not encoded again, only the gauges are
            Switch {
                Layout.fillWidth: true
                enabled: !simple.exporting
                checked: simple.gaugesOnly
                text: qsTr("Only the gauges, see-through (.webm)")
                onToggled: simple.gaugesOnly = checked
            }
            Label {
                Layout.fillWidth: true
                visible: simple.gaugesOnly
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("For laying over the video in an editor: only the gauges are made, the video is left as it is.")
            }
            // only a part: from where to where in the video
            Switch {
                id: partOnly
                enabled: !simple.exporting
                text: qsTr("Only a part")
                onToggled: if (checked && range.second.value <= range.first.value) {
                    range.setValues(0, playback.duration / 1000)
                }
            }
            GridLayout {
                Layout.fillWidth: true
                visible: partOnly.checked
                enabled: !simple.exporting
                columns: 3
                RangeSlider {
                    id: range
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, playback.duration / 1000)
                    stepSize: 0.1
                    first.value: 0
                    second.value: to
                }
                Label { text: qsTr("From %1").arg(window.timeText(range.first.value * 1000)) }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("to %1").arg(window.timeText(range.second.value * 1000)) }
                Button {
                    text: qsTr("From here")
                    onClicked: range.first.value = Math.min(playback.position / 1000, range.second.value)
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("To here")
                    onClicked: range.second.value = Math.max(playback.position / 1000, range.first.value)
                }
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
                    onClicked: partOnly.checked ? simple.startExportRange(range.first.value, range.second.value)
                                                : simple.startExport()
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
