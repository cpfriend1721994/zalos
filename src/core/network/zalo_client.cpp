#include "zalo_client.h"
#include <QJSEngine>
#include "crypto/db_cross.h"
#include "sync/mobile_history_sync.h"
#include "fs/file_utils.h"
#include "media/media_utils.h"
#include "call/zcall_engine.h"
#include "offline/offline_store.h"
#include "network/zalo_api_client.h"
#include "network/zalo_session.h"
#include "network/message_scheduler.h"
#include "platform/desktop_notifications.h"
#include "media/voice_recorder.h"
#include "platform/app_lock.h"
#include "platform/shortcut_map.h"
#include "platform/storage_manager.h"
#include "qrcodegen.hpp"
#include <QClipboard>
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
#include <QStandardPaths>

#include <algorithm>
#include <memory>

namespace Zalos::Core::Network {

namespace {

constexpr int kQrLifetimeSeconds = 120;
constexpr int kGroupInfoBatch = 50;

// Zalo reaction codes (zca-js models/Reaction.ts + apis/addReaction.ts) and their display emoji.
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

// Media caption from an attachment; preview placeholders ("[Photo] ", "[Video]") are not captions.
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

} // namespace

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

    // --- QR device linking ---
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

    // --- Browser session import ---
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

    // --- Authenticated session ---
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
    connect(session, &ZaloSession::friendEventReceived, this, [this](const QString&, const QJsonValue&) {
        refreshFriendRequests();
        refreshContacts();
    });
    connect(session, &ZaloSession::mobileSyncControlReceived, this, &ZaloClient::handleMobileSyncControl);
    m_historySync = new Sync::MobileHistorySync(session, this);
    connect(m_historySync, &Sync::MobileHistorySync::stateChanged, this,
            [this](int state, const QString& status, int progress) {
                setMobileSyncState(state, status, progress);
                if (state == Sync::MobileHistorySync::Done && !m_myUid.isEmpty()) {
                    QSettings().setValue(QStringLiteral("mobileSync/done/") + m_myUid, true); // never offered again
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
    m_labelDebounce->setInterval(1000); // Zalo PC syncLabelFromServer debounce
    connect(m_labelDebounce, &QTimer::timeout, this, &ZaloClient::loadLabels);
    // Friend name/avatar changes have no push: Zalo PC notices them through versioned refetches.
    m_profileRefreshTimer = new QTimer(this);
    m_profileRefreshTimer->setInterval(15 * 60 * 1000);
    connect(m_profileRefreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_isLoggedIn) return;
        loadAccountData();
        loadAliases();
    });

    // Local scheduled send: checked every 15 s while the app runs.
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
        // A system-sound preview ends when the player stops (its length is not known in advance).
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
        hooks.rawMessage = [this](const QString& msgId) { return m_rawMessages.value(msgId); };
        hooks.myUid = [this]() { return m_myUid; };
        hooks.myName = [this]() { return m_userName; };
        hooks.displayName = [this](const QString& uid) { return displayNameOf(uid); };
        hooks.notice = [this](const QString& text) { emit notice(text); };
        hooks.friendlyError = [this](const QString& e, int code) { return friendlyError(e, code); };
        hooks.tr = [this](const QString& vi, const QString& en) { return L(vi, en); };
        m_pinBoard = new PinBoard(std::move(hooks), this);
        connect(m_pinBoard, &PinBoard::pinsChanged, this, &ZaloClient::pinnedMessageChanged);
        connect(this, &ZaloClient::currentConversationChanged, this, [this]() { m_pinBoard->setCurrent(m_currentConvId); });
        connect(this, &ZaloClient::loginStateChanged, this, [this]() {
            if (m_isLoggedIn) m_pinBoard->reload(); // the open thread's board, now that a session exists
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
        markThreadRead(convId); // answering means it was read
    });

    // Voice calls through zcall-native (zalo-for-linux), bridged to the live session.
    m_calls = new Call::CallBridge(session, this);
    m_calls->setProfileResolver([this](const QString& uid) {
        const Profile p = m_users.value(uid);
        return qMakePair(p.name, p.avatar);
    });
    connect(session, &ZaloSession::voipControlReceived, m_calls, &Call::CallBridge::handleVoipControl);
    connect(m_calls, &Call::CallBridge::stateChanged, this, [this]() {
        const QString phase = m_calls->phase();
        if (phase == "incoming") m_sound->play("ringtone", m_selectedRingtone, true);
        else if (m_sound->isPlaying() && !m_isPreviewPlaying) m_sound->stop();
        if (phase != "incoming") m_notifications->close(QStringLiteral("call")); // answered, declined or gone
    });
    connect(m_calls, &Call::CallBridge::incomingCall, this, [this](const QString& name) {
        notifyIncomingCallDbus(name);
    });
    connect(m_calls, &Call::CallBridge::callFinished, this, [this](const QString& peerId, bool outgoing, int seconds) {
        // The server delivers the call log message itself; refresh that thread if it is open.
        Q_UNUSED(outgoing);
        Q_UNUSED(seconds);
        if (peerId == m_currentConvId) loadOlderMessages();
    });

    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(45000);
    connect(m_presenceTimer, &QTimer::timeout, this, &ZaloClient::refreshPresence);

    // Expires "typing..." indicators (Zalo sends one event every few seconds while typing).
    m_typingTimer = new QTimer(this);
    m_typingTimer->setInterval(1000);
    connect(m_typingTimer, &QTimer::timeout, this, [this]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (auto it = m_typingUntil.begin(); it != m_typingUntil.end();) {
            if (it.value() < now) {
                m_conversationModel->setTyping(it.key(), false);
                const bool current = it.key() == m_currentConvId;
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
        // Back after a drop: refetch everything another device may have changed meanwhile.
        if (connected && m_realtimeWasUp) resyncAfterReconnect();
        if (connected) m_realtimeWasUp = true;
        emit connectionStateChanged(connected);
        if (connected) {
            setSyncing(true);
            session->requestOldMessages(false);
            session->requestOldMessages(true);
            session->requestOldReactions(false);
            session->requestOldReactions(true);
            // History replies may be empty; never leave the spinner on forever.
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
        // Show the last session's real data right away; the live session replaces it when it connects.
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
    // Server error_code values observed by zca-js / ctdozlite (01-business-domain, error table).
    switch (code) {
    case 600:
    case -102: return L("phiên đăng nhập đã hết hạn", "your session has expired");
    case 114: return L("tham số không hợp lệ", "invalid parameters");
    case 429: return L("thao tác quá nhanh, hãy thử lại sau ít phút", "too many requests, try again in a few minutes");
    case -69: return L("máy chủ yêu cầu ngừng thử lại (dịch vụ không dành cho phiên đăng nhập này)",
                       "the server said to stop retrying (the service is not offered to this session)");
    case 18022: return L("chữ ký yêu cầu không hợp lệ", "the request signature was rejected");
    case 18060: return L("máy chủ yêu cầu tham số được mã hoá", "the server requires encrypted parameters");
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
    return error; // server-provided messages are already human readable
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
    // Profile cache only: the authenticated state is decided by the live session handshake.
    m_userName = settings.value("auth/userName", "").toString();
    m_userPhone = settings.value("auth/userPhone", "").toString();
    m_userAvatar = settings.value("auth/userAvatar", "").toString();
    m_cachedUid = settings.value("auth/uid", "").toString();
    m_isLoggedIn = false;

    m_appLanguage = settings.value("settings/language", "vi").toString();

    m_notifyNewMessage = settings.value("settings/notifyNewMessage", true).toBool();
    m_notifySound = settings.value("settings/notifySound", false).toBool();
    m_useAvatarAsChatBackground = settings.value("settings/useAvatarAsChatBackground", false).toBool();
    m_startHiddenInTray = settings.value("settings/startHiddenInTray", false).toBool();
    m_closeToTray = settings.value("settings/closeToTray", true).toBool();
    m_antiPeeking = settings.value("settings/antiPeeking", false).toBool();

    m_blockTyping = settings.value("privacy/blockTyping", false).toBool();
    m_blockDelivered = settings.value("privacy/blockDelivered", false).toBool();
    m_blockSeen = settings.value("privacy/blockSeen", false).toBool();

    m_selectedRingtone = settings.value("sound/selectedRingtone", "classic").toString();
    m_selectedNotiSound = settings.value("sound/selectedNotiSound", "pop").toString();
    // Keys from older builds that pointed at sounds we never shipped.
    // A system sound that was removed from the desktop falls back to the built-in default.
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
    settings.setValue("settings/useAvatarAsChatBackground", m_useAvatarAsChatBackground);
    settings.setValue("settings/startHiddenInTray", m_startHiddenInTray);
    settings.setValue("settings/closeToTray", m_closeToTray);
    settings.setValue("settings/language", m_appLanguage);

    settings.setValue("privacy/blockTyping", m_blockTyping);
    settings.setValue("privacy/blockDelivered", m_blockDelivered);
    settings.setValue("privacy/blockSeen", m_blockSeen);

    settings.setValue("sound/selectedRingtone", m_selectedRingtone);
    settings.setValue("sound/selectedNotiSound", m_selectedNotiSound);
}

// ---------------------------------------------------------------------------
// Authentication
// ---------------------------------------------------------------------------

void ZaloClient::loginWithPhone(const QString& phone, const QString& password, bool remember) {
    Q_UNUSED(remember);
    if (phone.trimmed().isEmpty() || password.isEmpty()) {
        setLoginStatus(L("Vui lòng nhập số điện thoại và mật khẩu.", "Please enter your phone number and password."), true);
        return;
    }
    // Zalo Web no longer exposes a password endpoint usable outside the official page (captcha + device checks).
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
    // Fallback matrix only mirrors the real login token; nothing is drawn without one.
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
    pumpStickerQueue(); // stickers of threads opened before sign-in
    // Give the main window a moment to appear before asking.
    QTimer::singleShot(1500, this, &ZaloClient::maybeOfferMobileSync);
    if (m_offlineStore && m_offlineStore->bindOwner(m_myUid)) {
        // Another account signed in: drop what the previous account's cache put on screen.
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

    m_calls->setLocalUser(m_myUid, m_userName, m_userAvatar);
    m_scheduler->setAccount(m_myUid);
    loadAccountData();
    loadAliases();
    loadUnreadMarks();
    loadQuickMessages();
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
        // A fresh QR / browser login: nothing is saved yet, so just report it.
        m_isLoggingIn = false;
        emit loginStateChanged();
        setLoginStatus(L("Không kết nối được tới máy chủ Zalo. Kiểm tra mạng và thử lại.",
                         "Couldn't reach Zalo's servers. Check your connection and try again."), true);
        return;
    }
    // Keep the saved session and retry with backoff: 5, 10, 20, 40, then every 60 seconds.
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
        // The session is gone: hide the cached view (the cache stays for the same account's next sign-in).
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
    // Signing out removes this account's cached history from the computer.
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
    m_aliases.clear();
    m_realtimeWasUp = false;
    if (m_profileRefreshTimer) m_profileRefreshTimer->stop();
    m_historyExhausted.clear();
    m_replyTarget.clear();
    m_scheduler->clear();
    m_notifications->closeAll();
    m_conversationModel->clearDrafts(); // stay on disk for that account, never shown to the next one
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

// ---------------------------------------------------------------------------
// Account data synchronisation
// ---------------------------------------------------------------------------

void ZaloClient::loadAccountData() {
    ZaloSession* session = m_apiClient->session();
    setSyncing(true);

    // Instant start from the account's own cache when nothing is on screen yet.
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

            // Store full profile data for self
            m_myProfile.userId = m_myUid;
            m_myProfile.displayName = m_userName;
            m_myProfile.zaloName = profile.value("zaloName").toString(m_userName);
            m_myProfile.avatar = m_userAvatar;
            m_myProfile.cover = profile.value("cover").toString();
            if (m_myProfile.cover.isEmpty()) m_myProfile.cover = profile.value("bgavatar").toString();
            m_myProfile.gender = profile.value("gender").toInt(0);
            m_myProfile.dob = profile.value("dob").toVariant().toLongLong();
            m_myProfile.sdob = profile.value("sdob").toString();
            m_myProfile.status = profile.value("status").toString();
            m_myProfile.phoneNumber = m_userPhone;
            m_myProfile.isFriend = false;
            m_myProfile.isBlocked = false;

            saveSession();
            emit userProfileChanged();
            emit currentProfileChanged();
        }
        finishJob();
    });

    session->fetchFriends([this, finishJob](bool ok, const QJsonValue& data, const QString&, int) {
        if (ok) {
            for (const QJsonValue& v : data.toArray()) {
                const QJsonObject u = v.toObject();
                const QString uid = idOf(u.value("userId"));
                if (uid.isEmpty()) continue;
                m_friendIds.insert(uid);
                m_users[uid] = {pickName(u), u.value("avatar").toString(), 0};
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
        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
        const QJsonObject obj = data.toObject();
        for (const char* key : {"chatEntries", "groupChatEntries"}) {
            for (const QJsonValue& v : obj.value(key).toArray()) {
                const QJsonObject e = v.toObject();
                const qint64 duration = e.value("duration").toVariant().toLongLong();
                const qint64 start = e.value("startTime").toVariant().toLongLong();
                if (duration != -1 && start + duration < nowSec) continue;
                const QString id = idOf(e.value("id"));
                m_mutedIds.insert(id);
                m_muteUntil.insert(id, duration > 0 ? (start + duration) * 1000 : 0);
                m_conversationModel->setMuted(id, true);
            }
        }
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
        if (m_conversationModel->contains(it.key())) applyProfileToConversation(it.key());
        persistProfile(it.key(), true);

        // Roster: memVerList holds "<uid>_<profile version>"; memberIds is the older field.
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
    // One request per 100 members (/api/social/group/members), across all groups waiting for a composite.
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
                persistProfile(uid, false);
            }
            for (auto g = m_groupRosters.cbegin(); g != m_groupRosters.cend(); ++g) {
                if (m_groups.value(g.key()).avatar.isEmpty()) updateGroupComposite(g.key());
            }
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
        if (unknown.size() >= 100) break; // one request per pass; the rest resolves on the next call
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
            persistProfile(uid, false);
            if (m_conversationModel->contains(uid)) applyProfileToConversation(uid);
        }
        // Ids the server did not return stay unnamed; never ask for them again in a loop.
        for (const QString& id : unknown) {
            if (displayNameOf(id).isEmpty()) m_unresolvedMembers.insert(id);
        }
        updateGroupComposite(groupId);
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

void ZaloClient::openLink(const QString& url) {
    if (url.isEmpty()) return;
    if (GroupAdmin::isGroupLink(url)) {
        m_groupAdmin->openJoin(url);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromUserInput(url));
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

void ZaloClient::toggleVoice(const QString& msgId, const QString& url) {
    if (msgId.isEmpty() || url.isEmpty()) return;
    if (m_voicePlayer && m_playingVoiceId == msgId) {
        m_voicePlayer->stop();
        return;
    }
    if (!m_voicePlayer) {
        m_voicePlayer = new QMediaPlayer(this);
        m_voicePlayer->setAudioOutput(new QAudioOutput(m_voicePlayer));
        connect(m_voicePlayer, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
            if (state == QMediaPlayer::StoppedState && !m_playingVoiceId.isEmpty()) {
                m_playingVoiceId.clear();
                emit voicePlaybackChanged();
            }
        });
        connect(m_voicePlayer, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& err) {
            const QString url = m_voicePlayer->source().toString();
            const QString id = m_playingVoiceId;
            qWarning() << "voice playback failed:" << err;
            m_playingVoiceId.clear();
            emit voicePlaybackChanged();
            emit notice(L("Không phát được trong ứng dụng, mở bằng trình phát của hệ thống…",
                          "Can't play it in the app; opening it with the system player…"));
            openFileMessage(url, QStringLiteral("voice-%1.aac").arg(id));
        });
    }
    m_voicePlayer->stop();
    m_playingVoiceId = msgId;
    emit voicePlaybackChanged();
    m_voicePlayer->setSource(QUrl(url));
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

QHash<QString, QJsonArray> ZaloClient::deliveredCandidates(const QJsonArray& msgs, bool isGroup) const {
    // Receipts go out for fresh messages from other people only (Zalo Web: sendDeliveredEvent on push).
    QHash<QString, QJsonArray> out;
    for (const QJsonValue& v : msgs) {
        const QJsonObject m = v.toObject();
        const QString from = idOf(m.value("uidFrom"));
        if (from.isEmpty() || from == "0" || from == m_myUid) continue;
        const QString type = m.value("msgType").toString();
        if (type == QLatin1String("chat.delete") || type == QLatin1String("group.poll")) continue;
        if (m.value("content").toObject().contains("deleteMsg")) continue;
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
    // One request of at most 100 ids at a time; the rest waits for the next round.
    if (m_pendingUserLookups.isEmpty() || m_userLookupInFlight) return;
    QStringList ids;
    for (auto it = m_pendingUserLookups.begin(); it != m_pendingUserLookups.end() && ids.size() < 100;) {
        ids << *it;
        it = m_pendingUserLookups.erase(it);
    }
    m_userLookupInFlight = true;
    m_apiClient->session()->fetchUserInfo(ids, [this](bool ok, const QJsonValue& data, const QString&, int) {
        m_userLookupInFlight = false;
        if (!m_pendingUserLookups.isEmpty()) m_userLookupTimer->start();
        if (!ok) return;
        m_conversationModel->beginBatch();
        const QJsonObject changed = data.toObject().value("changed_profiles").toObject();
        for (auto it = changed.begin(); it != changed.end(); ++it) {
            QString uid = it.key();
            if (uid.endsWith("_0")) uid.chop(2);
            const QJsonObject p = it.value().toObject();
            m_users[uid] = {pickName(p), p.value("avatar").toString(), 0};
            applyProfileToConversation(uid);
            persistProfile(uid, false);
        }
        m_conversationModel->endBatch();
    });
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
        item.name = threadId == myCloudId() ? L("Cloud của tôi", "My Cloud") : displayNameOf(threadId);
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
    if (!isGroup) p.name = displayNameOf(id); // nickname from any device wins over the profile name
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

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

void ZaloClient::callLogItem(const QJsonObject& obj, bool missed, Models::MessageItem* item, QString* label) const {
    // Call history bubbles (chat.recommended, action recommened.calltime / recommened.misscall):
    // params {calltype: 0 voice | 1 video, duration: seconds, isCaller: 1 when this account called, reason}.
    // The server title is not reliable ("sendBubbleMessage"), so the text is built here.
    const QJsonObject params = QJsonDocument::fromJson(obj.value("params").toString().toUtf8()).object();
    const bool video = params.value("calltype").toInt() == 1;
    const bool outgoing = params.value("isCaller").toInt() == 1;
    const int seconds = params.value("duration").toInt();
    item->messageType = "call";
    if (missed) {
        item->text = outgoing ? (video ? L("Cuộc gọi video đi không được trả lời", "Unanswered outgoing video call")
                                       : L("Cuộc gọi thoại đi không được trả lời", "Unanswered outgoing voice call"))
                              : (video ? L("Cuộc gọi video nhỡ", "Missed video call")
                                       : L("Cuộc gọi thoại nhỡ", "Missed voice call"));
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
    // Quote reply (zca-js TQuote): {ownerId, globalMsgId, msg, attach, fromD}.
    const QJsonObject quote = msg.value("quote").toObject();
    if (!quote.isEmpty()) {
        item.quoteMsgId = idOf(quote.value("globalMsgId"));
        const QString owner = idOf(quote.value("ownerId"));
        item.quoteSender = (!m_myUid.isEmpty() && owner == m_myUid) ? L("Bạn", "You")
                           : quote.value("fromD").toString(m_users.value(owner).name);
        item.quoteText = quote.value("msg").toString();
        if (item.quoteText.isEmpty()) {
            item.quoteText = quote.value("attach").toString().isEmpty() ? L("[Tin nhắn]", "[Message]")
                                                                         : L("[Tệp đính kèm]", "[Attachment]");
        }
    }
    item.ts = msg.value("ts").toVariant().toLongLong();
    item.timeStr = item.ts > 0 ? QDateTime::fromMSecsSinceEpoch(item.ts).toString("HH:mm") : QString();
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
        // Rich text: {styles: [{start, len, st}]} in params / textProperties (Zalo PC parses message.params).
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
        // chat.gif: `thumb` is a still JPEG, `href` the animated GIF (Zalo PC _makeGifItemFromMsg). Photos whose
        // original is a GIF/WebP animate too. The still stays the placeholder and sizes the tile.
        const QString hrefPath = QUrl(item.href).path().toLower();
        if (!item.href.isEmpty() && (type == "chat.gif" || hrefPath.endsWith(".gif") || hrefPath.endsWith(".webp")))
            item.extra.insert("animatedUrl", item.href);
        label = L("[Hình ảnh]", "[Photo]");
    } else if (type == "share.file") {
        item.messageType = "file";
        item.mediaUrl = obj.value("href").toString();
        item.href = item.mediaUrl;
        item.text = obj.value("title").toString();
        // params is a JSON string: {"fileSize":"482133","fileExt":"pdf",...}
        const QJsonObject params = QJsonDocument::fromJson(obj.value("params").toString().toUtf8()).object();
        item.fileSize = params.value("fileSize").toVariant().toLongLong();
        label = L("[Tệp] ", "[File] ") + item.text;
    } else if (type == "chat.sticker") {
        item.messageType = "sticker";
        // Render info (animated WebP / sprite / still) from the sticker cache; resolved later when unknown.
        const QVariantMap info = m_stickerStore->info(idOf(obj.value("id")));
        if (!info.isEmpty()) {
            item.mediaUrl = info.value("url").toString();
            item.extra.insert("sticker", info);
        }
        label = L("[Nhãn dán]", "[Sticker]");
    } else if (type == "chat.voice") {
        // params: {duration (ms), fileSize, m4a, waveformSamples}; href is the AAC file.
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
        label = L("[Video]", "[Video]");
    } else if (type == "chat.location.new") {
        item.text = L("[Vị trí]", "[Location]");
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
        // Contact card (Zalo PC MSG_CONTACT): title = name, thumb = avatar, params = the contact's uid,
        // description = JSON {phone, qrCodeUrl, gUid}. recommened.vip = an Official Account.
        item.messageType = "card";
        item.text = obj.value("title").toString();
        item.mediaUrl = obj.value("thumb").toString();
        QJsonObject desc = QJsonDocument::fromJson(obj.value("description").toString().toUtf8()).object();
        const QJsonValue params = obj.value("params");
        QString uid = params.isString() ? params.toString() : idOf(params);
        if (uid.startsWith('{')) uid = idOf(QJsonDocument::fromJson(uid.toUtf8()).object().value("uid")); // defensive
        item.href = obj.value("href").toString();
        item.extra = {{"uid", uid}, {"phone", desc.value("phone").toVariant().toString()},
                      {"qrCodeUrl", desc.value("qrCodeUrl").toString()},
                      {"official", obj.value("action").toString() == QLatin1String("recommened.vip")}};
        if (item.text.isEmpty()) item.text = L("Danh thiếp", "Contact card");
        label = L("[Danh thiếp] ", "[Contact card] ") + item.text;
    } else if (type == "chat.recommended" || type == "chat.link") {
        // Link card: title (page title or the URL), description, thumb, href.
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
        // group.poll content: {action: "create" | vote/update actions, params: "{pollId, ...}"}. Only "create"
        // is a chat card (Zalo PC routes the others to its poll manager); the rest refresh the card.
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
        if (item.text.isEmpty()) item.text = L("[Tin nhắn chưa hỗ trợ hiển thị]", "[Unsupported message]");
        label = item.text;
    }

    // @mentions: [{uid, pos, len, type}] on the message (group text).
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
    return item;
}

void ZaloClient::handleIncomingMessages(const QJsonArray& messages, bool isGroup, bool isOld) {
    if (isOld) setSyncing(false);
    // One sidebar re-sort/reset for the whole batch instead of one per message.
    m_conversationModel->beginBatch();
    struct BatchGuard {
        Models::ConversationModel* model;
        ~BatchGuard() { model->endBatch(); }
    } batchGuard{m_conversationModel};
    QHash<QString, QVector<Models::MessageItem>> byThread;
    QHash<QString, QVector<QPair<Models::MessageItem, QJsonObject>>> coldThreads;
    QJsonArray receiptCandidates;

    for (const QJsonValue& v : messages) {
        const QJsonObject msg = v.toObject();
        const QJsonValue content = msg.value("content");

        // Deletions done on another device (delete-for-me / delete conversation): never shown as a bubble.
        if (msg.value("msgType").toString() == QLatin1String("chat.delete")) {
            handleRemoteDeletes(content.isArray() ? content.toArray() : QJsonArray{content}, isGroup);
            continue;
        }

        // Recall ("undo"): content {globalMsgId, cliMsgId, deleteMsg, srcId, destId} (zca-js models/Undo.ts).
        if (content.isObject() && content.toObject().contains("deleteMsg")) {
            const QJsonObject undo = content.toObject();
            const QString uidFrom = idOf(msg.value("uidFrom"));
            const QString thread = isGroup ? idOf(msg.value("idTo"))
                                           : ((uidFrom == "0" || uidFrom == m_myUid) ? idOf(msg.value("idTo")) : uidFrom);
            const QString recalledId = idOf(undo.value("globalMsgId"));
            m_messageModel->markRecalled(thread, recalledId, L("Tin nhắn đã được thu hồi", "Message recalled"));
            if (isThreadResident(thread)) persistMessage(thread, recalledId);
            else if (m_offlineStore) m_offlineStore->markRecalled(recalledId, L("Tin nhắn đã được thu hồi", "Message recalled"));
            continue;
        }

        QString threadId;
        QString preview;
        const Models::MessageItem item = toMessageItem(msg, isGroup, &threadId, &preview);
        if (threadId.isEmpty() || item.id.isEmpty()) continue;
        if (item.messageType == QLatin1String("poll_update")) {
            // A vote or edit on a poll: refresh the card, no bubble.
            refreshPoll(item.pollData.value("poll_id").toLongLong(), true);
            continue;
        }
        // The same message can arrive by push (501/521) and again from an offline queue or 570.
        const bool alreadySeen = m_rawMessages.contains(item.id) || m_recentIds.contains(item.id);
        rememberMessageId(item.id);
        // Threads nobody has open go straight to the offline cache; they load when opened.
        if (!isThreadResident(threadId) && cacheUsable()) {
            coldThreads[threadId].append(qMakePair(item, msg));
        } else {
            byThread[threadId].append(item);
            m_rawMessages.insert(item.id, msg);
            m_messageThread.insert(item.id, threadId);
            if (item.messageType == "sticker" && !item.extra.contains("sticker")) {
                resolveSticker(threadId, item.id, content.toObject().value("id").toVariant().toLongLong());
            }
        }
        if (!item.isMine) {
            // A new message from someone ends their "typing" indicator.
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
        const bool unread = fresh && threadId != m_currentConvId;
        m_conversationModel->recordMessage(threadId, preview, prefix, item.ts, unread);
        if (fresh && !isThreadMuted(threadId)) {
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
        // Pull the thread's cached history in first so new and cached messages form one timeline.
        mergeCachedThread(it.key());
        touchResidentThread(it.key());
        m_messageModel->mergeMessages(it.key(), it.value());
        for (const auto& item : it.value()) {
            if (hasReactions(item.id)) refreshReactionSummary(it.key(), item.id);
            if (m_offlineStore) m_offlineStore->saveMessage(it.key(), item, m_rawMessages.value(item.id), isGroup);
        }
        if (m_offlineStore) {
            m_offlineStore->saveConversation(m_conversationModel->conversation(it.key()));
        }
    }
    if (m_offlineStore) m_offlineStore->endBatch();
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
    m_currentConvName = name;
    m_currentConvAvatar = avatar;
    mergeCachedThread(id);
    touchResidentThread(id);
    evictColdThreads();
    m_messageModel->loadConversation(id);
    // Unread bar above the first unread message (placed once enough history is loaded).
    m_messageModel->setUnreadMarker(id, m_conversationModel->conversation(id).unreadCount);
    m_conversationModel->clearUnread(id);
    m_notifications->close(QStringLiteral("thread:") + id); // read now: its popup is stale
    if (m_markedUnread.contains(id)) toggleUnreadMark(id);
    m_currentLastOnline = 0;
    emit currentConversationChanged();
    emit conversationStatusChanged();
    emit pinnedMessageChanged();

    if (m_isLoggedIn && !id.isEmpty()) {
        sendSeenForCurrent();
        if (!currentConversationIsGroup() && id != myCloudId()) {
            m_apiClient->session()->fetchLastOnline(id, [this, id](bool ok, const QJsonValue& data, const QString&, int) {
                if (!ok || id != m_currentConvId) return;
                const QJsonObject obj = data.toObject();
                if (!obj.value("settings").toObject().value("show_online_status").toBool(true)) return;
                qint64 ts = obj.value("lastOnline").toVariant().toLongLong();
                if (ts > 0 && ts < 10000000000LL) ts *= 1000;
                m_currentLastOnline = ts;
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
    if (m_currentConvId.isEmpty() || !m_isLoggedIn || m_loadingHistory) return;
    const QString convId = m_currentConvId;
    const bool isGroup = currentConversationIsGroup();
    const bool isMyCloud = convId == myCloudId();
    ZaloSession* session = m_apiClient->session();
    if (!isGroup && !isMyCloud) {
        // 1-1 history only exists on the socket; its pages are global and merged per thread.
        if (session->isRealtimeConnected()) session->requestOldMessages(false, m_messageModel->oldestMessageId(convId));
        return;
    }
    if (m_historyExhausted.contains(convId)) return;
    // Groups and My Cloud page through cloud history, 50 messages before the oldest shown id.
    m_loadingHistory = true;
    emit historyLoadingChanged();
    session->fetchCloudHistory(convId, isMyCloud, m_messageModel->oldestMessageId(convId), 50,
        [this, convId, isGroup, session](bool ok, const QJsonValue& data, const QString&, int) {
            m_loadingHistory = false;
            if (!ok) {
                emit historyLoadingChanged();
                // Service unavailable for this account: fall back to the socket history queue.
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
    // Positions come from the composer's untrimmed text.
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

    // Styles shift with the trimmed lead and are clipped to the sent text.
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
                                                       : m_rawMessages.value(m_replyTarget.value("id").toString());
    sendTextTo(m_currentConvId, currentConversationIsGroup(), cleanText, localMentions, mentions, quoted, styles);
}

void ZaloClient::sendTextTo(const QString& convId, bool isGroup, const QString& cleanText,
                            const QVector<Models::MessageMention>& localMentions, const QJsonArray& mentions,
                            const QJsonObject& quoted, const QJsonArray& styles) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Models::MessageItem local;
    local.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_localSeq);
    // The server echoes clientId as cliMsgId; that is what bridges this echo to the real message.
    // Zalo expects a millisecond timestamp; bump it so two quick sends never share an id.
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
    setDraft(convId, QString()); // sent: the draft is gone
    if (convId == m_currentConvId) m_messageModel->clearUnreadMarker(); // answering ends "unread"
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
            m_messageModel->removeMessage(convId, localId);
            emit notice(L("Không gửi được tin nhắn (%1): ", "Message not sent (%1): ").arg(friendlyError(error, code))
                        + text.left(40));
        }
    };
    ZaloSession* session = m_apiClient->session();
    if (!quoted.isEmpty()) {
        session->sendQuote(convId, isGroup, text, quoted, done, cliMsgId, styles); // quote + mention not combined yet
        return;
    }
    if (!mentions.isEmpty()) {
        session->sendMention(convId, text, mentions, done, cliMsgId, styles);
        return;
    }
    // A text with a link goes out as a link card when the server can preview it (Zalo Web: parselink + link).
    static const QRegularExpression urlRe(QStringLiteral(R"(https?://[^\s<>"']+)"));
    const QRegularExpressionMatch url = urlRe.match(text);
    if (!url.hasMatch() || !styles.isEmpty()) { // a link card carries no text styles
        session->sendText(convId, isGroup, text, done, cliMsgId, styles);
        return;
    }
    QString link = url.captured(0);
    while (!link.isEmpty() && QStringLiteral(".,;:!?)]}").contains(link.back())) link.chop(1);
    QPointer<ZaloSession> guard(session);
    session->parseLink(link, [guard, convId, isGroup, text, done, cliMsgId](bool ok, const QJsonValue& data,
                                                                           const QString&, int) {
        if (!guard) return;
        const QJsonObject preview = data.toObject().value("data").toObject();
        if (!ok || preview.value("href").toString().isEmpty()) {
            guard->sendText(convId, isGroup, text, done, cliMsgId); // no preview: plain text
            return;
        }
        guard->sendLink(convId, isGroup, text, preview, done);
    });
}

void ZaloClient::setReplyTarget(const QString& msgId) {
    const QVariantMap msg = m_messageModel->getMessageById(msgId);
    // Only server messages can be quoted: the quote carries their global id.
    if (msg.isEmpty() || !m_rawMessages.contains(msgId)) {
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
        emit notice(L("Cloud của tôi chưa sẵn sàng.", "My Cloud isn't available yet."));
        return;
    }
    openConversationWithContact(id, L("Cloud của tôi", "My Cloud"), QString());
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
        // "Until 8 AM": the next 08:00 local time (zca-js MuteDuration.UNTIL_8AM).
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
        // Timed mute ran out; the server lifts it on its own, mirror that locally.
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
    if (id.isEmpty() || !m_isLoggedIn) return;
    const QJsonObject last = m_rawMessages.value(m_messageModel->latestMessageId(id));
    if (last.isEmpty()) {
        // deleteconver needs the newest server message; never pretend the delete happened.
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
            if (p.isFriend) m_friendIds.insert(p.id);
        }
    }
    const auto convs = m_offlineStore->loadConversations();
    for (const auto& c : convs) {
        Models::ConversationItem item = c;
        item.isOnline = false; // presence is never taken from the cache
        if (item.isMuted) m_mutedIds.insert(item.id);
        if (item.isGroup) m_groupIds.insert(item.id);
        m_conversationModel->upsertConversation(item);
    }
    rebuildContacts();
    return !convs.isEmpty();
}

void ZaloClient::mergeCachedThread(const QString& convId) {
    if (!m_offlineStore || convId.isEmpty() || m_cacheMerged.contains(convId)) return;
    if (m_offlineStore->ownerUid().isEmpty() || m_offlineStore->ownerUid() != (m_myUid.isEmpty() ? m_cachedUid : m_myUid)) {
        return;
    }
    m_cacheMerged.insert(convId);
    QHash<QString, QJsonObject> raws;
    const auto cached = m_offlineStore->loadMessages(convId, &raws);
    for (auto it = raws.cbegin(); it != raws.cend(); ++it) {
        if (!m_rawMessages.contains(it.key())) m_rawMessages.insert(it.key(), it.value());
        m_messageThread.insert(it.key(), convId);
    }
    if (cached.isEmpty()) return;
    // Cache columns do not hold per-type details (link card, voice, call log, poll id, mentions):
    // rebuild those rows from their raw payload.
    QVector<Models::MessageItem> rows = cached;
    const bool isGroup = isGroupThread(convId);
    for (Models::MessageItem& row : rows) {
        const QJsonObject raw = raws.value(row.id);
        if (raw.isEmpty() || row.isRecalled) continue;
        const QString type = raw.value("msgType").toString();
        if (!raw.contains("mentions") && type != QLatin1String("chat.recommended") && type != QLatin1String("chat.voice")
            && type != QLatin1String("group.poll") && type != QLatin1String("chat.link")) {
            continue;
        }
        QString thread, preview;
        Models::MessageItem rebuilt = toMessageItem(raw, isGroup, &thread, &preview);
        if (rebuilt.messageType == QLatin1String("poll_update")) continue;
        rebuilt.id = row.id;
        rebuilt.status = row.status;
        rebuilt.reaction = row.reaction;
        row = rebuilt;
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

void ZaloClient::evictColdThreads() {
    // Keep the 12 most recently used threads in RAM (messages + raw TMessage JSON); older ones are in offline.db
    // and come back on open. Was 30 (memory review 2026-11-03).
    constexpr int kMaxResidentThreads = 12;
    if (!cacheUsable() || m_residentThreads.size() <= kMaxResidentThreads) return;
    QSet<QString> evicted;
    for (int i = 0; i < m_residentThreads.size() && m_residentThreads.size() - evicted.size() > kMaxResidentThreads; ++i) {
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

void ZaloClient::rememberMessageId(const QString& msgId) {
    constexpr int kMaxRecentIds = 5000;
    if (msgId.isEmpty() || m_recentIds.contains(msgId)) return;
    m_recentIds.insert(msgId);
    m_recentIdOrder.append(msgId);
    while (m_recentIdOrder.size() > kMaxRecentIds) m_recentIds.remove(m_recentIdOrder.takeFirst());
}

void ZaloClient::releaseFreedMemory() {
    // glibc keeps freed heap pages; after a big import/eviction hand them back to the OS (debounced).
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
    m_offlineStore->saveProfile({id, p.name, p.avatar, isGroup, p.memberCount, !isGroup && m_friendIds.contains(id)});
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
    QString name = QUrl(url).fileName();
    if (name.isEmpty() || !name.contains('.')) {
        name = QStringLiteral("zalo-%1.jpg").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
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
            emit notice(L("Không lưu được ảnh", "Couldn't save the picture")
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
    fetchBytes(url, [this, path](const QByteArray& data, const QString& error) {
        QSaveFile out(path);
        if (!error.isEmpty() || !out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
            emit notice(L("Tải tệp thất bại", "Download failed") + (error.isEmpty() ? QString() : " (" + error + ")"));
            return;
        }
        emit notice(L("Đã tải về ", "Downloaded to ") + path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
}

QString ZaloClient::displayNameOf(const QString& uid) const {
    const QString alias = m_aliases.value(uid);
    return alias.isEmpty() ? m_users.value(uid).name : alias;
}

void ZaloClient::loadAliases(int page, QHash<QString, QString> collected) {
    if (!m_isLoggedIn) return;
    constexpr int kPage = 100;
    m_apiClient->session()->fetchAliasList(page, kPage, [this, page, collected](bool ok, const QJsonValue& data,
                                                                               const QString&, int) mutable {
        if (!ok) return; // keep the aliases we have; next control/reconnect retries
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
    // The complete list: renamed, new and removed nicknames (a removed one falls back to the profile name).
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
    // Versioned lists are the truth after a gap: friends/groups/pins/mutes, nicknames, labels, unread marks,
    // recent threads. Nothing local is pushed to the server here.
    loadAccountData();
    loadAliases();
    loadLabels();
    loadUnreadMarks();
    preloadRecentThreads();
}

void ZaloClient::addSystemNotice(const QString& threadId, bool isGroup, const QString& text, qint64 ts) {
    if (threadId.isEmpty() || text.isEmpty()) return;
    Models::MessageItem notice;
    notice.id = QStringLiteral("sys-%1-%2").arg(threadId).arg(ts);
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
    // Zalo PC _processChatMessages: content [{uidFrom, uidTo, globalDelMsgId, type, actionType, ...}].
    for (const QJsonValue& v : entries) {
        const QJsonObject e = v.toObject();
        const QString from = idOf(e.value("uidFrom"));
        const QString to = idOf(e.value("uidTo"));
        const QString target = idOf(e.value("globalDelMsgId"));
        const QString thread = isGroup ? to : ((to == m_myUid || to == "0") ? from : to);
        if (thread.isEmpty() || target.isEmpty()) continue;
        const int type = e.value("type").toInt();
        if (type == 2 || type == 4) {
            // Conversation cleared up to `target` on another device.
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
        if (meAffected) {
            // Left or removed on another device: the conversation is gone for this account.
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
        // Poll board updates carry the poll id in groupTopic.params (Zalo PC: updateInfo.rawData.params.pollId).
        const QJsonValue rawParams = data.value("groupTopic").toObject().value("params");
        const QJsonObject params = rawParams.isString()
                                       ? QJsonDocument::fromJson(rawParams.toString().toUtf8()).object()
                                       : rawParams.toObject();
        const qint64 pollId = params.value("pollId").toVariant().toLongLong();
        if (pollId > 0) refreshPoll(pollId, true);
    } else if (act == "update_setting" || act == "force_sync" || act == "group_type" || act == "new_link"
               || act == "invite_multi" || act == "remove_multi") {
        loadGroupInfo({gid});
    }
}

void ZaloClient::handleSyncControl(const QString& actType, const QString& act, const QJsonValue& data) {
    // Changes made on the phone or the web. They only patch local state; nothing is written back
    // (Zalo PC rule: writes happen on explicit user actions only).
    const QJsonObject obj = data.toObject();
    if (m_pinBoard->handleControl(act, data)) return; // pin_topic / unpin_topic / update / reorder (any act_type)
    if (actType == "group") {
        handleGroupControl(act, obj);
    } else if (actType == "alias") {
        loadAliases(); // nickname set/removed on another device: the list is the truth
    } else if (actType == "profile") {
        if (act == "update") loadAccountData();          // my own name/avatar changed elsewhere
        else if (act == "online_status") refreshPresence();
    } else if (actType == "mute") {
        const QString id = idOf(obj.value("toId"));
        if (id.isEmpty()) return;
        const qint64 duration = obj.value("duration").toVariant().toLongLong();
        const bool mute = act == "set";
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
    }
}

void ZaloClient::preloadRecentThreads() {
    m_apiClient->session()->fetchLastMessages({}, {}, [this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return; // the socket queues still deliver new messages
        QJsonObject obj = data.toObject();
        if (obj.value("data").isObject()) obj = obj.value("data").toObject();
        QJsonArray users = obj.value("msgs").toArray();
        for (const QJsonValue& v : obj.value("pageMsgs").toArray()) users.append(v);
        // History, not news: no unread bump, no sound. Read points below set the badges.
        handleIncomingMessages(users, false, true);
        handleIncomingMessages(obj.value("groupMsgs").toArray(), true, true);
        applyReadPoints(obj.value("clearUnreads").toArray());
    });
}

void ZaloClient::applyReadPoints(const QJsonArray& clearUnreads) {
    for (const QJsonValue& v : clearUnreads) {
        const QJsonObject e = v.toObject();
        // Sub-chat cursors (type 2 with sct) are not conversation read points.
        if (e.value("type").toInt() == 2 && e.contains("sct")) continue;
        const QString id = idOf(e.value("idTo"));
        const QString point = idOf(e.value("lastMsgId"));
        if (id.isEmpty() || id == "0" || point.isEmpty() || point == "0") continue;
        if (!m_conversationModel->contains(id)) {
            // Only 1-1 threads are created from read points; stale groups may linger there.
            const QJsonValue g = e.value("isGroup");
            if (g.toBool() || g.toInt() == 1) continue;
            ensureConversation(id, false);
        }
        if (id == m_currentConvId || m_markedUnread.contains(id)) continue;
        m_conversationModel->setUnreadCount(id, m_messageModel->countIncomingAfter(id, point));
    }
}

void ZaloClient::showMessageNotification(const QString& threadId, const QString& title, const QString& body) {
    Platform::DesktopNotifications::Request request;
    // Anti-peeking also keeps sender and content off the notification popup.
    // A locked app (passcode) hides it the same way and offers no reply from the popup.
    const bool hide = m_antiPeeking || m_appLock->locked();
    request.title = hide || title.isEmpty() ? QStringLiteral("Zalos") : title;
    request.body = hide ? L("Bạn có tin nhắn mới", "You have a new message") : body.left(200);
    request.category = QStringLiteral("im.received");
    request.actions = {{QStringLiteral("default"), L("Mở", "Open")},
                       {QStringLiteral("mark-read"), L("Đánh dấu đã đọc", "Mark as read")}};
    request.inlineReply = !m_appLock->locked(); // only where the server supports it (KDE Plasma, …)
    request.replyPlaceholder = L("Trả lời…", "Reply…");
    // Same thread replaces its previous popup instead of stacking.
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
    // "default" (click on the popup) or "Mở": open that conversation and bring the window up.
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
    local.href = QUrl::fromLocalFile(filePath).toString(); // playable right away (toggleVoice)
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
    m_apiClient->session()->sendVoice(convId, isGroup, filePath, local.cliMsgId,
        [this, convId, localId](bool ok, const QJsonValue&, const QString& error, int code) {
            m_messageModel->setStatus(convId, localId, ok ? QStringLiteral("sent") : QStringLiteral("failed"));
            if (!ok) emit notice(L("Chưa gửi được tin nhắn thoại: ", "Voice message not sent: ") + friendlyError(error, code));
        });
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
    const auto conv = m_conversationModel->conversation(id);
    const Profile p = m_users.value(id);
    const QString name = !conv.name.isEmpty() ? conv.name : p.name;
    const QString avatar = !conv.avatarUrl.isEmpty() ? conv.avatarUrl : p.avatar;
    selectConversation(id, name, avatar);
}

void ZaloClient::markThreadRead(const QString& convId) {
    if (convId.isEmpty()) return;
    m_conversationModel->clearUnread(convId);
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
bool ZaloClient::profileIsBlocked() const { return m_activeProfile.isBlocked; }
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
    m_activeProfile.isBlocked = false; // filled from the profile reply below; mute is unrelated to blocking
    m_showUserProfileModal = true;
    emit currentProfileChanged();
    emit userProfileModalChanged();

    if (m_isLoggedIn && m_apiClient->session()) {
        m_apiClient->session()->fetchUserInfo({userId}, [this, userId](bool ok, const QJsonValue& data, const QString&, int) {
            m_profileLoading = false;
            if (ok && m_showUserProfileModal && m_activeProfile.userId == userId) {
                const QJsonObject changed = data.toObject().value("changed_profiles").toObject();
                QJsonObject p;
                if (changed.contains(userId)) {
                    p = changed.value(userId).toObject();
                } else if (changed.contains(userId + "_0")) {
                    p = changed.value(userId + "_0").toObject();
                } else if (!changed.isEmpty()) {
                    p = changed.begin().value().toObject();
                }

                if (!p.isEmpty()) {
                    QString name = pickName(p);
                    if (!name.isEmpty()) m_activeProfile.displayName = name;
                    m_activeProfile.zaloName = p.value("zaloName").toString(m_activeProfile.displayName);
                    m_activeProfile.avatar = p.value("avatar").toString(m_activeProfile.avatar);
                    m_activeProfile.cover = p.value("cover").toString();
                    if (m_activeProfile.cover.isEmpty()) m_activeProfile.cover = p.value("bgavatar").toString();
                    m_activeProfile.gender = p.value("gender").toInt(0);
                    m_activeProfile.dob = p.value("dob").toVariant().toLongLong();
                    m_activeProfile.sdob = p.value("sdob").toString();
                    m_activeProfile.status = p.value("status").toString();
                    m_activeProfile.phoneNumber = p.value("phoneNumber").toString();
                    m_activeProfile.isFriend = p.value("isFr").toInt() == 1 || m_friendIds.contains(userId);
                    m_activeProfile.isBlocked = p.value("isBlocked").toInt() == 1;
                }
            }
            emit currentProfileChanged();
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
        if (!cleanBio.isEmpty()) {
            m_apiClient->session()->updateProfileBio(cleanBio, [](bool, const QJsonValue&, const QString&, int) {});
        }
    }
}

void ZaloClient::blockUser(const QString& userId) {
    if (userId.isEmpty() || userId == m_myUid) return;
    m_activeProfile.isBlocked = true;
    emit currentProfileChanged();
    emit notice(L("Đã chặn người dùng này", "User blocked"));

    if (m_isLoggedIn && m_apiClient->session()) {
        m_apiClient->session()->blockUser(userId, [this](bool ok, const QJsonValue&, const QString& err, int) {
            if (!ok) emit notice(L("Không thể chặn: ", "Failed to block: ") + friendlyError(err));
        });
    }
}

void ZaloClient::unblockUser(const QString& userId) {
    if (userId.isEmpty() || userId == m_myUid) return;
    m_activeProfile.isBlocked = false;
    emit currentProfileChanged();
    emit notice(L("Đã bỏ chặn người dùng này", "User unblocked"));

    if (m_isLoggedIn && m_apiClient->session()) {
        m_apiClient->session()->unblockUser(userId, [this](bool ok, const QJsonValue&, const QString& err, int) {
            if (!ok) emit notice(L("Không thể bỏ chặn: ", "Failed to unblock: ") + friendlyError(err));
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
    // The card changes only after the server accepted the vote (fresh /api/poll/detail).
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

bool ZaloClient::tryPasteClipboardImage() {
    if (m_currentConvId.isEmpty()) return false;
    QClipboard* clip = QGuiApplication::clipboard();
    if (!clip) return false;
    const QMimeData* mime = clip->mimeData();
    if (!mime) return false;

    if (mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            const QString tempPath = QString("%1/zalos_paste_%2.png")
                                         .arg(tempDir)
                                         .arg(QDateTime::currentMSecsSinceEpoch());
            if (img.save(tempPath, "PNG")) {
                sendFile(QUrl::fromLocalFile(tempPath).toString(), "");
                return true;
            }
        }
    }

    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            const QString path = url.toLocalFile();
            if (path.isEmpty()) continue;
            const QFileInfo fi(path);
            const QString ext = fi.suffix().toLower();
            if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "webp" || ext == "bmp") {
                sendFile(url.toString(), "");
                return true;
            }
        }
    }
    return false;
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
        // Clearing the nickname: /api/alias/remove, then the profile name shows again.
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
    // Explicit user edit: the only path that writes an alias to the server. The profile name stays intact.
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

    // Local echo, bridged to the server copy by cliMsgId like a text (status follows the reply).
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
        m_messageModel->removeMessage(targetThreadId, localId);
        emit notice(L("Không gửi được danh thiếp: ", "Contact card not sent: ") + why);
    };
    // zca-js sendCard: the card carries the contact's QR link from /api/friend/mget-qr.
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
    local.senderId = m_myUid;
    local.senderName = m_userName;
    local.senderAvatar = m_userAvatar;
    local.status = QStringLiteral("sending");

    m_messageModel->mergeMessages(targetThreadId, {local});
    m_conversationModel->recordMessage(targetThreadId, text.isEmpty() ? L("[Tệp đính kèm]", "[Attachment]") : text, L("Bạn: ", "You: "), now, false);

    if (m_isLoggedIn && m_apiClient->session()) {
        const QString localId = local.id;
        if (msgType == "image" || msgType == "file") {
            if (!mediaUrl.isEmpty() && (QFile::exists(mediaUrl) || mediaUrl.startsWith("file://"))) {
                m_apiClient->session()->sendAttachment(targetThreadId, isGroup, mediaUrl, text,
                    [this, targetThreadId, localId](bool ok, const QJsonValue&, const QString&, int) {
                        if (ok) m_messageModel->setStatus(targetThreadId, localId, QStringLiteral("sent"));
                    });
            } else {
                const QString forwardContent = text.isEmpty() ? mediaUrl : (text + "\n" + mediaUrl);
                m_apiClient->session()->sendText(targetThreadId, isGroup, forwardContent,
                    [this, targetThreadId, localId](bool ok, const QJsonValue& data, const QString&, int) {
                        if (ok) {
                            m_messageModel->setStatus(targetThreadId, localId, QStringLiteral("sent"));
                            const QString serverId = idOf(data.toObject().value("msgId"));
                            if (!serverId.isEmpty()) m_messageModel->confirmMessage(targetThreadId, localId, serverId);
                        }
                    });
            }
        } else {
            m_apiClient->session()->sendText(targetThreadId, isGroup, text,
                [this, targetThreadId, localId](bool ok, const QJsonValue& data, const QString&, int) {
                    if (ok) {
                        m_messageModel->setStatus(targetThreadId, localId, QStringLiteral("sent"));
                        const QString serverId = idOf(data.toObject().value("msgId"));
                        if (!serverId.isEmpty()) m_messageModel->confirmMessage(targetThreadId, localId, serverId);
                    }
                });
        }
    }

    emit notice(L("Đã chuyển tiếp tin nhắn thành công", "Message forwarded successfully"));
    closeForwardModal();
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
    if (m_mobileSyncState != 0 && m_mobileSyncState != 5) return; // already running
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
    // "transfer_msg" (automatic transfer right after login, temp_key) is not implemented; only the
    // user-started flow ("syncmsgmb") is handled.
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

void ZaloClient::setUseAvatarAsChatBackground(bool val) {
    if (m_useAvatarAsChatBackground != val) {
        m_useAvatarAsChatBackground = val;
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
        // The AppImage path (APPIMAGE) survives updates in place; otherwise the binary itself.
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

    // The preview state follows the sound's length, independent of the audio device.
    if (!m_previewTimer) {
        m_previewTimer = new QTimer(this);
        m_previewTimer->setSingleShot(true);
        connect(m_previewTimer, &QTimer::timeout, this, [this]() {
            m_isPreviewPlaying = false;
            m_currentPreviewSound.clear();
            emit previewPlayingChanged();
        });
    }
    // System sounds have no known length: the preview ends when the player stops (SoundEngine::playingChanged),
    // with this timer as an upper bound.
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

void ZaloClient::answerCall() {
    m_incomingCallActive = false;
    m_callEngine->answerCall(false);
    emit incomingCallStateChanged(false);
}

void ZaloClient::declineCall() {
    m_incomingCallActive = false;
    m_callEngine->declineCall();
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
    // A real reconnect attempt; the store reports CONNECTED only when the socket is back.
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
        men["uid"] = m.uid;
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
    request.timeoutMs = 0; // until answered, declined or the call is gone (closed from CallBridge::stateChanged)
    m_notifications->show(QStringLiteral("call"), request);
}

void ZaloClient::requestScreenshot() {
    // xdg-desktop-portal Screenshot (Wayland and X11). The result arrives as the Response signal of a Request
    // object whose path is derived from our bus name and handle_token; subscribe before calling (no race).
    if (m_currentConvId.isEmpty()) return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!m_screenshotRequest.isEmpty())
        bus.disconnect("org.freedesktop.portal.Desktop", m_screenshotRequest, "org.freedesktop.portal.Request",
                       "Response", this, SLOT(onScreenshotResponse(uint,QVariantMap)));
    const QString token = QStringLiteral("zalos_shot_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    m_screenshotRequest = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    m_screenshotTarget = m_currentConvId;
    bus.connect("org.freedesktop.portal.Desktop", m_screenshotRequest, "org.freedesktop.portal.Request", "Response",
                this, SLOT(onScreenshotResponse(uint,QVariantMap)));

    QDBusMessage msg = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                                      "org.freedesktop.portal.Screenshot", "Screenshot");
    QVariantMap options{{"interactive", true}, {"modal", true}, {"handle_token", token}};
    msg << QString() << options;
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (!w->isError()) return;
        m_screenshotRequest.clear();
        emit notice(L("Không mở được công cụ chụp màn hình (cần xdg-desktop-portal): ",
                      "Couldn't open the screenshot tool (needs xdg-desktop-portal): ") + w->error().message());
    });
}

void ZaloClient::onScreenshotResponse(uint response, const QVariantMap& results) {
    QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_screenshotRequest,
                                             "org.freedesktop.portal.Request", "Response", this,
                                             SLOT(onScreenshotResponse(uint,QVariantMap)));
    m_screenshotRequest.clear();
    if (response != 0) return; // 1 = cancelled by the user, 2 = failed/denied
    const QString uri = results.value("uri").toString();
    if (uri.isEmpty() || m_screenshotTarget.isEmpty()) return;
    if (m_screenshotTarget != m_currentConvId) {
        emit notice(L("Đã chuyển hội thoại nên ảnh chụp chưa được gửi", "The conversation changed; the screenshot was not sent"));
        return;
    }
    sendFile(uri);
}

// ---------------------------------------------------------------------------
// Phase-1 features: attachments, stickers, reactions, recall, seen/typing,
// presence, labels and friend requests (zca-js apis/*).
// ---------------------------------------------------------------------------

QString ZaloClient::threadOfMessage(const QString& msgId) const {
    return m_messageThread.value(msgId, m_currentConvId);
}

bool ZaloClient::isGroupThread(const QString& threadId) const {
    // Group ids from getlg may not be loaded yet; the conversation item knows from the socket command.
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

    // Captions travel with images; for other files Zalo sends the text as its own message.
    if (!caption.trimmed().isEmpty() && !isImage) sendMessage(caption);

    // Local echo while uploading; the server copy replaces it through the realtime socket.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Models::MessageItem local;
    local.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_localSeq);
    local.ts = now;
    local.timeStr = QDateTime::fromMSecsSinceEpoch(now).toString("HH:mm");
    local.isMine = true;
    local.senderId = m_myUid;
    local.senderName = m_userName;
    local.status = QStringLiteral("sending");
    local.messageType = isImage ? QStringLiteral("image") : QStringLiteral("file");
    local.mediaUrl = isImage ? QUrl::fromLocalFile(path).toString() : QString();
    local.text = isImage ? caption.trimmed() : info.fileName();
    m_messageModel->mergeMessages(convId, {local});
    m_conversationModel->recordMessage(convId, isImage ? L("[Hình ảnh]", "[Photo]") : L("[Tệp] ", "[File] ") + info.fileName(),
                                       L("Bạn: ", "You: "), now, false);

    m_pendingAttachments++;
    emit attachmentStateChanged();
    const QString localId = local.id;
    m_apiClient->session()->sendAttachment(convId, isGroup, path, isImage ? caption.trimmed() : QString(),
        [this, convId, localId](bool ok, const QJsonValue& data, const QString& error, int code) {
            m_pendingAttachments = qMax(0, m_pendingAttachments - 1);
            emit attachmentStateChanged();
            if (!ok) {
                m_messageModel->removeMessage(convId, localId);
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
        });
}

void ZaloClient::searchStickers(const QString& keyword) {
    // Kept for older callers: the picker uses ZaloClient.stickers directly.
    m_stickerStore->search(keyword);
}

void ZaloClient::sendStickerItem(qint64 stickerId, qint64 cateId, int type) {
    if (m_currentConvId.isEmpty() || !m_isLoggedIn) return;
    const QString convId = m_currentConvId;
    m_apiClient->session()->sendSticker(convId, currentConversationIsGroup(), stickerId, cateId, type,
        [this](bool ok, const QJsonValue&, const QString& error, int) {
            if (!ok) emit notice(L("Không gửi được nhãn dán: ", "Couldn't send the sticker: ") + friendlyError(error));
            // On success the realtime socket delivers the sticker message.
        });
}

void ZaloClient::resolveSticker(const QString& convId, const QString& msgId, qint64 stickerId) {
    // Queued even while offline / before sign-in (cached and phone-imported threads): pumped after login.
    if (stickerId <= 0) return;
    const QString key = QString::number(stickerId);
    const bool alreadyRequested = m_stickerWaiters.contains(key);
    m_stickerWaiters[key].append({convId, msgId});
    if (alreadyRequested) return;
    m_stickerQueue.append(key);
    pumpStickerQueue();
}

void ZaloClient::pumpStickerQueue() {
    if (!m_isLoggedIn) return; // onSessionLoggedIn pumps again
    // At most 4 sticker detail requests in flight (a history import can reference thousands).
    while (m_stickerInFlight < 4 && !m_stickerQueue.isEmpty()) {
        const QString key = m_stickerQueue.takeFirst();
        ++m_stickerInFlight;
        m_apiClient->session()->fetchStickerDetail(key.toLongLong(), [this, key](bool ok, const QJsonValue& d, const QString&, int) {
        --m_stickerInFlight;
        QTimer::singleShot(0, this, &ZaloClient::pumpStickerQueue);
        const QVariantMap info = ok ? StickerStore::describe(d.toObject()) : QVariantMap();
        const QString url = info.value("url").toString();
        if (url.isEmpty()) {
            // Failed or no URL: keep the waiters and retry later (the sticker shows a placeholder meanwhile).
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
} // namespace

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
        // The reply carries no URL: read the new avatar back from the account info.
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
        loadGroupInfo({groupId}); // new avatar URL from getmg-v2
        emit notice(L("Đã đổi ảnh nhóm.", "Group photo changed."));
    });
}

QVariantList ZaloClient::systemSounds() const {
    return Platform::SoundEngine::systemSounds();
}

QVariantList ZaloClient::reactionList() const {
    QVariantList out;
    for (const auto& r : kReactions) {
        // "thanks" shares 🙏 with "pray": shown when received, not offered twice in the picker.
        if (QLatin1String(r.key) == QLatin1String("thanks")) continue;
        out.append(QVariantMap{{"key", QString::fromLatin1(r.key)}, {"emoji", QString::fromUtf8(r.emoji)}});
    }
    return out;
}

void ZaloClient::reactToMessage(const QString& msgId, const QString& reactionKey) {
    const QJsonObject raw = m_rawMessages.value(msgId);
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
            // Shown at once; the socket echo of this tap replaces the pending event (no double count).
            addReactionEvent(msgId, m_myUid, icon, QStringLiteral("pending:%1").arg(QDateTime::currentMSecsSinceEpoch()),
                             QDateTime::currentMSecsSinceEpoch());
            refreshReactionSummary(convId, msgId);
        });
}

QVariantMap ZaloClient::reactionDetails(const QString& msgId) const {
    // Every tap counts (a person can react many times and with several icons, like the Zalo apps).
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
        QStringList emojis = p.emojiOrder; // most used first
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
        if (x.value("isMe").toBool() != y.value("isMe").toBool()) return x.value("isMe").toBool(); // me first
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
    const QJsonObject raw = m_rawMessages.value(msgId);
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
        if (icon.isEmpty() && rType >= 0) { // some payloads carry only the type
            for (const auto& def : kReactions) {
                if (def.rType == rType) { icon = QString::fromLatin1(def.icon); break; }
            }
        }
        // The reaction event's own id dedupes replays (live 612 and the old 610/611 queues overlap).
        QString eventId = idOf(r.value("msgId"));
        if (eventId.isEmpty()) eventId = idOf(r.value("actionId"));
        if (eventId.isEmpty()) eventId = idOf(r.value("cliMsgId"));
        qint64 ts = r.value("ts").toVariant().toLongLong();
        if (ts > 0 && ts < 10000000000LL) ts *= 1000;
        const bool removal = icon.isEmpty() || rType < 0;
        for (const QJsonValue& target : content.value("rMsg").toArray()) {
            const QString msgId = idOf(target.toObject().value("gMsgID"));
            if (msgId.isEmpty()) continue;
            if (removal) {
                clearReactionsOf(msgId, uidFrom); // a removal takes back all of that person's reactions
            } else {
                const QString id = eventId.isEmpty()
                    ? QStringLiteral("anon:%1:%2:%3").arg(uidFrom, icon).arg(ts) : eventId;
                if (self) dropPendingReaction(msgId, icon); // our own tap came back from the server
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
    if (!m_reactedIdsLoaded && m_offlineStore && !m_offlineStore->ownerUid().isEmpty()) { // DB bound to the account
        m_reactedIds = m_offlineStore->reactedMessageIds();
        m_reactedIdsLoaded = true;
    }
    return m_reactedIds.contains(msgId) || !m_reactions.value(msgId).isEmpty();
}

void ZaloClient::addReactionEvent(const QString& msgId, const QString& uid, const QString& icon, const QString& eventId,
                                  qint64 ts) {
    auto& events = eventsOf(msgId);
    for (const auto& e : std::as_const(events)) {
        if (e.eventId == eventId) return; // replay
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

void ZaloClient::clearReactionsOf(const QString& msgId, const QString& uid) {
    auto& events = eventsOf(msgId);
    events.erase(std::remove_if(events.begin(), events.end(), [&uid](const auto& e) { return e.uid == uid; }), events.end());
    if (m_offlineStore) m_offlineStore->clearReactions(msgId, uid);
}

void ZaloClient::refreshReactionSummary(const QString& convId, const QString& msgId) {
    // Pill: up to 3 emojis, most used first, and the number of taps when more than one.
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
    // Direct column update: the message may live only on disk (history is not kept in RAM).
    if (m_offlineStore) m_offlineStore->setMessageReaction(msgId, summary);
}

bool ZaloClient::canRecall(const QString& msgId) const {
    const QJsonObject raw = m_rawMessages.value(msgId);
    if (raw.isEmpty()) return false;
    const QString from = idOf(raw.value("uidFrom"));
    // Zalo allows recalling your own messages; the server enforces its own time window.
    return from == "0" || from == m_myUid;
}

void ZaloClient::recallMessage(const QString& msgId) {
    const QJsonObject raw = m_rawMessages.value(msgId);
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
    const QJsonObject raw = m_rawMessages.value(msgId);
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

void ZaloClient::copyText(const QString& text) {
    if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(text);
}

void ZaloClient::notifyTyping() {
    if (!m_isLoggedIn || m_blockTyping || m_currentConvId.isEmpty() || m_currentConvId == myCloudId()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastTypingSent < 4000) return; // the web client throttles the same way
    m_lastTypingSent = now;
    m_apiClient->session()->sendTyping(m_currentConvId, currentConversationIsGroup(),
                                       [](bool, const QJsonValue&, const QString&, int) {});
}

void ZaloClient::handleTyping(const QString& threadId, const QString& userId, bool isGroup) {
    Q_UNUSED(isGroup);
    if (userId == m_myUid || threadId.isEmpty()) return;
    m_typingUntil[threadId] = QDateTime::currentMSecsSinceEpoch() + 6000;
    m_conversationModel->setTyping(threadId, true);
    if (!m_typingTimer->isActive()) m_typingTimer->start();
    if (threadId == m_currentConvId) emit conversationStatusChanged();
}

void ZaloClient::handleSeen(const QString& threadId, const QString& msgId, const QString& userId, bool isGroup) {
    if (userId == m_myUid) return;
    // 1-1: the peer saw our messages. Groups: show "seen" once any member saw them.
    Q_UNUSED(isGroup);
    m_messageModel->markSeenUpTo(threadId, msgId);
}

void ZaloClient::sendSeenForCurrent() {
    if (m_currentConvId.isEmpty()) return;
    sendSeenFor(m_currentConvId, currentConversationIsGroup());
}

void ZaloClient::sendSeenFor(const QString& convId, bool isGroup) {
    if (!m_isLoggedIn || m_blockSeen || convId.isEmpty()) return;
    // Newest unseen incoming messages first; the API accepts at most 50 per request.
    QList<QPair<qint64, QString>> candidates;
    for (auto it = m_messageThread.cbegin(); it != m_messageThread.cend(); ++it) {
        if (it.value() != convId || m_seenSent.contains(it.key())) continue;
        const QJsonObject raw = m_rawMessages.value(it.key());
        const QString from = idOf(raw.value("uidFrom"));
        if (raw.isEmpty() || from == "0" || from == m_myUid) continue;
        candidates.append({raw.value("ts").toVariant().toLongLong(), it.key()});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    QJsonArray unseen;
    for (const auto& c : candidates) {
        if (unseen.size() >= 50) break;
        unseen.append(m_rawMessages.value(c.second));
        m_seenSent.insert(c.second);
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
                m_currentLastOnline = ts;
                emit conversationStatusChanged();
            });
        }
    });
}

void ZaloClient::loadLabels() {
    m_labelManager->reload();
}

void ZaloClient::refreshFriendRequests() {
    if (!m_isLoggedIn) return;
    m_apiClient->session()->fetchFriendRecommendations([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        m_friendRequests.clear();
        for (const QJsonValue& v : data.toObject().value("recommItems").toArray()) {
            const QJsonObject info = v.toObject().value("dataInfo").toObject();
            if (info.value("recommType").toInt() != 2) continue; // 2 = received friend request
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

bool ZaloClient::currentConversationIsOnline() const {
    if (m_currentConvId.isEmpty() || currentConversationIsGroup()) return false;
    return m_onlineIds.contains(m_currentConvId);
}

QString ZaloClient::currentConversationStatus() const {
    if (m_currentConvId.isEmpty()) return {};
    if (m_typingUntil.contains(m_currentConvId)) return L("Đang soạn tin...", "Typing...");
    if (currentConversationIsGroup()) {
        const int members = currentConversationMemberCount();
        return members > 0 ? QString::number(members) + L(" thành viên", " members") : QString();
    }
    if (m_onlineIds.contains(m_currentConvId)) return L("Đang hoạt động", "Active now");
    if (m_currentLastOnline > 0) {
        qint64 lastMs = m_currentLastOnline;
        if (lastMs > 0 && lastMs < 10000000000LL) lastMs *= 1000;
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


void ZaloClient::startVoiceCall() {
    if (m_currentConvId.isEmpty() || currentConversationIsGroup()) return;
    m_calls->refreshAvailability();
    if (!m_calls->isAvailable()) {
        const QString r = m_calls->unavailableReason();
        QString why;
        if (r == "node_missing") why = L("chưa cài Node.js (hoặc Bun)", "Node.js (or Bun) is not installed");
        else if (r == "python_missing") why = L("chưa cài python3", "python3 is not installed");
        else if (r == "engine_missing") why = L("thiếu bộ gọi zcall-native đi kèm Zalos (hãy cài lại Zalos)",
                                                  "the bundled zcall-native engine is missing (reinstall Zalos)");
        else why = L("không tạo được khóa truyền tin nội bộ cho cuộc gọi", "the local call transport key could not be created");
        emit notice(L("Chưa gọi được: ", "Calls unavailable: ") + why);
        return;
    }
    m_calls->startVoiceCall(m_currentConvId, m_currentConvName, m_currentConvAvatar);
}

void ZaloClient::startVideoCall() {
    if (m_currentConvId.isEmpty() || currentConversationIsGroup()) return;
    m_calls->refreshAvailability();
    if (!m_calls->isAvailable()) {
        const QString r = m_calls->unavailableReason();
        QString why;
        if (r == "node_missing") why = L("chưa cài Node.js (hoặc Bun)", "Node.js (or Bun) is not installed");
        else if (r == "python_missing") why = L("chưa cài python3", "python3 is not installed");
        else if (r == "engine_missing") why = L("thiếu bộ gọi zcall-native đi kèm Zalos (hãy cài lại Zalos)",
                                                  "the bundled zcall-native engine is missing (reinstall Zalos)");
        else why = L("không tạo được khóa truyền tin nội bộ cho cuộc gọi", "the local call transport key could not be created");
        emit notice(L("Chưa gọi được: ", "Calls unavailable: ") + why);
        return;
    }
    m_calls->startVideoCall(m_currentConvId, m_currentConvName, m_currentConvAvatar);
}

// ---------------------------------------------------------------------------
// To-Do
// ---------------------------------------------------------------------------

namespace {

// The list response shape is not documented; accept the containers Zalo uses elsewhere.
QJsonArray extractTodoArray(const QJsonValue& data) {
    if (data.isArray()) return data.toArray();
    const QJsonObject obj = data.toObject();
    for (const char* key : {"data", "todos", "items", "list", "result", "tasks"}) {
        const QJsonValue v = obj.value(key);
        if (v.isArray()) return v.toArray();
        if (v.isObject()) {
            const QJsonArray nested = extractTodoArray(v);
            if (!nested.isEmpty()) return nested;
        }
    }
    return {};
}

QStringList parseIdList(const QJsonValue& v) {
    QJsonArray arr = v.toArray();
    if (v.isString()) arr = QJsonDocument::fromJson(v.toString().toUtf8()).array();
    QStringList out;
    for (const QJsonValue& x : arr) {
        out << (x.isObject() ? x.toObject().value("id").toVariant().toString() : x.toVariant().toString());
    }
    return out;
}

} // namespace

void ZaloClient::loadTodos(const QString& tab) {
    if (!m_isLoggedIn) return;
    m_todoTab = tab.isEmpty() ? QStringLiteral("assigned") : tab;
    QJsonObject query;
    if (m_todoTab == "done") query["status"] = QJsonObject{{"eq", 1}};
    else if (m_todoTab == "created") query["role"] = "creator";
    else query["role"] = "assignees";
    if (m_todoTab != "done") query["status"] = QJsonObject{{"lt", 1}};

    m_loadingTodos = true;
    emit todosChanged();
    m_apiClient->session()->todoList(query, 0, 50, [this](bool ok, const QJsonValue& data, const QString& error, int code) {
        m_loadingTodos = false;
        m_todoItems.clear();
        if (!ok) {
            m_apiClient->session()->logDiagnostic(QStringLiteral("todo list failed: %1 (code %2) via %3")
                                                      .arg(error).arg(code)
                                                      .arg(m_apiClient->session()->serviceUrls("boards").join(',')));
            // -69 "Stop retry": the board service refuses this session for good (seen with Zalo Web-type sign-ins,
            // where only group_board is offered); retrying or toasting on every visit helps nobody.
            if (code == -69 || error == QLatin1String("service_unavailable")) {
                m_todoUnavailable = L("Máy chủ Zalo chưa cho phiên đăng nhập này dùng To-Do cá nhân (mã -69, \u201cngừng thử lại\u201d). "
                                      "Danh sách công việc vẫn xem được trong ứng dụng Zalo trên điện thoại hoặc Zalo PC.",
                                      "Zalo's servers don't offer personal To-Do to this session (code -69, \u201cstop retry\u201d). "
                                      "Your tasks are still in the Zalo phone app or Zalo PC.");
                emit todosChanged();
                return;
            }
            emit todosChanged();
            if (error == QLatin1String("not_logged_in")) return; // the To-Do view already says to connect
            emit notice(L("Không tải được danh sách công việc: ", "Couldn't load tasks: ") + friendlyError(error, code));
            return;
        }
        m_todoUnavailable.clear();
        for (const QJsonValue& v : extractTodoArray(data)) {
            const QJsonObject t = v.toObject();
            QVariantMap item;
            item["id"] = t.value("id").toVariant().toString();
            item["content"] = t.value("content").toString();
            item["description"] = t.value("description").toString();
            item["status"] = t.value("status").toInt();
            qint64 due = t.value("dueDate").toVariant().toLongLong();
            if (due <= 0) due = t.value("due_date").toVariant().toLongLong();
            item["dueDate"] = due;
            const QString creator = t.value("creator").toVariant().toString();
            item["creatorName"] = creator == m_myUid ? L("Bạn", "You") : m_users.value(creator).name;
            QStringList names;
            for (const QString& id : parseIdList(t.value("assignees"))) {
                names << (id == m_myUid ? L("Bạn", "You") : m_users.value(id).name);
            }
            item["assigneeNames"] = names.join(", ");
            if (!item["id"].toString().isEmpty()) m_todoItems.append(item);
        }
        emit todosChanged();
    });
}

void ZaloClient::createTodo(const QString& content, qint64 dueDateMs) {
    if (!m_isLoggedIn || content.trimmed().isEmpty()) return;
    m_apiClient->session()->todoCreate(content.trimmed(), dueDateMs > 0 ? dueDateMs : -1, {m_myUid},
        [this](bool ok, const QJsonValue&, const QString& error, int) {
            if (!ok) {
                emit notice(L("Không tạo được công việc: ", "Couldn't create the task: ") + friendlyError(error));
                return;
            }
            loadTodos(m_todoTab);
        });
}

void ZaloClient::setTodoDone(const QString& id, bool done) {
    m_apiClient->session()->todoUpdateStatus(id, done ? 1 : 0, [this](bool ok, const QJsonValue&, const QString& error, int) {
        if (!ok) emit notice(L("Không cập nhật được công việc: ", "Couldn't update the task: ") + friendlyError(error));
        loadTodos(m_todoTab);
    });
}

void ZaloClient::deleteTodo(const QString& id) {
    m_apiClient->session()->todoDelete(id, [this](bool ok, const QJsonValue&, const QString& error, int) {
        if (!ok) emit notice(L("Không xóa được công việc: ", "Couldn't delete the task: ") + friendlyError(error));
        loadTodos(m_todoTab);
    });
}

void ZaloClient::setAppLanguage(const QString& lang) {
    if (m_appLanguage != lang && (lang == "vi" || lang == "en")) {
        m_appLanguage = lang;
        m_apiClient->setLanguage(lang);
        saveSession();
        emit appLanguageChanged();
    }
}

} // namespace Zalos::Core::Network
