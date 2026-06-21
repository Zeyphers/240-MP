import QtQuick
import Components

// Generic meta list. navParams.mode selects the data source:
//   "catalog"           -> load_catalog(transportUrl, type, id, genre, skip)
//   "continue_watching" -> load_continue_watching()
//   "library"           -> load_library()
FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string mode: navParams.mode || "catalog"
    property string listTitle: navParams.title || ""
    property string transportUrl: navParams.transportUrl || ""
    property string typeName: navParams.type || ""
    property string catalogId: navParams.id || ""

    // Genres: prepend "All" so the unfiltered catalog is always reachable.
    property var genres: {
        var g = navParams.genres || []
        return g.length > 0 ? ["All"].concat(g) : []
    }
    property int genreIndex: 0
    property string currentGenre: genres.length > 0 ? genres[genreIndex] : ""

    property var items: []
    property bool isLoading: false
    property bool loadingMore: false
    property int skip: 0
    property int pageSize: 0
    property bool exhausted: false

    function loadData(append) {
        if (append) loadingMore = true
        else { isLoading = true; skip = 0; exhausted = false }
        if (mode === "catalog")
            stremioBackend.load_catalog(transportUrl, typeName, catalogId, currentGenre, skip)
        else if (mode === "continue_watching")
            stremioBackend.load_continue_watching()
        else if (mode === "library")
            stremioBackend.load_library()
    }

    function changeGenre(dir) {
        if (genres.length === 0 || mode !== "catalog") return
        genreIndex = (genreIndex + dir + genres.length) % genres.length
        items = []
        loadData(false)
    }

    Connections {
        target: stremioBackend
        function onMetasLoaded(metas) {
            if (itemsRoot.loadingMore) {
                itemsRoot.loadingMore = false
                if (metas.length === 0) { itemsRoot.exhausted = true; return }
                itemsRoot.items = itemsRoot.items.concat(metas)
            } else {
                itemsRoot.isLoading = false
                itemsRoot.items = metas
                if (itemsRoot.pageSize === 0 && itemsRoot.mode === "catalog")
                    itemsRoot.pageSize = metas.length
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                itemList.currentIndex = Math.min(restore, Math.max(0, metas.length - 1))
                itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
            }
        }
        function onErrorOccurred(msg) {
            itemsRoot.isLoading = false
            itemsRoot.loadingMore = false
        }
    }

    Component.onCompleted: loadData(false)

    function maybePageNext() {
        if (mode !== "catalog" || loadingMore || exhausted) return
        if (pageSize > 0 && itemList.currentIndex >= items.length - 2) {
            skip = items.length
            loadData(true)
        }
    }

    function selectItem() {
        var it = items[itemList.currentIndex]
        if (!it) return
        var view = (it.type === "series") ? "MetaSeries.qml" : "Meta.qml"
        navigateTo(view, { type: it.type, id: it.id, name: it.name },
                   { currentIndex: itemList.currentIndex })
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
        subtitle: currentGenre !== "" && currentGenre !== "All" ? listTitle + " · " + currentGenre.toUpperCase() : listTitle
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Text {
        visible: isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05
    }
    Text {
        visible: !isLoading && items.length === 0
        text: mode === "continue_watching" ? "NOTHING IN PROGRESS"
              : (mode === "library" ? "LIBRARY IS EMPTY" : "NO ITEMS FOUND")
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05
    }

    ListView {
        id: itemList
        model: items
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true
        focus: true

        onCurrentIndexChanged: itemsRoot.maybePageNext()

        Keys.onUpPressed: if (currentIndex > 0) currentIndex--
        Keys.onDownPressed: if (currentIndex < count - 1) currentIndex++
        Keys.onLeftPressed: itemsRoot.changeGenre(-1)
        Keys.onRightPressed: itemsRoot.changeGenre(1)
        Keys.onReturnPressed: itemsRoot.selectItem()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                itemsRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: itemList.width
            height: root.sh * 0.0583333

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, itemList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: itemList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: {
                        var t = modelData.name || ""
                        if (modelData.releaseInfo) t += "  (" + modelData.releaseInfo + ")"
                        if (modelData.progress !== undefined && modelData.progress > 0)
                            t += "  " + Math.round(modelData.progress * 100) + "%"
                        return t
                    }
                    color: itemList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667
                    leftPadding: root.sw * 0.009375
                    rightPadding: root.sw * 0.009375
                    bottomPadding: root.sh * 0.00625
                    font.pixelSize: root.sh * 0.05
                }

                SequentialAnimation {
                    running: (itemList.currentIndex === index) && (rowText.implicitWidth > textClip.width)
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
        text: genres.length > 0
              ? root.hints.back + ":BACK  " + root.hints.navigate + ":NAVIGATE  " + root.hints.change + ":GENRE  " + root.hints.select + ":SELECT"
              : root.hints.back + ":BACK  " + root.hints.navigate + ":NAVIGATE  " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
