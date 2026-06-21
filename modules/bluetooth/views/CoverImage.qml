import QtQuick

// Crossfading cover image: keeps the current art visible until the next one has
// finished loading, then fades across — no pop to black between tracks.
Item {
    id: cf
    property url source: ""
    property int fillMode: Image.PreserveAspectCrop
    property int fadeMs: 300

    // `shown` is the visible image; `loadImg` is where the next one loads.
    property var shown: imgA
    property var loadImg: imgB

    Image {
        id: imgA
        anchors.fill: parent
        fillMode: cf.fillMode
        asynchronous: true; cache: true
        opacity: 1
        Behavior on opacity { NumberAnimation { duration: cf.fadeMs } }
    }
    Image {
        id: imgB
        anchors.fill: parent
        fillMode: cf.fillMode
        asynchronous: true; cache: true
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: cf.fadeMs } }
    }

    onSourceChanged: {
        if (source === "") { shown.opacity = 0; return }   // fade out to placeholder
        if (source === shown.source) return
        loadImg.source = source                            // load behind, fade when ready
    }

    Connections { target: imgA; function onStatusChanged() { cf._maybeFade(imgA) } }
    Connections { target: imgB; function onStatusChanged() { cf._maybeFade(imgB) } }

    function _maybeFade(img) {
        if (img !== loadImg || img.status !== Image.Ready) return
        loadImg.opacity = 1
        shown.opacity = 0
        var t = shown; shown = loadImg; loadImg = t        // swap roles
    }
}
