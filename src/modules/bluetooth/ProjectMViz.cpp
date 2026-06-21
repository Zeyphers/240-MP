#include "ProjectMViz.h"
#include "BluetoothBackend.h"

#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QByteArray>
#include <QDebug>

#include <libprojectM/projectM.hpp>
#include <libprojectM/PCM.hpp>

// ── Renderer (scene-graph render thread) ──────────────────────────────────────
// We take full control of preset selection: projectM's own auto-advance is locked
// off, and we drive switching ourselves so that
//   • Shuffle picks a *random* preset from the whole library (not the next one), and
//   • presets that render solid white on this GLES stack are detected (glReadPixels),
//     skipped, and blacklisted (persisted) so they never come back.
class ProjectMRenderer : public QQuickFramebufferObject::Renderer {
public:
    ~ProjectMRenderer() override { delete m_pm; }

    void synchronize(QQuickFramebufferObject *item) override {
        auto *v = static_cast<ProjectMViz *>(item);
        v->drainPcm(m_pcm);
        m_cmd = v->takePresetCmd();
        m_presetSeconds = v->presetSeconds();
        m_shuffle = v->shuffle();
        m_presetPath = v->presetPath();
        m_quality = v->quality();
        m_blendSeconds = v->blendSeconds();
        m_sensitivity = v->sensitivity();
        m_beatSwitching = v->beatSwitching();
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        return new QOpenGLFramebufferObject(size, fmt);
    }

    void render() override {
        QOpenGLFramebufferObject *fbo = framebufferObject();
        const QSize size = fbo ? fbo->size() : QSize();
        if (size.isEmpty()) return;
        if (!m_gl) m_gl = QOpenGLContext::currentContext()->functions();

        if (!m_pm) {
            createProjectM(size);
            m_glSize = size;
            if (!m_pm) return;
        } else if (size != m_glSize) {
            m_pm->projectM_resetGL(size.width(), size.height());
            m_glSize = size;
        }

        // Feed audio, and track energy for our own beat detection (projectM's
        // internal beat-cut is disabled by the preset lock, so we do it ourselves).
        m_lastBeat = false;
        if (!m_pcm.isEmpty()) {
            double e = 0.0;
            for (float x : m_pcm) e += double(x) * x;
            e /= m_pcm.size();
            if (m_energyAvg > 1e-5 && e > m_energyAvg * 1.6) m_lastBeat = true;
            m_energyAvg = m_energyAvg * 0.9 + e * 0.1;
            m_pm->pcm()->addPCMfloat(m_pcm.constData(), m_pcm.size());
            m_pcm.clear();
        }

        // When entering Shuffle, jump straight to a fresh random preset.
        if (m_shuffle && !m_wasShuffle) selectRandomGood();
        m_wasShuffle = m_shuffle;

        // Manual nav (Cycle screen) / explicit commands.
        switch (m_cmd) {
            case ProjectMViz::Next:   m_lastDir =  1; stepGood( 1); break;
            case ProjectMViz::Prev:   m_lastDir = -1; stepGood(-1); break;
            case ProjectMViz::Random: selectRandomGood();           break;
            default: break;
        }
        m_cmd = ProjectMViz::None;

        m_pm->renderFrame();
        m_framesSinceSwitch++;

        // White-preset detection: sample the just-rendered frame twice; if it's a
        // blank white frame both times, blacklist this preset and move on.
        if (!m_checkedCurrent && (m_framesSinceSwitch == 10 || m_framesSinceSwitch == 20)) {
            if (frameIsWhite(fbo)) {
                if (++m_whiteHits >= 2) { blacklistCurrent(); skip(); }
            } else {
                m_checkedCurrent = true;  // confirmed good, stop checking
                m_whiteHits = 0;
            }
        }

        // Shuffle advance (we own the timer; projectM's is locked off): on a strong
        // beat when beat-switching is enabled (min ~3s apart), else on the interval.
        if (m_shuffle) {
            const bool beat  = m_beatSwitching && m_lastBeat && m_framesSinceSwitch >= 90;
            const bool timed = m_framesSinceSwitch >= m_presetSeconds * 30;
            if (beat || timed) selectRandomGood();
        }

        update();   // keep the animation running
    }

private:
    void createProjectM(const QSize &size) {
        // Quality → mesh + internal texture size (perf vs sharpness on the Pi).
        int mx = 48, my = 32, tex = 1024;
        if (m_quality <= 0)      { mx = 32; my = 24; tex = 512;  }   // Performance
        else if (m_quality == 1) { mx = 48; my = 32; tex = 1024; }   // Balanced
        else                     { mx = 64; my = 48; tex = 1536; }   // Quality

        projectM::Settings s;
        s.meshX = mx;
        s.meshY = my;
        s.fps = 30;
        s.textureSize = tex;
        s.windowWidth = size.width();
        s.windowHeight = size.height();
        s.presetURL = m_presetPath.toStdString();
        s.titleFontURL = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
        s.menuFontURL  = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
        s.datadir = "/usr/share/projectM";
        s.presetDuration = m_presetSeconds > 0 ? m_presetSeconds : 20;
        s.smoothPresetDuration = m_blendSeconds;
        s.hardcutEnabled = m_beatSwitching;
        s.hardcutDuration = 8;
        s.hardcutSensitivity = 1.0f;
        s.beatSensitivity = float(m_sensitivity);
        s.aspectCorrection = true;
        s.shuffleEnabled = false;        // we do our own random selection
        s.softCutRatingsEnabled = false;
        try {
            m_pm = new projectM(s, projectM::FLAG_NONE);
            m_pm->projectM_resetGL(size.width(), size.height());
            m_pm->setPresetLock(true);   // disable projectM's internal auto-advance
        } catch (...) {
            qWarning("[ProjectMViz] failed to initialise projectM");
            delete m_pm;
            m_pm = nullptr;
            return;
        }
        m_playlist = m_pm->getPlaylistSize();
        loadBlacklist();
        m_wasShuffle = m_shuffle;
        if (m_shuffle) selectRandomGood();
        else           stepGood(1);      // land on the first non-blacklisted preset
    }

