#include "StremioBackend.h"
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonValue>
#include <QDateTime>
#include <QNetworkRequest>
#include <QSet>
#include <QDebug>
#include <algorithm>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

StremioBackend::StremioBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
    , m_endpoint(QStringLiteral("https://api.strem.io"))
    , m_nam(new QNetworkAccessManager(this))
{
    loadAddonsFromCache();
    m_library = loadLibrary();
}

// ---------------------------------------------------------------------------
// Auth file I/O
// ---------------------------------------------------------------------------

QJsonObject StremioBackend::loadAuth() const {
    QFile f(m_dataRoot + "/stremio_auth.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
    }
    return {};
}

void StremioBackend::saveAuth(const QJsonObject &auth) const {
    QFile f(m_dataRoot + "/stremio_auth.json");
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("[Stremio] Could not write stremio_auth.json: %s", qPrintable(f.errorString()));
        return;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(auth).toJson(QJsonDocument::Indented));
    f.close();
}

QString StremioBackend::authKey() const {
    return loadAuth()[QStringLiteral("authKey")].toString();
}

// ---------------------------------------------------------------------------
// Addon + library cache I/O
// ---------------------------------------------------------------------------

void StremioBackend::loadAddonsFromCache() {
    QFile f(m_dataRoot + "/stremio_addons.json");
    if (f.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) m_addons = doc.array();
    }
}

void StremioBackend::saveAddons(const QJsonArray &addons) {
    m_addons = addons;
    QFile f(m_dataRoot + "/stremio_addons.json");
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(addons).toJson(QJsonDocument::Compact));
}

QJsonArray StremioBackend::loadLibrary() const {
    QFile f(m_dataRoot + "/stremio_library.json");
    if (f.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) return doc.array();
    }
    return {};
}

void StremioBackend::saveLibrary(const QJsonArray &items) {
    m_library = items;
    QFile f(m_dataRoot + "/stremio_library.json");
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(items).toJson(QJsonDocument::Compact));
}

QJsonObject StremioBackend::libraryItemFor(const QString &metaId) const {
    for (const auto &v : m_library) {
        QJsonObject o = v.toObject();
        if (o["_id"].toString() == metaId) return o;
    }
    return {};
}

// ---------------------------------------------------------------------------
// api.strem.io request helper
// ---------------------------------------------------------------------------

void StremioBackend::apiRequest(const QString &method, const QJsonObject &params,
                                std::function<void(const QJsonValue &, const QString &)> cb) {
    QJsonObject body = params;
    QString key = authKey();
    if (!key.isEmpty()) body["authKey"] = key;

    QNetworkRequest req(QUrl(m_endpoint + "/api/" + method));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // Try to surface a server-provided message even on HTTP error codes.
            QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            QString msg = obj["error"].isObject() ? obj["error"].toObject()["message"].toString()
                                                   : obj["error"].toString();
            if (msg.isEmpty()) msg = reply->errorString();
            cb(QJsonValue::Null, msg);
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (obj.contains("error") && !obj["error"].isNull()) {
            QString msg = obj["error"].isObject() ? obj["error"].toObject()["message"].toString()
                                                  : obj["error"].toString();
            cb(QJsonValue::Null, msg.isEmpty() ? QStringLiteral("Request failed") : msg);
            return;
        }
        cb(obj["result"], QString());
    });
}

// ---------------------------------------------------------------------------
// Addon protocol HTTP helper
// ---------------------------------------------------------------------------

QString StremioBackend::baseUrl(const QString &transportUrl) {
    QString u = transportUrl;
    if (u.endsWith(QStringLiteral("/manifest.json")))
        u.chop(QStringLiteral("/manifest.json").length());
    while (u.endsWith('/')) u.chop(1);
    return u;
}

QNetworkReply *StremioBackend::httpGet(const QUrl &url) {
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return m_nam->get(req);
}

