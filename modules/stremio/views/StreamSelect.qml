import QtQuick
import Components

// Lists the resolved (direct-URL) streams for a movie/episode and hands the
// chosen one to the Player, attaching subtitles per the user's language setting.
FocusScope {
    id: streamRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params)
    signal goBack()

    property string typeName: navParams.type || "movie"
    property string streamId: navParams.streamId || ""
    property string metaId: navParams.metaId || ""
    property string videoId: navParams.videoId || ""
    property string title: navParams.title || ""
    property string poster: navParams.poster || ""
    property var streams: navParams.streams || []
    property int viewOffset: navParams.timeOffset || 0
    property int duration: navParams.duration || 0

    property var addonSubs: []      // subtitles from /subtitles addons

    Connections {
        target: stremioBackend
        function onSubtitlesLoaded(subs) { streamRoot.addonSubs = subs }
    }

    Component.onCompleted: {
        stremioBackend.load_subtitles(typeName, streamId, "")
        var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
        streamList.currentIndex = Math.min(restore, Math.max(0, streams.length - 1))
    }

    function chooseStream() {
        var s = streams[streamList.currentIndex]
        if (!s) return

        var prefLang = (appCore.get_setting(moduleRoot.moduleId, "subtitle_language") || "off")

        // Build the subtitle pool: subs embedded in the stream + addon subs.
        var pool = []
        var i
        if (s.subtitles)
            for (i = 0; i < s.subtitles.length; i++)
                if (s.subtitles[i].url) pool.push(s.subtitles[i])
        for (i = 0; i < addonSubs.length; i++)
            if (addonSubs[i].url) pool.push(addonSubs[i])

        var subFiles = []
        var subTrack = -1
        if (prefLang !== "off" && pool.length > 0) {
            var matching = []
            var others = []
            for (i = 0; i < pool.length; i++) {
                var lang = (pool[i].lang || "").toLowerCase()
                if (lang.substring(0, 3) === prefLang) matching.push(pool[i].url)
                else others.push(pool[i].url)
            }
            if (matching.length > 0) {
                subFiles = matching.concat(others)
                subTrack = 0           // mpv auto-selects the first loaded (preferred) sub
            } else {
                subFiles = others       // available but not auto-shown
                subTrack = -1
            }
        }

        navigateTo("Player.qml", {
            streamUrl: s.url,
            subFiles: subFiles,
            subTrack: subTrack,
            title: streamRoot.title,
            metaId: streamRoot.metaId,
            type: streamRoot.typeName,
            videoId: streamRoot.videoId,
            poster: streamRoot.poster,
            viewOffset: streamRoot.viewOffset,
            duration: streamRoot.duration
        })
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Select stream"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Text {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.2
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        text: streamRoot.title
        color: root.secondaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        elide: Text.ElideRight
        font.pixelSize: root.sh * 0.0375
    }

    Text {
        visible: streams.length === 0
        text: "NO PLAYABLE STREAMS\nNEEDS A DEBRID ADDON (E.G. TORRENTIO + REAL DEBRID)"
        color: root.tertiaryColor
        font.family: root.globalFont
        horizontalAlignment: Text.AlignHCenter
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.0416667
        wrapMode: Text.WordWrap
        width: root.sw * 0.6
    }

    ListView {
        id: streamList
        model: streams
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.27
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.48
        clip: true
        focus: true

        Keys.onUpPressed: if (currentIndex > 0) currentIndex--
        Keys.onDownPressed: if (currentIndex < count - 1) currentIndex++
        Keys.onReturnPressed: streamRoot.chooseStream()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                streamRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: streamList.width
            height: root.sh * 0.075

            Rectangle {
                anchors.fill: parent
                anchors.rightMargin: root.sw * 0.01
                color: streamList.currentIndex === index ? root.accentColor : "transparent"

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: root.sw * 0.009375
                    anchors.rightMargin: root.sw * 0.009375
                    verticalAlignment: Text.AlignVCenter
                    text: modelData.label || modelData.url
                    color: streamList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                    font.pixelSize: root.sh * 0.03
                }
            }
        }
    }

    Text {
        text: root.hints.back + ":BACK  " + root.hints.navigate + ":NAVIGATE  " + root.hints.select + ":PLAY"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
