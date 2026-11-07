#include "zalo_session.h"
#include "zalo_crypto.h"

#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QImageReader>
#include <QJsonDocument>
#include <QBuffer>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>
#include <QWebSocket>
#include <QtEndian>
#include <QCryptographicHash>

#include <memory>

namespace Zalos::Core::Network {

const QString ZaloSession::kDefaultUserAgent =
    QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                   "Chrome/130.0.0.0 Safari/537.36");

namespace {

QString compactJson(const QJsonObject& obj) {
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QByteArray encodeQuery(const QList<QPair<QString, QString>>& items) {
    QByteArray out;
    for (const auto& item : items) {
        if (!out.isEmpty()) out.append('&');
        out.append(QUrl::toPercentEncoding(item.first));
        out.append('=');
        out.append(QUrl::toPercentEncoding(item.second));
    }
    return out;
}

QJsonValue parseMaybeJsonString(const QJsonValue& value) {
    if (!value.isString()) return value;
    const QJsonDocument doc = QJsonDocument::fromJson(value.toString().toUtf8());
    if (doc.isObject()) return doc.object();
    if (doc.isArray()) return doc.array();
    return value;
}

}

ZaloSession::ZaloSession(QNetworkAccessManager* nam, SessionCookieJar* jar, QObject* parent)
    : QObject(parent), m_nam(nam), m_jar(jar) {
    m_pongWatchdog.setSingleShot(true);
    connect(&m_pongWatchdog, &QTimer::timeout, this, [this]() {
        logRealtime(QStringLiteral("no frame 15 s after ping: reconnecting"));
        QMetaObject::invokeMethod(this, [this]() { if (m_ws) m_ws->abort(); }, Qt::QueuedConnection);
    });

    m_pingTimer.setSingleShot(false);
    connect(&m_pingTimer, &QTimer::timeout, this, [this]() {
        sendWs(2, 1, QJsonObject{{"eventId", QDateTime::currentMSecsSinceEpoch()}}, QString());
        m_pongWatchdog.start(15000);
    });
}

void ZaloSession::logRealtime(const QString& line) const {
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("logs");
    QDir().mkpath(dir);
    QFile f(QDir(dir).filePath(QStringLiteral("realtime.log")));
    if (f.size() > 512 * 1024) f.remove();
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    f.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " " + line + "\n").toUtf8());
}

void ZaloSession::reset() {
    disconnectRealtime();
    m_uploadWaiters.clear();
    m_earlyFileDone.clear();
    m_secretKey.clear();
    m_uid.clear();
    m_loginInfo = {};
    m_serverSettings = {};
    m_extraVer = {};
}

QString ZaloSession::phonebookVersion() const {
    return QString::number(m_extraVer.value("phonebook").toVariant().toLongLong());
}

bool ZaloSession::saveCredentials(const QString& path) const {
    if (!m_jar || m_imei.isEmpty()) return false;
    QJsonArray cookies;
    for (const QNetworkCookie& c : m_jar->cookies()) {
        if (!c.domain().contains("zalo.me")) continue;
        QJsonObject obj{
            {"name", QString::fromUtf8(c.name())},
            {"value", QString::fromUtf8(c.value())},
            {"domain", c.domain()},
            {"path", c.path()},
        };
        if (c.expirationDate().isValid()) obj["expires"] = c.expirationDate().toSecsSinceEpoch();
        cookies.append(obj);
    }
    const QJsonObject root{{"imei", m_imei}, {"userAgent", m_userAgent}, {"cookies", cookies}};

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

bool ZaloSession::loadCredentials(const QString& path, SessionCookieJar* jar, QString* imei, QString* userAgent) {
    QFile file(path);
    if (!jar || !file.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value("imei").toString().isEmpty() || root.value("cookies").toArray().isEmpty()) return false;
    importCookies(jar, root.value("cookies").toArray());
    if (imei) *imei = root.value("imei").toString();
    if (userAgent) *userAgent = root.value("userAgent").toString(kDefaultUserAgent);
    return true;
}

void ZaloSession::importCookies(SessionCookieJar* jar, const QJsonArray& cookies) {
    for (const QJsonValue& v : cookies) {
        const QJsonObject obj = v.toObject();
        QNetworkCookie cookie(obj.value("name").toString().toUtf8(), obj.value("value").toString().toUtf8());
        QString domain = obj.value("domain").toString(".chat.zalo.me");
        if (!domain.startsWith('.')) domain.prepend('.');
        cookie.setDomain(domain);
        cookie.setPath(obj.value("path").toString("/"));
        if (obj.contains("expires")) {
            cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(obj.value("expires").toVariant().toLongLong()));
        }
        jar->insertCookie(cookie);
    }
}

void ZaloSession::login(const QString& imei, const QString& userAgent, const QString& language) {
    reset();
    m_imei = imei;
    m_userAgent = userAgent.isEmpty() ? kDefaultUserAgent : userAgent;
    m_language = language.isEmpty() ? QStringLiteral("vi") : language;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QJsonObject data{{"computer_name", "Web"}, {"imei", m_imei}, {"language", m_language}, {"ts", now}};
    const ZaloCrypto::ParamsEncryptor encryptor(kApiType, m_imei, now);

    QMap<QString, QString> params{
        {"zcid", encryptor.zcid()},
        {"zcid_ext", encryptor.zcidExt()},
        {"enc_ver", encryptor.encVersion()},
        {"params", encryptor.encrypt(compactJson(data).toUtf8())},
        {"type", QString::number(kApiType)},
        {"client_version", QString::number(kApiVersion)},
    };
    const QString sign = ZaloCrypto::signKey("getlogininfo", params);

    QList<QPair<QString, QString>> query;
    for (auto it = params.cbegin(); it != params.cend(); ++it) query.append(qMakePair(it.key(), it.value()));
    query.append(qMakePair(QStringLiteral("signkey"), sign));
    query.append(qMakePair(QStringLiteral("nretry"), QStringLiteral("0")));

    QNetworkRequest req(makeUrl("https://wpa.chat.zalo.me/api/login/getLoginInfo", query));
    applyHeaders(req);
    QNetworkReply* reply = m_nam->get(req);
    const QByteArray encryptKey = encryptor.encryptKey().toUtf8();
    connect(reply, &QNetworkReply::finished, this, [this, reply, encryptKey]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429 || status >= 500 || (reply->error() != QNetworkReply::NoError && status == 0)) {
            emit loginUnavailable(reply->errorString());
            return;
        }
        if (reply->error() != QNetworkReply::NoError || !reply->isOpen()) {
            emit loginFailed(reply->errorString());
            return;
        }
        const QJsonObject outer = QJsonDocument::fromJson(reply->readAll()).object();
        if (outer.isEmpty()) {
            emit loginUnavailable(QStringLiteral("empty_response"));
            return;
        }
        bool ok = false;
        const QByteArray plain = ZaloCrypto::decodeRespAES(encryptKey, outer.value("data").toString(), &ok);
        const QJsonObject inner = QJsonDocument::fromJson(plain).object();
        const QJsonObject info = inner.value("data").toObject();
        if (!ok || inner.value("error_code").toInt(-1) != 0 || info.value("zpw_enk").toString().isEmpty()) {
            const QString msg = inner.value("error_message").toString(outer.value("error_message").toString());
            emit loginFailed(msg.isEmpty() ? QStringLiteral("session_invalid") : msg);
            return;
        }
        m_loginInfo = info;
        m_secretKey = info.value("zpw_enk").toString();
        m_uid = info.value("uid").toVariant().toString();
        requestServerInfo();
    });
}

void ZaloSession::requestServerInfo() {
    QMap<QString, QString> signParams{
        {"imei", m_imei},
        {"type", QString::number(kApiType)},
        {"client_version", QString::number(kApiVersion)},
        {"computer_name", "Web"},
    };
    QList<QPair<QString, QString>> query{
        {"imei", m_imei},
        {"type", QString::number(kApiType)},
        {"client_version", QString::number(kApiVersion)},
        {"computer_name", "Web"},
        {"signkey", ZaloCrypto::signKey("getserverinfo", signParams)},
    };
    QNetworkRequest req(makeUrl("https://wpa.chat.zalo.me/api/login/getServerInfo", query, false));
    applyHeaders(req);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError && reply->isOpen()) {
            const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object().value("data").toObject();
            m_serverSettings = data.value("setttings").toObject();
            if (m_serverSettings.isEmpty()) m_serverSettings = data.value("settings").toObject();
            m_extraVer = data.value("extra_ver").toObject();
        }
        emit loggedIn();
    });
}

QStringList ZaloSession::serviceUrls(const QString& service) const {
    const QJsonObject map = m_loginInfo.value("zpw_service_map_v3").toObject();
    QJsonArray urls = map.value(service).toArray();
    if (urls.isEmpty() && service == "friend_board") urls = map.value("friendboard").toArray();
    if (urls.isEmpty() && (service == "alias" || service == "friendboard" || service == "friend_board")) {
        urls = map.value("friend").toArray();
        if (urls.isEmpty()) urls = map.value("profile").toArray();
    }
    if (urls.isEmpty() && service == "boards") {
        urls = map.value("board").toArray();
        if (urls.isEmpty()) urls = QJsonArray{QStringLiteral("https://board-wpa.api.zalo.me")};
    }
    QStringList out;
    for (const QJsonValue& v : urls) {
        const QString url = v.toString();
        if (!url.isEmpty() && !out.contains(url)) out << url;
    }
    return out;
}

QString ZaloSession::serviceUrl(const QString& service) const {
    const QStringList urls = serviceUrls(service);
    return urls.isEmpty() ? QString() : urls.at(qBound(0, m_preferredHost.value(service), int(urls.size()) - 1));
}

bool ZaloSession::isFailoverError(int networkError, int httpStatus) {
    if (httpStatus == 404 || httpStatus == 502 || httpStatus == 503 || httpStatus == 504) return true;
    switch (networkError) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::SslHandshakeFailedError:
        return true;
    default:
        return false;
    }
}

QUrl ZaloSession::makeUrl(const QString& base, const QList<QPair<QString, QString>>& query, bool withVersion) const {
    QList<QPair<QString, QString>> items = query;
    if (withVersion) {
        items.append(qMakePair(QStringLiteral("zpw_ver"), QString::number(kApiVersion)));
        items.append(qMakePair(QStringLiteral("zpw_type"), QString::number(kApiType)));
    }
    QByteArray encoded = base.toUtf8();
    if (!items.isEmpty()) {
        encoded.append(base.contains('?') ? '&' : '?');
        encoded.append(encodeQuery(items));
    }
    return QUrl::fromEncoded(encoded, QUrl::StrictMode);
}

void ZaloSession::applyHeaders(QNetworkRequest& req) const {
    req.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent.isEmpty() ? kDefaultUserAgent : m_userAgent);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setRawHeader("Accept", "application/json, text/plain, */*");
    req.setRawHeader("Accept-Language", "vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7");
    req.setRawHeader("Origin", "https://chat.zalo.me");
    req.setRawHeader("Referer", "https://chat.zalo.me/");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
}

void ZaloSession::apiGet(const QString& service, const QString& path, const QJsonObject& params, Callback cb,
                         bool alwaysSendParams) {
    sendApi(service, path, false, params, cb,
            alwaysSendParams ? QHash<QString, QString>{{"__params_in_query", "1"}} : QHash<QString, QString>{}, -1);
}

void ZaloSession::apiPost(const QString& service, const QString& path, const QJsonObject& params, Callback cb,
                          const QHash<QString, QString>& extraQuery) {
    sendApi(service, path, true, params, cb, extraQuery, -1);
}

