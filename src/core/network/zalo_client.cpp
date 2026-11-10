#include "zalo_client.h"
#include <QJSEngine>
#include <QCryptographicHash>
#include "crypto/db_cross.h"
#include "sync/mobile_history_sync.h"
#include "fs/file_utils.h"
#include "media/media_utils.h"
#include "call/zcall_engine.h"
#include "offline/offline_store.h"
#include "models/text_fold.h"
#include "network/zalo_api_client.h"
#include "network/zalo_session.h"
#include "network/message_scheduler.h"
#include "platform/desktop_notifications.h"
#include "media/voice_recorder.h"
#include "platform/app_lock.h"
#include "platform/shortcut_map.h"
#include "platform/storage_manager.h"
#include "platform/system_tray.h"
#include "qrcodegen.hpp"
#include <QClipboard>
#include <QProcess>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QPointer>
#include <QDir>
#include <QNetworkReply>
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
#include <QJsonDocument>
#include <QVariantMap>
#include <QSettings>
#include <QFile>
#include <QSystemTrayIcon>
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

namespace Zalos::Core::Network {

namespace {

constexpr int kQrLifetimeSeconds = 120;
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
    {"wow", ":o", 32, "😮"},        {"cry", ":-((", 2, "😭"},      {"angry", ":-h", 20, "😡"},
    {"kiss", ":-*", 8, "😘"},       {"tears_of_joy", ":')", 7, "😂"}, {"rose", "/-rose", 120, "🌹"},
    {"broken_heart", "/-break", 65, "💔"}, {"dislike", "/-weak", 4, "👎"}, {"love", ";xx", 29, "😍"},
    {"ok", "/-ok", 68, "👌"},       {"pray", "_()_", 73, "🙏"},    {"thanks", "/-thanks", 70, "🙏"},
};

QString reactionEmoji(const QString& icon) {
    for (const auto& r : kReactions) {
        if (icon == QLatin1String(r.icon)) return QString::fromUtf8(r.emoji);
    }
    return QStringLiteral("•");
}

QString idOf(const QJsonValue& v) {
    return v.toVariant().toString();
}

QString captionOf(const QJsonObject& attach) {
    static const QRegularExpression placeholder(QStringLiteral("^\\[[^\\]]{1,24}\\]$"));
    const QString title = attach.value("title").toString().trimmed();
    return placeholder.match(title).hasMatch() ? QString() : title;
}

QString pickName(const QJsonObject& obj) {
    for (const char* key : {"displayName", "display_name", "zaloName", "zalo_name", "name"}) {
        const QString v = obj.value(key).toString();
        if (!v.isEmpty()) return v;
    }
    return {};
}

bool isOfficialAccount(const QJsonObject& user) {
    for (const char* key : {"oaInfo", "oa_status", "oaStatus"}) {
        const QJsonValue v = user.value(key);
        if (v.isObject() && !v.toObject().isEmpty()) return true;
    }
    const QString id = idOf(user.value("userId").isUndefined() ? user.value("uid") : user.value("userId"));
    if (id.startsWith('g')) return false;
    if (user.value("type").toVariant().toInt() >= 1) return true;
    const QJsonValue biz = user.value("bizPkg");
    if (biz.isObject() && biz.toObject().value("pkgId").toVariant().toInt() > 0) return true;
    return false;
}

QString parseReactionSnapshot(const QJsonObject& msg) {
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

}

QString ZaloClient::backgroundOf(const QJsonObject& obj) {
    for (const char* key : {"cover", "bgavatar", "fullAvt"}) {
        const QString v = obj.value(key).toString();
        if (!v.isEmpty()) return v;
    }
    return {};
}

ZaloClient::ZaloClient(QObject* parent)
    : QObject(parent) {
    s_instance = this;
    QObject::connect(this, &QObject::destroyed, [](QObject* gone) {
        if (static_cast<QObject*>(s_instance) == gone) s_instance = nullptr;
    });
    m_conversationModel = new Models::ConversationModel(this);
    m_conversationModel->setCategoryFilter(QString());
    m_messageModel = new Models::MessageModel(this);
    m_contactModel = new Models::ContactModel(this);
    m_callEngine = new Call::ZCallEngine(this);
    m_offlineStore = new Offline::ZaloOfflineStore(this);
    m_apiClient = new ZaloApiClient(this);

    m_userLookupTimer = new QTimer(this);
    m_userLookupTimer->setSingleShot(true);
    m_userLookupTimer->setInterval(400);
    connect(m_userLookupTimer, &QTimer::timeout, this, &ZaloClient::resolveUnknownUsers);
    m_messageModel->setSenderResolver([this](const QString& uid) {
        return qMakePair(displayNameOf(uid), m_users.value(uid).avatar);
    });
    connect(m_messageModel, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex&, int first, int last) { queueUnknownSenders(first, last); });
    connect(m_messageModel, &QAbstractItemModel::modelReset, this,
            [this]() { queueUnknownSenders(0, m_messageModel->rowCount() - 1); });

    connect(m_callEngine, &Call::ZCallEngine::callStateChanged, this, [this](int state) {
        if (state == static_cast<int>(Call::CallState::RingingIncoming)) {
            m_incomingCallActive = true;
            m_incomingCallerName = m_callEngine->currentCaller();
            emit incomingCallStateChanged(true);
        } else if (state == static_cast<int>(Call::CallState::Idle) ||
                   state == static_cast<int>(Call::CallState::Ended) ||
                   state == static_cast<int>(Call::CallState::Declined)) {
            m_incomingCallActive = false;
            emit incomingCallStateChanged(false);
        }
    });

    connect(m_apiClient, &ZaloApiClient::qrTokenGenerated, this, [this](const QString& qrUrl, const QString& token) {
        m_qrImageUrl = qrUrl;
        m_qrCodeData = token;
        m_qrLoginStep = 0;
        m_qrSecondsRemaining = kQrLifetimeSeconds;
        emit qrCodeChanged();
        emit qrTimerTick();
        emit qrLoginStepChanged();
        generateQrMatrix();
    });
    connect(m_apiClient, &ZaloApiClient::qrScanned, this, [this](const QString& displayName, const QString& avatarUrl) {
        m_qrLoginStep = 1;
        m_scannedUserName = displayName;
        m_scannedUserAvatar = avatarUrl;
        emit qrLoginStepChanged();
    });
    connect(m_apiClient, &ZaloApiClient::qrConfirmed, this, [this]() {
        m_isLoggingIn = true;
        emit loginStateChanged();
        setLoginStatus(L("Đã xác nhận trên điện thoại. Đang đăng nhập...", "Confirmed on your phone. Signing in..."), false);
    });
    connect(m_apiClient, &ZaloApiClient::qrDeclined, this, [this]() {
        setLoginStatus(L("Đăng nhập đã bị từ chối trên điện thoại.", "The sign-in was declined on your phone."), true);
        refreshQrCode();
    });
    connect(m_apiClient, &ZaloApiClient::qrExpired, this, [this]() {
        if (!m_isLoggedIn) refreshQrCode();
    });
    connect(m_apiClient, &ZaloApiClient::qrFailed, this, [this](const QString&) {
        m_qrImageUrl.clear();
        emit qrCodeChanged();
        setLoginStatus(L("Không tải được mã QR. Vui lòng kiểm tra kết nối mạng rồi bấm \"Lấy mã mới\".",
                         "Couldn't load the QR code. Check your connection and press \"Get new code\"."), true);
    });

    connect(m_apiClient, &ZaloApiClient::credentialsUnavailable, this, [this](const QString& reason) {
        m_isLoggingIn = false;
        emit loginStateChanged();
        QString msg;
        if (reason == "no_secret_key") {
            msg = L("Không đọc được khóa bảo mật của trình duyệt. Hãy mở khóa ví/khóa hệ thống rồi thử lại.",
                    "Couldn't read the browser's encryption key. Unlock your system keyring and try again.");
        } else if (reason == "no_device_id") {
            msg = L("Không tìm thấy mã thiết bị Zalo Web trong trình duyệt. Hãy mở chat.zalo.me một lần rồi thử lại.",
                    "No Zalo Web device id found in the browser. Open chat.zalo.me once and try again.");
        } else if (reason == "python_unavailable" || reason == "script_missing") {
            msg = L("Thiếu thành phần đọc phiên trình duyệt (python3).", "The browser session helper (python3) is missing.");
        } else {
            msg = L("Không tìm thấy phiên Zalo Web đang đăng nhập trong trình duyệt.",
                    "No signed-in Zalo Web session was found in your browser.");
        }
        setLoginStatus(msg, true);
    });

    ZaloSession* session = m_apiClient->session();
    connect(session, &ZaloSession::loggedIn, this, &ZaloClient::onSessionLoggedIn);
    connect(session, &ZaloSession::loginFailed, this, &ZaloClient::onSessionLoginFailed);
    connect(session, &ZaloSession::loginUnavailable, this, &ZaloClient::onSessionLoginUnavailable);
    m_restoreRetryTimer = new QTimer(this);
    m_restoreRetryTimer->setSingleShot(true);
    connect(m_restoreRetryTimer, &QTimer::timeout, this, [this]() {
        if (m_isLoggedIn || !m_restoringSession) return;
        setLoginStatus(L("Đang kết nối tới máy chủ...", "Connecting to the server..."), false);
        if (!m_apiClient->restoreSavedSession(m_appLanguage)) onSessionLoginFailed(QStringLiteral("no_credentials"));
    });
    connect(session, &ZaloSession::messagesReceived, this, &ZaloClient::handleIncomingMessages);
    connect(session, &ZaloSession::reactionsReceived, this, &ZaloClient::handleReactions);
    connect(session, &ZaloSession::typingReceived, this, &ZaloClient::handleTyping);
    connect(session, &ZaloSession::seenReceived, this, &ZaloClient::handleSeen);
    connect(session, &ZaloSession::deliveredReceived, this, &ZaloClient::handleDeliveredReceipt);
    connect(session, &ZaloSession::friendEventReceived, this, [this](const QString& act, const QJsonValue&) {
        if (act == "req_v2" || act == "undo_req" || act == "reject") {
            refreshSentFriendRequests();
        }
        refreshFriendRequests();
        refreshContacts();
    });
    connect(session, &ZaloSession::mobileSyncControlReceived, this, &ZaloClient::handleMobileSyncControl);
    m_historySync = new Sync::MobileHistorySync(session, this);
    connect(m_historySync, &Sync::MobileHistorySync::stateChanged, this,
            [this](int state, const QString& status, int progress) {
                setMobileSyncState(state, status, progress);
                if (state == Sync::MobileHistorySync::Done && !m_myUid.isEmpty()) {
                    QSettings().setValue(QStringLiteral("mobileSync/done/") + m_myUid, true);
                }
                if (state == Sync::MobileHistorySync::Done || state == Sync::MobileHistorySync::Failed) {
                    emit notice(status);
                    releaseFreedMemory();
                }
            });
    connect(m_historySync, &Sync::MobileHistorySync::messagesImported, this,
            [this](const QJsonArray& messages, bool isGroup) { handleIncomingMessages(messages, isGroup, true); });
    connect(session, &ZaloSession::readPointsReceived, this, &ZaloClient::applyReadPoints);
    connect(session, &ZaloSession::controlReceived, this, &ZaloClient::handleSyncControl);
    m_labelDebounce = new QTimer(this);
    m_labelDebounce->setSingleShot(true);
    m_labelDebounce->setInterval(1000);
    connect(m_labelDebounce, &QTimer::timeout, this, &ZaloClient::loadLabels);
    m_profileRefreshTimer = new QTimer(this);
    m_profileRefreshTimer->setInterval(15 * 60 * 1000);
    connect(m_profileRefreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_isLoggedIn) return;
        loadAccountData();
        loadAliases();
    });

    m_scheduler = new MessageScheduler(this);
    m_scheduler->setReadyCheck([this]() { return m_isLoggedIn; });
    m_scheduler->setSender([this](const QString& convId, bool isGroup, const QString& text,
                                  std::function<void(bool, const QString&)> done) {
        m_lastClientId = qMax(QDateTime::currentMSecsSinceEpoch(), m_lastClientId + 1);
        m_apiClient->session()->sendText(convId, isGroup, text,
            [this, done](bool ok, const QJsonValue&, const QString& error, int code) {
                done(ok, ok ? QString() : friendlyError(error, code));
            }, QString::number(m_lastClientId));
    });
    connect(m_scheduler, &MessageScheduler::changed, this, &ZaloClient::scheduledMessagesChanged);
    connect(m_scheduler, &MessageScheduler::sendFailed, this, [this](const QString& error) {
        emit notice(L("Tin nhắn hẹn giờ chưa gửi được: ", "A scheduled message failed: ") + error);
    });

    m_sound = new Platform::SoundEngine(this);
    connect(m_sound, &Platform::SoundEngine::playingChanged, this, [this](bool playing) {
        if (playing || !m_isPreviewPlaying || !Platform::SoundEngine::isSystemKey(m_currentPreviewSound)) return;
        if (m_previewTimer) m_previewTimer->stop();
        m_isPreviewPlaying = false;
        m_currentPreviewSound.clear();
        emit previewPlayingChanged();
    });

    m_shortcuts = new Platform::ShortcutMap(this);
    m_appLock = new Platform::AppLock(this);
    m_storage = new Platform::StorageManager(this);
    {
        GroupAdmin::Hooks hooks;
        hooks.session = [this]() { return m_isLoggedIn ? m_apiClient->session() : nullptr; };
        hooks.myRole = [this](const QString& id) { return myGroupRole(id); };
        hooks.isMember = [this](const QString& id) { return m_groupIds.contains(id); };
        hooks.notice = [this](const QString& text) { emit notice(text); };
        hooks.friendlyError = [this](const QString& e, int code) { return friendlyError(e, code); };
        hooks.tr = [this](const QString& vi, const QString& en) { return L(vi, en); };
        hooks.groupChanged = [this](const QString& id) {
            if (m_isLoggedIn) loadGroupInfo({id});
        };
        hooks.openGroup = [this](const QString& id, const QString& name, const QString& avatar) {
            selectConversation(id, name, avatar);
        };
        m_groupAdmin = new GroupAdmin(std::move(hooks), this);
    }
    m_composer = new Models::RichComposer(this);
    {
        LabelManager::Hooks hooks;
        hooks.session = [this]() { return m_isLoggedIn ? m_apiClient->session() : nullptr; };
        hooks.isGroup = [this](const QString& id) { return isGroupThread(id); };
        hooks.notice = [this](const QString& text) { emit notice(text); };
        hooks.friendlyError = [this](const QString& e, int code) { return friendlyError(e, code); };
        hooks.tr = [this](const QString& vi, const QString& en) { return L(vi, en); };
        hooks.apply = [this](const QHash<QString, QVariantMap>& map) { m_conversationModel->applyLabels(map); };
        m_labelManager = new LabelManager(std::move(hooks), this);
    }
    {
        StickerStore::Hooks hooks;
        hooks.session = [this]() { return m_isLoggedIn ? m_apiClient->session() : nullptr; };
        hooks.fetchBytes = [this](const QString& url, std::function<void(const QByteArray&, const QString&)> done) {
            fetchBytes(url, std::move(done));
        };
        hooks.send = [this](qint64 id, qint64 cateId, int type) { sendStickerItem(id, cateId, type); };
        hooks.account = [this]() { return m_myUid; };
        m_stickerStore = new StickerStore(std::move(hooks), this);
    }
    {
        PinBoard::Hooks hooks;
        hooks.session = [this]() { return m_isLoggedIn ? m_apiClient->session() : nullptr; };
        hooks.isGroup = [this](const QString& id) { return isGroupThread(id); };
        hooks.rawMessage = [this](const QString& msgId) { return rawMessage(msgId); };
        hooks.myUid = [this]() { return m_myUid; };
        hooks.myName = [this]() { return m_userName; };
        hooks.displayName = [this](const QString& uid) { return displayNameOf(uid); };
        hooks.notice = [this](const QString& text) { emit notice(text); };
        hooks.friendlyError = [this](const QString& e, int code) { return friendlyError(e, code); };
        hooks.tr = [this](const QString& vi, const QString& en) { return L(vi, en); };
        m_pinBoard = new PinBoard(std::move(hooks), this);
        m_messageSearch = new Models::MessageSearchModel([this](const QString& query) {
            QVector<Models::MessageSearchModel::Hit> out;
            if (!cacheUsable()) return out;
            for (const auto& h : m_offlineStore->searchMessages(query, QString(), 80)) {
                if (m_hiddenThreads.contains(h.convId)) continue;
                const Models::ConversationItem conv = m_conversationModel->conversation(h.convId);
                if (conv.id.isEmpty()) continue;
                out.append({h.msgId, h.convId, conv.name, conv.avatarUrl, conv.isGroup, h.senderName, h.text, h.ts});
                if (out.size() >= 50) break;
            }
            return out;
        }, this);
        m_peopleSearch = new Models::MessageSearchModel([this](const QString& query) {
            QVector<Models::MessageSearchModel::Hit> out;
            const QStringList terms = Models::foldTerms(query);
            if (terms.isEmpty()) return out;
            auto add = [&](const QString& id, const Profile& p, bool isGroup) {
                if (id.isEmpty() || p.name.isEmpty() || m_hiddenThreads.contains(id)) return;
                if (!m_conversationModel->conversation(id).id.isEmpty()) return;
                if (!Models::containsAllTerms(Models::foldForSearch(p.name), terms)) return;
                const QString sub = isGroup ? (p.memberCount > 0 ? L("Nhóm · %1 thành viên", "Group · %1 members").arg(p.memberCount)
                                                                 : L("Nhóm", "Group"))
                                            : L("Bạn bè", "Friend");
                out.append({QString(), id, p.name, p.avatar, isGroup, QString(), sub, 0});
            };
            for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it) add(it.key(), it.value(), true);
            for (const QString& uid : std::as_const(m_friendIds)) add(uid, m_users.value(uid), false);
            std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
                if (a.isGroup != b.isGroup) return a.isGroup;
                return QString::localeAwareCompare(a.convName, b.convName) < 0;
            });
            if (out.size() > 30) out.resize(30);
            return out;
        }, this, 1);
        m_hiddenSearch = new Models::MessageSearchModel([this](const QString& query) {
            QVector<Models::MessageSearchModel::Hit> out;
            if (!verifyHiddenPin(query.trimmed())) return out;
            for (const QString& id : std::as_const(m_hiddenThreads)) {
                const Models::ConversationItem conv = m_conversationModel->conversation(id);
                const bool isGroup = conv.id.isEmpty() ? isGroupThread(id) : conv.isGroup;
                QString name = conv.name;
                if (name.isEmpty()) name = isGroup ? m_groups.value(id).name : displayNameOf(id);
                if (name.isEmpty()) name = isGroup ? L("Nhóm chưa đặt tên", "Unnamed group") : L("Người dùng Zalo", "Zalo user");
                out.append({QString(), id, name, conv.avatarUrl, isGroup, QString(), conv.lastMessage, conv.lastTs});
            }
            std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.ts > b.ts; });
            return out;
        }, this, 4);
        auto syncHiddenReveal = [this]() {
            m_conversationModel->setHiddenPinReveal(verifyHiddenPin(m_conversationModel->searchQuery().trimmed()));
        };
        connect(m_conversationModel, &Models::ConversationModel::filterChanged, this, syncHiddenReveal);
        connect(this, &ZaloClient::hiddenPinChanged, this, syncHiddenReveal);
        connect(this, &ZaloClient::currentConversationChanged, this, [this]() {
            if (!m_hiddenUnlockedId.isEmpty() && m_hiddenUnlockedId != m_currentConvId) m_hiddenUnlockedId.clear();
        });
        m_hiddenRefreshTimer = new QTimer(this);
        m_hiddenRefreshTimer->setInterval(24 * 60 * 60 * 1000);
        connect(m_hiddenRefreshTimer, &QTimer::timeout, this, &ZaloClient::loadHiddenConversations);
        connect(m_pinBoard, &PinBoard::pinsChanged, this, &ZaloClient::pinnedMessageChanged);
        connect(this, &ZaloClient::currentConversationChanged, this, [this]() { m_pinBoard->setCurrent(m_currentConvId); loadReminders(); });
        connect(this, &ZaloClient::currentConversationChanged, this, &ZaloClient::composerStateChanged);
        connect(this, &ZaloClient::currentConversationChanged, this, &ZaloClient::currentConversationBackgroundChanged);
        connect(this, &ZaloClient::loginStateChanged, this, [this]() {
            if (m_isLoggedIn) m_pinBoard->reload();
        });
    }

    m_voiceRecorder = new Media::VoiceRecorder(this);
    connect(m_voiceRecorder, &Media::VoiceRecorder::recordingChanged, this, [this]() {
        if (m_voiceRecorder->recording()) m_voiceTarget = m_currentConvId;
    });
    connect(m_voiceRecorder, &Media::VoiceRecorder::finished, this, [this](const QString& path, int durationMs) {
        sendVoiceMessage(m_voiceTarget, path, durationMs);
    });
    connect(m_voiceRecorder, &Media::VoiceRecorder::failed, this, [this](const QString& reason) {
        if (reason == QLatin1String("no_microphone")) emit notice(L("Không tìm thấy micro.", "No microphone found."));
        else if (reason == QLatin1String("no_aac_encoder"))
            emit notice(L("Thiếu bộ mã hóa AAC trong Qt Multimedia nên chưa ghi âm được.",
                          "Qt Multimedia has no AAC encoder, so recording is unavailable."));
        else emit notice(L("Ghi âm thất bại: ", "Recording failed: ") + reason);
    });

    m_notifications = new Platform::DesktopNotifications(this);
    connect(m_notifications, &Platform::DesktopNotifications::actionInvoked, this, &ZaloClient::onNotificationAction);
    connect(m_notifications, &Platform::DesktopNotifications::replied, this, [this](const QString& key, const QString& text) {
        if (!key.startsWith(QLatin1String("thread:")) || m_appLock->locked()) return;
        const QString convId = key.mid(7);
        const QString clean = text.trimmed();
        if (clean.isEmpty()) return;
        sendTextTo(convId, isGroupThread(convId), clean, {}, {}, {});
        markThreadRead(convId);
    });

    m_calls = new Call::CallBridge(session, this);
    m_calls->setProfileResolver([this](const QString& uid) {
        const Profile p = m_users.value(uid);
        return qMakePair(p.name, p.avatar);
    });
    connect(session, &ZaloSession::voipControlReceived, m_calls, &Call::CallBridge::handleVoipControl);
    connect(m_calls, &Call::CallBridge::stateChanged, this, [this]() {
        const QString phase = m_calls->phase();
        if (phase == "incoming") {
            m_sound->play("ringtone", m_selectedRingtone, true);
        } else if (phase == "outgoing") {
            m_sound->play("ringtone", "classic", true);
        } else if (m_sound->isPlaying() && !m_isPreviewPlaying) {
            m_sound->stop();
        }
        if (phase != "incoming") m_notifications->close(QStringLiteral("call"));
    });
    connect(m_calls, &Call::CallBridge::incomingCall, this, [this](const QString& name) {
        notifyIncomingCallDbus(name);
    });
    connect(m_calls, &Call::CallBridge::callFinished, this, [this](const QString& peerId, bool outgoing, int seconds) {
        const QString targetId = peerId.isEmpty() ? m_currentConvId : peerId;
        if (!targetId.isEmpty()) {
            const bool isGroup = isGroupThread(targetId);
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (isGroup) {
                addSystemNotice(targetId, true, L("Cuộc gọi nhóm đã kết thúc", "Group call ended"), now);
            } else {
                Models::MessageItem item;
                item.id = QStringLiteral("local-call-%1").arg(now);
                item.messageType = "call";
                item.isMine = outgoing;
                item.senderId = outgoing ? (m_myUid.isEmpty() ? "0" : m_myUid) : targetId;
                item.senderName = outgoing ? L("Tôi", "Me") : displayNameOf(targetId);
                item.ts = now;
                item.timeStr = QDateTime::fromMSecsSinceEpoch(now).toString("HH:mm");
                const bool video = m_calls->isVideo();
                const bool missed = seconds <= 0;
                if (missed) {
                    if (outgoing) {
                        item.text = video ? L("Cuộc gọi video đi đã hủy", "Canceled outgoing video call")
                                          : L("Cuộc gọi thoại đi đã hủy", "Canceled outgoing voice call");
                    } else {
                        item.text = video ? L("Cuộc gọi video nhỡ", "Missed video call")
                                          : L("Cuộc gọi thoại nhỡ", "Missed voice call");
                    }
                } else {
                    item.text = outgoing ? (video ? L("Cuộc gọi video đi", "Outgoing video call")
                                                  : L("Cuộc gọi thoại đi", "Outgoing voice call"))
                                         : (video ? L("Cuộc gọi video đến", "Incoming video call")
                                                  : L("Cuộc gọi thoại đến", "Incoming voice call"));
                }
                QString duration;
                if (!missed && seconds > 0) {
                    duration = seconds >= 60 ? L("%1 phút %2 giây", "%1 min %2 s").arg(seconds / 60).arg(seconds % 60)
                                             : L("%1 giây", "%1 s").arg(seconds);
                }
                item.extra = {{"video", video}, {"missed", missed}, {"outgoing", outgoing}, {"seconds", seconds},
                              {"duration", duration}};
                m_messageModel->mergeMessages(targetId, {item});
                if (m_offlineStore) m_offlineStore->saveMessage(targetId, item, {}, false);
                m_conversationModel->recordMessage(targetId, "[" + item.text + "]",
                                                  outgoing ? L("Bạn: ", "You: ") : (item.senderName + ": "), now, false);
            }
        }
        if (peerId == m_currentConvId) loadOlderMessages();
    });

    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(45000);
    connect(m_presenceTimer, &QTimer::timeout, this, &ZaloClient::refreshPresence);

    m_typingTimer = new QTimer(this);
    m_typingTimer->setInterval(1000);
    connect(m_typingTimer, &QTimer::timeout, this, [this]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (auto it = m_typingUntil.begin(); it != m_typingUntil.end();) {
            if (it.value() < now) {
                m_conversationModel->setTyping(it.key(), false);
                const bool current = it.key() == m_currentConvId;
                m_typingUser.remove(it.key());
                it = m_typingUntil.erase(it);
                if (current) emit conversationStatusChanged();
            } else {
                ++it;
            }
        }
        if (m_typingUntil.isEmpty()) m_typingTimer->stop();
    });
    connect(session, &ZaloSession::realtimeStateChanged, this, [this, session](bool connected) {
        m_isConnected = connected;
        m_offlineStore->setOfflineActive(!connected);
        if (connected && m_realtimeWasUp) resyncAfterReconnect();
        if (connected) m_realtimeWasUp = true;
        if (connected && !m_brokenMedia.isEmpty()) {
            m_brokenMedia.clear();
            ++m_brokenMediaRevision;
            emit brokenMediaChanged();
        }
        emit connectionStateChanged(connected);
        if (connected) {
            setSyncing(true);
            session->requestOldMessages(false);
            session->requestOldMessages(true);
            session->requestOldReactions(false);
            session->requestOldReactions(true);
            QTimer::singleShot(8000, this, [this]() { setSyncing(false); });
        }
    });
    connect(session, &ZaloSession::realtimeReplaced, this, [this]() {
        emit notice(L("Zalo đang được mở ở một trình duyệt hoặc máy tính khác nên kết nối tại đây đã tạm dừng.",
                      "Zalo was opened in another browser or computer, so this connection was paused."));
    });

    loadSession();

    m_qrTimer = new QTimer(this);
    connect(m_qrTimer, &QTimer::timeout, this, [this]() {
        if (m_isLoggedIn || m_qrImageUrl.isEmpty()) return;
        if (m_qrSecondsRemaining > 0) {
            m_qrSecondsRemaining--;
            emit qrTimerTick();
        } else {
            refreshQrCode();
        }
    });
    m_qrTimer->start(1000);

    m_apiClient->setLanguage(m_appLanguage);
    if (m_apiClient->restoreSavedSession(m_appLanguage)) {
        m_restoringSession = true;
        m_isLoggingIn = true;
        m_loginStatusMessage = L("Đang kết nối tới máy chủ...", "Connecting to the server...");
        if (loadOfflineCache()) {
            m_isOfflineMode = true;
            emit offlineModeChanged();
        }
        emit restoringSessionChanged();
    } else {
        m_apiClient->generateQrToken();
    }
}

QString ZaloClient::friendlyError(const QString& error, int code) const {
    switch (code) {
    case 600:
    case -102: return L("phiên đăng nhập đã hết hạn", "your session has expired");
    case 114: return L("tham số không hợp lệ", "invalid parameters");
    case 429: return L("thao tác quá nhanh, hãy thử lại sau ít phút", "too many requests, try again in a few minutes");
    case -69: return L("máy chủ yêu cầu ngừng thử lại (dịch vụ không dành cho phiên đăng nhập này)",
                       "the server said to stop retrying (the service is not offered to this session)");
    case 18022: return L("chữ ký yêu cầu không hợp lệ", "the request signature was rejected");
    case 18060: return L("máy chủ yêu cầu tham số được mã hoá", "the server requires encrypted parameters");
    case 126: return L("Tài khoản đã ngừng hoạt động.", "This account is no longer active.");
    case 164: return L("Bạn không còn là thành viên trong nhóm này", "You are no longer a member of this group");
    case 166: return L("Chỉ có trưởng/phó nhóm thực hiện được tính năng này.", "Only the group owner or deputies can do this.");
    case 204: return L("Hiện tại Zalo chưa hỗ trợ chia sẻ tập tin định dạng này.", "Zalo doesn't support sharing this file type yet.");
    case 211: return L("Phiên bản Zalo bên người nhận chưa hỗ trợ thu hồi tin nhắn",
                       "The recipient's Zalo version doesn't support recalling messages");
    case 216: return L("Danh tính người dùng này không hợp lệ.", "This user's identity is invalid.");
    case 301: return L("Bạn đã thu hồi quá số lần cho phép. Vui lòng thử lại sau 24 giờ.",
                       "You've recalled too many times. Please try again in 24 hours.");
    case 120: return L("Zalo: Hiện tại bạn không thể nhắn tin cho người lạ.", "Zalo: You can't message strangers right now.");
    case 121: return L("Zalo: Tin nhắn có nội dung không phù hợp để gửi cho người lạ. Vui lòng kiểm tra lại.",
                       "Zalo: This message isn't appropriate to send to a stranger. Please check it.");
    case 122: return L("Bạn chưa thể gửi tin nhắn đến người này vì người này chặn không nhận tin nhắn từ người lạ.",
                       "You can't message this person yet because they don't accept messages from strangers.");
    case 206: return L("Bạn không thể gửi File cho người lạ.", "You can't send files to strangers.");
    case 804: return L("Bạn đã đạt giới hạn trả lời người lạ trong tháng. Để tránh rủi ro bị lừa đảo, bạn tạm thời sẽ "
                       "không thể trả lời người này.",
                       "You've reached this month's limit for replying to strangers. To avoid scams, you temporarily "
                       "can't reply to this person.");
    default: break;
    }
    if (error == "not_logged_in") return L("bạn chưa đăng nhập", "you're not signed in");
    if (error == "service_unavailable")
        return L("máy chủ Zalo chưa cung cấp dịch vụ này cho phiên đăng nhập hiện tại",
                 "Zalo's servers don't offer this service to the current session");
    if (error == "decrypt_failed") return L("phản hồi từ máy chủ không hợp lệ", "the server reply was invalid");
    if (error.isEmpty()) return L("lỗi không xác định", "unknown error");
    if (error.contains("Host") || error.contains("network", Qt::CaseInsensitive) || error.contains("timed out"))
        return L("lỗi kết nối mạng", "network error");
    return error;
}

void ZaloClient::setLoginStatus(const QString& message, bool isError) {
    m_loginStatusMessage = message;
    m_hasLoginError = isError;
    emit loginStatusChanged();
}

void ZaloClient::setSyncing(bool syncing) {
    if (m_syncing == syncing) return;
    m_syncing = syncing;
    emit syncStateChanged();
}

void ZaloClient::loadSession() {
    QSettings settings("Zalos", "ZalosApp");
    m_userName = settings.value("auth/userName", "").toString();
    m_userPhone = settings.value("auth/userPhone", "").toString();
    m_userAvatar = settings.value("auth/userAvatar", "").toString();
    m_cachedUid = settings.value("auth/uid", "").toString();
    m_isLoggedIn = false;

    m_appLanguage = settings.value("settings/language", "vi").toString();

    m_notifyNewMessage = settings.value("settings/notifyNewMessage", true).toBool();
    m_notifySound = settings.value("settings/notifySound", false).toBool();
    if (settings.contains(QLatin1String("settings/chatBackgroundMode"))) {
        const QString mode = settings.value(QLatin1String("settings/chatBackgroundMode")).toString();
        if (mode == QLatin1String("avatar") || mode == QLatin1String("custom") || mode == QLatin1String("none")) {
            m_chatBackgroundMode = mode;
        } else {
            m_chatBackgroundMode = QStringLiteral("none");
        }
    } else if (settings.contains(QLatin1String("settings/useAvatarAsChatBackground"))) {
        const bool legacyAvatarBackground = settings.value(QLatin1String("settings/useAvatarAsChatBackground"), false).toBool();
        m_chatBackgroundMode = legacyAvatarBackground ? QStringLiteral("avatar") : QStringLiteral("none");
    } else {
        m_chatBackgroundMode = QStringLiteral("none");
    }
    settings.remove(QLatin1String("settings/useAvatarAsChatBackground"));
    m_chatBackgroundOpacity = qBound(0.05, settings.value("settings/chatBackgroundOpacity", 0.15).toReal(), 0.80);
    m_chatBackgroundImage = settings.value("settings/chatBackgroundImage").toString();
    if (!m_chatBackgroundImage.isEmpty() && !QFileInfo::exists(m_chatBackgroundImage)) m_chatBackgroundImage.clear();
    if (m_chatBackgroundMode == QLatin1String("custom") && m_chatBackgroundImage.isEmpty())
        m_chatBackgroundMode = QStringLiteral("none");
    m_showLabelFolders = settings.value("settings/showLabelFolders", false).toBool();
    m_hiddenDismissedAt = settings.value("settings/hiddenUnreadDismissedAt", 0).toLongLong();
    m_startHiddenInTray = settings.value("settings/startHiddenInTray", false).toBool();
    m_closeToTray = settings.value("settings/closeToTray", true).toBool();
    m_antiPeeking = settings.value("settings/antiPeeking", false).toBool();

    m_blockTyping = settings.value("privacy/blockTyping", false).toBool();
    m_blockDelivered = settings.value("privacy/blockDelivered", false).toBool();
    m_blockSeen = settings.value("privacy/blockSeen", false).toBool();

    m_selectedRingtone = settings.value("sound/selectedRingtone", "classic").toString();
    m_selectedNotiSound = settings.value("sound/selectedNotiSound", "pop").toString();
    if (!Platform::SoundEngine::isValidKey("ringtone", m_selectedRingtone)) m_selectedRingtone = "classic";
    if (!Platform::SoundEngine::isValidKey("notification", m_selectedNotiSound)) m_selectedNotiSound = "pop";

    m_currentConvId.clear();
    m_currentConvName.clear();
}

void ZaloClient::saveSession() {
    QSettings settings("Zalos", "ZalosApp");
    settings.setValue("auth/isLoggedIn", m_isLoggedIn);
    settings.setValue("auth/userName", m_userName);
    settings.setValue("auth/userPhone", m_userPhone);
    settings.setValue("auth/userAvatar", m_userAvatar);
    settings.setValue("auth/uid", m_isLoggedIn ? m_myUid : m_cachedUid);

    settings.setValue("settings/notifyNewMessage", m_notifyNewMessage);
    settings.setValue("settings/notifySound", m_notifySound);
    settings.setValue("settings/antiPeeking", m_antiPeeking);
    settings.setValue("settings/chatBackgroundMode", m_chatBackgroundMode);
    settings.setValue("settings/chatBackgroundOpacity", m_chatBackgroundOpacity);
    settings.remove(QLatin1String("settings/useAvatarAsChatBackground"));
    settings.setValue("settings/chatBackgroundImage", m_chatBackgroundImage);
    settings.setValue("settings/showLabelFolders", m_showLabelFolders);
    settings.setValue("settings/startHiddenInTray", m_startHiddenInTray);
    settings.setValue("settings/closeToTray", m_closeToTray);
    settings.setValue("settings/language", m_appLanguage);

    settings.setValue("privacy/blockTyping", m_blockTyping);
    settings.setValue("privacy/blockDelivered", m_blockDelivered);
    settings.setValue("privacy/blockSeen", m_blockSeen);

    settings.setValue("sound/selectedRingtone", m_selectedRingtone);
    settings.setValue("sound/selectedNotiSound", m_selectedNotiSound);
}

void ZaloClient::loginWithPhone(const QString& phone, const QString& password, bool remember) {
    Q_UNUSED(remember);
    if (phone.trimmed().isEmpty() || password.isEmpty()) {
        setLoginStatus(L("Vui lòng nhập số điện thoại và mật khẩu.", "Please enter your phone number and password."), true);
        return;
    }
    setLoginStatus(L("Zalo Web hiện không cho phép đăng nhập bằng mật khẩu từ ứng dụng bên ngoài. "
                     "Vui lòng quét mã QR hoặc dùng phiên trình duyệt.",
                     "Zalo Web doesn't allow password sign-in from third-party apps. "
                     "Please scan the QR code or use your browser session."),
                   true);
}

void ZaloClient::syncFromBrowserSession() {
    m_isLoggingIn = true;
    emit loginStateChanged();
    setLoginStatus(L("Đang đọc phiên đăng nhập từ trình duyệt...", "Reading the sign-in session from your browser..."), false);
    m_apiClient->syncFromBrowserSession();
}

void ZaloClient::cancelQrScan() {
    m_qrLoginStep = 0;
    m_scannedUserName.clear();
    m_scannedUserAvatar.clear();
    emit qrLoginStepChanged();
    refreshQrCode();
}

void ZaloClient::clearLoginError() {
    if (!m_hasLoginError && m_loginStatusMessage.isEmpty()) return;
    m_hasLoginError = false;
    m_loginStatusMessage.clear();
    emit loginStatusChanged();
}

void ZaloClient::refreshQrCode() {
    m_qrSecondsRemaining = kQrLifetimeSeconds;
    m_qrLoginStep = 0;
    m_scannedUserName.clear();
    m_scannedUserAvatar.clear();
    m_qrImageUrl.clear();
    m_qrCodeData.clear();
    generateQrMatrix();
    emit qrCodeChanged();
    emit qrTimerTick();
    emit qrLoginStepChanged();
    m_apiClient->generateQrToken();
}

void ZaloClient::generateQrMatrix() {
    m_qrSize = 0;
    m_qrMatrix.clear();
    if (!m_qrCodeData.isEmpty()) {
        try {
            using namespace qrcodegen;
            QrCode qr = QrCode::encodeText(m_qrCodeData.toUtf8().constData(), QrCode::Ecc::MEDIUM);
            m_qrSize = qr.getSize();
            m_qrMatrix.reserve(m_qrSize * m_qrSize);
            for (int y = 0; y < m_qrSize; ++y) {
                for (int x = 0; x < m_qrSize; ++x) {
                    m_qrMatrix.append(qr.getModule(x, y));
                }
            }
        } catch (...) {
            m_qrSize = 0;
            m_qrMatrix.clear();
        }
    }
    emit qrMatrixChanged();
}

