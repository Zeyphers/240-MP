import QtQuick

// Fullscreen mode. ◄► cycles screens: album Cover → Shuffle → Cycle.
//   Shuffle = projectM auto-shuffles through all presets.
//   Cycle   = projectM locked on one preset; ▲▼ steps through presets and it stays.
// Track info is centered at the bottom in every screen. Esc/Enter exits.
FocusScope {
    id: fullRoot

    property var navParams: ({})
    signal goBack()

    property var    modes: ["Cover", "Shuffle", "Cycle"]
    property int    modeIndex: 0
    property string mode: modes[modeIndex]

    property string artUrl: ""
    property string artColor: ""
    property string trackTitle: ""
    property string trackArtist: ""
    property string trackAlbum: ""
    property string status: ""

    property bool vizActive: mode === "Shuffle" || mode === "Cycle"

    // Settings (read once when the view is created; the settings UI lives outside
    // the module so values can't change while it's open). Falls back to the
    // manifest default when unset; tolerates legacy "ON"/"OFF" string values.
    property bool coloredBackground: boolSetting("colored_background", true)
    property bool showTrackInfo: boolSetting("show_track_info", true)
    property bool modeFlashEnabled: boolSetting("mode_flash", true)
    property bool coverFill: (appCore.get_setting(moduleRoot.moduleId, "album_art_scaling") || "Fit") === "Fill"
    property real infoScale: {
        var s = appCore.get_setting(moduleRoot.moduleId, "track_info_size") || "Medium"
        return s === "Small" ? 0.8 : (s === "Large" ? 1.3 : 1.0)
    }
    function boolSetting(key, def) {
        var v = appCore.get_setting(moduleRoot.moduleId, key)
        if (v === undefined || v === null) return def
        return (v === true || v === "ON")
    }

    // Friendly name shown in the big centered flash overlay.
    function modeLabelText(m) {
        if (m === "Cover")   return "ALBUM ART"
        if (m === "Shuffle") return "SHUFFLE"
        if (m === "Cycle")   return "BROWSE  ▲▼"
        return m
    }

    function cycle(dir) {
        modeIndex = (modeIndex + dir + modes.length) % modes.length
        appCore.save_setting(moduleRoot.moduleId, "fs_mode", mode)
        applyVizMode()
        flashModeLabel()
    }

    function flashModeLabel() { if (modeFlashEnabled) modeFlashAnim.restart() }

    // Push the current screen's behaviour onto the (single, reused) projectM item:
    // Shuffle auto-cycles all presets; Cycle locks on the current one.
    function applyVizMode() {
        if (!milkLoader.item) return
        milkLoader.item.shuffle = (mode === "Shuffle")
        milkLoader.item.locked  = (mode === "Cycle")
    }

    Connections {
        target: bluetoothBackend
        function onArtworkReady(u) { fullRoot.artUrl = u }
        function onArtworkColorReady(c) { fullRoot.artColor = c }
        function onTrackChanged(t, a, al, d) { fullRoot.trackTitle = t; fullRoot.trackArtist = a; fullRoot.trackAlbum = al }
        function onStatusChanged(s) { fullRoot.status = s }
        function onDeviceDisconnected() { fullRoot.goBack() }
    }

    Component.onCompleted: {
        // Restore the last-used screen; fall back to the configured default style.
        var saved = appCore.get_setting(moduleRoot.moduleId, "fs_mode")
        if (saved === undefined || saved === null || saved === "")
            saved = appCore.get_setting(moduleRoot.moduleId, "fs_default_style") || "Shuffle"
        var i = modes.indexOf(saved)
        if (i >= 0) modeIndex = i
        bluetoothBackend.refresh()
        flashModeLabel()   // announce the current screen on open
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back
            || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            goBack(); event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            cycle(-1); event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            cycle(1); event.accepted = true
        } else if (event.key === Qt.Key_Space) {
            bluetoothBackend.play_pause(); event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            // In Cycle, ▲▼ browse presets (and stay); otherwise skip tracks.
            if (fullRoot.mode === "Cycle" && milkLoader.item) milkLoader.item.previousPreset()
            else bluetoothBackend.previous_track()
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            if (fullRoot.mode === "Cycle" && milkLoader.item) milkLoader.item.nextPreset()
            else bluetoothBackend.next_track()
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: (fullRoot.coloredBackground && fullRoot.mode === "Cover" && fullRoot.artColor !== "") ? fullRoot.artColor : "black"
        Behavior on color { ColorAnimation { duration: 400 } }
    }

    // ── Full album cover ──
    CoverImage {
        anchors.fill: parent
        visible: fullRoot.mode === "Cover"
        source: fullRoot.artUrl
        fillMode: fullRoot.coverFill ? Image.PreserveAspectCrop : Image.PreserveAspectFit
    }
    Text {
        anchors.centerIn: parent
        visible: fullRoot.mode === "Cover" && fullRoot.artUrl === ""
        text: "♪"
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.3
    }

    // ── projectM / MilkDrop visualizer ──
    // One instance is kept alive across Shuffle and Cycle (switching just toggles
    // lock/shuffle, no re-init / black flash); torn down only when leaving to Cover.
    Loader {
        id: milkLoader
        anchors.fill: parent
        active: fullRoot.vizActive
        visible: active
        source: active ? "MilkDrop.qml" : ""
        onLoaded: fullRoot.applyVizMode()
    }

    // ── Mode flash (big, centered, fades out) ──
    // Pops up when the view opens or you switch screens, then fades away.
    Text {
        id: modeFlash
        anchors.centerIn: parent
        text: fullRoot.modeLabelText(fullRoot.mode)
        opacity: 0
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.11
        style: Text.Outline; styleColor: "black"

        SequentialAnimation {
            id: modeFlashAnim
            PropertyAction  { target: modeFlash; property: "opacity"; value: 1.0 }
            PauseAnimation  { duration: 1000 }
            NumberAnimation { target: modeFlash; property: "opacity"; to: 0.0; duration: 600; easing.type: Easing.InOutQuad }
        }
    }

    // ── Track info centered at the bottom ──
    Column {
        visible: fullRoot.showTrackInfo
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.sh * 0.06
        spacing: root.sh * 0.006
        width: parent.width * 0.9

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            text: fullRoot.trackTitle
            visible: text !== ""
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.05 * fullRoot.infoScale
            style: Text.Outline; styleColor: "black"
            elide: Text.ElideRight
            width: parent.width
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            text: fullRoot.trackArtist
            visible: text !== ""
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.036 * fullRoot.infoScale
            style: Text.Outline; styleColor: "black"
            elide: Text.ElideRight
            width: parent.width
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            text: fullRoot.trackAlbum
            visible: text !== ""
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.028 * fullRoot.infoScale
            style: Text.Outline; styleColor: "black"
            elide: Text.ElideRight
            width: parent.width
        }
    }

}