void ZaloSession::sendApi(const QString& service, const QString& path, bool post, const QJsonObject& params,
                          Callback cb, const QHash<QString, QString>& extraQuery, int hostIndex) {
    const QStringList hosts = serviceUrls(service);
    if (m_secretKey.isEmpty()) {
        cb(false, {}, QStringLiteral("not_logged_in"), -1);
        return;
    }
    if (hosts.isEmpty()) {
        logRealtime(QStringLiteral("no host for service \"%1\" (%2)").arg(service, path));
        cb(false, {}, QStringLiteral("service_unavailable"), -1);
        return;
    }
    const int first = qBound(0, m_preferredHost.value(service), int(hosts.size()) - 1);
    const int attempt = hostIndex < 0 ? 0 : hostIndex;
    const int index = (first + attempt) % hosts.size();
    const QString base = hosts.at(index);

    const bool rawParams = extraQuery.contains("__raw_params");
    const QString encrypted = ZaloCrypto::encodeAES(
        m_secretKey, rawParams ? extraQuery.value("__raw_params").toUtf8() : compactJson(params).toUtf8());
    const bool paramsInQuery = !post || extraQuery.value("__params_in_query") == "1";
    QList<QPair<QString, QString>> query;
    for (auto it = extraQuery.cbegin(); it != extraQuery.cend(); ++it) {
        if (!it.key().startsWith("__")) query.append(qMakePair(it.key(), it.value()));
    }
    if (paramsInQuery && (post || rawParams || !params.isEmpty() || extraQuery.contains("__params_in_query"))) query.append(qMakePair(QStringLiteral("params"), encrypted));

    QNetworkRequest req(makeUrl(base + path, query));
    applyHeaders(req);
    QNetworkReply* reply = post ? m_nam->post(req, paramsInQuery ? QByteArray() : encodeQuery({{"params", encrypted}}))
                                : m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, cb, service, path, post, params, extraQuery, attempt, index, hosts]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if ((reply->error() != QNetworkReply::NoError || status == 404)
            && isFailoverError(reply->error(), status) && attempt + 1 < hosts.size()) {
            reply->deleteLater();
            sendApi(service, path, post, params, cb, extraQuery, attempt + 1);
            return;
        }
        if (reply->error() == QNetworkReply::NoError) m_preferredHost[service] = index;
        handleApiReply(reply, cb);
    });
}

void ZaloSession::handleApiReply(QNetworkReply* reply, Callback cb) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError || !reply->isOpen()) {
        cb(false, {}, reply->errorString(), -1);
        return;
    }
    const QJsonObject outer = QJsonDocument::fromJson(reply->readAll()).object();
    const int outerCode = outer.value("error_code").toInt(-1);
    if (outerCode != 0) {
        cb(false, parseMaybeJsonString(outer.value("data")), outer.value("error_message").toString(), outerCode);
        return;
    }
    const QJsonValue rawData = outer.value("data");
    if (!rawData.isString()) {
        cb(true, rawData, QString(), 0);
        return;
    }
    bool ok = false;
    const QByteArray plain = ZaloCrypto::decodeAES(m_secretKey, rawData.toString(), &ok);
    const QJsonObject inner = QJsonDocument::fromJson(plain).object();
    if (!ok || inner.isEmpty()) {
        cb(false, {}, QStringLiteral("decrypt_failed"), -1);
        return;
    }
    const int innerCode = inner.value("error_code").toInt(-1);
    if (innerCode != 0) {
        cb(false, inner.value("data"), inner.value("error_message").toString(), innerCode);
        return;
    }
    cb(true, parseMaybeJsonString(inner.value("data")), QString(), 0);
}

void ZaloSession::fetchAccountInfo(Callback cb) {
    apiGet("profile", "/api/social/profile/me-v2", {}, cb);
}

void ZaloSession::fetchFriends(Callback cb) {
    apiGet("profile", "/api/social/friend/getfriends",
           {{"incInvalid", 1}, {"page", 1}, {"count", 20000}, {"avatar_size", 120}, {"actiontime", 0}, {"imei", m_imei}},
           cb);
}

void ZaloSession::fetchGroupIds(Callback cb) {
    apiGet("group_poll", "/api/group/getlg/v4", {}, cb);
}

void ZaloSession::fetchGroupInfo(const QStringList& groupIds, Callback cb) {
    QJsonObject verMap;
    for (const QString& id : groupIds) verMap.insert(id, 0);
    apiPost("group", "/api/group/getmg-v2", {{"gridVerMap", compactJson(verMap)}}, cb);
}

void ZaloSession::fetchPinnedConversations(Callback cb) {
    apiGet("conversation", "/api/pinconvers/list", {{"imei", m_imei}}, cb);
}

void ZaloSession::fetchUserInfo(const QStringList& userIds, Callback cb) {
    QJsonArray ids;
    for (const QString& id : userIds) ids.append(id.contains('_') ? id : id + "_0");
    apiPost("profile", "/api/social/friend/getprofiles/v2",
            {{"phonebook_version", m_extraVer.value("phonebook").toVariant().toLongLong()},
             {"friend_pversion_map", ids},
             {"avatar_size", 120},
             {"language", m_language},
             {"show_online_status", 1},
             {"imei", m_imei}},
            cb);
}

void ZaloSession::fetchGroupHistory(const QString& groupId, int count, Callback cb) {
    apiGet("group", "/api/group/history", {{"grid", groupId}, {"count", count}}, cb);
}

void ZaloSession::fetchLastMessages(const QStringList& threadIds, const QSet<QString>& groupIds, Callback cb) {
    QJsonObject map;
    if (threadIds.size() <= 25) {
        for (const QString& id : threadIds) map.insert(id + (groupIds.contains(id) ? "_1" : "_0"), QStringLiteral("1"));
    }
    apiGet("conversation", "/api/preloadconvers/get-last-msgs",
           {{"threadIdLocalMsgId", compactJson(map)}, {"imei", m_imei}}, cb);
}

void ZaloSession::fetchCloudHistory(const QString& threadId, bool isMyCloud, const QString& beforeMsgId, int count,
                                    Callback cb) {
    QJsonObject params{
        {"groupId", threadId},
        {"globalMsgId", beforeMsgId.isEmpty() || beforeMsgId == "0" ? QStringLiteral("9223372036854775808") : beforeMsgId},
        {"count", count},
        {"msgIds", QJsonArray()},
        {"imei", m_imei},
        {"src", 3},
    };
    if (isMyCloud) params["isOA"] = 1;
    apiGet("group_cloud_message", "/api/cm/getoldv2", params, cb);
}

void ZaloSession::sendQuote(const QString& threadId, bool isGroup, const QString& text, const QJsonObject& quoted,
                            Callback cb, const QString& cliMsgId, const QJsonArray& styles,
                            const QJsonArray& mentions, qint64 ttl) {
    const QJsonValue content = quoted.value("content");
    QJsonObject params{
        {"message", text},
        {"clientId", cliMsgId.isEmpty() ? QDateTime::currentMSecsSinceEpoch() : cliMsgId.toLongLong()},
        {"qmsgOwner", quoted.value("uidFrom")},
        {"qmsgId", quoted.value("msgId")},
        {"qmsgCliId", quoted.value("cliMsgId")},
        {"qmsgType", clientMessageType(quoted.value("msgType").toString())},
        {"qmsgTs", quoted.value("ts")},
        {"qmsg", content.isString() ? content.toString() : QString()},
        {"qmsgTTL", quoted.value("ttl").toInt(0)},
        {"ttl", ttl},
    };
    if (isGroup) {
        QJsonObject attach;
        if (content.isObject()) {
            attach = content.toObject();
            attach["thumbUrl"] = attach.value("thumb");
            attach["oriUrl"] = attach.value("href");
            attach["normalUrl"] = attach.value("href");
        } else if (quoted.value("propertyExt").isObject()) {
            attach = quoted.value("propertyExt").toObject();
        }
        params["grid"] = threadId;
        params["visibility"] = 0;
        params["qmsgAttach"] = compactJson(attach);
        if (!mentions.isEmpty()) {
            params["mentionInfo"] = QString::fromUtf8(QJsonDocument(mentions).toJson(QJsonDocument::Compact));
        }
        if (const QString props = textPropertiesJson(styles); !props.isEmpty()) params["textProperties"] = props;
        apiPost("group", "/api/group/quote", params, cb);
    } else {
        params["toid"] = threadId;
        params["imei"] = m_imei;
        if (const QString props = textPropertiesJson(styles); !props.isEmpty()) params["textProperties"] = props;
        apiPost("chat", "/api/message/quote", params, cb);
    }
}

void ZaloSession::setUnreadMark(const QString& threadId, bool isGroup, bool unread, Callback cb) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString mine = isGroup ? "convsGroup" : "convsUser";
    const QString other = isGroup ? "convsUser" : "convsGroup";
    QJsonObject inner;
    if (unread) {
        inner[mine] = QJsonArray{QJsonObject{{"id", threadId}, {"cliMsgId", QString::number(now)}, {"fromUid", "0"},
                                             {"ts", now}}};
        inner[other] = QJsonArray();
        inner["imei"] = m_imei;
    } else {
        inner[mine] = QJsonArray{threadId};
        inner[other] = QJsonArray();
        inner[mine + "Data"] = QJsonArray{QJsonObject{{"id", threadId}, {"ts", now}}};
        inner[other + "Data"] = QJsonArray();
    }
    apiPost("conversation", unread ? "/api/conv/addUnreadMark" : "/api/conv/removeUnreadMark",
            {{"param", compactJson(inner)}}, cb);
}

void ZaloSession::deleteConversation(const QString& threadId, bool isGroup, const QJsonObject& lastMsg, Callback cb) {
    QJsonObject params{
        {"cliMsgId", QString::number(QDateTime::currentMSecsSinceEpoch())},
        {"conver", QJsonObject{{"ownerId", lastMsg.value("uidFrom")},
                               {"cliMsgId", lastMsg.value("cliMsgId")},
                               {"globalMsgId", lastMsg.value("msgId")}}},
        {"onlyMe", 1},
        {"imei", m_imei},
    };
    params[isGroup ? "grid" : "toid"] = threadId;
    apiPost(isGroup ? "group" : "chat", isGroup ? "/api/group/deleteconver" : "/api/message/deleteconver", params, cb,
            {{"nretry", "0"}});
}

void ZaloSession::setMute(const QString& threadId, bool isGroup, qint64 seconds, Callback cb) {
    apiPost("profile", "/api/social/profile/setmute",
            {{"toid", threadId}, {"duration", seconds == 0 ? -1 : seconds}, {"action", seconds == 0 ? 3 : 1},
             {"startTime", QDateTime::currentSecsSinceEpoch()}, {"muteType", isGroup ? 2 : 1}, {"imei", m_imei}},
            cb);
}

QString ZaloSession::textPropertiesJson(const QJsonArray& styles) {
    QJsonArray valid;
    for (const QJsonValue& v : styles) {
        const QJsonObject o = v.toObject();
        const int start = o.value("start").toInt(-1);
        const int len = o.value("len").toInt();
        const QString st = o.value("st").toString();
        if (start < 0 || len <= 0 || st.isEmpty()) continue;
        valid.append(QJsonObject{{"start", start}, {"len", len}, {"st", st}});
    }
    if (valid.isEmpty()) return {};
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"styles", valid}, {"ver", 0}}).toJson(QJsonDocument::Compact));
}