void ZaloClient::onSessionLoggedIn() {
    ZaloSession* session = m_apiClient->session();
    m_restoreAttempts = 0;
    m_restoringSession = false;
    emit restoringSessionChanged();
    m_myUid = session->uid();
    loadDrafts();
    pumpStickerQueue();
    QTimer::singleShot(1500, this, &ZaloClient::maybeOfferMobileSync);
    if (m_offlineStore && m_offlineStore->bindOwner(m_myUid)) {
        m_conversationModel->clearAll();
        m_messageModel->clearAll();
        m_rawMessages.clear();
        m_messageThread.clear();
        m_cacheMerged.clear();
    }
    m_cachedUid = m_myUid;
    m_isLoggedIn = true;
    m_isLoggingIn = false;
    const bool wasOffline = m_isOfflineMode;
    m_isOfflineMode = false;
    if (wasOffline) emit offlineModeChanged();
    m_hasLoginError = false;
    m_loginStatusMessage = L("Đăng nhập thành công!", "Signed in successfully!");
    m_apiClient->cancelQrLogin();
    session->saveCredentials(ZaloApiClient::credentialsPath());
    saveSession();

    emit loginStateChanged();
    emit loginStatusChanged();
    emit userProfileChanged();
    emit kikiStateChanged();

    m_calls->setLocalUser(m_myUid, m_userName, m_userAvatar);
    m_scheduler->setAccount(m_myUid);
    loadAccountData();
    loadAliases();
    loadUnreadMarks();
    loadQuickMessages();
    loadServerSettings();
    loadHiddenConversations();
    loadReadOnlyThreads();
    loadArchivedChats();
    loadAutoDeleteSettings();
    m_profileRefreshTimer->start();
    preloadRecentThreads();
    loadLabels();
    refreshPresence();
    refreshFriendRequests();
    m_presenceTimer->start();
    session->connectRealtime();
}

void ZaloClient::onSessionLoginUnavailable(const QString& reason) {
    Q_UNUSED(reason);
    if (!m_restoringSession) {
        m_isLoggingIn = false;
        emit loginStateChanged();
        setLoginStatus(L("Không kết nối được tới máy chủ Zalo. Kiểm tra mạng và thử lại.",
                         "Couldn't reach Zalo's servers. Check your connection and try again."), true);
        return;
    }
    const int delay = qMin(60, 5 << qMin(m_restoreAttempts, 4));
    ++m_restoreAttempts;
    setLoginStatus(L("Chưa kết nối được máy chủ. Thử lại sau %1 giây...", "Can't reach the server. Retrying in %1 s...")
                       .arg(delay), !m_isOfflineMode);
    m_restoreRetryTimer->start(delay * 1000);
}

void ZaloClient::onSessionLoginFailed(const QString& reason) {
    Q_UNUSED(reason);
    m_restoreRetryTimer->stop();
    m_restoreAttempts = 0;
    if (m_isOfflineMode) {
        m_isOfflineMode = false;
        m_conversationModel->clearAll();
        m_messageModel->clearAll();
        m_rawMessages.clear();
        m_messageThread.clear();
        m_cacheMerged.clear();
        openWelcomeScreen();
        emit offlineModeChanged();
    }
    const bool wasRestoring = m_restoringSession;
    m_restoringSession = false;
    emit restoringSessionChanged();
    m_isLoggingIn = false;
    emit loginStateChanged();
    if (wasRestoring) {
        m_apiClient->forgetSession();
        setLoginStatus(L("Phiên đăng nhập đã hết hạn. Vui lòng đăng nhập lại.",
                         "Your session has expired. Please sign in again."), true);
    } else {
        setLoginStatus(L("Không thể xác thực phiên đăng nhập với máy chủ Zalo.",
                         "Couldn't verify the session with Zalo's servers."), true);
    }
    refreshQrCode();
}

void ZaloClient::logout() {
    m_groupAdmin->reset();
    m_pinBoard->reset();
    m_labelManager->reset();
    m_compositeLookups.clear();
    m_compositeAsked.clear();
    if (m_showSyncPrompt) {
        m_showSyncPrompt = false;
        emit syncPromptChanged();
    }
    m_restoreRetryTimer->stop();
    m_restoreAttempts = 0;
    if (m_offlineStore) m_offlineStore->clearCache();
    m_cachedUid.clear();
    m_cacheMerged.clear();
    m_residentThreads.clear();
    m_recentIds.clear();
    m_recentIdOrder.clear();
    m_calls->shutdown();
    m_apiClient->forgetSession();
    m_isLoggedIn = false;
    m_restoringSession = false;
    emit restoringSessionChanged();
    m_isOfflineMode = false;
    m_isConnected = false;
    m_hasLoginError = false;
    m_qrLoginStep = 0;
    m_userName.clear();
    m_userPhone.clear();
    m_userAvatar.clear();
    m_myUid.clear();
    m_hasBusinessPackage = false;
    m_businessPackageName.clear();
    m_loginStatusMessage.clear();
    m_showSettingsModal = false;
    m_showUserProfileModal = false;
    m_currentConvId.clear();
    m_currentConvName.clear();
    m_currentConvAvatar.clear();
    m_users.clear();
    m_groups.clear();
    m_friendIds.clear();
    m_groupIds.clear();
    m_pendingUserLookups.clear();
    m_mutedIds.clear();
    m_muteUntil.clear();
    m_markedUnread.clear();
    m_hiddenThreads.clear();
    m_hiddenPinHash.clear();
    m_hiddenUnlockedId.clear();
    m_hiddenUnread = false;
    if (m_hiddenRefreshTimer) m_hiddenRefreshTimer->stop();
    m_aliases.clear();
    m_realtimeWasUp = false;
    if (m_profileRefreshTimer) m_profileRefreshTimer->stop();
    m_historyExhausted.clear();
    m_replyTarget.clear();
    m_scheduler->clear();
    m_notifications->closeAll();
    m_conversationModel->clearDrafts();
    m_historyLoaded.clear();
    m_rawMessages.clear();
    m_messageThread.clear();
    m_reactions.clear();
    m_reactedIds.clear();
    m_reactedIdsLoaded = false;
    m_onlineIds.clear();
    m_seenSent.clear();
    m_typingUntil.clear();
    m_friendRequests.clear();
    m_currentLastOnline = 0;
    m_presenceTimer->stop();
    m_conversationModel->clearAll();
    m_messageModel->clearAll();
    m_contactModel->clearAll();
    saveSession();
    refreshQrCode();
    emit loginStateChanged();
    emit userProfileChanged();
    emit currentConversationChanged();
    emit connectionStateChanged(false);
    emit settingsModalChanged();
    emit userProfileModalChanged();
}

void ZaloClient::loadAccountData() {
    ZaloSession* session = m_apiClient->session();
    setSyncing(true);

    if (m_conversationModel->rowCount() == 0) loadOfflineCache();

    m_pendingSyncJobs = 4;
    auto finishJob = [this]() {
        if (--m_pendingSyncJobs <= 0) setSyncing(false);
    };

    session->fetchAccountInfo([this, finishJob](bool ok, const QJsonValue& data, const QString&, int) {
        if (ok) {
            const QJsonObject profile = data.toObject().value("profile").toObject();
            const QString name = pickName(profile);
            if (!name.isEmpty()) m_userName = name;
            m_userAvatar = profile.value("avatar").toString(m_userAvatar);
            m_calls->setLocalUser(m_myUid, m_userName, m_userAvatar);
            const QString phone = profile.value("phoneNumber").toString();
            if (!phone.isEmpty()) m_userPhone = phone;

            m_myProfile.userId = m_myUid;
            m_myProfile.displayName = m_userName;
            m_myProfile.zaloName = profile.value("zaloName").toString(m_userName);
            m_myProfile.avatar = m_userAvatar;
            m_myProfile.cover = backgroundOf(profile);
            m_myProfile.gender = profile.value("gender").toInt(0);
            m_myProfile.dob = profile.value("dob").toVariant().toLongLong();
            m_myProfile.sdob = profile.value("sdob").toString();
            m_myProfile.status = profile.value("status").toString();
            m_myProfile.phoneNumber = m_userPhone;
            m_myProfile.isFriend = false;
            m_myProfile.isBlocked = false;

            const QJsonValue bizVal = profile.value("bizPkg");
            if (bizVal.isObject()) {
                const QJsonObject bizObj = bizVal.toObject();
                const int pkgId = bizObj.value("pkgId").toVariant().toInt();
                m_hasBusinessPackage = (pkgId > 0);
                m_businessPackageName = bizObj.value("label").toString(bizObj.value("name").toString());
            } else {
                m_hasBusinessPackage = false;
                m_businessPackageName.clear();
            }

            saveSession();
            emit userProfileChanged();
            emit currentProfileChanged();
        }
        finishJob();
    });

    session->fetchFriends([this, finishJob](bool ok, const QJsonValue& data, const QString&, int) {
        if (ok) {
            m_friendsKnown = true;
            for (const QJsonValue& v : data.toArray()) {
                const QJsonObject u = v.toObject();
                const QString uid = idOf(u.value("userId"));
                if (uid.isEmpty()) continue;
                m_friendIds.insert(uid);
                m_users[uid] = {pickName(u), u.value("avatar").toString(), 0};
                noteAccountFlags(uid, u);
                applyProfileToConversation(uid);
                persistProfile(uid, false);
            }
            rebuildContacts();
        }
        finishJob();
    });

    session->fetchGroupIds([this, finishJob](bool ok, const QJsonValue& data, const QString&, int) {
        if (ok) {
            QStringList ids = data.toObject().value("gridVerMap").toObject().keys();
            for (const QString& id : ids) m_groupIds.insert(id);
            loadGroupInfo(ids);
        }
        finishJob();
    });

    session->fetchPinnedConversations([this, finishJob](bool ok, const QJsonValue& data, const QString&, int) {
        if (ok) {
            for (const QJsonValue& v : data.toObject().value("conversations").toArray()) {
                QString raw = v.toString();
                const bool isGroup = raw.startsWith('g');
                if (raw.startsWith('g') || raw.startsWith('u')) raw.remove(0, 1);
                ensureConversation(raw, isGroup);
                m_conversationModel->setPinned(raw, true);
            }
        }
        finishJob();
    });

    session->apiGet("profile", "/api/social/profile/getmute", {{"imei", session->imei()}},
                    [this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        const QJsonObject obj = data.toObject();
        qint64 nowSec = obj.value("systemTime").toVariant().toLongLong();
        if (nowSec > 100000000000LL) nowSec /= 1000;
        if (nowSec <= 0) nowSec = QDateTime::currentSecsSinceEpoch();
        QSet<QString> nextMuted;
        QHash<QString, qint64> nextUntil;
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
                nextMuted.insert(id);
                nextUntil.insert(id, !infinite && duration > 0 ? (start + duration) * 1000 : 0);
                m_conversationModel->setMuted(id, true);
            }
        }
        for (const QString& oldId : std::as_const(m_mutedIds)) {
            if (!nextMuted.contains(oldId)) m_conversationModel->setMuted(oldId, false);
        }
        m_mutedIds = std::move(nextMuted);
        m_muteUntil = std::move(nextUntil);
        emit currentConversationChanged();
    });
}

void ZaloClient::loadGroupInfo(const QStringList& groupIds) {
    for (int i = 0; i < groupIds.size(); i += kGroupInfoBatch) {
        const QStringList batch = groupIds.mid(i, kGroupInfoBatch);
        m_apiClient->session()->fetchGroupInfo(batch, [this](bool ok, const QJsonValue& data, const QString&, int) {
            if (ok) applyGroupInfo(data.toObject().value("gridInfoMap").toObject());
        });
    }
}

void ZaloClient::applyGroupInfo(const QJsonObject& map) {
    for (auto it = map.begin(); it != map.end(); ++it) {
        const QJsonObject g = it.value().toObject();
        QString avatar = g.value("avt").toString();
        if (avatar.isEmpty()) avatar = g.value("fullAvt").toString();
        m_groups[it.key()] = {g.value("name").toString(), avatar, g.value("totalMember").toInt()};
        m_groupIds.insert(it.key());
        m_groupAdmin->rememberSettings(it.key(), g.value("setting").toObject());
        if (it.key() == m_currentConvId) emit composerStateChanged();
        if (m_conversationModel->contains(it.key())) applyProfileToConversation(it.key());
        persistProfile(it.key(), true);

        GroupRoster roster;
        for (const QJsonValue& v : g.value("memVerList").toArray()) {
            QString id = v.toString();
            const int sep = id.lastIndexOf('_');
            if (sep > 0) id.truncate(sep);
            if (!id.isEmpty()) roster.members << id;
        }
        if (roster.members.isEmpty()) {
            for (const QJsonValue& v : g.value("memberIds").toArray()) roster.members << idOf(v);
        }
        for (const QJsonValue& v : g.value("adminIds").toArray()) roster.admins.insert(idOf(v));
        roster.creator = idOf(g.value("creatorId"));
        if (!roster.creator.isEmpty() && !roster.members.contains(roster.creator)) roster.members.prepend(roster.creator);
        if (!roster.members.isEmpty()) {
            m_groupRosters.insert(it.key(), roster);
            emit groupMembersChanged(it.key());
        }
        updateGroupComposite(it.key());
    }
    rebuildContacts();
}

void ZaloClient::updateGroupComposite(const QString& groupId) {
    if (!m_groups.value(groupId).avatar.isEmpty()) {
        m_conversationModel->setMemberAvatars(groupId, {});
        return;
    }
    QVariantList members;
    bool missing = false;
    for (const QString& id : m_groupRosters.value(groupId).members) {
        const bool isMe = !m_myUid.isEmpty() && id == m_myUid;
        const QString avatar = isMe ? m_userAvatar : m_users.value(id).avatar;
        const QString name = isMe ? m_userName : displayNameOf(id);
        if (avatar.isEmpty() && name.isEmpty() && !isMe && !m_compositeAsked.contains(id)) {
            m_compositeLookups.insert(id);
            missing = true;
        }
        members.append(QVariantMap{{"avatar", avatar}, {"name", name}});
        if (members.size() >= 4) break;
    }
    m_conversationModel->setMemberAvatars(groupId, members);
    if (missing) QTimer::singleShot(400, this, &ZaloClient::lookupCompositeMembers);
}

void ZaloClient::lookupCompositeMembers() {
    if (!m_isLoggedIn || m_compositeLookups.isEmpty()) return;
    QStringList batch;
    for (auto it = m_compositeLookups.begin(); it != m_compositeLookups.end() && batch.size() < 100;) {
        batch << *it;
        m_compositeAsked.insert(*it);
        it = m_compositeLookups.erase(it);
    }
    m_apiClient->session()->fetchGroupMembersInfo(batch, [this](bool ok, const QJsonValue& data, const QString&, int) {
        if (ok) {
            const QJsonObject profiles = data.toObject().value("profiles").toObject();
            for (auto it = profiles.begin(); it != profiles.end(); ++it) {
                QString uid = it.key();
                if (uid.endsWith("_0")) uid.chop(2);
                const QJsonObject p = it.value().toObject();
                const QString name = pickName(p);
                if (name.isEmpty() || m_users.contains(uid)) continue;
                m_users[uid] = {name, p.value("avatar").toString(), 0};
                noteAccountFlags(uid, p);
                persistProfile(uid, false);
            }
            for (auto g = m_groupRosters.cbegin(); g != m_groupRosters.cend(); ++g) {
                if (m_groups.value(g.key()).avatar.isEmpty()) updateGroupComposite(g.key());
            }
            m_messageModel->refreshSenders();
        }
        if (!m_compositeLookups.isEmpty()) QTimer::singleShot(200, this, &ZaloClient::lookupCompositeMembers);
    });
}

QVariantList ZaloClient::groupMembers(const QString& groupId) {
    if (groupId.isEmpty()) return {};
    if (!m_groupRosters.contains(groupId)) {
        if (m_isLoggedIn) loadGroupInfo({groupId});
        return {};
    }
    const GroupRoster& roster = m_groupRosters[groupId];
    QVariantList owner, admins, members;
    bool missing = false;
    for (const QString& id : roster.members) {
        const bool isMe = !m_myUid.isEmpty() && id == m_myUid;
        const QString name = isMe ? m_userName : displayNameOf(id);
        if (name.isEmpty() && !isMe) missing = true;
        const QString role = id == roster.creator ? QStringLiteral("owner")
                             : roster.admins.contains(id) ? QStringLiteral("admin") : QStringLiteral("member");
        const QVariantMap entry{{"id", id}, {"name", name}, {"avatar", isMe ? m_userAvatar : m_users.value(id).avatar},
                                {"role", role}, {"isMe", isMe}};
        (role == "owner" ? owner : role == "admin" ? admins : members).append(entry);
    }
    if (missing) resolveGroupMembers(groupId);
    return owner + admins + members;
}

QString ZaloClient::myGroupRole(const QString& groupId) const {
    const auto it = m_groupRosters.constFind(groupId);
    if (it == m_groupRosters.cend() || m_myUid.isEmpty()) return {};
    if (it->creator == m_myUid) return QStringLiteral("owner");
    if (it->admins.contains(m_myUid)) return QStringLiteral("admin");
    return it->members.contains(m_myUid) ? QStringLiteral("member") : QString();
}

void ZaloClient::resolveGroupMembers(const QString& groupId) {
    if (!m_isLoggedIn || m_memberLookupInFlight.contains(groupId)) return;
    QStringList unknown;
    for (const QString& id : m_groupRosters.value(groupId).members) {
        if (id != m_myUid && displayNameOf(id).isEmpty() && !m_unresolvedMembers.contains(id)) unknown << id;
        if (unknown.size() >= 100) break;
    }
    if (unknown.isEmpty()) return;
    m_memberLookupInFlight.insert(groupId);
    m_apiClient->session()->fetchGroupMembersInfo(unknown, [this, groupId, unknown](bool ok, const QJsonValue& data,
                                                                                    const QString&, int) {
        m_memberLookupInFlight.remove(groupId);
        if (!ok) return;
        const QJsonObject profiles = data.toObject().value("profiles").toObject();
        for (auto it = profiles.begin(); it != profiles.end(); ++it) {
            QString uid = it.key();
            if (uid.endsWith("_0")) uid.chop(2);
            const QJsonObject p = it.value().toObject();
            const QString name = pickName(p);
            if (name.isEmpty()) continue;
            m_users[uid] = {name, p.value("avatar").toString(), 0};
            noteAccountFlags(uid, p);
            persistProfile(uid, false);
            if (m_conversationModel->contains(uid)) applyProfileToConversation(uid);
        }
        for (const QString& id : unknown) {
            if (displayNameOf(id).isEmpty()) m_unresolvedMembers.insert(id);
        }
        updateGroupComposite(groupId);
        m_messageModel->refreshSenders();
        emit groupMembersChanged(groupId);
    });
}

void ZaloClient::setGroupDeputy(const QString& groupId, const QString& memberId, bool add) {
    if (!m_isLoggedIn || groupId.isEmpty() || memberId.isEmpty()) return;
    m_apiClient->session()->setGroupDeputy(groupId, memberId, add,
        [this, groupId, memberId, add](bool ok, const QJsonValue&, const QString& err, int code) {
            if (!ok) {
                emit notice((add ? L("Không thêm được phó nhóm: ", "Could not add deputy: ")
                                 : L("Không gỡ được phó nhóm: ", "Could not remove deputy: ")) + friendlyError(err, code));
                return;
            }
            if (add) m_groupRosters[groupId].admins.insert(memberId);
            else m_groupRosters[groupId].admins.remove(memberId);
            emit groupMembersChanged(groupId);
            emit notice(add ? L("Đã thêm phó nhóm", "Deputy added") : L("Đã gỡ quyền phó nhóm", "Deputy removed"));
        });
}

void ZaloClient::transferGroupOwner(const QString& groupId, const QString& memberId) {
    if (!m_isLoggedIn || groupId.isEmpty() || memberId.isEmpty()) return;
    m_apiClient->session()->changeGroupOwner(groupId, memberId,
        [this, groupId, memberId](bool ok, const QJsonValue&, const QString& err, int code) {
            if (!ok) {
                emit notice(L("Không chuyển được quyền trưởng nhóm: ", "Could not transfer ownership: ")
                            + friendlyError(err, code));
                return;
            }
            m_groupRosters[groupId].creator = memberId;
            m_groupRosters[groupId].admins.remove(memberId);
            emit groupMembersChanged(groupId);
            loadGroupInfo({groupId});
            emit notice(L("Đã chuyển quyền trưởng nhóm", "Ownership transferred"));
        });
}

void ZaloClient::disperseGroup(const QString& groupId) {
    if (!m_isLoggedIn || groupId.isEmpty()) return;
    m_apiClient->session()->disperseGroup(groupId, [this, groupId](bool ok, const QJsonValue&, const QString& err, int code) {
        if (!ok) {
            emit notice(L("Không giải tán được nhóm: ", "Could not disband the group: ") + friendlyError(err, code));
            return;
        }
        if (groupId == m_currentConvId) openWelcomeScreen();
        m_conversationModel->removeConversation(groupId);
        m_groupIds.remove(groupId);
        m_groups.remove(groupId);
        m_groupRosters.remove(groupId);
        if (m_offlineStore) m_offlineStore->deleteConversation(groupId);
        rebuildContacts();
        emit notice(L("Đã giải tán nhóm", "Group disbanded"));
    });
}

void ZaloClient::openLink(const QString& url, const QString& title, const QString& thumb) {
    if (url.isEmpty()) return;
    if (GroupAdmin::isGroupLink(url)) {
        m_groupAdmin->openJoin(url);
        return;
    }
    if (isAppOnlyLink(url)) {
        emit appOnlyLinkRequested(url, title, thumb);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromUserInput(url));
}

bool ZaloClient::isAppOnlyLink(const QString& url) {
    const QUrl u = QUrl::fromUserInput(url);
    const QString host = u.host().toLower();
    if (host == QLatin1String("zalo.video") || host.endsWith(QLatin1String(".zalo.video"))) return true;
    if (host != QLatin1String("zalo.me") && host != QLatin1String("www.zalo.me")) return false;
    const QString path = u.path();
    return path.startsWith(QLatin1String("/v/")) || path.startsWith(QLatin1String("/s/"));
}

QVariantMap ZaloClient::qrModules(const QString& text) const {
    QVariantMap out{{"size", 0}, {"modules", QVariantList{}}};
    if (text.isEmpty()) return out;
    try {
        using namespace qrcodegen;
        const QrCode qr = QrCode::encodeText(text.toUtf8().constData(), QrCode::Ecc::MEDIUM);
        const int size = qr.getSize();
        QVariantList modules;
        modules.reserve(size * size);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) modules.append(qr.getModule(x, y));
        out["size"] = size;
        out["modules"] = modules;
    } catch (...) {
    }
    return out;
}

QVariantList ZaloClient::groupsForInvite(const QString& userId) const {
    QVariantList out;
    for (const QString& id : m_groupIds) {
        const Profile p = m_groups.value(id);
        if (p.name.isEmpty()) continue;
        const auto roster = m_groupRosters.constFind(id);
        const bool isMember = roster != m_groupRosters.cend() && roster->members.contains(userId);
        out.append(QVariantMap{{"id", id}, {"name", p.name}, {"avatar", p.avatar}, {"isMember", isMember}});
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return QString::localeAwareCompare(a.toMap().value("name").toString(), b.toMap().value("name").toString()) < 0;
    });
    return out;
}

void ZaloClient::refreshPoll(qint64 pollId, bool force) {
    if (!m_isLoggedIn || pollId <= 0) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && now - m_pollFetchedAt.value(pollId) < 30000) return;
    m_pollFetchedAt.insert(pollId, now);
    m_apiClient->session()->fetchPollDetail(pollId, [this, pollId](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        const QJsonObject d = data.toObject();
        QVariantMap poll{
            {"poll_id", pollId},
            {"question", d.value("question").toString()},
            {"closed", d.value("closed").toBool()},
            {"num_vote", d.value("num_vote").toInt()},
            {"allow_multi_choices", d.value("allow_multi_choices").toBool()},
            {"is_anonymous", d.value("is_anonymous").toBool()},
            {"expired_time", d.value("expired_time").toVariant().toLongLong()},
            {"creator", idOf(d.value("creator"))},
            {"loaded", true},
        };
        QVariantList options;
        for (const QJsonValue& v : d.value("options").toArray()) {
            const QJsonObject o = v.toObject();
            options.append(QVariantMap{{"option_id", o.value("option_id").toInt()},
                                       {"content", o.value("content").toString()},
                                       {"votes", o.value("votes").toInt()},
                                       {"voted", o.value("voted").toBool()}});
        }
        poll["options"] = options;
        m_messageModel->setPollData(pollId, poll);
    });
}

qint64 ZaloClient::voicePosition() const {
    return m_voicePlayer ? m_voicePlayer->position() : 0;
}

qint64 ZaloClient::voiceDuration() const {
    return m_voicePlayer ? m_voicePlayer->duration() : 0;
}

void ZaloClient::seekVoice(qint64 positionMs) {
    if (m_voicePlayer && !m_playingVoiceId.isEmpty()) {
        m_voicePlayer->setPosition(qMax<qint64>(0, positionMs));
    }
}

void ZaloClient::setVoicePlaybackRate(qreal rate) {
    if (qFuzzyCompare(m_voicePlaybackRate, rate) || rate <= 0.0) return;
    m_voicePlaybackRate = rate;
    if (m_voicePlayer) {
        m_voicePlayer->setPlaybackRate(m_voicePlaybackRate);
    }
    emit voicePlaybackChanged();
}

void ZaloClient::toggleVoice(const QString& msgId, const QString& url) {
    if (msgId.isEmpty() || url.isEmpty()) return;
    if (m_voicePlayer && m_playingVoiceId == msgId) {
        m_voicePlayer->stop();
        return;
    }
    if (!m_voicePlayer) {
        m_voicePlayer = new QMediaPlayer(this);
        m_voicePlayer->setAudioOutput(new QAudioOutput(m_voicePlayer));
        connect(m_voicePlayer, &QMediaPlayer::positionChanged, this, [this](qint64) {
            emit voicePlaybackChanged();
        });
        connect(m_voicePlayer, &QMediaPlayer::durationChanged, this, [this](qint64) {
            emit voicePlaybackChanged();
        });
        connect(m_voicePlayer, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
            if (state == QMediaPlayer::StoppedState && !m_playingVoiceId.isEmpty()) {
                m_playingVoiceId.clear();
                emit voicePlaybackChanged();
            }
        });
        connect(m_voicePlayer, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& err) {
            const QString url = m_voicePlayer->source().toString();
            const QString id = m_playingVoiceId;
            if (!id.isEmpty()) m_voiceErrors.insert(id);
            qWarning() << "voice playback failed:" << err;
            m_playingVoiceId.clear();
            emit voicePlaybackChanged();
            emit notice(L("Không phát được trong ứng dụng, mở bằng trình phát của hệ thống…",
                          "Can't play it in the app; opening it with the system player…"));
            openRemoteFile(url, QStringLiteral("voice-%1.aac").arg(id),
                           L("Tin nhắn thoại không tồn tại", "The voice message no longer exists"));
        });
    }
    m_voicePlayer->stop();
    m_playingVoiceId = msgId;
    emit voicePlaybackChanged();
    m_voicePlayer->setSource(QUrl(url));
    m_voicePlayer->setPlaybackRate(m_voicePlaybackRate);
    m_voicePlayer->play();
}

void ZaloClient::loadQuickMessages() {
    if (!m_isLoggedIn) return;
    m_apiClient->session()->fetchQuickMessages([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        QVariantList items;
        for (const QJsonValue& v : data.toObject().value("items").toArray()) {
            const QJsonObject q = v.toObject();
            const QString text = q.value("message").toObject().value("title").toString();
            if (text.isEmpty()) continue;
            items.append(QVariantMap{{"id", q.value("id").toVariant().toLongLong()},
                                     {"keyword", q.value("keyword").toString()},
                                     {"text", text},
                                     {"hasMedia", !q.value("media").toObject().value("items").toArray().isEmpty()}});
        }
        m_quickMessages = items;
        emit quickMessagesChanged();
    });
}

void ZaloClient::createQuickMessage(const QString& keyword, const QString& title) {
    if (!m_isLoggedIn || keyword.trimmed().isEmpty() || title.trimmed().isEmpty()) return;
    m_apiClient->session()->createQuickMessage(keyword.trimmed(), title.trimmed(),
        [this](bool ok, const QJsonValue&, const QString& error, int) {
            if (!ok) { emit notice(L("Không thể tạo tin nhắn nhanh: ", "Couldn't create quick message: ") + friendlyError(error)); return; }
            loadQuickMessages();
        });
}

void ZaloClient::updateQuickMessage(qint64 id, const QString& keyword, const QString& title) {
    if (!m_isLoggedIn || id <= 0 || keyword.trimmed().isEmpty() || title.trimmed().isEmpty()) return;
    m_apiClient->session()->updateQuickMessage(id, keyword.trimmed(), title.trimmed(),
        [this](bool ok, const QJsonValue&, const QString& error, int) {
            if (!ok) { emit notice(L("Không thể cập nhật tin nhắn nhanh: ", "Couldn't update quick message: ") + friendlyError(error)); return; }
            loadQuickMessages();
        });
}

void ZaloClient::deleteQuickMessage(qint64 id) {
    if (!m_isLoggedIn || id <= 0) return;
    m_apiClient->session()->deleteQuickMessage(id, [this](bool ok, const QJsonValue&, const QString& error, int) {
        if (!ok) { emit notice(L("Không thể xóa tin nhắn nhanh: ", "Couldn't delete quick message: ") + friendlyError(error)); return; }
        loadQuickMessages();
    });
}

void ZaloClient::loadServerSettings() {
    if (!m_isLoggedIn) return;
    m_apiClient->session()->getSettings([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        const QJsonObject o = data.toObject();
        if (o.isEmpty()) return;
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            const QJsonValue v = it.value();
            if (v.isBool()) m_serverSettings[it.key()] = v.toBool() ? 1 : 0;
            else if (v.isDouble()) m_serverSettings[it.key()] = v.toInt();
        }
        emit serverSettingsChanged();
    });
}

void ZaloClient::setServerSetting(const QString& type, int value) {
    if (!m_isLoggedIn || type.isEmpty()) return;
    const int previous = m_serverSettings.value(type, value);
    m_serverSettings[type] = value;
    emit serverSettingsChanged();
    m_apiClient->session()->updateSetting(type, value, [this, type, previous](bool ok, const QJsonValue&, const QString& error, int) {
        if (ok) return;
        m_serverSettings[type] = previous;
        emit serverSettingsChanged();
        emit notice(L("Không thể cập nhật cài đặt: ", "Couldn't update setting: ") + friendlyError(error));
    });
}

bool ZaloClient::kikiAvailable() const {
    return (m_apiClient && m_apiClient->session())
        ? m_apiClient->session()->kikiAssistantConfig().value(QStringLiteral("enable")).toVariant().toBool()
        : false;
}

QString ZaloClient::kikiUrl() const {
    return (m_apiClient && m_apiClient->session())
        ? m_apiClient->session()->kikiAssistantConfig().value(QStringLiteral("h5_url")).toString()
        : QString();
}

void ZaloClient::loadReminders() {
    m_reminders.clear();
    if (!m_isLoggedIn || m_currentConvId.isEmpty()) {
        m_loadingReminders = false;
        emit remindersChanged();
        return;
    }
    const QString conv = m_currentConvId;
    m_loadingReminders = true;
    emit remindersChanged();
    m_apiClient->session()->listReminders(conv, isGroupThread(conv),
        [this, conv](bool ok, const QJsonValue& data, const QString&, int) {
            if (conv != m_currentConvId) return;
            m_loadingReminders = false;
            m_reminders.clear();
            QJsonArray arr;
            if (data.isArray()) {
                arr = data.toArray();
            } else {
                const QJsonObject o = data.toObject();
                for (const char* k : {"list", "items", "data", "reminders", "topics"}) {
                    if (o.value(QLatin1String(k)).isArray()) { arr = o.value(QLatin1String(k)).toArray(); break; }
                }
            }
            for (const QJsonValue& v : arr) {
                const QJsonObject r = v.toObject();
                QString id = r.value("id").toVariant().toString();
                if (id.isEmpty()) id = r.value("topicId").toVariant().toString();
                if (id.isEmpty()) id = r.value("reminderId").toVariant().toString();
                const QJsonValue pv = r.value("params");
                QString title = pv.isObject() ? pv.toObject().value("title").toString()
                              : (pv.isString() ? QJsonDocument::fromJson(pv.toString().toUtf8()).object().value("title").toString()
                                               : r.value("title").toString());
                if (id.isEmpty() && title.isEmpty()) continue;
                m_reminders.append(QVariantMap{{"id", id}, {"title", title}, {"emoji", r.value("emoji").toString()},
                                               {"startTime", r.value("startTime").toVariant().toLongLong()},
                                               {"repeat", r.value("repeat").toInt()},
                                               {"creatorUid", r.value("creatorUid").toVariant().toString()}});
            }
            emit remindersChanged();
        });
}

void ZaloClient::createReminder(const QString& title, qint64 startTimeMs, const QString& emoji, int repeat) {
    if (!m_isLoggedIn || m_currentConvId.isEmpty() || title.trimmed().isEmpty()) return;
    const QString conv = m_currentConvId;
    m_apiClient->session()->createReminder(conv, isGroupThread(conv), title.trimmed(), emoji, startTimeMs, repeat,
        [this, conv](bool ok, const QJsonValue&, const QString& error, int) {
            if (!ok) { emit notice(L("Không thể tạo nhắc hẹn: ", "Couldn't create reminder: ") + friendlyError(error)); return; }
            emit notice(L("Đã tạo nhắc hẹn", "Reminder created"));
            if (conv == m_currentConvId) loadReminders();
        });
}

void ZaloClient::removeReminder(const QString& reminderId) {
    if (!m_isLoggedIn || m_currentConvId.isEmpty() || reminderId.isEmpty()) return;
    const QString conv = m_currentConvId;
    m_apiClient->session()->removeReminder(conv, isGroupThread(conv), reminderId,
        [this, conv](bool ok, const QJsonValue&, const QString& error, int) {
            if (!ok) { emit notice(L("Không thể xóa nhắc hẹn: ", "Couldn't delete reminder: ") + friendlyError(error)); return; }
            if (conv == m_currentConvId) loadReminders();
        });
}

void ZaloClient::createNote(const QString& title) {
    if (!m_isLoggedIn || m_currentConvId.isEmpty() || title.trimmed().isEmpty()) return;
    if (!isGroupThread(m_currentConvId)) {
        emit notice(L("Ghi chú chỉ dùng trong nhóm", "Notes are only available in groups"));
        return;
    }
    const QString conv = m_currentConvId;
    m_apiClient->session()->createNote(conv, title.trimmed(), true,
        [this, conv](bool ok, const QJsonValue&, const QString& error, int) {
            if (!ok) { emit notice(L("Không thể tạo ghi chú: ", "Couldn't create note: ") + friendlyError(error)); return; }
            emit notice(L("Đã tạo ghi chú", "Note created"));
            if (conv == m_currentConvId && m_pinBoard) m_pinBoard->reload();
        });
}

QHash<QString, QJsonArray> ZaloClient::deliveredCandidates(const QJsonArray& msgs, bool isGroup) const {
    QHash<QString, QJsonArray> out;
    for (const QJsonValue& v : msgs) {
        const QJsonObject m = v.toObject();
        const QString from = idOf(m.value("uidFrom"));
        if (from.isEmpty() || from == "0" || from == m_myUid) continue;
        const QString type = m.value("msgType").toString();
        if (type == QLatin1String("chat.delete") || type == QLatin1String("group.poll")) continue;
        if (!undoTargetOf(m).isEmpty()) continue;
        if (idOf(m.value("msgId")).isEmpty()) continue;
        out[isGroup ? idOf(m.value("idTo")) : from].append(m);
    }
    return out;
}

void ZaloClient::sendDeliveredReceipts(const QJsonArray& msgs, bool isGroup) {
    if (!m_isLoggedIn || m_blockDelivered) return;
    const auto byThread = deliveredCandidates(msgs, isGroup);
    for (auto it = byThread.cbegin(); it != byThread.cend(); ++it) {
        m_apiClient->session()->sendDelivered(it.key(), isGroup, it.value(), [](bool, const QJsonValue&, const QString&, int) {});
    }
}

void ZaloClient::resolveUnknownUsers() {
    if (m_pendingUserLookups.isEmpty() || m_userLookupInFlight) return;
    QStringList ids;
    for (auto it = m_pendingUserLookups.begin(); it != m_pendingUserLookups.end() && ids.size() < 100;) {
        ids << *it;
        it = m_pendingUserLookups.erase(it);
    }
    m_userLookupInFlight = true;
    m_apiClient->session()->fetchUserInfo(ids, [this, ids](bool ok, const QJsonValue& data, const QString&, int) {
        m_userLookupInFlight = false;
        if (!m_pendingUserLookups.isEmpty()) m_userLookupTimer->start();
        QSet<QString> resolved;
        if (ok) {
            m_conversationModel->beginBatch();
            const QJsonObject changed = data.toObject().value("changed_profiles").toObject();
            for (auto it = changed.begin(); it != changed.end(); ++it) {
                QString uid = it.key();
                if (uid.endsWith("_0")) uid.chop(2);
                const QJsonObject p = it.value().toObject();
                const QString name = pickName(p);
                if (name.isEmpty()) continue;
                resolved.insert(uid);
                m_users[uid] = {name, p.value("avatar").toString(), 0};
                noteAccountFlags(uid, p);
                applyProfileToConversation(uid);
                persistProfile(uid, false);
            }
            m_conversationModel->endBatch();
            m_messageModel->refreshSenders();
        }
        QStringList missing;
        for (const QString& id : ids) {
            if (!resolved.contains(id) && displayNameOf(id).isEmpty() && !m_unresolvedMembers.contains(id)) {
                missing << id;
            }
        }
        if (missing.isEmpty() || !m_isLoggedIn || !m_apiClient->session()) return;
        m_apiClient->session()->fetchGroupMembersInfo(missing, [this, missing](bool ok2, const QJsonValue& d2,
                                                                               const QString&, int) {
            if (!ok2) return;
            m_conversationModel->beginBatch();
            const QJsonObject profiles = d2.toObject().value("profiles").toObject();
            for (auto it = profiles.begin(); it != profiles.end(); ++it) {
                QString uid = it.key();
                if (uid.endsWith("_0")) uid.chop(2);
                const QJsonObject p = it.value().toObject();
                const QString name = pickName(p);
                if (name.isEmpty()) continue;
                m_users[uid] = {name, p.value("avatar").toString(), 0};
                noteAccountFlags(uid, p);
                if (m_conversationModel->contains(uid)) applyProfileToConversation(uid);
                persistProfile(uid, false);
            }
            for (const QString& id : missing) {
                if (displayNameOf(id).isEmpty()) m_unresolvedMembers.insert(id);
            }
            m_conversationModel->endBatch();
            m_messageModel->refreshSenders();
        });
    });
}

