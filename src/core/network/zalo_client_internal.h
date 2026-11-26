#pragma once

#include "zalo_client.h"
#include <QJSEngine>
#include <QCryptographicHash>
#include "crypto/db_cross.h"
#include "sync/mobile_history_sync.h"
#include "fs/file_utils.h"
#include "platform/file_picker.h"
#include "media/media_utils.h"
#include "call/zcall_engine.h"
#include "offline/offline_store.h"
#include "models/text_fold.h"
#include "network/zalo_api_client.h"
#include "network/zalo_session.h"
#include "network/error_catalog.h"
#include "network/message_scheduler.h"
#include "platform/desktop_notifications.h"
#include "media/voice_recorder.h"
#include "platform/app_lock.h"
#include "platform/shortcut_map.h"
#include "platform/storage_manager.h"
#include "platform/system_tray.h"
#include "platform/render_profile.h"
#include "platform/multimedia_support.h"
#include "platform/avatar_image_provider.h"
#include "platform/app_updater.h"
#include "qrcodegen.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QVersionNumber>
#include <QProcess>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QPointer>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QThreadPool>
#include <QDesktopServices>
#include <QSaveFile>
#include <QRegularExpression>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <QDateTime>
#include <QFileInfo>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QUrl>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QJsonDocument>
#include <QVariantMap>
#include <QSettings>
#include <QFile>
#include <QTimer>
#include <QDebug>
#include <QHostInfo>
#include <QCryptographicHash>
#include <QMimeData>
#include <QImage>
#include <QScreen>
#include <QPixmap>
#include <QStandardPaths>

#include <algorithm>
#include <memory>


namespace Zalos::Core::Network::Detail {

constexpr int kQrLifetimeSeconds = 120;

template <typename Key, typename T>
void dropOldestIfFull(BoundedHash<Key, T>& c) {
    if (c.size() >= kMaxTrackedIds) c.removeOldest();
}
constexpr int kGroupInfoBatch = 50;
constexpr int kProfileInfoBatch = 100;

struct ReactionDef {
    const char* key;
    const char* icon;
    int rType;
    const char* emoji;
};
constexpr ReactionDef kReactions[] = {
    {"heart", "/-heart", 5, "❤️"},  {"like", "/-strong", 3, "👍"}, {"haha", ":>", 0, "😆"},
    {"wow", ":o", 32, "😮"},        {"cry", ":-((", 2, "😢"},      {"angry", ":-h", 20, "😡"},
    {"kiss", ":-*", 8, "😘"},       {"tears_of_joy", ":')", 7, "😂"}, {"rose", "/-rose", 120, "🌹"},
    {"broken_heart", "/-break", 65, "💔"}, {"dislike", "/-weak", 4, "👎"}, {"love", ";xx", 29, "😍"},
    {"ok", "/-ok", 68, "👌"},       {"pray", "_()_", 73, "🙇"},    {"thanks", "/-thanks", 70, "🙏"},
    {"shit", "/-shit", 66, "💩"},   {"confused", ";-/", 51, "😖"}, {"wink", ";-)", 45, "😉"},
    {"fade", "/-fade", 121, "🥀"},  {"sun", "/-li", 67, "⚡"},     {"birthday", "/-bd", 126, "🎂"},
    {"bomb", "/-bome", 127, "💣"},  {"peace", "/-v", 69, "✌️"},    {"punch", "/-punch", 71, "👊"},
    {"share", "/-share", 72, "🤝"}, {"no", "/-no", 131, "🙅"},     {"bad", "/-bad", 132, "😒"},
    {"love_you", "/-loveu", 133, "🫶"}, {"sad", "--b", 1, "😓"},   {"very_sad", ":((", 16, "😭"},
    {"cool", "x-)", 21, "😄"},      {"nerd", "8-)", 22, "🤓"},     {"big_smile", ";-d", 23, "🤪"},
    {"sunglasses", "b-)", 26, "😎"}, {"neutral", ":--|", 30, "😑"}, {"sad_face", "p-(", 35, "😩"},
    {"bye", ":-bye", 36, "🙋"},     {"sleepy", "|-)", 38, "🥱"},   {"wipe", ":wipe", 39, "😅"},
    {"dig", ":-dig", 42, "🤨"},     {"anguish", "&-(", 44, "😠"},  {"handclap", ":handclap", 46, "👏"},
    {"angry_face", ">-|", 47, "🙄"}, {"f_chair", ":-f", 48, "😪"}, {"l_chair", ":-l", 49, "😔"},
    {"r_chair", ":-r", 50, "😕"},   {"silent", ";-x", 52, "🤬"},   {"surprise", ":-o", 53, "🤯"},
    {"embarrassed", ";-s", 54, "🤢"}, {"afraid", ";-a", 60, "🥳"}, {"sad2", ":-<", 61, "🥺"},
    {"big_laugh", ":))", 62, "🤣"}, {"rich", "$-)", 63, "🤩"},     {"beer", "/-beer", 99, "🍺"},
};

inline QString reactionEmoji(const QString& icon) {
    for (const auto& r : kReactions) {
        if (icon == QLatin1String(r.icon)) return QString::fromUtf8(r.emoji);
    }
    return QStringLiteral("•");
}

inline QString idOf(const QJsonValue& v) {
    return v.toVariant().toString();
}

inline QString pickString(const QJsonObject& obj, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const QString v = obj.value(QLatin1String(key)).toString().trimmed();
        if (!v.isEmpty()) return v;
    }
    return {};
}

