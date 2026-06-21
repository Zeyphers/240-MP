#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QByteArray>
#include <QVector>
#include <functional>

class QNetworkAccessManager;
class QTimer;
class QProcess;

// BluetoothBackend — turns the Pi into a Bluetooth A2DP audio receiver.
//
// When the module opens we make the adapter discoverable + pairable and register
// a "just works" pairing agent, so a phone can find "240-MP" and connect. Once a
// phone is streaming, BlueZ exposes the now-playing metadata (title/artist/album)
// and transport controls over D-Bus (org.bluez.MediaPlayer1); we surface those to
// QML and forward Play/Pause/Next/Previous back to the phone. Album art isn't sent
// over Bluetooth, so we look it up online from the iTunes Search API by artist+album.
//
// All BlueZ/D-Bus work is Linux-only (compiled only on the Pi). The header stays
// free of QtDBus types so the rest of the app builds unchanged on macOS.
class BluetoothBackend : public QObject {
    Q_OBJECT
public:
    explicit BluetoothBackend(const QString &dataRoot, QObject *parent = nullptr);
    ~BluetoothBackend() override;

    Q_INVOKABLE void    start_receiver();   // make discoverable, begin listening
    Q_INVOKABLE void    stop_receiver();    // stop being discoverable
    Q_INVOKABLE QString receiver_name();    // the name shown on the phone
    Q_INVOKABLE QString preset_dir();       // curated projectM preset dir (falls back to system)
    Q_INVOKABLE void    set_disconnect_on_exit(bool on); // honour the Settings toggle
    Q_INVOKABLE void    clear_preset_blacklist();        // "Reset Hidden Presets" action
    Q_INVOKABLE void    play_pause();
    Q_INVOKABLE void    next_track();
    Q_INVOKABLE void    previous_track();
    Q_INVOKABLE void    refresh();           // re-emit current state (for view switches)

    // Internal handlers called by the D-Bus listener (Linux only). Plain Qt types
    // only, so this header never needs QtDBus.
    void hConnected(const QString &devicePath);
    void hDisconnected(const QString &devicePath);
    void hPlayer(const QString &playerPath, const QVariantMap &props);
    void hPlayerRemoved(const QString &playerPath); // player object went away (e.g. switch to video) — keep device connected

signals:
    void receiverNameChanged(const QString &name);
    void deviceConnected(const QString &name);
    void deviceDisconnected();
    void trackChanged(const QString &title, const QString &artist, const QString &album, int durationMs);
    void statusChanged(const QString &status);   // "playing" | "paused" | "stopped"
    void positionChanged(int positionMs);
    void artworkReady(const QString &url);        // "" when none found
    void artworkColorReady(const QString &color); // average color of the cover (hex), "" if none
    void levelsChanged(const QVariant &levels);   // QList<double> 0..1 per band
    void pcmSamples(const QVector<float> &samples); // raw mono PCM blocks for the projectM visualizer
    void errorOccurred(const QString &message);

private:
    void fetchArtwork(const QString &artist, const QString &album, const QString &title);
    void searchArtwork(const QString &term, const QString &entity,
                       const QString &key, std::function<void()> onFail);
    void fetchArtColor(const QString &url, const QString &key);
    void pollPosition();
    void startCapture();
    void stopCapture();
    void processAudio();

    QString m_dataRoot;
    QString m_name = QStringLiteral("240-MP");
    QString m_adapterPath;
    QString m_devicePath;
    QString m_playerPath;
    QString m_status;
    QString m_artKey;
    QString m_trackKey;
    // last-known state, so a view that's recreated can be repopulated via refresh()
    QString m_lastDeviceName;
    QString m_lastTitle, m_lastArtist, m_lastAlbum, m_lastArt, m_lastArtColor;
    int     m_lastDuration = 0;
    int     m_lastPosition = 0;
    bool    m_subscribed = false;
    bool    m_disconnectOnExit = true;
    QTimer *m_posTimer = nullptr;
    QProcess *m_capture = nullptr;     // parec — captures the output for the visualizer
    QByteArray m_pcm;                  // accumulating PCM bytes
    float   m_peak = 0.0f;             // adaptive normalization for level scaling
    QNetworkAccessManager *m_nam = nullptr;
    QObject *m_agent = nullptr;     // BtAgent (defined in the .cpp, Linux only)
    QObject *m_listener = nullptr;  // BtListener (defined in the .cpp, Linux only)
};