void ZaloClient::queueUnknownSenders(int first, int last) {
    bool queued = false;
    auto maybeQueue = [&](const QString& uid) {
        if (uid.isEmpty() || uid == QLatin1String("0") || uid == QLatin1String("-1")
            || uid == QLatin1String("system") || uid == m_myUid) return;
        if (!displayNameOf(uid).isEmpty() || m_groupIds.contains(uid) || m_unresolvedMembers.contains(uid)) return;
        m_pendingUserLookups.insert(uid);
        queued = true;
    };
    for (int row = qMax(0, first); row <= last && row < m_messageModel->rowCount(); ++row) {
        const QModelIndex idx = m_messageModel->index(row);
        maybeQueue(idx.data(Models::MessageModel::SenderIdRole).toString());
        const QString msgId = idx.data(Models::MessageModel::IdRole).toString();
        const QJsonObject raw = m_rawMessages.value(msgId);
        if (!raw.isEmpty()) {
            maybeQueue(idOf(raw.value("quote").toObject().value("ownerId")));
            for (const QJsonValue& mv : raw.value("mentions").toArray()) {
                maybeQueue(idOf(mv.toObject().value("uid")));
            }
        }
    }
    if (queued && m_isLoggedIn) m_userLookupTimer->start();
}

void ZaloClient::rebuildContacts() {
    QVector<Models::ContactItem> items;
    items.reserve(m_friendIds.size() + m_groups.size());
    for (const QString& uid : m_friendIds) {
        const Profile p = m_users.value(uid);
        Models::ContactItem c;
        c.id = uid;
        c.name = displayNameOf(uid);
        c.avatarUrl = p.avatar;
        c.category = "friends";
        items.append(c);
    }
    for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it) {
        Models::ContactItem c;
        c.id = it.key();
        c.name = it->name;
        c.avatarUrl = it->avatar;
        c.isGroup = true;
        c.memberCount = it->memberCount;
        c.category = "groups";
        items.append(c);
    }
    items += m_friendRequests;
    m_contactModel->setContacts(items);
    m_contactModel->setOnlineIds(m_onlineIds);
    syncFriendContext();
}

void ZaloClient::refreshContacts() {
    if (!m_isLoggedIn) return;
    loadAccountData();
}

void ZaloClient::ensureConversation(const QString& threadId, bool isGroup) {
    if (threadId.isEmpty() || m_conversationModel->contains(threadId)) return;
    Models::ConversationItem item;
    item.id = threadId;
    item.isGroup = isGroup;
    item.isMuted = m_mutedIds.contains(threadId);
    item.isOnline = !isGroup && m_onlineIds.contains(threadId);
    item.category = "priority";
    if (isGroup) {
        const Profile g = m_groups.value(threadId);
        item.name = g.name;
        item.avatarUrl = g.avatar;
        item.memberCount = g.memberCount;
        if (!m_groups.contains(threadId)) loadGroupInfo({threadId});
    } else {
        const Profile u = m_users.value(threadId);
        item.name = threadId == myCloudId() ? L("My Documents", "My Documents") : displayNameOf(threadId);
        item.avatarUrl = u.avatar;
        if (!m_users.contains(threadId) && threadId != myCloudId()) {
            m_pendingUserLookups.insert(threadId);
            m_userLookupTimer->start();
        }
    }
    m_conversationModel->upsertConversation(item);
    if (m_offlineStore) {
        m_offlineStore->saveConversation(item);
    }
}

void ZaloClient::applyProfileToConversation(const QString& id) {
    const bool isGroup = m_groups.contains(id);
    Profile p = isGroup ? m_groups.value(id) : m_users.value(id);
    if (!isGroup) p.name = displayNameOf(id);
    m_conversationModel->updateProfile(id, p.name, p.avatar, isGroup ? p.memberCount : -1);
    if (m_offlineStore) {
        m_offlineStore->saveConversation(m_conversationModel->conversation(id));
    }
    if (id == m_currentConvId) {
        if (!p.name.isEmpty()) m_currentConvName = p.name;
        if (!p.avatar.isEmpty()) m_currentConvAvatar = p.avatar;
        emit currentConversationChanged();
    }
}

void ZaloClient::callLogItem(const QJsonObject& obj, bool missed, Models::MessageItem* item, QString* label) const {
    const QJsonObject params = QJsonDocument::fromJson(obj.value("params").toString().toUtf8()).object();
    const int ct = params.contains("calltype") ? params.value("calltype").toInt() : params.value("callType").toInt();
    const bool video = (ct == 1 || ct == 3 || ct == 6 || params.value("video").toBool() || params.value("isVideo").toBool());
    const bool outgoing = params.value("isCaller").toInt() == 1 || params.value("is_caller").toBool();
    const int seconds = params.value("duration").toInt();
    item->messageType = "call";
    if (missed) {
        if (outgoing) {
            item->text = seconds == 0
                ? (video ? L("Cuộc gọi video đi đã hủy", "Canceled outgoing video call")
                         : L("Cuộc gọi thoại đi đã hủy", "Canceled outgoing voice call"))
                : (video ? L("Cuộc gọi video đi không được trả lời", "Unanswered outgoing video call")
                         : L("Cuộc gọi thoại đi không được trả lời", "Unanswered outgoing voice call"));
        } else {
            item->text = video ? L("Cuộc gọi video nhỡ", "Missed video call")
                               : L("Cuộc gọi thoại nhỡ", "Missed voice call");
        }
    } else {
        item->text = outgoing ? (video ? L("Cuộc gọi video đi", "Outgoing video call") : L("Cuộc gọi thoại đi", "Outgoing voice call"))
                              : (video ? L("Cuộc gọi video đến", "Incoming video call") : L("Cuộc gọi thoại đến", "Incoming voice call"));
    }
    QString duration;
    if (!missed && seconds > 0) {
        duration = seconds >= 60 ? L("%1 phút %2 giây", "%1 min %2 s").arg(seconds / 60).arg(seconds % 60)
                                 : L("%1 giây", "%1 s").arg(seconds);
    }
    item->extra = {{"video", video}, {"missed", missed}, {"outgoing", outgoing}, {"seconds", seconds},
                   {"duration", duration}};
    *label = "[" + item->text + "]";
}

Models::MessageItem ZaloClient::toMessageItem(const QJsonObject& msg, bool isGroup, QString* threadId, QString* preview) const {
    Models::MessageItem item;
    const QString uidFrom = idOf(msg.value("uidFrom"));
    const QString idTo = idOf(msg.value("idTo"));
    const bool isSelf = uidFrom == "0" || (!m_myUid.isEmpty() && uidFrom == m_myUid);
    *threadId = isGroup ? idTo : (isSelf ? idTo : uidFrom);

    item.id = idOf(msg.value("msgId"));
    item.cliMsgId = idOf(msg.value("cliMsgId"));
    if (item.id.isEmpty()) item.id = item.cliMsgId;
    const QJsonObject quote = msg.value("quote").toObject();
    if (!quote.isEmpty()) {
        item.quoteMsgId = idOf(quote.value("globalMsgId"));
        if (item.quoteMsgId.isEmpty() || item.quoteMsgId == QLatin1String("0")) {
            item.quoteMsgId = idOf(quote.value("msgId"));
        }
        if (item.quoteMsgId.isEmpty() || item.quoteMsgId == QLatin1String("0")) {
            item.quoteMsgId = idOf(quote.value("cliMsgId"));
        }
        const QString owner = idOf(quote.value("ownerId"));
        QString qSender;
        if (!m_myUid.isEmpty() && owner == m_myUid) {
            qSender = L("Bạn", "You");
        } else {
            qSender = displayNameOf(owner);
            if (qSender.isEmpty()) qSender = quote.value("fromD").toString();
        }
        item.quoteSender = qSender;
        item.quoteText = quote.value("msg").toString();
        if (item.quoteText.isEmpty()) {
            item.quoteText = quote.value("attach").toString().isEmpty() ? L("[Tin nhắn]", "[Message]")
                                                                         : L("[Tệp đính kèm]", "[Attachment]");
        }
    }
    item.ts = msg.value("ts").toVariant().toLongLong();
    item.timeStr = item.ts >= kMinPlausibleEpochMs ? QDateTime::fromMSecsSinceEpoch(item.ts).toString("HH:mm") : QString();
    item.isMine = isSelf;
    item.status = isSelf ? QStringLiteral("sent") : QString();
    item.senderId = isSelf ? m_myUid : uidFrom;
    const Profile sender = m_users.value(item.senderId);
    item.senderName = isSelf ? m_userName : m_aliases.value(item.senderId, msg.value("dName").toString(sender.name));
    if (item.senderName.isEmpty()) item.senderName = sender.name;
    item.senderAvatar = isSelf ? m_userAvatar : sender.avatar;

    const QString type = msg.value("msgType").toString();
    const QJsonValue content = msg.value("content");
    const QJsonObject obj = content.toObject();
    QString label;

    if (content.isString()) {
        item.text = content.toString();
        label = item.text;
        for (const char* key : {"textProperties", "params", "propertyExt"}) {
            const QJsonValue raw = msg.value(QLatin1String(key));
            const QJsonObject props = raw.isString() ? QJsonDocument::fromJson(raw.toString().toUtf8()).object() : raw.toObject();
            QVariantList styles;
            for (const QJsonValue& v : props.value("styles").toArray()) {
                const QJsonObject o = v.toObject();
                const int start = o.value("start").toInt(-1);
                const int len = o.value("len").toInt();
                if (start < 0 || len <= 0 || start + len > item.text.size()) continue;
                styles.append(QVariantMap{{"start", start}, {"len", len}, {"st", o.value("st").toString()}});
            }
            if (!styles.isEmpty()) {
                item.extra.insert("styles", styles);
                break;
            }
        }
    } else if (type == "chat.photo" || type == "chat.gif" || type == "chat.doodle") {
        item.messageType = "image";
        item.mediaUrl = obj.value("thumb").toString();
        item.href = obj.value("href").toString();
        if (item.mediaUrl.isEmpty()) item.mediaUrl = item.href;
        item.text = captionOf(obj);
        const QString hrefPath = QUrl(item.href).path().toLower();
        if (!item.href.isEmpty() && (type == "chat.gif" || hrefPath.endsWith(".gif") || hrefPath.endsWith(".webp")))
            item.extra.insert("animatedUrl", item.href);
        label = L("[Hình ảnh]", "[Photo]");
    } else if (type == "share.file") {
        item.messageType = "file";
        item.mediaUrl = obj.value("href").toString();
        item.href = item.mediaUrl;
        const QJsonValue rawParams = obj.value("params");
        const QJsonObject params = rawParams.isString()
            ? QJsonDocument::fromJson(rawParams.toString().toUtf8()).object() : rawParams.toObject();
        QString title = obj.value("title").toString().trimmed();
        QString ext = params.value("fileExt").toString().trimmed();
        while (ext.startsWith('.')) ext.remove(0, 1);
        if (title.isEmpty() || title.compare(QLatin1String("renewMedia"), Qt::CaseInsensitive) == 0) {
            const QString urlFile = QUrl(item.href).fileName();
            if (!urlFile.isEmpty() && urlFile.compare(QLatin1String("renewMedia"), Qt::CaseInsensitive) != 0) {
                title = urlFile;
            } else {
                title = QStringLiteral("attachment-%1").arg(item.id.isEmpty() ? QString::number(item.ts) : item.id);
            }
        }
        if (!ext.isEmpty() && !title.endsWith(QLatin1Char('.') + ext, Qt::CaseInsensitive)) {
            title += QLatin1Char('.') + ext;
        }
        item.text = title;
        item.fileSize = params.value("fileSize").toVariant().toLongLong();
        label = L("[Tệp] ", "[File] ") + item.text;
    } else if (type == "chat.sticker") {
        item.messageType = "sticker";
        const QVariantMap info = m_stickerStore->info(idOf(obj.value("id")));
        if (!info.isEmpty()) {
            item.mediaUrl = info.value("url").toString();
            item.extra.insert("sticker", info);
        }
        label = L("[Nhãn dán]", "[Sticker]");
    } else if (type == "chat.voice") {
        const QJsonObject params = QJsonDocument::fromJson(obj.value("params").toString().toUtf8()).object();
        item.messageType = "voice";
        item.href = obj.value("href").toString();
        if (item.href.isEmpty()) item.href = params.value("m4a").toString();
        item.fileSize = params.value("fileSize").toVariant().toLongLong();
        item.extra = {{"durationMs", params.value("duration").toVariant().toLongLong()}};
        item.text = L("Tin nhắn thoại", "Voice message");
        label = L("[Tin nhắn thoại]", "[Voice message]");
    } else if (type == "chat.video.msg") {
        item.messageType = "video";
        item.mediaUrl = obj.value("thumb").toString();
        item.href = obj.value("href").toString();
        item.text = captionOf(obj);
        const QJsonValue videoParams = obj.value("params");
        item.fileSize = (videoParams.isString() ? QJsonDocument::fromJson(videoParams.toString().toUtf8()).object()
                                                : videoParams.toObject()).value("fileSize").toVariant().toLongLong();
        label = L("[Video]", "[Video]");
    } else if (type == "chat.location.new") {
        const QJsonValue rawParams = obj.value("params");
        const QJsonObject params = rawParams.isString()
                                       ? QJsonDocument::fromJson(rawParams.toString().toUtf8()).object()
                                       : rawParams.toObject();
        auto field = [&](const char* key) {
            const QString v = params.value(QLatin1String(key)).toVariant().toString();
            return v.isEmpty() ? obj.value(QLatin1String(key)).toVariant().toString() : v;
        };
        bool latOk = false, lngOk = false;
        const double lat = field("latitude").toDouble(&latOk);
        const double lng = field("longitude").toDouble(&lngOk);
        const bool hasCoords = latOk && lngOk && qAbs(lat) <= 90 && qAbs(lng) <= 180 && !(lat == 0 && lng == 0);
        const QString coords = hasCoords ? QStringLiteral("%1, %2").arg(lat, 0, 'f', 6).arg(lng, 0, 'f', 6) : QString();
        const QString title = field("title");
        item.messageType = "location";
        item.text = !title.isEmpty() ? title : L("Vị trí được chia sẻ", "Shared location");
        if (hasCoords) {
            item.href = QStringLiteral("https://www.google.com/maps?q=%1,%2").arg(lat, 0, 'f', 6).arg(lng, 0, 'f', 6);
        }
        item.extra = {{"desc", field("description")}, {"coords", coords}};
        label = L("[Vị trí] ", "[Location] ") + item.text;
    } else if (type == "chat.ecard") {
        item.messageType = "ecard";
        const QJsonValue rawParams = obj.value("params");
        const QJsonObject params = rawParams.isString()
                                       ? QJsonDocument::fromJson(rawParams.toString().toUtf8()).object()
                                       : rawParams.toObject();
        const QString ecardType = obj.value("type").toVariant().toString();
        QVariantList stickers;
        const QJsonArray actions = params.value("actions").toArray();
        if (!actions.isEmpty()) {
            const QJsonValue rawData = actions.first().toObject().value("data");
            const QJsonObject data = rawData.isString()
                                         ? QJsonDocument::fromJson(rawData.toString().toUtf8()).object()
                                         : rawData.toObject();
            for (const QJsonValue& v : data.value("stickerList").toArray()) {
                const QJsonObject s = v.toObject();
                const QString sid = idOf(s.value("id"));
                if (sid.isEmpty()) continue;
                stickers.append(QVariantMap{{"id", sid},
                                            {"cateId", s.value("catId").toVariant().toLongLong()},
                                            {"type", s.value("type").toInt(7)}});
            }
        }
        const QString title = obj.value("title").toString();
        const QString who = item.senderName;
        if (ecardType == QLatin1String("1") && !who.isEmpty()) {
            item.text = L("Hôm nay là sinh nhật của %1", "Today is %1's birthday").arg(who);
        } else {
            item.text = title.isEmpty() ? L("Thiệp", "Card") : title;
        }
        const QString desc = obj.value("description").toString();
        item.extra = {{"ecardType", ecardType},
                      {"desc", ecardType == QLatin1String("1") && !stickers.isEmpty()
                                   ? L("Gửi ngay sticker dưới đây thay lời chúc mừng",
                                       "Send one of these stickers as your wishes")
                                   : (desc == title ? QString() : desc)},
                      {"thumb", obj.value("thumb").toString()},
                      {"stickers", stickers}};
        label = L("[Thiệp] ", "[Card] ") + item.text;
    } else if (type == "chat.list.action" && (content.isArray() ? content.toArray().at(0).toObject() : obj)
                                                    .value("href").toString().isEmpty()) {
        const QJsonObject first = content.isArray() ? content.toArray().at(0).toObject() : obj;
        const QString title = first.value("title").toString();
        const QString desc = first.value("description").toString();
        if (isGroup) {
            item.messageType = "system";
            item.text = !title.isEmpty() ? title : desc;
        } else {
            item.messageType = "link";
            item.text = !title.isEmpty() ? title : desc;
            item.mediaUrl = first.value("thumb").toString();
            item.extra = {{"desc", desc == item.text ? QString() : desc}, {"domain", QString()}};
        }
        if (item.text.isEmpty())
            item.text = L("Tin nhắn chưa hỗ trợ hiển thị trên phiên bản hiện tại", "This message isn't supported in the current version");
        label = item.text;
    } else if (type == "chat.list.action") {
        const QJsonArray cards = content.toArray();
        const QJsonObject first = cards.isEmpty() ? obj : cards.first().toObject();
        const QString title = first.value("title").toString();
        const QString href = first.value("href").toString();
        item.messageType = "link";
        item.href = href;
        item.mediaUrl = first.value("thumb").toString();
        item.text = title.isEmpty() ? href : title;
        const QString desc = first.value("description").toString();
        item.extra = {{"desc", desc == title ? QString() : desc}, {"domain", QUrl(href).host()}};
        label = L("[Liên kết] ", "[Link] ") + item.text;
    } else if (type == "chat.webcontent" || type == "chat.webcontent.v2") {
        const QString title = obj.value("title").toString();
        const QString href = obj.value("href").toString();
        const QJsonValue rawParams = obj.value("params");
        const QJsonObject params = rawParams.isString()
                                       ? QJsonDocument::fromJson(rawParams.toString().toUtf8()).object()
                                       : rawParams.toObject();
        const QJsonObject custom = params.value("customMsg").toObject().value("msg").toObject();
        QString localized = custom.value(m_appLanguage == QLatin1String("en") ? "en" : "vi").toString();
        if (localized.isEmpty()) localized = custom.value("en").toString();
        if (localized.isEmpty()) localized = custom.value("vi").toString();
        localized = fillHighlights(localized, params.value("customMsg").toObject().value("highLightsV2").toArray());
        const QString templateId = params.value("item").toObject().value("zinstantdata_id").toString();
        const int webType = params.value("type").toVariant().toInt();
        const bool birthday = webType == 11 || templateId == QLatin1String("templateId=14030");
        const bool bankCard = templateId == QLatin1String("templateId=11845")
                              || obj.value("action").toString() == QLatin1String("zinstant.bankcard")
                              || params.value("action").toString() == QLatin1String("zinstant.bankcard");
        if (bankCard) {
            item.messageType = "bankcard";
            item.text = !localized.isEmpty() ? localized : (!title.isEmpty() ? title : L("Thông tin chuyển khoản", "Bank transfer details"));
            item.extra = {{"desc", L("Mở Zalo trên điện thoại để xem chi tiết và chuyển khoản",
                                     "Open Zalo on your phone to see the details and transfer")}};
            label = L("[Chuyển khoản] ", "[Bank transfer] ") + item.text;
        } else if (birthday) {
            item.messageType = "ecard";
            item.text = !localized.isEmpty() ? localized
                      : (!item.senderName.isEmpty() ? L("Hôm nay là sinh nhật của %1", "Today is %1's birthday").arg(item.senderName)
                                                    : title);
            item.extra = {{"ecardType", QStringLiteral("1")},
                          {"desc", L("Gửi ngay sticker dưới đây thay lời chúc mừng", "Send one of these stickers as your wishes")},
                          {"thumb", QString()},
                          {"stickers", QVariantList{}},
                          {"suggestStickers", true}};
            label = L("[Sinh nhật] ", "[Birthday] ") + item.text;
        } else if (!localized.isEmpty() && !isGroup && webType != 9
                   && params.value("customMsg").toObject().value("highLightsV2").toArray().isEmpty()
                   && templateId != QLatin1String("templateId=11366") && templateId != QLatin1String("templateId=10709")
                   && templateId != QLatin1String("templateId=10667")) {
            const QString body = localized.trimmed();
            const qsizetype cut = body.indexOf(QLatin1Char('\n'));
            item.messageType = "link";
            item.text = cut > 0 ? body.left(cut).trimmed() : body;
            item.mediaUrl = obj.value("thumb").toString();
            QString buttonLabel = params.value("actionTitle").toString();
            QString buttonUrl = href;
            for (const QJsonValue& a : params.value("actions").toArray()) {
                const QJsonObject act = a.toObject();
                const QString at = act.value("actionTitle").toString(act.value("title").toString());
                const QString au = act.value("actionUrl").toString(act.value("href").toString());
                if (!at.isEmpty()) { buttonLabel = at; if (!au.isEmpty()) buttonUrl = au; break; }
            }
            if (buttonUrl.isEmpty() && !href.isEmpty()) buttonUrl = href;
            item.href = buttonUrl;
            item.extra = {{"desc", cut > 0 ? body.mid(cut + 1).trimmed() : QString()},
                          {"domain", buttonUrl.isEmpty() ? QString() : QUrl(buttonUrl).host()},
                          {"actionLabel", buttonLabel}};
            label = item.text;
        } else if (!localized.isEmpty()) {
            item.messageType = "system";
            item.text = localized;
            for (const QJsonValue& a : params.value("actions").toArray()) {
                const QJsonObject act = a.toObject();
                if (act.value("actionType").toString() != QLatin1String("action.open.calendar.event")) continue;
                const QJsonValue raw = act.value("actionData");
                const QJsonObject data = raw.isString() ? QJsonDocument::fromJson(raw.toString().toUtf8()).object()
                                                        : raw.toObject();
                const QString eventId = idOf(data.value("eventId"));
                if (!eventId.isEmpty()) item.extra.insert("reminderEventId", eventId);
            }
            label = item.text;
        } else if (!href.isEmpty()) {
            item.messageType = "link";
            item.href = href;
            item.mediaUrl = obj.value("thumb").toString();
            item.text = title.isEmpty() ? L("Nội dung web của Zalo", "Zalo web content") : title;
            const QString desc = obj.value("description").toString();
            item.extra = {{"desc", desc == title ? QString() : desc}, {"domain", QUrl(href).host()}};
            label = L("[Liên kết] ", "[Link] ") + item.text;
        } else if (!isGroup && webType != 6 && webType != 16 && webType != 19) {
            item.messageType = "link";
            item.mediaUrl = obj.value("thumb").toString();
            const QString desc = webType == 34 && !title.isEmpty() ? title : obj.value("description").toString();
            item.text = webType == 34 ? L("Zalo gửi thông báo về an toàn tài khoản của bạn", "Zalo sent an account safety notice")
                      : !title.isEmpty() ? title
                      : (!desc.isEmpty() ? desc
                                         : L("Tin nhắn chưa hỗ trợ hiển thị trên phiên bản hiện tại",
                                             "This message isn't supported in the current version"));
            item.extra = {{"desc", desc == item.text ? QString() : desc}, {"domain", QString()}};
            label = item.text;
        } else {
            item.messageType = "system";
            if (webType == 6) {
                item.text = title.contains(QLatin1String("created"), Qt::CaseInsensitive)
                                ? L("Bạn vừa tạo nhóm", "You just created the group")
                                : L("Bạn đã trở thành thành viên của nhóm", "You became a member of the group");
            } else if (webType == 19) {
                item.text = L("Bạn được bổ nhiệm làm trưởng nhóm", "You were appointed as the group owner");
            } else if (webType == 34) {
                item.text = L("Zalo gửi thông báo về an toàn tài khoản của bạn", "Zalo sent an account safety notice");
            } else if (!title.isEmpty()) {
                item.text = title;
            } else {
                item.text = L("Tin nhắn chưa hỗ trợ hiển thị trên phiên bản hiện tại",
                              "This message isn't supported in the current version");
            }
            label = item.text;
        }
    } else if (type == QLatin1String("chat.todo")) {
        const QJsonValue rawMessage = obj.value("message");
        QString msg = rawMessage.isString() ? rawMessage.toString() : rawMessage.toObject().value("message").toString();
        if (msg.isEmpty()) msg = obj.value("title").toString();
        if (msg.isEmpty()) msg = obj.value("description").toString();
        item.messageType = "system";
        item.text = msg.isEmpty() ? L("Nhắc nhở", "Reminder") : msg;
        const QJsonValue rawParams = obj.value("params");
        const QJsonObject params = rawParams.isString()
                                       ? QJsonDocument::fromJson(rawParams.toString().toUtf8()).object()
                                       : rawParams.toObject();
        const QString topicId = idOf(params.value("topicId").isUndefined() ? params.value("id") : params.value("topicId"));
        if (!topicId.isEmpty()) item.extra.insert("reminderEventId", topicId);
        label = item.text;
    } else if ((type == "chat.recommended" || type == "chat.link")
               && obj.value("action").toString() == QLatin1String("recommened.misscall")) {
        callLogItem(obj, true, &item, &label);
    } else if ((type == "chat.recommended" || type == "chat.link")
               && obj.value("action").toString() == QLatin1String("recommened.calltime")) {
        callLogItem(obj, false, &item, &label);
    } else if ((type == "chat.recommended" || type == "chat.link")
               && (obj.value("action").toString() == QLatin1String("recommened.user")
                   || obj.value("action").toString() == QLatin1String("recommened.vip"))) {
        item.messageType = "card";
        item.text = obj.value("title").toString();
        item.mediaUrl = obj.value("thumb").toString();
        QJsonObject desc = QJsonDocument::fromJson(obj.value("description").toString().toUtf8()).object();
        const QJsonValue params = obj.value("params");
        QString uid = params.isString() ? params.toString() : idOf(params);
        if (uid.startsWith('{')) uid = idOf(QJsonDocument::fromJson(uid.toUtf8()).object().value("uid"));
        item.href = obj.value("href").toString();
        item.extra = {{"uid", uid}, {"phone", desc.value("phone").toVariant().toString()},
                      {"qrCodeUrl", desc.value("qrCodeUrl").toString()},
                      {"official", obj.value("action").toString() == QLatin1String("recommened.vip")}};
        if (item.text.isEmpty()) item.text = L("Danh thiếp", "Contact card");
        label = L("[Danh thiếp] ", "[Contact card] ") + item.text;
    } else if (type == "chat.recommended" || type == "chat.link") {
        const QString title = obj.value("title").toString();
        const QString href = obj.value("href").toString();
        item.messageType = "link";
        item.href = href;
        item.mediaUrl = obj.value("thumb").toString();
        item.text = title.isEmpty() ? href : title;
        const QString desc = obj.value("description").toString();
        item.extra = {{"desc", desc == title ? QString() : desc}, {"domain", QUrl(href).host()}};
        label = L("[Liên kết] ", "[Link] ") + item.text;
    } else if (type == "group.poll" || type == "chat.poll" || obj.contains("poll_id")) {
        const QJsonValue rawParams = obj.value("params");
        const QJsonObject params = rawParams.isString()
                                       ? QJsonDocument::fromJson(rawParams.toString().toUtf8()).object()
                                       : rawParams.toObject();
        const QString action = obj.value("action").toString();
        item.messageType = (action.isEmpty() || action == QLatin1String("create")) ? QStringLiteral("poll")
                                                                                  : QStringLiteral("poll_update");
        QString q = obj.value("question").toString(params.value("question").toString());
        item.text = q.isEmpty() ? L("Cuộc bình chọn", "Poll") : q;
        label = L("[Bình chọn] ", "[Poll] ") + item.text;
        QVariantMap pData;
        qint64 pollId = obj.value("poll_id").toVariant().toLongLong();
        if (pollId <= 0) pollId = params.value("pollId").toVariant().toLongLong();
        if (pollId <= 0) pollId = params.value("poll_id").toVariant().toLongLong();
        pData["poll_id"] = pollId;
        pData["question"] = item.text;
        pData["closed"] = obj.value("closed").toBool(false);
        pData["num_vote"] = obj.value("num_vote").toInt(0);
        pData["allow_multi_choices"] = obj.value("allow_multi_choices").toBool(false);
        QVariantList optList;
        const QJsonArray optionsArr = obj.value("options").toArray();
        for (const QJsonValue& ov : optionsArr) {
            const QJsonObject oObj = ov.toObject();
            QVariantMap optMap;
            optMap["option_id"] = oObj.value("option_id").toInt();
            optMap["content"] = oObj.value("content").toString();
            optMap["votes"] = oObj.value("votes").toInt();
            optMap["voted"] = oObj.value("voted").toBool(false);
            optList.append(optMap);
        }
        pData["options"] = optList;
        item.pollData = pData;
    } else if (type == "chat.contact" || msg.value("msgType").toInt() == 6 || obj.contains("contactUid")) {
        item.messageType = "card";
        const QString uid = idOf(obj.value("contactUid"));
        QString cName = obj.value("contactName").toString();
        if (cName.isEmpty()) cName = displayNameOf(uid);
        item.text = cName.isEmpty() ? L("Danh thiếp", "Contact card") : cName;
        item.mediaUrl = m_users.value(uid).avatar;
        item.extra = {{"uid", uid}, {"phone", obj.value("phone").toVariant().toString()},
                      {"qrCodeUrl", obj.value("qrCodeUrl").toString()}};
        label = L("[Danh thiếp] ", "[Contact Card] ") + item.text;
    } else {
        item.text = obj.value("title").toString();
        if (item.text.isEmpty()) item.text = obj.value("description").toString();
        if (item.text.isEmpty())
            item.text = L("[Tin nhắn chưa hỗ trợ ở phiên bản hiện tại]", "[Message not supported in this version]");
        label = item.text;
    }
    if (type == QLatin1String("chat.undo") || type == QLatin1String("chat.delete.everyone")) {
        item.isRecalled = true;
        item.messageType = "text";
        item.mediaUrl.clear();
        item.href.clear();
        item.extra.clear();
        item.text = type == QLatin1String("chat.undo") ? L("Tin nhắn đã thu hồi", "Message recalled")
                                                       : L("Tin nhắn đã bị xóa", "Message deleted");
        label = item.text;
    }

    for (const QJsonValue& v : msg.value("mentions").toArray()) {
        const QJsonObject m = v.toObject();
        Models::MessageMention mention;
        mention.uid = idOf(m.value("uid"));
        mention.pos = m.value("pos").toInt(-1);
        mention.len = m.value("len").toInt();
        if (mention.pos >= 0 && mention.len > 0 && mention.pos + mention.len <= item.text.size()) {
            item.mentions.append(mention);
        }
    }

    if (preview) {
        *preview = label.simplified();
    }
    if (const qint64 expireOn = Offline::ZaloOfflineStore::expiryOf(msg, item.ts); expireOn > 0) {
        item.extra.insert(QStringLiteral("expireOn"), expireOn);
        item.expireOn = expireOn;
    } else if (item.extra.contains(QStringLiteral("expireOn"))) {
        item.expireOn = item.extra.value(QStringLiteral("expireOn")).toLongLong();
    }

    const QJsonObject decorLog = msg.value(QLatin1String("decorLog")).isString()
        ? QJsonDocument::fromJson(msg.value(QLatin1String("decorLog")).toString().toUtf8()).object()
        : msg.value(QLatin1String("decorLog")).toObject();
    const QJsonObject ref = msg.value(QLatin1String("reference")).isString()
        ? QJsonDocument::fromJson(msg.value(QLatin1String("reference")).toString().toUtf8()).object()
        : msg.value(QLatin1String("reference")).toObject();
    item.isForwarded = decorLog.contains(QLatin1String("fw")) || ref.contains(QLatin1String("fw"))
        || msg.contains(QLatin1String("forwardRefLog")) || msg.value(QLatin1String("isForward")).toBool();

    item.isE2EE = msg.value(QLatin1String("e2eeStatus")).toInt() > 0 || msg.value(QLatin1String("e2ee")).toBool()
        || msg.contains(QLatin1String("e2eeExt")) || msg.value(QLatin1String("msgType")).toString() == QLatin1String("chat.e2ee.replace")
        || (msg.contains(QLatin1String("params")) && msg.value(QLatin1String("params")).toString().contains(QLatin1String("e2ee")));

    const QString snapshotReaction = parseReactionSnapshot(msg);
    if (!snapshotReaction.isEmpty()) item.reaction = snapshotReaction;

    const QJsonArray seenUids = msg.value(QLatin1String("seenUids")).toArray();
    for (const QJsonValue& v : seenUids) {
        const QString u = idOf(v);
        if (!u.isEmpty() && u != QLatin1String("0") && !item.seenUids.contains(u)) item.seenUids.append(u);
    }
    if (!item.seenUids.isEmpty() && item.isMine && item.status.isEmpty()) item.status = QStringLiteral("seen");

    return item;
}

void ZaloClient::handleIncomingMessages(const QJsonArray& messages, bool isGroup, bool isOld) {
    if (isOld) setSyncing(false);
    m_conversationModel->beginBatch();
    struct BatchGuard {
        Models::ConversationModel* model;
        ~BatchGuard() { model->endBatch(); }
    } batchGuard{m_conversationModel};
    QHash<QString, QVector<Models::MessageItem>> byThread;
    QHash<QString, QJsonObject> batchRaws;
    QHash<QString, QVector<QPair<Models::MessageItem, QJsonObject>>> coldThreads;
    QJsonArray receiptCandidates;
    QHash<QString, QPair<QString, QString>> recalls;
    for (const QJsonValue& v : messages) {
        const QJsonObject msg = v.toObject();
        const QString target = undoTargetOf(msg);
        if (target.isEmpty()) continue;
        const QString uidFrom = idOf(msg.value("uidFrom"));
        const QString thread = isGroup ? idOf(msg.value("idTo"))
                                       : ((uidFrom == "0" || uidFrom == m_myUid) ? idOf(msg.value("idTo")) : uidFrom);
        recalls.insert(target, qMakePair(thread, msg.value("msgType").toString() == QLatin1String("chat.delete.everyone")
                                                     ? L("Tin nhắn đã bị xóa", "Message deleted")
                                                     : L("Tin nhắn đã được thu hồi", "Message recalled")));
    }

    for (const QJsonValue& v : messages) {
        const QJsonObject msg = v.toObject();
        const QJsonValue content = msg.value("content");

        if (msg.value("msgType").toString() == QLatin1String("chat.delete")) {
            handleRemoteDeletes(content.isArray() ? content.toArray() : QJsonArray{content}, isGroup);
            continue;
        }

        if (!undoTargetOf(msg).isEmpty()) continue;

        QString threadId;
        QString preview;
        Models::MessageItem item = toMessageItem(msg, isGroup, &threadId, &preview);
        if (threadId.isEmpty() || item.id.isEmpty()) continue;
        if (const auto recall = recalls.constFind(item.id); recall != recalls.cend()) {
            item.isRecalled = true;
            item.text = recall->second;
            item.messageType = "text";
            item.mediaUrl.clear();
            item.href.clear();
            preview = recall->second;
            recalls.erase(recall);
        }
        if (const qint64 expireOn = item.extra.value(QStringLiteral("expireOn")).toLongLong();
            expireOn > 0 && expireOn <= QDateTime::currentMSecsSinceEpoch()) {
            if (m_offlineStore) m_offlineStore->deleteMessage(item.id);
            continue;
        }
        if (item.messageType == QLatin1String("poll_update")) {
            refreshPoll(item.pollData.value("poll_id").toLongLong(), true);
            continue;
        }
        const bool alreadySeen = m_messageThread.contains(item.id) || m_recentIds.contains(item.id);
        rememberMessageId(item.id);
        if (!isThreadResident(threadId) && cacheUsable()) {
            coldThreads[threadId].append(qMakePair(item, msg));
        } else {
            byThread[threadId].append(item);
            batchRaws.insert(item.id, msg);
            if (!cacheUsable()) m_rawMessages.insert(item.id, msg);
            m_messageThread.insert(item.id, threadId);
            if (item.messageType == "sticker" && !item.extra.contains("sticker")) {
                resolveSticker(threadId, item.id, content.toObject().value("id").toVariant().toLongLong());
            }
        }
        if (!item.isMine) {
            m_typingUntil.remove(threadId);
            m_conversationModel->setTyping(threadId, false);
        }

        ensureConversation(threadId, isGroup);
        if (!item.isMine && !item.senderId.isEmpty() && !m_users.contains(item.senderId)) {
            m_pendingUserLookups.insert(item.senderId);
            m_userLookupTimer->start();
        }

        QString prefix;
        if (item.isMine) {
            prefix = L("Bạn: ", "You: ");
        } else if (isGroup && !item.senderName.isEmpty()) {
            prefix = item.senderName + ": ";
        }
        const bool fresh = !isOld && !item.isMine && !alreadySeen;
        if (fresh) receiptCandidates.append(msg);
        const bool quiet = isQuietMessageType(msg.value("msgType").toString());
        const bool unread = fresh && !quiet && threadId != m_currentConvId;
        bool hasMention = false;
        if (unread && !m_myUid.isEmpty()) {
            for (const auto& men : item.mentions) {
                if (men.uid == m_myUid || men.uid == QLatin1String("-1")) {
                    hasMention = true;
                    break;
                }
            }
        }
        m_conversationModel->recordMessage(threadId, preview, prefix, item.ts, unread, hasMention);
        if (fresh && quiet) {
        } else if (fresh && m_hiddenThreads.contains(threadId)) {
            if (threadId != m_currentConvId && !isThreadMuted(threadId)) markHiddenUnread(item.ts);
        } else if (fresh && !isThreadMuted(threadId)) {
            const auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
            const bool focused = app && app->focusWindow();
            if (threadId != m_currentConvId || !focused) {
                if (m_notifySound && !m_sound->isPlaying()) m_sound->play("notification", m_selectedNotiSound);
                if (m_notifyNewMessage && !focused) {
                    const QString title = isGroup ? m_groups.value(threadId).name : item.senderName;
                    showMessageNotification(threadId, title, isGroup ? item.senderName + ": " + preview : preview);
                }
            }
        }
    }

    if (m_offlineStore) m_offlineStore->beginBatch();
    for (auto it = coldThreads.cbegin(); it != coldThreads.cend(); ++it) {
        for (const auto& [item, raw] : it.value()) m_offlineStore->saveMessage(it.key(), item, raw, isGroup);
        m_offlineStore->saveConversation(m_conversationModel->conversation(it.key()));
    }
    for (auto it = byThread.begin(); it != byThread.end(); ++it) {
        mergeCachedThread(it.key());
        touchResidentThread(it.key());
        m_messageModel->mergeMessages(it.key(), it.value());
        for (const auto& item : it.value()) {
            if (hasReactions(item.id)) refreshReactionSummary(it.key(), item.id);
            if (m_offlineStore) m_offlineStore->saveMessage(it.key(), item, batchRaws.value(item.id), isGroup);
        }
        if (m_offlineStore) {
            m_offlineStore->saveConversation(m_conversationModel->conversation(it.key()));
        }
    }
    for (auto it = recalls.cbegin(); it != recalls.cend(); ++it) {
        const QString& thread = it.value().first;
        m_messageModel->markRecalled(thread, it.key(), it.value().second);
        if (isThreadResident(thread) && !m_messageModel->message(thread, it.key()).id.isEmpty()) persistMessage(thread, it.key());
        else if (m_offlineStore) m_offlineStore->markRecalled(it.key(), it.value().second);
    }
    if (m_offlineStore) m_offlineStore->endBatch();
    scheduleExpiry();
    if (!byThread.isEmpty()) evictColdThreads();
    sendDeliveredReceipts(receiptCandidates, isGroup);
    if (byThread.contains(m_currentConvId)) {
        sendSeenForCurrent();
        emit conversationStatusChanged();
    }
}

