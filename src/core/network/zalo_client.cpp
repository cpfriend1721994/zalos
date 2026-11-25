#include "zalo_client_internal.h"

namespace Zalos::Core::Network {

using namespace Detail;

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
    QPointer<ZaloClient> pickerOwner(this);
    Platform::FilePicker::setUnavailableHandler([pickerOwner]() {
        if (!pickerOwner) return;
        emit pickerOwner->notice(pickerOwner->L(
            "Không mở được hộp thoại chọn tệp. Hãy cài xdg-desktop-portal (kèm giao diện GNOME/KDE/GTK) hoặc zenity/kdialog.",
            "Couldn't open the file chooser. Install xdg-desktop-portal (with its GNOME/KDE/GTK backend) or zenity/kdialog."));
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
        if (m_qrTimer && !m_qrTimer->isActive()) m_qrTimer->start(1000);
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
    connect(m_apiClient, &ZaloApiClient::qrFailed, this, [this](const QString& reason) {
        ErrorCatalog::trace("qr", reason);
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
    watchConnectivity();
    connect(session, &ZaloSession::messagesReceived, this, &ZaloClient::handleIncomingMessages);
    connect(session, &ZaloSession::reactionsReceived, this, &ZaloClient::handleReactions);
    connect(this, &ZaloClient::currentConversationChanged, this, &ZaloClient::refreshUnreadReactions);
    connect(session, &ZaloSession::typingReceived, this, &ZaloClient::handleTyping);
    connect(session, &ZaloSession::seenReceived, this, &ZaloClient::handleSeen);
    connect(session, &ZaloSession::deliveredReceived, this, &ZaloClient::handleDeliveredReceipt);
    connect(session, &ZaloSession::friendEventReceived, this, [this](const QString& act, const QJsonValue&) {
        if (act == "req_v2" || act == "undo_req" || act == "reject") {
            refreshSentFriendRequests();
        }
        refreshFriendRequests();
        m_friendEventDebounce->start();
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
            [this](const QJsonArray& messages, bool isGroup) {
                const auto before = m_offlineStore ? m_offlineStore->saveStats() : Offline::ZaloOfflineStore::SaveStats{};
                const QHash<QString, int> dropsBefore = m_ingestDrops;
                handleIncomingMessages(messages, isGroup, true);
                QHash<QString, int> dropsDelta;
                for (auto it = m_ingestDrops.cbegin(); it != m_ingestDrops.cend(); ++it) {
                    const int d = it.value() - dropsBefore.value(it.key());
                    if (d > 0) dropsDelta.insert(it.key(), d);
                }
                m_historySync->recordIngestDelta(dropsDelta);
                if (!m_offlineStore) return;
                const auto after = m_offlineStore->saveStats();
                m_historySync->recordStoreDelta(after.inserted - before.inserted, after.updated - before.updated,
                                                after.deduped - before.deduped, after.failed - before.failed);
            });
    connect(session, &ZaloSession::readPointsReceived, this, &ZaloClient::applyReadPoints);
    connect(session, &ZaloSession::controlReceived, this, &ZaloClient::handleSyncControl);
    m_labelDebounce = new QTimer(this);
    m_labelDebounce->setSingleShot(true);
    m_labelDebounce->setInterval(1000);
    connect(m_labelDebounce, &QTimer::timeout, this, &ZaloClient::loadLabels);
    m_memoryTrimTimer = new QTimer(this);
    m_memoryTrimTimer->setSingleShot(true);
    m_memoryTrimTimer->setInterval(1500);
    connect(m_memoryTrimTimer, &QTimer::timeout, this, &ZaloClient::memoryTrimRequested);
    m_readPointSyncTimer = new QTimer(this);
    m_readPointSyncTimer->setSingleShot(true);
    connect(m_readPointSyncTimer, &QTimer::timeout, this, &ZaloClient::syncReadPoints);
    m_profileRefreshTimer = new QTimer(this);
    m_profileRefreshTimer->setInterval(15 * 60 * 1000);
    connect(m_profileRefreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_isLoggedIn) return;
        loadAccountData();
        loadAliases();
        scheduleReadPointSync();
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
        emit notice(L("Tin nhắn hẹn giờ chưa gửi được: ", "A scheduled message failed: ") + friendlyError(error));
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
        hooks.persist = [this](const QJsonObject& snapshot) {
            if (m_offlineStore) {
                m_offlineStore->setMetaValue(
                    QStringLiteral("labels_cache"),
                    QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Compact)));
            }
        };
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
    m_emojiCatalog = new Models::EmojiCatalog(this);
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
        connect(this, &ZaloClient::currentConversationChanged, this, &ZaloClient::conversationStatusChanged);
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
        else if (reason == QLatin1String("no_multimedia"))
            emit notice(L("Máy thiếu thành phần ghi âm của Qt Multimedia (GStreamer/FFmpeg) nên chưa ghi âm được.",
                          "This system lacks the Qt Multimedia recording components (GStreamer/FFmpeg), so recording is unavailable."));
        else if (reason == QLatin1String("no_aac_encoder"))
            emit notice(L("Thiếu bộ mã hóa AAC trong Qt Multimedia nên chưa ghi âm được.",
                          "Qt Multimedia has no AAC encoder, so recording is unavailable."));
        else {
            ErrorCatalog::trace("media", QStringLiteral("recording failed reason=%1").arg(reason));
            emit notice(L("Ghi âm thất bại. Vui lòng thử lại.", "Recording failed. Please try again."));
        }
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
        const QString prevPhase = m_lastCallPhase;
        m_lastCallPhase = phase;
        if (phase == "incoming") {
            m_sound->play("ringtone", m_selectedRingtone, true);
            if (prevPhase != "incoming" && !m_windowVisible) emit windowActivationRequested(QString());
        } else if (phase == "outgoing") {
            m_sound->play("ringtone", "classic", true);
        } else {
            if (m_sound->isPlaying() && !m_isPreviewPlaying) m_sound->stop();
            if (phase == "ended" && !prevPhase.isEmpty() && prevPhase != "ended" && prevPhase != "incoming"
                && m_notifySound) {
                m_sound->play("notification", "call_end", false);
            }
        }
        if (phase != "incoming") m_notifications->close(QStringLiteral("call"));
        syncCallTrayState();
    });
    connect(m_calls, &Call::CallBridge::cameraStateChanged, this, &ZaloClient::syncCallTrayState);
    connect(m_calls, &Call::CallBridge::incomingCall, this, [this](const QString& name) {
        notifyIncomingCallDbus(name);
    });
    connect(m_calls, &Call::CallBridge::callFinished, this,
            [this](const QString& peerId, bool outgoing, int seconds, bool isVideo) {
        const QString targetId = peerId.isEmpty() ? m_currentConvId : peerId;
        const bool isGroup = !targetId.isEmpty() && isGroupThread(targetId);
        if (!targetId.isEmpty()) {
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
                const bool video = isVideo;
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
                mergeCachedThread(targetId);
                touchResidentThread(targetId);
                m_messageModel->mergeMessages(targetId, {item});
                m_conversationModel->recordMessage(targetId, "[" + item.text + "]",
                                                  outgoing ? L("Bạn: ", "You: ") : (item.senderName + ": "), now, false);
            }
        }
        if (m_isLoggedIn && m_apiClient && m_apiClient->session()) {
            const QPointer<ZaloClient> self(this);
            QTimer::singleShot(800, this, [this, self, isGroup]() {
                if (!self || !m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
                if (m_apiClient->session()->isRealtimeConnected()) {
                    m_apiClient->session()->requestOldMessages(isGroup, QString());
                }
            });
        }
    });

    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(45000);
    connect(m_presenceTimer, &QTimer::timeout, this, &ZaloClient::refreshPresence);

    m_friendEventDebounce = new QTimer(this);
    m_friendEventDebounce->setSingleShot(true);
    m_friendEventDebounce->setInterval(1000);
    connect(m_friendEventDebounce, &QTimer::timeout, this, &ZaloClient::refreshContacts);

    m_rebuildContactsTimer = new QTimer(this);
    m_rebuildContactsTimer->setSingleShot(true);
    m_rebuildContactsTimer->setInterval(40);
    connect(m_rebuildContactsTimer, &QTimer::timeout, this, &ZaloClient::rebuildContactsNow);

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
        if (connected) scheduleReadPointSync();
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
        if (m_isLoggedIn) {
            m_qrTimer->stop();
            return;
        }
        if (m_isLoggingIn || m_qrLoginStep >= 1 || m_qrImageUrl.isEmpty()) return;
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
    const bool en = m_appLanguage == QLatin1String("en");
    const ErrorCatalog::Message m = ErrorCatalog::describe(code, error);
    if (m.traceRaw) ErrorCatalog::trace("api", QStringLiteral("code=%1 raw=%2").arg(code).arg(error));
    return ErrorCatalog::render(code, error, en);
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

    m_appLanguage = settings.value("settings/language", "en").toString();

    m_notifyNewMessage = settings.value("settings/notifyNewMessage", true).toBool();
    m_notifyReactions = settings.value("settings/notifyReactions", true).toBool();
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
    m_showBusinessButton = settings.value("settings/showBusinessButton", true).toBool();
    m_showKikiButton = settings.value("settings/showKikiButton", true).toBool();
    m_hardwareAcceleration = Platform::RenderGuard::hardwareAccelerationEnabled();
    m_startupHardwareAcceleration = m_hardwareAcceleration;
    m_hardwareAccelerationFailed = !m_hardwareAcceleration && !Platform::RenderGuard::lastFailure().isEmpty();
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
    settings.setValue("settings/notifyReactions", m_notifyReactions);
    settings.setValue("settings/notifySound", m_notifySound);
    settings.setValue("settings/antiPeeking", m_antiPeeking);
    settings.setValue("settings/chatBackgroundMode", m_chatBackgroundMode);
    settings.setValue("settings/chatBackgroundOpacity", m_chatBackgroundOpacity);
    settings.remove(QLatin1String("settings/useAvatarAsChatBackground"));
    settings.setValue("settings/chatBackgroundImage", m_chatBackgroundImage);
    settings.setValue("settings/showLabelFolders", m_showLabelFolders);
    settings.setValue("settings/showBusinessButton", m_showBusinessButton);
    settings.setValue("settings/showKikiButton", m_showKikiButton);
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
    if (m_offlineStore && m_offlineStore->bindOwner(m_myUid)) {
        m_conversationModel->clearAll();
        m_messageModel->clearAll();
        m_rawMessages.clear();
        m_messageThread.clear();
        m_cacheMerged.clear();
        m_labelManager->reset();
        resetReadPoints();
    }
    m_cachedUid = m_myUid;
    m_isLoggedIn = true;
    m_isLoggingIn = false;
    QTimer::singleShot(15000, this, [this]() {
        releaseFreedMemory();
        startIdleMemoryTrim();
    });
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
    loadLabels();
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
    refreshPresence();
    refreshFriendRequests();
    if (m_windowVisible) m_presenceTimer->start();
    session->connectRealtime();
    if (!m_currentConvId.isEmpty() && !m_historyLoaded.contains(m_currentConvId)) {
        fetchInitialConversationHistory(m_currentConvId);
    }
}

void ZaloClient::onSessionLoginUnavailable(const QString& reason) {
    ErrorCatalog::trace("login", QStringLiteral("unavailable reason=%1").arg(reason));
    if (!m_restoringSession) {
        m_isLoggingIn = false;
        emit loginStateChanged();
        setLoginStatus(L("Không kết nối được tới máy chủ Zalo. Kiểm tra mạng và thử lại.",
                         "Couldn't reach Zalo's servers. Check your connection and try again."), true);
        return;
    }
    const int delay = qMin(30, 5 << qMin(m_restoreAttempts, 3));
    ++m_restoreAttempts;
    setLoginStatus(L("Chưa kết nối được máy chủ. Thử lại sau %1 giây...", "Can't reach the server. Retrying in %1 s...")
                       .arg(delay), !m_isOfflineMode);
    m_restoreRetryTimer->start(delay * 1000);
}

void ZaloClient::watchConnectivity() {
    m_reconnectProbeTimer = new QTimer(this);
    m_reconnectProbeTimer->setSingleShot(true);
    m_reconnectProbeTimer->setInterval(2000);
    connect(m_reconnectProbeTimer, &QTimer::timeout, this, [this]() {
        if (!m_isLoggedIn && !m_restoringSession) return;
        ErrorCatalog::trace("network", QStringLiteral("reconnect probe reason=%1").arg(m_reconnectProbeReason));
        syncOfflineData();
    });
    if (QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
        if (QNetworkInformation* info = QNetworkInformation::instance()) {
            connect(info, &QNetworkInformation::reachabilityChanged, this, [this](QNetworkInformation::Reachability r) {
                if (r == QNetworkInformation::Reachability::Online) scheduleReconnectProbe(QStringLiteral("network-online"));
            });
        }
    }
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
                                         QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"),
                                         this, SLOT(onPrepareForSleep(bool)));
}

