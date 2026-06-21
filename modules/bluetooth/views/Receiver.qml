import QtQuick
import Components

FocusScope {
    id: receiverRoot

    property var navParams: ({})
    signal navigateTo(string path, var params)
    signal goBack()

    property string receiverName: ""
    property bool   connected: false
    property string deviceName: ""
    property string trackTitle: ""
    property string trackArtist: ""
    property string trackAlbum: ""
    property string status: ""           // "playing" | "paused" | ...
    property string artUrl: ""
    property int    positionMs: 0
    property int    durationMs: 0

    property bool hasTrack: trackTitle !== "" || trackArtist !== ""

    // Settings read once at creation (settings UI lives outside the module).
    // Falls back to the manifest default; tolerates legacy "ON"/"OFF" strings.
    property bool showPlayhead: boolSetting("show_playhead", true)
    property bool autoFullscreen: boolSetting("auto_fullscreen", false)
    function boolSetting(key, def) {
        var v = appCore.get_setting(moduleRoot.moduleId, key)
        if (v === undefined || v === null) return def
        return (v === true || v === "ON")
    }

    // Guard so the refresh() during onCompleted (which re-emits the last track)
    // doesn't trip auto-fullscreen; only a genuine new track after load does.
    property bool _ready: false

    function formatTime(ms) {
        if (ms <= 0) return "0:00"
        var s = Math.floor(ms / 1000)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60
        if (h > 0) return h + ":" + (m < 10 ? "0" : "") + m + ":" + (sec < 10 ? "0" : "") + sec
        return m + ":" + (sec < 10 ? "0" : "") + sec
    }

    Connections {
        target: bluetoothBackend
        function onReceiverNameChanged(name) { receiverRoot.receiverName = name }
        function onDeviceConnected(name) { receiverRoot.connected = true; receiverRoot.deviceName = name }
        function onDeviceDisconnected() {
            receiverRoot.connected = false; receiverRoot.deviceName = ""
            receiverRoot.trackTitle = ""; receiverRoot.trackArtist = ""; receiverRoot.trackAlbum = ""
            receiverRoot.artUrl = ""; receiverRoot.status = ""
        }
        function onTrackChanged(title, artist, album, duration) {
            receiverRoot.trackTitle = title; receiverRoot.trackArtist = artist; receiverRoot.trackAlbum = album
            receiverRoot.durationMs = duration
            receiverRoot.positionMs = 0
            // keep the current cover until the next one loads (CoverImage crossfades)
            // Jump straight to the fullscreen visualizer when a new track starts,
            // if enabled. Guarded by _ready so the onCompleted refresh doesn't fire it.
            if (receiverRoot._ready && receiverRoot.autoFullscreen && receiverRoot.connected
                && (title !== "" || artist !== ""))
                receiverRoot.navigateTo("VizFull.qml", {})
        }
        function onStatusChanged(s) { receiverRoot.status = s }
        function onPositionChanged(p) { receiverRoot.positionMs = p }
        function onArtworkReady(url) { receiverRoot.artUrl = url }
        function onErrorOccurred(msg) { console.log("[Bluetooth] " + msg) }
    }

    Component.onCompleted: {
        receiverName = bluetoothBackend.receiver_name()
        bluetoothBackend.refresh()   // repopulate state (the receiver runs from Root now)
        _ready = true                // refresh()'s re-emits are done; arm auto-fullscreen
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack(); event.accepted = true
        } else if (event.key === Qt.Key_Space) {
            bluetoothBackend.play_pause(); event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            bluetoothBackend.previous_track(); event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            bluetoothBackend.next_track(); event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (connected && hasTrack) navigateTo("VizFull.qml", {})
            event.accepted = true
        }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: connected ? deviceName : "Discoverable"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    // ── Waiting for a phone to connect ──
    Column {
        visible: !connected
        anchors.centerIn: parent
        spacing: root.sh * 0.03
        width: root.sw * 0.7

        Text {
            text: "CONNECT TO"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            text: receiverName
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0916667
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            text: "OPEN BLUETOOTH ON YOUR PHONE AND SELECT THIS DEVICE"
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            width: parent.width
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    // ── Connected, nothing playing yet ──
    Text {
        visible: connected && !hasTrack
        anchors.centerIn: parent
        text: "CONNECTED — PLAY SOMETHING ON YOUR PHONE"
        color: root.secondaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.05
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        width: root.sw * 0.6
    }

    // ── Now playing ──
    Row {
        visible: connected && hasTrack
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 0
        spacing: root.sw * 0.05

        // Album art (or placeholder)
        Rectangle {
            width: root.sh * 0.52
            height: root.sh * 0.52
            color: root.surfaceColor
            border.color: root.tertiaryColor
            border.width: root.sh * 0.003125

            CoverImage {
                anchors.fill: parent
                anchors.margins: root.sh * 0.003125
                source: receiverRoot.artUrl
                fillMode: Image.PreserveAspectCrop
            }
            Text {
                anchors.centerIn: parent
                visible: receiverRoot.artUrl === ""
                text: "♪"   // musical note
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.2
            }
        }

        // Track info + controls
        Column {
            width: root.sw * 0.46
            anchors.verticalCenter: parent.verticalCenter
            spacing: root.sh * 0.0208333

            Text {
                text: receiverRoot.trackTitle || "UNKNOWN TITLE"
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.07
                elide: Text.ElideRight
                width: parent.width
            }
            Text {
                text: receiverRoot.trackArtist
                visible: receiverRoot.trackArtist !== ""
                color: root.accentColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.052
                elide: Text.ElideRight
                width: parent.width
            }
            Text {
                text: receiverRoot.trackAlbum
                visible: receiverRoot.trackAlbum !== ""
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.042
                elide: Text.ElideRight
                width: parent.width
            }

            Item { width: 1; height: root.sh * 0.02 }   // spacer

            Text {
                text: receiverRoot.status === "playing" ? "▶  PLAYING"
                      : (receiverRoot.status === "paused" ? "⎉  PAUSED" : receiverRoot.status.toUpperCase())
                color: root.tertiaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.04
            }
        }
    }

    // Playhead — spans the whole screen under the artwork + text
    Item {
        id: playhead
        visible: receiverRoot.showPlayhead && connected && hasTrack && durationMs > 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.sw * 0.115625
        anchors.rightMargin: root.sw * 0.115625
        anchors.bottomMargin: root.sh * 0.16
        height: root.sh * 0.06

        Text {
            id: elapsed
            anchors.left: parent.left
            anchors.bottom: bar.top
            anchors.bottomMargin: root.sh * 0.012
            text: receiverRoot.formatTime(receiverRoot.positionMs)
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.036
        }
        Text {
            anchors.right: parent.right
            anchors.bottom: bar.top
            anchors.bottomMargin: root.sh * 0.012
            text: receiverRoot.formatTime(receiverRoot.durationMs)
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.036
        }

        Rectangle {
            id: bar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.sh * 0.01
            color: root.tertiaryColor

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.width * Math.max(0, Math.min(1,
                       receiverRoot.durationMs > 0 ? receiverRoot.positionMs / receiverRoot.durationMs : 0))
                color: root.accentColor
            }
        }
    }

    // Footer
    Text {
        text: connected && hasTrack
              ? root.hints.back + ":BACK  " + root.hints.change + ":SKIP  " + root.hints.play_pause + ":PLAY/PAUSE  " + root.hints.select + ":FULLSCREEN"
              : root.hints.back + ":BACK"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