void ZaloClient::openWelcomeScreen() {
    m_currentConvId.clear();
    m_currentConvName.clear();
    m_currentConvAvatar.clear();
    m_messageModel->loadConversation(QString());
    emit currentConversationChanged();
    emit pinnedMessageChanged();
}

void ZaloClient::selectConversation(const QString& id, const QString& name, const QString& avatar) {
    m_currentConvId = id;
    const Models::ConversationItem existing = m_conversationModel->conversation(id);
    const Profile known = isGroupThread(id) ? m_groups.value(id) : m_users.value(id);
    m_currentConvName = !name.isEmpty() ? name : (!existing.name.isEmpty() ? existing.name : known.name);
    m_currentConvAvatar = !avatar.isEmpty() ? avatar : (!existing.avatarUrl.isEmpty() ? existing.avatarUrl : known.avatar);
    mergeCachedThread(id);
    touchResidentThread(id);
    evictColdThreads();
    m_messageModel->loadConversation(id);
    m_messageModel->setUnreadMarker(id, m_conversationModel->conversation(id).unreadCount);
    m_conversationModel->clearUnread(id);
    if (m_offlineStore) m_offlineStore->saveConversation(m_conversationModel->conversation(id));
    m_notifications->close(QStringLiteral("thread:") + id);
    if (m_markedUnread.contains(id)) toggleUnreadMark(id);
    m_currentLastOnline = 0;
    emit currentConversationChanged();
    emit conversationStatusChanged();
    emit pinnedMessageChanged();
    emit composerStateChanged();

    if (m_isLoggedIn && !id.isEmpty()) {
        sendSeenForCurrent();
        refreshStrangerRank(id);
        if (!currentConversationIsGroup() && id != myCloudId()) {
            m_apiClient->session()->fetchLastOnline(id, [this, id](bool ok, const QJsonValue& data, const QString&, int) {
                if (!ok || id != m_currentConvId) return;
                const QJsonObject obj = data.toObject();
                if (!obj.value("settings").toObject().value("show_online_status").toBool(true)) return;
                qint64 ts = obj.value("lastOnline").toVariant().toLongLong();
                if (ts > 0 && ts < 10000000000LL) ts *= 1000;
                m_currentLastOnline = ts > 0 && ts < kMinPlausibleEpochMs ? -1 : ts;
                emit conversationStatusChanged();
            });
        }
    }

    if (!m_isLoggedIn || m_historyLoaded.contains(id)) return;
    m_historyLoaded.insert(id);
    if (currentConversationIsGroup()) {
        m_loadingHistory = true;
        emit historyLoadingChanged();
        m_apiClient->session()->fetchGroupHistory(id, 50, [this](bool ok, const QJsonValue& data, const QString&, int) {
            m_loadingHistory = false;
            emit historyLoadingChanged();
            if (ok) handleIncomingMessages(data.toObject().value("groupMsgs").toArray(), true, true);
        });
    } else if (m_messageModel->threadSize(id) < 20) {
        loadOlderMessages();
    }
}

void ZaloClient::loadOlderMessages() {
    if (m_currentConvId.isEmpty() || m_loadingHistory) return;
    const QString convId = m_currentConvId;
    if (loadOlderFromCache(convId)) return;
    if (!m_isLoggedIn) return;
    const bool isGroup = currentConversationIsGroup();
    const bool isMyCloud = convId == myCloudId();
    ZaloSession* session = m_apiClient->session();
    if (!isGroup && !isMyCloud) {
        if (session->isRealtimeConnected()) session->requestOldMessages(false, m_messageModel->oldestMessageId(convId));
        return;
    }
    if (m_historyExhausted.contains(convId)) return;
    m_loadingHistory = true;
    emit historyLoadingChanged();
    session->fetchCloudHistory(convId, isMyCloud, m_messageModel->oldestMessageId(convId), 50,
        [this, convId, isGroup, session](bool ok, const QJsonValue& data, const QString&, int) {
            m_loadingHistory = false;
            if (!ok) {
                emit historyLoadingChanged();
                if (session->isRealtimeConnected()) {
                    session->requestOldMessages(isGroup, m_messageModel->oldestMessageId(convId));
                }
                return;
            }
            QJsonArray msgs = data.isArray() ? data.toArray() : QJsonArray();
            const QJsonObject obj = data.toObject();
            for (const char* key : {"groupMsgs", "msgs", "messages"}) {
                for (const QJsonValue& v : obj.value(key).toArray()) msgs.append(v);
            }
            const int before = m_messageModel->threadSize(convId);
            handleIncomingMessages(msgs, isGroup, true);
            if (msgs.isEmpty() || m_messageModel->threadSize(convId) == before) m_historyExhausted.insert(convId);
            emit historyLoadingChanged();
        });
}

void ZaloClient::sendMessage(const QString& text) {
    sendMessageWithMentions(text, {});
}

void ZaloClient::sendMessageWithMentions(const QString& text, const QVariantList& mentionList) {
    sendComposedMessage(text, mentionList, {});
}

void ZaloClient::sendComposedMessage(const QString& text, const QVariantList& mentionList, const QVariantList& styleList) {
    const QString cleanText = text.trimmed();
    if (cleanText.isEmpty() || m_currentConvId.isEmpty()) return;
    int lead = 0;
    while (lead < text.size() && text.at(lead).isSpace()) ++lead;
    QJsonArray mentions;
    QVector<Models::MessageMention> localMentions;
    for (const QVariant& v : mentionList) {
        const QVariantMap m = v.toMap();
        Models::MessageMention mention;
        mention.uid = m.value("uid").toString();
        mention.pos = m.value("pos").toInt() - lead;
        mention.len = m.value("len").toInt();
        if (mention.uid.isEmpty() || mention.pos < 0 || mention.len <= 0 || mention.pos + mention.len > cleanText.size()) continue;
        localMentions.append(mention);
        mentions.append(QJsonObject{{"uid", mention.uid}, {"pos", mention.pos}, {"len", mention.len}});
    }

    QJsonArray styles;
    for (const QVariant& v : styleList) {
        const QVariantMap m = v.toMap();
        const QString st = m.value("st").toString();
        int start = m.value("start").toInt() - lead;
        int end = start + m.value("len").toInt();
        start = qMax(0, start);
        end = qMin(end, int(cleanText.size()));
        if (!Models::RichComposer::isKnownStyle(st) || end <= start) continue;
        styles.append(QJsonObject{{"start", start}, {"len", end - start}, {"st", st}});
    }
    const QJsonObject quoted = m_replyTarget.isEmpty() ? QJsonObject()
                                                       : rawMessage(m_replyTarget.value("id").toString());
    sendTextTo(m_currentConvId, currentConversationIsGroup(), cleanText, localMentions, mentions, quoted, styles);
}

void ZaloClient::sendTextTo(const QString& convId, bool isGroup, const QString& cleanText,
                            const QVector<Models::MessageMention>& localMentions, const QJsonArray& mentions,
                            const QJsonObject& quoted, const QJsonArray& styles) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Models::MessageItem local;
    local.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_localSeq);
    m_lastClientId = qMax(now, m_lastClientId + 1);
    local.cliMsgId = QString::number(m_lastClientId);
    local.text = cleanText;
    local.ts = now;
    local.timeStr = QDateTime::fromMSecsSinceEpoch(now).toString("HH:mm");
    local.isMine = true;
    local.senderId = m_myUid;
    local.senderName = m_userName;
    local.senderAvatar = m_userAvatar;
    local.status = QStringLiteral("sending");
    if (isGroup) local.mentions = localMentions;
    if (!styles.isEmpty()) local.extra.insert("styles", styles.toVariantList());
    if (!quoted.isEmpty()) {
        local.quoteMsgId = m_replyTarget.value("id").toString();
        local.quoteSender = m_replyTarget.value("senderName").toString();
        local.quoteText = m_replyTarget.value("text").toString();
    }
    m_messageModel->mergeMessages(convId, {local});
    m_conversationModel->recordMessage(convId, cleanText, L("Bạn: ", "You: "), now, false);
    setDraft(convId, QString());
    if (convId == m_currentConvId) m_messageModel->clearUnreadMarker();
    if (!quoted.isEmpty()) clearReplyTarget();
    emit messageSent(cleanText);

    if (!m_isLoggedIn) {
        m_messageModel->setStatus(convId, local.id, QStringLiteral("failed"));
        if (m_isOfflineMode) {
            emit notice(L("Đang ngoại tuyến nên chưa gửi được tin nhắn. Hãy gửi lại khi đã kết nối.",
                          "You're offline, so the message wasn't sent. Send it again once connected."));
        }
        return;
    }
    sendTextNow(convId, isGroup, cleanText, isGroup ? mentions : QJsonArray(), quoted, local.id, local.cliMsgId, styles);
}