void ZaloClient::onPrepareForSleep(bool sleeping) {
    if (!sleeping) scheduleReconnectProbe(QStringLiteral("resume"));
}

void ZaloClient::scheduleReconnectProbe(const QString& reason) {
    if (!m_reconnectProbeTimer || (!m_isLoggedIn && !m_restoringSession)) return;
    m_reconnectProbeReason = reason;
    m_reconnectProbeTimer->start();
}

void ZaloClient::onSessionLoginFailed(const QString& reason) {
    ErrorCatalog::trace("login", QStringLiteral("failed reason=%1").arg(reason));
    m_restoreRetryTimer->stop();
    m_restoreAttempts = 0;
    if (m_isOfflineMode) {
        m_isOfflineMode = false;
        m_conversationModel->clearAll();
        m_messageModel->clearAll();
        m_rawMessages.clear();
        m_messageThread.clear();
        m_cacheMerged.clear();
        resetReadPoints();
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
    ++m_accountLoadGen;
    if (m_historySync) m_historySync->cancel();
    if (m_stickerStore) m_stickerStore->reset();
    if (m_voicePlayer) m_voicePlayer->stop();
    m_playingVoiceId.clear();
    m_voiceErrors.clear();
    emit voicePlaybackChanged();
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
    resetReadPoints();
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
    m_loadingHistory = false;
    setSyncing(false);
    m_friendsKnown = false;
    m_userLookupInFlight = false;
    m_qrLoginStep = 0;
    m_userName.clear();
    m_userPhone.clear();
    m_userAvatar.clear();
    m_myUid.clear();
    m_myProfile = {};
    m_activeProfile = {};
    m_profileLoading = false;
    m_hasBusinessPackage = false;
    m_businessPackageName.clear();
    m_businessLoading = false;
    m_businessProfile.clear();
    m_businessCatalogs.clear();
    m_selectedCatalogId.clear();
    m_businessProducts.clear();
    m_businessAutoReplies.clear();
    m_loginStatusMessage.clear();
    m_showSettingsModal = false;
    m_showUserProfileModal = false;
    m_currentConvId.clear();
    m_lastChatId.clear();
    m_currentConvName.clear();
    m_currentConvAvatar.clear();
    m_users.clear();
    m_groups.clear();
    m_groupVersions.clear();
    m_groupRosters.clear();
    m_groupMembersCache.clear();
    m_friendIds.clear();
    m_groupIds.clear();
    m_pendingUserLookups.clear();
    m_memberLookupInFlight.clear();
    m_unresolvedMembers.clear();
    m_stickerWaiters.clear();
    m_stickerQueue.clear();
    m_stickerAttempts.clear();
    m_stickerInFlight = 0;
    m_stickerPrefetching.clear();
    m_downloadsInFlight.clear();
    m_officialIds.clear();
    m_inactiveIds.clear();
    m_readOnlyThreads.clear();
    m_strangerRanks.clear();
    m_strangerLocks.clear();
    if (m_strangerUnlockTimer) m_strangerUnlockTimer->stop();
    m_archivedThreads.clear();
    m_autoDeleteTtl.clear();
    m_pollFetchedAt.clear();
    m_pollDetails.clear();
    m_pollGroups.clear();
    m_quickMessages.clear();
    m_serverSettings.clear();
    m_reminders.clear();
    m_loadingReminders = false;
    m_closeFriendIds.clear();
    m_groupInvites.clear();
    m_friendRecommendations.clear();
    m_profileRelatedGroups.clear();
    m_linkedDevices.clear();
    m_sentFriendRequests.clear();
    m_myAvatarHistory.clear();
    m_mutedIds.clear();
    m_muteUntil.clear();
    m_markedUnread.clear();
    m_hiddenThreads.clear();
    m_hiddenPinHash.clear();
    m_hiddenUnlockedId.clear();
    m_hiddenUnread = false;
    if (m_hiddenRefreshTimer) m_hiddenRefreshTimer->stop();
    m_aliases.clear();
    m_lastGroupSender.clear();
    m_realtimeWasUp = false;
    if (m_profileRefreshTimer) m_profileRefreshTimer->stop();
    if (m_userLookupTimer) m_userLookupTimer->stop();
    if (m_typingTimer) m_typingTimer->stop();
    if (m_labelDebounce) m_labelDebounce->stop();
    if (m_expiryTimer) m_expiryTimer->stop();
    if (m_reconnectProbeTimer) m_reconnectProbeTimer->stop();
    m_historyExhausted.clear();
    m_cloudPageState.clear();
    m_replyTarget.clear();
    m_scheduler->clear();
    m_notifications->closeAll();
    m_conversationModel->clearDrafts();
    m_historyLoaded.clear();
    m_rawMessages.clear();
    m_messageThread.clear();
    m_reactions.clear();
    m_reactedIds.clear();
    m_reactionClearedTs.clear();
    m_reactedIdsLoaded = false;
    m_onlineIds.clear();
    m_blockedIds.clear();
    m_blockedUsers.clear();
    m_serverSharedMedia.clear();
    m_bankCardFetching.clear();
    m_seenSent.clear();
    m_typingUser.clear();
    m_typingUntil.clear();
    emit blockedUsersChanged();
    m_friendRequests.clear();
    m_currentLastOnline = 0;
    m_presenceTimer->stop();
    if (m_friendEventDebounce) m_friendEventDebounce->stop();
    if (m_rebuildContactsTimer) m_rebuildContactsTimer->stop();
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
    const quint64 gen = ++m_accountLoadGen;
    setSyncing(true);

    if (m_conversationModel->rowCount() == 0) loadOfflineCache();

    auto pendingJobs = std::make_shared<int>(4);
    auto finishJob = [this, pendingJobs, gen]() {
        if (gen != m_accountLoadGen) return;
        if (--(*pendingJobs) <= 0) {
            setSyncing(false);
            maybeOfferMobileSync();
        }
    };

    session->fetchAccountInfo([this, session, finishJob, gen](bool ok, const QJsonValue& data, const QString&, int) {
        if (gen != m_accountLoadGen) return;
        if (ok) {
            const QJsonObject profile = pickOwnProfileObject(data);
            const QString name = pickName(profile);
            if (!name.isEmpty()) m_userName = name;
            const QString avatar = pickAvatar(profile);
            if (!avatar.isEmpty()) m_userAvatar = avatar;
            m_calls->setLocalUser(m_myUid, m_userName, m_userAvatar);
            QString rawPhone;
            for (const char* key : {"phoneNumber", "phone_number", "phone", "phoneNum"}) {
                const QString v = profile.value(key).toVariant().toString().trimmed();
                if (!v.isEmpty()) {
                    rawPhone = v;
                    break;
                }
            }
            if (!rawPhone.isEmpty()) m_userPhone = normalizeDisplayPhone(rawPhone);

            m_myProfile.userId = m_myUid;
            m_myProfile.displayName = m_userName;
            m_myProfile.zaloName = profile.value("zaloName").toString(profile.value("zalo_name").toString(m_userName));
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

            if ((m_userName.isEmpty() || m_userAvatar.isEmpty()) && !m_myUid.isEmpty()) {
                QPointer<ZaloClient> guard(this);
                session->fetchUserInfo({m_myUid}, [guard, gen](bool uOk, const QJsonValue& uData, const QString&, int) {
                    if (!guard || gen != guard->m_accountLoadGen || !uOk) return;
                    const QJsonObject changed = uData.toObject().value("changed_profiles").toObject();
                    QJsonObject p = changed.value(guard->m_myUid).toObject();
                    if (p.isEmpty()) p = changed.value(guard->m_myUid + QLatin1String("_0")).toObject();
                    if (p.isEmpty() && !changed.isEmpty()) p = changed.begin().value().toObject();
                    if (p.isEmpty()) return;
                    const QString fallbackName = pickName(p);
                    const QString fallbackAvatar = pickAvatar(p);
                    if (guard->m_userName.isEmpty() && !fallbackName.isEmpty()) {
                        guard->m_userName = fallbackName;
                        guard->m_myProfile.displayName = fallbackName;
                    }
                    if (guard->m_userAvatar.isEmpty() && !fallbackAvatar.isEmpty()) {
                        guard->m_userAvatar = fallbackAvatar;
                        guard->m_myProfile.avatar = fallbackAvatar;
                    }
                    guard->m_calls->setLocalUser(guard->m_myUid, guard->m_userName, guard->m_userAvatar);
                    guard->saveSession();
                    emit guard->userProfileChanged();
                    emit guard->currentProfileChanged();
                });
            }

            saveSession();
            emit userProfileChanged();
            emit currentProfileChanged();
        }
        finishJob();
    });

    session->fetchFriends([this, finishJob, gen](bool ok, const QJsonValue& data, const QString&, int) {
        if (gen != m_accountLoadGen) return;
        if (ok) {
            m_friendsKnown = true;
            if (m_offlineStore) m_offlineStore->beginBatch();
            m_conversationModel->beginBatch();
            QSet<QString> resolved;
            for (const QJsonValue& v : data.toArray()) {
                const QJsonObject u = v.toObject();
                const QString uid = idOf(u.value("userId"));
                if (uid.isEmpty()) continue;
                m_friendIds.insert(uid);
                m_users[uid] = {pickName(u), u.value("avatar").toString(), 0};
                if (!m_lastGroupSender.isEmpty()) resolved.insert(uid);
                noteAccountFlags(uid, u);
                applyProfileToConversation(uid);
                persistProfile(uid, false);
            }
            if (!resolved.isEmpty()) refreshGroupSenderPrefixes(resolved);
            m_conversationModel->endBatch();
            if (m_offlineStore) m_offlineStore->endBatch();
            m_groupMembersCache.clear();
            rebuildContacts();
            const QString cloudId = myCloudId();
            if (!cloudId.isEmpty() && m_users.value(cloudId).avatar.isEmpty() && !m_unresolvedMembers.contains(cloudId)) {
                m_pendingUserLookups.insert(cloudId);
                m_userLookupTimer->start();
            }
        }
        finishJob();
    });

    session->fetchGroupIds([this, finishJob, gen](bool ok, const QJsonValue& data, const QString&, int) {
        if (gen != m_accountLoadGen) return;
        if (ok) {
            const QJsonObject gridMap = data.toObject().value("gridVerMap").toObject();
            QStringList changedIds;
            for (auto it = gridMap.constBegin(); it != gridMap.constEnd(); ++it) {
                const QString id = it.key();
                m_groupIds.insert(id);
                const QString ver = it.value().toVariant().toString();
                if (ver.isEmpty() || !m_groups.contains(id) || !m_groupRosters.contains(id)
                    || m_groupVersions.value(id) != ver) {
                    changedIds << id;
                    if (!ver.isEmpty()) m_groupVersions.insert(id, ver);
                }
            }
            if (!changedIds.isEmpty()) loadGroupInfo(changedIds);
        }
        finishJob();
    });

    session->fetchPinnedConversations([this, finishJob, gen](bool ok, const QJsonValue& data, const QString&, int) {
        if (gen != m_accountLoadGen) return;
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
                    [this, gen](bool ok, const QJsonValue& data, const QString&, int) {
        if (gen != m_accountLoadGen || !ok) return;
        const MuteSnapshot snap = parseMuteSnapshot(data.toObject(), QDateTime::currentSecsSinceEpoch());
        for (const QString& id : snap.muted) m_conversationModel->setMuted(id, true);
        for (const QString& oldId : std::as_const(m_mutedIds)) {
            if (!snap.muted.contains(oldId)) m_conversationModel->setMuted(oldId, false);
        }
        m_mutedIds = snap.muted;
        m_muteUntil = snap.until;
        emit currentConversationChanged();
    });
    refreshBlockedUsers();
}

void ZaloClient::loadGroupInfo(const QStringList& groupIds) {
    for (int i = 0; i < groupIds.size(); i += kGroupInfoBatch) {
        const QStringList batch = groupIds.mid(i, kGroupInfoBatch);
        m_apiClient->session()->fetchGroupInfo(batch, [this, batch](bool ok, const QJsonValue& data, const QString&, int) {
            if (ok) {
                applyGroupInfo(data.toObject().value("gridInfoMap").toObject());
            } else {
                for (const QString& id : batch) m_groupVersions.remove(id);
            }
        });
    }
}

void ZaloClient::applyGroupInfo(const QJsonObject& map) {
    if (m_offlineStore) m_offlineStore->beginBatch();
    m_conversationModel->beginBatch();
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
            m_groupMembersCache.remove(it.key());
            emit groupMembersChanged(it.key());
        }
        updateGroupComposite(it.key());
    }
    m_conversationModel->endBatch();
    if (m_offlineStore) m_offlineStore->endBatch();
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
            QSet<QString> resolved;
            const QJsonObject profiles = data.toObject().value("profiles").toObject();
            for (auto it = profiles.begin(); it != profiles.end(); ++it) {
                QString uid = it.key();
                if (uid.endsWith("_0")) uid.chop(2);
                const QJsonObject p = it.value().toObject();
                const QString name = pickName(p);
                if (name.isEmpty() || m_users.contains(uid)) continue;
                m_users[uid] = {name, p.value("avatar").toString(), 0};
                if (!m_lastGroupSender.isEmpty()) resolved.insert(uid);
                noteAccountFlags(uid, p);
                persistProfile(uid, false);
            }
            if (!resolved.isEmpty()) refreshGroupSenderPrefixes(resolved);
            for (auto g = m_groupRosters.cbegin(); g != m_groupRosters.cend(); ++g) {
                if (m_groups.value(g.key()).avatar.isEmpty()) updateGroupComposite(g.key());
            }
            m_groupMembersCache.clear();
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
    const auto cached = m_groupMembersCache.constFind(groupId);
    if (cached != m_groupMembersCache.cend()) return *cached;
    const GroupRoster& roster = m_groupRosters[groupId];
    QVariantList owner, admins, members;
    bool missing = false;
    for (const QString& id : roster.members) {
        const bool isMe = !m_myUid.isEmpty() && id == m_myUid;
        const QString name = isMe ? m_userName : displayNameOf(id);
        if (name.isEmpty() && !isMe) missing = true;
        const QString role = id == roster.creator ? QStringLiteral("owner")
                             : roster.admins.contains(id) ? QStringLiteral("admin") : QStringLiteral("member");
        const QVariantMap entry{{"id", id}, {"name", name},
                                {"foldedName", Models::foldForSearch(name)},
                                {"avatar", isMe ? m_userAvatar : m_users.value(id).avatar},
                                {"role", role}, {"isMe", isMe}};
        (role == "owner" ? owner : role == "admin" ? admins : members).append(entry);
    }
    if (missing) resolveGroupMembers(groupId);
    const QVariantList result = owner + admins + members;
    m_groupMembersCache.insert(groupId, result);
    return result;
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
        QSet<QString> resolved;
        const QJsonObject profiles = data.toObject().value("profiles").toObject();
        for (auto it = profiles.begin(); it != profiles.end(); ++it) {
            QString uid = it.key();
            if (uid.endsWith("_0")) uid.chop(2);
            const QJsonObject p = it.value().toObject();
            const QString name = pickName(p);
            if (name.isEmpty()) continue;
            m_users[uid] = {name, p.value("avatar").toString(), 0};
            if (!m_lastGroupSender.isEmpty()) resolved.insert(uid);
            noteAccountFlags(uid, p);
            persistProfile(uid, false);
            if (m_conversationModel->contains(uid)) applyProfileToConversation(uid);
        }
        if (!resolved.isEmpty()) refreshGroupSenderPrefixes(resolved);
        for (const QString& id : unknown) {
            if (displayNameOf(id).isEmpty()) {
                if (!m_unresolvedMembers.contains(id)) dropOldestIfFull(m_unresolvedMembers);
                m_unresolvedMembers.insert(id);
            }
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
            m_groupMembersCache.remove(groupId);
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
            m_groupMembersCache.remove(groupId);
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
        m_groupVersions.remove(groupId);
        m_groupRosters.remove(groupId);
        m_groupMembersCache.remove(groupId);
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
    openExternalUrl(url);
}

QString ZaloClient::linkifyText(const QString& text) const {
    return Models::MessageModel::linkifyPlain(text);
}

QUrl ZaloClient::externalUrl(const QString& url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) return QUrl();
    const QUrl u = QUrl::fromUserInput(trimmed);
    if (!u.isValid() || u.scheme().isEmpty()) return QUrl();
    if (u.isLocalFile()) return u;
    if (u.host().isEmpty() && u.scheme() != QLatin1String("mailto") && u.scheme() != QLatin1String("tel")) return QUrl();
    return u;
}

