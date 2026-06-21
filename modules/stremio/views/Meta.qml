import QtQuick
import Components

// Movie (single-video) detail: play/resume + library toggle.
FocusScope {
    id: metaRoot

    property var navParams: ({})

    signal navigateTo(string path, var params)
    signal goBack()

    property string typeName: navParams.type || "movie"
    property string metaId: navParams.id || ""
    property string fallbackName: navParams.name || ""

    property var detail: null
    property bool inLibrary: false
    property int focusRow: 0          // 0 = play, 1 = library
    property bool awaitingStreams: false

    function durationStr(rt) { return rt ? rt : "" }

    Connections {
        target: stremioBackend
        function onMetaLoaded(d) {
            metaRoot.detail = d
            metaRoot.inLibrary = d.inLibrary === true
        }
        function onStreamsLoaded(streams) {
            if (!metaRoot.awaitingStreams) return
            metaRoot.awaitingStreams = false
            var d = metaRoot.detail || {}
            metaRoot.navigateTo("StreamSelect.qml", {
                type: metaRoot.typeName,
                streamId: metaRoot.metaId,
                metaId: metaRoot.metaId,
                videoId: metaRoot.metaId,
                title: (d.name || metaRoot.fallbackName),
                poster: d.poster || "",
                streams: streams,
                timeOffset: d.timeOffset || 0,
                duration: d.duration || 0
            })
        }
        function onLibraryChanged() {
            metaRoot.inLibrary = stremioBackend.is_in_library(metaRoot.metaId)
        }
        function onErrorOccurred(msg) {
            metaRoot.awaitingStreams = false
            metaRoot.statusText = msg
        }
    }

    property string statusText: ""

    Component.onCompleted: stremioBackend.load_meta(typeName, metaId)

    function play() {
        if (awaitingStreams) return
        awaitingStreams = true
        statusText = "FINDING STREAMS..."
        stremioBackend.resolve_streams(typeName, metaId)
    }

    function toggleLibrary() {
        if (!detail) return
        if (inLibrary) {
            stremioBackend.library_remove(metaId)
        } else {
            stremioBackend.library_add({ id: metaId, name: detail.name, type: typeName, poster: detail.poster })
        }
    }

    focus: true
    Keys.onUpPressed: if (focusRow > 0) focusRow--
    Keys.onDownPressed: if (focusRow < 1) focusRow++
    Keys.onReturnPressed: {
        if (focusRow === 0) play()
        else toggleLibrary()
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: typeName.toUpperCase()
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

    Item {
        visible: detail !== null
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true

        Row {
            id: header
            height: root.sh * 0.35
            spacing: root.sw * 0.0375

            // PLAY / RESUME button
            Rectangle {
                color: focusRow === 0 ? root.accentColor : root.surfaceColor
                border.color: focusRow === 0 ? root.accentColor : root.tertiaryColor
                width: root.sw * 0.1875
                height: root.sh * 0.1166667
                border.width: root.sh * 0.003125

                Text {
                    anchors.centerIn: parent
                    text: (detail && detail.timeOffset > 0) ? "RSUM ►" : "PLAY ►"
                    color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.05
                }
            }

            Column {
                width: root.sw * 0.54375
                spacing: root.sh * 0.0166667

                Text {
                    text: detail ? detail.name : ""
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    width: parent.width
                    font.pixelSize: root.sh * 0.05
                }
                Text {
                    text: {
                        if (!detail) return ""
                        var parts = []
                        if (detail.releaseInfo) parts.push(String(detail.releaseInfo))
                        if (detail.runtime) parts.push(String(detail.runtime))
                        if (detail.imdbRating) parts.push("★ " + detail.imdbRating)
                        return parts.join("  -  ")
                    }
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    width: parent.width
                    font.pixelSize: root.sh * 0.0333333
                }

                Item {
                    id: summaryContainer
                    width: parent.width
                    height: root.sh * 0.1583333
                    clip: true
                    Text {
                        id: summaryText
                        width: parent.width
                        text: detail ? detail.description : ""
                        color: root.primaryColor
                        font.family: root.globalFont
                        wrapMode: Text.WordWrap
                        font.pixelSize: root.sh * 0.0291667
                        lineHeight: 1.3
                    }
                    SequentialAnimation {
                        running: detail !== null && summaryText.implicitHeight > summaryContainer.height
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) summaryText.y = 0
                        PauseAnimation { duration: 3000 }
                        NumberAnimation {
                            target: summaryText; property: "y"
                            to: summaryContainer.height - summaryText.implicitHeight
                            duration: Math.abs(to) * 120
                        }
                        PauseAnimation { duration: 4000 }
                        PropertyAction { target: summaryText; property: "y"; value: 0 }
                    }
                }
            }
        }

        // Library toggle row
        Item {
            id: libraryRow
            anchors.top: header.bottom
            anchors.topMargin: root.sh * 0.02
            anchors.left: parent.left
            width: root.sw * 0.4
            height: root.sh * 0.0583333

            Rectangle {
                anchors.fill: parent
                color: focusRow === 1 ? root.accentColor : "transparent"
            }
            Text {
                text: metaRoot.inLibrary ? "✓ IN LIBRARY  (REMOVE)" : "+ ADD TO LIBRARY"
                color: focusRow === 1 ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.009375
                font.pixelSize: root.sh * 0.0416667
            }
        }

        // Status (finding streams / errors)
        Text {
            anchors.top: libraryRow.bottom
            anchors.topMargin: root.sh * 0.02
            anchors.left: parent.left
            anchors.leftMargin: root.sw * 0.009375
            visible: metaRoot.statusText !== ""
            text: metaRoot.statusText
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0333333
        }
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