inline QString captionOf(const QJsonObject& attach) {
    static const QRegularExpression placeholder(QStringLiteral("^\\[[^\\]]{1,24}\\]$"));
    for (const char* key : {"title", "description", "caption"}) {
        const QString candidate = attach.value(QLatin1String(key)).toString().trimmed();
        if (candidate.isEmpty() || placeholder.match(candidate).hasMatch()) continue;
        if (candidate.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || candidate.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
            continue;
        }
        return candidate;
    }
    return {};
}

inline QJsonObject unwrapCloudHistoryPayload(const QJsonValue& raw, QJsonArray* msgsOut) {
    QJsonValue val = raw;
    if (val.isString()) {
        const QJsonDocument doc = QJsonDocument::fromJson(val.toString().toUtf8());
        if (doc.isObject()) val = doc.object();
        else if (doc.isArray()) val = doc.array();
    }
    if (val.isArray()) {
        for (const QJsonValue& v : val.toArray()) msgsOut->append(v);
        return {};
    }
    QJsonObject obj = val.toObject();
    if (obj.contains("data") && !obj.contains("groupMsgs") && !obj.contains("msgs") && !obj.contains("messages")) {
        const QJsonValue inner = obj.value("data");
        if (inner.isString()) {
            const QJsonDocument doc = QJsonDocument::fromJson(inner.toString().toUtf8());
            if (doc.isArray()) {
                for (const QJsonValue& v : doc.array()) msgsOut->append(v);
                return obj;
            }
            if (doc.isObject()) obj = doc.object();
        } else if (inner.isObject()) {
            obj = inner.toObject();
        }
    }
    for (const char* key : {"groupMsgs", "msgs", "messages", "data"}) {
        for (const QJsonValue& v : obj.value(QLatin1String(key)).toArray()) msgsOut->append(v);
    }
    return obj;
}

inline QString pickName(const QJsonObject& obj) {
    for (const char* key : {"displayName", "display_name", "zaloName", "zalo_name", "name", "username"}) {
        const QString v = obj.value(key).toString().trimmed();
        if (!v.isEmpty()) return v;
    }
    return {};
}

inline QString pickAvatar(const QJsonObject& obj) {
    for (const char* key : {"avatar", "avt", "avatarUrl", "fullAvt"}) {
        const QString v = obj.value(key).toString().trimmed();
        if (!v.isEmpty()) return v;
    }
    return {};
}