bool ZaloClient::openExternalUrl(const QString& url) {
    const QUrl u = externalUrl(url);
    if (!u.isValid()) {
        emit notice(L("Liên kết không hợp lệ.", "This link is not valid."));
        return false;
    }
    if (u.isLocalFile()) return openLocalFile(u.toLocalFile());
    if (!Fs::FileUtils::sandboxed()) {
        const QString failureFile = nextOpenFailureFile();
        if (Fs::FileUtils::openUrlDetached(u.toString(QUrl::FullyEncoded), failureFile)) {
            watchOpenFailure(failureFile, u);
            return true;
        }
    }
    if (QDesktopServices::openUrl(u)) return true;
    reportOpenFailure(u);
    return false;
}

QString ZaloClient::nextOpenFailureFile() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/open");
    if (!QDir().mkpath(dir)) return QString();
    const QDateTime staleBefore = QDateTime::currentDateTime().addSecs(-60);
    const QFileInfoList old = QDir(dir).entryInfoList({QStringLiteral("*.fail")}, QDir::Files);
    for (const QFileInfo& fi : old) {
        if (fi.lastModified() < staleBefore) QFile::remove(fi.absoluteFilePath());
    }
    return dir + QStringLiteral("/%1-%2.fail").arg(QCoreApplication::applicationPid()).arg(++m_openSeq);
}

