import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal navigateTo(string path, var params)
    signal goBack()

    property string streamUrl: navParams.streamUrl || ""
    property var    subFiles:  navParams.subFiles  || []
    property int    subTrack:  navParams.subTrack  !== undefined ? navParams.subTrack : -1
    property string itemTitle: navParams.title     || ""
    property string metaId:    navParams.metaId    || ""
    property string typeName:  navParams.type      || "movie"
    property string videoId:   navParams.videoId   || ""
    property int    viewOffset: navParams.viewOffset || 0
    property int    durationMs: navParams.duration   || 0

    property bool reported: false
    property bool overlayVisible: false
    property int  choiceIndex: 0
    property string resumeSetting: "ask"

    property int lastKnownPositionMs: 0
    property int lastKnownDurationMs: 0

    function reportProgress() {
        var pos = lastKnownPositionMs || mpvController.position
        var dur = lastKnownDurationMs || mpvController.duration || durationMs
        if (pos > 0)
            stremioBackend.report_progress(metaId, typeName, videoId, pos, dur)
    }

    function doStartPlayback(offsetMs) {
        mpvController.loadAndPlay(streamUrl, offsetMs / 1000.0,
                                  0, subTrack, subFiles,
                                  false, -1, 0.0, "", false, "", false)
    }

    function formatTime(ms) {
        var s = Math.floor(ms / 1000)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60
        if (h > 0) return h + ":" + (m < 10 ? "0" : "") + m + ":" + (sec < 10 ? "0" : "") + sec
        return m + ":" + (sec < 10 ? "0" : "") + sec
    }

    Connections {
        target: mpvController
        function onPositionChanged(ms) { if (ms > 0) playerRoot.lastKnownPositionMs = ms }
        function onDurationChanged(ms) { if (ms > 0) playerRoot.lastKnownDurationMs = ms }
        function onPlaybackFinished(finalPos, finalDur) {
            if (!playerRoot.reported) {
                playerRoot.reported = true
                playerRoot.reportProgress()
            }
            playerRoot.goBack()
        }
        function onPlaybackFailed() { playerRoot.goBack() }
    }

    // Periodically persist progress so a power-loss still records resume position.
    Timer {
        interval: 10000
        repeat: true
        running: !overlayVisible
        onTriggered: if (mpvController.position > 0) playerRoot.reportProgress()
    }

    Component.onCompleted: {
        if (streamUrl === "") { goBack(); return }
        resumeSetting = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"
        if (viewOffset > 0 && resumeSetting === "ask") {
            overlayVisible = true
        } else if (viewOffset > 0 && resumeSetting === "always") {
            doStartPlayback(viewOffset)
        } else {
            doStartPlayback(0)
        }
    }

    focus: true
    Keys.onPressed: function(event) {
        if (overlayVisible) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
            } else if (event.key === Qt.Key_Up) {
                choiceIndex = 0
            } else if (event.key === Qt.Key_Down) {
                choiceIndex = 1
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                overlayVisible = false
                doStartPlayback(choiceIndex === 0 ? viewOffset : 0)
            }
            event.accepted = true
            return
        }
        // Forward transport keys to mpv (it owns the screen during playback).
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) mpvController.sendKey("ESC")
        else if (event.key === Qt.Key_Backspace) mpvController.sendKey("BS")
        else if (event.key === Qt.Key_Up) mpvController.sendKey("UP")
        else if (event.key === Qt.Key_Down) mpvController.sendKey("DOWN")
        else if (event.key === Qt.Key_Left) mpvController.sendKey("LEFT")
        else if (event.key === Qt.Key_Right) mpvController.sendKey("RIGHT")
        else if (event.key === Qt.Key_Space) mpvController.sendKey("SPACE")
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) mpvController.sendKey("ENTER")
        event.accepted = true
    }

    Rectangle { anchors.fill: parent; color: "black" }

    // Resume prompt
    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: overlayVisible

        Column {
            anchors.centerIn: parent
            spacing: root.sh * 0.05

            Text {
                text: "RESUME PLAYBACK?"
                color: root.secondaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Column {
                Repeater {
                    model: [ "Resume from " + formatTime(viewOffset), "Start from the beginning" ]
                    delegate: Item {
                        width: root.sw * 0.5
                        height: root.sh * 0.0583333

                        Rectangle {
                            anchors.fill: choiceText
                            color: root.accentColor
                            visible: index === choiceIndex
                        }
                        Text {
                            id: choiceText
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData
                            color: index === choiceIndex ? root.surfaceColor : root.primaryColor
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            topPadding: root.sh * 0.0041667
                            leftPadding: root.sw * 0.009375
                            rightPadding: root.sw * 0.009375
                            bottomPadding: root.sh * 0.00625
                            font.pixelSize: root.sh * 0.0416667
                        }
                    }
                }
            }

            Text {
                text: root.hints.back + ":BACK  " + root.hints.navigate + ":NAVIGATE  " + root.hints.select + ":SELECT"
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }
}