void ZaloSession::sendText(const QString& threadId, bool isGroup, const QString& text, Callback cb,
                           const QString& cliMsgId, const QJsonArray& styles, qint64 ttl) {
    const qint64 clientId = cliMsgId.isEmpty() ? QDateTime::currentMSecsSinceEpoch() : cliMsgId.toLongLong();
    QJsonObject params{{"message", text}, {"clientId", clientId}, {"ttl", ttl}};
    const QString props = textPropertiesJson(styles);
    if (!props.isEmpty()) params.insert("textProperties", props);
    if (isGroup) {
        params.insert("visibility", 0);
        params.insert("grid", threadId);
        apiPost("group", "/api/group/sendmsg", params, cb, {{"nretry", "0"}});
    } else {
        params.insert("imei", m_imei);
        params.insert("toid", threadId);
        apiPost("chat", "/api/message/sms", params, cb, {{"nretry", "0"}});
    }
}

void ZaloSession::findUserByPhone(const QString& phone, Callback cb) {
    QString normalized = phone;
    normalized.remove(' ').remove('-').remove('.');
    if (normalized.startsWith('+')) normalized.remove(0, 1);
    if (normalized.startsWith('0') && m_language == "vi") normalized = "84" + normalized.mid(1);
    apiGet("friend", "/api/friend/profile/get",
           {{"phone", normalized}, {"avatar_size", 240}, {"language", m_language}, {"imei", m_imei}, {"reqSrc", 40}},
           cb);
}

void ZaloSession::sendFriendRequest(const QString& userId, const QString& message, Callback cb) {
    apiPost("friend", "/api/friend/sendreq",
            {{"toid", userId},
             {"msg", message},
             {"reqsrc", 30},
             {"imei", m_imei},
             {"language", m_language},
             {"srcParams", compactJson({{"uidTo", userId}})}},
            cb);
}

void ZaloSession::createGroup(const QString& name, const QStringList& members, Callback cb) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QJsonArray memberArr;
    QJsonArray memberTypes;
    for (const QString& m : members) {
        memberArr.append(m);
        memberTypes.append(-1);
    }
    QJsonObject params{
        {"clientId", now},
        {"gname", name.isEmpty() ? QString::number(now) : name},
        {"gdesc", QJsonValue::Null},
        {"members", memberArr},
        {"membersTypes", memberTypes},
        {"nameChanged", name.isEmpty() ? 0 : 1},
        {"createLink", 1},
        {"clientLang", m_language},
        {"imei", m_imei},
        {"zsource", 601},
    };
    apiPost("group", "/api/group/create/v2", params, cb, {{"__params_in_query", "1"}});
}

void ZaloSession::addReaction(const QString& threadId, bool isGroup, const QJsonObject& msg, const QString& rIcon,
                              int rType, Callback cb) {
    const QString mType = msg.value("msgType").toString();
    const int reactMsgType = (mType == "chat.sticker" || msg.contains("stickerId")) ? 5 : 1;
    const QJsonObject message{
        {"rMsg", QJsonArray{QJsonObject{{"gMsgID", msg.value("msgId").toVariant().toLongLong()},
                                        {"cMsgID", msg.value("cliMsgId").toVariant().toLongLong()},
                                        {"msgType", reactMsgType}}}},
        {"rIcon", rIcon},
        {"rType", rType},
        {"source", 6},
    };
    QJsonObject params{
        {"react_list", QJsonArray{QJsonObject{{"message", compactJson(message)},
                                              {"clientId", QDateTime::currentMSecsSinceEpoch()}}}},
    };
    if (isGroup) {
        params["grid"] = threadId;
        params["imei"] = m_imei;
    } else {
        params["toid"] = threadId;
    }
    apiPost("reaction", isGroup ? "/api/group/reaction" : "/api/message/reaction", params, cb);
}

void ZaloSession::undoMessage(const QString& threadId, bool isGroup, const QJsonObject& msg, Callback cb) {
    QJsonObject params{
        {"msgId", msg.value("msgId")},
        {"clientId", QDateTime::currentMSecsSinceEpoch()},
        {"cliMsgIdUndo", msg.value("cliMsgId")},
    };
    if (isGroup) {
        params["grid"] = threadId;
        params["visibility"] = 0;
        params["imei"] = m_imei;
    } else {
        params["toid"] = threadId;
    }
    if (isGroup) apiPost("group", "/api/group/undomsg", params, cb);
    else apiPost("chat", "/api/message/undo", params, cb);
}

void ZaloSession::deleteMessage(const QString& threadId, bool isGroup, const QJsonObject& msg, bool onlyMe,
                                Callback cb) {
    QString owner = msg.value("uidFrom").toVariant().toString();
    if (owner == "0") owner = m_uid;
    QJsonObject params{
        {isGroup ? "grid" : "toid", threadId},
        {"cliMsgId", QDateTime::currentMSecsSinceEpoch()},
        {"msgs", QJsonArray{QJsonObject{{"cliMsgId", msg.value("cliMsgId").toVariant().toString()},
                                        {"globalMsgId", msg.value("msgId").toVariant().toString()},
                                        {"ownerId", owner},
                                        {"destId", threadId}}}},
        {"onlyMe", onlyMe ? 1 : 0},
    };
    if (!isGroup) params["imei"] = m_imei;
    if (isGroup) apiPost("group", "/api/group/deletemsg", params, cb);
    else apiPost("chat", "/api/message/delete", params, cb);
}

void ZaloSession::sendSeen(const QString& threadId, bool isGroup, const QJsonArray& msgs, Callback cb) {
    if (msgs.isEmpty()) return;
    auto flag = [](const QJsonValue& v) { return v.isUndefined() || v.isNull() ? -1 : 0; };
    QJsonArray data;
    for (const QJsonValue& v : msgs) {
        const QJsonObject m = v.toObject();
        const QString idTo = m.value("idTo").toVariant().toString();
        data.append(QJsonObject{
            {"cmi", m.value("cliMsgId").toVariant().toString()},
            {"gmi", m.value("msgId").toVariant().toString()},
            {"si", m.value("uidFrom").toVariant().toString()},
            {"di", idTo == m_uid ? QStringLiteral("0") : idTo},
            {"mt", m.value("msgType").toString()},
            {"st", flag(m.value("st"))},
            {"at", flag(m.value("at"))},
            {"cmd", flag(m.value("cmd"))},
            {"ts", m.value("ts").toVariant().toLongLong() || m.value("ts").toVariant().toString() == "0" ? 0 : -1},
        });
    }
    QJsonObject infos{{"data", data}, {isGroup ? "grid" : "senderId", threadId}};
    QJsonObject params{{"msgInfos", compactJson(infos)}};
    if (isGroup) params["imei"] = m_imei;
    if (isGroup) apiPost("group", "/api/group/seenv2", params, cb, {{"nretry", "0"}});
    else apiPost("chat", "/api/message/seenv2", params, cb, {{"nretry", "0"}});
}

void ZaloSession::sendTyping(const QString& threadId, bool isGroup, Callback cb) {
    QJsonObject params{{isGroup ? "grid" : "toid", threadId}, {"imei", m_imei}};
    if (!isGroup) params["destType"] = 3;
    if (isGroup) apiPost("group", "/api/group/typing", params, cb);
    else apiPost("chat", "/api/message/typing", params, cb);
}

int ZaloSession::sharefileSetting(const char* key, int fallback) const {
    const QJsonValue v = m_serverSettings.value("features").toObject().value("sharefile").toObject().value(key);
    return v.isDouble() ? v.toInt() : fallback;
}

void ZaloSession::sendAttachment(const QString& threadId, bool isGroup, const QString& filePath,
                                 const QString& caption, Callback cb, const QString& sendClientId, qint64 ttl) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        cb(false, {}, QStringLiteral("file_unreadable"), -1);
        return;
    }
    const QFileInfo info(filePath);
    const QString ext = info.suffix().toLower();
    const qint64 maxBytes = qint64(sharefileSetting("max_size_share_file_v3", 1024)) * 1024 * 1024;
    if (info.size() <= 0 || info.size() > maxBytes) {
        cb(false, {}, QStringLiteral("file_too_large"), -2);
        return;
    }
    const QJsonArray restricted = m_serverSettings.value("features").toObject().value("sharefile").toObject()
                                      .value("restricted_ext_file").toArray();
    if (restricted.contains(QJsonValue(ext))) {
        cb(false, {}, QStringLiteral("file_type_blocked"), -3);
        return;
    }

    const bool isImage = (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp");
    const bool isVideo = (ext == "mp4" || ext == "mov" || ext == "webm" || ext == "mkv" || ext == "avi");
    QSize size;
    if (isImage) size = QImageReader(filePath).size();
    const UploadKind kind = isImage ? UploadKind::Photo : (isVideo ? UploadKind::Video : UploadKind::File);
    uploadChunks(threadId, isGroup, file.readAll(), info.fileName(), kind,
                 size.width(), size.height(), caption, cb, sendClientId, ttl);
}

void ZaloSession::sendVoice(const QString& threadId, bool isGroup, const QString& filePath, const QString& clientId,
                            Callback cb, qint64 ttl) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0) {
        cb(false, {}, QStringLiteral("file_unreadable"), -1);
        return;
    }
    uploadChunks(threadId, isGroup, file.readAll(), QFileInfo(filePath).fileName(), UploadKind::Voice, 0, 0, {}, cb,
                 clientId, ttl);
}