void ZaloClient::watchOpenFailure(const QString& failureFile, const QUrl& url) {
    if (failureFile.isEmpty()) return;
    auto* timer = new QTimer(this);
    timer->setInterval(250);
    auto ticks = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [this, timer, ticks, failureFile, url]() {
        if (QFile::exists(failureFile)) {
            QFile::remove(failureFile);
            timer->stop();
            timer->deleteLater();
            reportOpenFailure(url);
            return;
        }
        if (++(*ticks) >= 40) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

void ZaloClient::reportOpenFailure(const QUrl& url) {
    ErrorCatalog::trace("open", QStringLiteral("open url failed scheme=%1").arg(url.scheme()));
    copyText(url.toString());
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        emit notice(L("Không mở được liên kết vì máy chưa có trình duyệt mặc định. Đã sao chép liên kết — hãy cài "
                      "hoặc chọn trình duyệt mặc định trong cài đặt hệ thống rồi dán vào trình duyệt.",
                      "Couldn't open the link because no default web browser is set up. The link was copied — "
                      "install or choose a default browser in your system settings, then paste it there."));
        return;
    }
    emit notice(L("Không có ứng dụng nào mở được liên kết này. Đã sao chép liên kết.",
                  "No app can open this link. The link was copied."));
}

bool ZaloClient::openLocalFile(const QString& path) {
    if (Fs::FileUtils::openLocalPath(path)) return true;
    ErrorCatalog::trace("open", QStringLiteral("open local file failed suffix=%1").arg(QFileInfo(path).suffix()));
    emit notice(L("Không có ứng dụng nào mở được tệp này. Tệp vẫn được lưu tại ", "No app could open this file. It is saved at ")
                + QDir::toNativeSeparators(path));
    return false;
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
    if (!m_pollFetchedAt.contains(pollId)) dropOldestIfFull(m_pollFetchedAt);
    m_pollFetchedAt.insert(pollId, now);
    m_apiClient->session()->fetchPollDetail(pollId, [this, pollId](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) {
            if (m_pollDialogId == pollId)
                emit notice(L("Có lỗi xảy ra khi lấy thông tin bình chọn", "Couldn't load the poll"));
            return;
        }
        storePollDetail(pollId, data.toObject());
    });
}

