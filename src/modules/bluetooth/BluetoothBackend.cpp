#include "BluetoothBackend.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <QColor>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <cmath>
#include <algorithm>

#ifdef Q_OS_LINUX
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusVariant>
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusMetaType>
#include <unistd.h>   // getuid

// BlueZ D-Bus type aliases.
typedef QMap<QString, QVariantMap>            InterfaceList;   // a{sa{sv}}
typedef QMap<QDBusObjectPath, InterfaceList>  ManagedObjects;  // a{oa{sa{sv}}}
Q_DECLARE_METATYPE(InterfaceList)
Q_DECLARE_METATYPE(ManagedObjects)

static const QString  kBluez      = QStringLiteral("org.bluez");
static const QString  kProps      = QStringLiteral("org.freedesktop.DBus.Properties");
static const QString  kAdapter1   = QStringLiteral("org.bluez.Adapter1");
static const QString  kDevice1    = QStringLiteral("org.bluez.Device1");
static const QString  kPlayer1    = QStringLiteral("org.bluez.MediaPlayer1");
static const QString  kAgentPath  = QStringLiteral("/com/240mp/btagent");

// ── Pairing agent ───────────────────────────────────────────────────────────
// "NoInputNoOutput" agent that auto-accepts pairing/authorization so a phone can
// connect without entering a PIN.
class BtAgent : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")
public:
    using QObject::QObject;
public slots:
    void Release() {}
    QString RequestPinCode(const QDBusObjectPath &) { return QStringLiteral("0000"); }
    uint    RequestPasskey(const QDBusObjectPath &) { return 0; }
    void    DisplayPinCode(const QDBusObjectPath &, const QString &) {}
    void    DisplayPasskey(const QDBusObjectPath &, uint, ushort) {}
    void    RequestConfirmation(const QDBusObjectPath &, uint) {}   // accept
    void    RequestAuthorization(const QDBusObjectPath &) {}        // accept
    void    AuthorizeService(const QDBusObjectPath &, const QString &) {} // accept
    void    Cancel() {}
};

// ── D-Bus signal listener ─────────────────────────────────────────────────────
// Receives raw BlueZ signals (which carry the object path), parses them, and calls
// back into BluetoothBackend with plain Qt types.
class BtListener : public QObject {
    Q_OBJECT
public:
    explicit BtListener(BluetoothBackend *b) : QObject(b), m_b(b) {}
public slots:
    void onPropertiesChanged(const QDBusMessage &msg) {
        const QList<QVariant> a = msg.arguments();
        if (a.size() < 2) return;
        const QString iface = a.at(0).toString();
        const QVariantMap changed = qdbus_cast<QVariantMap>(a.at(1));
        const QString path = msg.path();
        if (iface == kPlayer1) {
            m_b->hPlayer(path, changed);
        } else if (iface == kDevice1 && changed.contains("Connected")) {
            if (changed.value("Connected").toBool()) m_b->hConnected(path);
            else                                     m_b->hDisconnected(path);
        }
    }
    void onInterfacesAdded(const QDBusMessage &msg) {
        const QList<QVariant> a = msg.arguments();
        if (a.size() < 2) return;
        const QString path = a.at(0).value<QDBusObjectPath>().path();
        const InterfaceList ifaces = qdbus_cast<InterfaceList>(a.at(1));
        if (ifaces.contains(kDevice1) && ifaces.value(kDevice1).value("Connected").toBool())
            m_b->hConnected(path);
        if (ifaces.contains(kPlayer1))
            m_b->hPlayer(path, ifaces.value(kPlayer1));
    }
    void onInterfacesRemoved(const QDBusMessage &msg) {
        const QList<QVariant> a = msg.arguments();
        if (a.size() < 2) return;
        const QString path = a.at(0).value<QDBusObjectPath>().path();
        const QStringList ifaces = a.at(1).toStringList();
        // A removed Device1 is a real disconnect. A removed MediaPlayer1 (without
        // the device going away) is just the phone's player object churning —
        // e.g. switching to a video — and must NOT be treated as a disconnect.
        if (ifaces.contains(kDevice1))
            m_b->hDisconnected(path);
        else if (ifaces.contains(kPlayer1))
            m_b->hPlayerRemoved(path);
    }
private:
    BluetoothBackend *m_b;
};

