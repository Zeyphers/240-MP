import QtQuick
import Components

FocusScope {
    id: loginRoot

    property var navParams: ({})

    signal navigateTo(string path, var params)
    signal replaceWith(string path, var params)
    signal goBack()

    property int field: 0          // 0 = email, 1 = password
    property bool busy: false
    property string errorMsg: ""

    function submit() {
        if (busy) return
        if (emailInput.text.trim() === "" || passwordInput.text === "") {
            errorMsg = "ENTER EMAIL AND PASSWORD"
            return
        }
        errorMsg = ""
        busy = true
        stremioBackend.login(emailInput.text.trim(), passwordInput.text)
    }

    function focusField(f) {
        field = f
        if (f === 0) emailInput.forceActiveFocus()
        else passwordInput.forceActiveFocus()
    }

    Connections {
        target: stremioBackend
        function onAuthSuccess() {
            loginRoot.busy = false
            loginRoot.replaceWith("Home.qml", {})
        }
        function onErrorOccurred(msg) {
            loginRoot.busy = false
            loginRoot.errorMsg = msg
        }
    }

    Component.onCompleted: focusField(0)

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Sign in"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Column {
        anchors.centerIn: parent
        width: root.sw * 0.5
        spacing: root.sh * 0.05

        // EMAIL
        Column {
            width: parent.width
            spacing: root.sh * 0.0125
            Text {
                text: "EMAIL"
                color: field === 0 ? root.accentColor : root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0291667
            }
            Rectangle {
                width: parent.width
                height: root.sh * 0.06
                color: "transparent"
                border.color: field === 0 ? root.accentColor : root.tertiaryColor
                border.width: root.sh * 0.003125
                TextInput {
                    id: emailInput
                    anchors.fill: parent
                    anchors.leftMargin: root.sw * 0.0125
                    anchors.rightMargin: root.sw * 0.0125
                    verticalAlignment: TextInput.AlignVCenter
                    color: root.primaryColor
                    selectionColor: root.accentColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0416667
                    cursorVisible: field === 0
                    activeFocusOnPress: true
                    inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                    onActiveFocusChanged: if (activeFocus) loginRoot.field = 0
                    Keys.onDownPressed: loginRoot.focusField(1)
                    Keys.onReturnPressed: loginRoot.focusField(1)
                    Keys.onEnterPressed: loginRoot.focusField(1)
                    Keys.onEscapePressed: loginRoot.goBack()
                }
            }
        }

        // PASSWORD
        Column {
            width: parent.width
            spacing: root.sh * 0.0125
            Text {
                text: "PASSWORD"
                color: field === 1 ? root.accentColor : root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0291667
            }
            Rectangle {
                width: parent.width
                height: root.sh * 0.06
                color: "transparent"
                border.color: field === 1 ? root.accentColor : root.tertiaryColor
                border.width: root.sh * 0.003125
                TextInput {
                    id: passwordInput
                    anchors.fill: parent
                    anchors.leftMargin: root.sw * 0.0125
                    anchors.rightMargin: root.sw * 0.0125
                    verticalAlignment: TextInput.AlignVCenter
                    color: root.primaryColor
                    selectionColor: root.accentColor
                    echoMode: TextInput.Password
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0416667
                    cursorVisible: field === 1
                    activeFocusOnPress: true
                    onActiveFocusChanged: if (activeFocus) loginRoot.field = 1
                    Keys.onUpPressed: loginRoot.focusField(0)
                    Keys.onReturnPressed: loginRoot.submit()
                    Keys.onEnterPressed: loginRoot.submit()
                    Keys.onEscapePressed: loginRoot.goBack()
                }
            }
        }

        // Status line
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: loginRoot.busy ? "SIGNING IN..." : loginRoot.errorMsg
            visible: text !== ""
            color: loginRoot.busy ? root.tertiaryColor : root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0333333
            wrapMode: Text.WordWrap
        }
    }

    // Footer
    Text {
        text: root.hints.back + ":BACK  " + root.hints.navigate + ":FIELD  " + root.hints.select + ":SIGN IN"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