void ZaloClient::storePollDetail(qint64 pollId, const QJsonObject& detail) {
    if (pollId <= 0 || detail.isEmpty()) return;
    if (!m_pollDetails.contains(pollId)) dropOldestIfFull(m_pollDetails);
    m_pollDetails.insert(pollId, detail);
    m_messageModel->setPollData(pollId, pollView(pollId), m_pollGroups.value(pollId));
    ++m_pollRevision;
    emit pollRevisionChanged();
}

bool ZaloClient::canManagePoll(qint64 pollId, const QJsonObject& detail) const {
    if (m_myUid.isEmpty()) return false;
    if (idOf(detail.value("creator")) == m_myUid) return true;
    const QString gid = m_pollGroups.value(pollId, m_pollDialogGroupId);
    const auto roster = m_groupRosters.constFind(gid);
    return roster != m_groupRosters.cend() && (roster->creator == m_myUid || roster->admins.contains(m_myUid));
}

QVariantMap ZaloClient::pollView(qint64 pollId) const {
    const auto it = m_pollDetails.constFind(pollId);
    if (it == m_pollDetails.cend()) return {{"poll_id", pollId}, {"loaded", false}};
    return buildPollView(it.value(), m_myUid, canManagePoll(pollId, it.value()), QDateTime::currentMSecsSinceEpoch(),
                         [this](const QString& uid) {
                             if (uid == m_myUid) return qMakePair(m_userName, m_userAvatar);
                             return qMakePair(displayNameOf(uid), avatarOf(uid));
                         });
}

