import QtQuick
import Bluetooth 1.0

// projectM (MilkDrop) visualizer surface. Loaded by VizFull while the Shuffle or
// Cycle screen is active (one instance reused across both — VizFull flips
// `shuffle`/`locked`). `import Bluetooth` resolves the ProjectMViz C++ type (Pi-only).
// All projectM tuning is read once here from module settings (the settings UI lives
// outside the module, so values can't change while this is on screen).
Item {
    id: milkRoot

    // Driven by VizFull per screen.
    property bool shuffle: true   // Shuffle screen: auto-cycle all presets
    property bool locked: false   // Cycle screen: stay on the current preset

    function nextPreset()     { viz.nextPreset() }
    function previousPreset() { viz.previousPreset() }

    function _s(key, def) { var v = appCore.get_setting(moduleRoot.moduleId, key); return (v === undefined || v === null || v === "") ? def : v }
    function _bool(key, def) { var v = _s(key, def); return (v === true || v === "ON") }

    property int presetSeconds: parseInt(_s("milkdrop_preset_seconds", "20")) || 20

    property int quality: { var q = _s("visualizer_quality", "Balanced");
        return q === "Performance" ? 0 : (q === "Quality" ? 2 : 1) }
    property int blend: { var b = _s("preset_blend", "Smooth");
        return b === "Instant" ? 0 : (b === "Fast" ? 1 : (b === "Slow" ? 6 : 3)) }
    property real sensitivity: { var s = _s("visualizer_sensitivity", "Medium");
        return s === "Low" ? 0.6 : (s === "High" ? 1.8 : 1.0) }
    property bool beatSwitching: _bool("beat_switching", false)

    ProjectMViz {
        id: viz
        anchors.fill: parent
        audioSource: bluetoothBackend
        presetPath: bluetoothBackend.preset_dir()
        presetSeconds: milkRoot.presetSeconds
        shuffle: milkRoot.shuffle
        locked: milkRoot.locked
        quality: milkRoot.quality
        blendSeconds: milkRoot.blend
        sensitivity: milkRoot.sensitivity
        beatSwitching: milkRoot.beatSwitching
    }
}