void ZaloSession::uploadChunks(const QString& threadId, bool isGroup, const QByteArray& content,
                               const QString& fileName, UploadKind kind, int width, int height, const QString& caption,
                               Callback cb, const QString& sendClientId, qint64 ttl) {
    const bool isImage = kind == UploadKind::Photo;
    const int chunkSize = qMax(65536, sharefileSetting("chunk_size_file", 3145728));
    const int totalChunk = int((content.size() + chunkSize - 1) / chunkSize);
    const qint64 clientId = sendClientId.isEmpty() ? QDateTime::currentMSecsSinceEpoch() : sendClientId.toLongLong();
    const QString base = serviceUrl("file");
    if (base.isEmpty()) {
        cb(false, {}, QStringLiteral("not_logged_in"), -1);
        return;
    }
    const QString uploadUrl = base + "/api/" + (isGroup ? "group" : "message") + "/"
                              + (isImage ? "photo_original/upload"
                                         : kind == UploadKind::Voice ? "voice/upload" : "asyncfile/upload");

    QJsonObject baseParams{
        {"totalChunk", totalChunk},
        {"fileName", fileName},
        {"clientId", clientId},
        {"totalSize", content.size()},
        {"imei", m_imei},
        {"isE2EE", 0},
        {"jxl", 0},
    };
    baseParams[isGroup ? "grid" : "toid"] = threadId;
    if (kind == UploadKind::Voice) baseParams["fileType"] = fileName.endsWith(".amr") ? "amr" : "aac";

    auto uploadOne = std::make_shared<std::function<void(int)>>();
    const std::weak_ptr<std::function<void(int)>> weakUpload = uploadOne;
    *uploadOne = [=, this](int index) {
        const auto keep = weakUpload.lock();
        if (!keep) return;
        QJsonObject params = baseParams;
        params["chunkId"] = index + 1;
        const QString encrypted = ZaloCrypto::encodeAES(m_secretKey, compactJson(params).toUtf8());
        QNetworkRequest req(makeUrl(uploadUrl, {qMakePair(QStringLiteral("type"), QString(isGroup ? "11" : "2")),
                                                qMakePair(QStringLiteral("params"), encrypted)}));
        applyHeaders(req);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant());

        auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"chunkContent\"; filename=\"%1\"").arg(fileName));
        part.setBody(content.mid(qint64(index) * chunkSize, chunkSize));
        multi->append(part);

        QNetworkReply* reply = m_nam->post(req, multi);
        multi->setParent(reply);
        connect(reply, &QNetworkReply::finished, this, [=, this]() {
            handleApiReply(reply, [=, this](bool ok, const QJsonValue& data, const QString& error, int code) {
                if (!ok) {
                    cb(false, {}, error, code);
                    return;
                }
                if (index + 1 < totalChunk) {
                    (*keep)(index + 1);
                    return;
                }
                const QJsonObject res = data.toObject();
                if (isImage) {
                    QJsonObject send{
                        {"photoId", res.value("photoId")},
                        {"clientId", QString::number(clientId)},
                        {"desc", caption},
                        {"width", width},
                        {"height", height},
                        {"rawUrl", res.value("normalUrl")},
                        {"hdUrl", res.value("hdUrl")},
                        {"thumbUrl", res.value("thumbUrl")},
                        {"hdSize", QString::number(content.size())},
                        {"zsource", -1},
                        {"ttl", ttl},
                        {"jcp", "{\"convertible\":\"jxl\"}"},
                    };
                    if (isGroup) {
                        send["grid"] = threadId;
                        send["oriUrl"] = res.value("normalUrl");
                    } else {
                        send["toid"] = threadId;
                        send["normalUrl"] = res.value("normalUrl");
                    }
                    apiPost("file", QString("/api/%1/photo_original/send?").arg(isGroup ? "group" : "message"),
                            send, cb, {{"nretry", "0"}});
                    return;
                }
                const QString fileId = res.value("fileId").toVariant().toString();
                if (fileId.isEmpty() || fileId == "-1") {
                    cb(false, {}, QStringLiteral("upload_rejected"), -1);
                    return;
                }
                const QString checksum = QString::fromLatin1(
                    QCryptographicHash::hash(content, QCryptographicHash::Md5).toHex());
                auto awaitFileDone = [this, cb](const QString& fid, std::function<void(const QJsonObject&)> onDone) {
                    const auto early = m_earlyFileDone.constFind(fid);
                    if (early != m_earlyFileDone.constEnd()) {
                        const QJsonObject data = early.value();
                        m_earlyFileDone.erase(early);
                        onDone(data);
                        return;
                    }
                    m_uploadWaiters.insert(fid, std::move(onDone));
                    QTimer::singleShot(120000, this, [this, fid, cb]() {
                        if (m_uploadWaiters.remove(fid) > 0) cb(false, {}, QStringLiteral("upload_timeout"), -1);
                    });
                };
                if (kind == UploadKind::Voice) {
                    awaitFileDone(fileId, [=, this](const QJsonObject& fileData) {
                        const QString aac = fileData.value("aac").toString();
                        const QString amr = fileData.value("amr").toString();
                        const QString voiceUrl = !amr.isEmpty() ? amr : !aac.isEmpty() ? aac
                                                                     : fileData.value("url").toString();
                        if (voiceUrl.isEmpty()) {
                            cb(false, {}, QStringLiteral("upload_rejected"), -1);
                            return;
                        }
                        const QJsonObject info{{"voiceUrl", voiceUrl},
                                               {"m4aUrl", !aac.isEmpty() ? aac : voiceUrl},
                                               {"fileSize", content.size()}};
                        QJsonObject send{
                            {"ttl", 0},
                            {"zsource", -1},
                            {"msgType", 3},
                            {"clientId", sendClientId.isEmpty() ? QString::number(QDateTime::currentMSecsSinceEpoch())
                                                                : sendClientId},
                            {"msgInfo", compactJson(info)},
                            {"imei", m_imei},
                        };
                        if (isGroup) {
                            send["grid"] = threadId;
                            send["visibility"] = 0;
                        } else {
                            send["toId"] = threadId;
                        }
                        apiPost("file", QString("/api/%1/forward?").arg(isGroup ? "group" : "message"),
                                send, cb, {{"nretry", "0"}});
                    });
                    return;
                }
                awaitFileDone(fileId, [=, this](const QJsonObject& fileData) {
                    const QString fileUrl = fileData.value("url").toString();
                    QJsonObject send{
                        {"fileId", fileId},
                        {"checksum", checksum},
                        {"checksumSha", ""},
                        {"extention", QFileInfo(fileName).suffix()},
                        {"totalSize", content.size()},
                        {"fileName", fileName},
                        {"clientId", res.value("clientFileId")},
                        {"fType", kind == UploadKind::Video ? 2 : 1},
                        {"fileCount", 0},
                        {"fdata", "{}"},
                        {"fileUrl", fileUrl},
                        {"zsource", -1},
                        {"ttl", ttl},
                    };
                    send[isGroup ? "grid" : "toid"] = threadId;
                    apiPost("file", QString("/api/%1/asyncfile/msg?").arg(isGroup ? "group" : "message"),
                            send, cb, {{"nretry", "0"}});
                });
            });
        });
    };
    (*uploadOne)(0);
}

namespace {

bool avatarJpeg(const QString& path, QByteArray* jpeg, QSize* size) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) return false;
    if (qMax(image.width(), image.height()) > 1080) image = image.scaled(1080, 1080, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QBuffer buffer(jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!image.convertToFormat(QImage::Format_RGB888).save(&buffer, "JPEG", 90)) return false;
    *size = image.size();
    return true;
}

}

void ZaloSession::uploadProfileAvatar(const QString& imagePath, Callback cb) {
    QByteArray jpeg;
    QSize size;
    if (!avatarJpeg(imagePath, &jpeg, &size)) {
        cb(false, {}, QStringLiteral("file_unreadable"), -1);
        return;
    }
    const QString base = serviceUrl("file");
    if (base.isEmpty() || m_secretKey.isEmpty()) {
        cb(false, {}, QStringLiteral("not_logged_in"), -1);
        return;
    }
    const QJsonObject dims{{"width", size.width()}, {"height", size.height()}};
    QJsonObject processed = dims;
    processed["size"] = jpeg.size();
    const QJsonObject params{
        {"avatarSize", 120},
        {"clientId", QString::number(QDateTime::currentMSecsSinceEpoch())},
        {"language", "vi"},
        {"metaData", compactJson(QJsonObject{{"origin", dims}, {"processed", processed}})},
    };
    postMultipartFile(base + "/api/profile/upavatar", params, jpeg, cb);
}

void ZaloSession::uploadGroupAvatar(const QString& groupId, const QString& imagePath, Callback cb) {
    QByteArray jpeg;
    QSize size;
    if (!avatarJpeg(imagePath, &jpeg, &size)) {
        cb(false, {}, QStringLiteral("file_unreadable"), -1);
        return;
    }
    const QString base = serviceUrl("file");
    if (base.isEmpty() || m_secretKey.isEmpty()) {
        cb(false, {}, QStringLiteral("not_logged_in"), -1);
        return;
    }
    const QJsonObject params{
        {"grid", groupId},
        {"avatarSize", 120},
        {"clientId", QString::number(QDateTime::currentMSecsSinceEpoch())},
        {"originWidth", size.width()},
        {"originHeight", size.height()},
        {"imei", m_imei},
    };
    postMultipartFile(base + "/api/group/upavatar", params, jpeg, cb);
}

void ZaloSession::postMultipartFile(const QString& url, const QJsonObject& params, const QByteArray& content,
                                   Callback cb) {
    const QString encrypted = ZaloCrypto::encodeAES(m_secretKey, compactJson(params).toUtf8());
    QNetworkRequest req(makeUrl(url, {qMakePair(QStringLiteral("params"), encrypted)}));
    applyHeaders(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant());
    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentTypeHeader, "image/jpeg");
    part.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"fileContent\"; filename=\"blob\""));
    part.setBody(content);
    multi->append(part);
    QNetworkReply* reply = m_nam->post(req, multi);
    multi->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() { handleApiReply(reply, cb); });
}

void ZaloSession::sendSticker(const QString& threadId, bool isGroup, qint64 stickerId, qint64 cateId, int type,
                              Callback cb, const QString& cliMsgId, qint64 ttl) {
    const qint64 clientId = cliMsgId.isEmpty() ? QDateTime::currentMSecsSinceEpoch() : cliMsgId.toLongLong();
    QJsonObject params{
        {"stickerId", stickerId},
        {"cateId", cateId},
        {"type", type},
        {"clientId", clientId},
        {"imei", m_imei},
        {"zsource", 101},
        {"ttl", ttl},
    };
    params[isGroup ? "grid" : "toid"] = threadId;
    if (isGroup) apiPost("group", "/api/group/sticker", params, cb, {{"nretry", "0"}});
    else apiPost("chat", "/api/message/sticker", params, cb, {{"nretry", "0"}});
}

void ZaloSession::searchStickers(const QString& keyword, Callback cb) {
    apiGet("sticker", "/api/message/sticker/suggest/stickers",
           {{"keyword", keyword}, {"gif", 1}, {"guggy", 0}, {"imei", m_imei}}, cb);
}

void ZaloSession::fetchStickerDetail(qint64 stickerId, Callback cb) {
    apiGet("sticker", "/api/message/sticker/sticker_detail", {{"sid", stickerId}}, cb);
}

void ZaloSession::fetchStickerCategories(Callback cb) {
    sendApi("sticker", "/api/message/sticker/category/list/v2", false, {}, cb, {{"__raw_params", QString()}}, -1);
}

void ZaloSession::fetchPersonalStickerList(Callback cb) {
    apiGet("sticker", "/api/message/sticker/personalized/list", {{"imei", m_imei}}, cb);
}

void ZaloSession::fetchCategoryStickers(qint64 cateId, Callback cb) {
    sendApi("sticker", "/api/message/sticker/category/sticker_detail", false, {}, cb,
            {{"__raw_params", QStringLiteral("{\"cid\":\"%1\"}").arg(cateId)}}, -1);
}

void ZaloSession::searchStickerIds(const QString& keyword, int limit, Callback cb) {
    apiGet("sticker", "/api/message/sticker/search",
           {{"keyword", keyword}, {"limit", limit}, {"srcType", 0}, {"imei", m_imei}}, cb);
}

void ZaloSession::fetchFriendOnlines(Callback cb) {
    apiGet("profile", "/api/social/friend/onlines", {{"imei", m_imei}}, cb);
}

void ZaloSession::fetchLastOnline(const QString& userId, Callback cb) {
    apiGet("profile", "/api/social/profile/lastOnline", {{"uid", userId}, {"conv_type", 1}, {"imei", m_imei}}, cb);
}

void ZaloSession::fetchProfileRank(const QString& userId, Callback cb) {
    apiGet("profile", "/api/social/profile/getrank", {{"profile_id", userId}, {"imei", m_imei}}, cb);
}

void ZaloSession::fetchLabels(Callback cb) {
    apiGet("label", "/api/convlabel/get", {{"imei", m_imei}}, cb);
}

void ZaloSession::fetchFriendRecommendations(Callback cb) {
    apiGet("friend", "/api/friend/recommendsv2/list", {{"imei", m_imei}}, cb);
}

void ZaloSession::acceptFriendRequest(const QString& userId, Callback cb) {
    apiPost("friend", "/api/friend/accept", {{"fid", userId}, {"language", m_language}}, cb);
}

void ZaloSession::rejectFriendRequest(const QString& userId, Callback cb) {
    apiPost("friend", "/api/friend/reject", {{"fid", userId}}, cb);
}