inline QString normalizeDisplayPhone(const QString& raw) {
    QString digits;
    digits.reserve(raw.size());
    for (const QChar ch : raw) {
        if (ch.isDigit()) digits.append(ch);
    }
    if (digits.startsWith(QLatin1String("84")) && digits.size() >= 11 && digits.size() <= 12
        && digits.at(2) != QLatin1Char('0')) {
        return QLatin1Char('0') + digits.mid(2);
    }
    return raw.trimmed();
}

inline QJsonObject pickOwnProfileObject(const QJsonValue& data) {
    const QJsonObject root = data.toObject();
    const QJsonObject candidates[] = {
        root.value("profile").toObject(),
        root.value("info").toObject(),
        root.value("user").toObject(),
        root,
    };
    for (const QJsonObject& obj : candidates) {
        if (!obj.isEmpty() && (!pickName(obj).isEmpty() || !pickAvatar(obj).isEmpty())) return obj;
    }
    return candidates[0].isEmpty() ? root : candidates[0];
}

inline bool isOfficialAccount(const QJsonObject& user) {
    const QJsonValue oa = user.value("oaInfo");
    if (oa.isObject() && !oa.toObject().isEmpty()) return true;
    const QString id = idOf(user.value("userId").isUndefined() ? user.value("uid") : user.value("userId"));
    if (id.startsWith('g')) return false;
    if (user.value("type").toVariant().toInt() >= 1) return true;
    return false;
}

struct MuteSnapshot {
    QSet<QString> muted;
    QHash<QString, qint64> until;
};

inline MuteSnapshot parseMuteSnapshot(const QJsonObject& obj, qint64 fallbackNowSec) {
    qint64 nowSec = obj.value("systemTime").toVariant().toLongLong();
    if (nowSec > 100000000000LL) nowSec /= 1000;
    if (nowSec <= 0) nowSec = fallbackNowSec;
    MuteSnapshot out;
    for (const char* key : {"chatEntries", "groupChatEntries"}) {
        for (const QJsonValue& v : obj.value(key).toArray()) {
            const QJsonObject e = v.toObject();
            const QString id = idOf(e.value("id"));
            if (id.isEmpty()) continue;
            const int muteMode = e.value("muteMode").toInt(0);
            const qint64 duration = e.value("duration").toVariant().toLongLong();
            const qint64 start = e.value("startTime").toVariant().toLongLong();
            const bool infinite = duration == -1 || muteMode == 2 || muteMode == 3;
            if (!infinite && (duration == 0 || start + duration < nowSec)) continue;
            out.muted.insert(id);
            out.until.insert(id, !infinite && duration > 0 ? (start + duration) * 1000 : 0);
        }
    }
    return out;
}

inline QString parseReactionSnapshot(const QJsonObject& msg) {
    QJsonValue reactsVal = msg.value(QLatin1String("reacts"));
    if (reactsVal.isUndefined() || reactsVal.isNull()) {
        reactsVal = msg.value(QLatin1String("reactions"));
    }
    if (reactsVal.isUndefined() || reactsVal.isNull()) return {};

    QJsonObject rootObj;
    if (reactsVal.isObject()) {
        rootObj = reactsVal.toObject();
        if (rootObj.contains(QLatin1String("reactions"))) {
            reactsVal = rootObj.value(QLatin1String("reactions"));
        }
    }

    QMap<QString, int> counts;
    QStringList order;

    if (reactsVal.isObject()) {
        const QJsonObject users = reactsVal.toObject();
        for (auto it = users.constBegin(); it != users.constEnd(); ++it) {
            if (!it.value().isObject()) continue;
            const QJsonObject types = it.value().toObject();
            for (auto tit = types.constBegin(); tit != types.constEnd(); ++tit) {
                bool ok = false;
                const int rType = tit.key().toInt(&ok);
                if (!ok || rType < 0) continue;
                const int n = tit.value().toVariant().toInt();
                if (n <= 0) continue;
                QString icon;
                for (const auto& def : kReactions) {
                    if (def.rType == rType) { icon = QString::fromLatin1(def.icon); break; }
                }
                const QString emoji = reactionEmoji(icon);
                if (!counts.contains(emoji)) order.append(emoji);
                counts[emoji] += n;
            }
        }
    } else if (reactsVal.isArray()) {
        const QJsonArray arr = reactsVal.toArray();
        for (const QJsonValue& item : arr) {
            if (item.isString()) {
                const QString emoji = reactionEmoji(item.toString());
                if (!counts.contains(emoji)) order.append(emoji);
                counts[emoji] += 1;
            } else if (item.isObject()) {
                const QJsonObject o = item.toObject();
                const QString icon = o.value(QLatin1String("rIcon")).toString();
                const int n = o.value(QLatin1String("count")).toInt(1);
                if (!icon.isEmpty() && n > 0) {
                    const QString emoji = reactionEmoji(icon);
                    if (!counts.contains(emoji)) order.append(emoji);
                    counts[emoji] += n;
                }
            }
        }
    }

    if (counts.isEmpty()) return {};

    std::stable_sort(order.begin(), order.end(), [&counts](const QString& a, const QString& b) {
        return counts.value(a) > counts.value(b);
    });

    int total = 0;
    for (int n : std::as_const(counts)) total += n;
    return order.mid(0, 3).join(QString()) + (total > 1 ? QLatin1Char(' ') + QString::number(total) : QString());
}

