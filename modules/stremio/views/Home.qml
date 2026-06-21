import QtQuick
import Components

FocusScope {
    id: homeRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var items: []          // combined menu: actions + type headers + catalog rows
    property bool isLoading: true

    // Human-friendly section names for content types.
    function typeLabel(t) {
        var map = { "movie": "MOVIES", "series": "TV SHOWS", "channel": "CHANNELS",
                    "tv": "LIVE TV", "anime": "ANIME", "other": "OTHER" }
        return map[t] || t.toUpperCase()
    }
    // Order sections: movies, series, then the rest alphabetically.
    function typeRank(t) {
        if (t === "movie") return 0
        if (t === "series") return 1
        return 2
    }

    function rebuild(catalogRows) {
        var menu = [
            { kind: "action", action: "search",   label: "SEARCH" },
            { kind: "action", action: "continue", label: "CONTINUE WATCHING" },
            { kind: "action", action: "library",  label: "MY LIBRARY" }
        ]

        // Group catalogs by content type.
        var groups = {}
        var i
        for (i = 0; i < catalogRows.length; i++) {
            var r = catalogRows[i]
            if (!groups[r.type]) groups[r.type] = []
            groups[r.type].push(r)
        }
        var types = Object.keys(groups)
        types.sort(function(a, b) {
            var ra = typeRank(a), rb = typeRank(b)
            return ra !== rb ? ra - rb : (a < b ? -1 : 1)
        })

        for (var t = 0; t < types.length; t++) {
            var type = types[t]
            menu.push({ kind: "header", label: typeLabel(type) })
            var rows = groups[type]
            for (var j = 0; j < rows.length; j++) {
                var c = rows[j]
                menu.push({ kind: "catalog",
                            label: (c.name + "  ·  " + c.addonName).toUpperCase(),
                            transportUrl: c.transportUrl, type: c.type, id: c.id,
                            genres: c.genres, name: c.name })
            }
        }
        items = menu
        isLoading = false
        // Make sure the cursor never rests on a header.
        if (!isSelectable(menuList.currentIndex))
            menuList.currentIndex = nextSelectable(menuList.currentIndex, 1)
    }

    function isSelectable(i) { return items[i] && items[i].kind !== "header" }
    function nextSelectable(from, dir) {
        var i = from + dir
        while (i >= 0 && i < items.length) { if (isSelectable(i)) return i; i += dir }
        return from
    }

    Connections {
        target: stremioBackend
        function onCatalogMenuLoaded(rows) { homeRoot.rebuild(rows) }
        function onErrorOccurred(msg) { homeRoot.isLoading = false }
    }

    Component.onCompleted: {
        rebuild([])                 // show fixed entries immediately
        isLoading = true
        stremioBackend.load_catalog_menu()
        var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
        menuList.currentIndex = isSelectable(restore) ? restore : nextSelectable(restore, 1)
    }

    function selectItem() {
        var it = items[menuList.currentIndex]
        if (!it || it.kind === "header") return
        if (it.action === "search") {
            navigateTo("Search.qml", {}, { currentIndex: menuList.currentIndex })
        } else if (it.action === "continue") {
            navigateTo("Items.qml", { mode: "continue_watching", title: "CONTINUE WATCHING" },
                       { currentIndex: menuList.currentIndex })
        } else if (it.action === "library") {
            navigateTo("Items.qml", { mode: "library", title: "MY LIBRARY" },
                       { currentIndex: menuList.currentIndex })
        } else {
            navigateTo("Items.qml", {
                mode: "catalog", title: it.name.toUpperCase(),
                transportUrl: it.transportUrl, type: it.type, id: it.id, genres: it.genres
            }, { currentIndex: menuList.currentIndex })
        }
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
        subtitle: "Home"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Text {
        visible: isLoading && items.length <= 3
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05
    }

    ListView {
        id: menuList
        model: items
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true
        focus: true

        Keys.onUpPressed: currentIndex = homeRoot.nextSelectable(currentIndex, -1)
        Keys.onDownPressed: currentIndex = homeRoot.nextSelectable(currentIndex, 1)
        Keys.onReturnPressed: homeRoot.selectItem()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                homeRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: menuList.width
            height: modelData.kind === "header" ? root.sh * 0.07 : root.sh * 0.0583333

            // --- Section header (non-selectable) ---
            Text {
                visible: modelData.kind === "header"
                text: modelData.label
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                bottomPadding: root.sh * 0.008
                leftPadding: root.sw * 0.009375
                font.pixelSize: root.sh * 0.03
            }
            Rectangle {
                visible: modelData.kind === "header"
                color: root.tertiaryColor
                height: root.sh * 0.002
                width: root.sw * 0.74
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: root.sw * 0.009375
            }

            // --- Selectable row (action or catalog) ---
            Item {
                id: textClip
                visible: modelData.kind !== "header"
                width: Math.min(rowText.implicitWidth, menuList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: menuList.currentIndex === index
                }
                Text {
                    id: rowText
                    text: modelData.label
                    color: menuList.currentIndex === index ? root.surfaceColor : root.primaryColor
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
                    running: (menuList.currentIndex === index) && (rowText.implicitWidth > textClip.width)
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
