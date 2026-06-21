import QtQuick
import Components

// Search with two result columns: Movies (left) and TV Shows (right).
FocusScope {
    id: searchRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var movies: []
    property var series: []
    property bool isLoading: false
    property bool searched: false
    property int col: 0            // 0 = movies, 1 = tv shows

    function runSearch() {
        var q = queryInput.text.trim()
        if (q === "") return
        isLoading = true
        searched = true
        movies = []
        series = []
        stremioBackend.search(q)
    }

    function focusInput() { queryInput.forceActiveFocus() }

    function focusCol(c) {
        // Fall back to the other column if the requested one is empty.
        if (c === 0 && movies.length === 0 && series.length > 0) c = 1
        else if (c === 1 && series.length === 0 && movies.length > 0) c = 0
        col = c
        if (c === 0 && movies.length > 0) { moviesList.currentIndex = Math.max(0, moviesList.currentIndex); moviesList.forceActiveFocus() }
        else if (c === 1 && series.length > 0) { tvList.currentIndex = Math.max(0, tvList.currentIndex); tvList.forceActiveFocus() }
    }

    Connections {
        target: stremioBackend
        function onMetasLoaded(metas) {
            searchRoot.isLoading = false
            var mv = [], sv = []
            for (var i = 0; i < metas.length; i++) {
                if (metas[i].type === "series") sv.push(metas[i])
                else mv.push(metas[i])
            }
            searchRoot.movies = mv
            searchRoot.series = sv
        }
        function onErrorOccurred(msg) { searchRoot.isLoading = false }
    }

    Component.onCompleted: focusInput()

    function openItem(it, idx) {
        if (!it) return
        var view = (it.type === "series") ? "MetaSeries.qml" : "Meta.qml"
        navigateTo(view, { type: it.type, id: it.id, name: it.name }, { currentIndex: idx })
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Search"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    // Search field
    Rectangle {
        id: queryBox
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.21
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.06
        color: "transparent"
        border.color: queryInput.activeFocus ? root.accentColor : root.tertiaryColor
        border.width: root.sh * 0.003125

        TextInput {
            id: queryInput
            anchors.fill: parent
            anchors.leftMargin: root.sw * 0.0125
            anchors.rightMargin: root.sw * 0.0125
            verticalAlignment: TextInput.AlignVCenter
            color: root.primaryColor
            selectionColor: root.accentColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0416667
            cursorVisible: activeFocus
            inputMethodHints: Qt.ImhNoAutoUppercase
            Keys.onReturnPressed: searchRoot.runSearch()
            Keys.onEnterPressed: searchRoot.runSearch()
            Keys.onDownPressed: searchRoot.focusCol(0)
            Keys.onEscapePressed: searchRoot.goBack()
        }
        Text {
            visible: queryInput.text === ""
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: root.sw * 0.0125
            text: "TYPE A TITLE, THEN PRESS ENTER"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333
        }
    }

    Text {
        visible: isLoading
        text: "SEARCHING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05
    }
    Text {
        visible: !isLoading && searched && movies.length === 0 && series.length === 0
        text: "NO RESULTS"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05
    }

    // Column headers
    Row {
        id: colHeaders
        visible: searched && !isLoading && (movies.length + series.length) > 0
        anchors.top: queryBox.bottom
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.03
        anchors.leftMargin: root.sw * 0.115625
        spacing: root.sw * 0.025
        Text {
            width: root.sw * 0.37
            text: "MOVIES (" + movies.length + ")"
            color: col === 0 ? root.accentColor : root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.03
        }
        Text {
            width: root.sw * 0.37
            text: "TV SHOWS (" + series.length + ")"
            color: col === 1 ? root.accentColor : root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.03
        }
    }

    // MOVIES column
    ListView {
        id: moviesList
        model: movies
        anchors.top: colHeaders.bottom
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.015
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.37
        height: root.sh * 0.36
        clip: true

        Keys.onUpPressed: { if (currentIndex > 0) currentIndex--; else searchRoot.focusInput() }
        Keys.onDownPressed: if (currentIndex < count - 1) currentIndex++
        Keys.onRightPressed: searchRoot.focusCol(1)
        Keys.onReturnPressed: searchRoot.openItem(movies[currentIndex], currentIndex)
        Keys.onEscapePressed: searchRoot.goBack()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) { searchRoot.goBack(); event.accepted = true }
        }

        delegate: resultDelegate
        property bool isActiveCol: searchRoot.col === 0 && moviesList.activeFocus
    }

    // TV SHOWS column
    ListView {
        id: tvList
        model: series
        anchors.top: colHeaders.bottom
        anchors.left: moviesList.right
        anchors.topMargin: root.sh * 0.015
        anchors.leftMargin: root.sw * 0.025
        width: root.sw * 0.37
        height: root.sh * 0.36
        clip: true

        Keys.onUpPressed: { if (currentIndex > 0) currentIndex--; else searchRoot.focusInput() }
        Keys.onDownPressed: if (currentIndex < count - 1) currentIndex++
        Keys.onLeftPressed: searchRoot.focusCol(0)
        Keys.onReturnPressed: searchRoot.openItem(series[currentIndex], currentIndex)
        Keys.onEscapePressed: searchRoot.goBack()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) { searchRoot.goBack(); event.accepted = true }
        }

        delegate: resultDelegate
        property bool isActiveCol: searchRoot.col === 1 && tvList.activeFocus
    }

    // Shared delegate for both columns.
    Component {
        id: resultDelegate
        Item {
            width: ListView.view.width
            height: root.sh * 0.0583333
            property bool selected: ListView.view.currentIndex === index && ListView.view.activeFocus

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, ListView.view ? ListView.view.width : parent.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: selected
                }
                Text {
                    id: rowText
                    text: {
                        var t = modelData.name || ""
                        if (modelData.releaseInfo) t += "  (" + modelData.releaseInfo + ")"
                        return t
                    }
                    color: selected ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    topPadding: root.sh * 0.0041667
                    leftPadding: root.sw * 0.009375
                    rightPadding: root.sw * 0.009375
                    bottomPadding: root.sh * 0.00625
                    font.pixelSize: root.sh * 0.04
                }
                SequentialAnimation {
                    running: selected && (rowText.implicitWidth > textClip.width)
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
        text: root.hints.back + ":BACK  " + root.hints.navigate + ":NAVIGATE  " + root.hints.change + ":COLUMN  " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