// ── helpers ──────────────────────────────────────────────────────────────────
static QString findAdapter() {
    QDBusInterface om(kBluez, QStringLiteral("/"),
                      QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                      QDBusConnection::systemBus());
    QDBusReply<ManagedObjects> reply = om.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid()) return {};
    const ManagedObjects objs = reply.value();
    for (auto it = objs.constBegin(); it != objs.constEnd(); ++it)
        if (it.value().contains(kAdapter1)) return it.key().path();
    return {};
}

static void setAdapterProp(const QString &adapterPath, const QString &name, const QVariant &v) {
    QDBusInterface p(kBluez, adapterPath, kProps, QDBusConnection::systemBus());
    p.call(QStringLiteral("Set"), kAdapter1, name, QVariant::fromValue(QDBusVariant(v)));
}

static QString deviceAlias(const QString &devicePath) {
    QDBusInterface p(kBluez, devicePath, kProps, QDBusConnection::systemBus());
    QDBusReply<QDBusVariant> r = p.call(QStringLiteral("Get"), kDevice1, QStringLiteral("Alias"));
    return r.isValid() ? r.value().variant().toString() : QStringLiteral("Phone");
}
#endif // Q_OS_LINUX

// ── BluetoothBackend ──────────────────────────────────────────────────────────

BluetoothBackend::BluetoothBackend(const QString &dataRoot, QObject *parent)
    : QObject(parent), m_dataRoot(dataRoot), m_nam(new QNetworkAccessManager(this))
{
    m_posTimer = new QTimer(this);
    m_posTimer->setInterval(1000);
    connect(m_posTimer, &QTimer::timeout, this, &BluetoothBackend::pollPosition);
#ifdef Q_OS_LINUX
    qDBusRegisterMetaType<InterfaceList>();
    qDBusRegisterMetaType<ManagedObjects>();
#endif
}

BluetoothBackend::~BluetoothBackend() = default;

QString BluetoothBackend::receiver_name() { return m_name; }

// Prefer the curated preset dir in the data dir (built by
// scripts/build-projectm-presets.sh — it strips the MilkDrop2 pixel-shader presets
// that render solid white on the Pi's GLES). Fall back to the full system set.
QString BluetoothBackend::preset_dir() {
    const QString curated = m_dataRoot + QStringLiteral("/projectm-presets");
    if (QDir(curated).exists())
        return curated;
    return QStringLiteral("/usr/share/projectM/presets");
}

void BluetoothBackend::set_disconnect_on_exit(bool on) { m_disconnectOnExit = on; }

// "Reset Hidden Presets" — drop the white-preset blacklist so they can be re-tried.
void BluetoothBackend::clear_preset_blacklist() {
    QFile::remove(m_dataRoot + QStringLiteral("/projectm-blacklist.txt"));
}

#ifdef Q_OS_LINUX

void BluetoothBackend::start_receiver() {
    m_adapterPath = findAdapter();
    if (m_adapterPath.isEmpty()) {
        emit errorOccurred(QStringLiteral("NO BLUETOOTH ADAPTER FOUND"));
        return;
    }
    auto bus = QDBusConnection::systemBus();

    // Make the adapter a visible, pairable target named m_name.
    setAdapterProp(m_adapterPath, QStringLiteral("Powered"), true);
    setAdapterProp(m_adapterPath, QStringLiteral("Alias"), m_name);
    setAdapterProp(m_adapterPath, QStringLiteral("Pairable"), true);
    setAdapterProp(m_adapterPath, QStringLiteral("PairableTimeout"), uint(0));
    setAdapterProp(m_adapterPath, QStringLiteral("DiscoverableTimeout"), uint(0));
    setAdapterProp(m_adapterPath, QStringLiteral("Discoverable"), true);

    // Register a just-works pairing agent (best-effort — ignore if one exists).
    if (!m_agent) {
        m_agent = new BtAgent(this);
        bus.registerObject(kAgentPath, m_agent, QDBusConnection::ExportAllSlots);
    }
    QDBusInterface am(kBluez, QStringLiteral("/org/bluez"),
                      QStringLiteral("org.bluez.AgentManager1"), bus);
    am.call(QStringLiteral("RegisterAgent"),
            QVariant::fromValue(QDBusObjectPath(kAgentPath)), QStringLiteral("NoInputNoOutput"));
    am.call(QStringLiteral("RequestDefaultAgent"), QVariant::fromValue(QDBusObjectPath(kAgentPath)));

    // Subscribe to BlueZ signals once.
    if (!m_subscribed) {
        m_listener = new BtListener(this);
        bus.connect(kBluez, QString(), kProps, QStringLiteral("PropertiesChanged"),
                    m_listener, SLOT(onPropertiesChanged(QDBusMessage)));
        bus.connect(kBluez, QStringLiteral("/"),
                    QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                    QStringLiteral("InterfacesAdded"),
                    m_listener, SLOT(onInterfacesAdded(QDBusMessage)));
        bus.connect(kBluez, QStringLiteral("/"),
                    QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                    QStringLiteral("InterfacesRemoved"),
                    m_listener, SLOT(onInterfacesRemoved(QDBusMessage)));
        m_subscribed = true;
    }

    emit receiverNameChanged(m_name);
    startCapture();

    // Pick up a phone that's already connected/playing.
    QDBusInterface om(kBluez, QStringLiteral("/"),
                      QStringLiteral("org.freedesktop.DBus.ObjectManager"), bus);
    QDBusReply<ManagedObjects> reply = om.call(QStringLiteral("GetManagedObjects"));
    if (reply.isValid()) {
        const ManagedObjects objs = reply.value();
        for (auto it = objs.constBegin(); it != objs.constEnd(); ++it) {
            const InterfaceList &ifaces = it.value();
            if (ifaces.contains(kDevice1) && ifaces.value(kDevice1).value("Connected").toBool())
                hConnected(it.key().path());
            if (ifaces.contains(kPlayer1))
                hPlayer(it.key().path(), ifaces.value(kPlayer1));
        }
    }
}