void ZaloSession::fetchSentFriendRequests(Callback cb) {
    apiGet("friend", "/api/friend/requested/list", {{"imei", m_imei}}, cb, true);
}

void ZaloSession::undoFriendRequest(const QString& userId, Callback cb) {
    apiPost("friend", "/api/friend/undo", {{"fid", userId}}, cb);
}

void ZaloSession::findUserByUsername(const QString& username, Callback cb) {
    apiGet("friend", "/api/friend/search/by-user-name", {{"user_name", username}, {"avatar_size", 240}}, cb);
}

void ZaloSession::fetchAvatarList(Callback cb) {
    apiGet("profile", "/api/social/avatar-list",
           {{"page", 1}, {"albumId", "0"}, {"count", 50}, {"imei", m_imei}}, cb);
}

void ZaloSession::reuseAvatar(const QString& photoId, Callback cb) {
    apiGet("profile", "/api/social/reuse-avatar",
           {{"photoId", photoId}, {"isPostSocial", 0}, {"imei", m_imei}}, cb);
}

void ZaloSession::deleteAvatarPhotos(const QStringList& photoIds, Callback cb) {
    QJsonArray delPhotos;
    for (const QString& id : photoIds) delPhotos.append(QJsonObject{{"photoId", id}});
    const QString delJson = QString::fromUtf8(QJsonDocument(delPhotos).toJson(QJsonDocument::Compact));
    apiGet("profile", "/api/social/del-avatars", {{"delPhotos", delJson}, {"imei", m_imei}}, cb);
}

void ZaloSession::pullMobileMessages(const QString& pcName, const QString& publicKey, qint64 fromSeqId,
                                     qint64 minSeqId, bool isRetry, Callback cb) {
    apiGet("file", "/api/message/pull_mobile_msg",
           {{"pc_name", pcName}, {"public_key", publicKey}, {"from_seq_id", fromSeqId}, {"is_retry", isRetry ? 1 : 0},
            {"min_seq_id", minSeqId}, {"temp_key", ""}, {"imei", m_imei}},
           cb);
}

void ZaloSession::cancelPullMobileMessages(const QString& pcName, const QString& publicKey, Callback cb) {
    apiGet("file", "/api/message/cancel_pull_mobile_msg",
           {{"pc_name", pcName}, {"public_key", publicKey}, {"imei", m_imei}}, cb);
}

void ZaloSession::cleanMobileSync(const QString& publicKey, Callback cb) {
    apiGet("file", "/api/message/delete_snapshot_mobile_msg",
           {{"public_key", publicKey}, {"imei", m_imei}}, cb);
}

void ZaloSession::fetchAliasList(int page, int count, Callback cb) {
    apiGet("alias", "/api/alias/list", {{"page", page}, {"count", count}, {"imei", m_imei}}, cb);
}

void ZaloSession::fetchUnreadMarks(Callback cb) {
    apiGet("conversation", "/api/conv/getUnreadMark", {}, cb, true);
}

void ZaloSession::fetchNoisedIds(const QStringList& userIds, const QStringList& groupIds, Callback cb) {
    apiGet("profile", "/api/znoise",
           {{"fids", QJsonArray::fromStringList(userIds)}, {"gids", QJsonArray::fromStringList(groupIds)}}, cb);
}

QNetworkReply* ZaloSession::getWithCookies(const QUrl& url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent.isEmpty() ? kDefaultUserAgent : m_userAgent);
    QStringList parts;
    for (const QNetworkCookie& c : m_jar->cookiesForUrl(QUrl(QStringLiteral("https://chat.zalo.me/")))) {
        parts << QString::fromUtf8(c.name()) + "=" + QString::fromUtf8(c.value());
    }
    if (!parts.isEmpty()) req.setRawHeader("Cookie", parts.join("; ").toUtf8());
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return m_nam->get(req);
}

void ZaloSession::fetchGroupMembersInfo(const QStringList& memberIds, Callback cb) {
    QJsonArray ids;
    for (const QString& id : memberIds) ids.append(id.endsWith("_0") ? id : id + "_0");
    apiGet("profile", "/api/social/group/members", {{"friend_pversion_map", ids}}, cb);
}

void ZaloSession::fetchPollDetail(qint64 pollId, Callback cb) {
    apiPost("group", "/api/poll/detail", {{"poll_id", pollId}, {"imei", m_imei}}, cb);
}

void ZaloSession::parseLink(const QString& link, Callback cb) {
    apiGet("file", "/api/message/parselink", {{"link", link}, {"version", 1}, {"imei", m_imei}}, cb);
}

void ZaloSession::sendLink(const QString& threadId, bool isGroup, const QString& text, const QJsonObject& preview,
                           Callback cb, const QString& cliMsgId, qint64 ttl) {
    const qint64 clientId = cliMsgId.isEmpty() ? QDateTime::currentMSecsSinceEpoch() : cliMsgId.toLongLong();
    QJsonObject params{
        {"msg", text},
        {"href", preview.value("href")},
        {"src", preview.value("src")},
        {"title", preview.value("title")},
        {"desc", preview.value("desc")},
        {"thumb", preview.value("thumb")},
        {"type", 2},
        {"media", compactJson(preview.value("media").toObject())},
        {"ttl", ttl},
        {"clientId", clientId},
    };
    if (isGroup) {
        params["grid"] = threadId;
        params["imei"] = m_imei;
        apiPost("group", "/api/group/sendlink", params, cb, {{"nretry", "0"}});
    } else {
        params["toId"] = threadId;
        params["mentionInfo"] = "";
        apiPost("chat", "/api/message/link", params, cb, {{"nretry", "0"}});
    }
}

void ZaloSession::sendMention(const QString& groupId, const QString& text, const QJsonArray& mentions, Callback cb,
                              const QString& cliMsgId, const QJsonArray& styles, qint64 ttl) {
    QJsonArray valid;
    for (const QJsonValue& v : mentions) {
        const QJsonObject m = v.toObject();
        const QString uid = m.value("uid").toVariant().toString();
        const int pos = m.value("pos").toInt(-1);
        const int len = m.value("len").toInt();
        if (uid.isEmpty() || pos < 0 || len <= 0 || pos + len > text.size()) continue;
        valid.append(QJsonObject{{"pos", pos}, {"uid", uid}, {"len", len}, {"type", uid == "-1" ? 1 : 0}});
    }
    if (valid.isEmpty()) {
        sendText(groupId, true, text, cb, cliMsgId, styles, ttl);
        return;
    }
    const qint64 clientId = cliMsgId.isEmpty() ? QDateTime::currentMSecsSinceEpoch() : cliMsgId.toLongLong();
    QJsonObject params{{"message", text}, {"clientId", clientId},
                       {"mentionInfo", QString::fromUtf8(QJsonDocument(valid).toJson(QJsonDocument::Compact))},
                       {"visibility", 0}, {"ttl", ttl}, {"grid", groupId}};
    if (const QString props = textPropertiesJson(styles); !props.isEmpty()) params.insert("textProperties", props);
    apiPost("group", "/api/group/mention", params, cb, {{"nretry", "0"}});
}

void ZaloSession::sendDelivered(const QString& threadId, bool isGroup, const QJsonArray& msgs, Callback cb) {
    if (msgs.isEmpty()) return;
    auto flag = [](const QJsonValue& v) { return v.isUndefined() || v.isNull() ? -1 : 0; };
    QJsonArray data;
    for (const QJsonValue& v : msgs) {
        const QJsonObject m = v.toObject();
        const QString idTo = m.value("idTo").toVariant().toString();
        data.append(QJsonObject{
            {"cmi", m.value("cliMsgId").toVariant().toString()},
            {"gmi", m.value("msgId").toVariant().toString()},
            {"si", m.value("uidFrom").toVariant().toString()},
            {"di", idTo == m_uid ? QStringLiteral("0") : idTo},
            {"mt", m.value("msgType").toString()},
            {"st", flag(m.value("st"))},
            {"at", flag(m.value("at"))},
            {"cmd", flag(m.value("cmd"))},
            {"ts", m.value("ts").isUndefined() || m.value("ts").isNull() ? -1 : 0},
        });
        if (data.size() >= 50) break;
    }
    QJsonObject infos{{"seen", 0}, {"data", data}};
    if (isGroup) infos["grid"] = threadId;
    QJsonObject params{{"msgInfos", compactJson(infos)}};
    if (isGroup) params["imei"] = m_imei;
    if (isGroup) apiPost("group", "/api/group/deliveredv2", params, cb);
    else apiPost("chat", "/api/message/deliveredv2", params, cb);
}

void ZaloSession::fetchQuickMessages(Callback cb) {
    apiGet("quick_message", "/api/quickmessage/list", {{"version", 0}, {"lang", 0}, {"imei", m_imei}}, cb);
}

void ZaloSession::createQuickMessage(const QString& keyword, const QString& title, Callback cb) {
    apiGet("quick_message", "/api/quickmessage/create",
           {{"keyword", keyword}, {"message", QJsonObject{{"title", title}, {"params", ""}}},
            {"type", 0}, {"imei", m_imei}}, cb);
}

void ZaloSession::updateQuickMessage(qint64 itemId, const QString& keyword, const QString& title, Callback cb) {
    apiGet("quick_message", "/api/quickmessage/update",
           {{"itemId", itemId}, {"keyword", keyword}, {"message", QJsonObject{{"title", title}, {"params", ""}}},
            {"type", 0}, {"imei", m_imei}}, cb);
}

void ZaloSession::deleteQuickMessage(qint64 itemId, Callback cb) {
    apiGet("quick_message", "/api/quickmessage/delete", {{"itemIds", QJsonArray{itemId}}}, cb);
}

void ZaloSession::getSettings(Callback cb) {
    apiGet("chat", "/api/setting/me", {}, cb, true);
}

void ZaloSession::updateSetting(const QString& type, int value, Callback cb) {
    apiGet("chat", "/api/setting/update", {{type, value}}, cb);
}

void ZaloSession::fetchHiddenConversations(Callback cb) {
    apiGet("conversation", "/api/hiddenconvers/get-all", {{"imei", m_imei}}, cb);
}

void ZaloSession::setHiddenConversation(const QString& threadId, bool isGroup, bool hide, Callback cb) {
    const QJsonArray arr{QJsonObject{{"thread_id", threadId}, {"is_group", isGroup ? 1 : 0}}};
    const QString threads = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    apiPost("conversation", "/api/hiddenconvers/add-remove",
            {{hide ? "add_threads" : "del_threads", threads},
             {hide ? "del_threads" : "add_threads", QStringLiteral("[]")},
             {"imei", m_imei}}, cb);
}

void ZaloSession::updateHiddenConversPin(const QString& pinMd5, Callback cb) {
    apiGet("conversation", "/api/hiddenconvers/update-pin", {{"new_pin", pinMd5}, {"imei", m_imei}}, cb);
}

void ZaloSession::resetHiddenConversPin(Callback cb) {
    apiGet("conversation", "/api/hiddenconvers/reset", {}, cb, true);
}

void ZaloSession::fetchArchivedChats(Callback cb) {
    apiGet("label", "/api/archivedchat/list", {{"version", 1}, {"imei", m_imei}}, cb);
}

void ZaloSession::setArchivedChat(const QString& threadId, bool isGroup, bool archive, Callback cb) {
    const QJsonArray ids{QJsonObject{{"id", threadId}, {"type", isGroup ? 1 : 0}}};
    apiPost("label", "/api/archivedchat/update",
            {{"actionType", archive ? 0 : 1}, {"ids", ids}, {"imei", m_imei},
             {"version", QDateTime::currentMSecsSinceEpoch()}}, cb);
}