    // ---- preset selection helpers ----
    QString urlAt(unsigned int idx) const {
        return m_pm ? QString::fromStdString(m_pm->getPresetURL(idx)) : QString();
    }
    bool blacklisted(unsigned int idx) const {
        return m_blacklist.contains(urlAt(idx));
    }
    void selectIndex(unsigned int idx) {
        m_pm->selectPreset(idx, true);
        m_framesSinceSwitch = 0;
        m_checkedCurrent = false;
        m_whiteHits = 0;
    }
    void selectRandomGood() {
        if (!m_pm || m_playlist == 0) return;
        for (int tries = 0; tries < 50; ++tries) {
            unsigned int idx = QRandomGenerator::global()->bounded(int(m_playlist));
            if (!blacklisted(idx)) { selectIndex(idx); return; }
        }
        selectIndex(QRandomGenerator::global()->bounded(int(m_playlist)));
    }
    void stepGood(int dir) {
        if (!m_pm || m_playlist == 0) return;
        unsigned int cur = 0; m_pm->selectedPresetIndex(cur);
        const int n = int(m_playlist);
        for (int k = 1; k <= n; ++k) {
            int idx = (int(cur) + dir * k) % n;
            if (idx < 0) idx += n;
            if (!blacklisted(idx)) { selectIndex(idx); return; }
        }
    }
    void skip() { if (m_shuffle) selectRandomGood(); else stepGood(m_lastDir); }

    // ---- white-frame detection ----
    bool frameIsWhite(QOpenGLFramebufferObject *fbo) {
        if (!m_gl || !fbo) return false;
        fbo->bind();   // ensure we read our FBO regardless of projectM's end state
        const int w = fbo->width(), h = fbo->height();
        if (w <= 0 || h <= 0) return false;
        auto rowWhite = [&](int y) -> bool {
            QByteArray buf(w * 4, 0);
            m_gl->glReadPixels(0, y, w, 1, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
            const uchar *p = reinterpret_cast<const uchar *>(buf.constData());
            int white = 0;
            for (int i = 0; i < w; ++i)
                if (p[i*4] >= 248 && p[i*4+1] >= 248 && p[i*4+2] >= 248) ++white;
            return white >= int(w * 0.98);
        };
        return rowWhite(h / 2) && rowWhite(h / 3);
    }

    // ---- blacklist persistence ----
    QString blacklistFile() const {
        if (m_presetPath.isEmpty()) return QString();
        // Sibling of the preset dir, e.g. .../240-MP/projectm-blacklist.txt
        return QFileInfo(m_presetPath).dir().filePath(QStringLiteral("projectm-blacklist.txt"));
    }
    void loadBlacklist() {
        m_blacklist.clear();
        const QString path = blacklistFile();
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (!line.isEmpty()) m_blacklist.insert(line);
        }
    }
    void blacklistCurrent() {
        if (!m_pm) return;
        unsigned int cur = 0;
        if (!m_pm->selectedPresetIndex(cur)) return;
        const QString url = urlAt(cur);
        if (url.isEmpty() || m_blacklist.contains(url)) return;
        m_blacklist.insert(url);
        const QString path = blacklistFile();
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::Append | QIODevice::Text))
            f.write(url.toUtf8() + '\n');
    }

    projectM *m_pm = nullptr;
    QOpenGLFunctions *m_gl = nullptr;
    QSize m_glSize;
    QVector<float> m_pcm;
    int m_cmd = ProjectMViz::None;
    int m_presetSeconds = 20;
    bool m_shuffle = true;
    bool m_wasShuffle = true;
    QString m_presetPath;
    int m_quality = 1;
    int m_blendSeconds = 3;
    qreal m_sensitivity = 1.0;
    bool m_beatSwitching = false;

    unsigned int m_playlist = 0;
    QSet<QString> m_blacklist;
    int m_framesSinceSwitch = 0;
    bool m_checkedCurrent = false;
    int m_whiteHits = 0;
    int m_lastDir = 1;
    double m_energyAvg = 0.0;
    bool m_lastBeat = false;
};