QVariantMap ZaloClient::buildPollView(const QJsonObject& d, const QString& myUid, bool canManage, qint64 nowMs,
                                      const PersonLookup& person) {
    const bool closed = d.value("closed").toBool();
    const bool joined = d.value("joined").toBool();
    const bool hidePreview = d.value("is_hide_vote_preview").toBool();
    const bool anonymous = d.value("is_anonymous").toBool();
    const qint64 expiredTime = d.value("expired_time").toVariant().toLongLong();
    const bool expired = expiredTime > 0 && expiredTime <= nowMs;
    const QString creator = idOf(d.value("creator"));
    const QJsonArray rawOptions = d.value("options").toArray();
    int totalVotes = 0;
    QSet<QString> distinctVoters;
    for (const QJsonValue& v : rawOptions) {
        const QJsonObject o = v.toObject();
        totalVotes += o.value("votes").toInt();
        for (const QJsonValue& u : o.value("voters").toArray()) distinctVoters.insert(idOf(u));
    }
    int voterCount = d.value("num_vote").toInt();
    if (voterCount <= 0) voterCount = int(distinctVoters.size());
    const bool hideResults = hidePreview && !joined;
    const bool afterVoteLock = joined && hidePreview;
    const bool canVote = !closed && !expired && !afterVoteLock;

    QVariantList options;
    for (const QJsonValue& v : rawOptions) {
        const QJsonObject o = v.toObject();
        const int votes = o.value("votes").toInt();
        QVariantList voters;
        for (const QJsonValue& u : o.value("voters").toArray()) {
            const QString uid = idOf(u);
            if (uid.isEmpty()) continue;
            const auto who = person ? person(uid) : qMakePair(QString(), QString());
            voters.append(QVariantMap{{"uid", uid}, {"name", who.first.isEmpty() ? uid : who.first},
                                      {"avatar", who.second}, {"isMe", uid == myUid}});
        }
        const double weight = totalVotes > 0 ? double(votes) / totalVotes : 0.0;
        options.append(QVariantMap{{"option_id", o.value("option_id").toInt()},
                                   {"content", o.value("content").toString()},
                                   {"votes", votes},
                                   {"voted", o.value("voted").toBool()},
                                   {"weight", weight},
                                   {"percent", qRound(weight * 100)},
                                   {"voters", voters},
                                   {"preview", voters.mid(0, 2)}});
    }
    const QString lockReason = closed ? QStringLiteral("closed")
                             : expired ? QStringLiteral("expired")
                             : afterVoteLock ? QStringLiteral("after_vote") : QString();
    const QString action = !canVote ? QStringLiteral("view") : joined ? QStringLiteral("change") : QStringLiteral("vote");
    const auto creatorInfo = (person && !creator.isEmpty()) ? person(creator) : qMakePair(QString(), QString());
    QStringList cardVi, cardEn, fullVi, fullEn;
    if (expiredTime > 0) {
        const QString at = QDateTime::fromMSecsSinceEpoch(expiredTime).toString(QStringLiteral("HH:mm dd/MM/yyyy"));
        cardVi << QStringLiteral("Kết thúc lúc ") + at;
        cardEn << QStringLiteral("Ends at ") + at;
    }
    fullVi = cardVi;
    fullEn = cardEn;
    const bool multi = d.value("allow_multi_choices").toBool();
    if (multi) {
        cardVi << QStringLiteral("Chọn nhiều phương án");
        cardEn << QStringLiteral("Multiple choices");
    }
    fullVi << (multi ? QStringLiteral("Chọn nhiều phương án") : QStringLiteral("Chỉ chọn một phương án"));
    fullEn << (multi ? QStringLiteral("Multiple choices") : QStringLiteral("Single choice"));
    if (d.value("allow_add_new_option").toBool()) {
        fullVi << QStringLiteral("Có thể thêm phương án");
        fullEn << QStringLiteral("Options can be added");
    }
    if (anonymous) {
        cardVi << QStringLiteral("Ẩn người bình chọn");
        cardEn << QStringLiteral("Voters hidden");
        fullVi << QStringLiteral("Ẩn người bình chọn");
        fullEn << QStringLiteral("Voters hidden");
    }
    if (hidePreview) {
        cardVi << QStringLiteral("Ẩn kết quả khi chưa bình chọn");
        cardEn << QStringLiteral("Results hidden until you vote");
        fullVi << QStringLiteral("Ẩn kết quả khi chưa bình chọn");
        fullEn << QStringLiteral("Results hidden until you vote");
    }
    const QString sep = QStringLiteral(" · ");
    return {
        {"poll_id", d.value("poll_id").toVariant().toLongLong()},
        {"loaded", true},
        {"question", d.value("question").toString()},
        {"closed", closed},
        {"joined", joined},
        {"expired", expired},
        {"expired_time", expiredTime},
        {"created_time", d.value("created_time").toVariant().toLongLong()},
        {"creator", creator},
        {"creator_name", creatorInfo.first},
        {"creator_is_me", !creator.isEmpty() && creator == myUid},
        {"allow_multi_choices", d.value("allow_multi_choices").toBool()},
        {"allow_add_new_option", d.value("allow_add_new_option").toBool()},
        {"is_anonymous", anonymous},
        {"is_hide_vote_preview", hidePreview},
        {"total_votes", totalVotes},
        {"voter_count", voterCount},
        {"hide_results", hideResults},
        {"can_vote", canVote},
        {"can_add_option", canVote && d.value("allow_add_new_option").toBool() && rawOptions.size() < 30},
        {"can_manage", canManage},
        {"can_view_voters", !anonymous && !hideResults && voterCount > 0},
        {"lock_reason", lockReason},
        {"action", action},
        {"settings_card_vi", cardVi.join(sep)},
        {"settings_card_en", cardEn.join(sep)},
        {"settings_full_vi", fullVi.join(sep)},
        {"settings_full_en", fullEn.join(sep)},
        {"options", options},
    };
}

