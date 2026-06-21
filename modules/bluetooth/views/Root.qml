import QtQuick

FocusScope {
    id: moduleRoot

    signal goBack()
    property var navParams: ({})

    property string moduleId: "com.240mp.bluetooth"
    property var _moduleInfo: appCore.get_module_info(moduleId)
    property string moduleName: _moduleInfo.name || ""
    property string moduleIcon: _moduleInfo.icon || ""

    property var navStack: []
    property var currentParams: ({})

    function navigateTo(viewPath, params, fromState) {
        var resolved = Qt.resolvedUrl(viewPath)
        navStack.push({ source: internalLoader.source, params: currentParams, listState: fromState || {} })
        currentParams = params || {}
        internalLoader.setSource(resolved, { "navParams": params || {} })
    }

    function navigateBack() {
        if (navStack.length === 0) { moduleRoot.goBack(); return }
        var prev = navStack.pop()
        if (!prev.source || prev.source.toString() === "") { moduleRoot.goBack(); return }
        currentParams = prev.params
        internalLoader.setSource(prev.source, { "navParams": prev.params })
    }

    Loader {
        id: internalLoader
        anchors.fill: parent
        focus: true
        onLoaded: { if (item) item.forceActiveFocus() }
        Connections {
            target: internalLoader.item
            ignoreUnknownSignals: true
            function onNavigateTo(path, params, listState) { moduleRoot.navigateTo(path, params, listState) }
            function onGoBack() { moduleRoot.navigateBack() }
        }
    }

    // Receiver lifecycle lives here (not in a child view) so it survives internal
    // navigation between the receiver and the fullscreen visualizer, and only
    // stops — disconnecting the device — when the whole module is left.
    Component.onCompleted: {
        var d = appCore.get_setting(moduleId, "disconnect_on_exit")
        bluetoothBackend.set_disconnect_on_exit(d === undefined || d === null ? true : (d === true || d === "ON"))
        bluetoothBackend.start_receiver()
        navigateTo("Receiver.qml", {})
    }
    Component.onDestruction: bluetoothBackend.stop_receiver()
}