void ZaloSession::fetchAutoDeleteChats(Callback cb) {
    apiGet("conversation", "/api/conv/autodelete/getConvers", {}, cb, true);
}

void ZaloSession::setAutoDeleteChat(const QString& threadId, bool isGroup, qint64 ttl, Callback cb) {
    apiPost("conversation", "/api/conv/autodelete/updateConvers",
            {{"threadId", threadId}, {"isGroup", isGroup ? 1 : 0}, {"ttl", ttl}, {"clientLang", m_language}}, cb);
}

void ZaloSession::setGroupDeputy(const QString& groupId, const QString& memberId, bool add, Callback cb) {
    QJsonObject params{{"grid", groupId}, {"imei", m_imei}};
    params.insert("members", QJsonArray{memberId});
    apiGet("group", add ? "/api/group/admins/add" : "/api/group/admins/remove", params, cb);
}

void ZaloSession::changeGroupOwner(const QString& groupId, const QString& memberId, Callback cb) {
    apiGet("group", "/api/group/change-owner",
           {{"grid", groupId}, {"newAdminId", memberId}, {"imei", m_imei}, {"language", m_language}}, cb);
}

void ZaloSession::disperseGroup(const QString& groupId, Callback cb) {
    apiPost("group", "/api/group/disperse", {{"grid", groupId}, {"imei", m_imei}}, cb);
}

void ZaloSession::fetchGroupLink(const QString& groupId, Callback cb) {
    apiGet("group", "/api/group/link/detail", {{"grid", groupId}, {"imei", m_imei}}, cb);
}

void ZaloSession::newGroupLink(const QString& groupId, Callback cb) {
    apiGet("group", "/api/group/link/new", {{"grid", groupId}, {"imei", m_imei}}, cb);
}

void ZaloSession::disableGroupLink(const QString& groupId, Callback cb) {
    apiGet("group", "/api/group/link/disable", {{"grid", groupId}}, cb);
}

void ZaloSession::fetchGroupLinkInfo(const QString& link, int memberPage, Callback cb) {
    apiGet("group", "/api/group/link/ginfo",
           {{"link", link}, {"avatar_size", 120}, {"member_avatar_size", 120}, {"mpage", qMax(1, memberPage)}}, cb);
}

void ZaloSession::joinGroupLink(const QString& link, Callback cb) {
    apiGet("group", "/api/group/link/join", {{"link", link}, {"clientLang", m_language}}, cb);
}

void ZaloSession::fetchPendingMembers(const QString& groupId, Callback cb) {
    apiGet("group", "/api/group/pending-mems/list", {{"grid", groupId}, {"imei", m_imei}}, cb);
}

void ZaloSession::reviewPendingMembers(const QString& groupId, const QStringList& memberIds, bool approve,
                                       Callback cb) {
    apiGet("group", "/api/group/pending-mems/review",
           {{"grid", groupId}, {"members", QJsonArray::fromStringList(memberIds)}, {"isApprove", approve ? 1 : 0}},
           cb);
}

void ZaloSession::updateGroupSettings(const QString& groupId, const QJsonObject& setting, Callback cb) {
    static const char* const kKeys[] = {"blockName", "signAdminMsg", "setTopicOnly", "enableMsgHistory",
                                        "joinAppr", "lockCreatePost", "lockCreatePoll", "lockSendMsg",
                                        "lockViewMember", "bannFeature", "dirtyMedia", "banDuration"};
    QJsonObject params;
    for (const char* key : kKeys) params.insert(key, setting.value(key).toInt());
    params.insert("blocked_members", QJsonArray{});
    params.insert("grid", groupId);
    params.insert("imei", m_imei);
    apiGet("group", "/api/group/setting/update", params, cb);
}

void ZaloSession::fetchBlockedMembers(const QString& groupId, int page, int count, Callback cb) {
    apiGet("group", "/api/group/blockedmems/list",
           {{"grid", groupId}, {"page", qMax(1, page)}, {"count", qBound(1, count, 200)}, {"imei", m_imei}}, cb);
}

void ZaloSession::setMembersBlocked(const QString& groupId, const QStringList& memberIds, bool blocked, Callback cb) {
    apiGet("group", blocked ? "/api/group/blockedmems/add" : "/api/group/blockedmems/remove",
           {{"grid", groupId}, {"members", QJsonArray::fromStringList(memberIds)}}, cb);
}

void ZaloSession::inviteToGroups(const QString& userId, const QStringList& groupIds, Callback cb) {
    apiGet("group", "/api/group/invite/multi",
           {{"grids", QJsonArray::fromStringList(groupIds)}, {"member", userId}, {"memberType", -1},
            {"srcInteraction", 2}, {"clientLang", m_language}}, cb);
}

void ZaloSession::removeFriendAlias(const QString& friendId, Callback cb) {
    apiGet("alias", "/api/alias/remove", {{"friendId", friendId}}, cb);
}

void ZaloSession::createPoll(const QString& groupId, const QString& question, const QStringList& options,
                             bool allowMultiChoices, Callback cb) {
    QJsonArray optArr;
    for (const QString& opt : options) {
        const QString trimmed = opt.trimmed();
        if (!trimmed.isEmpty()) {
            optArr.append(trimmed);
        }
    }
    const QJsonObject params{
        {"group_id", groupId},
        {"question", question},
        {"options", optArr},
        {"expired_time", 0},
        {"pinAct", false},
        {"allow_multi_choices", allowMultiChoices},
        {"allow_add_new_option", true},
        {"is_hide_vote_preview", false},
        {"is_anonymous", false},
        {"poll_type", 0},
        {"src", 1},
        {"imei", m_imei}
    };
    apiPost("group", "/api/poll/create", params, cb);
}

void ZaloSession::votePoll(qint64 pollId, const QList<int>& optionIds, Callback cb) {
    QJsonArray optArr;
    for (int id : optionIds) {
        optArr.append(id);
    }
    const QJsonObject params{
        {"poll_id", pollId},
        {"option_ids", optArr},
        {"imei", m_imei}
    };
    apiGet("group", "/api/poll/vote", params, cb);
}

void ZaloSession::lockPoll(qint64 pollId, Callback cb) {
    const QJsonObject params{
        {"poll_id", pollId},
        {"imei", m_imei}
    };
    apiPost("group", "/api/poll/end", params, cb);
}

void ZaloSession::changeGroupName(const QString& groupId, const QString& newName, Callback cb) {
    const QJsonObject params{
        {"grid", groupId},
        {"gname", newName.isEmpty() ? QString::number(QDateTime::currentMSecsSinceEpoch()) : newName},
        {"imei", m_imei}
    };
    apiPost("group", "/api/group/updateinfo", params, cb);
}

void ZaloSession::addUserToGroup(const QString& groupId, const QStringList& memberIds, Callback cb) {
    QJsonArray memberArr;
    QJsonArray memberTypes;
    for (const QString& m : memberIds) {
        memberArr.append(m);
        memberTypes.append(-1);
    }
    const QJsonObject params{
        {"grid", groupId},
        {"members", memberArr},
        {"memberTypes", memberTypes},
        {"imei", m_imei},
        {"clientLang", m_language}
    };
    apiPost("group", "/api/group/invite/v2", params, cb);
}

void ZaloSession::removeUserFromGroup(const QString& groupId, const QString& memberId, Callback cb) {
    const QJsonObject params{
        {"grid", groupId},
        {"members", QJsonArray{memberId}},
        {"imei", m_imei}
    };
    apiPost("group", "/api/group/kick", params, cb);
}

void ZaloSession::leaveGroup(const QString& groupId, bool silent, Callback cb) {
    const QJsonObject params{
        {"grids", QJsonArray{groupId}},
        {"imei", m_imei},
        {"silent", silent ? 1 : 0},
        {"language", m_language}
    };
    apiPost("group", "/api/group/leave", params, cb);
}

void ZaloSession::changeFriendAlias(const QString& friendId, const QString& alias, Callback cb) {
    const QJsonObject params{
        {"friendId", friendId},
        {"alias", alias},
        {"imei", m_imei}
    };
    apiGet("alias", "/api/alias/update", params, cb);
}

void ZaloSession::sendContactCard(const QString& threadId, bool isGroup, const QString& contactId,
                                 const QString& qrCodeUrl, const QString& phone, const QString& cliMsgId, Callback cb) {
    QJsonObject msgInfo{{"contactUid", contactId}, {"qrCodeUrl", qrCodeUrl}};
    if (!phone.isEmpty()) msgInfo.insert("phone", phone);
    QJsonObject params{{"ttl", 0},
                       {"msgType", 6},
                       {"clientId", cliMsgId.isEmpty() ? QString::number(QDateTime::currentMSecsSinceEpoch()) : cliMsgId},
                       {"msgInfo", compactJson(msgInfo)}};
    if (isGroup) {
        params["grid"] = threadId;
        params["visibility"] = 0;
        apiPost("file", "/api/group/forward", params, cb);
    } else {
        params["toId"] = threadId;
        params["imei"] = m_imei;
        apiPost("file", "/api/message/forward", params, cb);
    }
}

void ZaloSession::fetchUserQr(const QStringList& userIds, Callback cb) {
    apiPost("friend", "/api/friend/mget-qr", {{"fids", QJsonArray::fromStringList(userIds)}}, cb);
}

void ZaloSession::updateLabels(const QJsonArray& labelData, qint64 version, Callback cb) {
    apiPost("label", "/api/convlabel/update",
            {{"labelData", QString::fromUtf8(QJsonDocument(labelData).toJson(QJsonDocument::Compact))},
             {"version", version},
             {"imei", m_imei}},
            cb);
}

int ZaloSession::clientMessageType(const QString& msgType) {
    static const QHash<QString, int> kClientTypes{
        {"webchat", 1}, {"chat.voice", 31}, {"chat.photo", 32}, {"chat.sticker", 36}, {"chat.doodle", 37},
        {"chat.recommended", 38}, {"chat.link", 38}, {"chat.video.msg", 44}, {"share.file", 46},
        {"chat.gif", 49}, {"chat.location.new", 43},
    };
    return kClientTypes.value(msgType, 1);
}

void ZaloSession::fetchGroupPins(const QString& groupId, qint64 boardVersion, Callback cb) {
    apiGet("group_board", "/api/board/pin/list", {{"groupId", groupId}, {"boardVersion", boardVersion}, {"imei", m_imei}}, cb);
}

void ZaloSession::createGroupPin(const QString& groupId, const QJsonObject& topic, Callback cb) {
    QJsonObject params{{"grid", groupId},
                       {"type", topic.value("type").toInt(2)},
                       {"color", topic.value("color").toInt(-16777216)},
                       {"emoji", topic.value("emoji").toString()},
                       {"startTime", -1},
                       {"duration", -1},
                       {"params", compactJson(topic.value("params").toObject())},
                       {"pinAct", 1},
                       {"repeat", 0},
                       {"src", -1},
                       {"imei", m_imei}};
    apiPost("group_board", "/api/board/topic/createv2", params, cb);
}

