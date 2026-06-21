import QtQuick
import Components

// Series detail: a library toggle row plus an episode list. Selecting an
// episode resolves streams for that episode id (imdb:season:episode).
FocusScope {
    id: seriesRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string typeName: navParams.type || "series"
    property string metaId: navParams.id || ""
    property string fallbackName: navParams.name || ""

    property var detail: null
    property var episodes: []
    property bool inLibrary: false
    property bool onLibraryRow: false      // focus is on the library toggle vs the list
    property bool awaitingStreams: false
    property string pendingVideoId: ""
    property string pendingTitle: ""
    property string statusText: ""

    Connections {
        target: stremioBackend
        function onMetaLoaded(d) {
            seriesRoot.detail = d
            seriesRoot.inLibrary = d.inLibrary === true
            seriesRoot.episodes = d.videos || []
            // Preselect the resume episode, if any.
            var resumeIdx = 0
            if (d.videoId) {
                for (var i = 0; i < seriesRoot.episodes.length; i++)
                    if (seriesRoot.episodes[i].id === d.videoId) { resumeIdx = i; break }
            }
            epList.currentIndex = Math.min(resumeIdx, Math.max(0, seriesRoot.episodes.length - 1))
            epList.positionViewAtIndex(epList.currentIndex, ListView.Contain)
            epList.forceActiveFocus()
        }
        function onStreamsLoaded(streams) {
            if (!seriesRoot.awaitingStreams) return
            seriesRoot.awaitingStreams = false
            var d = seriesRoot.detail || {}
            var resumeOffset = (seriesRoot.pendingVideoId === d.videoId) ? (d.timeOffset || 0) : 0
            var resumeDur    = (seriesRoot.pendingVideoId === d.videoId) ? (d.duration || 0) : 0
            seriesRoot.navigateTo("StreamSelect.qml", {
                type: seriesRoot.typeName,
                streamId: seriesRoot.pendingVideoId,
                metaId: seriesRoot.metaId,
                videoId: seriesRoot.pendingVideoId,
                title: seriesRoot.pendingTitle,
                poster: d.poster || "",
                streams: streams,
                timeOffset: resumeOffset,
                duration: resumeDur
            }, { currentIndex: epList.currentIndex })
        }
        function onLibraryChanged() {
            seriesRoot.inLibrary = stremioBackend.is_in_library(seriesRoot.metaId)
        }
        function onErrorOccurred(msg) {
            seriesRoot.awaitingStreams = false
            seriesRoot.statusText = msg
        }
    }

    Component.onCompleted: stremioBackend.load_meta(typeName, metaId)

    function playEpisode() {
        var ep = episodes[epList.currentIndex]
        if (!ep || awaitingStreams) return
        awaitingStreams = true
        pendingVideoId = ep.id
        pendingTitle = (detail ? detail.name : fallbackName) + "  S" + ep.season + "E" + ep.episode
        statusText = "FINDING STREAMS..."
        stremioBackend.resolve_streams(typeName, ep.id)
    }

    function toggleLibrary() {
        if (!detail) return
        if (inLibrary) stremioBackend.library_remove(metaId)
        else stremioBackend.library_add({ id: metaId, name: detail.name, type: typeName, poster: detail.poster })
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
        subtitle: detail ? detail.name : fallbackName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Text {
        visible: !detail
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05
    }

    // Library toggle row
    Item {
        id: libraryRow
        visible: detail !== null
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.225
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.5
        height: root.sh * 0.0583333

        Rectangle {
            anchors.fill: parent
            color: seriesRoot.onLibraryRow ? root.accentColor : "transparent"
        }
        Text {
            text: seriesRoot.inLibrary ? "✓ IN LIBRARY  (REMOVE)" : "+ ADD TO LIBRARY"
            color: seriesRoot.onLibraryRow ? root.surfaceColor : root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: root.sw * 0.009375
            font.pixelSize: root.sh * 0.0416667
        }
    }

    ListView {
        id: epList
        visible: detail !== null
        model: episodes
        anchors.top: libraryRow.bottom
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.02
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.40
        clip: true

        Keys.onUpPressed: {
            if (seriesRoot.onLibraryRow) return
            if (currentIndex > 0) currentIndex--
            else seriesRoot.onLibraryRow = true
        }
        Keys.onDownPressed: {
            if (seriesRoot.onLibraryRow) { seriesRoot.onLibraryRow = false; return }
            if (currentIndex < count - 1) currentIndex++
        }
        Keys.onReturnPressed: {
            if (seriesRoot.onLibraryRow) seriesRoot.toggleLibrary()
            else seriesRoot.playEpisode()
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                seriesRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: epList.width
            height: root.sh * 0.0583333

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, epList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: epList.currentIndex === index && !seriesRoot.onLibraryRow
                }
                Text {
                    id: rowText
                    text: "S" + modelData.season + "E" + modelData.episode + "  ·  " + (modelData.title || "")
                    color: (epList.currentIndex === index && !seriesRoot.onLibraryRow)
                           ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    topPadding: root.sh * 0.0041667
                    leftPadding: root.sw * 0.009375
                    rightPadding: root.sw * 0.009375
                    bottomPadding: root.sh * 0.00625
                    font.pixelSize: root.sh * 0.05
                }
                SequentialAnimation {
                    running: (epList.currentIndex === index && !seriesRoot.onLibraryRow) && (rowText.implicitWidth > textClip.width)
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) rowText.x = 0
                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: rowText; property: "x"
                        to: textClip.width - rowText.implicitWidth
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: rowText; property: "x"; value: 0 }
                }
            }
        }
    }

    Text {
        visible: seriesRoot.statusText !== ""
        text: seriesRoot.statusText
        color: root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.rightMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }

    Text {
        text: root.hints.back + ":BACK  " + root.hints.navigate + ":NAVIGATE  " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