void BluetoothBackend::stop_receiver() {
    m_posTimer->stop();
    stopCapture();
    // Disconnect the phone/computer so it's only connected while this menu is open
    // (unless the user disabled that in Settings). Use an async method call (no
    // QDBusInterface, which would block on D-Bus introspection, and no blocking
    // Disconnect which waits for teardown) so leaving the module is instant.
    if (m_disconnectOnExit && !m_devicePath.isEmpty()) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            kBluez, m_devicePath, kDevice1, QStringLiteral("Disconnect"));
        QDBusConnection::systemBus().asyncCall(msg);
        m_devicePath.clear();
    }
    if (!m_adapterPath.isEmpty())
        setAdapterProp(m_adapterPath, QStringLiteral("Discoverable"), false);
}

void BluetoothBackend::hConnected(const QString &devicePath) {
    m_devicePath = devicePath;
    // Trust the device so services (A2DP/AVRCP) are auto-authorized and it can
    // reconnect cleanly — this is what makes MacBooks / PCs handshake reliably,
    // not just phones.
    QDBusInterface p(kBluez, devicePath, kProps, QDBusConnection::systemBus());
    p.call(QStringLiteral("Set"), kDevice1, QStringLiteral("Trusted"),
           QVariant::fromValue(QDBusVariant(true)));
    m_lastDeviceName = deviceAlias(devicePath);
    emit deviceConnected(m_lastDeviceName);
}

void BluetoothBackend::hDisconnected(const QString &devicePath) {
    if (!m_devicePath.isEmpty() && !devicePath.startsWith(m_devicePath)
        && devicePath != m_playerPath) return;
    m_devicePath.clear();
    m_playerPath.clear();
    m_status.clear();
    m_trackKey.clear();
    m_lastDeviceName.clear();
    m_lastTitle.clear(); m_lastArtist.clear(); m_lastAlbum.clear(); m_lastArt.clear(); m_lastArtColor.clear();
    m_lastDuration = 0; m_lastPosition = 0;
    if (m_posTimer) m_posTimer->stop();
    emit deviceDisconnected();
}

void BluetoothBackend::hPlayerRemoved(const QString &playerPath) {
    // The MediaPlayer1 object disappeared (e.g. the phone switched to a video, or
    // briefly tore down its player). The device is still connected, so keep the
    // last-known track/artwork on screen and just stop position polling — do NOT
    // emit deviceDisconnected. A new player will arrive via InterfacesAdded.
    if (!m_playerPath.isEmpty() && playerPath != m_playerPath) return;
    m_playerPath.clear();
    m_trackKey.clear();   // so the same track on the next player object re-registers
    if (m_posTimer) m_posTimer->stop();
}