void ZaloSession::createReminder(const QString& threadId, bool isGroup, const QString& title, const QString& emoji,
                                 qint64 startTime, int repeat, Callback cb) {
    const QString glyph = emoji.isEmpty() ? QStringLiteral("⏰") : emoji;
    const qint64 when = startTime > 0 ? startTime : QDateTime::currentMSecsSinceEpoch();
    if (isGroup) {
        QJsonObject params{{"grid", threadId}, {"type", 0}, {"color", -16245706}, {"emoji", glyph},
                           {"startTime", when}, {"duration", -1},
                           {"params", compactJson(QJsonObject{{"title", title}})},
                           {"repeat", repeat}, {"src", 1}, {"imei", m_imei}, {"pinAct", 0}};
        apiPost("group_board", "/api/board/topic/createv2", params, cb);
        return;
    }
    const QJsonObject objectData{{"toUid", threadId}, {"type", 0}, {"color", -16245706}, {"emoji", glyph},
                                 {"startTime", when}, {"duration", -1},
                                 {"params", QJsonObject{{"title", title}}}, {"needPin", false},
                                 {"repeat", repeat}, {"creatorUid", m_uid}, {"src", 1}};
    apiPost("group_board", "/api/board/oneone/create",
            {{"objectData", compactJson(objectData)}, {"imei", m_imei}}, cb);
}

void ZaloSession::listReminders(const QString& threadId, bool isGroup, Callback cb) {
    const QJsonObject objectData = isGroup
        ? QJsonObject{{"group_id", threadId}, {"board_type", 1}, {"page", 1}, {"count", 20}, {"last_id", 0}, {"last_type", 0}}
        : QJsonObject{{"uid", threadId}, {"board_type", 1}, {"page", 1}, {"count", 20}, {"last_id", 0}, {"last_type", 0}};
    QJsonObject params{{"objectData", compactJson(objectData)}};
    if (isGroup) params.insert("imei", m_imei);
    apiGet("group_board", isGroup ? "/api/board/listReminder" : "/api/board/oneone/list", params, cb, true);
}

void ZaloSession::removeReminder(const QString& threadId, bool isGroup, const QString& reminderId, Callback cb) {
    if (isGroup) {
        apiPost("group_board", "/api/board/topic/remove",
                {{"grid", threadId}, {"topicId", reminderId}, {"imei", m_imei}}, cb);
        return;
    }
    apiPost("group_board", "/api/board/oneone/remove", {{"uid", threadId}, {"reminderId", reminderId}}, cb);
}

void ZaloSession::createNote(const QString& groupId, const QString& title, bool pin, Callback cb) {
    QJsonObject params{{"grid", groupId}, {"type", 0}, {"color", -16777216}, {"emoji", QString()},
                       {"startTime", -1}, {"duration", -1},
                       {"params", compactJson(QJsonObject{{"title", title}})},
                       {"repeat", 0}, {"src", 1}, {"imei", m_imei}, {"pinAct", pin ? 1 : 0}};
    apiPost("group_board", "/api/board/topic/createv2", params, cb);
}

void ZaloSession::unpinGroupPins(const QString& groupId, const QJsonArray& topics, qint64 boardVersion, Callback cb) {
    if (topics.size() == 1) {
        apiGet("group_board", "/api/board/unpinv2",
               {{"grid", groupId}, {"imei", m_imei}, {"topic", topics.first()}, {"boardVersion", boardVersion}}, cb);
    } else {
        apiGet("group_board", "/api/board/multi_unpin", {{"grid", groupId}, {"topics", topics}, {"boardVersion", boardVersion}}, cb);
    }
}

void ZaloSession::reorderGroupPins(const QString& groupId, const QJsonArray& topics, qint64 boardVersion, Callback cb) {
    apiPost("group_board", "/api/board/pin/update", {{"groupId", groupId}, {"topics", topics}, {"boardVersion", boardVersion}}, cb);
}

void ZaloSession::fetchFriendPins(const QString& userId, qint64 version, Callback cb) {
    apiGet("friend_board", "/api/friendboard/list", {{"conversationId", userId}, {"version", version}, {"imei", m_imei}}, cb);
}

void ZaloSession::createFriendPin(const QString& userId, const QJsonObject& topic, qint64 version, Callback cb) {
    QJsonObject t = topic;
    t.insert("params", compactJson(topic.value("params").toObject()));
    t.insert("src", -1);
    t.insert("pinAct", 1);
    apiGet("friend_board", "/api/friendboard/create",
           {{"conversationId", userId}, {"topic", t}, {"version", version}, {"lang", m_language}}, cb);
}

void ZaloSession::unpinFriendPins(const QString& userId, const QJsonArray& topics, qint64 version, Callback cb) {
    apiGet("friend_board", "/api/friendboard/multi_unpin",
           {{"conversationId", userId}, {"topics", topics}, {"version", version}, {"lang", m_language}}, cb);
}

void ZaloSession::reorderFriendPins(const QString& userId, const QJsonArray& topics, qint64 version, Callback cb) {
    apiGet("friend_board", "/api/friendboard/reorder",
           {{"conversationId", userId}, {"topics", topics}, {"version", version}, {"lang", m_language}}, cb);
}

void ZaloSession::updateProfile(const QString& name, int gender, const QString& dob, Callback cb) {
    const QJsonObject profileObj{
        {"name", name},
        {"gender", gender},
        {"dob", dob}
    };
    const QJsonObject params{
        {"profile", QString::fromUtf8(QJsonDocument(profileObj).toJson(QJsonDocument::Compact))},
        {"biz", QStringLiteral("{}")},
        {"language", m_language}
    };
    apiPost("profile", "/api/social/profile/update", params, cb);
}

void ZaloSession::updateProfileBio(const QString& status, Callback cb) {
    const QJsonObject params{
        {"status", status}
    };
    apiPost("profile", "/api/social/profile/status", params, cb);
}

void ZaloSession::blockUser(const QString& userId, Callback cb) {
    const QJsonObject params{
        {"fid", userId},
        {"imei", m_imei}
    };
    apiPost("friend", "/api/friend/block", params, cb);
}

void ZaloSession::unblockUser(const QString& userId, Callback cb) {
    const QJsonObject params{
        {"fid", userId},
        {"imei", m_imei}
    };
    apiPost("friend", "/api/friend/unblock", params, cb);
}

void ZaloSession::removeFriend(const QString& userId, Callback cb) {
    const QJsonObject params{
        {"fid", userId},
        {"imei", m_imei}
    };
    apiPost("friend", "/api/friend/remove", params, cb);
}