void ZaloClient::sendTextNow(const QString& convId, bool isGroup, const QString& text, const QJsonArray& mentions,
                             const QJsonObject& quoted, const QString& localId, const QString& cliMsgId,
                             const QJsonArray& styles) {
    auto done = [this, convId, localId, text](bool ok, const QJsonValue& data, const QString& error, int code) {
        if (ok) {
            const QString serverId = idOf(data.toObject().value("msgId"));
            m_messageModel->setStatus(convId, localId, QStringLiteral("sent"));
            if (!serverId.isEmpty()) m_messageModel->confirmMessage(convId, localId, serverId);
        } else {
            m_messageModel->setStatus(convId, localId, QStringLiteral("failed"));
            if (code == 164 && isGroupThread(convId)) {
                markThreadReadOnly(convId, QStringLiteral("removed"), QDateTime::currentMSecsSinceEpoch());
                return;
            }
            emit notice(L("Không gửi được tin nhắn (%1): ", "Message not sent (%1): ").arg(friendlyError(error, code))
                        + text.left(40));
        }
    };
    ZaloSession* session = m_apiClient->session();
    const qint64 ttl = m_autoDeleteTtl.value(convId, 0);
    if (!quoted.isEmpty()) {
        session->sendQuote(convId, isGroup, text, quoted, done, cliMsgId, styles, mentions, ttl);
        return;
    }
    if (!mentions.isEmpty()) {
        session->sendMention(convId, text, mentions, done, cliMsgId, styles, ttl);
        return;
    }
    static const QRegularExpression urlRe(QStringLiteral(R"(https?://[^\s<>"']+)"));
    const QRegularExpressionMatch url = urlRe.match(text);
    if (!url.hasMatch() || !styles.isEmpty()) {
        session->sendText(convId, isGroup, text, done, cliMsgId, styles, ttl);
        return;
    }
    QString link = url.captured(0);
    while (!link.isEmpty() && QStringLiteral(".,;:!?)]}").contains(link.back())) link.chop(1);
    QPointer<ZaloSession> guard(session);
    session->parseLink(link, [guard, convId, isGroup, text, done, cliMsgId, ttl](bool ok, const QJsonValue& data,
                                                                           const QString&, int) {
        if (!guard) return;
        const QJsonObject preview = data.toObject().value("data").toObject();
        if (!ok || preview.value("href").toString().isEmpty()) {
            guard->sendText(convId, isGroup, text, done, cliMsgId, QJsonArray(), ttl);
            return;
        }
        guard->sendLink(convId, isGroup, text, preview, done, cliMsgId, ttl);
    });
}

void ZaloClient::setReplyTarget(const QString& msgId) {
    const QVariantMap msg = m_messageModel->getMessageById(msgId);
    if (msg.isEmpty() || rawMessage(msgId).isEmpty()) {
        clearReplyTarget();
        return;
    }
    QString text = msg.value("text").toString();
    if (text.isEmpty()) {
        const QString type = msg.value("messageType").toString();
        text = type == "image" ? L("[Hình ảnh]", "[Photo]") : type == "sticker" ? L("[Nhãn dán]", "[Sticker]")
             : type == "file" ? L("[Tệp]", "[File]") : L("[Tin nhắn]", "[Message]");
    }
    QString sender = msg.value("isMine").toBool() ? L("Bạn", "You") : msg.value("senderName").toString();
    if (sender.isEmpty()) sender = m_users.value(msg.value("senderId").toString()).name;
    if (sender.isEmpty() && !currentConversationIsGroup()) sender = m_currentConvName;
    m_replyTarget = {{"id", msgId}, {"senderName", sender}, {"text", text}};
    emit replyTargetChanged();
}

void ZaloClient::clearReplyTarget() {
    if (m_replyTarget.isEmpty()) return;
    m_replyTarget.clear();
    emit replyTargetChanged();
}

void ZaloClient::sendAttachment(const QString& type, const QString& name, const QString& url) {
    Q_UNUSED(type);
    Q_UNUSED(name);
    if (url.isEmpty()) {
        emit notice(L("Hãy chọn một tệp để gửi.", "Choose a file to send."));
        return;
    }
    sendFile(url);
}

bool ZaloClient::currentConversationIsGroup() const {
    return !m_currentConvId.isEmpty() &&
           (m_groupIds.contains(m_currentConvId) || m_conversationModel->conversation(m_currentConvId).isGroup);
}

int ZaloClient::currentConversationMemberCount() const {
    return m_groups.value(m_currentConvId).memberCount;
}

bool ZaloClient::currentConversationPinned() const {
    return m_conversationModel->conversation(m_currentConvId).isPinned;
}

bool ZaloClient::currentConversationMuted() const {
    return m_mutedIds.contains(m_currentConvId);
}

QString ZaloClient::myCloudId() const {
    return m_apiClient->session()->send2meId();
}

void ZaloClient::openMyCloud() {
    const QString id = myCloudId();
    if (id.isEmpty()) {
        emit notice(L("My Documents chưa sẵn sàng.", "My Documents isn't available yet."));
        return;
    }
    openConversationWithContact(id, L("My Documents", "My Documents"), QString());
}

QVariantList ZaloClient::cloudSharedItems(const QString& kind) const {
    const QString cloudId = myCloudId();
    if (cloudId.isEmpty() || !m_isLoggedIn) {
        return {};
    }

    auto tagList = [](QVariantList list, const QString& type) {
        for (auto& item : list) {
            QVariantMap m = item.toMap();
            m[QStringLiteral("itemType")] = type;
            if (m.value(QStringLiteral("title")).toString().isEmpty()) {
                m[QStringLiteral("title")] = (type == QLatin1String("media"))
                    ? (m.value(QStringLiteral("isVideo")).toBool() ? QStringLiteral("Video") : QStringLiteral("Hình ảnh"))
                    : (type == QLatin1String("file") ? QStringLiteral("Tập tin") : QStringLiteral("Liên kết"));
            }
            item = m;
        }
        return list;
    };

    if (m_currentConvId == cloudId && m_messageModel) {
        if (kind == QLatin1String("media")) return tagList(m_messageModel->sharedItems(QStringLiteral("media"), 100), QStringLiteral("media"));
        if (kind == QLatin1String("file")) return tagList(m_messageModel->sharedItems(QStringLiteral("file"), 100), QStringLiteral("file"));
        if (kind == QLatin1String("link")) return tagList(m_messageModel->sharedItems(QStringLiteral("link"), 100), QStringLiteral("link"));
        QVariantList all;
        all.append(tagList(m_messageModel->sharedItems(QStringLiteral("media"), 50), QStringLiteral("media")));
        all.append(tagList(m_messageModel->sharedItems(QStringLiteral("file"), 50), QStringLiteral("file")));
        all.append(tagList(m_messageModel->sharedItems(QStringLiteral("link"), 50), QStringLiteral("link")));
        std::sort(all.begin(), all.end(), [](const QVariant& a, const QVariant& b) {
            return a.toMap().value("ts").toLongLong() > b.toMap().value("ts").toLongLong();
        });
        return all;
    }

    if (!m_offlineStore) return {};
    const auto msgs = m_offlineStore->loadMessages(cloudId, nullptr, 300);
    static const QRegularExpression urlRegex(QStringLiteral(R"(https?://[^\s<>"']+)"));

    QVariantList mediaList;
    QVariantList fileList;
    QVariantList linkList;

    for (int i = msgs.size() - 1; i >= 0; --i) {
        const auto& m = msgs.at(i);
        if (m.isRecalled) continue;
        QVariantMap e{
            {QStringLiteral("msgId"), m.id},
            {QStringLiteral("senderName"), m.senderName},
            {QStringLiteral("ts"), m.ts},
            {QStringLiteral("timeStr"), m.timeStr}
        };

        if (m.messageType == QLatin1String("image") || m.messageType == QLatin1String("video")) {
            if (m.mediaUrl.isEmpty()) continue;
            e[QStringLiteral("itemType")] = QStringLiteral("media");
            e[QStringLiteral("thumb")] = m.mediaUrl;
            e[QStringLiteral("url")] = m.href.isEmpty() ? m.mediaUrl : m.href;
            const bool isVid = (m.messageType == QLatin1String("video"));
            e[QStringLiteral("isVideo")] = isVid;
            e[QStringLiteral("title")] = m.text.isEmpty() ? (isVid ? QStringLiteral("Video") : QStringLiteral("Hình ảnh")) : m.text;
            mediaList.append(e);
        } else if (m.messageType == QLatin1String("file")) {
            e[QStringLiteral("itemType")] = QStringLiteral("file");
            e[QStringLiteral("title")] = m.text.isEmpty() ? QStringLiteral("Tập tin") : m.text;
            e[QStringLiteral("url")] = m.href.isEmpty() ? m.mediaUrl : m.href;
            e[QStringLiteral("fileSize")] = m.fileSize;
            fileList.append(e);
        } else if (m.messageType == QLatin1String("link")) {
            e[QStringLiteral("itemType")] = QStringLiteral("link");
            e[QStringLiteral("url")] = m.href;
            e[QStringLiteral("title")] = m.text.isEmpty() ? m.href : m.text;
            e[QStringLiteral("thumb")] = m.mediaUrl;
            linkList.append(e);
        } else {
            const auto match = urlRegex.match(m.text);
            if (match.hasMatch()) {
                QString link = match.captured(0);
                while (!link.isEmpty() && QStringLiteral(".,;:!?)]}").contains(link.back())) link.chop(1);
                e[QStringLiteral("itemType")] = QStringLiteral("link");
                e[QStringLiteral("url")] = link;
                e[QStringLiteral("title")] = link;
                linkList.append(e);
            }
        }
    }

    if (kind == QLatin1String("media")) return mediaList;
    if (kind == QLatin1String("file")) return fileList;
    if (kind == QLatin1String("link")) return linkList;

    QVariantList all = mediaList;
    all.append(fileList);
    all.append(linkList);
    std::sort(all.begin(), all.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("ts").toLongLong() > b.toMap().value("ts").toLongLong();
    });
    return all;
}

void ZaloClient::openConversationWithContact(const QString& id, const QString& name, const QString& avatarUrl) {
    const bool isGroup = isGroupThread(id);
    if (!m_conversationModel->contains(id)) {
        ensureConversation(id, isGroup);
        m_conversationModel->updateProfile(id, name, avatarUrl);
    }
    setCurrentNavigationTab("messages");
    selectConversation(id, name, avatarUrl);
}

void ZaloClient::findUserByPhone(const QString& phone) {
    if (!m_isLoggedIn) {
        emit userSearchFinished(false, {}, L("Bạn cần đăng nhập trước.", "Please sign in first."));
        return;
    }
    m_apiClient->session()->findUserByPhone(phone, [this](bool ok, const QJsonValue& data, const QString& error, int code) {
        const QJsonObject obj = data.toObject();
        const QString uid = idOf(obj.value("uid"));
        if (!ok || uid.isEmpty()) {
            emit userSearchFinished(false, {}, code == 216 || ok
                ? L("Số điện thoại chưa đăng ký Zalo hoặc không cho phép tìm kiếm.",
                    "This number isn't on Zalo or doesn't allow search.")
                : L("Không tìm được: ", "Search failed: ") + friendlyError(error));
            return;
        }
        QVariantMap profile;
        profile["uid"] = uid;
        profile["name"] = pickName(obj);
        profile["avatar"] = obj.value("avatar").toString();
        m_users[uid] = {profile["name"].toString(), profile["avatar"].toString(), 0};
        noteAccountFlags(uid, obj);
        emit userSearchFinished(true, profile, QString());
    });
}

void ZaloClient::sendFriendRequest(const QString& uid, const QString& message) {
    m_apiClient->session()->sendFriendRequest(uid, message, [this](bool ok, const QJsonValue&, const QString& error, int) {
        emit friendRequestFinished(ok, ok ? L("Đã gửi lời mời kết bạn.", "Friend request sent.")
                                          : L("Không gửi được lời mời: ", "Couldn't send the request: ") + friendlyError(error));
    });
}

void ZaloClient::createGroup(const QString& name, const QStringList& members) {
    if (members.size() < 2) {
        emit groupCreateFinished(false, L("Nhóm cần ít nhất 3 thành viên (gồm cả bạn).",
                                          "A group needs at least 3 members including you."));
        return;
    }
    m_apiClient->session()->createGroup(name, members,
        [this, name](bool ok, const QJsonValue& data, const QString& error, int) {
            if (!ok) {
                emit groupCreateFinished(false, L("Không tạo được nhóm: ", "Couldn't create the group: ") + friendlyError(error));
                return;
            }
            const QString groupId = idOf(data.toObject().value("groupId"));
            emit groupCreateFinished(true, L("Đã tạo nhóm.", "Group created."));
            if (!groupId.isEmpty()) {
                m_groupIds.insert(groupId);
                m_groups[groupId] = {name, QString(), 0};
                loadGroupInfo({groupId});
                openConversationWithContact(groupId, name, QString());
            }
        });
}

void ZaloClient::pinConversation(const QString& id) {
    if (id.isEmpty()) return;
    const bool pinned = !m_conversationModel->conversation(id).isPinned;
    const bool isGroup = isGroupThread(id);
    m_apiClient->session()->apiPost("conversation", "/api/pinconvers/updatev2",
        {{"actionType", pinned ? 1 : 2}, {"conversations", QJsonArray{(isGroup ? "g" : "u") + id}}},
        [this, id, pinned](bool ok, const QJsonValue&, const QString&, int) {
            if (!ok) {
                emit notice(L("Không thể cập nhật ghim hội thoại.", "Couldn't update the pinned state."));
                return;
            }
            m_conversationModel->setPinned(id, pinned);
            emit currentConversationChanged();
        });
}

void ZaloClient::muteConversation(const QString& id) {
    if (id.isEmpty()) return;
    muteConversationFor(id, isThreadMuted(id) ? 0 : -1);
}

void ZaloClient::muteConversationFor(const QString& id, int seconds) {
    if (id.isEmpty()) return;
    qint64 duration = seconds;
    if (seconds == -2) {
        const QDateTime now = QDateTime::currentDateTime();
        QDateTime next(now.date(), QTime(8, 0));
        if (now.time().hour() >= 8) next = next.addDays(1);
        duration = qMax<qint64>(60, now.secsTo(next));
    }
    const bool mute = duration != 0;
    m_apiClient->session()->setMute(id, isGroupThread(id), duration,
        [this, id, mute, duration](bool ok, const QJsonValue&, const QString& error, int code) {
            if (!ok) {
                emit notice(L("Không thể thay đổi thông báo của hội thoại: ", "Couldn't change notifications for this chat: ")
                            + friendlyError(error, code));
                return;
            }
            if (mute) {
                m_mutedIds.insert(id);
                m_muteUntil.insert(id, duration > 0 ? QDateTime::currentMSecsSinceEpoch() + duration * 1000 : 0);
            } else {
                m_mutedIds.remove(id);
                m_muteUntil.remove(id);
            }
            m_conversationModel->setMuted(id, mute);
            emit currentConversationChanged();
        });
}

bool ZaloClient::isThreadMuted(const QString& threadId) {
    if (!m_mutedIds.contains(threadId)) return false;
    const qint64 until = m_muteUntil.value(threadId, 0);
    if (until > 0 && until <= QDateTime::currentMSecsSinceEpoch()) {
        m_mutedIds.remove(threadId);
        m_muteUntil.remove(threadId);
        m_conversationModel->setMuted(threadId, false);
        return false;
    }
    return true;
}

void ZaloClient::toggleUnreadMark(const QString& id) {
    if (id.isEmpty() || !m_isLoggedIn) return;
    const bool unread = !m_markedUnread.contains(id);
    m_apiClient->session()->setUnreadMark(id, isGroupThread(id), unread,
        [this, id, unread](bool ok, const QJsonValue&, const QString& error, int code) {
            if (!ok) {
                emit notice(L("Không thể đánh dấu hội thoại: ", "Couldn't mark the conversation: ")
                            + friendlyError(error, code));
                return;
            }
            if (unread) {
                m_markedUnread.insert(id);
                if (m_conversationModel->conversation(id).unreadCount == 0) m_conversationModel->setUnreadCount(id, 1);
            } else {
                m_markedUnread.remove(id);
                m_conversationModel->clearUnread(id);
            }
        });
}

void ZaloClient::deleteConversation(const QString& id) {
    if (id.isEmpty()) return;
    if (m_readOnlyThreads.contains(id)) {
        if (id == m_currentConvId) openWelcomeScreen();
        m_messageModel->clearThread(id);
        m_conversationModel->removeConversation(id);
        m_historyLoaded.remove(id);
        m_markedUnread.remove(id);
        m_groupIds.remove(id);
        m_groups.remove(id);
        m_groupRosters.remove(id);
        if (m_offlineStore) m_offlineStore->deleteConversation(id);
        m_readOnlyThreads.remove(id);
        saveReadOnlyThreads();
        rebuildContacts();
        emit notice(L("Đã xoá hội thoại.", "Conversation deleted."));
        return;
    }
    if (!m_isLoggedIn) return;
    const QJsonObject last = rawMessage(m_messageModel->latestMessageId(id));
    if (last.isEmpty()) {
        emit notice(L("Chưa tải được tin nhắn mới nhất của hội thoại nên chưa thể xoá. Hãy mở hội thoại rồi thử lại.",
                      "The latest message of this chat isn't loaded yet. Open the chat and try again."));
        return;
    }
    m_apiClient->session()->deleteConversation(id, isGroupThread(id), last,
        [this, id](bool ok, const QJsonValue&, const QString& error, int code) {
            if (!ok) {
                emit notice(L("Không thể xoá hội thoại: ", "Couldn't delete the conversation: ") + friendlyError(error, code));
                return;
            }
            if (id == m_currentConvId) openWelcomeScreen();
            m_messageModel->clearThread(id);
            m_conversationModel->removeConversation(id);
            m_historyLoaded.remove(id);
            m_markedUnread.remove(id);
            if (m_offlineStore) m_offlineStore->deleteConversation(id);
            emit notice(L("Đã xoá hội thoại.", "Conversation deleted."));
        });
}

bool ZaloClient::loadOfflineCache() {
    if (!m_offlineStore || m_cachedUid.isEmpty() || m_offlineStore->ownerUid() != m_cachedUid) return false;
    if (m_myUid.isEmpty()) m_myUid = m_cachedUid;
    loadDrafts();
    for (const auto& p : m_offlineStore->loadProfiles()) {
        if (p.isGroup) {
            m_groups[p.id] = {p.name, p.avatar, p.memberCount};
            m_groupIds.insert(p.id);
        } else {
            if (!m_users.contains(p.id)) m_users[p.id] = {p.name, p.avatar, 0};
            if (p.isFriend) {
                m_friendIds.insert(p.id);
                m_friendsKnown = true;
            }
            if (p.isOfficial) m_officialIds.insert(p.id);
        }
    }
    const auto convs = m_offlineStore->loadConversations();
    m_conversationModel->beginBatch();
    struct BatchGuard {
        Models::ConversationModel* model;
        ~BatchGuard() { model->endBatch(); }
    } batchGuard{m_conversationModel};
    for (const auto& c : convs) {
        Models::ConversationItem item = c;
        if (item.name.compare(QLatin1String("Cloud của tôi"), Qt::CaseInsensitive) == 0
            || item.name.compare(QLatin1String("My Cloud"), Qt::CaseInsensitive) == 0
            || item.name.compare(QLatin1String("Truyền File"), Qt::CaseInsensitive) == 0) {
            item.name = L("My Documents", "My Documents");
        }
        item.isOnline = false;
        if (item.isMuted) m_mutedIds.insert(item.id);
        if (item.isGroup) m_groupIds.insert(item.id);
        m_conversationModel->upsertConversation(item);
    }
    rebuildContacts();
    purgeExpiredMessages();
    migrateCachedParses();
    return !convs.isEmpty();
}

void ZaloClient::migrateCachedParses() {
    if (!m_offlineStore) return;
    const QString version = QString::number(kCacheParseVersion);
    if (m_offlineStore->metaValue(QStringLiteral("parse_version")) == version) return;
    const QSet<QString> touched = m_offlineStore->reparseStored(
        [this](const QJsonObject& raw, bool isGroup, Models::MessageItem* out) {
            if (!isReparsedOnLoad(raw.value("msgType").toString()) && !raw.contains("quote")
                && !raw.contains("mentions")) return false;
            QString thread, preview;
            *out = toMessageItem(raw, isGroup, &thread, &preview);
            return out->messageType != QLatin1String("poll_update");
        });
    for (const QString& convId : touched) {
        const auto latest = m_offlineStore->loadMessages(convId, nullptr, 1, 0);
        if (!latest.isEmpty()) m_conversationModel->updateLastMessage(convId, latest.first().text, latest.first().timeStr);
    }
    m_offlineStore->setMetaValue(QStringLiteral("parse_version"), version);
}

void ZaloClient::purgeExpiredMessages() {
    if (!m_offlineStore) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> touched;
    for (const auto& [convId, msgId] : m_offlineStore->purgeExpired(now)) {
        m_messageModel->removeMessage(convId, msgId);
        touched.insert(convId);
    }
    for (const QString& convId : std::as_const(touched)) {
        const auto latest = m_offlineStore->loadMessages(convId, nullptr, 1, 0);
        m_conversationModel->updateLastMessage(convId, latest.isEmpty() ? QString() : latest.first().text,
                                               latest.isEmpty() ? QString() : latest.first().timeStr);
    }
    scheduleExpiry();
}

void ZaloClient::scheduleExpiry() {
    if (!m_offlineStore) return;
    if (!m_expiryTimer) {
        m_expiryTimer = new QTimer(this);
        m_expiryTimer->setSingleShot(true);
        connect(m_expiryTimer, &QTimer::timeout, this, &ZaloClient::purgeExpiredMessages);
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 next = m_offlineStore->nextExpiry(now);
    if (next <= 0) {
        m_expiryTimer->stop();
        return;
    }
    m_expiryTimer->start(int(qBound<qint64>(1000, next - now + 500, 6LL * 60 * 60 * 1000)));
}

void ZaloClient::mergeCachedThread(const QString& convId) {
    if (!m_offlineStore || convId.isEmpty() || m_cacheMerged.contains(convId)) return;
    if (m_offlineStore->ownerUid().isEmpty() || m_offlineStore->ownerUid() != (m_myUid.isEmpty() ? m_cachedUid : m_myUid)) {
        return;
    }
    m_cacheMerged.insert(convId);
    QHash<QString, QJsonObject> raws;
    const auto cached = m_offlineStore->loadMessages(convId, &raws, kCachePageRows);
    mergeCacheRows(convId, cached, raws);
}

bool ZaloClient::loadOlderFromCache(const QString& convId) {
    if (!cacheUsable() || !m_cacheMerged.contains(convId)) return false;
    const qint64 oldest = m_messageModel->oldestTimestamp(convId);
    if (oldest <= 0) return false;
    QHash<QString, QJsonObject> raws;
    const auto rows = m_offlineStore->loadMessages(convId, &raws, kCachePageRows + 1, oldest);
    const int before = m_messageModel->threadSize(convId);
    mergeCacheRows(convId, rows, raws);
    return m_messageModel->threadSize(convId) > before;
}

QJsonObject ZaloClient::rawMessage(const QString& msgId) const {
    if (msgId.isEmpty()) return {};
    const auto it = m_rawMessages.constFind(msgId);
    if (it != m_rawMessages.constEnd()) return it.value();
    if (!m_messageThread.contains(msgId) || !cacheUsable()) return {};
    return m_offlineStore->rawMessage(msgId);
}

void ZaloClient::mergeCacheRows(const QString& convId, QVector<Models::MessageItem> rows,
                                const QHash<QString, QJsonObject>& raws) {
    for (const auto& row : rows) m_messageThread.insert(row.id, convId);
    if (rows.isEmpty()) return;
    const bool isGroup = isGroupThread(convId);
    for (Models::MessageItem& row : rows) {
        const QJsonObject raw = raws.value(row.id);
        if (!raw.isEmpty() && !row.isRecalled) {
            const QJsonArray seenUids = raw.value(QLatin1String("seenUids")).toArray();
            for (const QJsonValue& v : seenUids) {
                const QString u = idOf(v);
                if (!u.isEmpty() && u != QLatin1String("0") && !row.seenUids.contains(u)) row.seenUids.append(u);
            }
            const QString type = raw.value("msgType").toString();
            if (raw.contains("mentions") || raw.contains("quote") || isReparsedOnLoad(type)) {
                QString thread, preview;
                Models::MessageItem rebuilt = toMessageItem(raw, isGroup, &thread, &preview);
                if (rebuilt.messageType == QLatin1String("poll_update")) continue;
                rebuilt.id = row.id;
                rebuilt.status = row.status;
                rebuilt.reaction = row.reaction;
                for (const QString& u : row.seenUids) {
                    if (!rebuilt.seenUids.contains(u)) rebuilt.seenUids.append(u);
                }
                row = rebuilt;
            }
        }
        if (!row.seenUids.isEmpty() && row.isMine && row.status.isEmpty()) row.status = QStringLiteral("seen");
    }
    m_messageModel->mergeMessages(convId, rows);
    for (const auto& item : rows) {
        if (hasReactions(item.id)) refreshReactionSummary(convId, item.id);
        if (item.messageType == "sticker" && !item.extra.contains("sticker")) {
            const qint64 stickerId = raws.value(item.id).value("content").toObject().value("id").toVariant().toLongLong();
            if (stickerId > 0) resolveSticker(convId, item.id, stickerId);
        }
    }
}

bool ZaloClient::cacheUsable() const {
    if (!m_offlineStore) return false;
    const QString owner = m_offlineStore->ownerUid();
    return !owner.isEmpty() && owner == (m_myUid.isEmpty() ? m_cachedUid : m_myUid);
}

bool ZaloClient::isThreadResident(const QString& convId) const {
    return convId == m_currentConvId || m_cacheMerged.contains(convId) || m_messageModel->threadSize(convId) > 0;
}

void ZaloClient::touchResidentThread(const QString& convId) {
    if (convId.isEmpty()) return;
    m_residentThreads.removeAll(convId);
    m_residentThreads.append(convId);
}

void ZaloClient::evictColdThreads(int maxResident) {
    if (!cacheUsable() || m_residentThreads.size() <= maxResident) return;
    QSet<QString> evicted;
    for (int i = 0; i < m_residentThreads.size() && m_residentThreads.size() - evicted.size() > maxResident; ++i) {
        const QString t = m_residentThreads.at(i);
        if (t == m_currentConvId || m_messageModel->hasLocalEcho(t)) continue;
        evicted.insert(t);
    }
    if (evicted.isEmpty()) return;
    for (auto it = m_messageThread.begin(); it != m_messageThread.end();) {
        if (evicted.contains(it.value())) {
            m_rawMessages.remove(it.key());
            it = m_messageThread.erase(it);
        } else {
            ++it;
        }
    }
    for (const QString& t : evicted) {
        m_messageModel->clearThread(t);
        m_cacheMerged.remove(t);
        m_residentThreads.removeAll(t);
    }
    releaseFreedMemory();
}

void ZaloClient::trimMemoryForBackground() {
    if (cacheUsable()) {
        m_rawMessages.clear();
        m_rawMessages.squeeze();
        evictColdThreads(m_currentConvId.isEmpty() ? 0 : 1);
    }
    if (m_storage) {
        m_storage->cleanAvatarMemory();
        Platform::StorageManager::cleanTemporaryFiles();
    }
    m_typingUser.clear();
    m_typingUntil.clear();
    releaseFreedMemory();
}

void ZaloClient::rememberMessageId(const QString& msgId) {
    constexpr int kMaxRecentIds = 5000;
    if (msgId.isEmpty() || m_recentIds.contains(msgId)) return;
    m_recentIds.insert(msgId);
    m_recentIdOrder.append(msgId);
    while (m_recentIdOrder.size() > kMaxRecentIds) m_recentIds.remove(m_recentIdOrder.takeFirst());
}

void ZaloClient::releaseFreedMemory() {
    if (!m_trimTimer) {
        m_trimTimer = new QTimer(this);
        m_trimTimer->setSingleShot(true);
        m_trimTimer->setInterval(2000);
        connect(m_trimTimer, &QTimer::timeout, this, []() {
#ifdef __GLIBC__
            malloc_trim(0);
#endif
        });
    }
    m_trimTimer->start();
}

void ZaloClient::persistMessage(const QString& convId, const QString& msgId) {
    if (!m_offlineStore || convId.isEmpty() || msgId.isEmpty()) return;
    const Models::MessageItem item = m_messageModel->message(convId, msgId);
    if (item.id.isEmpty()) return;
    m_offlineStore->saveMessage(convId, item, m_rawMessages.value(msgId), isGroupThread(convId));
}

void ZaloClient::persistProfile(const QString& id, bool isGroup) {
    if (!m_offlineStore || id.isEmpty() || m_myUid.isEmpty()) return;
    const Profile p = isGroup ? m_groups.value(id) : m_users.value(id);
    m_offlineStore->saveProfile({id, p.name, p.avatar, isGroup, p.memberCount, !isGroup && m_friendIds.contains(id),
                                 !isGroup && m_officialIds.contains(id)});
}

void ZaloClient::fetchBytes(const QString& url, std::function<void(const QByteArray&, const QString&)> done) {
    const QUrl u(url);
    if (u.isLocalFile()) {
        QFile f(u.toLocalFile());
        if (!f.open(QIODevice::ReadOnly)) {
            done({}, f.errorString());
            return;
        }
        done(f.readAll(), QString());
        return;
    }
    QNetworkReply* reply = m_apiClient->session()->getWithCookies(u);
    connect(reply, &QNetworkReply::finished, this, [reply, done]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            done({}, http > 0 ? QStringLiteral("HTTP %1").arg(http) : reply->errorString());
            return;
        }
        done(reply->readAll(), QString());
    });
}

QString ZaloClient::suggestedMediaFileName(const QString& url) const {
    const QUrl u(url);
    QString name = u.fileName();
    if (name.isEmpty() || !name.contains('.') || name.compare(QLatin1String("renewMedia"), Qt::CaseInsensitive) == 0) {
        const QString lower = url.toLower();
        const bool isVid = lower.contains(QLatin1String("video")) || lower.contains(QLatin1String(".mp4"))
                           || lower.contains(QLatin1String(".mov")) || lower.contains(QLatin1String(".webm"));
        name = QStringLiteral("zalo-%1.%2")
                   .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"),
                        isVid ? QStringLiteral("mp4") : QStringLiteral("jpg"));
    }
    return name;
}

void ZaloClient::saveMediaTo(const QString& url, const QString& destinationFileUrl) {
    const QString dest = QUrl(destinationFileUrl).isLocalFile() ? QUrl(destinationFileUrl).toLocalFile() : destinationFileUrl;
    if (url.isEmpty() || dest.isEmpty()) return;
    fetchBytes(url, [this, dest](const QByteArray& data, const QString& error) {
        QSaveFile out(dest);
        if (!error.isEmpty() || data.isEmpty() || !out.open(QIODevice::WriteOnly) || out.write(data) != data.size()
            || !out.commit()) {
            emit notice(L("Không lưu được tệp đa phương tiện", "Couldn't save the media file")
                        + (error.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(error)));
            return;
        }
        emit notice(L("Đã lưu vào ", "Saved to ") + dest);
    });
}

void ZaloClient::copyImage(const QString& url) {
    fetchBytes(url, [this](const QByteArray& data, const QString& error) {
        QImage image;
        if (error.isEmpty()) image.loadFromData(data);
        if (image.isNull()) {
            emit notice(L("Không sao chép được ảnh.", "Couldn't copy the picture."));
            return;
        }
        QGuiApplication::clipboard()->setImage(image);
        emit notice(L("Đã sao chép ảnh.", "Picture copied."));
    });
}

void ZaloClient::openFileMessage(const QString& url, const QString& fileName) {
    openRemoteFile(url, fileName, L("File không còn tồn tại trên hệ thống", "The file no longer exists on the server"));
}

bool ZaloClient::isGoneError(const QString& error) {
    return error == QLatin1String("HTTP 403") || error == QLatin1String("HTTP 404") || error == QLatin1String("HTTP 410");
}

void ZaloClient::openRemoteFile(const QString& url, const QString& fileName, const QString& goneText) {
    if (url.isEmpty()) {
        emit notice(L("Tệp này không còn đường dẫn tải.", "This file has no download link."));
        return;
    }
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath("Zalos");
    QDir().mkpath(dir);
    QString safe = QFileInfo(fileName).fileName();
    if (safe.isEmpty()) safe = suggestedMediaFileName(url);
    const QString path = QDir(dir).filePath(safe);
    if (QFileInfo::exists(path)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return;
    }
    emit notice(L("Đang tải ", "Downloading ") + safe + "...");
    fetchBytes(url, [this, path, goneText](const QByteArray& data, const QString& error) {
        if (isGoneError(error)) {
            emit notice(goneText);
            return;
        }
        QSaveFile out(path);
        if (!error.isEmpty() || !out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
            emit notice(L("Tải tệp thất bại", "Download failed") + (error.isEmpty() ? QString() : " (" + error + ")"));
            return;
        }
        emit notice(L("Đã tải về ", "Downloaded to ") + path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
}

QString ZaloClient::undoTargetOf(const QJsonObject& msg) {
    const QString type = msg.value("msgType").toString();
    const QJsonValue raw = msg.value("content");
    const QJsonObject content = raw.isString() ? QJsonDocument::fromJson(raw.toString().toUtf8()).object() : raw.toObject();
    const bool undo = type == QLatin1String("chat.undo") || type == QLatin1String("chat.delete.everyone")
                      || content.contains(QLatin1String("deleteMsg"));
    if (!undo) return {};
    for (const char* key : {"globalMsgId", "msgId", "cliMsgId"}) {
        const QString id = idOf(content.value(QLatin1String(key)));
        if (!id.isEmpty() && id != "0" && id != idOf(msg.value("msgId"))) return id;
    }
    return {};
}

bool ZaloClient::isReparsedOnLoad(const QString& msgType) {
    static const QSet<QString> types{
        QStringLiteral("chat.recommended"), QStringLiteral("chat.voice"), QStringLiteral("group.poll"),
        QStringLiteral("chat.link"), QStringLiteral("chat.webcontent"), QStringLiteral("chat.webcontent.v2"),
        QStringLiteral("chat.ecard"), QStringLiteral("chat.list.action"), QStringLiteral("chat.location.new"),
        QStringLiteral("chat.undo"), QStringLiteral("chat.delete.everyone")};
    return types.contains(msgType);
}

bool ZaloClient::isQuietMessageType(const QString& msgType) {
    static const QSet<QString> quiet{QStringLiteral("chat.webcontent"), QStringLiteral("chat.webcontent.v2"),
                                     QStringLiteral("chat.livelocation"), QStringLiteral("chat.video.live.msg"),
                                     QStringLiteral("chat.list.action")};
    return quiet.contains(msgType);
}

QString ZaloClient::fillHighlights(const QString& text, const QJsonArray& highlights) const {
    static const QRegularExpression placeholder(QStringLiteral("%(\\d+)\\$s"));
    if (!text.contains(QLatin1Char('%'))) return text;
    QString out;
    qsizetype last = 0;
    auto it = placeholder.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        const int index = m.captured(1).toInt() - 1;
        if (index < 0 || index >= highlights.size()) continue;
        const QJsonObject h = highlights.at(index).toObject();
        const QString uid = idOf(h.value("uid"));
        QString name;
        if (!uid.isEmpty() && uid == m_myUid) name = L("Bạn", "You");
        if (name.isEmpty() && !uid.isEmpty()) name = displayNameOf(uid);
        if (name.isEmpty()) name = h.value("dpn").toString();
        out += name;
    }
    out += text.mid(last);
    return out.simplified();
}

QString ZaloClient::displayNameOf(const QString& uid) const {
    if (!uid.isEmpty() && uid == myCloudId()) return L("My Documents", "My Documents");
    const QString alias = m_aliases.value(uid);
    return alias.isEmpty() ? m_users.value(uid).name : alias;
}

QString ZaloClient::avatarOf(const QString& uid) const {
    if (!m_myUid.isEmpty() && uid == m_myUid) return m_userAvatar;
    return m_users.value(uid).avatar;
}

void ZaloClient::loadAliases(int page, QHash<QString, QString> collected) {
    if (!m_isLoggedIn) return;
    constexpr int kPage = 100;
    m_apiClient->session()->fetchAliasList(page, kPage, [this, page, collected](bool ok, const QJsonValue& data,
                                                                               const QString&, int) mutable {
        if (!ok) return;
        const QJsonArray items = data.toObject().value("items").toArray();
        for (const QJsonValue& v : items) {
            const QJsonObject o = v.toObject();
            const QString uid = idOf(o.value("userId"));
            const QString alias = o.value("alias").toString().trimmed();
            if (!uid.isEmpty() && !alias.isEmpty()) collected.insert(uid, alias);
        }
        if (items.size() >= kPage && page < 50) {
            loadAliases(page + 1, collected);
            return;
        }
        applyAliases(collected);
    });
}

void ZaloClient::applyAliases(const QHash<QString, QString>& complete) {
    QSet<QString> touched;
    for (auto it = complete.cbegin(); it != complete.cend(); ++it) {
        if (m_aliases.value(it.key()) != it.value()) touched.insert(it.key());
    }
    for (auto it = m_aliases.cbegin(); it != m_aliases.cend(); ++it) {
        if (!complete.contains(it.key())) touched.insert(it.key());
    }
    m_aliases = complete;
    if (touched.isEmpty()) return;
    m_conversationModel->beginBatch();
    for (const QString& uid : touched) applyProfileToConversation(uid);
    m_conversationModel->endBatch();
    m_messageModel->refreshSenders();
    rebuildContacts();
}

void ZaloClient::loadUnreadMarks() {
    if (!m_isLoggedIn) return;
    m_apiClient->session()->fetchUnreadMarks([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        QJsonObject obj = data.toObject();
        if (obj.value("data").isObject()) obj = obj.value("data").toObject();
        QSet<QString> marked;
        for (const char* key : {"convsUser", "convsGroup"}) {
            for (const QJsonValue& v : obj.value(key).toArray()) {
                const QString id = idOf(v.isObject() ? v.toObject().value("id") : v);
                if (!id.isEmpty()) marked.insert(id);
            }
        }
        for (const QString& id : std::as_const(m_markedUnread)) {
            if (!marked.contains(id)) m_conversationModel->clearUnread(id);
        }
        m_markedUnread = marked;
        for (const QString& id : marked) {
            if (m_conversationModel->contains(id) && m_conversationModel->conversation(id).unreadCount == 0) {
                m_conversationModel->setUnreadCount(id, 1);
            }
        }
    });
}

void ZaloClient::resyncAfterReconnect() {
    if (!m_isLoggedIn) return;
    loadAccountData();
    loadAliases();
    loadLabels();
    loadUnreadMarks();
    preloadRecentThreads();
}

void ZaloClient::addSystemNotice(const QString& threadId, bool isGroup, const QString& text, qint64 ts) {
    if (threadId.isEmpty() || text.isEmpty()) return;
    if (ts < kMinPlausibleEpochMs) ts = QDateTime::currentMSecsSinceEpoch();
    Models::MessageItem notice;
    notice.id = QStringLiteral("sys-%1-%2-%3").arg(threadId).arg(ts).arg(qHash(text), 0, 16);
    notice.messageType = QStringLiteral("system");
    notice.senderId = QStringLiteral("system");
    notice.text = text;
    notice.ts = ts;
    notice.timeStr = QDateTime::fromMSecsSinceEpoch(ts).toString("HH:mm");
    if (isThreadResident(threadId) || !cacheUsable()) m_messageModel->mergeMessages(threadId, {notice});
    if (m_offlineStore) m_offlineStore->saveMessage(threadId, notice, {}, isGroup);
    m_conversationModel->recordMessage(threadId, text, QString(), ts, false);
}

void ZaloClient::handleRemoteDeletes(const QJsonArray& entries, bool isGroup) {
    for (const QJsonValue& v : entries) {
        const QJsonObject e = v.toObject();
        const QString from = idOf(e.value("uidFrom"));
        const QString to = idOf(e.value("uidTo"));
        const QString target = idOf(e.value("globalDelMsgId"));
        const QString thread = isGroup ? to : ((to == m_myUid || to == "0") ? from : to);
        if (thread.isEmpty() || target.isEmpty()) continue;
        const int type = e.value("type").toInt();
        if (type == 2 || type == 4) {
            m_messageModel->removeMessagesUpTo(thread, target);
            if (m_offlineStore) m_offlineStore->deleteMessagesUpTo(thread, target);
            if (m_messageModel->threadSize(thread) == 0) {
                if (thread == m_currentConvId) openWelcomeScreen();
                m_conversationModel->removeConversation(thread);
                m_cacheMerged.remove(thread);
                if (m_offlineStore) m_offlineStore->deleteConversation(thread);
            }
        } else {
            m_messageModel->removeMessage(thread, target);
            m_rawMessages.remove(target);
            if (m_offlineStore) m_offlineStore->deleteMessage(target);
        }
    }
}

void ZaloClient::handleGroupControl(const QString& act, const QJsonObject& data) {
    const QString gid = idOf(data.value("groupId").isUndefined() ? data.value("group_id") : data.value("groupId"));
    if (gid.isEmpty()) return;
    const QString actor = idOf(data.value("sourceId"));
    const QString actorName = actor == m_myUid ? L("Bạn", "You")
                              : (displayNameOf(actor).isEmpty() ? L("Một thành viên", "A member") : displayNameOf(actor));
    QStringList names;
    bool meAffected = false;
    for (const QJsonValue& v : data.value("updateMembers").toArray()) {
        const QJsonObject m = v.toObject();
        const QString id = idOf(m.value("id"));
        if (id == m_myUid) meAffected = true;
        const QString n = id == m_myUid ? L("bạn", "you") : m.value("dName").toString(displayNameOf(id));
        if (!n.isEmpty()) names << n;
    }
    const qint64 ts = data.value("time").toVariant().toLongLong() > 0 ? data.value("time").toVariant().toLongLong()
                                                                        : QDateTime::currentMSecsSinceEpoch();
    const QString who = names.join(", ");

    if (act == "update") {
        const QString name = data.value("groupName").toString();
        if (!name.isEmpty()) {
            m_groups[gid].name = name;
            applyProfileToConversation(gid);
            addSystemNotice(gid, true, L("%1 đã đổi tên nhóm thành \"%2\"", "%1 renamed the group to \"%2\"").arg(actorName, name), ts);
        }
    } else if (act == "update_avatar") {
        QString avatar = data.value("avt").toString();
        if (avatar.isEmpty()) avatar = data.value("fullAvt").toString();
        if (!avatar.isEmpty()) {
            m_groups[gid].avatar = avatar;
            applyProfileToConversation(gid);
        }
        addSystemNotice(gid, true, L("%1 đã đổi ảnh đại diện nhóm", "%1 changed the group photo").arg(actorName), ts);
    } else if (act == "join") {
        if (meAffected) ensureConversation(gid, true);
        loadGroupInfo({gid});
        addSystemNotice(gid, true, actor.isEmpty() || names.contains(actorName)
                                       ? L("%1 đã tham gia nhóm", "%1 joined the group").arg(who)
                                       : L("%1 đã thêm %2 vào nhóm", "%1 added %2").arg(actorName, who), ts);
    } else if (act == "leave" || act == "remove_member" || act == "block_member") {
        if (meAffected && act != "leave") {
            markThreadReadOnly(gid, QStringLiteral("removed"), ts);
            return;
        }
        if (meAffected) {
            if (gid == m_currentConvId) openWelcomeScreen();
            m_conversationModel->removeConversation(gid);
            m_groupIds.remove(gid);
            m_groups.remove(gid);
            if (m_offlineStore) m_offlineStore->deleteConversation(gid);
            rebuildContacts();
            return;
        }
        loadGroupInfo({gid});
        addSystemNotice(gid, true, act == "leave" ? L("%1 đã rời nhóm", "%1 left the group").arg(who)
                                                  : L("%1 đã xóa %2 khỏi nhóm", "%1 removed %2").arg(actorName, who), ts);
    } else if (act == "add_admin" || act == "remove_admin") {
        if (m_groupRosters.contains(gid)) {
            for (const QJsonValue& v : data.value("updateMembers").toArray()) {
                const QString id = idOf(v.toObject().value("id"));
                if (act == "add_admin") m_groupRosters[gid].admins.insert(id);
                else m_groupRosters[gid].admins.remove(id);
            }
            emit groupMembersChanged(gid);
        }
        addSystemNotice(gid, true, act == "add_admin"
                                       ? L("%1 đã thêm %2 làm phó nhóm", "%1 made %2 a deputy").arg(actorName, who)
                                       : L("%1 đã xóa quyền phó nhóm của %2", "%1 removed %2 as deputy").arg(actorName, who), ts);
    } else if (act == "update_board" || act == "new_pin_topic" || act == "update_pin_topic") {
        const QJsonValue rawParams = data.value("groupTopic").toObject().value("params");
        const QJsonObject params = rawParams.isString()
                                       ? QJsonDocument::fromJson(rawParams.toString().toUtf8()).object()
                                       : rawParams.toObject();
        const qint64 pollId = params.value("pollId").toVariant().toLongLong();
        if (pollId > 0) refreshPoll(pollId, true);
    } else if (act == "disperse" || act == "disband") {
        if (actor != m_myUid) markThreadReadOnly(gid, QStringLiteral("disbanded"), ts);
    } else if (act == "update_ttl") {
        QJsonValue raw = data.value("ttl");
        if (raw.isUndefined()) raw = data.value("data").toObject().value("ttl");
        qint64 ttl = raw.toVariant().toLongLong();
        if (ttl > 0 && ttl < 10000000) ttl *= 1000;
        if (ttl > 0) m_autoDeleteTtl.insert(gid, ttl);
        else m_autoDeleteTtl.remove(gid);
        emit conversationFlagsChanged();
        addSystemNotice(gid, true, ttl > 0
                                       ? L("%1 đã đặt thời gian tự xóa cho tin nhắn là %2", "%1 set disappearing messages to %2")
                                             .arg(actorName, ttlText(ttl))
                                       : L("%1 đã tắt tin nhắn tự xóa", "%1 turned off disappearing messages").arg(actorName), ts);
    } else if (act == "change_owner") {
        loadGroupInfo({gid});
        if (!who.isEmpty())
            addSystemNotice(gid, true, L("%1 đã chuyển quyền trưởng nhóm cho %2", "%1 made %2 the group owner")
                                           .arg(actorName, who), ts);
        if (gid == m_currentConvId) emit composerStateChanged();
    } else if (act == "update_setting" || act == "force_sync" || act == "group_type" || act == "new_link"
               || act == "invite_multi" || act == "remove_multi") {
        loadGroupInfo({gid});
    }
}

void ZaloClient::markThreadReadOnly(const QString& id, const QString& reason, qint64 ts) {
    if (id.isEmpty()) return;
    const bool fresh = m_readOnlyThreads.value(id) != reason;
    m_readOnlyThreads.insert(id, reason);
    saveReadOnlyThreads();
    if (fresh) {
        addSystemNotice(id, true, reason == QLatin1String("disbanded")
                                      ? L("Trưởng nhóm đã giải tán nhóm", "The owner disbanded the group")
                                      : L("Bạn đã được xoá khỏi nhóm", "You were removed from the group"), ts);
    }
    if (id == m_currentConvId) emit composerStateChanged();
}

void ZaloClient::loadReadOnlyThreads() {
    m_readOnlyThreads.clear();
    if (m_myUid.isEmpty()) return;
    QSettings settings("Zalos", "ZalosApp");
    const QVariantMap map = settings.value(QStringLiteral("readonlyThreads/") + m_myUid).toMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it) m_readOnlyThreads.insert(it.key(), it.value().toString());
    m_strangerLocks.clear();
    const QVariantMap locks = settings.value(QStringLiteral("strangerLocks/") + m_myUid).toMap();
    for (auto it = locks.cbegin(); it != locks.cend(); ++it) m_strangerLocks.insert(it.key(), it.value().toLongLong());
    scheduleStrangerUnlock();
    emit composerStateChanged();
}

void ZaloClient::saveReadOnlyThreads() const {
    if (m_myUid.isEmpty()) return;
    QVariantMap map;
    for (auto it = m_readOnlyThreads.cbegin(); it != m_readOnlyThreads.cend(); ++it) map.insert(it.key(), it.value());
    QSettings settings("Zalos", "ZalosApp");
    settings.setValue(QStringLiteral("readonlyThreads/") + m_myUid, map);
}

bool ZaloClient::isStrangerLocked(const QString& id) const {
    const auto it = m_strangerLocks.constFind(id);
    return it != m_strangerLocks.cend() && *it > QDateTime::currentMSecsSinceEpoch() && !m_friendIds.contains(id);
}

bool ZaloClient::noteSendFailure(const QString& convId, int code, const QJsonValue& data) {
    if (code != 804 || convId.isEmpty() || isGroupThread(convId)) return false;
    const QJsonObject info = data.isString() ? QJsonDocument::fromJson(data.toString().toUtf8()).object() : data.toObject();
    const qint64 until = info.value("bannedExpireTimestamp").toVariant().toLongLong();
    if (until <= QDateTime::currentMSecsSinceEpoch()) return false;
    m_strangerLocks.insert(convId, until);
    saveStrangerLocks();
    scheduleStrangerUnlock();
    emit composerStateChanged();
    return true;
}

void ZaloClient::saveStrangerLocks() const {
    if (m_myUid.isEmpty()) return;
    QVariantMap map;
    for (auto it = m_strangerLocks.cbegin(); it != m_strangerLocks.cend(); ++it) map.insert(it.key(), it.value());
    QSettings settings("Zalos", "ZalosApp");
    settings.setValue(QStringLiteral("strangerLocks/") + m_myUid, map);
}

void ZaloClient::scheduleStrangerUnlock() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 next = 0;
    for (auto it = m_strangerLocks.begin(); it != m_strangerLocks.end();) {
        if (it.value() <= now) {
            it = m_strangerLocks.erase(it);
            continue;
        }
        if (next == 0 || it.value() < next) next = it.value();
        ++it;
    }
    if (!m_strangerUnlockTimer) {
        m_strangerUnlockTimer = new QTimer(this);
        m_strangerUnlockTimer->setSingleShot(true);
        connect(m_strangerUnlockTimer, &QTimer::timeout, this, [this]() {
            scheduleStrangerUnlock();
            saveStrangerLocks();
            emit composerStateChanged();
        });
    }
    if (next == 0) {
        m_strangerUnlockTimer->stop();
        return;
    }
    m_strangerUnlockTimer->start(int(qBound<qint64>(1000, next - now + 500, 6LL * 60 * 60 * 1000)));
}

bool ZaloClient::currentConversationIsStranger() const {
    return m_conversationModel->isStranger(m_currentConvId) && !m_blockedIds.contains(m_currentConvId);
}

QString ZaloClient::strangerBannerTitle() const {
    const StrangerRank r = m_strangerRanks.value(m_currentConvId);
    if (!r.title.isEmpty()) return r.title;
    if (r.rank == 2) return L("Cảnh báo lừa đảo", "Scam warning");
    if (r.rank == 1) return L("Cảnh giác lừa đảo", "Beware of scams");
    return L("Người này chưa là bạn", "This person isn't your friend yet");
}

QString ZaloClient::strangerBannerText() const {
    const StrangerRank r = m_strangerRanks.value(m_currentConvId);
    if (!r.text.isEmpty()) return r.text;
    if (r.rank == 2) return L("Cẩn trọng khi chia sẻ thông tin với người này.", "Be careful sharing information with this person.");
    if (r.rank == 1) return L("Cẩn thận khi chia sẻ thông tin với người lạ", "Be mindful when sharing information with strangers");
    return L("Lưu ý rủi ro lừa đảo từ người lạ.", "Watch out for scams from strangers.");
}

void ZaloClient::applyStrangerRank(const QString& id, const QJsonObject& data) {
    StrangerRank r;
    const QString rank = data.value("userRank").toString(data.value("rank_type").toString(data.value("rank").toString()));
    r.rank = rank == QLatin1String("BLACK") ? 2 : (rank == QLatin1String("GRAY") ? 1 : 0);
    const QJsonObject render = data.value("data_render").toObject();
    const QString lang = m_appLanguage == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("vi");
    r.title = render.value("title").toObject().value(lang).toString();
    r.text = render.value("desc").toObject().value(lang).toString();
    m_strangerRanks.insert(id, r);
    if (id == m_currentConvId) emit composerStateChanged();
}

#ifdef ZALOS_TESTING
void ZaloClient::setStrangerRankForTesting(const QString& id, const QJsonObject& data) {
    applyStrangerRank(id, data);
}
#endif

void ZaloClient::refreshStrangerRank(const QString& convId) {
    if (!m_isLoggedIn || m_strangerRanks.contains(convId) || !m_conversationModel->isStranger(convId)) return;
    m_strangerRanks.insert(convId, StrangerRank{});
    QPointer<ZaloClient> guard(this);
    m_apiClient->session()->fetchProfileRank(convId, [guard, convId](bool ok, const QJsonValue& data, const QString&, int) {
        if (!guard || !ok) return;
        guard->applyStrangerRank(convId, data.toObject());
    });
}

void ZaloClient::syncFriendContext() {
    m_conversationModel->setFriendContext(m_friendIds, m_officialIds, m_myUid, m_friendsKnown);
    emit composerStateChanged();
}

QString ZaloClient::composerLockReason() const {
    if (m_currentConvId.isEmpty()) return {};
    if (m_readOnlyThreads.contains(m_currentConvId))
        return L("Bạn không thể gửi tin nhắn vào nhóm được nữa", "You can no longer send messages to this group");
    if (isStrangerLocked(m_currentConvId))
        return friendlyError(QString(), 804) + QLatin1Char(' ')
               + L("Bạn có thể trả lời lại lúc %1.", "You can reply again at %1.")
                     .arg(QDateTime::fromMSecsSinceEpoch(m_strangerLocks.value(m_currentConvId)).toString("HH:mm dd/MM/yyyy"));
    if (!isGroupThread(m_currentConvId)) return {};
    if (m_groupAdmin->settingsOf(m_currentConvId).value("lockSendMsg").toVariant().toInt() != 1) return {};
    const auto roster = m_groupRosters.constFind(m_currentConvId);
    if (roster != m_groupRosters.cend() && (roster->creator == m_myUid || roster->admins.contains(m_myUid))) return {};
    return L("Nhóm không cho phép thành viên gửi tin nhắn", "Only admins can send messages in this group");
}

QString ZaloClient::ttlText(qint64 ttlMs) const {
    const qint64 hours = ttlMs / 3600000;
    if (hours >= 24 && hours % 24 == 0) return L("%1 ngày", "%1 day(s)").arg(hours / 24);
    if (hours >= 1) return L("%1 giờ", "%1 hour(s)").arg(hours);
    return L("%1 phút", "%1 minute(s)").arg(qMax<qint64>(1, ttlMs / 60000));
}

void ZaloClient::handleSyncControl(const QString& actType, const QString& act, const QJsonValue& data) {
    const QJsonObject obj = data.toObject();
    if (actType == "hidden_chat") {
        auto threadOf = [](const QJsonValue& v) {
            QString id = idOf(v.toObject().value("thread_id"));
            if (id.size() > 1 && (id.startsWith('g') || id.startsWith('u')) && id.at(1).isDigit()) id.remove(0, 1);
            return id;
        };
        if (act == "add_remove") {
            QSet<QString> next = m_hiddenThreads;
            for (const QJsonValue& v : obj.value("add_items").toArray()) {
                const QString id = threadOf(v);
                if (!id.isEmpty()) next.insert(id);
            }
            for (const QJsonValue& v : obj.value("del_items").toArray()) next.remove(threadOf(v));
            applyHiddenSet(next);
        } else if (act == "reset") {
            const QSet<QString> dropped = m_hiddenThreads;
            m_hiddenPinHash.clear();
            emit hiddenPinChanged();
            dropHiddenConversationsLocally(dropped);
            applyHiddenSet({});
        } else if (act == "change_pin") {
            m_hiddenPinHash = obj.value("pin").toString().toLower();
            emit hiddenPinChanged();
        }
        return;
    }
    if (m_pinBoard->handleControl(act, data)) return;
    if (actType == "group") {
        handleGroupControl(act, obj);
    } else if (actType == "alias") {
        loadAliases();
    } else if (actType == "profile") {
        if (act == "update") loadAccountData();
        else if (act == "online_status") refreshPresence();
    } else if (actType == "mute") {
        QString id = idOf(obj.value("toId"));
        if (id.isEmpty()) id = idOf(obj.value("id"));
        if (id.isEmpty()) return;
        const int muteMode = obj.value("muteMode").toInt(0);
        const qint64 duration = obj.value("duration").toVariant().toLongLong();
        const bool mute = (act == "set") && (duration != 0 || muteMode == 2 || muteMode == 3);
        if (mute) {
            m_mutedIds.insert(id);
            m_muteUntil.insert(id, duration > 0 ? QDateTime::currentMSecsSinceEpoch() + duration * 1000 : 0);
        } else {
            m_mutedIds.remove(id);
            m_muteUntil.remove(id);
        }
        m_conversationModel->setMuted(id, mute);
        if (id == m_currentConvId) emit currentConversationChanged();
    } else if (actType == "pinned_convers") {
        const bool pin = obj.value("action").toInt() == 1;
        for (const QJsonValue& v : obj.value("pinIds").toArray()) {
            QString raw = v.toString();
            const bool isGroup = raw.startsWith('g');
            if (raw.startsWith('g') || raw.startsWith('u')) raw.remove(0, 1);
            if (pin) ensureConversation(raw, isGroup);
            m_conversationModel->setPinned(raw, pin);
        }
        emit currentConversationChanged();
    } else if (actType == "label_convers") {
        m_labelDebounce->start();
    } else if (actType == "mark_unread") {
        for (const char* key : {"convsUser", "convsGroup"}) {
            for (const QJsonValue& v : obj.value(key).toArray()) {
                const QString id = idOf(v.isObject() ? v.toObject().value("id") : v);
                if (id.isEmpty()) continue;
                if (act == "add") {
                    m_markedUnread.insert(id);
                    if (id != m_currentConvId && m_conversationModel->conversation(id).unreadCount == 0) {
                        m_conversationModel->setUnreadCount(id, 1);
                    }
                } else if (m_markedUnread.remove(id)) {
                    m_conversationModel->clearUnread(id);
                }
            }
        }
    } else if (actType == "archived_chat") {
        loadArchivedChats();
    } else if (actType == "conv_settings") {
        loadAutoDeleteSettings();
    } else if (actType == "my_settings") {
        loadServerSettings();
    } else if (actType == "quick_message") {
        loadQuickMessages();
    }
}

void ZaloClient::preloadRecentThreads() {
    m_apiClient->session()->fetchLastMessages({}, {}, [this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        QJsonObject obj = data.toObject();
        if (obj.value("data").isObject()) obj = obj.value("data").toObject();
        QJsonArray users = obj.value("msgs").toArray();
        for (const QJsonValue& v : obj.value("pageMsgs").toArray()) users.append(v);
        handleIncomingMessages(users, false, true);
        handleIncomingMessages(obj.value("groupMsgs").toArray(), true, true);
        applyReadPoints(obj.value("clearUnreads").toArray());
    });
}

void ZaloClient::applyReadPoints(const QJsonArray& clearUnreads) {
    for (const QJsonValue& v : clearUnreads) {
        const QJsonObject e = v.toObject();
        if (e.value("type").toInt() == 2 && e.contains("sct")) continue;
        const QString id = idOf(e.value("idTo"));
        const QString point = idOf(e.value("lastMsgId"));
        if (id.isEmpty() || id == "0" || point.isEmpty() || point == "0") continue;
        const QJsonValue g = e.value("isGroup");
        const bool isGroup = g.toBool() || g.toInt() == 1 || isGroupThread(id);
        if (!m_conversationModel->contains(id)) {
            if (isGroup) continue;
            ensureConversation(id, false);
        }
        if (id == m_currentConvId || m_markedUnread.contains(id)) continue;

        const auto conv = m_conversationModel->conversation(id);
        const int serverIncoming = m_messageModel->countIncomingAfter(id, point);
        if (conv.unreadCount == 0 && serverIncoming > 0) {
            sendSeenFor(id, isGroup);
            continue;
        }

        m_conversationModel->setUnreadCount(id, serverIncoming);
    }
}

void ZaloClient::showMessageNotification(const QString& threadId, const QString& title, const QString& body) {
    Platform::DesktopNotifications::Request request;
    const bool hide = m_antiPeeking || m_appLock->locked();
    request.title = hide || title.isEmpty() ? QStringLiteral("Zalos") : title;
    request.body = hide ? L("Bạn có tin nhắn mới", "You have a new message") : body.left(200);
    request.category = QStringLiteral("im.received");
    request.actions = {{QStringLiteral("default"), L("Mở", "Open")},
                       {QStringLiteral("mark-read"), L("Đánh dấu đã đọc", "Mark as read")}};
    request.inlineReply = !m_appLock->locked();
    request.replyPlaceholder = L("Trả lời…", "Reply…");
    m_notifications->show(QStringLiteral("thread:") + threadId, request);
}

void ZaloClient::onNotificationAction(const QString& key, const QString& action, const QString& activationToken) {
    if (key == QLatin1String("call")) {
        if (action == QLatin1String("answer")) m_calls->accept();
        else if (action == QLatin1String("decline")) m_calls->reject();
        if (action != QLatin1String("decline")) emit windowActivationRequested(activationToken);
        return;
    }
    if (!key.startsWith(QLatin1String("thread:"))) return;
    const QString convId = key.mid(7);
    if (action == QLatin1String("mark-read")) {
        markThreadRead(convId);
        m_notifications->close(key);
        return;
    }
    if (m_currentNavigationTab != QLatin1String("messages")) setCurrentNavigationTab(QStringLiteral("messages"));
    openConversationById(convId);
    emit windowActivationRequested(activationToken);
}

void ZaloClient::sendVoiceMessage(const QString& convId, const QString& filePath, int durationMs) {
    if (convId.isEmpty() || filePath.isEmpty()) return;
    const bool isGroup = isGroupThread(convId);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Models::MessageItem local;
    local.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_localSeq);
    m_lastClientId = qMax(now, m_lastClientId + 1);
    local.cliMsgId = QString::number(m_lastClientId);
    local.messageType = QStringLiteral("voice");
    local.text = L("Tin nhắn thoại", "Voice message");
    local.href = QUrl::fromLocalFile(filePath).toString();
    local.extra = QVariantMap{{"durationMs", durationMs}};
    local.ts = now;
    local.timeStr = QDateTime::fromMSecsSinceEpoch(now).toString("HH:mm");
    local.isMine = true;
    local.senderId = m_myUid;
    local.senderName = m_userName;
    local.senderAvatar = m_userAvatar;
    local.status = QStringLiteral("sending");
    m_messageModel->mergeMessages(convId, {local});
    m_conversationModel->recordMessage(convId, L("[Tin nhắn thoại]", "[Voice message]"), L("Bạn: ", "You: "), now, false);
    if (!m_isLoggedIn) {
        m_messageModel->setStatus(convId, local.id, QStringLiteral("failed"));
        emit notice(L("Đang ngoại tuyến nên chưa gửi được tin nhắn thoại.", "You're offline, so the voice message wasn't sent."));
        return;
    }
    const QString localId = local.id;
    const qint64 ttl = m_autoDeleteTtl.value(convId, 0);
    m_apiClient->session()->sendVoice(convId, isGroup, filePath, local.cliMsgId,
        [this, convId, localId](bool ok, const QJsonValue&, const QString& error, int code) {
            m_messageModel->setStatus(convId, localId, ok ? QStringLiteral("sent") : QStringLiteral("failed"));
            if (!ok) emit notice(L("Chưa gửi được tin nhắn thoại: ", "Voice message not sent: ") + friendlyError(error, code));
        }, ttl);
}

void ZaloClient::setDraft(const QString& convId, const QString& text) {
    if (convId.isEmpty()) return;
    const QString clean = text.trimmed().isEmpty() ? QString() : text;
    if (m_conversationModel->draft(convId) == clean) return;
    m_conversationModel->setDraft(convId, clean);
    if (m_myUid.isEmpty()) return;
    QSettings settings;
    settings.beginGroup(QStringLiteral("drafts/") + m_myUid);
    if (clean.isEmpty()) settings.remove(convId);
    else settings.setValue(convId, clean);
}

QString ZaloClient::draftFor(const QString& convId) const {
    return m_conversationModel->draft(convId);
}

void ZaloClient::loadDrafts() {
    m_conversationModel->clearDrafts();
    if (m_myUid.isEmpty()) return;
    QSettings settings;
    settings.beginGroup(QStringLiteral("drafts/") + m_myUid);
    for (const QString& convId : settings.childKeys()) {
        m_conversationModel->setDraft(convId, settings.value(convId).toString());
    }
}

void ZaloClient::selectAdjacentConversation(int delta) {
    const QString id = m_conversationModel->adjacentId(m_currentConvId, delta);
    if (!id.isEmpty() && id != m_currentConvId) openConversationById(id);
}

void ZaloClient::openPinnedConversation(int n) {
    const QStringList pinned = m_conversationModel->pinnedIds();
    if (n >= 1 && n <= pinned.size()) openConversationById(pinned.at(n - 1));
}

void ZaloClient::openConversationById(const QString& id) {
    if (id.isEmpty()) return;
    if (needsHiddenPin(id)) {
        requestHiddenFlow(QStringLiteral("open"), id, false);
        return;
    }
    const auto conv = m_conversationModel->conversation(id);
    const Profile p = m_users.value(id);
    const QString name = !conv.name.isEmpty() ? conv.name : p.name;
    const QString avatar = !conv.avatarUrl.isEmpty() ? conv.avatarUrl : p.avatar;
    selectConversation(id, name, avatar);
}

void ZaloClient::markThreadRead(const QString& convId) {
    if (convId.isEmpty()) return;
    m_conversationModel->clearUnread(convId);
    if (m_offlineStore) m_offlineStore->saveConversation(m_conversationModel->conversation(convId));
    if (m_markedUnread.contains(convId)) toggleUnreadMark(convId);
    sendSeenFor(convId, isGroupThread(convId));
}

QVariantList ZaloClient::scheduledMessages() const {
    return m_scheduler->entriesFor(m_currentConvId);
}

bool ZaloClient::scheduleMessage(const QString& text, qint64 sendAtMs) {
    QString error;
    if (!m_scheduler->schedule(m_currentConvId, currentConversationIsGroup(), text, sendAtMs, &error)) {
        if (error == QLatin1String("past")) emit notice(L("Hãy chọn thời điểm ở tương lai.", "Pick a time in the future."));
        return false;
    }
    emit notice(L("Đã hẹn giờ gửi lúc %1. Zalos cần đang mở để gửi.", "Scheduled for %1. Zalos must be running to send it.")
                    .arg(QDateTime::fromMSecsSinceEpoch(sendAtMs).toString("HH:mm dd/MM")));
    return true;
}

void ZaloClient::cancelScheduledMessage(const QString& id) {
    m_scheduler->cancel(id);
}

bool ZaloClient::testE2EEDecryption() {
    bool ok = Crypto::DbCross::verifyCryptoSelfTest();
    if (ok) {
        emit e2eeVerified(true, L("Mô-đun mã hóa hoạt động bình thường.", "The encryption module works correctly."));
    } else {
        emit e2eeVerified(false, L("Lỗi kiểm tra mô-đun mã hóa.", "The encryption module self-test failed."));
    }
    return ok;
}

void ZaloClient::setShowSettingsModal(bool show) {
    if (m_showSettingsModal != show) {
        m_showSettingsModal = show;
        emit settingsModalChanged();
    }
}

void ZaloClient::openSettings(const QString& initialTab) {
    m_settingsInitialTab = initialTab.isEmpty() ? "appearance" : initialTab;
    m_showSettingsModal = true;
    m_showUserProfileModal = false;
    loadServerSettings();
    emit settingsModalChanged();
}

void ZaloClient::closeSettings() {
    m_showSettingsModal = false;
    emit settingsModalChanged();
}