void BluetoothBackend::hPlayer(const QString &playerPath, const QVariantMap &props) {
    m_playerPath = playerPath;
    if (props.contains(QStringLiteral("Status"))) {
        m_status = props.value(QStringLiteral("Status")).toString();
        emit statusChanged(m_status);
        if (m_status == QLatin1String("playing")) m_posTimer->start();
        else                                       m_posTimer->stop();
    }
    if (props.contains(QStringLiteral("Track"))) {
        const QVariantMap t = qdbus_cast<QVariantMap>(props.value(QStringLiteral("Track")));
        const QString title  = t.value(QStringLiteral("Title")).toString();
        const QString artist = t.value(QStringLiteral("Artist")).toString();
        const QString album  = t.value(QStringLiteral("Album")).toString();
        const int duration   = t.value(QStringLiteral("Duration")).toInt();
        // Phones re-send identical Track metadata periodically — only react to a
        // genuine change so the artwork doesn't flicker.
        const QString key = title + "" + artist + "" + album;
        // Ignore a wholly empty Track: the player object churns (e.g. switching
        // to a video) and briefly reports blank metadata — keep the last-known
        // track on screen rather than blanking the view to "play something".
        const bool emptyTrack = title.isEmpty() && artist.isEmpty() && album.isEmpty();
        if (key != m_trackKey && !emptyTrack) {
            m_trackKey = key;
            m_lastTitle = title; m_lastArtist = artist; m_lastAlbum = album; m_lastDuration = duration;
            emit trackChanged(title, artist, album, duration);
            fetchArtwork(artist, album, title);
        }
    }
    if (props.contains(QStringLiteral("Position"))) {
        m_lastPosition = props.value(QStringLiteral("Position")).toInt();
        emit positionChanged(m_lastPosition);
    }
}

void BluetoothBackend::pollPosition() {
    if (m_playerPath.isEmpty()) return;
    QDBusInterface p(kBluez, m_playerPath, kProps, QDBusConnection::systemBus());
    QDBusReply<QDBusVariant> r = p.call(QStringLiteral("Get"), kPlayer1, QStringLiteral("Position"));
    if (r.isValid()) { m_lastPosition = r.value().variant().toInt(); emit positionChanged(m_lastPosition); }
}

// ── Audio-reactive visualizer capture ────────────────────────────────────────
// We tap the default sink's monitor with parec (mono 16 kHz s16), run a small FFT
// on each block, and emit normalized per-band levels for the QML visualizer.
static const int    kSampleRate = 16000;
static const int    kFftN       = 512;   // 32 ms blocks → ~31 updates/sec
static const int    kBands      = 24;

static void fft512(float *re, float *im) {
    const int n = kFftN;
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len;
        float wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                float a = re[i + k],           b = im[i + k];
                float c = re[i + k + len / 2], d = im[i + k + len / 2];
                float vr = c * cr - d * ci, vi = c * ci + d * cr;
                re[i + k] = a + vr; im[i + k] = b + vi;
                re[i + k + len / 2] = a - vr; im[i + k + len / 2] = b - vi;
                float ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

void BluetoothBackend::startCapture() {
    if (m_capture) return;
    m_capture = new QProcess(this);
    connect(m_capture, &QProcess::readyReadStandardOutput, this, &BluetoothBackend::processAudio);
    // Ensure parec can reach the user's PipeWire even when we run as a system
    // service (which doesn't inherit XDG_RUNTIME_DIR).
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.value(QStringLiteral("XDG_RUNTIME_DIR")).isEmpty())
        env.insert(QStringLiteral("XDG_RUNTIME_DIR"), QStringLiteral("/run/user/%1").arg(getuid()));
    m_capture->setProcessEnvironment(env);
    // pw-record taps the default sink's output (where the Bluetooth audio plays)
    // and streams raw mono PCM to stdout. It ships with PipeWire — no extra package,
    // and (unlike parec's monitor capture) it actually works on this stack.
    const QStringList args = {
        QStringLiteral("--raw"),
        QStringLiteral("-P"), QStringLiteral("{ stream.capture.sink=true }"),
        QStringLiteral("--rate"),     QString::number(kSampleRate),
        QStringLiteral("--channels"), QStringLiteral("1"),
        QStringLiteral("--format"),   QStringLiteral("s16"),
        QStringLiteral("-"),
    };
    m_capture->start(QStringLiteral("pw-record"), args);
    if (!m_capture->waitForStarted(1500)) {
        qWarning("[Bluetooth] pw-record not available — visualizer will be idle");
        m_capture->deleteLater();
        m_capture = nullptr;
    }
}

