#pragma once
#include <QQuickFramebufferObject>
#include <QVector>
#include <QMutex>
#include <QString>

// ProjectMViz — a QtQuick item that renders the projectM (MilkDrop-compatible)
// music visualizer into its own framebuffer, driven by the live PCM the
// BluetoothBackend captures off the audio sink.
//
// projectM owns a lot of global GL state and must run on the scene-graph render
// thread with a current GL context, so the actual rendering lives in a
// QQuickFramebufferObject::Renderer (defined in the .cpp). This item only holds
// the thread-safe handoff buffers and the QML-facing properties. Linux/Pi-only
// (libprojectM is not present in the macOS build).
class ProjectMViz : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(QObject* audioSource READ audioSource WRITE setAudioSource NOTIFY audioSourceChanged)
    Q_PROPERTY(int presetSeconds READ presetSeconds WRITE setPresetSeconds NOTIFY presetSecondsChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY lockedChanged)
    Q_PROPERTY(QString presetPath READ presetPath WRITE setPresetPath NOTIFY presetPathChanged)
    // Creation-time tuning (read by the renderer when projectM is first built).
    Q_PROPERTY(int quality READ quality WRITE setQuality NOTIFY qualityChanged)            // 0 Perf, 1 Balanced, 2 Quality
    Q_PROPERTY(int blendSeconds READ blendSeconds WRITE setBlendSeconds NOTIFY blendSecondsChanged)
    Q_PROPERTY(qreal sensitivity READ sensitivity WRITE setSensitivity NOTIFY sensitivityChanged)
    Q_PROPERTY(bool beatSwitching READ beatSwitching WRITE setBeatSwitching NOTIFY beatSwitchingChanged)
public:
    enum PresetCmd { None = 0, Next, Prev, Random };

    explicit ProjectMViz(QQuickItem *parent = nullptr);
    Renderer *createRenderer() const override;

    QObject *audioSource() const { return m_audioSource; }
    void setAudioSource(QObject *src);
    int presetSeconds() const { return m_presetSeconds; }
    void setPresetSeconds(int s);
    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool b);
    bool locked() const { return m_locked; }
    void setLocked(bool b);
    QString presetPath() const { return m_presetPath; }
    void setPresetPath(const QString &p);
    int quality() const { return m_quality; }
    void setQuality(int q);
    int blendSeconds() const { return m_blendSeconds; }
    void setBlendSeconds(int s);
    qreal sensitivity() const { return m_sensitivity; }
    void setSensitivity(qreal s);
    bool beatSwitching() const { return m_beatSwitching; }
    void setBeatSwitching(bool b);

    Q_INVOKABLE void nextPreset();
    Q_INVOKABLE void previousPreset();
    Q_INVOKABLE void randomPreset();

    // Render-thread handoff — called by the Renderer from synchronize(), while the
    // GUI thread is blocked, so plain member access is safe there. The mutex guards
    // against onPcm() (GUI thread) racing the drain.
    void drainPcm(QVector<float> &out);
    int  takePresetCmd();

signals:
    void audioSourceChanged();
    void presetSecondsChanged();
    void shuffleChanged();
    void lockedChanged();
    void presetPathChanged();
    void qualityChanged();
    void blendSecondsChanged();
    void sensitivityChanged();
    void beatSwitchingChanged();

public slots:
    void onPcm(const QVector<float> &samples);

private:
    QObject *m_audioSource = nullptr;
    QMutex m_lock;
    QVector<float> m_pcm;
    int m_presetCmd = None;
    int m_presetSeconds = 20;
    bool m_shuffle = true;
    bool m_locked = false;
    QString m_presetPath = QStringLiteral("/usr/share/projectM/presets");
    int m_quality = 1;
    int m_blendSeconds = 3;
    qreal m_sensitivity = 1.0;
    bool m_beatSwitching = false;
};