void ZaloSession::connectRealtime() {
    const QJsonArray urls = m_loginInfo.value("zpw_ws").toArray();
    if (urls.isEmpty() || m_ws) return;

    m_ws = new QWebSocket(QStringLiteral("https://chat.zalo.me"), QWebSocketProtocol::VersionLatest, this);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &ZaloSession::onWsBinary);
    connect(m_ws, &QWebSocket::textMessageReceived, this, [this](const QString&) { m_pongWatchdog.stop(); });
    connect(m_ws, &QWebSocket::pong, this, [this](quint64, const QByteArray&) { m_pongWatchdog.stop(); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(m_ws, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
#else
    connect(m_ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, [this](QAbstractSocket::SocketError e) {
#endif
        logRealtime(QStringLiteral("socket error %1: %2").arg(int(e)).arg(m_ws ? m_ws->errorString() : QString()));
    });
    connect(m_ws, &QWebSocket::disconnected, this, [this]() {
        const bool wasReady = m_realtimeReady;
        const auto closeCode = m_ws ? m_ws->closeCode() : QWebSocketProtocol::CloseCodeNormal;
        logRealtime(QStringLiteral("disconnected: close code %1 %2").arg(int(closeCode))
                        .arg(m_ws ? m_ws->closeReason() : QString()));
        m_pingTimer.stop();
        m_pongWatchdog.stop();
        m_realtimeReady = false;
        m_cipherKey.clear();
        m_serverQueues.clear();
        m_pullPages.clear();
        if (m_ws) {
            m_ws->deleteLater();
            m_ws = nullptr;
        }
        if (wasReady) emit realtimeStateChanged(false);
        if (closeCode == 3000 || m_secretKey.isEmpty()) return;
        const QJsonArray all = m_loginInfo.value("zpw_ws").toArray();
        m_wsUrlIndex = all.isEmpty() ? 0 : (m_wsUrlIndex + 1) % all.size();
        QTimer::singleShot(5000, this, &ZaloSession::connectRealtime);
    });

    const QString base = urls.at(qMin(m_wsUrlIndex, int(urls.size()) - 1)).toString();
    QNetworkRequest req(makeUrl(base, {{"t", QString::number(QDateTime::currentMSecsSinceEpoch())}}));
    req.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    req.setRawHeader("Accept-Language", "vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7");
    req.setRawHeader("Cache-Control", "no-cache");
    QStringList cookieParts;
    for (const QNetworkCookie& c : m_jar->cookiesForUrl(QUrl("https://chat.zalo.me/"))) {
        cookieParts << QString::fromUtf8(c.name()) + "=" + QString::fromUtf8(c.value());
    }
    req.setRawHeader("Cookie", cookieParts.join("; ").toUtf8());
    m_ws->open(req);
}

void ZaloSession::disconnectRealtime() {
    m_pingTimer.stop();
    m_pongWatchdog.stop();
    m_realtimeReady = false;
    m_cipherKey.clear();
    if (m_ws) {
        QWebSocket* ws = m_ws;
        m_ws = nullptr;
        ws->disconnect(this);
        ws->close();
        ws->deleteLater();
    }
}

namespace {
bool isHistoryQueue(int cmd, int subCmd) {
    return subCmd == 1 && (cmd == 510 || cmd == 511 || cmd == 610 || cmd == 611);
}
constexpr int kMaxPullPages = 20;
}

QList<QPair<int, int>> ZaloSession::queuesFromAuth(const QJsonObject& auth) {
    QList<QPair<int, int>> out;
    const QJsonObject data = auth.value("data").toObject();
    for (const char* key : {"ctrl_qCmds", "qCmds"}) {
        for (const QJsonValue& v : data.value(key).toArray()) {
            const QJsonObject q = v.toObject();
            if (!q.contains("cmd")) continue;
            const QPair<int, int> pair(q.value("cmd").toInt(), q.value("subCmd").toInt(0));
            if (pair.first > 0 && !out.contains(pair)) out.append(pair);
        }
    }
    return out;
}

QJsonObject ZaloSession::nextPullPage(const QJsonObject& data, int cmd, int subCmd) {
    if (data.value("more").toInt() != 1) return {};
    const QJsonObject status = data.value("queueStatus").toObject()
                                   .value(QStringLiteral("%1_%2").arg(cmd).arg(subCmd)).toObject();
    QJsonValue lastId = data.value("lastActionId");
    if (lastId.isUndefined() || lastId.isNull()) lastId = status.value("lastId");
    if (lastId.isUndefined() || lastId.isNull()) return {};
    return {{"first", false}, {"lastId", lastId}, {"preIds", status.value("ids").toArray()}};
}

void ZaloSession::pullServerQueues() {
    for (const auto& q : std::as_const(m_serverQueues)) {
        if (isHistoryQueue(q.first, q.second)) continue;
        sendWs(q.first, q.second, {{"first", true}, {"lastId", 1}, {"preIds", QJsonArray()}}, QStringLiteral("reqId"));
    }
}

void ZaloSession::requestOldMessages(bool isGroup, const QString& lastMsgId) {
    sendWs(isGroup ? 511 : 510, 1,
           {{"first", true}, {"lastId", lastMsgId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(lastMsgId)},
            {"preIds", QJsonArray()}});
}

void ZaloSession::requestOldReactions(bool isGroup) {
    const int cmd = isGroup ? 611 : 610;
    if (!canPullQueue(cmd, 1)) return;
    sendWs(cmd, 1, {{"first", true}, {"lastId", QJsonValue(QJsonValue::Null)}, {"preIds", QJsonArray()}});
}

bool ZaloSession::canPullQueue(int cmd, int subCmd) const {
    if (m_serverQueues.isEmpty()) return true;
    const QPair<int, int> pair(cmd, subCmd);
    return m_serverQueues.contains(pair);
}

void ZaloSession::sendWs(int cmd, int subCmd, QJsonObject data, const QString& reqIdKey) {
    if (!m_ws || m_ws->state() != QAbstractSocket::ConnectedState) return;
    if (!reqIdKey.isEmpty()) data[reqIdKey] = QStringLiteral("req_%1").arg(m_wsRequestId++);
    const QByteArray payload = QJsonDocument(data).toJson(QJsonDocument::Compact);
    QByteArray frame(4, '\0');
    frame[0] = 1;
    qToLittleEndian<quint16>(static_cast<quint16>(cmd), frame.data() + 1);
    frame[3] = static_cast<char>(subCmd);
    m_ws->sendBinaryMessage(frame + payload);
}

void ZaloSession::onWsBinary(const QByteArray& frame) {
    m_pongWatchdog.stop();
    if (frame.size() < 4) return;
    const int version = static_cast<quint8>(frame.at(0));
    const int cmd = qFromLittleEndian<quint16>(frame.constData() + 1);
    const int subCmd = static_cast<quint8>(frame.at(3));
    const QJsonObject parsed = QJsonDocument::fromJson(frame.mid(4)).object();
    if (parsed.isEmpty() || version != 1) return;

    if (cmd == 1 && subCmd == 1 && parsed.contains("key")) {
        m_cipherKey = parsed.value("key").toString();
        m_serverQueues.clear();
        m_pullPages.clear();
        if (parsed.value("data").isString()) {
            bool authOk = false;
            const QByteArray body = ZaloCrypto::decodeEventData(parsed.value("data").toString(),
                                                                parsed.value("encrypt").toInt(-1), m_cipherKey, &authOk);
            if (authOk) m_serverQueues = queuesFromAuth(QJsonDocument::fromJson(body).object());
        }
        const int interval = m_serverSettings.value("features").toObject().value("socket").toObject()
                                 .value("ping_interval").toInt(60000);
        m_pingTimer.start(qMax(10000, interval));
        m_realtimeReady = true;
        emit realtimeStateChanged(true);
        pullServerQueues();
        return;
    }
    if (cmd == 3000) {
        emit realtimeReplaced();
        logRealtime(QStringLiteral("cmd 3000: another client took over"));
        QMetaObject::invokeMethod(this, [this]() {
            if (m_ws) m_ws->close(QWebSocketProtocol::CloseCode(3000));
        }, Qt::QueuedConnection);
        return;
    }

    const bool userLive = (cmd == 501 || cmd == 551) && subCmd == 0;
    const bool groupLive = cmd == 521 && subCmd == 0;
    const bool userOld = cmd == 510 && subCmd == 1;
    const bool groupOld = cmd == 511 && subCmd == 1;
    const bool offlineMsgs = cmd == 513 || cmd == 514 || cmd == 515 || cmd == 517 || cmd == 518 || cmd == 534
                             || cmd == 570;

    auto isKnownCommand = [cmd, userLive, groupLive, userOld, groupOld, offlineMsgs]() -> bool {
        if (userLive || groupLive || userOld || groupOld || offlineMsgs) return true;
        switch (cmd) {
        case 502: case 522: case 533:
        case 601: case 602: case 603: case 604:
        case 610: case 611: case 612:
            return true;
        default:
            return false;
        }
    };
    if (!isKnownCommand()) return;

    bool ok = false;
    const QByteArray json = ZaloCrypto::decodeEventData(parsed.value("data").toString(),
                                                        parsed.value("encrypt").toInt(-1), m_cipherKey, &ok);
    if (!ok) return;
    const QJsonObject data = QJsonDocument::fromJson(json).object().value("data").toObject();

    if (cmd == 533) {
        if (data.contains("queueId") && data.contains("ids")) {
            sendWs(570, 0, {{"queueId", data.value("queueId")}, {"ids", data.value("ids")}, {"lastId", "1"}},
                   QStringLiteral("reqId"));
        }
        return;
    }

    const QJsonObject next = nextPullPage(data, cmd, subCmd);
    if (!next.isEmpty()) {
        int& pages = m_pullPages[(cmd << 8) | subCmd];
        if (pages++ < kMaxPullPages) {
            sendWs(cmd, subCmd, next, isHistoryQueue(cmd, subCmd) ? QStringLiteral("req_id") : QStringLiteral("reqId"));
        }
    }

    if (data.contains("clearUnreads")) emit readPointsReceived(data.value("clearUnreads").toArray());

    if (userLive || groupLive || userOld || groupOld) {
        const bool isGroup = groupLive || groupOld;
        const QJsonArray msgs = splitReactionMessages(data.value(isGroup ? "groupMsgs" : "msgs").toArray(), isGroup);
        if (!msgs.isEmpty() || userOld || groupOld) emit messagesReceived(msgs, isGroup, userOld || groupOld);
        return;
    }
    if (offlineMsgs) {
        emitMessageBuckets(data, false);
        return;
    }

    if (cmd == 612 || cmd == 610 || cmd == 611) {
        emitReactionArrays(data, cmd != 611, cmd != 610);
        return;
    }

    if (cmd == 602) {
        for (const QJsonValue& v : data.value("actions").toArray()) {
            const QJsonObject action = v.toObject();
            if (action.value("act_type").toString() != "typing") continue;
            QString rawData = action.value("data").toString().trimmed();
            if (!rawData.startsWith('{') && !rawData.isEmpty()) rawData = "{" + rawData + "}";
            const QJsonObject t = QJsonDocument::fromJson(rawData.toUtf8()).object();
            const bool isGroup = action.value("act").toString() == "gtyping";
            const QString uid = t.value("uid").toVariant().toString();
            emit typingReceived(isGroup ? t.value("gid").toVariant().toString() : uid, uid, isGroup);
        }
        return;
    }

    if (cmd == 502 || cmd == 522) {
        for (const QJsonValue& v : data.value("delivereds").toArray()) {
            const QJsonObject del = v.toObject();
            const QString thread = del.value(cmd == 522 ? "groupId" : "idTo").toVariant().toString();
            const QString msgId = del.value("msgId").toVariant().toString();
            if (!thread.isEmpty() && !msgId.isEmpty()) {
                emit deliveredReceived(thread, msgId, cmd == 522);
            }
        }
        if (cmd == 502) {
            for (const QJsonValue& v : data.value("seens").toArray()) {
                const QJsonObject seen = v.toObject();
                const QString thread = seen.value("idTo").toVariant().toString();
                emit seenReceived(thread, seen.value("msgId").toVariant().toString(), thread, false);
            }
        } else {
            for (const QJsonValue& v : data.value("groupSeens").toArray()) {
                const QJsonObject seen = v.toObject();
                for (const QJsonValue& u : seen.value("seenUids").toArray()) {
                    emit seenReceived(seen.value("groupId").toVariant().toString(),
                                      seen.value("msgId").toVariant().toString(), u.toVariant().toString(), true);
                }
            }
        }
        return;
    }

    if (cmd == 601 || cmd == 603 || cmd == 604) {
        handleControls(data.value("controls").toArray());
        emitReactionArrays(data, true, true);
    }
}

void ZaloSession::emitReactionArrays(const QJsonObject& data, bool users, bool groups) {
    auto normalise = [](const QJsonArray& in) {
        QJsonArray out;
        for (const QJsonValue& v : in) {
            QJsonObject r = v.toObject();
            if (r.value("content").isString())
                r["content"] = QJsonDocument::fromJson(r.value("content").toString().toUtf8()).object();
            out.append(r);
        }
        return out;
    };
    if (users) {
        const QJsonArray reacts = normalise(data.value("reacts").toArray());
        if (!reacts.isEmpty()) emit reactionsReceived(reacts, false);
    }
    if (groups) {
        const QJsonArray reactGroups = normalise(data.value("reactGroups").toArray());
        if (!reactGroups.isEmpty()) emit reactionsReceived(reactGroups, true);
    }
}

QJsonArray ZaloSession::splitReactionMessages(const QJsonArray& msgs, bool isGroup) {
    QJsonArray kept, reactions;
    for (const QJsonValue& v : msgs) {
        const QJsonObject m = v.toObject();
        if (m.value("msgType").toString() != QLatin1String("chat.reaction")) {
            kept.append(v);
            continue;
        }
        QJsonValue content = m.value("content");
        QJsonObject c = content.isString() ? QJsonDocument::fromJson(content.toString().toUtf8()).object() : content.toObject();
        if (c.value("params").isString()) {
            const QJsonObject inner = QJsonDocument::fromJson(c.value("params").toString().toUtf8()).object();
            if (inner.contains("rMsg")) c = inner;
        }
        if (!c.contains("rMsg")) continue;
        QJsonObject r = m;
        r["content"] = c;
        reactions.append(r);
    }
    if (!reactions.isEmpty()) emit reactionsReceived(reactions, isGroup);
    return kept;
}

void ZaloSession::emitMessageBuckets(const QJsonObject& data, bool isOld) {
    QJsonArray users = data.value("msgs").toArray();
    for (const QJsonValue& v : data.value("pageMsgs").toArray()) users.append(v);
    users = splitReactionMessages(users, false);
    const QJsonArray groups = splitReactionMessages(data.value("groupMsgs").toArray(), true);
    emitReactionArrays(data, true, true);
    if (!users.isEmpty()) emit messagesReceived(users, false, isOld);
    if (!groups.isEmpty()) emit messagesReceived(groups, true, isOld);
}

void ZaloSession::handleControls(const QJsonArray& controls) {
    for (const QJsonValue& v : controls) {
        const QJsonObject content = v.toObject().value("content").toObject();
        const QString type = content.value("act_type").toString();
        if (type == "file_done") {
            const QString fileId = content.value("fileId").toVariant().toString();
            const QJsonValue raw = content.value("data");
            const QJsonObject fileData = raw.isString()
                ? QJsonDocument::fromJson(raw.toString().toUtf8()).object() : raw.toObject();
            auto waiter = m_uploadWaiters.take(fileId);
            if (waiter) {
                waiter(fileData);
            } else if (!fileId.isEmpty()) {
                m_earlyFileDone.insert(fileId, fileData);
                QTimer::singleShot(120000, this, [this, fileId]() { m_earlyFileDone.remove(fileId); });
            }
        } else if (type == "voip") {
            emit voipControlReceived(content);
        } else if (type == "syncmsgmb" || type == "transfer_msg") {
            emit mobileSyncControlReceived(content);
        } else if (type == "fr") {
            const QString act = content.value("act").toString();
            if (act == "pin_create" || act == "pin_unpin") {
                QJsonValue cdata = content.value("data");
                const QString raw = cdata.toString().trimmed();
                if (raw.startsWith('{') || raw.startsWith('[')) {
                    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
                    cdata = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
                }
                emit controlReceived("pin_board", act, cdata);
            } else if (act != "req") {
                emit friendEventReceived(act, content.value("data"));
            }
        } else if (!type.isEmpty()) {
            QJsonValue data = content.value("data");
            const QString raw = data.toString().trimmed();
            if (raw.startsWith('{') || raw.startsWith('[')) {
                const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
                data = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
            }
            emit controlReceived(type, content.value("act").toString(), data);
        }
    }
}

}