void ZaloClient::setShowUserProfileModal(bool show) {
    if (m_showUserProfileModal != show) {
        m_showUserProfileModal = show;
        emit userProfileModalChanged();
    }
}

QString ZaloClient::profileUserId() const { return m_activeProfile.userId; }
bool ZaloClient::profileIsSelf() const { return m_activeProfile.userId.isEmpty() || m_activeProfile.userId == m_myUid; }
QString ZaloClient::profileDisplayName() const { return m_activeProfile.displayName; }
QString ZaloClient::profileZaloName() const { return m_activeProfile.zaloName; }
QString ZaloClient::profileAvatar() const { return m_activeProfile.avatar; }
QString ZaloClient::profileCover() const { return m_activeProfile.cover; }
int ZaloClient::profileGender() const { return m_activeProfile.gender; }
QString ZaloClient::profileDob() const { return m_activeProfile.sdob; }
QString ZaloClient::profileBio() const { return m_activeProfile.status; }
QString ZaloClient::profilePhone() const { return m_activeProfile.phoneNumber; }
bool ZaloClient::profileIsFriend() const { return m_activeProfile.isFriend; }
bool ZaloClient::profileIsOfficial() const {
    return isOfficialOrSystemAccount(m_activeProfile.userId, m_activeProfile.displayName);
}
bool ZaloClient::isOfficialAccountForTesting(const QJsonObject& user) { return isOfficialAccount(user); }
bool ZaloClient::profileIsBlocked() const { return m_activeProfile.isBlocked; }
bool ZaloClient::profileIsInactive() const { return m_inactiveIds.contains(m_activeProfile.userId); }

bool ZaloClient::currentConversationBlocked() const {
    return !m_currentConvId.isEmpty() && !isGroupThread(m_currentConvId) && m_blockedIds.contains(m_currentConvId);
}

void ZaloClient::noteAccountFlags(const QString& uid, const QJsonObject& profile) {
    if (uid.isEmpty() || uid.startsWith('g')) return;
    if (isOfficialAccount(profile)) m_officialIds.insert(uid);
    bool changed = false;
    if (const QJsonValue status = profile.value("accountStatus"); !status.isUndefined() && !status.isNull()) {
        const bool inactive = status.toVariant().toInt() != 0;
        changed |= inactive ? !m_inactiveIds.contains(uid) : m_inactiveIds.contains(uid);
        if (inactive) m_inactiveIds.insert(uid);
        else m_inactiveIds.remove(uid);
    }
    if (const QJsonValue blocked = profile.value("isBlocked"); !blocked.isUndefined() && !blocked.isNull()) {
        const bool on = blocked.toVariant().toInt() == 1 || blocked.toBool();
        changed |= on ? !m_blockedIds.contains(uid) : m_blockedIds.contains(uid);
        if (on) m_blockedIds.insert(uid);
        else m_blockedIds.remove(uid);
    }
    if (changed && uid == m_currentConvId) {
        emit composerStateChanged();
        emit conversationStatusChanged();
    }
}
bool ZaloClient::profileLoading() const { return m_profileLoading; }

void ZaloClient::openUserProfile() {
    viewProfile(m_myUid);
}

void ZaloClient::viewProfile(const QString& userId) {
    if (userId.isEmpty() || userId == m_myUid) {
        if (m_myProfile.userId.isEmpty()) {
            m_myProfile.userId = m_myUid;
            m_myProfile.displayName = m_userName;
            m_myProfile.avatar = m_userAvatar;
            m_myProfile.phoneNumber = m_userPhone;
        }
        m_activeProfile = m_myProfile;
        m_profileLoading = false;
        m_showUserProfileModal = true;
        emit currentProfileChanged();
        emit userProfileModalChanged();
        return;
    }

    m_profileLoading = true;
    m_activeProfile = UserProfileData{};
    m_activeProfile.userId = userId;
    if (m_users.contains(userId)) {
        m_activeProfile.displayName = m_users[userId].name;
        m_activeProfile.avatar = m_users[userId].avatar;
    }
    m_activeProfile.isFriend = m_friendIds.contains(userId);
    m_activeProfile.isBlocked = false;
    m_showUserProfileModal = true;
    emit currentProfileChanged();
    emit userProfileModalChanged();

    if (m_isLoggedIn && m_apiClient->session()) {
        auto applyProfileJson = [this, userId](const QJsonObject& p) {
            if (p.isEmpty() || !m_showUserProfileModal || m_activeProfile.userId != userId) return;
            const QString name = pickName(p);
            if (!name.isEmpty()) m_activeProfile.displayName = name;
            m_activeProfile.zaloName = p.value("zaloName").toString(m_activeProfile.displayName);
            m_activeProfile.avatar = p.value("avatar").toString(m_activeProfile.avatar);
            m_activeProfile.cover = backgroundOf(p);
            m_activeProfile.gender = p.value("gender").toInt(0);
            m_activeProfile.dob = p.value("dob").toVariant().toLongLong();
            m_activeProfile.sdob = p.value("sdob").toString();
            m_activeProfile.status = p.value("status").toString();
            m_activeProfile.phoneNumber = p.value("phoneNumber").toString();
            m_activeProfile.isFriend = p.value("isFr").toInt() == 1 || m_friendIds.contains(userId);
            m_activeProfile.isBlocked = p.value("isBlocked").toInt() == 1 || m_blockedIds.contains(userId);
            if (!name.isEmpty()) {
                m_users[userId] = {name, m_activeProfile.avatar, 0};
                persistProfile(userId, false);
                if (m_conversationModel->contains(userId)) applyProfileToConversation(userId);
                m_messageModel->refreshSenders();
            }
            noteAccountFlags(userId, p);
        };
        m_apiClient->session()->fetchUserInfo({userId}, [this, userId, applyProfileJson](bool ok, const QJsonValue& data,
                                                                                         const QString&, int) {
            QJsonObject p;
            if (ok) {
                const QJsonObject changed = data.toObject().value("changed_profiles").toObject();
                if (changed.contains(userId)) {
                    p = changed.value(userId).toObject();
                } else if (changed.contains(userId + "_0")) {
                    p = changed.value(userId + "_0").toObject();
                } else if (!changed.isEmpty()) {
                    p = changed.begin().value().toObject();
                }
            }
            if (!p.isEmpty()) {
                m_profileLoading = false;
                applyProfileJson(p);
                emit currentProfileChanged();
                return;
            }
            if (!m_isLoggedIn || !m_apiClient->session()) {
                m_profileLoading = false;
                emit currentProfileChanged();
                return;
            }
            m_apiClient->session()->fetchGroupMembersInfo({userId}, [this, userId, applyProfileJson](bool ok2,
                                                                                                     const QJsonValue& d2,
                                                                                                     const QString&, int) {
                m_profileLoading = false;
                if (ok2) {
                    const QJsonObject profiles = d2.toObject().value("profiles").toObject();
                    QJsonObject p2;
                    if (profiles.contains(userId)) p2 = profiles.value(userId).toObject();
                    else if (profiles.contains(userId + "_0")) p2 = profiles.value(userId + "_0").toObject();
                    else if (!profiles.isEmpty()) p2 = profiles.begin().value().toObject();
                    if (!p2.isEmpty()) applyProfileJson(p2);
                }
                emit currentProfileChanged();
            });
        });
    }
}

void ZaloClient::closeUserProfile() {
    m_showUserProfileModal = false;
    emit userProfileModalChanged();
}

void ZaloClient::updateMyProfile(const QString& name, int gender, const QString& dob, const QString& bio) {
    if (name.trimmed().isEmpty()) {
        emit notice(L("Tên hiển thị không được để trống", "Display name cannot be empty"));
        return;
    }
    const QString cleanName = name.trimmed();
    const QString cleanBio = bio.trimmed();
    const bool bioChanged = cleanBio != m_myProfile.status;

    m_myProfile.displayName = cleanName;
    m_myProfile.gender = gender;
    m_myProfile.sdob = dob;
    m_myProfile.status = cleanBio;
    m_userName = cleanName;
    m_activeProfile = m_myProfile;

    emit userProfileChanged();
    emit currentProfileChanged();
    saveSession();
    emit notice(L("Đã cập nhật thông tin cá nhân", "Profile updated successfully"));

    if (m_isLoggedIn && m_apiClient->session()) {
        m_apiClient->session()->updateProfile(cleanName, gender, dob, [this](bool ok, const QJsonValue&, const QString& err, int) {
            if (!ok) emit notice(L("Không thể cập nhật hồ sơ: ", "Failed to update profile: ") + friendlyError(err));
        });
        if (bioChanged) {
            m_apiClient->session()->updateProfileBio(cleanBio, [this](bool ok, const QJsonValue&, const QString& err, int) {
                if (!ok) emit notice(L("Không thể cập nhật trạng thái: ", "Failed to update status: ") + friendlyError(err));
            });
        }
    }
}

void ZaloClient::updateMyStatus(const QString& status) {
    const QString clean = status.trimmed();
    if (clean == m_myProfile.status) return;
    const QString previous = m_myProfile.status;
    m_myProfile.status = clean;
    if (m_activeProfile.userId == m_myUid || m_activeProfile.userId.isEmpty()) m_activeProfile.status = clean;
    emit userProfileChanged();
    emit currentProfileChanged();
    saveSession();
    if (!m_isLoggedIn || !m_apiClient->session()) return;
    m_apiClient->session()->updateProfileBio(clean, [this, previous](bool ok, const QJsonValue&, const QString& err, int) {
        if (ok) {
            emit notice(L("Đã cập nhật trạng thái", "Status updated"));
            return;
        }
        m_myProfile.status = previous;
        if (m_activeProfile.userId == m_myUid || m_activeProfile.userId.isEmpty()) m_activeProfile.status = previous;
        emit userProfileChanged();
        emit currentProfileChanged();
        emit notice(L("Không thể cập nhật trạng thái: ", "Failed to update status: ") + friendlyError(err));
    });
}

void ZaloClient::blockUser(const QString& userId) {
    if (userId.isEmpty() || userId == m_myUid) return;
    if (m_activeProfile.userId == userId) m_activeProfile.isBlocked = true;
    m_blockedIds.insert(userId);
    emit currentProfileChanged();
    emit composerStateChanged();
    emit notice(L("Bạn đã chặn tin nhắn và cuộc gọi từ người này", "You blocked messages and calls from this person"));

    if (m_isLoggedIn && m_apiClient->session()) {
        m_apiClient->session()->blockUser(userId, [this, userId](bool ok, const QJsonValue&, const QString& err, int) {
            if (ok) return;
            m_blockedIds.remove(userId);
            if (m_activeProfile.userId == userId) m_activeProfile.isBlocked = false;
            emit currentProfileChanged();
            emit composerStateChanged();
            emit notice(L("Không thể chặn: ", "Failed to block: ") + friendlyError(err));
        });
    }
}

void ZaloClient::unblockUser(const QString& userId) {
    if (userId.isEmpty() || userId == m_myUid) return;
    if (m_activeProfile.userId == userId) m_activeProfile.isBlocked = false;
    m_blockedIds.remove(userId);
    emit currentProfileChanged();
    emit composerStateChanged();
    emit notice(L("Đã bỏ chặn người dùng này", "User unblocked"));

    if (m_isLoggedIn && m_apiClient->session()) {
        m_apiClient->session()->unblockUser(userId, [this, userId](bool ok, const QJsonValue&, const QString& err, int) {
            if (ok) return;
            m_blockedIds.insert(userId);
            if (m_activeProfile.userId == userId) m_activeProfile.isBlocked = true;
            emit currentProfileChanged();
            emit composerStateChanged();
            emit notice(L("Không thể bỏ chặn: ", "Failed to unblock: ") + friendlyError(err));
        });
    }
}

void ZaloClient::removeFriend(const QString& userId) {
    if (userId.isEmpty() || userId == m_myUid) return;
    m_friendIds.remove(userId);
    m_activeProfile.isFriend = false;
    rebuildContacts();
    emit currentProfileChanged();
    emit notice(L("Đã xóa khỏi danh sách bạn bè", "Removed from friends list"));

    if (m_isLoggedIn && m_apiClient->session()) {
        m_apiClient->session()->removeFriend(userId, [this](bool ok, const QJsonValue&, const QString& err, int) {
            if (!ok) emit notice(L("Không thể xóa bạn bè: ", "Failed to remove friend: ") + friendlyError(err));
        });
    }
}

void ZaloClient::setShowAddFriendModal(bool show) {
    if (m_showAddFriendModal != show) {
        m_showAddFriendModal = show;
        emit addFriendModalChanged();
    }
}

void ZaloClient::openAddFriend() {
    m_showAddFriendModal = true;
    m_showCreateGroupModal = false;
    emit addFriendModalChanged();
}

void ZaloClient::closeAddFriend() {
    m_showAddFriendModal = false;
    emit addFriendModalChanged();
}

void ZaloClient::setShowCreateGroupModal(bool show) {
    if (m_showCreateGroupModal != show) {
        m_showCreateGroupModal = show;
        emit createGroupModalChanged();
    }
}

void ZaloClient::openCreateGroup() {
    m_showCreateGroupModal = true;
    m_showAddFriendModal = false;
    emit createGroupModalChanged();
}

void ZaloClient::closeCreateGroup() {
    m_showCreateGroupModal = false;
    emit createGroupModalChanged();
}

void ZaloClient::setShowConversationInfo(bool show) {
    if (m_showConversationInfo != show) {
        m_showConversationInfo = show;
        emit conversationInfoChanged();
    }
}

void ZaloClient::toggleConversationInfo() {
    m_showConversationInfo = !m_showConversationInfo;
    emit conversationInfoChanged();
}

void ZaloClient::setShowForwardModal(bool show) {
    if (m_showForwardModal != show) {
        m_showForwardModal = show;
        emit forwardModalChanged();
    }
}

void ZaloClient::openShareCard(const QString& userId, const QString& name) {
    if (userId.isEmpty()) return;
    m_forwardMode = QStringLiteral("card");
    m_forwardCardUid = userId;
    m_forwardSourceMsgId.clear();
    m_forwardSourcePreview = name.isEmpty() ? displayNameOf(userId) : name;
    m_showForwardModal = true;
    emit forwardModalChanged();
}

void ZaloClient::openSendCardPicker() {
    if (m_currentConvId.isEmpty()) return;
    m_forwardMode = QStringLiteral("pickContact");
    m_forwardCardUid.clear();
    m_forwardSourceMsgId.clear();
    m_forwardSourcePreview = L("Gửi danh thiếp tới ", "Send a contact card to ") + m_currentConvName;
    m_showForwardModal = true;
    emit forwardModalChanged();
}

QVariantList ZaloClient::friendsForCard() const {
    QVariantList out;
    for (const QString& id : m_friendIds) {
        const QString name = displayNameOf(id);
        if (name.isEmpty()) continue;
        out.append(QVariantMap{{"id", id}, {"name", name}, {"avatar", m_users.value(id).avatar}});
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return QString::localeAwareCompare(a.toMap().value("name").toString(), b.toMap().value("name").toString()) < 0;
    });
    return out;
}

void ZaloClient::openForwardModal(const QString& msgId, const QString& previewText) {
    m_forwardMode = QStringLiteral("message");
    m_forwardCardUid.clear();
    m_forwardSourceMsgId = msgId;
    m_forwardSourcePreview = previewText;
    m_showForwardModal = true;
    emit forwardModalChanged();
}

void ZaloClient::closeForwardModal() {
    m_showForwardModal = false;
    m_forwardSourceMsgId.clear();
    m_forwardSourcePreview.clear();
    m_forwardCardUid.clear();
    m_forwardMode = QStringLiteral("message");
    emit forwardModalChanged();
}

void ZaloClient::setShowCreatePollModal(bool show) {
    if (m_showCreatePollModal != show) {
        m_showCreatePollModal = show;
        emit createPollModalChanged();
    }
}

void ZaloClient::openCreatePollModal() {
    m_showCreatePollModal = true;
    emit createPollModalChanged();
}

void ZaloClient::closeCreatePollModal() {
    m_showCreatePollModal = false;
    emit createPollModalChanged();
}

void ZaloClient::createPoll(const QString& groupId, const QString& question, const QStringList& options, bool allowMultiChoices) {
    if (groupId.isEmpty() || question.trimmed().isEmpty()) return;
    QStringList validOpts;
    for (const QString& opt : options) {
        if (!opt.trimmed().isEmpty()) validOpts.append(opt.trimmed());
    }
    if (validOpts.size() < 2) {
        emit notice(L("Cần ít nhất 2 phương án bình chọn", "At least 2 poll options are required"));
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 pollId = now;
    Models::MessageItem local;
    local.id = QStringLiteral("local-poll-%1").arg(now);
    local.text = question.trimmed();
    local.messageType = QStringLiteral("poll");
    local.ts = now;
    local.timeStr = QDateTime::fromMSecsSinceEpoch(now).toString("HH:mm");
    local.isMine = true;
    local.senderId = m_myUid;
    local.senderName = m_userName;
    local.senderAvatar = m_userAvatar;
    local.status = QStringLiteral("sent");

    QVariantMap pData;
    pData["poll_id"] = pollId;
    pData["question"] = local.text;
    pData["closed"] = false;
    pData["num_vote"] = 0;
    pData["allow_multi_choices"] = allowMultiChoices;
    QVariantList optList;
    for (int i = 0; i < validOpts.size(); ++i) {
        QVariantMap oMap;
        oMap["option_id"] = i + 1;
        oMap["content"] = validOpts[i];
        oMap["votes"] = 0;
        oMap["voted"] = false;
        optList.append(oMap);
    }
    pData["options"] = optList;
    local.pollData = pData;

    m_messageModel->mergeMessages(groupId, {local});
    m_conversationModel->recordMessage(groupId, L("[Bình chọn] ", "[Poll] ") + local.text, L("Bạn: ", "You: "), now, false);

    if (m_isLoggedIn && m_apiClient->session()) {
        m_apiClient->session()->createPoll(groupId, question.trimmed(), validOpts, allowMultiChoices,
            [this, groupId](bool ok, const QJsonValue&, const QString& err, int) {
                if (!ok) {
                    emit notice(L("Tạo bình chọn thất bại: ", "Create poll failed: ") + friendlyError(err));
                } else {
                    emit notice(L("Đã tạo cuộc bình chọn thành công", "Poll created successfully"));
                }
            });
    }

    closeCreatePollModal();
}

void ZaloClient::votePoll(qint64 pollId, int optionId) {
    if (pollId <= 0 || optionId <= 0) return;
    if (!m_isLoggedIn) {
        emit notice(L("Cần kết nối để bình chọn", "Voting needs a connection"));
        return;
    }
    m_apiClient->session()->votePoll(pollId, {optionId}, [this, pollId](bool ok, const QJsonValue&, const QString& err, int code) {
        if (!ok) {
            emit notice(L("Bình chọn thất bại: ", "Vote failed: ") + friendlyError(err, code));
            return;
        }
        refreshPoll(pollId, true);
    });
}

void ZaloClient::changeGroupName(const QString& groupId, const QString& newName) {
    if (groupId.isEmpty() || newName.trimmed().isEmpty()) return;
    const QString cleanName = newName.trimmed();
    if (m_currentConvId == groupId) {
        m_currentConvName = cleanName;
        emit currentConversationChanged();
    }
    m_conversationModel->updateProfile(groupId, cleanName, QString());
    if (m_isLoggedIn && m_apiClient->session()) {
        m_apiClient->session()->changeGroupName(groupId, cleanName, [this](bool ok, const QJsonValue&, const QString& err, int) {
            if (!ok) {
                emit notice(L("Không đổi được tên nhóm: ", "Failed to rename group: ") + friendlyError(err));
            } else {
                emit notice(L("Đã đổi tên nhóm thành công", "Group name updated successfully"));
            }
        });
    }
}

void ZaloClient::addMemberToGroup(const QString& groupId, const QString& memberId) {
    if (groupId.isEmpty() || memberId.isEmpty()) return;
    if (m_isLoggedIn && m_apiClient->session()) {
        m_apiClient->session()->addUserToGroup(groupId, {memberId}, [this](bool ok, const QJsonValue&, const QString& err, int) {
            if (!ok) {
                emit notice(L("Thêm thành viên thất bại: ", "Failed to add member: ") + friendlyError(err));
            } else {
                emit notice(L("Đã thêm thành viên vào nhóm", "Member added to group"));
            }
        });
    }
}

void ZaloClient::removeMemberFromGroup(const QString& groupId, const QString& memberId) {
    if (groupId.isEmpty() || memberId.isEmpty()) return;
    if (m_isLoggedIn && m_apiClient->session()) {
        m_apiClient->session()->removeUserFromGroup(groupId, memberId, [this](bool ok, const QJsonValue&, const QString& err, int) {
            if (!ok) {
                emit notice(L("Xóa thành viên thất bại: ", "Failed to remove member: ") + friendlyError(err));
            } else {
                emit notice(L("Đã xóa thành viên khỏi nhóm", "Member removed from group"));
            }
        });
    }
}

void ZaloClient::leaveGroup(const QString& groupId, bool silent) {
    if (groupId.isEmpty()) return;
    if (m_isLoggedIn && m_apiClient->session()) {
        m_apiClient->session()->leaveGroup(groupId, silent, [this, groupId](bool ok, const QJsonValue&, const QString& err, int) {
            if (!ok) {
                emit notice(L("Rời nhóm thất bại: ", "Failed to leave group: ") + friendlyError(err));
            } else {
                emit notice(L("Đã rời nhóm thành công", "Left group successfully"));
                if (m_currentConvId == groupId) {
                    openWelcomeScreen();
                }
            }
        });
    }
}

QVariantMap ZaloClient::fileInfoForUrl(const QString& fileUrlOrPath) {
    const QUrl url(fileUrlOrPath);
    const QString path = url.isLocalFile() ? url.toLocalFile() : fileUrlOrPath;
    const QFileInfo fi(path);
    const QString ext = fi.suffix().toLower();
    const bool isImg = (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp" || ext == "gif" || ext == "bmp");
    const bool isVid = (ext == "mp4" || ext == "mov" || ext == "webm" || ext == "mkv" || ext == "avi");
    QVariantMap item;
    item["url"] = url.isLocalFile() ? url.toString() : QUrl::fromLocalFile(path).toString();
    item["path"] = path;
    item["name"] = fi.fileName();
    item["isImage"] = isImg;
    item["isVideo"] = isVid;
    item["size"] = fi.size();
    return item;
}

QVariantList ZaloClient::getClipboardAttachments() {
    QVariantList list;
    QClipboard* clip = QGuiApplication::clipboard();
    if (!clip) return list;
    const QMimeData* mime = clip->mimeData();
    if (!mime) return list;

    if (mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            const QString tempPath = QString("%1/zalos_paste_%2_%3.png")
                                         .arg(tempDir)
                                         .arg(QDateTime::currentMSecsSinceEpoch())
                                         .arg(++m_localSeq);
            if (img.save(tempPath, "PNG")) {
                list.append(fileInfoForUrl(tempPath));
                return list;
            }
        }
    }

    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            const QString path = url.toLocalFile();
            if (path.isEmpty() || !QFileInfo::exists(path)) continue;
            list.append(fileInfoForUrl(path));
        }
    }
    return list;
}

bool ZaloClient::tryPasteClipboardImage() {
    if (m_currentConvId.isEmpty()) return false;
    const QVariantList items = getClipboardAttachments();
    if (items.isEmpty()) return false;
    for (const QVariant& v : items) {
        sendFile(v.toMap().value("url").toString(), QString());
    }
    return true;
}

bool ZaloClient::hasPinnedMessage() const {
    return !m_pinBoard->pins().isEmpty();
}

QString ZaloClient::currentPinnedMessageId() const {
    return m_pinBoard->pins().value(0).toMap().value("msgId").toString();
}

QString ZaloClient::currentPinnedMessageText() const {
    return m_pinBoard->pins().value(0).toMap().value("title").toString();
}

QString ZaloClient::currentPinnedMessageSender() const {
    return m_pinBoard->pins().value(0).toMap().value("senderName").toString();
}

void ZaloClient::pinMessage(const QString& msgId) {
    m_pinBoard->pinMessage(msgId);
}

void ZaloClient::unpinMessage() {
    const QString top = m_pinBoard->pins().value(0).toMap().value("id").toString();
    if (!top.isEmpty()) m_pinBoard->unpin(top);
}

void ZaloClient::changeFriendAlias(const QString& friendId, const QString& newAlias) {
    if (friendId.isEmpty() || !m_isLoggedIn) return;
    const QString clean = newAlias.trimmed();
    if (clean.isEmpty()) {
        if (!m_aliases.contains(friendId)) return;
        m_apiClient->session()->removeFriendAlias(friendId, [this, friendId](bool ok, const QJsonValue&,
                                                                             const QString& err, int code) {
            if (!ok) {
                emit notice(L("Không xóa được biệt danh: ", "Failed to remove alias: ") + friendlyError(err, code));
                return;
            }
            m_aliases.remove(friendId);
            applyProfileToConversation(friendId);
            rebuildContacts();
            emit notice(L("Đã xóa biệt danh", "Alias removed"));
        });
        return;
    }
    m_apiClient->session()->changeFriendAlias(friendId, clean, [this, friendId, clean](bool ok, const QJsonValue&,
                                                                                       const QString& err, int code) {
        if (!ok) {
            emit notice(L("Không đổi được biệt danh: ", "Failed to change alias: ") + friendlyError(err, code));
            return;
        }
        m_aliases.insert(friendId, clean);
        applyProfileToConversation(friendId);
        rebuildContacts();
        emit notice(L("Đã cập nhật biệt danh bạn bè", "Friend alias updated"));
    });
}

void ZaloClient::sendContactCard(const QString& targetThreadId, const QString& contactId, const QString& contactName, const QString& phone) {
    if (targetThreadId.isEmpty() || contactId.isEmpty()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool isGroup = isGroupThread(targetThreadId);
    QString name = contactName.isEmpty() ? displayNameOf(contactId) : contactName;
    if (contactId == m_myUid) name = m_userName;
    const QString avatar = contactId == m_myUid ? m_userAvatar : m_users.value(contactId).avatar;

    Models::MessageItem local;
    local.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_localSeq);
    m_lastClientId = qMax(now, m_lastClientId + 1);
    local.cliMsgId = QString::number(m_lastClientId);
    local.text = name.isEmpty() ? L("Danh thiếp", "Contact card") : name;
    local.messageType = QStringLiteral("card");
    local.mediaUrl = avatar;
    local.extra = {{"uid", contactId}, {"phone", phone}};
    local.ts = now;
    local.timeStr = QDateTime::fromMSecsSinceEpoch(now).toString("HH:mm");
    local.isMine = true;
    local.senderId = m_myUid;
    local.senderName = m_userName;
    local.senderAvatar = m_userAvatar;
    local.status = QStringLiteral("sending");
    m_messageModel->mergeMessages(targetThreadId, {local});
    m_conversationModel->recordMessage(targetThreadId, L("[Danh thiếp] ", "[Contact card] ") + local.text,
                                       L("Bạn: ", "You: "), now, false);

    if (!m_isLoggedIn) {
        m_messageModel->setStatus(targetThreadId, local.id, QStringLiteral("failed"));
        emit notice(L("Cần kết nối tới Zalo để gửi danh thiếp", "Connect to Zalo to send a contact card"));
        return;
    }
    const QString localId = local.id;
    const QString cliMsgId = local.cliMsgId;
    auto fail = [this, targetThreadId, localId](const QString& why) {
        m_messageModel->setStatus(targetThreadId, localId, QStringLiteral("failed"));
        emit notice(L("Không gửi được danh thiếp: ", "Contact card not sent: ") + why);
    };
    QPointer<ZaloSession> session(m_apiClient->session());
    session->fetchUserQr({contactId}, [this, session, targetThreadId, isGroup, contactId, phone, localId, cliMsgId, fail](
                                          bool ok, const QJsonValue& data, const QString& error, int code) {
        if (!session) return;
        const QString qr = ok ? data.toObject().value(contactId).toString() : QString();
        if (qr.isEmpty()) {
            fail(ok ? L("máy chủ không trả về mã QR của người này", "the server returned no QR code for this person")
                    : friendlyError(error, code));
            return;
        }
        session->sendContactCard(targetThreadId, isGroup, contactId, qr, phone, cliMsgId,
            [this, targetThreadId, localId, fail](bool sent, const QJsonValue& reply, const QString& err, int errCode) {
                if (!sent) {
                    fail(friendlyError(err, errCode));
                    return;
                }
                m_messageModel->setStatus(targetThreadId, localId, QStringLiteral("sent"));
                const QString serverId = idOf(reply.toObject().value("msgId"));
                if (!serverId.isEmpty()) m_messageModel->confirmMessage(targetThreadId, localId, serverId);
            });
    });
}

void ZaloClient::forwardMessage(const QString& targetThreadId, const QString& msgId) {
    if (targetThreadId.isEmpty() || msgId.isEmpty()) return;
    const QVariantMap item = m_messageModel->getMessageById(msgId);
    if (item.isEmpty()) {
        emit notice(L("Không tìm thấy tin nhắn cần chuyển tiếp", "Message to forward not found"));
        return;
    }

    const QString text = item.value("text").toString();
    const QString msgType = item.value("messageType").toString();
    const QString mediaUrl = item.value("mediaUrl").toString();
    const bool isGroup = isGroupThread(targetThreadId);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    Models::MessageItem local;
    local.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_localSeq);
    local.text = text;
    local.messageType = msgType.isEmpty() ? QStringLiteral("text") : msgType;
    local.mediaUrl = mediaUrl;
    local.ts = now;
    local.timeStr = QDateTime::fromMSecsSinceEpoch(now).toString("HH:mm");
    local.isMine = true;
    local.isForwarded = true;
    local.senderId = m_myUid;
    local.senderName = m_userName;
    local.senderAvatar = m_userAvatar;
    local.status = QStringLiteral("sending");

    m_messageModel->mergeMessages(targetThreadId, {local});
    m_conversationModel->recordMessage(targetThreadId, text.isEmpty() ? L("[Tệp đính kèm]", "[Attachment]") : text, L("Bạn: ", "You: "), now, false);

    emit notice(L("Đã chuyển tiếp tin nhắn thành công", "Message forwarded successfully"));
    closeForwardModal();

    if (!m_isLoggedIn || !m_apiClient->session()) {
        m_messageModel->setStatus(targetThreadId, local.id, QStringLiteral("failed"));
        return;
    }

    const QString localId = local.id;
    if (msgType == "image" || msgType == "file") {
        if (!mediaUrl.isEmpty() && (QFile::exists(mediaUrl) || mediaUrl.startsWith("file://"))) {
            const QString localPath = mediaUrl.startsWith("file://") ? QUrl(mediaUrl).toLocalFile() : mediaUrl;
            m_apiClient->session()->sendAttachment(targetThreadId, isGroup, localPath, text,
                [this, targetThreadId, localId](bool ok, const QJsonValue& data, const QString&, int) {
                    m_messageModel->setStatus(targetThreadId, localId, ok ? QStringLiteral("sent") : QStringLiteral("failed"));
                    if (ok) {
                        const QString serverId = idOf(data.toObject().value("msgId"));
                        if (!serverId.isEmpty()) m_messageModel->confirmMessage(targetThreadId, localId, serverId);
                    }
                });
        } else if (!mediaUrl.isEmpty() && (mediaUrl.startsWith("http://") || mediaUrl.startsWith("https://"))) {
            fetchBytes(mediaUrl, [this, targetThreadId, isGroup, text, localId, msgType, mediaUrl](const QByteArray& data, const QString& err) {
                if (data.isEmpty() || !err.isEmpty()) {
                    const QString forwardContent = text.isEmpty() ? mediaUrl : (text + "\n" + mediaUrl);
                    m_apiClient->session()->sendText(targetThreadId, isGroup, forwardContent,
                        [this, targetThreadId, localId](bool ok, const QJsonValue& res, const QString&, int) {
                            m_messageModel->setStatus(targetThreadId, localId, ok ? QStringLiteral("sent") : QStringLiteral("failed"));
                            if (ok) {
                                const QString serverId = idOf(res.toObject().value("msgId"));
                                if (!serverId.isEmpty()) m_messageModel->confirmMessage(targetThreadId, localId, serverId);
                            }
                        });
                    return;
                }
                const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
                const QString tempPath = QString("%1/zalos_fwd_%2_%3.%4")
                                             .arg(tempDir)
                                             .arg(QDateTime::currentMSecsSinceEpoch())
                                             .arg(++m_localSeq)
                                             .arg(msgType == "image" ? "png" : "bin");
                QFile tf(tempPath);
                if (tf.open(QIODevice::WriteOnly)) {
                    tf.write(data);
                    tf.close();
                    m_apiClient->session()->sendAttachment(targetThreadId, isGroup, tempPath, text,
                        [this, targetThreadId, localId](bool ok, const QJsonValue& res, const QString&, int) {
                            m_messageModel->setStatus(targetThreadId, localId, ok ? QStringLiteral("sent") : QStringLiteral("failed"));
                            if (ok) {
                                const QString serverId = idOf(res.toObject().value("msgId"));
                                if (!serverId.isEmpty()) m_messageModel->confirmMessage(targetThreadId, localId, serverId);
                            }
                        });
                }
            });
        } else {
            m_apiClient->session()->sendText(targetThreadId, isGroup, text.isEmpty() ? mediaUrl : text,
                [this, targetThreadId, localId](bool ok, const QJsonValue& data, const QString&, int) {
                    m_messageModel->setStatus(targetThreadId, localId, ok ? QStringLiteral("sent") : QStringLiteral("failed"));
                    if (ok) {
                        const QString serverId = idOf(data.toObject().value("msgId"));
                        if (!serverId.isEmpty()) m_messageModel->confirmMessage(targetThreadId, localId, serverId);
                    }
                });
        }
    } else {
        m_apiClient->session()->sendText(targetThreadId, isGroup, text,
            [this, targetThreadId, localId](bool ok, const QJsonValue& data, const QString&, int) {
                m_messageModel->setStatus(targetThreadId, localId, ok ? QStringLiteral("sent") : QStringLiteral("failed"));
                if (ok) {
                    const QString serverId = idOf(data.toObject().value("msgId"));
                    if (!serverId.isEmpty()) m_messageModel->confirmMessage(targetThreadId, localId, serverId);
                }
            });
    }
}

void ZaloClient::setMobileSyncState(int state, const QString& status, int progress) {
    m_mobileSyncState = state;
    m_mobileSyncStatus = status;
    m_mobileSyncProgress = progress;
    emit mobileSyncStateChanged();
    emit mobileSyncProgressChanged();
}

void ZaloClient::maybeOfferMobileSync() {
    if (!m_isLoggedIn || m_myUid.isEmpty() || m_showSyncPrompt) return;
    QSettings settings;
    if (settings.value(QStringLiteral("mobileSync/done/") + m_myUid).toBool()) return;
    if (settings.value(QStringLiteral("mobileSync/neverAsk/") + m_myUid).toBool()) return;
    if (m_mobileSyncState != 0 && m_mobileSyncState != 5) return;
    m_showSyncPrompt = true;
    emit syncPromptChanged();
}

void ZaloClient::acceptSyncPrompt() {
    m_showSyncPrompt = false;
    emit syncPromptChanged();
    openSettings(QStringLiteral("sync"));
    requestMobileSync();
}

void ZaloClient::dismissSyncPrompt(bool permanently) {
    if (permanently && !m_myUid.isEmpty()) QSettings().setValue(QStringLiteral("mobileSync/neverAsk/") + m_myUid, true);
    m_showSyncPrompt = false;
    emit syncPromptChanged();
}

void ZaloClient::requestMobileSync() {
    if (!m_isLoggedIn) {
        setMobileSyncState(5, L("Bạn cần đăng nhập trước khi đồng bộ.", "Sign in before syncing."), 0);
        return;
    }
    const QString host = QHostInfo::localHostName();
    m_historySync->setLanguage(m_appLanguage);
    m_historySync->start(m_myUid, host.isEmpty() ? QStringLiteral("zalos") : host);
}

void ZaloClient::cancelMobileSync() {
    m_historySync->cancel();
    setMobileSyncState(0, QString(), 0);
}

void ZaloClient::handleMobileSyncControl(const QJsonObject& content) {
    if (content.value("act_type").toString() == QLatin1String("syncmsgmb")) m_historySync->handleControl(content);
}

void ZaloClient::setCurrentNavigationTab(const QString& tab) {
    if (m_currentNavigationTab != tab) {
        m_currentNavigationTab = tab;
        emit navigationTabChanged();
    }
}

void ZaloClient::setNotifyNewMessage(bool val) {
    if (m_notifyNewMessage != val) {
        m_notifyNewMessage = val;
        saveSession();
        emit settingsChanged();
    }
}

void ZaloClient::setAntiPeeking(bool val) {
    if (m_antiPeeking == val) return;
    m_antiPeeking = val;
    saveSession();
    emit settingsChanged();
}

void ZaloClient::setNotifySound(bool val) {
    if (m_notifySound != val) {
        m_notifySound = val;
        saveSession();
        emit settingsChanged();
    }
}

void ZaloClient::setChatBackgroundMode(const QString& mode) {
    const QString normalized = mode == QLatin1String("avatar") || mode == QLatin1String("custom")
                                  ? mode : QStringLiteral("none");
    if (m_chatBackgroundMode != normalized) {
        m_chatBackgroundMode = normalized;
        saveSession();
        emit settingsChanged();
        emit currentConversationBackgroundChanged();
    }
}

void ZaloClient::setChatBackgroundOpacity(qreal opacity) {
    const qreal bounded = qBound(0.05, opacity, 0.80);
    if (!qFuzzyCompare(m_chatBackgroundOpacity, bounded)) {
        m_chatBackgroundOpacity = bounded;
        saveSession();
        emit settingsChanged();
    }
}

QString ZaloClient::currentConversationBackground() const {
    if (m_chatBackgroundMode == QLatin1String("avatar")) return m_currentConvAvatar;
    if (m_chatBackgroundMode == QLatin1String("custom") && !m_chatBackgroundImage.isEmpty())
        return QUrl::fromLocalFile(m_chatBackgroundImage).toString();
    return QString();
}

void ZaloClient::setChatBackgroundImage(const QString& urlOrPath) {
    const QString source = urlOrPath.startsWith(QLatin1String("file:"))
                              ? QUrl(urlOrPath).toLocalFile() : urlOrPath;
    if (source.isEmpty() || !QFileInfo::exists(source)) return;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString suffix = QFileInfo(source).suffix().toLower();
    const QString target = dir + QStringLiteral("/background.") + (suffix.isEmpty() ? QStringLiteral("png") : suffix);

    if (QFileInfo(source).absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
        QFile::remove(target);
        if (!QFile::copy(source, target)) return;
    }

    m_chatBackgroundImage = target;
    m_chatBackgroundMode = QStringLiteral("custom");
    saveSession();
    emit settingsChanged();
    emit chatBackgroundImageChanged();
    emit currentConversationBackgroundChanged();
}

