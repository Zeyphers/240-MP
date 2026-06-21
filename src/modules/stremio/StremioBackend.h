#pragma once
#include <QObject>
#include <QVariant>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

// StremioBackend — a 240-MP module backend for Stremio.
//
// Two network layers (mirrors how the official clients are split):
//   1. Account layer  — api.strem.io: login, addon collection, library datastore.
//      All calls are POST {endpoint}/api/{method} with a JSON body
//      Object.assign({authKey}, params); response is { result, error }.
//   2. Addon protocol — each installed addon is its own HTTP server addressed by
//      its transportUrl (".../manifest.json"). We hit /catalog, /meta, /stream and
//      /subtitles on it directly.
//
// Streaming model: direct-URL only. Streams that carry a playable `url` (Real
// Debrid / other debrid + plain HTTP addons) are handed straight to mpv. Pure
// torrent streams (infoHash, no url) are filtered out — there is no bundled
// streaming server.
class StremioBackend : public QObject {
    Q_OBJECT
public:
    explicit StremioBackend(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);

    // --- Sync state (no HTTP) ---
    Q_INVOKABLE QString get_auth_state();          // "none" | "authed"
    Q_INVOKABLE QString get_account_name();
    Q_INVOKABLE bool    is_in_library(const QString &metaId);

    // --- Auth ---
    Q_INVOKABLE void login(const QString &email, const QString &password);
    Q_INVOKABLE void logout();

    // --- Browsing ---
    Q_INVOKABLE void load_catalog_menu();                                   // -> catalogMenuLoaded
    Q_INVOKABLE void load_catalog(const QString &transportUrl, const QString &type,
                                  const QString &id, const QString &genre, int skip); // -> metasLoaded
    Q_INVOKABLE void search(const QString &query);                          // -> metasLoaded
    Q_INVOKABLE void load_continue_watching();                             // -> metasLoaded
    Q_INVOKABLE void load_library();                                        // -> metasLoaded

    // --- Detail / playback ---
    Q_INVOKABLE void load_meta(const QString &type, const QString &id);     // -> metaLoaded
    Q_INVOKABLE void resolve_streams(const QString &type, const QString &id); // -> streamsLoaded
    Q_INVOKABLE void load_subtitles(const QString &type, const QString &id,
                                    const QString &videoHash);              // -> subtitlesLoaded

    // --- Library mutations / progress ---
    Q_INVOKABLE void library_add(const QVariant &meta);
    Q_INVOKABLE void library_remove(const QString &metaId);
    Q_INVOKABLE void report_progress(const QString &metaId, const QString &type,
                                     const QString &videoId, int timeOffsetMs,
                                     int durationMs);

    // --- Settings dynamic options / apply ---
    Q_INVOKABLE void getAccounts();
    Q_INVOKABLE void getQualities();
    Q_INVOKABLE void getSubtitleLanguages();
    Q_INVOKABLE void get_resume_playback_options();

signals:
    // Auth
    void authSuccess();
    void logoutComplete();
    void authStateChanged();

    // Browsing / detail
    void catalogMenuLoaded(const QVariant &rows);
    void metasLoaded(const QVariant &metas);
    void metaLoaded(const QVariant &detail);
    void streamsLoaded(const QVariant &streams);
    void subtitlesLoaded(const QVariant &subtitles);

    // Library
    void libraryChanged();

    // Settings + errors
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    void errorOccurred(const QString &message);

private:
    // --- api.strem.io ---
    QString authKey() const;
    void apiRequest(const QString &method, const QJsonObject &params,
                    std::function<void(const QJsonValue &result, const QString &error)> cb);

    // --- addon protocol ---
    static QString baseUrl(const QString &transportUrl);   // strip trailing /manifest.json
    QNetworkReply *httpGet(const QUrl &url);
    void getJson(const QUrl &url, std::function<void(const QJsonObject &obj, bool ok)> cb);
    // Does `manifest` advertise `resource` for `type` (and id prefix, when given)?
    static bool addonSupports(const QJsonObject &manifest, const QString &resource,
                              const QString &type, const QString &id = {});

    // --- formatting ---
    QVariantMap formatMeta(const QJsonObject &m) const;     // catalog/meta item -> QML map
    static QString catalogExtraPath(const QString &genre, int skip);

    // --- addon cache ---
    void loadAddonsFromCache();                             // populate m_addons from disk
    void saveAddons(const QJsonArray &addons);

    // --- library cache ---
    QJsonArray loadLibrary() const;                         // from stremio_library.json
    void saveLibrary(const QJsonArray &items);
    QJsonObject libraryItemFor(const QString &metaId) const;
    void datastorePut(const QJsonObject &item);             // push one item to api.strem.io

    // --- auth file I/O ---
    QJsonObject loadAuth() const;
    void saveAuth(const QJsonObject &auth) const;

    QString  m_appRoot;
    QString  m_dataRoot;
    QString  m_endpoint;        // https://api.strem.io
    QNetworkAccessManager *m_nam;
    QJsonArray m_addons;        // installed addon descriptors {transportUrl, manifest, flags}
    QJsonArray m_library;       // cached library items
};