void BluetoothBackend::stopCapture() {
    if (m_capture) {
        m_capture->disconnect();
        m_capture->kill();
        m_capture->waitForFinished(500);
        m_capture->deleteLater();
        m_capture = nullptr;
    }
    m_pcm.clear();
    m_peak = 0.0f;
}

void BluetoothBackend::processAudio() {
    if (!m_capture) return;
    m_pcm.append(m_capture->readAllStandardOutput());

    const int blockBytes = kFftN * 2;   // 16-bit mono
    while (m_pcm.size() >= blockBytes) {
        const auto *s = reinterpret_cast<const qint16 *>(m_pcm.constData());
        // Hand the raw mono block to the projectM visualizer (it does its own
        // beat detection / FFT). Cheap copy; only consumed when MilkDrop is shown.
        QVector<float> mono(kFftN);
        for (int i = 0; i < kFftN; i++) mono[i] = s[i] / 32768.0f;
        emit pcmSamples(mono);

        float re[kFftN], im[kFftN];
        for (int i = 0; i < kFftN; i++) {
            float w = 0.5f - 0.5f * std::cos(2.0 * M_PI * i / (kFftN - 1)); // Hann window
            re[i] = (s[i] / 32768.0f) * w;
            im[i] = 0.0f;
        }
        fft512(re, im);

        // Log-spaced bands over bins 2..kFftN/2.
        QVariantList levels;
        levels.reserve(kBands);
        const int minBin = 2, maxBin = kFftN / 2;
        float frameMax = 0.0f;
        float vals[kBands];
        for (int b = 0; b < kBands; b++) {
            int lo = int(minBin * std::pow(double(maxBin) / minBin, double(b) / kBands));
            int hi = int(minBin * std::pow(double(maxBin) / minBin, double(b + 1) / kBands));
            if (hi <= lo) hi = lo + 1;
            float sum = 0.0f;
            for (int k = lo; k < hi && k < maxBin; k++)
                sum += std::sqrt(re[k] * re[k] + im[k] * im[k]);
            float v = sum / (hi - lo);
            vals[b] = v;
            frameMax = std::max(frameMax, v);
        }
        // Adaptive normalization so it auto-scales to loud/quiet without manual gain.
        m_peak = std::max(m_peak * 0.992f, frameMax);
        for (int b = 0; b < kBands; b++) {
            float lvl = (m_peak > 1e-6f) ? std::sqrt(vals[b] / m_peak) : 0.0f;
            levels.append(double(std::min(1.0f, std::max(0.0f, lvl))));
        }
        emit levelsChanged(levels);

        m_pcm.remove(0, blockBytes);
    }
    // Don't let the buffer grow unbounded if QML stalls.
    if (m_pcm.size() > blockBytes * 8) m_pcm.remove(0, m_pcm.size() - blockBytes);
}

static void callPlayer(const QString &playerPath, const QString &method) {
    QDBusInterface pl(kBluez, playerPath, kPlayer1, QDBusConnection::systemBus());
    pl.call(method);
}

void BluetoothBackend::play_pause() {
    if (m_playerPath.isEmpty()) { emit errorOccurred(QStringLiteral("NOTHING PLAYING")); return; }
    callPlayer(m_playerPath, m_status == QLatin1String("playing")
               ? QStringLiteral("Pause") : QStringLiteral("Play"));
}
void BluetoothBackend::next_track() {
    if (!m_playerPath.isEmpty()) callPlayer(m_playerPath, QStringLiteral("Next"));
}
void BluetoothBackend::previous_track() {
    if (!m_playerPath.isEmpty()) callPlayer(m_playerPath, QStringLiteral("Previous"));
}

#else  // ── non-Linux stub (module is excluded from the macOS build) ───────────

void BluetoothBackend::start_receiver() { emit errorOccurred(QStringLiteral("BLUETOOTH IS LINUX-ONLY")); }
void BluetoothBackend::stop_receiver() {}
void BluetoothBackend::hConnected(const QString &) {}
void BluetoothBackend::hDisconnected(const QString &) {}
void BluetoothBackend::hPlayerRemoved(const QString &) {}
void BluetoothBackend::hPlayer(const QString &, const QVariantMap &) {}
void BluetoothBackend::pollPosition() {}
void BluetoothBackend::startCapture() {}
void BluetoothBackend::stopCapture() {}
void BluetoothBackend::processAudio() {}
void BluetoothBackend::play_pause() {}
void BluetoothBackend::next_track() {}
void BluetoothBackend::previous_track() {}