void ZaloClient::clearChatBackgroundImage() {
    bool changed = false;
    if (!m_chatBackgroundImage.isEmpty()) {
        QFile::remove(m_chatBackgroundImage);
        m_chatBackgroundImage.clear();
        changed = true;
        emit chatBackgroundImageChanged();
    }
    if (m_chatBackgroundMode == QLatin1String("custom")) {
        m_chatBackgroundMode = QStringLiteral("none");
        changed = true;
    }
    if (changed) {
        saveSession();
        emit settingsChanged();
        emit currentConversationBackgroundChanged();
    }
}

void ZaloClient::setShowLabelFolders(bool val) {
    if (m_showLabelFolders != val) {
        m_showLabelFolders = val;
        saveSession();
        emit settingsChanged();
    }
}

void ZaloClient::setStartHiddenInTray(bool val) {
    if (m_startHiddenInTray != val) {
        m_startHiddenInTray = val;
        saveSession();
        emit settingsChanged();
    }
}

void ZaloClient::setBlockTyping(bool val) {
    if (m_blockTyping != val) {
        m_blockTyping = val;
        saveSession();
        emit privacySettingsChanged();
    }
}

void ZaloClient::setBlockDelivered(bool val) {
    if (m_blockDelivered != val) {
        m_blockDelivered = val;
        saveSession();
        emit privacySettingsChanged();
    }
}

void ZaloClient::setBlockSeen(bool val) {
    if (m_blockSeen != val) {
        m_blockSeen = val;
        saveSession();
        emit privacySettingsChanged();
    }
}

void ZaloClient::setCloseToTray(bool val) {
    if (m_closeToTray == val) return;
    m_closeToTray = val;
    saveSession();
    emit settingsChanged();
}

bool ZaloClient::trayAvailable() const {
    return QSystemTrayIcon::isSystemTrayAvailable();
}

QString ZaloClient::autostartFilePath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/autostart/zalos.desktop";
}

bool ZaloClient::startWithSystem() const {
    return QFile::exists(autostartFilePath());
}

void ZaloClient::setStartWithSystem(bool val) {
    const QString path = autostartFilePath();
    if (val == QFile::exists(path)) return;
    if (!val) {
        QFile::remove(path);
    } else {
        const QString exe = qEnvironmentVariable("APPIMAGE", QCoreApplication::applicationFilePath());
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit notice(L("Không tạo được mục khởi động cùng hệ thống.", "Couldn't create the autostart entry."));
            return;
        }
        QString quoted = exe;
        quoted.replace('"', "\\\"");
        file.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Zalos\nComment=Zalo cho Linux\n"
                                  "Exec=\"%1\" --autostart\nIcon=zalos\nTerminal=false\nStartupNotify=false\n"
                                  "X-GNOME-Autostart-enabled=true\n").arg(quoted).toUtf8());
    }
    emit settingsChanged();
}

void ZaloClient::setSelectedRingtone(const QString& val) {
    if (m_selectedRingtone != val) {
        m_selectedRingtone = val;
        saveSession();
        emit soundSettingsChanged();
    }
}

void ZaloClient::setSelectedNotiSound(const QString& val) {
    if (m_selectedNotiSound != val) {
        m_selectedNotiSound = val;
        saveSession();
        emit soundSettingsChanged();
    }
}

void ZaloClient::playSoundPreview(const QString& soundType, const QString& soundKey) {
    m_currentPreviewSound = soundKey;
    m_isPreviewPlaying = true;
    emit previewPlayingChanged();
    m_sound->play(soundType == "ringtone" ? "ringtone" : "notification", soundKey, false);

    if (!m_previewTimer) {
        m_previewTimer = new QTimer(this);
        m_previewTimer->setSingleShot(true);
        connect(m_previewTimer, &QTimer::timeout, this, [this]() {
            m_isPreviewPlaying = false;
            m_currentPreviewSound.clear();
            emit previewPlayingChanged();
        });
    }
    m_previewTimer->start(Platform::SoundEngine::isSystemKey(soundKey) ? 8000
                                                                         : qMax(300, Platform::SoundEngine::durationMs(soundKey)));
}

void ZaloClient::stopSoundPreview() {
    if (m_previewTimer) m_previewTimer->stop();
    m_sound->stop();
    m_isPreviewPlaying = false;
    m_currentPreviewSound.clear();
    emit previewPlayingChanged();
}

#ifdef ZALOS_TESTING
void ZaloClient::triggerSimulatedIncomingCall(const QString& callerName) {
    m_incomingCallerName = callerName.isEmpty() ? L("Người gọi Zalo", "Zalo caller") : callerName;
    m_incomingCallActive = true;
    m_callEngine->startCall("peer-incoming", m_incomingCallerName, false);
    emit incomingCallStateChanged(true);

    notifyIncomingCallDbus(m_incomingCallerName);
}
#endif

void ZaloClient::answerCall(bool withCamera) {
    m_incomingCallActive = false;
    m_callEngine->answerCall(withCamera);
    if (m_calls) m_calls->accept(!withCamera);
    emit incomingCallStateChanged(false);
}

void ZaloClient::declineCall() {
    m_incomingCallActive = false;
    m_callEngine->declineCall();
    if (m_calls) m_calls->reject();
    emit incomingCallStateChanged(false);
}

QVariantMap ZaloClient::getDiskUsage(const QString& path) {
    auto usage = Fs::FileUtils::getDiskUsage(path);
    QVariantMap map;
    map["available"] = usage.available;
    map["free"] = usage.free;
    map["total"] = usage.total;
    return map;
}

QVariantMap ZaloClient::getDirectorySize(const QString& path) {
    auto res = Fs::FileUtils::getDirectorySize(path);
    QVariantMap map;
    map["path"] = res.path;
    map["totalSize"] = res.totalSize;
    map["fileCount"] = res.fileCount;
    map["dirCount"] = res.dirCount;
    map["durationMs"] = res.durationMs;
    return map;
}

bool ZaloClient::generateThumbnail(const QString& inputPath, const QString& outputPath, int width, int height) {
    return Media::MediaUtils::generateThumbnailFs(inputPath, outputPath, width, height);
}

bool ZaloClient::extractVideoThumbnail(const QString& videoPath, const QString& outputPath) {
    return Media::MediaUtils::extractVideoThumbnail(videoPath, outputPath);
}

QString ZaloClient::recognizeImageText(const QString& imagePath) {
    return Media::MediaUtils::recognizeTextFromImage(imagePath);
}

void ZaloClient::syncOfflineData() {
    m_offlineStore->startSyncRecovery();
    if (m_isLoggedIn && !m_isConnected) m_apiClient->session()->connectRealtime();
    else if (m_restoringSession && m_restoreRetryTimer->isActive()) m_restoreRetryTimer->start(0);
}

QVariantMap ZaloClient::parseBinNetPayload(const QByteArray& payload) {
    QVariantMap res;
    if (payload.isEmpty()) return res;

    auto msg = Crypto::DbCross::parseBinNet(reinterpret_cast<const uint8_t*>(payload.constData()), payload.size());
    res["text"] = QString::fromStdString(msg.text);
    res["dataSource"] = QString::fromStdString(msg.dataSource);
    res["quoteStatus"] = msg.quoteStatus;

    if (!msg.quote.msg.empty() || msg.quote.ownerId != 0) {
        QVariantMap q;
        q["ownerId"] = static_cast<qint64>(msg.quote.ownerId);
        q["cliMsgId"] = static_cast<qint64>(msg.quote.cliMsgId);
        q["msg"] = QString::fromStdString(msg.quote.msg);
        res["quote"] = q;
    }

    QVariantList attachList;
    for (const auto& a : msg.attachs) {
        QVariantMap att;
        att["type"] = QString::fromStdString(a.type);
        att["id"] = a.id;
        att["title"] = QString::fromStdString(a.title);
        att["href"] = QString::fromStdString(a.href);
        att["thumb"] = QString::fromStdString(a.thumb);
        att["ext"] = QString::fromStdString(a.ext);
        attachList.append(att);
    }
    res["attachs"] = attachList;

    QVariantList mentionList;
    for (const auto& m : msg.mentions) {
        QVariantMap men;
        men["uid"] = static_cast<qint64>(m.uid);
        men["pos"] = m.pos;
        men["len"] = m.len;
        mentionList.append(men);
    }
    res["mentions"] = mentionList;
    return res;
}

bool ZaloClient::extractZdbBackup(const QString& zdbPath, const QString& outputDir, const QString& privateKey) {
    return Crypto::DbCross::decompressAndDecryptZdb4(zdbPath.toStdString(), outputDir.toStdString(), privateKey.toStdString());
}

QVariantMap ZaloClient::deriveSrtpKeys(const QString& sessId) {
    QVariantMap res;
    QByteArray master30 = sessId.toUtf8();
    if (master30.size() < 30) {
        master30 = master30.leftJustified(30, '0');
    }
    auto keys = Call::ZCallEngine::deriveSrtpSessionKeys(master30);
    res["encKey"] = keys.encKey.toHex();
    res["authKey"] = keys.authKey.toHex();
    res["saltKey"] = keys.saltKey.toHex();
    return res;
}

void ZaloClient::notifyIncomingCallDbus(const QString& callerName) {
    Platform::DesktopNotifications::Request request;
    request.title = m_antiPeeking || m_appLock->locked() ? L("Cuộc gọi đến", "Incoming call")
                                  : L("Cuộc gọi đến từ %1", "Incoming call from %1").arg(callerName);
    request.body = L("Đang đổ chuông...", "Ringing...");
    request.icon = QStringLiteral("call-start");
    request.category = QStringLiteral("call.incoming");
    request.actions = {{QStringLiteral("default"), L("Mở", "Open")},
                       {QStringLiteral("answer"), L("Trả lời", "Answer")},
                       {QStringLiteral("decline"), L("Từ chối", "Decline")}};
    request.urgency = 2;
    request.timeoutMs = 0;
    m_notifications->show(QStringLiteral("call"), request);
}

void ZaloClient::requestScreenshot(bool hideWindow) {
    if (m_currentConvId.isEmpty()) {
        const QString cloudId = myCloudId();
        if (!cloudId.isEmpty()) {
            openMyCloud();
        } else if (!m_conversationModel->conversations().isEmpty()) {
            const auto first = m_conversationModel->conversations().first();
            selectConversation(first.id, first.name, first.avatarUrl);
        }
    }
    m_screenshotTarget = m_currentConvId;
    if (hideWindow) {
        emit requestHideForScreenshot();
        QTimer::singleShot(350, this, [this]() { executeScreenshotCapture(); });
        return;
    }
    executeScreenshotCapture();
}

void ZaloClient::executeScreenshotCapture() {
    const QString tempPath = QDir::tempPath() + QStringLiteral("/zalos_shot_%1.png").arg(QDateTime::currentMSecsSinceEpoch());

    auto onFinishedCapture = [this, tempPath](bool ok) {
        emit requestRestoreAfterScreenshot();
        if (ok && QFile::exists(tempPath) && QFileInfo(tempPath).size() > 500) {
            if (m_screenshotTarget.isEmpty()) m_screenshotTarget = m_currentConvId;
            if (m_screenshotTarget != m_currentConvId && !m_screenshotTarget.isEmpty()) {
                const auto conv = m_conversationModel->conversation(m_screenshotTarget);
                selectConversation(m_screenshotTarget, conv.name, conv.avatarUrl);
            }
            sendFile(tempPath);
            emit notice(L("Đã chụp và gửi ảnh màn hình thành công", "Screenshot captured and sent successfully"));
        }
    };

    const QString spectacle = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
    if (!spectacle.isEmpty()) {
        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [proc, onFinishedCapture](int exitCode, QProcess::ExitStatus) {
            proc->deleteLater();
            onFinishedCapture(exitCode == 0);
        });
        proc->start(spectacle, {QStringLiteral("-r"), QStringLiteral("-b"), QStringLiteral("-n"), QStringLiteral("-o"), tempPath});
        return;
    }

    const QString gnomeShot = QStandardPaths::findExecutable(QStringLiteral("gnome-screenshot"));
    if (!gnomeShot.isEmpty()) {
        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [proc, onFinishedCapture](int exitCode, QProcess::ExitStatus) {
            proc->deleteLater();
            onFinishedCapture(exitCode == 0);
        });
        proc->start(gnomeShot, {QStringLiteral("-a"), QStringLiteral("-f"), tempPath});
        return;
    }

    const QString flameshot = QStandardPaths::findExecutable(QStringLiteral("flameshot"));
    if (!flameshot.isEmpty()) {
        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [proc, onFinishedCapture](int exitCode, QProcess::ExitStatus) {
            proc->deleteLater();
            onFinishedCapture(exitCode == 0);
        });
        proc->start(flameshot, {QStringLiteral("gui"), QStringLiteral("-r"), QStringLiteral("-p"), tempPath});
        return;
    }

    const QString grim = QStandardPaths::findExecutable(QStringLiteral("grim"));
    const QString slurp = QStandardPaths::findExecutable(QStringLiteral("slurp"));
    if (!grim.isEmpty()) {
        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [proc, onFinishedCapture](int exitCode, QProcess::ExitStatus) {
            proc->deleteLater();
            onFinishedCapture(exitCode == 0);
        });
        if (!slurp.isEmpty()) {
            proc->start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("grim -g \"$(slurp)\" \"%1\"").arg(tempPath)});
        } else {
            proc->start(grim, {tempPath});
        }
        return;
    }

    const QString maim = QStandardPaths::findExecutable(QStringLiteral("maim"));
    if (!maim.isEmpty()) {
        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [proc, onFinishedCapture](int exitCode, QProcess::ExitStatus) {
            proc->deleteLater();
            onFinishedCapture(exitCode == 0);
        });
        proc->start(maim, {QStringLiteral("-s"), tempPath});
        return;
    }

    if (QGuiApplication::platformName().contains(QLatin1String("xcb"))) {
        QScreen* screen = QGuiApplication::primaryScreen();
        if (screen) {
            QPixmap pix = screen->grabWindow(0);
            if (!pix.isNull() && pix.save(tempPath, "PNG")) {
                onFinishedCapture(true);
                return;
            }
        }
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit requestRestoreAfterScreenshot();
        emit notice(L("Không tìm thấy công cụ chụp màn hình (cài đặt spectacle, gnome-screenshot hoặc flameshot)",
                      "No screenshot utility found (please install spectacle, gnome-screenshot, or flameshot)"));
        return;
    }
    if (!m_screenshotRequest.isEmpty())
        bus.disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), m_screenshotRequest,
                       QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                       this, SLOT(onScreenshotResponse(uint,QVariantMap)));
    const QString token = QStringLiteral("zalos_shot_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    m_screenshotRequest = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), m_screenshotRequest,
                QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                this, SLOT(onScreenshotResponse(uint,QVariantMap)));

    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
                                                      QStringLiteral("/org/freedesktop/portal/desktop"),
                                                      QStringLiteral("org.freedesktop.portal.Screenshot"),
                                                      QStringLiteral("Screenshot"));
    QVariantMap options{{QStringLiteral("interactive"), true}, {QStringLiteral("modal"), true}, {QStringLiteral("handle_token"), token}};
    msg << QString() << options;
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, tempPath](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (!w->isError()) {
            QDBusPendingReply<QDBusObjectPath> reply = *w;
            if (reply.isValid() && !reply.value().path().isEmpty()) {
                const QString actualPath = reply.value().path();
                if (actualPath != m_screenshotRequest) {
                    QDBusConnection bus = QDBusConnection::sessionBus();
                    bus.disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), m_screenshotRequest,
                                   QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                                   this, SLOT(onScreenshotResponse(uint,QVariantMap)));
                    m_screenshotRequest = actualPath;
                    bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), m_screenshotRequest,
                                QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                                this, SLOT(onScreenshotResponse(uint,QVariantMap)));
                }
            }
            return;
        }

        m_screenshotRequest.clear();
        QScreen* screen = QGuiApplication::primaryScreen();
        if (screen) {
            QPixmap pix = screen->grabWindow(0);
            if (!pix.isNull() && pix.save(tempPath, "PNG")) {
                emit requestRestoreAfterScreenshot();
                sendFile(tempPath);
                emit notice(L("Đã chụp và gửi ảnh màn hình toàn cảnh", "Captured and sent full screen screenshot"));
                return;
            }
        }
        emit requestRestoreAfterScreenshot();
        emit notice(L("Không mở được công cụ chụp màn hình (hãy cài đặt spectacle, gnome-screenshot hoặc xdg-desktop-portal)",
                      "Couldn't open screenshot tool (needs spectacle, gnome-screenshot, or xdg-desktop-portal)"));
    });
}

void ZaloClient::onScreenshotResponse(uint response, const QVariantMap& results) {
    QDBusConnection::sessionBus().disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), m_screenshotRequest,
                                             QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
                                             SLOT(onScreenshotResponse(uint,QVariantMap)));
    m_screenshotRequest.clear();
    emit requestRestoreAfterScreenshot();
    if (response != 0) {
        const QString tempPath = QDir::tempPath() + QStringLiteral("/zalos_shot_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
        QScreen* screen = QGuiApplication::primaryScreen();
        if (screen) {
            QPixmap pix = screen->grabWindow(0);
            if (!pix.isNull() && pix.save(tempPath, "PNG")) {
                sendFile(tempPath);
                emit notice(L("Đã chụp và gửi ảnh màn hình toàn cảnh", "Captured and sent full screen screenshot"));
                return;
            }
        }
        return;
    }
    const QString uri = results.value(QStringLiteral("uri")).toString();
    if (uri.isEmpty() || m_screenshotTarget.isEmpty()) return;
    if (m_screenshotTarget != m_currentConvId) {
        emit notice(L("Đã chuyển hội thoại nên ảnh chụp chưa được gửi", "The conversation changed; the screenshot was not sent"));
        return;
    }
    sendFile(uri);
}

void ZaloClient::openBusinessTools() {
    m_businessTab = QStringLiteral("profile");
    m_showBusinessToolsModal = true;
    emit businessTabChanged();
    emit businessToolsModalChanged();
}

void ZaloClient::closeBusinessTools() {
    m_showBusinessToolsModal = false;
    emit businessToolsModalChanged();
}

void ZaloClient::openBusinessCatalog() {
    m_businessTab = QStringLiteral("catalog");
    m_showBusinessToolsModal = true;
    emit businessTabChanged();
    emit businessToolsModalChanged();
}

void ZaloClient::openBusinessAutoReply() {
    m_businessTab = QStringLiteral("autoreply");
    m_showBusinessToolsModal = true;
    emit businessTabChanged();
    emit businessToolsModalChanged();
}

void ZaloClient::openBusinessPackage() {
    m_businessTab = QStringLiteral("package");
    m_showBusinessToolsModal = true;
    emit businessTabChanged();
    emit businessToolsModalChanged();
}

void ZaloClient::setBusinessTab(const QString& tab) {
    if (tab == m_businessTab) return;
    m_businessTab = tab;
    emit businessTabChanged();
}

void ZaloClient::setShowBusinessToolsModal(bool show) {
    if (show == m_showBusinessToolsModal) return;
    m_showBusinessToolsModal = show;
    emit businessToolsModalChanged();
}

void ZaloClient::sendProductToCurrentChat(const QString& name, const QString& price, const QString& description, const QString& imageUrl) {
    if (m_currentConvId.isEmpty()) {
        emit notice(L("Vui lòng mở một cuộc trò chuyện để gửi sản phẩm", "Please open a conversation to send product"));
        return;
    }
    QString text = QStringLiteral("🛍️ %1\n💰 %2").arg(name, price);
    if (!description.trimmed().isEmpty()) text += QStringLiteral("\n📝 %1").arg(description.trimmed());
    sendMessage(text);
    if (!imageUrl.trimmed().isEmpty()) {
        sendFile(imageUrl);
    }
    closeBusinessTools();
    emit notice(L("Đã gửi thông tin sản phẩm vào hội thoại", "Product sent to conversation"));
}

void ZaloClient::printMedia(const QString& url) {
    if (url.isEmpty()) return;
    fetchBytes(url, [this](const QByteArray& data, const QString& error) {
        if (!error.isEmpty() || data.isEmpty()) {
            emit notice(L("Không tải được hình ảnh để in.", "Couldn't load picture for printing."));
            return;
        }
        const QString tempPath = QDir::tempPath() + QStringLiteral("/zalos_print_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
        QFile f(tempPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(data);
            f.close();
            if (!QProcess::startDetached("lpr", {tempPath})) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath));
            }
            emit notice(L("Đã gửi lệnh in.", "Sent to printer."));
        }
    });
}

QVariantMap ZaloClient::windowGeometry() const {
    QSettings settings("Zalos", "ZalosApp");
    QVariantMap map;
    map["x"] = settings.value(QStringLiteral("window/x"), -1);
    map["y"] = settings.value(QStringLiteral("window/y"), -1);
    map["width"] = settings.value(QStringLiteral("window/width"), 1040);
    map["height"] = settings.value(QStringLiteral("window/height"), 680);
    map["maximized"] = settings.value(QStringLiteral("window/maximized"), false);
    return map;
}

void ZaloClient::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings("Zalos", "ZalosApp");
    if (!maximized && width > 0 && height > 0) {
        settings.setValue(QStringLiteral("window/x"), x);
        settings.setValue(QStringLiteral("window/y"), y);
        settings.setValue(QStringLiteral("window/width"), width);
        settings.setValue(QStringLiteral("window/height"), height);
    }
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

void ZaloClient::notifyWindowVisibility(bool visible) {
    if (Platform::SystemTrayController::s_instance) {
        Platform::SystemTrayController::s_instance->setWindowVisible(visible);
    }
}

QString ZaloClient::threadOfMessage(const QString& msgId) const {
    return m_messageThread.value(msgId, m_currentConvId);
}

bool ZaloClient::isGroupThread(const QString& threadId) const {
    return m_groupIds.contains(threadId) || m_conversationModel->conversation(threadId).isGroup;
}

void ZaloClient::sendFile(const QString& fileUrlOrPath, const QString& caption) {
    if (m_currentConvId.isEmpty() || !m_isLoggedIn) return;
    const QUrl url(fileUrlOrPath);
    const QString path = url.isLocalFile() ? url.toLocalFile() : fileUrlOrPath;
    const QFileInfo info(path);
    if (!info.isFile()) {
        emit notice(L("Không đọc được tệp đã chọn.", "Couldn't read the selected file."));
        return;
    }

    const QString convId = m_currentConvId;
    const bool isGroup = currentConversationIsGroup();
    const QString ext = info.suffix().toLower();
    const bool isImage = ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp";
    const bool isVideo = ext == "mp4" || ext == "mov" || ext == "webm" || ext == "mkv" || ext == "avi";

    if (!caption.trimmed().isEmpty() && !isImage) sendMessage(caption);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastClientId = qMax(now, m_lastClientId + 1);
    Models::MessageItem local;
    local.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_localSeq);
    local.cliMsgId = QString::number(m_lastClientId);
    local.ts = now;
    local.timeStr = QDateTime::fromMSecsSinceEpoch(now).toString("HH:mm");
    local.isMine = true;
    local.senderId = m_myUid;
    local.senderName = m_userName;
    local.status = QStringLiteral("sending");
    local.messageType = isImage ? QStringLiteral("image") : (isVideo ? QStringLiteral("video") : QStringLiteral("file"));
    local.mediaUrl = (isImage || isVideo) ? QUrl::fromLocalFile(path).toString() : QString();
    local.text = isImage ? caption.trimmed() : info.fileName();
    m_messageModel->mergeMessages(convId, {local});
    m_conversationModel->recordMessage(convId, isImage ? L("[Hình ảnh]", "[Photo]") : (isVideo ? L("[Video] ", "[Video] ") + info.fileName() : L("[Tệp] ", "[File] ") + info.fileName()),
                                       L("Bạn: ", "You: "), now, false);

    m_pendingAttachments++;
    emit attachmentStateChanged();
    const QString localId = local.id;
    const QString cliMsgId = local.cliMsgId;
    const qint64 ttl = m_autoDeleteTtl.value(convId, 0);
    m_apiClient->session()->sendAttachment(convId, isGroup, path, isImage ? caption.trimmed() : QString(),
        [this, convId, localId](bool ok, const QJsonValue& data, const QString& error, int code) {
            m_pendingAttachments = qMax(0, m_pendingAttachments - 1);
            emit attachmentStateChanged();
            if (!ok) {
                m_messageModel->setStatus(convId, localId, QStringLiteral("failed"));
                QString reason = error;
                if (code == -2) reason = L("tệp quá lớn", "the file is too large");
                else if (code == -3) reason = L("định dạng tệp bị Zalo chặn", "Zalo blocks this file type");
                else if (error == "upload_timeout") reason = L("máy chủ không phản hồi", "the server didn't respond");
                emit notice(L("Không gửi được tệp: ", "Couldn't send the file: ") + reason);
                return;
            }
            const QString serverId = idOf(data.toObject().value("msgId"));
            m_messageModel->setStatus(convId, localId, QStringLiteral("sent"));
            if (!serverId.isEmpty()) m_messageModel->confirmMessage(convId, localId, serverId);
        }, cliMsgId, ttl);
}

void ZaloClient::searchStickers(const QString& keyword) {
    m_stickerStore->search(keyword);
}

void ZaloClient::sendStickerItem(qint64 stickerId, qint64 cateId, int type) {
    if (m_currentConvId.isEmpty() || !m_isLoggedIn) return;
    const QString convId = m_currentConvId;
    const bool isGroup = currentConversationIsGroup();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastClientId = qMax(now, m_lastClientId + 1);
    const QString cliMsgId = QString::number(m_lastClientId);

    Models::MessageItem local;
    local.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_localSeq);
    local.cliMsgId = cliMsgId;
    local.messageType = QStringLiteral("sticker");
    local.text = L("[Nhãn dán]", "[Sticker]");
    local.mediaUrl = stickerThumb(QString::number(stickerId));
    local.ts = now;
    local.timeStr = QDateTime::fromMSecsSinceEpoch(now).toString("HH:mm");
    local.isMine = true;
    local.senderId = m_myUid;
    local.senderName = m_userName;
    local.status = QStringLiteral("sending");
    m_messageModel->mergeMessages(convId, {local});
    m_conversationModel->recordMessage(convId, L("[Nhãn dán]", "[Sticker]"), L("Bạn: ", "You: "), now, false);

    const QString localId = local.id;
    const qint64 ttl = m_autoDeleteTtl.value(convId, 0);
    m_apiClient->session()->sendSticker(convId, isGroup, stickerId, cateId, type,
        [this, convId, localId](bool ok, const QJsonValue& data, const QString& error, int code) {
            if (ok) {
                m_messageModel->setStatus(convId, localId, QStringLiteral("sent"));
                const QString serverId = idOf(data.toObject().value("msgId"));
                if (!serverId.isEmpty()) m_messageModel->confirmMessage(convId, localId, serverId);
            } else {
                m_messageModel->setStatus(convId, localId, QStringLiteral("failed"));
                emit notice(L("Không gửi được nhãn dán: ", "Couldn't send the sticker: ") + friendlyError(error, code));
            }
        }, cliMsgId, ttl);
}

QString ZaloClient::stickerThumb(const QString& stickerId) const {
    return m_stickerStore->info(stickerId).value("url").toString();
}

void ZaloClient::prefetchStickers(const QVariantList& stickers) {
    if (!m_isLoggedIn) return;
    for (const QVariant& v : stickers) {
        const QString id = v.toMap().value("id").toString();
        if (id.isEmpty() || !stickerThumb(id).isEmpty() || m_stickerPrefetching.contains(id)) continue;
        m_stickerPrefetching.insert(id);
        m_apiClient->session()->fetchStickerDetail(id.toLongLong(), [this, id](bool ok, const QJsonValue& d, const QString&, int) {
            m_stickerPrefetching.remove(id);
            const QVariantMap info = ok ? StickerStore::describe(d.toObject()) : QVariantMap();
            if (info.value("url").toString().isEmpty()) return;
            m_stickerStore->remember(info);
            ++m_stickerRevision;
            emit stickerRevisionChanged();
        });
    }
}

void ZaloClient::resolveSticker(const QString& convId, const QString& msgId, qint64 stickerId) {
    if (stickerId <= 0) return;
    const QString key = QString::number(stickerId);
    const bool alreadyRequested = m_stickerWaiters.contains(key);
    m_stickerWaiters[key].append({convId, msgId});
    if (alreadyRequested) return;
    m_stickerQueue.append(key);
    pumpStickerQueue();
}

void ZaloClient::pumpStickerQueue() {
    if (!m_isLoggedIn) return;
    while (m_stickerInFlight < 4 && !m_stickerQueue.isEmpty()) {
        const QString key = m_stickerQueue.takeFirst();
        ++m_stickerInFlight;
        m_apiClient->session()->fetchStickerDetail(key.toLongLong(), [this, key](bool ok, const QJsonValue& d, const QString&, int) {
        --m_stickerInFlight;
        QTimer::singleShot(0, this, &ZaloClient::pumpStickerQueue);
        const QVariantMap info = ok ? StickerStore::describe(d.toObject()) : QVariantMap();
        const QString url = info.value("url").toString();
        if (url.isEmpty()) {
            const int attempt = ++m_stickerAttempts[key];
            if (attempt < 3) {
                QTimer::singleShot(30000, this, [this, key]() {
                    if (m_stickerWaiters.contains(key) && !m_stickerQueue.contains(key)) {
                        m_stickerQueue.append(key);
                        pumpStickerQueue();
                    }
                });
            } else {
                m_stickerWaiters.remove(key);
            }
            return;
        }
        m_stickerAttempts.remove(key);
        const auto waiters = m_stickerWaiters.take(key);
        m_stickerStore->remember(info);
        for (const auto& w : waiters) {
            m_messageModel->setSticker(w.first, w.second, info);
            persistMessage(w.first, w.second);
        }
        });
    }
}

namespace {
QString localPath(const QString& fileUrlOrPath) {
    const QUrl url(fileUrlOrPath);
    return url.isLocalFile() ? url.toLocalFile() : fileUrlOrPath;
}
}

void ZaloClient::changeMyAvatar(const QString& fileUrlOrPath) {
    if (!m_isLoggedIn) {
        emit notice(L("Cần kết nối tới Zalo để đổi ảnh đại diện.", "Connect to Zalo to change your avatar."));
        return;
    }
    emit notice(L("Đang tải ảnh đại diện lên…", "Uploading your avatar…"));
    m_apiClient->session()->uploadProfileAvatar(localPath(fileUrlOrPath), [this](bool ok, const QJsonValue&, const QString& error, int code) {
        if (!ok) {
            emit notice(L("Không đổi được ảnh đại diện: ", "Couldn't change your avatar: ") + friendlyError(error, code));
            return;
        }
        m_apiClient->session()->fetchAccountInfo([this](bool ok2, const QJsonValue& data, const QString&, int) {
            if (ok2) {
                const QString avatar = data.toObject().value("profile").toObject().value("avatar").toString();
                if (!avatar.isEmpty()) {
                    m_userAvatar = avatar;
                    m_myProfile.avatar = avatar;
                    if (m_activeProfile.userId == m_myUid) m_activeProfile.avatar = avatar;
                    m_calls->setLocalUser(m_myUid, m_userName, m_userAvatar);
                    emit userProfileChanged();
                    emit currentProfileChanged();
                }
            }
            emit notice(L("Đã đổi ảnh đại diện.", "Avatar changed."));
        });
    });
}

void ZaloClient::findUserByUsername(const QString& username) {
    const QString uname = username.trimmed();
    if (!m_isLoggedIn) {
        emit userSearchFinished(false, {}, L("Bạn cần đăng nhập trước.", "Please sign in first."));
        return;
    }
    if (uname.isEmpty()) {
        emit userSearchFinished(false, {}, L("Nhập tên người dùng để tìm.", "Enter a username to search."));
        return;
    }
    m_apiClient->session()->findUserByUsername(uname, [this](bool ok, const QJsonValue& data, const QString& error, int code) {
        const QJsonObject obj = data.toObject();
        const QString uid = idOf(obj.value("uid"));
        if (!ok || uid.isEmpty()) {
            emit userSearchFinished(false, {}, code == 216 || ok
                ? L("Không tìm thấy người dùng với tên này.", "No user found with this username.")
                : L("Không tìm được: ", "Search failed: ") + friendlyError(error));
            return;
        }
        QVariantMap profile;
        profile["uid"] = uid;
        profile["name"] = pickName(obj);
        profile["avatar"] = obj.value("avatar").toString();
        m_users[uid] = {profile["name"].toString(), profile["avatar"].toString(), 0};
        noteAccountFlags(uid, obj);
        emit userSearchFinished(true, profile, QString());
    });
}

void ZaloClient::forwardMessageToMany(const QStringList& targetThreadIds, const QString& msgId) {
    int sent = 0;
    for (const QString& tid : targetThreadIds) {
        if (tid.isEmpty()) continue;
        forwardMessage(tid, msgId);
        ++sent;
    }
    if (sent > 0)
        emit notice(L("Đã chuyển tiếp tới %1 hội thoại", "Forwarded to %1 chats").arg(sent));
}

void ZaloClient::refreshSentFriendRequests() {
    if (!m_isLoggedIn || !m_apiClient->session()) return;
    m_apiClient->session()->fetchSentFriendRequests([this](bool ok, const QJsonValue& data, const QString&, int) {
        m_sentFriendRequests.clear();
        const QJsonObject obj = data.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const QJsonObject r = it.value().toObject();
            const QString uid = idOf(r.value("userId"));
            if (uid.isEmpty()) continue;
            QVariantMap m;
            m["uid"] = uid;
            m["name"] = pickName(r);
            m["avatar"] = r.value("avatar").toString();
            m["message"] = r.value("fReqInfo").toObject().value("message").toString();
            m_sentFriendRequests.append(m);
        }
        emit sentFriendRequestsChanged();
    });
}

void ZaloClient::cancelSentFriendRequest(const QString& uid) {
    if (uid.isEmpty() || !m_apiClient->session()) return;
    m_apiClient->session()->undoFriendRequest(uid, [this](bool ok, const QJsonValue&, const QString& error, int) {
        if (!ok) {
            emit notice(L("Không thể thu hồi lời mời: ", "Couldn't cancel the request: ") + friendlyError(error));
            return;
        }
        emit notice(L("Đã thu hồi lời mời kết bạn", "Friend request cancelled"));
        refreshSentFriendRequests();
    });
}

void ZaloClient::loadMyAvatarHistory() {
    if (!m_isLoggedIn || !m_apiClient->session()) return;
    m_apiClient->session()->fetchAvatarList([this](bool ok, const QJsonValue& data, const QString&, int) {
        m_myAvatarHistory.clear();
        if (ok) {
            const QJsonArray photos = data.toObject().value("photos").toArray();
            for (const QJsonValue& v : photos) {
                const QJsonObject p = v.toObject();
                QVariantMap m;
                m["photoId"] = p.value("photoId").toString();
                m["thumb"] = p.value("thumbnail").toString();
                m["url"] = p.value("url").toString();
                m_myAvatarHistory.append(m);
            }
        }
        emit myAvatarHistoryChanged();
    });
}

void ZaloClient::reuseMyAvatar(const QString& photoId) {
    if (photoId.isEmpty() || !m_apiClient->session()) return;
    m_apiClient->session()->reuseAvatar(photoId, [this](bool ok, const QJsonValue&, const QString& error, int) {
        if (!ok) {
            emit notice(L("Không thể đặt lại ảnh đại diện: ", "Couldn't set avatar: ") + friendlyError(error));
            return;
        }
        m_apiClient->session()->fetchAccountInfo([this](bool ok2, const QJsonValue& data, const QString&, int) {
            if (ok2) {
                const QString avatar = data.toObject().value("profile").toObject().value("avatar").toString();
                if (!avatar.isEmpty()) {
                    m_userAvatar = avatar;
                    m_myProfile.avatar = avatar;
                    if (m_activeProfile.userId == m_myUid) m_activeProfile.avatar = avatar;
                    m_calls->setLocalUser(m_myUid, m_userName, m_userAvatar);
                    emit userProfileChanged();
                    emit currentProfileChanged();
                }
            }
            emit notice(L("Đã cập nhật ảnh đại diện", "Avatar updated"));
        });
        loadMyAvatarHistory();
    });
}

void ZaloClient::deleteMyAvatar(const QString& photoId) {
    if (photoId.isEmpty() || !m_apiClient->session()) return;
    m_apiClient->session()->deleteAvatarPhotos({photoId}, [this](bool ok, const QJsonValue&, const QString& error, int) {
        if (!ok) {
            emit notice(L("Không thể xóa ảnh: ", "Couldn't delete photo: ") + friendlyError(error));
            return;
        }
        loadMyAvatarHistory();
    });
}

void ZaloClient::changeGroupAvatar(const QString& groupId, const QString& fileUrlOrPath) {
    if (!m_isLoggedIn || groupId.isEmpty()) {
        emit notice(L("Cần kết nối tới Zalo để đổi ảnh nhóm.", "Connect to Zalo to change the group photo."));
        return;
    }
    emit notice(L("Đang tải ảnh nhóm lên…", "Uploading the group photo…"));
    m_apiClient->session()->uploadGroupAvatar(groupId, localPath(fileUrlOrPath), [this, groupId](bool ok, const QJsonValue&, const QString& error, int code) {
        if (!ok) {
            emit notice(L("Không đổi được ảnh nhóm: ", "Couldn't change the group photo: ") + friendlyError(error, code));
            return;
        }
        loadGroupInfo({groupId});
        emit notice(L("Đã đổi ảnh nhóm.", "Group photo changed."));
    });
}

QVariantList ZaloClient::systemSounds() const {
    return Platform::SoundEngine::systemSounds();
}

QVariantList ZaloClient::reactionList() const {
    QVariantList out;
    for (const auto& r : kReactions) {
        if (QLatin1String(r.key) == QLatin1String("thanks")) continue;
        out.append(QVariantMap{{"key", QString::fromLatin1(r.key)}, {"emoji", QString::fromUtf8(r.emoji)}});
    }
    return out;
}

void ZaloClient::reactToMessage(const QString& msgId, const QString& reactionKey) {
    const QJsonObject raw = rawMessage(msgId);
    if (raw.isEmpty() || !m_isLoggedIn) return;
    const ReactionDef* def = nullptr;
    for (const auto& r : kReactions) {
        if (reactionKey == QLatin1String(r.key)) def = &r;
    }
    if (!def) return;
    const QString convId = threadOfMessage(msgId);
    const QString icon = QString::fromLatin1(def->icon);
    m_apiClient->session()->addReaction(convId, isGroupThread(convId), raw, icon, def->rType,
        [this, convId, msgId, icon](bool ok, const QJsonValue&, const QString&, int) {
            if (!ok) {
                emit notice(L("Không thả được cảm xúc.", "Couldn't add the reaction."));
                return;
            }
            addReactionEvent(msgId, m_myUid, icon, QStringLiteral("pending:%1").arg(QDateTime::currentMSecsSinceEpoch()),
                             QDateTime::currentMSecsSinceEpoch());
            refreshReactionSummary(convId, msgId);
        });
}