QString ZaloClient::pollDraftCode(const QString& question, const QStringList& options) {
    const QString q = question.trimmed();
    if (q.isEmpty()) return QStringLiteral("question");
    if (q.size() > 200) return QStringLiteral("question_length");
    QStringList clean;
    QSet<QString> seen;
    for (const QString& o : options) {
        const QString t = o.trimmed();
        if (t.isEmpty()) continue;
        if (t.size() > 120) return QStringLiteral("option_length");
        const QString key = t.simplified().toCaseFolded();
        if (seen.contains(key)) return QStringLiteral("duplicate");
        seen.insert(key);
        clean.append(t);
    }
    if (clean.size() < 2) return QStringLiteral("min_options");
    if (clean.size() > 30) return QStringLiteral("max_options");
    return {};
}

QString ZaloClient::pollDraftError(const QString& question, const QStringList& options) const {
    const QString code = pollDraftCode(question, options);
    if (code == QLatin1String("question")) return L("Vui lòng đặt câu hỏi cho bình chọn.", "Please enter a poll question.");
    if (code == QLatin1String("question_length"))
        return L("Câu hỏi bình chọn tối đa 200 ký tự.", "The poll question can be at most 200 characters.");
    if (code == QLatin1String("option_length"))
        return L("Mỗi phương án tối đa 120 ký tự.", "Each option can be at most 120 characters.");
    if (code == QLatin1String("duplicate")) return L("Bạn đã tạo trùng phương án.", "You entered the same option twice.");
    if (code == QLatin1String("min_options"))
        return L("Bạn không thể tạo bình chọn ít hơn 2 phương án.", "A poll needs at least 2 options.");
    if (code == QLatin1String("max_options"))
        return L("Bạn đã tạo quá số lựa chọn cho phép (30).", "A poll can have at most 30 options.");
    return {};
}