#endif

// ── artwork lookup (platform-independent) ─────────────────────────────────────

// Strip parenthetical/bracketed noise and "feat."/"- Single" suffixes that throw
// off the iTunes match.
static QString cleanTerm(QString s) {
    s.remove(QRegularExpression(QStringLiteral("\\([^)]*\\)")));
    s.remove(QRegularExpression(QStringLiteral("\\[[^\\]]*\\]")));
    s.remove(QRegularExpression(QStringLiteral("(?i)\\bfeat\\.?.*$")));
    s.remove(QRegularExpression(QStringLiteral("(?i)\\bft\\.?.*$")));
    s.remove(QRegularExpression(QStringLiteral("(?i)\\s-\\s(single|ep|remaster).*$")));
    return s.simplified();
}

void BluetoothBackend::searchArtwork(const QString &term, const QString &entity,
                                     const QString &key, std::function<void()> onFail) {
    if (term.trimmed().isEmpty()) { onFail(); return; }
    QUrl url(QStringLiteral("https://itunes.apple.com/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("term"), term);
    q.addQueryItem(QStringLiteral("entity"), entity);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
    url.setQuery(q);

    QNetworkReply *reply = m_nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, onFail]() {
        reply->deleteLater();
        if (key != m_artKey) return;                       // track changed since
        if (reply->error() != QNetworkReply::NoError) { onFail(); return; }
        const QJsonArray results = QJsonDocument::fromJson(reply->readAll())
                                       .object().value("results").toArray();
        if (results.isEmpty()) { onFail(); return; }
        QString art = results.first().toObject().value("artworkUrl100").toString();
        if (art.isEmpty()) { onFail(); return; }
        art.replace("100x100bb", "600x600bb");             // request a larger image
        m_lastArt = art;
        emit artworkReady(art);
        fetchArtColor(art, key);
    });
}

// Download the cover and compute its average colour (scale to 1x1 with smooth
// filtering) so the fullscreen view can tint its background to match.
void BluetoothBackend::fetchArtColor(const QString &url, const QString &key) {
    QNetworkReply *reply = m_nam->get(QNetworkRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
        reply->deleteLater();
        if (key != m_artKey) return;
        if (reply->error() != QNetworkReply::NoError) return;
        QImage img;
        if (!img.loadFromData(reply->readAll())) return;
        QColor c = img.scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixelColor(0, 0);
        m_lastArtColor = c.name();
        emit artworkColorReady(m_lastArtColor);
    });
}

// Re-emit the current state so a view that was just (re)created shows the right
// thing — used when switching between the receiver and the fullscreen visualizer.
void BluetoothBackend::refresh() {
    if (m_lastDeviceName.isEmpty()) { emit deviceDisconnected(); return; }
    emit deviceConnected(m_lastDeviceName);
    if (!m_lastTitle.isEmpty() || !m_lastArtist.isEmpty())
        emit trackChanged(m_lastTitle, m_lastArtist, m_lastAlbum, m_lastDuration);
    if (!m_status.isEmpty())   emit statusChanged(m_status);
    if (m_lastPosition > 0)    emit positionChanged(m_lastPosition);
    emit artworkReady(m_lastArt);
    emit artworkColorReady(m_lastArtColor);
}

void BluetoothBackend::fetchArtwork(const QString &artist, const QString &album, const QString &title) {
    const QString a  = cleanTerm(artist);
    const QString tt = cleanTerm(title);
    const QString al = cleanTerm(album);

    const QString key = (a + "|" + al + "|" + tt).toLower();
    if (key == m_artKey) return;        // already fetching/showing this track
    m_artKey = key;
    m_lastArtColor.clear();
    emit artworkColorReady(QString());  // reset tint until the new cover is analysed

    if (a.isEmpty() && tt.isEmpty()) { emit artworkReady(QString()); return; }

    // Best hit rate: match the exact song first, then fall back to the album,
    // then give up (UI shows the music-note placeholder).
    searchArtwork(a + " " + tt, QStringLiteral("song"), key, [this, a, al, key]() {
        if (al.isEmpty()) { emit artworkReady(QString()); return; }
        searchArtwork(a + " " + al, QStringLiteral("album"), key,
                      [this]() { emit artworkReady(QString()); });
    });
}

#ifdef Q_OS_LINUX
#include "BluetoothBackend.moc"
#endif