QVariantMap ZaloClient::reactionDetails(const QString& msgId) const {
    const QVector<Offline::ZaloOfflineStore::ReactionEvent> events =
        m_reactions.contains(msgId) ? m_reactions.value(msgId)
                                    : (m_offlineStore ? m_offlineStore->reactionEventsOf(msgId)
                                                      : QVector<Offline::ZaloOfflineStore::ReactionEvent>());
    QMap<QString, int> counts;
    QStringList order;
    struct Person { QMap<QString, int> perEmoji; QStringList emojiOrder; int total{0}; };
    QHash<QString, Person> byUser;
    QStringList userOrder;
    for (const auto& e : events) {
        const QString emoji = reactionEmoji(e.icon);
        if (!counts.contains(emoji)) order.append(emoji);
        counts[emoji]++;
        if (!byUser.contains(e.uid)) userOrder.append(e.uid);
        Person& p = byUser[e.uid];
        if (!p.perEmoji.contains(emoji)) p.emojiOrder.append(emoji);
        p.perEmoji[emoji]++;
        p.total++;
    }
    QVariantList people;
    for (const QString& uid : userOrder) {
        const Person& p = byUser.value(uid);
        const bool me = !m_myUid.isEmpty() && uid == m_myUid;
        QString name = me ? m_userName : displayNameOf(uid);
        if (name.isEmpty()) name = L("Người dùng Zalo", "Zalo user");
        QStringList emojis = p.emojiOrder;
        std::stable_sort(emojis.begin(), emojis.end(), [&p](const QString& a, const QString& b) {
            return p.perEmoji.value(a) > p.perEmoji.value(b);
        });
        QVariantMap perEmoji;
        for (auto it = p.perEmoji.cbegin(); it != p.perEmoji.cend(); ++it) perEmoji.insert(it.key(), it.value());
        people.append(QVariantMap{{"uid", uid}, {"name", name}, {"avatar", me ? m_userAvatar : m_users.value(uid).avatar},
                                  {"emoji", emojis.value(0)}, {"emojis", emojis.join(QString())}, {"count", p.total},
                                  {"perEmoji", perEmoji}, {"isMe", me}});
    }
    std::sort(people.begin(), people.end(), [](const QVariant& a, const QVariant& b) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        if (x.value("isMe").toBool() != y.value("isMe").toBool()) return x.value("isMe").toBool();
        if (x.value("count").toInt() != y.value("count").toInt()) return x.value("count").toInt() > y.value("count").toInt();
        return QString::localeAwareCompare(x.value("name").toString(), y.value("name").toString()) < 0;
    });
    QVariantList groups;
    for (const QString& emoji : order) groups.append(QVariantMap{{"emoji", emoji}, {"count", counts.value(emoji)}});
    std::stable_sort(groups.begin(), groups.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("count").toInt() > b.toMap().value("count").toInt();
    });
    return {{"total", int(events.size())}, {"known", !events.isEmpty()}, {"groups", groups}, {"people", people}};
}

void ZaloClient::removeMyReaction(const QString& msgId) {
    const QJsonObject raw = rawMessage(msgId);
    if (raw.isEmpty() || !m_isLoggedIn) return;
    bool mine = false;
    for (const auto& e : eventsOf(msgId)) mine = mine || e.uid == m_myUid;
    if (!mine) return;
    const QString convId = threadOfMessage(msgId);
    m_apiClient->session()->addReaction(convId, isGroupThread(convId), raw, QString(), -1,
        [this, convId, msgId](bool ok, const QJsonValue&, const QString&, int) {
            if (!ok) {
                emit notice(L("Không gỡ được cảm xúc.", "Couldn't remove the reaction."));
                return;
            }
            clearReactionsOf(msgId, m_myUid);
            refreshReactionSummary(convId, msgId);
        });
}

void ZaloClient::handleReactions(const QJsonArray& reactions, bool isGroup) {
    for (const QJsonValue& v : reactions) {
        const QJsonObject r = v.toObject();
        QString uidFrom = idOf(r.value("uidFrom"));
        const bool self = uidFrom == "0" || uidFrom == m_myUid;
        if (self) uidFrom = m_myUid;
        const QString thread = (isGroup || self) ? idOf(r.value("idTo")) : uidFrom;
        const QJsonObject content = r.value("content").toObject();
        QString icon = content.value("rIcon").toString();
        const int rType = content.value("rType").toInt(-1);
        if (icon.isEmpty() && rType >= 0) {
            for (const auto& def : kReactions) {
                if (def.rType == rType) { icon = QString::fromLatin1(def.icon); break; }
            }
        }
        QString eventId = idOf(r.value("msgId"));
        if (eventId.isEmpty()) eventId = idOf(r.value("actionId"));
        if (eventId.isEmpty()) eventId = idOf(r.value("cliMsgId"));
        qint64 ts = r.value("ts").toVariant().toLongLong();
        if (ts > 0 && ts < 10000000000LL) ts *= 1000;
        const bool removal = icon.isEmpty() || rType < 0;
        for (const QJsonValue& target : content.value("rMsg").toArray()) {
            const QJsonObject tObj = target.toObject();
            QString msgId = idOf(tObj.value("gMsgID"));
            if (msgId.isEmpty() || msgId == QLatin1String("0")) {
                msgId = idOf(tObj.value("cMsgID"));
            }
            if (msgId.isEmpty() || msgId == QLatin1String("0")) continue;
            if (removal) {
                clearReactionsOf(msgId, uidFrom, ts);
            } else {
                const QString id = eventId.isEmpty()
                    ? QStringLiteral("anon:%1:%2:%3").arg(uidFrom, icon).arg(ts) : eventId;
                if (self) dropPendingReaction(msgId, icon);
                addReactionEvent(msgId, uidFrom, icon, id, ts);
            }
            refreshReactionSummary(m_messageThread.value(msgId, thread == "0" ? m_myUid : thread), msgId);
        }
    }
}

QVector<Offline::ZaloOfflineStore::ReactionEvent>& ZaloClient::eventsOf(const QString& msgId) {
    auto it = m_reactions.find(msgId);
    if (it == m_reactions.end()) {
        it = m_reactions.insert(msgId, m_offlineStore ? m_offlineStore->reactionEventsOf(msgId)
                                                      : QVector<Offline::ZaloOfflineStore::ReactionEvent>());
    }
    return it.value();
}

bool ZaloClient::hasReactions(const QString& msgId) {
    if (!m_reactedIdsLoaded && m_offlineStore && !m_offlineStore->ownerUid().isEmpty()) {
        m_reactedIds = m_offlineStore->reactedMessageIds();
        m_reactedIdsLoaded = true;
    }
    return m_reactedIds.contains(msgId) || !m_reactions.value(msgId).isEmpty();
}

void ZaloClient::addReactionEvent(const QString& msgId, const QString& uid, const QString& icon, const QString& eventId,
                                  qint64 ts) {
    auto& events = eventsOf(msgId);
    for (const auto& e : std::as_const(events)) {
        if (e.eventId == eventId) return;
    }
    events.append({uid, icon, eventId, ts});
    m_reactedIds.insert(msgId);
    if (m_offlineStore) m_offlineStore->addReactionEvent(msgId, uid, icon, eventId, ts);
}

void ZaloClient::dropPendingReaction(const QString& msgId, const QString& icon) {
    auto& events = eventsOf(msgId);
    for (int i = 0; i < events.size(); ++i) {
        const auto& e = events.at(i);
        if (e.uid == m_myUid && e.icon == icon && e.eventId.startsWith(QLatin1String("pending:"))) {
            if (m_offlineStore) m_offlineStore->removeReactionEvent(msgId, e.eventId);
            events.removeAt(i);
            return;
        }
    }
}

void ZaloClient::clearReactionsOf(const QString& msgId, const QString& uid, qint64 clearTs) {
    auto& events = eventsOf(msgId);
    events.erase(std::remove_if(events.begin(), events.end(), [&uid, clearTs](const auto& e) {
        return e.uid == uid && (clearTs <= 0 || e.ts <= clearTs);
    }), events.end());
    if (m_offlineStore) m_offlineStore->clearReactions(msgId, uid);
}

void ZaloClient::refreshReactionSummary(const QString& convId, const QString& msgId) {
    QMap<QString, int> counts;
    QStringList order;
    for (const auto& e : eventsOf(msgId)) {
        const QString emoji = reactionEmoji(e.icon);
        if (!counts.contains(emoji)) order.append(emoji);
        counts[emoji]++;
    }
    std::stable_sort(order.begin(), order.end(), [&counts](const QString& a, const QString& b) {
        return counts.value(a) > counts.value(b);
    });
    int total = 0;
    for (int n : std::as_const(counts)) total += n;
    const QString summary = order.isEmpty() ? QString()
        : order.mid(0, 3).join("") + (total > 1 ? " " + QString::number(total) : QString());
    m_messageModel->setReaction(convId, msgId, summary);
    if (m_offlineStore) m_offlineStore->setMessageReaction(msgId, summary);
}

bool ZaloClient::canRecall(const QString& msgId) const {
    const QJsonObject raw = rawMessage(msgId);
    if (raw.isEmpty()) return false;
    const QString from = idOf(raw.value("uidFrom"));
    return from == "0" || from == m_myUid;
}

void ZaloClient::recallMessage(const QString& msgId) {
    const QJsonObject raw = rawMessage(msgId);
    if (raw.isEmpty() || !m_isLoggedIn) return;
    const QString convId = threadOfMessage(msgId);
    m_apiClient->session()->undoMessage(convId, isGroupThread(convId), raw,
        [this, convId, msgId](bool ok, const QJsonValue&, const QString& error, int) {
            if (!ok) {
                emit notice(L("Không thu hồi được tin nhắn: ", "Couldn't recall the message: ") + friendlyError(error));
                return;
            }
            m_messageModel->markRecalled(convId, msgId, L("Tin nhắn đã được thu hồi", "Message recalled"));
            persistMessage(convId, msgId);
        });
}

void ZaloClient::deleteMessageForMe(const QString& msgId) {
    const QJsonObject raw = rawMessage(msgId);
    const QString convId = threadOfMessage(msgId);
    if (raw.isEmpty() || !m_isLoggedIn) {
        m_messageModel->removeMessage(convId, msgId);
        return;
    }
    m_apiClient->session()->deleteMessage(convId, isGroupThread(convId), raw, true,
        [this, convId, msgId](bool ok, const QJsonValue&, const QString& error, int) {
            if (!ok) {
                emit notice(L("Không xóa được tin nhắn: ", "Couldn't delete the message: ") + friendlyError(error));
                return;
            }
            m_messageModel->removeMessage(convId, msgId);
        });
}

QStringList ZaloClient::searchConversationMessages(const QString& query) {
    QStringList ids;
    const QString convId = m_currentConvId;
    if (convId.isEmpty() || query.trimmed().isEmpty()) return ids;
    QSet<QString> seen;
    if (cacheUsable()) {
        const auto hits = m_offlineStore->searchMessages(query, convId, 500);
        for (auto it = hits.crbegin(); it != hits.crend(); ++it) {
            if (!seen.contains(it->msgId)) { seen.insert(it->msgId); ids << it->msgId; }
        }
    }
    const QList<int> rows = m_messageModel->searchMessages(query);
    QStringList extra;
    for (int row : rows) {
        const QString id = m_messageModel->data(m_messageModel->index(row, 0), Models::MessageModel::IdRole).toString();
        if (!id.isEmpty() && !seen.contains(id)) { seen.insert(id); extra << id; }
    }
    if (extra.isEmpty()) return ids;
    ids << extra;
    QHash<QString, qint64> tsOf;
    for (const QString& id : ids) {
        const QVariantMap m = m_messageModel->getMessageById(id);
        tsOf.insert(id, m.isEmpty() && cacheUsable() ? m_offlineStore->messageTs(id) : m.value("ts").toLongLong());
    }
    std::stable_sort(ids.begin(), ids.end(), [&tsOf](const QString& a, const QString& b) { return tsOf.value(a) < tsOf.value(b); });
    return ids;
}

bool ZaloClient::revealMessage(const QString& msgId) {
    if (msgId.isEmpty()) return false;
    if (m_messageModel->rowOfMessage(msgId) >= 0) return true;
    const QString convId = m_currentConvId;
    if (convId.isEmpty() || !cacheUsable()) return false;
    QString owner;
    const qint64 ts = m_offlineStore->messageTs(msgId, &owner);
    const qint64 oldest = m_messageModel->oldestTimestamp(convId);
    if (ts <= 0 || owner != convId || oldest <= 0 || ts >= oldest) return false;
    const int between = m_offlineStore->countMessagesBetween(convId, ts, oldest);
    QHash<QString, QJsonObject> raws;
    const auto rows = m_offlineStore->loadMessages(convId, &raws, between + 20, oldest);
    mergeCacheRows(convId, rows, raws);
    return m_messageModel->rowOfMessage(msgId) >= 0;
}

void ZaloClient::openSearchResult(const QString& id, const QString& name, const QString& avatar, bool isGroup) {
    if (id.isEmpty()) return;
    if (!m_conversationModel->contains(id)) {
        ensureConversation(id, isGroup);
        m_conversationModel->updateProfile(id, name, avatar);
    }
    setCurrentNavigationTab("messages");
    selectConversation(id, name, avatar);
}

void ZaloClient::openMessageFromSearch(const QString& convId, const QString& msgId) {
    if (convId.isEmpty()) return;
    if (convId != m_currentConvId) {
        const Models::ConversationItem conv = m_conversationModel->conversation(convId);
        selectConversation(convId, conv.name, conv.avatarUrl);
    }
    if (revealMessage(msgId)) emit revealRequested(msgId);
    else emit notice(L("Không mở được tin nhắn này trong lịch sử đã lưu", "Couldn't open this message in the stored history"));
}

void ZaloClient::markMediaBroken(const QString& url) {
    if (url.isEmpty() || m_brokenMedia.contains(url)) return;
    if (m_brokenMedia.size() >= 2000) m_brokenMedia.clear();
    m_brokenMedia.insert(url);
    ++m_brokenMediaRevision;
    emit brokenMediaChanged();
}

void ZaloClient::retryMedia(const QString& url) {
    if (!m_brokenMedia.remove(url)) return;
    ++m_brokenMediaRevision;
    emit brokenMediaChanged();
}

void ZaloClient::copyText(const QString& text) {
    if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(text);
}

void ZaloClient::notifyTyping() {
    if (!m_isLoggedIn || m_blockTyping || m_currentConvId.isEmpty() || m_currentConvId == myCloudId()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastTypingSent < 4000) return;
    m_lastTypingSent = now;
    m_apiClient->session()->sendTyping(m_currentConvId, currentConversationIsGroup(),
                                       [](bool, const QJsonValue&, const QString&, int) {});
}

void ZaloClient::handleTyping(const QString& threadId, const QString& userId, bool isGroup) {
    if (userId == m_myUid || threadId.isEmpty()) return;
    m_typingUntil[threadId] = QDateTime::currentMSecsSinceEpoch() + 6000;
    if (isGroup && !userId.isEmpty()) m_typingUser[threadId] = userId;
    else m_typingUser.remove(threadId);
    m_conversationModel->setTyping(threadId, true);
    if (!m_typingTimer->isActive()) m_typingTimer->start();
    if (threadId == m_currentConvId) emit conversationStatusChanged();
}

void ZaloClient::handleDeliveredReceipt(const QString& threadId, const QString& msgId, bool isGroup) {
    Q_UNUSED(isGroup);
    if (threadId.isEmpty() || msgId.isEmpty()) return;
    m_messageModel->markDeliveredUpTo(threadId, msgId);
}

void ZaloClient::handleSeen(const QString& threadId, const QString& msgId, const QString& userId, bool isGroup) {
    if (userId == m_myUid) return;
    if (isGroup && !userId.isEmpty()) {
        m_messageModel->recordGroupSeen(threadId, msgId, userId);
        if (m_offlineStore) m_offlineStore->recordGroupSeen(msgId, userId);
    } else {
        m_messageModel->markSeenUpTo(threadId, msgId);
    }
}

void ZaloClient::sendSeenForCurrent() {
    if (m_currentConvId.isEmpty()) return;
    sendSeenFor(m_currentConvId, currentConversationIsGroup());
}

void ZaloClient::sendSeenFor(const QString& convId, bool isGroup) {
    if (!m_isLoggedIn || m_blockSeen || convId.isEmpty()) return;
    QList<QPair<qint64, QString>> candidates;
    for (auto it = m_messageThread.cbegin(); it != m_messageThread.cend(); ++it) {
        if (it.value() != convId || m_seenSent.contains(it.key())) continue;
        const QJsonObject raw = rawMessage(it.key());
        const QString from = idOf(raw.value("uidFrom"));
        if (raw.isEmpty() || from == "0" || from == m_myUid) continue;
        candidates.append({raw.value("ts").toVariant().toLongLong(), it.key()});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    QJsonArray unseen;
    for (const auto& c : candidates) {
        if (unseen.size() >= 50) break;
        unseen.append(rawMessage(c.second));
        m_seenSent.insert(c.second);
    }
    if (unseen.isEmpty()) {
        const auto& thread = m_messageModel->threadOf(convId);
        for (auto it = thread.crbegin(); it != thread.crend(); ++it) {
            if (unseen.size() >= 50) break;
            if (it->isMine || it->isRecalled || m_seenSent.contains(it->id)) continue;
            bool ok = false;
            qulonglong idNum = it->id.toULongLong(&ok);
            if (!ok || idNum == 0) continue;
            QJsonObject fallbackItem{
                {"cliMsgId", it->cliMsgId.isEmpty() ? it->id : it->cliMsgId},
                {"msgId", it->id},
                {"uidFrom", it->senderId.isEmpty() ? convId : it->senderId},
                {"idTo", isGroup ? convId : QStringLiteral("0")},
                {"msgType", it->messageType.isEmpty() ? QStringLiteral("webchat") : it->messageType},
                {"st", 0},
                {"at", 0},
                {"cmd", 510},
                {"ts", it->ts}
            };
            unseen.append(fallbackItem);
            m_seenSent.insert(it->id);
        }
    }
    if (unseen.isEmpty()) return;
    m_apiClient->session()->sendSeen(convId, isGroup, unseen, [](bool, const QJsonValue&, const QString&, int) {});
}

void ZaloClient::refreshPresence() {
    if (!m_isLoggedIn) return;
    m_apiClient->session()->fetchFriendOnlines([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        QSet<QString> online;
        for (const QJsonValue& v : data.toObject().value("onlines").toArray()) {
            online.insert(idOf(v.toObject().value("userId")));
        }
        m_onlineIds = online;
        m_conversationModel->setOnlineIds(online);
        m_contactModel->setOnlineIds(online);
        emit conversationStatusChanged();

        if (!m_currentConvId.isEmpty() && !currentConversationIsGroup() && !m_onlineIds.contains(m_currentConvId)) {
            m_apiClient->session()->fetchLastOnline(m_currentConvId, [this](bool ok2, const QJsonValue& data2, const QString&, int) {
                if (!ok2) return;
                const QJsonObject obj = data2.toObject();
                if (!obj.value("settings").toObject().value("show_online_status").toBool(true)) return;
                qint64 ts = obj.value("lastOnline").toVariant().toLongLong();
                if (ts > 0 && ts < 10000000000LL) ts *= 1000;
                m_currentLastOnline = ts > 0 && ts < kMinPlausibleEpochMs ? -1 : ts;
                emit conversationStatusChanged();
            });
        }
    });
}

void ZaloClient::loadLabels() {
    m_labelManager->reload();
}

void ZaloClient::loadHiddenConversations() {
    if (!m_isLoggedIn) return;
    m_apiClient->session()->fetchHiddenConversations([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        const QJsonObject obj = data.toObject();
        QSet<QString> ids;
        for (const QJsonValue& v : obj.value("threads").toArray()) {
            const QString id = idOf(v.toObject().value("thread_id"));
            if (!id.isEmpty()) ids.insert(id);
        }
        const QString pin = obj.value("pin").toString().trimmed().toLower();
        if (pin != m_hiddenPinHash) {
            m_hiddenPinHash = pin;
            emit hiddenPinChanged();
        }
        applyHiddenSet(ids);
        if (m_hiddenRefreshTimer) m_hiddenRefreshTimer->start();
    });
}

void ZaloClient::applyHiddenSet(const QSet<QString>& ids) {
    m_hiddenThreads = ids;
    m_conversationModel->setHiddenThreads(m_hiddenThreads);
    emit conversationFlagsChanged();
}

void ZaloClient::dropHiddenConversationsLocally(const QSet<QString>& ids) {
    for (const QString& id : ids) {
        if (id == m_currentConvId) openWelcomeScreen();
        m_conversationModel->removeConversation(id);
        if (m_offlineStore) m_offlineStore->deleteConversation(id);
    }
}

void ZaloClient::markHiddenUnread(qint64 ts) {
    if (m_hiddenUnread || (ts > 0 && ts <= m_hiddenDismissedAt)) return;
    m_hiddenUnread = true;
    emit hiddenUnreadChanged();
}

void ZaloClient::dismissHiddenUnread() {
    m_hiddenDismissedAt = QDateTime::currentMSecsSinceEpoch();
    QSettings settings("Zalos", "ZalosApp");
    settings.setValue("settings/hiddenUnreadDismissedAt", m_hiddenDismissedAt);
    if (!m_hiddenUnread) return;
    m_hiddenUnread = false;
    emit hiddenUnreadChanged();
}

QString ZaloClient::hiddenPinDigest(const QString& pin) {
    return QString::fromLatin1(QCryptographicHash::hash(pin.toUtf8(), QCryptographicHash::Md5).toHex());
}

bool ZaloClient::verifyHiddenPin(const QString& pin) const {
    static const QRegularExpression fourDigits(QStringLiteral("^\\d{4}$"));
    if (m_hiddenPinHash.isEmpty() || !fourDigits.match(pin).hasMatch()) return false;
    return hiddenPinDigest(pin) == m_hiddenPinHash;
}

void ZaloClient::setHiddenPin(const QString& pin, const QString& thenHideId) {
    static const QRegularExpression fourDigits(QStringLiteral("^\\d{4}$"));
    if (!m_isLoggedIn || !fourDigits.match(pin).hasMatch()) {
        emit hiddenOpFinished(QStringLiteral("pin"), false);
        return;
    }
    const QString digest = hiddenPinDigest(pin);
    m_apiClient->session()->updateHiddenConversPin(digest, [this, digest, thenHideId](bool ok, const QJsonValue&,
                                                                                      const QString& error, int code) {
        if (!ok) {
            emit notice(code < 0 && error != QLatin1String("decrypt_failed")
                            ? L("Không có kết nối. Vui lòng thử lại sau", "No connection. Please try again later")
                            : L("Có lỗi trong quá trình thay đổi mã PIN. Vui lòng thử lại (%1)",
                                "Couldn't change the PIN. Please try again (%1)").arg(code));
            emit hiddenOpFinished(QStringLiteral("pin"), false);
            return;
        }
        m_hiddenPinHash = digest;
        emit hiddenPinChanged();
        emit hiddenOpFinished(QStringLiteral("pin"), true);
        if (!thenHideId.isEmpty()) hideConversation(thenHideId, true);
    });
}

void ZaloClient::resetHiddenPin() {
    if (!m_isLoggedIn) {
        emit hiddenOpFinished(QStringLiteral("reset"), false);
        return;
    }
    m_apiClient->session()->resetHiddenConversPin([this](bool ok, const QJsonValue&, const QString& error, int) {
        if (!ok) {
            emit notice(L("Không thể xóa mã PIN: ", "Couldn't remove the PIN: ") + friendlyError(error));
            emit hiddenOpFinished(QStringLiteral("reset"), false);
            return;
        }
        const QSet<QString> dropped = m_hiddenThreads;
        m_hiddenPinHash.clear();
        emit hiddenPinChanged();
        dropHiddenConversationsLocally(dropped);
        applyHiddenSet({});
        emit notice(L("Đã xóa thành công mã PIN và các trò chuyện ẩn", "The PIN and the hidden conversations were removed"));
        emit hiddenOpFinished(QStringLiteral("reset"), true);
    });
}

bool ZaloClient::canHideConversation(const QString& id) const {
    return !id.isEmpty() && !m_officialIds.contains(id) && !m_hiddenThreads.contains(id);
}

bool ZaloClient::needsHiddenPin(const QString& id) const {
    return m_hiddenThreads.contains(id) && id != m_currentConvId && id != m_hiddenUnlockedId;
}

void ZaloClient::openHiddenConversation(const QString& id, const QString& msgId) {
    if (id.isEmpty()) return;
    m_hiddenUnlockedId = id;
    if (!msgId.isEmpty()) {
        openMessageFromSearch(id, msgId);
        return;
    }
    const Models::ConversationItem conv = m_conversationModel->conversation(id);
    QString name = conv.name;
    if (name.isEmpty()) name = isGroupThread(id) ? m_groups.value(id).name : displayNameOf(id);
    selectConversation(id, name, conv.avatarUrl);
}

void ZaloClient::requestHiddenFlow(const QString& flow, const QString& id, bool bypass) {
    QString name;
    if (!id.isEmpty()) {
        name = m_conversationModel->conversation(id).name;
        if (name.isEmpty()) name = isGroupThread(id) ? m_groups.value(id).name : displayNameOf(id);
    }
    emit hiddenFlowRequested(flow, id, name, bypass);
}

void ZaloClient::loadArchivedChats() {
    if (!m_isLoggedIn) return;
    m_apiClient->session()->fetchArchivedChats([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        m_archivedThreads.clear();
        for (const QJsonValue& v : data.toObject().value("items").toArray()) {
            const QJsonObject o = v.toObject();
            QString id = idOf(o.value("id"));
            if (id.isEmpty()) id = idOf(o.value("conversationId"));
            if (id.isEmpty()) id = idOf(o.value("threadId"));
            if (id.isEmpty() && v.isString()) id = v.toString();
            if (!id.isEmpty()) m_archivedThreads.insert(id);
        }
        m_conversationModel->setArchivedThreads(m_archivedThreads);
        emit conversationFlagsChanged();
    });
}

void ZaloClient::loadAutoDeleteSettings() {
    if (!m_isLoggedIn) return;
    m_apiClient->session()->fetchAutoDeleteChats([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        m_autoDeleteTtl.clear();
        for (const QJsonValue& v : data.toObject().value("convers").toArray()) {
            const QJsonObject o = v.toObject();
            const QString id = idOf(o.value("destId"));
            const qint64 ttl = o.value("ttl").toVariant().toLongLong();
            if (!id.isEmpty() && ttl > 0) m_autoDeleteTtl.insert(id, ttl);
        }
        emit conversationFlagsChanged();
    });
}

void ZaloClient::hideConversation(const QString& id, bool hide) {
    const QString op = hide ? QStringLiteral("hide") : QStringLiteral("unhide");
    if (id.isEmpty() || !m_isLoggedIn || (hide && m_officialIds.contains(id))) {
        emit hiddenOpFinished(op, false);
        return;
    }
    m_apiClient->session()->setHiddenConversation(id, isGroupThread(id), hide,
                                                  [this, id, hide, op](bool ok, const QJsonValue&, const QString&, int) {
        if (!ok) {
            emit hiddenOpFinished(op, false);
            return;
        }
        QSet<QString> next = m_hiddenThreads;
        if (hide) {
            next.insert(id);
            if (id == m_currentConvId) m_hiddenUnlockedId = id;
        } else {
            next.remove(id);
        }
        applyHiddenSet(next);
        emit hiddenOpFinished(op, true);
    });
}

void ZaloClient::archiveConversation(const QString& id, bool archive) {
    if (id.isEmpty()) return;
    m_apiClient->session()->setArchivedChat(id, isGroupThread(id), archive,
                                            [this, id, archive](bool ok, const QJsonValue&, const QString& error, int) {
        if (!ok) {
            emit notice(L("Không thể lưu trữ hội thoại: ", "Couldn't archive the conversation: ") + friendlyError(error));
            return;
        }
        if (archive) m_archivedThreads.insert(id);
        else m_archivedThreads.remove(id);
        m_conversationModel->setArchivedThreads(m_archivedThreads);
        emit conversationFlagsChanged();
    });
}

void ZaloClient::setConversationAutoDelete(const QString& id, qint64 ttl) {
    if (id.isEmpty()) return;
    m_apiClient->session()->setAutoDeleteChat(id, isGroupThread(id), ttl,
                                             [this, id, ttl](bool ok, const QJsonValue&, const QString& error, int) {
        if (!ok) {
            emit notice(L("Không thể đặt tin tự xóa: ", "Couldn't set disappearing messages: ") + friendlyError(error));
            return;
        }
        if (ttl > 0) m_autoDeleteTtl.insert(id, ttl);
        else m_autoDeleteTtl.remove(id);
        emit conversationFlagsChanged();
    });
}

void ZaloClient::refreshFriendRequests() {
    if (!m_isLoggedIn) return;
    m_apiClient->session()->fetchFriendRecommendations([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        m_friendRequests.clear();
        for (const QJsonValue& v : data.toObject().value("recommItems").toArray()) {
            const QJsonObject info = v.toObject().value("dataInfo").toObject();
            if (info.value("recommType").toInt() != 2) continue;
            Models::ContactItem c;
            c.id = idOf(info.value("userId"));
            c.name = pickName(info);
            c.avatarUrl = info.value("avatar").toString();
            c.category = "invites";
            c.subtitle = info.value("recommInfo").toObject().value("message").toString();
            m_friendRequests.append(c);
            m_users[c.id] = {c.name, c.avatarUrl, 0};
        }
        rebuildContacts();
    });
}

void ZaloClient::acceptFriendRequest(const QString& uid) {
    m_apiClient->session()->acceptFriendRequest(uid, [this, uid](bool ok, const QJsonValue&, const QString& error, int) {
        if (!ok) {
            emit notice(L("Không đồng ý kết bạn được: ", "Couldn't accept the request: ") + friendlyError(error));
            return;
        }
        emit notice(L("Đã trở thành bạn bè.", "You are now friends."));
        m_friendIds.insert(uid);
        m_friendRequests.erase(std::remove_if(m_friendRequests.begin(), m_friendRequests.end(),
                                              [&](const Models::ContactItem& c) { return c.id == uid; }),
                               m_friendRequests.end());
        rebuildContacts();
    });
}

void ZaloClient::rejectFriendRequest(const QString& uid) {
    m_apiClient->session()->rejectFriendRequest(uid, [this, uid](bool ok, const QJsonValue&, const QString& error, int) {
        if (!ok) {
            emit notice(L("Không từ chối được lời mời: ", "Couldn't decline the request: ") + friendlyError(error));
            return;
        }
        m_friendRequests.erase(std::remove_if(m_friendRequests.begin(), m_friendRequests.end(),
                                              [&](const Models::ContactItem& c) { return c.id == uid; }),
                               m_friendRequests.end());
        rebuildContacts();
    });
}

bool ZaloClient::isOfficialOrSystemAccount(const QString& id, const QString& name) const {
    if (id.isEmpty() || id.startsWith('g')) return false;
    if (m_officialIds.contains(id)) return true;
    const QString key = Models::foldForSearch(name.isEmpty() ? displayNameOf(id) : name).simplified();
    if (key.isEmpty()) return false;
    if (!m_friendIds.contains(id) && (key == QLatin1String("zalo") || key.startsWith(QLatin1String("zalo "))))
        return true;
    return key == QLatin1String("zbusiness") || key == QLatin1String("zcloud") || key == QLatin1String("zalopay")
        || key == QLatin1String("thoi tiet") || key.contains(QLatin1String("medlatec"));
}

bool ZaloClient::currentConversationIsCloud() const {
    if (m_currentConvId.isEmpty()) return false;
    if (m_currentConvId == myCloudId() || m_currentConvId == m_myUid) return true;
    const QString name = m_currentConvName.trimmed();
    if (name.compare(QLatin1String("My Documents"), Qt::CaseInsensitive) == 0 ||
        name.compare(QLatin1String("Cloud của tôi"), Qt::CaseInsensitive) == 0 ||
        name.compare(QLatin1String("Truyền File"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    return false;
}

bool ZaloClient::currentConversationIsOfficial() const {
    const QString name = m_currentConvName.isEmpty() && !m_currentConvId.isEmpty()
                             ? m_conversationModel->conversation(m_currentConvId).name
                             : m_currentConvName;
    return isOfficialOrSystemAccount(m_currentConvId, name);
}

bool ZaloClient::currentConversationCanCall() const {
    if (m_currentConvId.isEmpty()) return false;
    if (currentConversationIsCloud() || currentConversationIsOfficial()) return false;
    if (currentConversationBlocked() || m_inactiveIds.contains(m_currentConvId)) return false;
    return true;
}

bool ZaloClient::currentConversationCanCreateGroup() const {
    if (m_currentConvId.isEmpty()) return false;
    if (currentConversationIsCloud() || currentConversationIsOfficial()) return false;
    return true;
}

bool ZaloClient::currentConversationCanShareContact() const {
    if (m_currentConvId.isEmpty() || currentConversationIsGroup()) return false;
    if (currentConversationIsCloud() || currentConversationIsOfficial()) return false;
    return true;
}

bool ZaloClient::currentConversationIsOnline() const {
    if (m_currentConvId.isEmpty() || currentConversationIsGroup() || currentConversationIsCloud() || currentConversationIsOfficial()) return false;
    return m_onlineIds.contains(m_currentConvId);
}

QString ZaloClient::currentConversationStatus() const {
    if (m_currentConvId.isEmpty() || currentConversationIsCloud()) return {};
    if (m_typingUntil.contains(m_currentConvId)) {
        if (currentConversationIsGroup() && m_typingUser.contains(m_currentConvId)) {
            const QString name = displayNameOf(m_typingUser.value(m_currentConvId));
            if (!name.isEmpty()) return L("%1 đang soạn tin...", "%1 is typing...").arg(name);
        }
        return L("Đang soạn tin...", "Typing...");
    }
    if (currentConversationIsGroup()) {
        const int members = currentConversationMemberCount();
        return members > 0 ? QString::number(members) + L(" thành viên", " members") : QString();
    }
    if (currentConversationIsOfficial()) return L("Tài khoản chính thức", "Official account");
    if (m_inactiveIds.contains(m_currentConvId)) return L("Tài khoản đã ngừng hoạt động", "Account no longer active");
    if (m_onlineIds.contains(m_currentConvId)) return L("Đang hoạt động", "Active now");
    if (m_currentLastOnline < 0) return {};
    if (m_currentLastOnline > 0) {
        qint64 lastMs = m_currentLastOnline;
        if (lastMs > 0 && lastMs < 10000000000LL) lastMs *= 1000;
        if (lastMs < kMinPlausibleEpochMs) return {};
        const qint64 diffMs = QDateTime::currentMSecsSinceEpoch() - lastMs;
        const qint64 minutes = diffMs / 60000;
        if (minutes < 1) return L("Vừa mới truy cập", "Just now");
        if (minutes < 60) return L("Truy cập %1 phút trước", "Last seen %1m ago").arg(qMax<qint64>(1, minutes));
        if (minutes < 24 * 60) return L("Truy cập %1 giờ trước", "Last seen %1h ago").arg(minutes / 60);
        const qint64 days = minutes / (24 * 60);
        if (days == 1) return L("Truy cập hôm qua", "Last seen yesterday");
        if (days < 7) return L("Truy cập %1 ngày trước", "Last seen %1d ago").arg(days);
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(lastMs);
        return L("Truy cập %1", "Last seen %1").arg(dt.toString("dd/MM/yyyy"));
    }
    return L("Truy cập gần đây", "Recently active");
}

bool ZaloClient::currentConversationHasPresence() const {
    if (m_currentConvId.isEmpty() || currentConversationIsGroup() || currentConversationIsCloud()) return false;
    if (currentConversationIsOfficial() || m_inactiveIds.contains(m_currentConvId)) return false;
    return m_currentLastOnline >= 0;
}

void ZaloClient::startVoiceCall() {
    if (m_currentConvId.isEmpty() || currentConversationIsGroup() || !currentConversationCanCall()) return;
    m_calls->refreshAvailability();
    if (!m_calls->isAvailable()) {
        const QString r = m_calls->unavailableReason();
        QString why;
        if (r == "node_missing") why = L("chưa cài Node.js (hoặc Bun)", "Node.js (or Bun) is not installed");
        else if (r == "python_missing") why = L("chưa cài python3", "python3 is not installed");
        else if (r == "engine_missing") why = L("thiếu thành phần gọi đi kèm Zalos (hãy cài lại Zalos)",
                                                  "a bundled component needed for calls is missing (reinstall Zalos)");
        else why = L("không tạo được khóa truyền tin nội bộ cho cuộc gọi", "the local call transport key could not be created");
        emit notice(L("Chưa gọi được: ", "Calls unavailable: ") + why);
        return;
    }
    m_calls->startVoiceCall(m_currentConvId, m_currentConvName, m_currentConvAvatar);
}

void ZaloClient::startVideoCall() {
    if (m_currentConvId.isEmpty() || currentConversationIsGroup() || !currentConversationCanCall()) return;
    m_calls->refreshAvailability();
    if (!m_calls->isAvailable()) {
        const QString r = m_calls->unavailableReason();
        QString why;
        if (r == "node_missing") why = L("chưa cài Node.js (hoặc Bun)", "Node.js (or Bun) is not installed");
        else if (r == "python_missing") why = L("chưa cài python3", "python3 is not installed");
        else if (r == "engine_missing") why = L("thiếu thành phần gọi đi kèm Zalos (hãy cài lại Zalos)",
                                                  "a bundled component needed for calls is missing (reinstall Zalos)");
        else why = L("không tạo được khóa truyền tin nội bộ cho cuộc gọi", "the local call transport key could not be created");
        emit notice(L("Chưa gọi được: ", "Calls unavailable: ") + why);
        return;
    }
    m_calls->startVideoCall(m_currentConvId, m_currentConvName, m_currentConvAvatar);
}

void ZaloClient::startGroupCall(bool isVideo) {
    if (m_currentConvId.isEmpty() || !currentConversationIsGroup()) return;
    m_calls->refreshAvailability();
    if (!m_calls->isAvailable()) {
        const QString r = m_calls->unavailableReason();
        QString why;
        if (r == "node_missing") why = L("chưa cài Node.js (hoặc Bun)", "Node.js (or Bun) is not installed");
        else if (r == "python_missing") why = L("chưa cài python3", "python3 is not installed");
        else if (r == "engine_missing") why = L("thiếu thành phần gọi đi kèm Zalos (hãy cài lại Zalos)",
                                                  "a bundled component needed for calls is missing (reinstall Zalos)");
        else why = L("không tạo được khóa truyền tin nội bộ cho cuộc gọi", "the local call transport key could not be created");
        emit notice(L("Chưa gọi được: ", "Calls unavailable: ") + why);
        return;
    }
    const QVariantList members = groupMembers(m_currentConvId);
    m_calls->startGroupCall(m_currentConvId, m_currentConvName, m_currentConvAvatar, members, isVideo);
}

void ZaloClient::setAppLanguage(const QString& lang) {
    if (m_appLanguage != lang && (lang == "vi" || lang == "en")) {
        m_appLanguage = lang;
        m_apiClient->setLanguage(lang);
        saveSession();
        emit appLanguageChanged();
    }
}

}