QString ZaloClient::pollTemplateText(const QString& raw) {
    QString t = raw;
    t.remove(QStringLiteral("%1$s"));
    t.remove(QStringLiteral("%2$s"));
    t.remove(QStringLiteral("\"\""));
    t.remove(QStringLiteral("“”"));
    return t.simplified();
}

QString ZaloClient::pollErrorText(const QString& prefix, const QString& err, int code) const {
    if (code == 166) return L("Bình chọn đã bị khóa nên không thể thay đổi.", "The poll is closed, so it can't be changed.");
    return prefix + friendlyError(err, code);
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
    if (!m_voicePlayer && !Platform::MultimediaSupport::available()) {
        emit notice(L("Máy thiếu thành phần phát âm thanh (GStreamer/FFmpeg cho Qt Multimedia) nên chưa phát được tin nhắn thoại.",
                      "This system lacks the Qt Multimedia playback components (GStreamer/FFmpeg), so voice messages can't play."));
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
            if (!id.isEmpty()) {
                if (!m_voiceErrors.contains(id)) dropOldestIfFull(m_voiceErrors);
                m_voiceErrors.insert(id);
            }
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
                const QString avatar = p.value("avatar").toString();
                const QString name = uid == myCloudId() ? L("My Documents", "My Documents") : pickName(p);
                if (name.isEmpty()) continue;
                resolved.insert(uid);
                m_users[uid] = {name, avatar, 0};
                if (uid == myCloudId() && avatar.isEmpty()) m_unresolvedMembers.insert(uid);
                noteAccountFlags(uid, p);
                applyProfileToConversation(uid);
                persistProfile(uid, false);
            }
            refreshGroupSenderPrefixes(resolved);
            m_conversationModel->endBatch();
            m_groupMembersCache.clear();
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
            QSet<QString> resolved2;
            const QJsonObject profiles = d2.toObject().value("profiles").toObject();
            for (auto it = profiles.begin(); it != profiles.end(); ++it) {
                QString uid = it.key();
                if (uid.endsWith("_0")) uid.chop(2);
                const QJsonObject p = it.value().toObject();
                const QString name = pickName(p);
                if (name.isEmpty()) continue;
                resolved2.insert(uid);
                m_users[uid] = {name, p.value("avatar").toString(), 0};
                noteAccountFlags(uid, p);
                if (m_conversationModel->contains(uid)) applyProfileToConversation(uid);
                persistProfile(uid, false);
            }
            refreshGroupSenderPrefixes(resolved2);
            for (const QString& id : missing) {
                if (displayNameOf(id).isEmpty()) {
                    if (!m_unresolvedMembers.contains(id)) dropOldestIfFull(m_unresolvedMembers);
                    m_unresolvedMembers.insert(id);
                }
            }
            m_conversationModel->endBatch();
            m_groupMembersCache.clear();
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
    const auto& thread = m_messageModel->threadOf(m_currentConvId);
    for (int row = qMax(0, first); row <= last && row < thread.size(); ++row) {
        const auto& item = thread.at(row);
        maybeQueue(item.senderId);
        for (const auto& men : item.mentions) {
            maybeQueue(men.uid);
        }
        if (!m_rawMessages.isEmpty()) {
            const QJsonObject raw = m_rawMessages.value(item.id);
            if (!raw.isEmpty()) maybeQueue(idOf(raw.value("quote").toObject().value("ownerId")));
        }
    }
    if (queued && m_isLoggedIn) m_userLookupTimer->start();
}

void ZaloClient::rebuildContacts() {
    if (m_friendIds.size() + m_groups.size() <= 50) {
        if (m_rebuildContactsTimer) m_rebuildContactsTimer->stop();
        rebuildContactsNow();
        return;
    }
    if (m_rebuildContactsTimer && !m_rebuildContactsTimer->isActive())
        m_rebuildContactsTimer->start();
}

void ZaloClient::rebuildContactsNow() {
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
        if (!m_users.contains(threadId)
            || (threadId == myCloudId() && m_users.value(threadId).avatar.isEmpty() && !m_unresolvedMembers.contains(threadId))) {
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

QString ZaloClient::resolvedSenderName(const Models::MessageItem& item) const {
    if (item.isMine) return m_userName;
    const QString alias = m_aliases.value(item.senderId);
    if (!alias.isEmpty()) return alias;
    if (!item.senderName.isEmpty()) return item.senderName;
    return displayNameOf(item.senderId);
}

QString ZaloClient::previewPrefixFor(const Models::MessageItem& item, bool isGroup) const {
    if (item.isMine) return L("Bạn: ", "You: ");
    if (isGroup && item.messageType != QLatin1String("system")) {
        const QString name = resolvedSenderName(item);
        if (!name.isEmpty()) return name + QStringLiteral(": ");
    }
    return QString();
}

void ZaloClient::refreshGroupSenderPrefixes(const QSet<QString>& uids) {
    if (uids.isEmpty() || m_lastGroupSender.isEmpty()) return;
    for (auto it = m_lastGroupSender.cbegin(); it != m_lastGroupSender.cend(); ++it) {
        if (!uids.contains(it.value().uid)) continue;
        const QString& convId = it.key();
        const auto conv = m_conversationModel->conversation(convId);
        if (conv.id.isEmpty() || !conv.isGroup || conv.lastTs != it.value().ts) continue;
        const QString name = displayNameOf(it.value().uid);
        if (name.isEmpty()) continue;
        m_conversationModel->setSenderPrefix(convId, name + QStringLiteral(": "));
        if (m_offlineStore) m_offlineStore->saveConversation(m_conversationModel->conversation(convId));
    }
}

}