void StremioBackend::getJson(const QUrl &url, std::function<void(const QJsonObject &, bool)> cb) {
    auto *reply = httpGet(url);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { cb({}, false); return; }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        cb(obj, true);
    });
}

bool StremioBackend::addonSupports(const QJsonObject &manifest, const QString &resource,
                                   const QString &type, const QString &id) {
    auto prefixOk = [&](const QJsonArray &prefixes) {
        if (prefixes.isEmpty() || id.isEmpty()) return true;
        for (const auto &p : prefixes)
            if (id.startsWith(p.toString())) return true;
        return false;
    };
    const QJsonArray topTypes    = manifest["types"].toArray();
    const QJsonArray topPrefixes = manifest["idPrefixes"].toArray();

    for (const auto &rv : manifest["resources"].toArray()) {
        if (rv.isString()) {
            if (rv.toString() != resource) continue;
            if (!topTypes.contains(type)) continue;
            if (!prefixOk(topPrefixes)) continue;
            return true;
        }
        QJsonObject r = rv.toObject();
        if (r["name"].toString() != resource) continue;
        QJsonArray rtypes = r.contains("types") ? r["types"].toArray() : topTypes;
        if (!rtypes.contains(type)) continue;
        QJsonArray rprefixes = r.contains("idPrefixes") ? r["idPrefixes"].toArray() : topPrefixes;
        if (!prefixOk(rprefixes)) continue;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Sync state
// ---------------------------------------------------------------------------

QString StremioBackend::get_auth_state() {
    return authKey().isEmpty() ? QStringLiteral("none") : QStringLiteral("authed");
}

QString StremioBackend::get_account_name() {
    QJsonObject user = loadAuth()["user"].toObject();
    QString email = user["email"].toString();
    return email.isEmpty() ? user["_id"].toString() : email;
}

bool StremioBackend::is_in_library(const QString &metaId) {
    QJsonObject it = libraryItemFor(metaId);
    return !it.isEmpty() && !it["removed"].toBool(false) && !it["temp"].toBool(false);
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------

void StremioBackend::login(const QString &email, const QString &password) {
    QJsonObject params{{"email", email}, {"password", password}};
    apiRequest("login", params, [this](const QJsonValue &result, const QString &error) {
        if (!error.isEmpty()) { emit errorOccurred(error.toUpper()); return; }
        QJsonObject res = result.toObject();
        QString key = res["authKey"].toString();
        if (key.isEmpty()) { emit errorOccurred("LOGIN FAILED"); return; }

        QJsonObject auth;
        auth["authKey"] = key;
        auth["user"]    = res["user"].toObject();
        saveAuth(auth);
        emit authStateChanged();

        // Pull the user's addon collection, then their library, then report success.
        apiRequest("addonCollectionGet", QJsonObject{{"update", true}, {"addFromURL", QJsonArray{}}},
                   [this](const QJsonValue &r2, const QString &e2) {
            if (e2.isEmpty()) saveAddons(r2.toObject()["addons"].toArray());
            apiRequest("datastoreGet", QJsonObject{{"collection", "libraryItem"}, {"all", true}},
                       [this](const QJsonValue &r3, const QString &) {
                if (r3.isArray()) saveLibrary(r3.toArray());
                emit authSuccess();
            });
        });
    });
}

void StremioBackend::logout() {
    QString key = authKey();
    if (!key.isEmpty())
        apiRequest("logout", QJsonObject{{"authKey", key}}, [](const QJsonValue &, const QString &) {});
    // Clear local state regardless of the network result.
    QFile::remove(m_dataRoot + "/stremio_auth.json");
    QFile::remove(m_dataRoot + "/stremio_addons.json");
    QFile::remove(m_dataRoot + "/stremio_library.json");
    m_addons = {};
    m_library = {};
    emit authStateChanged();
    emit logoutComplete();
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

QVariantMap StremioBackend::formatMeta(const QJsonObject &m) const {
    QString id = m.contains("id") ? m["id"].toString() : m["imdb_id"].toString();
    QString releaseInfo = m["releaseInfo"].toString();
    if (releaseInfo.isEmpty() && m.contains("year")) releaseInfo = m["year"].toVariant().toString();

    QStringList genres;
    for (const auto &g : m["genres"].toArray()) genres << g.toString();

    return QVariantMap{
        {"id",          id},
        {"type",        m["type"].toString("movie")},
        {"name",        m["name"].toString().toUpper()},
        {"poster",      m["poster"].toString()},
        {"description", m.contains("description") ? m["description"].toString() : m["overview"].toString()},
        {"releaseInfo", releaseInfo},
        {"imdbRating",  m["imdbRating"].toVariant()},
        {"runtime",     m["runtime"].toString()},
        {"genres",      genres},
    };
}

QString StremioBackend::catalogExtraPath(const QString &genre, int skip) {
    QStringList parts;
    if (!genre.isEmpty() && genre != "All")
        parts << "genre=" + QString::fromUtf8(QUrl::toPercentEncoding(genre));
    if (skip > 0)
        parts << "skip=" + QString::number(skip);
    if (parts.isEmpty()) return QString();
    return "/" + parts.join("&");
}

// ---------------------------------------------------------------------------
// Catalog menu
// ---------------------------------------------------------------------------

void StremioBackend::load_catalog_menu() {
    auto build = [this]() {
        QVariantList rows;
        for (const auto &av : m_addons) {
            QJsonObject addon = av.toObject();
            QJsonObject manifest = addon["manifest"].toObject();
            QString transportUrl = addon["transportUrl"].toString();
            QString addonName = manifest["name"].toString();

            for (const auto &cv : manifest["catalogs"].toArray()) {
                QJsonObject cat = cv.toObject();
                QString type = cat["type"].toString();
                QString id   = cat["id"].toString();
                QString name = cat["name"].toString();
                if (type.isEmpty() || id.isEmpty()) continue;

                // Gather genre options and detect search-only catalogs.
                QStringList genres;
                bool searchRequired = false;
                for (const auto &ev : cat["extra"].toArray()) {
                    QJsonObject ex = ev.toObject();
                    QString exName = ex["name"].toString();
                    if (exName == "genre")
                        for (const auto &o : ex["options"].toArray()) genres << o.toString();
                    if (exName == "search" && ex["isRequired"].toBool(false))
                        searchRequired = true;
                }
                // Older manifest form.
                for (const auto &rv : cat["extraRequired"].toArray())
                    if (rv.toString() == "search") searchRequired = true;
                if (searchRequired) continue;   // not browsable from the board

                QString typeLabel = type.toUpper();
                rows.append(QVariantMap{
                    {"transportUrl", transportUrl},
                    {"type",         type},
                    {"id",           id},
                    {"name",         name},
                    {"addonName",    addonName},
                    {"label",        (addonName + " · " + name).toUpper() + "  [" + typeLabel + "]"},
                    {"genres",       genres},
                });
            }
        }
        emit catalogMenuLoaded(rows);
    };

    // Refresh from the server when signed in; fall back to the on-disk cache.
    if (!authKey().isEmpty()) {
        apiRequest("addonCollectionGet", QJsonObject{{"update", true}, {"addFromURL", QJsonArray{}}},
                   [this, build](const QJsonValue &r, const QString &e) {
            if (e.isEmpty() && r.toObject()["addons"].isArray())
                saveAddons(r.toObject()["addons"].toArray());
            build();
        });
    } else {
        build();
    }
}

// ---------------------------------------------------------------------------
// Catalog / search / library / continue watching
// ---------------------------------------------------------------------------

void StremioBackend::load_catalog(const QString &transportUrl, const QString &type,
                                  const QString &id, const QString &genre, int skip) {
    QString url = baseUrl(transportUrl) + "/catalog/" + type + "/" + id
                + catalogExtraPath(genre, skip) + ".json";
    getJson(QUrl(url), [this](const QJsonObject &obj, bool ok) {
        if (!ok) { emit errorOccurred("COULD NOT LOAD CATALOG"); return; }
        QVariantList metas;
        for (const auto &mv : obj["metas"].toArray())
            metas.append(formatMeta(mv.toObject()));
        emit metasLoaded(metas);
    });
}

void StremioBackend::search(const QString &query) {
    QString q = query.trimmed();
    if (q.isEmpty()) { emit metasLoaded(QVariantList{}); return; }

    // Find every catalog that advertises search support.
    struct Target { QString url; };
    QList<QString> urls;
    for (const auto &av : m_addons) {
        QJsonObject addon = av.toObject();
        QJsonObject manifest = addon["manifest"].toObject();
        QString tUrl = addon["transportUrl"].toString();
        for (const auto &cv : manifest["catalogs"].toArray()) {
            QJsonObject cat = cv.toObject();
            bool hasSearch = false;
            for (const auto &ev : cat["extra"].toArray())
                if (ev.toObject()["name"].toString() == "search") hasSearch = true;
            for (const auto &rv : cat["extraSupported"].toArray())
                if (rv.toString() == "search") hasSearch = true;
            if (!hasSearch) continue;
            QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(q));
            urls << baseUrl(tUrl) + "/catalog/" + cat["type"].toString() + "/"
                    + cat["id"].toString() + "/search=" + encoded + ".json";
        }
    }
    if (urls.isEmpty()) { emit metasLoaded(QVariantList{}); return; }

    auto *pending = new int(urls.size());
    auto *acc     = new QVariantList();
    auto *seen    = new QSet<QString>();
    for (const QString &u : urls) {
        getJson(QUrl(u), [this, pending, acc, seen](const QJsonObject &obj, bool ok) {
            if (ok) {
                for (const auto &mv : obj["metas"].toArray()) {
                    QVariantMap m = formatMeta(mv.toObject());
                    QString key = m["type"].toString() + ":" + m["id"].toString();
                    if (seen->contains(key)) continue;
                    seen->insert(key);
                    acc->append(m);
                }
            }
            if (--(*pending) == 0) {
                emit metasLoaded(*acc);
                delete pending; delete acc; delete seen;
            }
        });
    }
}

void StremioBackend::load_continue_watching() {
    auto emitFromCache = [this]() {
        QVariantList out;
        QList<QJsonObject> items;
        for (const auto &v : m_library) items << v.toObject();
        // Most-recently-watched first.
        std::sort(items.begin(), items.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a["state"].toObject()["lastWatched"].toString()
                 > b["state"].toObject()["lastWatched"].toString();
        });
        for (const QJsonObject &o : items) {
            QJsonObject st = o["state"].toObject();
            int off = st["timeOffset"].toInt();
            int dur = st["duration"].toInt();
            if (off <= 0) continue;                         // nothing watched yet
            if (st["flaggedWatched"].toInt() == 1) continue; // marked finished
            if (dur > 0 && off >= dur * 0.95) continue;      // effectively done
            QVariantMap m = formatMeta(o);
            m["videoId"]    = st["video_id"].toString();
            m["timeOffset"] = off;
            m["duration"]   = dur;
            m["progress"]   = dur > 0 ? (double)off / dur : 0.0;
            out.append(m);
        }
        emit metasLoaded(out);
    };

    if (authKey().isEmpty()) { emitFromCache(); return; }
    apiRequest("datastoreGet", QJsonObject{{"collection", "libraryItem"}, {"all", true}},
               [this, emitFromCache](const QJsonValue &r, const QString &) {
        if (r.isArray()) saveLibrary(r.toArray());
        emitFromCache();
    });
}

void StremioBackend::load_library() {
    auto emitFromCache = [this]() {
        QList<QJsonObject> items;
        for (const auto &v : m_library) {
            QJsonObject o = v.toObject();
            if (o["removed"].toBool(false) || o["temp"].toBool(false)) continue;
            items << o;
        }
        std::sort(items.begin(), items.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a["name"].toString().toLower() < b["name"].toString().toLower();
        });
        QVariantList out;
        for (const QJsonObject &o : items) out.append(formatMeta(o));
        emit metasLoaded(out);
    };

    if (authKey().isEmpty()) { emitFromCache(); return; }
    apiRequest("datastoreGet", QJsonObject{{"collection", "libraryItem"}, {"all", true}},
               [this, emitFromCache](const QJsonValue &r, const QString &) {
        if (r.isArray()) saveLibrary(r.toArray());
        emitFromCache();
    });
}

// ---------------------------------------------------------------------------
// Meta detail
// ---------------------------------------------------------------------------

void StremioBackend::load_meta(const QString &type, const QString &id) {
    QString transportUrl;
    for (const auto &av : m_addons) {
        QJsonObject addon = av.toObject();
        if (addonSupports(addon["manifest"].toObject(), "meta", type, id)) {
            transportUrl = addon["transportUrl"].toString();
            break;
        }
    }
    if (transportUrl.isEmpty()) { emit errorOccurred("NO META PROVIDER FOR THIS ITEM"); return; }

    QString url = baseUrl(transportUrl) + "/meta/" + type + "/" + id + ".json";
    getJson(QUrl(url), [this, type, id](const QJsonObject &obj, bool ok) {
        if (!ok) { emit errorOccurred("COULD NOT LOAD DETAILS"); return; }
        QJsonObject meta = obj["meta"].toObject();
        QVariantMap detail = formatMeta(meta);
        detail["inLibrary"] = is_in_library(id);

        // Episodes for series.
        QVariantList videos;
        for (const auto &vv : meta["videos"].toArray()) {
            QJsonObject v = vv.toObject();
            int season  = v.contains("season")  ? v["season"].toInt()  : v["seasonNumber"].toInt();
            int episode = v.contains("episode") ? v["episode"].toInt() : v["episodeNumber"].toInt();
            videos.append(QVariantMap{
                {"id",       v["id"].toString()},
                {"title",    v["title"].toString().isEmpty() ? v["name"].toString() : v["title"].toString()},
                {"season",   season},
                {"episode",  episode},
                {"released", v["released"].toString()},
                {"overview", v["overview"].toString()},
            });
        }
        detail["videos"] = videos;

        // Resume info from the library, if any.
        QJsonObject st = libraryItemFor(id)["state"].toObject();
        detail["timeOffset"] = st["timeOffset"].toInt();
        detail["duration"]   = st["duration"].toInt();
        detail["videoId"]    = st["video_id"].toString();

        emit metaLoaded(detail);
    });
}

// ---------------------------------------------------------------------------
// Streams
// ---------------------------------------------------------------------------

void StremioBackend::resolve_streams(const QString &type, const QString &id) {
    QList<QString> urls;
    for (const auto &av : m_addons) {
        QJsonObject addon = av.toObject();
        if (addonSupports(addon["manifest"].toObject(), "stream", type, id))
            urls << baseUrl(addon["transportUrl"].toString()) + "/stream/" + type + "/" + id + ".json";
    }
    if (urls.isEmpty()) { emit streamsLoaded(QVariantList{}); return; }

    auto rankFor = [](const QString &text) -> int {
        QString t = text.toLower();
        if (t.contains("2160") || t.contains("4k") || t.contains("uhd")) return 4;
        if (t.contains("1080")) return 3;
        if (t.contains("720"))  return 2;
        if (t.contains("480"))  return 1;
        return 0;
    };

    auto *pending = new int(urls.size());
    auto *acc     = new QVariantList();
    for (const QString &u : urls) {
        getJson(QUrl(u), [this, pending, acc, rankFor](const QJsonObject &obj, bool ok) {
            if (ok) {
                for (const auto &sv : obj["streams"].toArray()) {
                    QJsonObject s = sv.toObject();
                    QString url = s["url"].toString();
                    if (url.isEmpty() && !s["ytId"].toString().isEmpty())
                        url = "https://www.youtube.com/watch?v=" + s["ytId"].toString();
                    if (url.isEmpty()) continue;   // skip torrent-only / external streams

                    QString name  = s["name"].toString();
                    QString title = s["title"].toString().isEmpty() ? s["description"].toString()
                                                                    : s["title"].toString();
                    QString label = (name + " " + title).trimmed();
                    label.replace('\n', ' ');

                    // Subtitles embedded in the stream object.
                    QVariantList subs;
                    for (const auto &subv : s["subtitles"].toArray()) {
                        QJsonObject sub = subv.toObject();
                        subs.append(QVariantMap{
                            {"id",   sub["id"].toVariant()},
                            {"url",  sub["url"].toString()},
                            {"lang", sub["lang"].toString()},
                        });
                    }

                    acc->append(QVariantMap{
                        {"url",        url},
                        {"name",       name},
                        {"title",      title},
                        {"label",      label.isEmpty() ? url : label},
                        {"rank",       rankFor(name + " " + title)},
                        {"subtitles",  subs},
                    });
                }
            }
            if (--(*pending) == 0) {
                // Sort best quality first; stable so each addon's order is preserved within a rank.
                std::stable_sort(acc->begin(), acc->end(), [](const QVariant &a, const QVariant &b) {
                    return a.toMap()["rank"].toInt() > b.toMap()["rank"].toInt();
                });
                emit streamsLoaded(*acc);
                delete pending; delete acc;
            }
        });
    }
}

void StremioBackend::load_subtitles(const QString &type, const QString &id, const QString &videoHash) {
    QList<QString> urls;
    for (const auto &av : m_addons) {
        QJsonObject addon = av.toObject();
        if (!addonSupports(addon["manifest"].toObject(), "subtitles", type, id)) continue;
        QString extra = videoHash.isEmpty() ? QString() : "/videoHash=" + videoHash;
        urls << baseUrl(addon["transportUrl"].toString())
                + "/subtitles/" + type + "/" + id + extra + ".json";
    }
    if (urls.isEmpty()) { emit subtitlesLoaded(QVariantList{}); return; }

    auto *pending = new int(urls.size());
    auto *acc     = new QVariantList();
    for (const QString &u : urls) {
        getJson(QUrl(u), [this, pending, acc](const QJsonObject &obj, bool ok) {
            if (ok) {
                for (const auto &subv : obj["subtitles"].toArray()) {
                    QJsonObject sub = subv.toObject();
                    if (sub["url"].toString().isEmpty()) continue;
                    acc->append(QVariantMap{
                        {"id",   sub["id"].toVariant()},
                        {"url",  sub["url"].toString()},
                        {"lang", sub["lang"].toString()},
                    });
                }
            }
            if (--(*pending) == 0) {
                emit subtitlesLoaded(*acc);
                delete pending; delete acc;
            }
        });
    }
}

// ---------------------------------------------------------------------------
// Library mutations + progress
// ---------------------------------------------------------------------------

static QString nowIso() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

void StremioBackend::datastorePut(const QJsonObject &item) {
    // Update the local cache (replace by _id, else append).
    QString id = item["_id"].toString();
    bool replaced = false;
    QJsonArray lib = m_library;
    for (int i = 0; i < lib.size(); ++i) {
        if (lib[i].toObject()["_id"].toString() == id) { lib[i] = item; replaced = true; break; }
    }
    if (!replaced) lib.append(item);
    saveLibrary(lib);

    if (authKey().isEmpty()) return;
    apiRequest("datastorePut",
               QJsonObject{{"collection", "libraryItem"}, {"changes", QJsonArray{item}}},
               [](const QJsonValue &, const QString &e) {
        if (!e.isEmpty()) qWarning("[Stremio] datastorePut failed: %s", qPrintable(e));
    });
}

void StremioBackend::library_add(const QVariant &meta) {
    QVariantMap m = meta.toMap();
    QString id = m["id"].toString();
    if (id.isEmpty()) return;

    QJsonObject item = libraryItemFor(id);
    bool isNew = item.isEmpty();
    item["_id"]   = id;
    item["name"]  = m["name"].toString();
    item["type"]  = m["type"].toString();
    item["poster"] = m["poster"].toString();
    item["removed"] = false;
    item["temp"]    = false;
    if (isNew) {
        item["_ctime"] = nowIso();
        item["state"]  = QJsonObject{{"timeOffset", 0}, {"duration", 0},
                                     {"flaggedWatched", 0}, {"video_id", ""}};
    }
    item["_mtime"] = nowIso();
    datastorePut(item);
    emit libraryChanged();
}

void StremioBackend::library_remove(const QString &metaId) {
    QJsonObject item = libraryItemFor(metaId);
    if (item.isEmpty()) return;
    item["removed"] = true;
    item["_mtime"]  = nowIso();
    datastorePut(item);
    emit libraryChanged();
}

void StremioBackend::report_progress(const QString &metaId, const QString &type,
                                     const QString &videoId, int timeOffsetMs, int durationMs) {
    if (metaId.isEmpty() || timeOffsetMs <= 0) return;

    QJsonObject item = libraryItemFor(metaId);
    bool isNew = item.isEmpty();
    if (isNew) {
        item["_id"]    = metaId;
        item["type"]   = type;
        item["_ctime"] = nowIso();
        item["removed"] = false;
        item["temp"]    = true;   // appears in Continue Watching without being "in library"
    }
    QJsonObject st = item["state"].toObject();
    st["timeOffset"]   = timeOffsetMs;
    st["duration"]     = durationMs;
    st["video_id"]     = videoId.isEmpty() ? metaId : videoId;
    st["lastWatched"]  = nowIso();
    st["flaggedWatched"] = (durationMs > 0 && timeOffsetMs >= durationMs * 0.95) ? 1 : 0;
    item["state"]  = st;
    item["_mtime"] = nowIso();
    datastorePut(item);
}

// ---------------------------------------------------------------------------
// Settings dynamic options
// ---------------------------------------------------------------------------

void StremioBackend::getAccounts() {
    QString name = get_account_name();
    QVariantList opts;
    if (!name.isEmpty()) opts.append(QVariantMap{{"id", name}, {"label", name}});
    emit dynamicOptionsReady("account", opts);
}

void StremioBackend::getQualities() {
    emit dynamicOptionsReady("preferred_quality", QVariantList{
        QVariantMap{{"id", "any"},  {"label", "Any"}},
        QVariantMap{{"id", "2160"}, {"label", "4K (2160p)"}},
        QVariantMap{{"id", "1080"}, {"label", "1080p"}},
        QVariantMap{{"id", "720"},  {"label", "720p"}},
        QVariantMap{{"id", "480"},  {"label", "480p"}},
    });
}

void StremioBackend::getSubtitleLanguages() {
    emit dynamicOptionsReady("subtitle_language", QVariantList{
        QVariantMap{{"id", "off"}, {"label", "Off"}},
        QVariantMap{{"id", "eng"}, {"label", "English"}},
        QVariantMap{{"id", "spa"}, {"label", "Spanish"}},
        QVariantMap{{"id", "fre"}, {"label", "French"}},
        QVariantMap{{"id", "ger"}, {"label", "German"}},
        QVariantMap{{"id", "ita"}, {"label", "Italian"}},
        QVariantMap{{"id", "por"}, {"label", "Portuguese"}},
        QVariantMap{{"id", "dut"}, {"label", "Dutch"}},
        QVariantMap{{"id", "rus"}, {"label", "Russian"}},
    });
}

void StremioBackend::get_resume_playback_options() {
    emit dynamicOptionsReady("resume_playback", QVariantList{
        QVariantMap{{"id", "ask"},    {"label", "Ask"}},
        QVariantMap{{"id", "always"}, {"label", "Always"}},
        QVariantMap{{"id", "never"},  {"label", "Never"}},
    });
}