struct BankCardFields {
    int binBank{0};
    QString bankName;
    QString accountNumber;
    QString accountHolder;
};

inline void scanBankJson(const QJsonValue& v, BankCardFields* out, int depth = 0) {
    if (!out || depth > 6) return;
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (out->binBank <= 0) {
            for (const char* k : {"binBank", "bin", "bankBin"}) {
                const int b = o.value(QLatin1String(k)).toVariant().toInt();
                if (b >= 100000) { out->binBank = b; break; }
            }
        }
        if (out->bankName.isEmpty()) {
            for (const char* k : {"bankName", "shortName", "short_name"}) {
                const QString s = o.value(QLatin1String(k)).toString().trimmed();
                if (!s.isEmpty()) { out->bankName = s; break; }
            }
        }
        if (out->accountNumber.isEmpty()) {
            for (const char* k : {"numAccBank", "accountNumber", "bank_number", "accNum", "accountNo"}) {
                const QString s = o.value(QLatin1String(k)).toVariant().toString().trimmed();
                if (!s.isEmpty()) { out->accountNumber = s; break; }
            }
        }
        if (out->accountHolder.isEmpty()) {
            for (const char* k : {"nameAccBank", "accountHolder", "holder_name", "accName", "ownerName"}) {
                const QString s = o.value(QLatin1String(k)).toString().trimmed();
                if (!s.isEmpty() && s != QLatin1String("---")) { out->accountHolder = s; break; }
            }
        }
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            if (it.value().isObject() || it.value().isArray()) {
                scanBankJson(it.value(), out, depth + 1);
            } else if (it.key() == QLatin1String("zinstant_data") && it.value().isString()) {
                const QJsonDocument sub = QJsonDocument::fromJson(it.value().toString().toUtf8());
                if (!sub.isNull()) scanBankJson(sub.isObject() ? QJsonValue(sub.object()) : QJsonValue(sub.array()), out, depth + 1);
            }
        }
    } else if (v.isArray()) {
        for (const QJsonValue& item : v.toArray()) scanBankJson(item, out, depth + 1);
    }
}

inline BankCardFields extractBankCardFields(const QJsonValue& root, const QString& fallbackText = QString()) {
    BankCardFields out;
    scanBankJson(root, &out);
    if (out.bankName.isEmpty() && out.binBank > 0) {
        out.bankName = ZaloClient::bankShortNameForBin(out.binBank);
    }
    if (out.accountNumber.isEmpty() && !fallbackText.isEmpty()) {
        static const QRegularExpression accRe(QStringLiteral(R"(\b\d{6,20}\b)"));
        const QRegularExpressionMatch m = accRe.match(fallbackText);
        if (m.hasMatch()) out.accountNumber = m.captured(0);
    }
    return out;
}


}