// ── Item (GUI thread) ─────────────────────────────────────────────────────────

ProjectMViz::ProjectMViz(QQuickItem *parent) : QQuickFramebufferObject(parent) {
    // FBO content is bottom-up relative to QtQuick's top-left origin.
    setMirrorVertically(true);
}

QQuickFramebufferObject::Renderer *ProjectMViz::createRenderer() const {
    return new ProjectMRenderer();
}

void ProjectMViz::setAudioSource(QObject *src) {
    if (m_audioSource == src) return;
    if (auto *old = qobject_cast<BluetoothBackend *>(m_audioSource))
        disconnect(old, &BluetoothBackend::pcmSamples, this, &ProjectMViz::onPcm);
    m_audioSource = src;
    if (auto *bt = qobject_cast<BluetoothBackend *>(src))
        connect(bt, &BluetoothBackend::pcmSamples, this, &ProjectMViz::onPcm);
    emit audioSourceChanged();
}

void ProjectMViz::onPcm(const QVector<float> &samples) {
    QMutexLocker lock(&m_lock);
    m_pcm += samples;
    const int cap = 8192;
    if (m_pcm.size() > cap) m_pcm.remove(0, m_pcm.size() - cap);
    lock.unlock();
    update();
}

void ProjectMViz::drainPcm(QVector<float> &out) {
    QMutexLocker lock(&m_lock);
    out += m_pcm;
    m_pcm.clear();
}

int ProjectMViz::takePresetCmd() {
    QMutexLocker lock(&m_lock);
    int c = m_presetCmd;
    m_presetCmd = None;
    return c;
}

void ProjectMViz::nextPreset()     { { QMutexLocker l(&m_lock); m_presetCmd = Next; }   update(); }
void ProjectMViz::previousPreset() { { QMutexLocker l(&m_lock); m_presetCmd = Prev; }   update(); }
void ProjectMViz::randomPreset()   { { QMutexLocker l(&m_lock); m_presetCmd = Random; } update(); }

void ProjectMViz::setPresetSeconds(int s) {
    if (s == m_presetSeconds) return;
    m_presetSeconds = s;
    emit presetSecondsChanged();
}
void ProjectMViz::setShuffle(bool b) {
    if (b == m_shuffle) return;
    m_shuffle = b;
    emit shuffleChanged();
    update();
}
void ProjectMViz::setLocked(bool b) {
    if (b == m_locked) return;
    m_locked = b;
    emit lockedChanged();
    update();
}
void ProjectMViz::setPresetPath(const QString &p) {
    if (p == m_presetPath) return;
    m_presetPath = p;
    emit presetPathChanged();
}
void ProjectMViz::setQuality(int q) {
    if (q == m_quality) return;
    m_quality = q;
    emit qualityChanged();
}
void ProjectMViz::setBlendSeconds(int s) {
    if (s == m_blendSeconds) return;
    m_blendSeconds = s;
    emit blendSecondsChanged();
}
void ProjectMViz::setSensitivity(qreal s) {
    if (qFuzzyCompare(s, m_sensitivity)) return;
    m_sensitivity = s;
    emit sensitivityChanged();
}
void ProjectMViz::setBeatSwitching(bool b) {
    if (b == m_beatSwitching) return;
    m_beatSwitching = b;
    emit beatSwitchingChanged();
}
