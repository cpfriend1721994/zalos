#pragma once

#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <memory>

class QMediaPlayer;

#include "models/conversation_model.h"
#include "models/message_model.h"
#include "models/contact_model.h"

#include "call/zcall_engine.h"
#include "call/call_bridge.h"
#include "offline/offline_store.h"
#include "network/zalo_api_client.h"
#include "network/group_admin.h"
#include "network/label_manager.h"
#include "network/sticker_store.h"
#include "network/pin_board.h"
#include "models/rich_composer.h"
#include "platform/sound_engine.h"

#include "media/voice_recorder.h"
#include "platform/app_lock.h"
#include "platform/shortcut_map.h"
#include "platform/storage_manager.h"

namespace Zalos::Core::Platform { class DesktopNotifications; }

namespace Zalos::Core::Sync { class MobileHistorySync; }

namespace Zalos::Core::Network {

class ZaloClient : public QObject {
    Q_OBJECT

    // Core Native Wrappers from zalo-for-linux
    Q_PROPERTY(Zalos::Core::Call::ZCallEngine* callEngine READ callEngine CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Offline::ZaloOfflineStore* offlineStore READ offlineStore CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Network::ZaloApiClient* apiClient READ apiClient CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Call::CallBridge* calls READ calls CONSTANT FINAL)
    // Voice message recorder (composer mic button); the finished take is sent to the thread it was started in.
    Q_PROPERTY(Zalos::Core::Media::VoiceRecorder* voiceRecorder READ voiceRecorder CONSTANT FINAL)
    // Keyboard shortcut table (shortcut_map.h), passcode lock (app_lock.h) and local storage (storage_manager.h).
    Q_PROPERTY(Zalos::Core::Platform::ShortcutMap* shortcuts READ shortcuts CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Platform::AppLock* appLock READ appLock CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Platform::StorageManager* storage READ storage CONSTANT FINAL)
    // Group link / permissions / join requests / blocked list, join-by-link preview, invite to groups.
    Q_PROPERTY(Zalos::Core::Network::GroupAdmin* groupAdmin READ groupAdmin CONSTANT FINAL)
    // Conversation labels ("Phân loại"): list, assign, create/edit/delete/reorder (LabelManageModal).
    Q_PROPERTY(Zalos::Core::Network::LabelManager* labels READ labels CONSTANT FINAL)
    // Sticker packs, pack contents, search, recent; render info for animated stickers (StickerPicker / ZSticker).
    Q_PROPERTY(Zalos::Core::Network::StickerStore* stickers READ stickers CONSTANT FINAL)
    // Pinned messages of the open conversation (group board / friend board), with pin, unpin, move to top.
    Q_PROPERTY(Zalos::Core::Network::PinBoard* pins READ pins CONSTANT FINAL)
    // Composer rich text (Zalo text styles), attached to ChatArea's msgInput document.
    Q_PROPERTY(Zalos::Core::Models::RichComposer* composer READ composer CONSTANT FINAL)
    // First sign-in of an account that never synced its phone history: ask once (Main.qml SyncPromptDialog).
    Q_PROPERTY(bool showSyncPrompt READ showSyncPrompt NOTIFY syncPromptChanged FINAL)

    // Session & Auth
    Q_PROPERTY(bool isLoggedIn READ isLoggedIn NOTIFY loginStateChanged FINAL)
    Q_PROPERTY(bool isRestoringSession READ isRestoringSession NOTIFY restoringSessionChanged FINAL)
    Q_PROPERTY(QString currentUserName READ currentUserName NOTIFY userProfileChanged FINAL)
    // Signed-in account's uid ("" before sign-in).
    Q_PROPERTY(QString myUid READ myUid NOTIFY userProfileChanged FINAL)
    Q_PROPERTY(QString currentUserPhone READ currentUserPhone NOTIFY userProfileChanged FINAL)
    Q_PROPERTY(QString currentUserAvatar READ currentUserAvatar NOTIFY userProfileChanged FINAL)
    Q_PROPERTY(bool isOfflineMode READ isOfflineMode NOTIFY offlineModeChanged FINAL)
    Q_PROPERTY(bool isLoggingIn READ isLoggingIn NOTIFY loginStateChanged FINAL)
    Q_PROPERTY(bool hasLoginError READ hasLoginError NOTIFY loginStatusChanged FINAL)
    Q_PROPERTY(QString loginStatusMessage READ loginStatusMessage NOTIFY loginStatusChanged FINAL)
    Q_PROPERTY(QString qrImageUrl READ qrImageUrl NOTIFY qrCodeChanged FINAL)
    Q_PROPERTY(QString qrCodeData READ qrCodeData NOTIFY qrCodeChanged FINAL)
    Q_PROPERTY(int qrSecondsRemaining READ qrSecondsRemaining NOTIFY qrTimerTick FINAL)
    Q_PROPERTY(int qrSize READ qrSize NOTIFY qrMatrixChanged FINAL)
    Q_PROPERTY(QVariantList qrMatrix READ qrMatrix NOTIFY qrMatrixChanged FINAL)
    Q_PROPERTY(int qrLoginStep READ qrLoginStep NOTIFY qrLoginStepChanged FINAL)
    Q_PROPERTY(QString scannedUserName READ scannedUserName NOTIFY qrLoginStepChanged FINAL)
    Q_PROPERTY(QString scannedUserAvatar READ scannedUserAvatar NOTIFY qrLoginStepChanged FINAL)

    // Chat view
    Q_PROPERTY(QString currentConversationId READ currentConversationId NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(QString currentConversationName READ currentConversationName NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(QString currentConversationAvatar READ currentConversationAvatar NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationIsGroup READ currentConversationIsGroup NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(int currentConversationMemberCount READ currentConversationMemberCount NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationPinned READ currentConversationPinned NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationMuted READ currentConversationMuted NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationIsOnline READ currentConversationIsOnline NOTIFY conversationStatusChanged FINAL)
    Q_PROPERTY(bool isLoadingHistory READ isLoadingHistory NOTIFY historyLoadingChanged FINAL)
    Q_PROPERTY(bool isSyncing READ isSyncing NOTIFY syncStateChanged FINAL)
    Q_PROPERTY(QString myCloudId READ myCloudId NOTIFY userProfileChanged FINAL)
    Q_PROPERTY(QString currentConversationStatus READ currentConversationStatus NOTIFY conversationStatusChanged FINAL)
    Q_PROPERTY(bool isSendingAttachment READ isSendingAttachment NOTIFY attachmentStateChanged FINAL)
    Q_PROPERTY(QVariantList todoItems READ todoItems NOTIFY todosChanged FINAL)
    Q_PROPERTY(bool isLoadingTodos READ isLoadingTodos NOTIFY todosChanged FINAL)
    // Why personal To-Do cannot be used with this session ("" = it can): shown in ToDoView instead of toasts.
    Q_PROPERTY(QString todoUnavailable READ todoUnavailable NOTIFY todosChanged FINAL)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStateChanged FINAL)
    Q_PROPERTY(bool isE2EESupported READ isE2EESupported CONSTANT FINAL)
    Q_PROPERTY(bool isIncomingCallActive READ isIncomingCallActive NOTIFY incomingCallStateChanged FINAL)
    Q_PROPERTY(QString incomingCallerName READ incomingCallerName NOTIFY incomingCallStateChanged FINAL)
    Q_PROPERTY(Zalos::Core::Models::ConversationModel* conversationModel READ conversationModel CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Models::MessageModel* messageModel READ messageModel CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Models::ContactModel* contactModel READ contactModel CONSTANT FINAL)
    Q_PROPERTY(QString appLanguage READ appLanguage WRITE setAppLanguage NOTIFY appLanguageChanged FINAL)

    // Modals
    Q_PROPERTY(bool showSettingsModal READ showSettingsModal WRITE setShowSettingsModal NOTIFY settingsModalChanged FINAL)
    Q_PROPERTY(QString settingsInitialTab READ settingsInitialTab NOTIFY settingsModalChanged FINAL)
    Q_PROPERTY(bool showUserProfileModal READ showUserProfileModal WRITE setShowUserProfileModal NOTIFY userProfileModalChanged FINAL)
    Q_PROPERTY(bool showAddFriendModal READ showAddFriendModal WRITE setShowAddFriendModal NOTIFY addFriendModalChanged FINAL)
    Q_PROPERTY(bool showCreateGroupModal READ showCreateGroupModal WRITE setShowCreateGroupModal NOTIFY createGroupModalChanged FINAL)
    Q_PROPERTY(bool showConversationInfo READ showConversationInfo WRITE setShowConversationInfo NOTIFY conversationInfoChanged FINAL)
    Q_PROPERTY(bool showForwardModal READ showForwardModal WRITE setShowForwardModal NOTIFY forwardModalChanged FINAL)
    Q_PROPERTY(QString forwardSourceMsgId READ forwardSourceMsgId NOTIFY forwardModalChanged FINAL)
    Q_PROPERTY(QString forwardSourcePreview READ forwardSourcePreview NOTIFY forwardModalChanged FINAL)
    // ForwardMessageModal modes: "message" (forward forwardSourceMsgId), "card" (share forwardCardUid's contact
    // card to the chosen conversations), "pickContact" (choose a friend whose card goes to the open conversation).
    Q_PROPERTY(QString forwardMode READ forwardMode NOTIFY forwardModalChanged FINAL)
    Q_PROPERTY(QString forwardCardUid READ forwardCardUid NOTIFY forwardModalChanged FINAL)
    Q_PROPERTY(bool showCreatePollModal READ showCreatePollModal WRITE setShowCreatePollModal NOTIFY createPollModalChanged FINAL)
    Q_PROPERTY(QString currentNavigationTab READ currentNavigationTab WRITE setCurrentNavigationTab NOTIFY navigationTabChanged FINAL)

    // Pinned Message in current conversation
    Q_PROPERTY(bool hasPinnedMessage READ hasPinnedMessage NOTIFY pinnedMessageChanged FINAL)
    Q_PROPERTY(QString currentPinnedMessageId READ currentPinnedMessageId NOTIFY pinnedMessageChanged FINAL)
    Q_PROPERTY(QString currentPinnedMessageText READ currentPinnedMessageText NOTIFY pinnedMessageChanged FINAL)
    Q_PROPERTY(QString currentPinnedMessageSender READ currentPinnedMessageSender NOTIFY pinnedMessageChanged FINAL)

    // Profile Modal Properties (Viewing self or another user)
    Q_PROPERTY(QString profileUserId READ profileUserId NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(bool profileIsSelf READ profileIsSelf NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(QString profileDisplayName READ profileDisplayName NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(QString profileZaloName READ profileZaloName NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(QString profileAvatar READ profileAvatar NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(QString profileCover READ profileCover NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(int profileGender READ profileGender NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(QString profileDob READ profileDob NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(QString profileBio READ profileBio NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(QString profilePhone READ profilePhone NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(bool profileIsFriend READ profileIsFriend NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(bool profileIsBlocked READ profileIsBlocked NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(bool profileLoading READ profileLoading NOTIFY currentProfileChanged FINAL)

    // Mobile E2EE Sync
    Q_PROPERTY(int mobileSyncState READ mobileSyncState NOTIFY mobileSyncStateChanged FINAL)
    Q_PROPERTY(QString mobileSyncStatus READ mobileSyncStatus NOTIFY mobileSyncStateChanged FINAL)
    Q_PROPERTY(int mobileSyncProgress READ mobileSyncProgress NOTIFY mobileSyncProgressChanged FINAL)

    // Settings
    Q_PROPERTY(QVariantMap replyTarget READ replyTarget NOTIFY replyTargetChanged FINAL)
    // Scheduled messages of the open conversation: [{id, text, sendAt, state}].
    Q_PROPERTY(QVariantList scheduledMessages READ scheduledMessages NOTIFY scheduledMessagesChanged FINAL)
    Q_PROPERTY(bool historyExhausted READ historyExhausted NOTIFY historyLoadingChanged FINAL)
    Q_PROPERTY(bool notifyNewMessage READ notifyNewMessage WRITE setNotifyNewMessage NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool notifySound READ notifySound WRITE setNotifySound NOTIFY settingsChanged FINAL)
    // Anti-peeking: messages are dimmed until hovered and notifications hide sender/content.
    Q_PROPERTY(bool antiPeeking READ antiPeeking WRITE setAntiPeeking NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool useAvatarAsChatBackground READ useAvatarAsChatBackground WRITE setUseAvatarAsChatBackground NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool startHiddenInTray READ startHiddenInTray WRITE setStartHiddenInTray NOTIFY settingsChanged FINAL)
    // Closing the window hides it to the tray (Zalo PC behaviour) instead of quitting.
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY settingsChanged FINAL)
    // Launch at sign-in to the desktop: ~/.config/autostart/zalos.desktop exists.
    Q_PROPERTY(bool startWithSystem READ startWithSystem WRITE setStartWithSystem NOTIFY settingsChanged FINAL)
    // A system tray is available (hide-to-tray settings only make sense with one).
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT FINAL)
    // Decided once in main.cpp before the window exists: start hidden in the tray (no flash of the window).
    Q_PROPERTY(bool launchHidden READ launchHidden CONSTANT FINAL)

    // ZaPrivacy Settings
    Q_PROPERTY(bool blockTyping READ blockTyping WRITE setBlockTyping NOTIFY privacySettingsChanged FINAL)
    Q_PROPERTY(bool blockDelivered READ blockDelivered WRITE setBlockDelivered NOTIFY privacySettingsChanged FINAL)
    Q_PROPERTY(bool blockSeen READ blockSeen WRITE setBlockSeen NOTIFY privacySettingsChanged FINAL)

    // ZaSounds Settings
    Q_PROPERTY(QString selectedRingtone READ selectedRingtone WRITE setSelectedRingtone NOTIFY soundSettingsChanged FINAL)
    Q_PROPERTY(QString selectedNotiSound READ selectedNotiSound WRITE setSelectedNotiSound NOTIFY soundSettingsChanged FINAL)
    Q_PROPERTY(bool isPreviewPlaying READ isPreviewPlaying NOTIFY previewPlayingChanged FINAL)
    Q_PROPERTY(QString currentPreviewSound READ currentPreviewSound NOTIFY previewPlayingChanged FINAL)

    // Quick messages (/api/quickmessage/list): [{id, keyword, text, hasMedia}]
    Q_PROPERTY(QVariantList quickMessages READ quickMessages NOTIFY quickMessagesChanged FINAL)
    // Voice message being played in-app (empty when idle).
    Q_PROPERTY(QString playingVoiceId READ playingVoiceId NOTIFY voicePlaybackChanged FINAL)

public:
    // The instance main() (or a test) created; QML's "ZaloClient" singleton resolves to it.
    static inline ZaloClient* s_instance = nullptr;
    explicit ZaloClient(QObject* parent = nullptr);

    // Auth Getters
    bool isLoggedIn() const { return m_isLoggedIn; }
    bool isRestoringSession() const { return m_restoringSession; }
    QString currentUserName() const { return m_userName; }
    QString currentUserPhone() const { return m_userPhone; }
    QString currentUserAvatar() const { return m_userAvatar; }
    bool isOfflineMode() const { return m_isOfflineMode; }
    bool isLoggingIn() const { return m_isLoggingIn; }
    bool hasLoginError() const { return m_hasLoginError; }
    QString loginStatusMessage() const { return m_loginStatusMessage; }
    QString qrImageUrl() const { return m_qrImageUrl; }
    QString qrCodeData() const { return m_qrCodeData; }
    int qrSecondsRemaining() const { return m_qrSecondsRemaining; }
    int qrSize() const { return m_qrSize; }
    QVariantList qrMatrix() const { return m_qrMatrix; }
    int qrLoginStep() const { return m_qrLoginStep; }
    QString scannedUserName() const { return m_scannedUserName; }
    QString scannedUserAvatar() const { return m_scannedUserAvatar; }

    // Chat Getters
    QString currentConversationId() const { return m_currentConvId; }
    QString currentConversationName() const { return m_currentConvName; }
    QString currentConversationAvatar() const { return m_currentConvAvatar; }
    bool currentConversationIsGroup() const;
    int currentConversationMemberCount() const;
    bool currentConversationPinned() const;
    bool currentConversationMuted() const;
    bool currentConversationIsOnline() const;
    bool isLoadingHistory() const { return m_loadingHistory; }
    bool isSyncing() const { return m_syncing; }
    QString myCloudId() const;
    QString currentConversationStatus() const;
    bool isSendingAttachment() const { return m_pendingAttachments > 0; }
    QVariantList todoItems() const { return m_todoItems; }
    bool isLoadingTodos() const { return m_loadingTodos; }
    QString todoUnavailable() const { return m_todoUnavailable; }
    Platform::SoundEngine* soundEngine() const { return m_sound; }
    Call::CallBridge* calls() const { return m_calls; }
    Media::VoiceRecorder* voiceRecorder() const { return m_voiceRecorder; }
    Platform::ShortcutMap* shortcuts() const { return m_shortcuts; }
    Platform::AppLock* appLock() const { return m_appLock; }
    Platform::StorageManager* storage() const { return m_storage; }
    GroupAdmin* groupAdmin() const { return m_groupAdmin; }
    LabelManager* labels() const { return m_labelManager; }
    StickerStore* stickers() const { return m_stickerStore; }
    PinBoard* pins() const { return m_pinBoard; }
    QString myUid() const { return m_myUid; }
    Models::RichComposer* composer() const { return m_composer; }
    bool showSyncPrompt() const { return m_showSyncPrompt; }
    // "Đồng bộ ngay": starts the phone sync and shows its progress (Settings > Đồng bộ tin nhắn).
    Q_INVOKABLE void acceptSyncPrompt();
    // "Để sau" (ask again at the next sign-in) or "Không hỏi lại" (`permanently`).
    Q_INVOKABLE void dismissSyncPrompt(bool permanently);
    // Decides whether to ask (signed in, phone history never synced for this uid, not declined for good).
    void maybeOfferMobileSync();
    // chat.next / chat.previous: the conversation `delta` rows away in the sidebar's order.
    Q_INVOKABLE void selectAdjacentConversation(int delta);
    // chat.pinnedN: the n-th (1-based) pinned conversation.
    Q_INVOKABLE void openPinnedConversation(int n);
    // Local echo + upload of a recorded voice message (ADTS AAC file) to `convId`.
    void sendVoiceMessage(const QString& convId, const QString& filePath, int durationMs);
    Q_INVOKABLE void startVoiceCall();
    Q_INVOKABLE void startVideoCall();
    bool isConnected() const { return m_isConnected; }
    bool isE2EESupported() const { return true; }

    bool isIncomingCallActive() const { return m_incomingCallActive; }
    QString incomingCallerName() const { return m_incomingCallerName; }

    Models::ConversationModel* conversationModel() const { return m_conversationModel; }
    Models::MessageModel* messageModel() const { return m_messageModel; }
    Models::ContactModel* contactModel() const { return m_contactModel; }

    QString appLanguage() const { return m_appLanguage; }
    Q_INVOKABLE void setAppLanguage(const QString& lang);

    Q_INVOKABLE void openConversationWithContact(const QString& id, const QString& name, const QString& avatarUrl);
    Call::ZCallEngine* callEngine() const { return m_callEngine; }
    Offline::ZaloOfflineStore* offlineStore() const { return m_offlineStore; }
    ZaloApiClient* apiClient() const { return m_apiClient; }

    // Modals
    bool showSettingsModal() const { return m_showSettingsModal; }
    void setShowSettingsModal(bool show);
    QString settingsInitialTab() const { return m_settingsInitialTab; }

    bool showUserProfileModal() const { return m_showUserProfileModal; }
    void setShowUserProfileModal(bool show);

    // Profile Modal Getters
    QString profileUserId() const;
    bool profileIsSelf() const;
    QString profileDisplayName() const;
    QString profileZaloName() const;
    QString profileAvatar() const;
    QString profileCover() const;
    int profileGender() const;
    QString profileDob() const;
    QString profileBio() const;
    QString profilePhone() const;
    bool profileIsFriend() const;
    bool profileIsBlocked() const;
    bool profileLoading() const;

    bool showAddFriendModal() const { return m_showAddFriendModal; }
    void setShowAddFriendModal(bool show);

    bool showCreateGroupModal() const { return m_showCreateGroupModal; }
    void setShowCreateGroupModal(bool show);

    bool showConversationInfo() const { return m_showConversationInfo; }
    void setShowConversationInfo(bool show);

    bool showForwardModal() const { return m_showForwardModal; }
    void setShowForwardModal(bool show);
    QString forwardSourceMsgId() const { return m_forwardSourceMsgId; }
    QString forwardSourcePreview() const { return m_forwardSourcePreview; }
    Q_INVOKABLE void openForwardModal(const QString& msgId, const QString& previewText = QString());
    Q_INVOKABLE void closeForwardModal();
    QString forwardMode() const { return m_forwardMode; }
    QString forwardCardUid() const { return m_forwardCardUid; }
    Q_INVOKABLE void openShareCard(const QString& userId, const QString& name);
    Q_INVOKABLE void openSendCardPicker();
    // Friends for the card picker: [{id, name, avatar}] sorted by name.
    Q_INVOKABLE QVariantList friendsForCard() const;
    Q_INVOKABLE void forwardMessage(const QString& targetThreadId, const QString& msgId);

    // Group Polls
    bool showCreatePollModal() const { return m_showCreatePollModal; }
    void setShowCreatePollModal(bool show);
    Q_INVOKABLE void openCreatePollModal();
    Q_INVOKABLE void closeCreatePollModal();
    Q_INVOKABLE void createPoll(const QString& groupId, const QString& question, const QStringList& options, bool allowMultiChoices);
    Q_INVOKABLE void votePoll(qint64 pollId, int optionId);

    // Group management APIs
    Q_INVOKABLE void changeGroupName(const QString& groupId, const QString& newName);
    Q_INVOKABLE void addMemberToGroup(const QString& groupId, const QString& memberId);
    Q_INVOKABLE void removeMemberFromGroup(const QString& groupId, const QString& memberId);
    Q_INVOKABLE void leaveGroup(const QString& groupId, bool silent = false);

    // Clipboard image paste
    Q_INVOKABLE bool tryPasteClipboardImage();

    // Pinned Message APIs
    bool hasPinnedMessage() const;
    QString currentPinnedMessageId() const;
    QString currentPinnedMessageText() const;
    QString currentPinnedMessageSender() const;
    // Kept for older callers: the top pin of the open conversation (ZaloClient.pins has the whole board).
    Q_INVOKABLE void pinMessage(const QString& msgId);
    Q_INVOKABLE void unpinMessage();

    // Friend Alias & Contact Card
    Q_INVOKABLE void changeFriendAlias(const QString& friendId, const QString& newAlias);
    Q_INVOKABLE void sendContactCard(const QString& targetThreadId, const QString& contactId, const QString& contactName, const QString& phone = QString());

    // Mobile E2EE Sync Getters & APIs
    int mobileSyncState() const { return m_mobileSyncState; }
    QString mobileSyncStatus() const { return m_mobileSyncStatus; }
    int mobileSyncProgress() const { return m_mobileSyncProgress; }
    Q_INVOKABLE void requestMobileSync();
    Q_INVOKABLE void cancelMobileSync();

    QString currentNavigationTab() const { return m_currentNavigationTab; }
    Q_INVOKABLE void setCurrentNavigationTab(const QString& tab);

    // Settings
    bool notifyNewMessage() const { return m_notifyNewMessage; }
    void setNotifyNewMessage(bool val);

    bool notifySound() const { return m_notifySound; }
    bool antiPeeking() const { return m_antiPeeking; }
    Q_INVOKABLE void setAntiPeeking(bool val);
    QVariantMap replyTarget() const { return m_replyTarget; }
    QVariantList scheduledMessages() const;
    bool historyExhausted() const { return m_historyExhausted.contains(m_currentConvId); }
    void setNotifySound(bool val);

    bool useAvatarAsChatBackground() const { return m_useAvatarAsChatBackground; }
    void setUseAvatarAsChatBackground(bool val);

    bool startHiddenInTray() const { return m_startHiddenInTray; }
    void setStartHiddenInTray(bool val);
    bool closeToTray() const { return m_closeToTray; }
    void setCloseToTray(bool val);
    bool startWithSystem() const;
    void setStartWithSystem(bool val);
    bool trayAvailable() const;
    bool launchHidden() const { return m_launchHidden; }
    void setLaunchHidden(bool hidden) { m_launchHidden = hidden; }
    static QString autostartFilePath();


    // ZaPrivacy Getters & Setters
    bool blockTyping() const { return m_blockTyping; }
    void setBlockTyping(bool val);

    bool blockDelivered() const { return m_blockDelivered; }
    void setBlockDelivered(bool val);

    bool blockSeen() const { return m_blockSeen; }
    void setBlockSeen(bool val);

    // ZaSounds Getters & Setters
    QString selectedRingtone() const { return m_selectedRingtone; }
    void setSelectedRingtone(const QString& val);

    QString selectedNotiSound() const { return m_selectedNotiSound; }
    void setSelectedNotiSound(const QString& val);

    bool isPreviewPlaying() const { return m_isPreviewPlaying; }
    QString currentPreviewSound() const { return m_currentPreviewSound; }

    Q_INVOKABLE void playSoundPreview(const QString& soundType, const QString& soundKey);
    Q_INVOKABLE void stopSoundPreview();

    // Invokable APIs
    Q_INVOKABLE void loginWithPhone(const QString& phone, const QString& password, bool remember = true);
    Q_INVOKABLE void syncFromBrowserSession();
    Q_INVOKABLE void cancelQrScan();
    Q_INVOKABLE void refreshQrCode();
    Q_INVOKABLE void clearLoginError();
    Q_INVOKABLE void logout();

#ifdef ZALOS_TESTING
    // Applies a complete nickname list as the alias-list refetch does.
    void applyAliasesForTesting(const QHash<QString, QString>& aliases) { applyAliases(aliases); }
    // Applies a getmg-v2 gridInfoMap as loadGroupInfo does.
    void applyGroupInfoForTesting(const QJsonObject& gridInfoMap) { applyGroupInfo(gridInfoMap); }
    // Converts a raw server message as the realtime path does.
    Models::MessageItem toMessageItemForTesting(const QJsonObject& msg, bool isGroup) const {
        QString thread, preview;
        return toMessageItem(msg, isGroup, &thread, &preview);
    }
    // Messages that would get a "delivered" receipt, grouped by thread.
    QHash<QString, QJsonArray> deliveredCandidatesForTesting(const QJsonArray& msgs, bool isGroup) const {
        return deliveredCandidates(msgs, isGroup);
    }
    void setMyUidForTesting(const QString& uid) { m_myUid = uid; }
    void handleReactionsForTesting(const QJsonArray& reactions, bool isGroup) { handleReactions(reactions, isGroup); }
    Platform::DesktopNotifications* notificationsForTesting() const { return m_notifications; }
    void reloadDraftsForTesting() { loadDrafts(); }
    // Opens the offline cache of `uid` as the app does at an offline start.
    bool openOfflineCacheForTesting(const QString& uid) {
        m_cachedUid = uid;
        return loadOfflineCache();
    }
    // Renders the signed-in layout in headless tests without a live session.
    void setLoggedInForTesting(bool loggedIn) {
        m_isLoggedIn = loggedIn;
        m_restoringSession = false;
        emit restoringSessionChanged();
        emit loginStateChanged();
    }
#endif

    // Group roster from getmg-v2 (memVerList, adminIds, creatorId); names of non-friends come from
    // /api/social/group/members. Entries: {id, name, avatar, role: "owner"|"admin"|"member", isMe}.
    Q_INVOKABLE QVariantList groupMembers(const QString& groupId);
    Q_INVOKABLE QString myGroupRole(const QString& groupId) const;
    Q_INVOKABLE void setGroupDeputy(const QString& groupId, const QString& memberId, bool add);
    Q_INVOKABLE void transferGroupOwner(const QString& groupId, const QString& memberId);
    Q_INVOKABLE void disperseGroup(const QString& groupId);
    // Fetches /api/poll/detail and updates every loaded copy of the poll (throttled per poll).
    Q_INVOKABLE void refreshPoll(qint64 pollId, bool force = false);
    // mentions: [{uid, pos, len}] with positions in `text` (before trimming); groups only.
    Q_INVOKABLE void sendMessageWithMentions(const QString& text, const QVariantList& mentions);
    // Composer send: mentions [{uid, pos, len}] and text styles [{start, len, st}] over the untrimmed text.
    Q_INVOKABLE void sendComposedMessage(const QString& text, const QVariantList& mentions, const QVariantList& styles);
    QVariantList quickMessages() const { return m_quickMessages; }
    Q_INVOKABLE void loadQuickMessages();
    QString playingVoiceId() const { return m_playingVoiceId; }
    // Plays (or stops) a voice message; falls back to the system player when Qt Multimedia cannot decode it.
    Q_INVOKABLE void toggleVoice(const QString& msgId, const QString& url);

    Q_INVOKABLE void openWelcomeScreen();
    Q_INVOKABLE void selectConversation(const QString& id, const QString& name, const QString& avatar);
    // Unsent composer text per thread, kept per account in QSettings ("drafts/<uid>") and shown in the sidebar.
    Q_INVOKABLE void setDraft(const QString& convId, const QString& text);
    Q_INVOKABLE QString draftFor(const QString& convId) const;
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void sendAttachment(const QString& type, const QString& name, const QString& url);
    Q_INVOKABLE bool testE2EEDecryption();

    Q_INVOKABLE void openSettings(const QString& initialTab = "appearance");
    Q_INVOKABLE void closeSettings();
    Q_INVOKABLE void openUserProfile();
    Q_INVOKABLE void viewProfile(const QString& userId);
    Q_INVOKABLE void closeUserProfile();
    Q_INVOKABLE void updateMyProfile(const QString& name, int gender, const QString& dob, const QString& bio);
    Q_INVOKABLE void blockUser(const QString& userId);
    Q_INVOKABLE void unblockUser(const QString& userId);
    Q_INVOKABLE void removeFriend(const QString& userId);

    Q_INVOKABLE void openAddFriend();
    Q_INVOKABLE void closeAddFriend();
    Q_INVOKABLE void openCreateGroup();
    Q_INVOKABLE void closeCreateGroup();
    Q_INVOKABLE void toggleConversationInfo();
    Q_INVOKABLE void findUserByPhone(const QString& phone);
    Q_INVOKABLE void sendFriendRequest(const QString& uid, const QString& message);
    Q_INVOKABLE void createGroup(const QString& name, const QStringList& members);
    Q_INVOKABLE void openMyCloud();
    Q_INVOKABLE void loadOlderMessages();
    Q_INVOKABLE void refreshContacts();

    // Phase-1 messaging features
    Q_INVOKABLE void sendFile(const QString& fileUrlOrPath, const QString& caption = QString());
    Q_INVOKABLE void searchStickers(const QString& keyword);
    Q_INVOKABLE void sendStickerItem(qint64 stickerId, qint64 cateId, int type);
    Q_INVOKABLE void reactToMessage(const QString& msgId, const QString& reactionKey);
    // New avatar for the signed-in account / a group (owner or deputy), from a local picture (file:// URL or path).
    Q_INVOKABLE void changeMyAvatar(const QString& fileUrlOrPath);
    Q_INVOKABLE void changeGroupAvatar(const QString& groupId, const QString& fileUrlOrPath);
    // Every reaction zalos can send ([{key, emoji}], Zalo order) — the message menu scrolls through them.
    Q_INVOKABLE QVariantList reactionList() const;
    // Desktop sound-theme sounds for the notification/ringtone pickers ([{key: "system:<path>", label}]).
    Q_INVOKABLE QVariantList systemSounds() const;
    Q_INVOKABLE bool canRecall(const QString& msgId) const;
    Q_INVOKABLE void recallMessage(const QString& msgId);
    Q_INVOKABLE void deleteMessageForMe(const QString& msgId);
    Q_INVOKABLE void copyText(const QString& text);
    // Who reacted with what (live session only; restored history keeps the summary):
    // {total, known, groups:[{emoji, count}], people:[{uid, name, avatar, emoji, isMe}]}
    Q_INVOKABLE QVariantMap reactionDetails(const QString& msgId) const;
    // Takes back our reaction (zca-js Reactions.NONE: rIcon "", rType -1).
    Q_INVOKABLE void removeMyReaction(const QString& msgId);
    // A transient notice (Main.qml toast) raised from QML.
    Q_INVOKABLE void showNotice(const QString& text) { emit notice(text); }
    // Chat links: a zalo.me/g/<code> invite opens the in-app group preview, anything else the browser.
    Q_INVOKABLE void openLink(const QString& url);
    // Groups of the account for the "Add to group" picker: [{id, name, avatar, isMember}] (isMember: `userId`
    // is already in the group's roster), sorted by name.
    Q_INVOKABLE QVariantList groupsForInvite(const QString& userId) const;
    // Composite avatar members of a group without a photo (see ZAvatar.memberAvatars); [] otherwise.
    // `revision` only re-triggers QML bindings (pass the member count).
    Q_INVOKABLE QVariantList groupComposite(const QString& groupId, int revision = 0) const {
        Q_UNUSED(revision);
        return m_conversationModel->memberAvatars(groupId);
    }
    // Media helpers for the image viewer and file messages. `url` may be file:// or http(s) (session cookies).
    Q_INVOKABLE void saveMediaTo(const QString& url, const QString& destinationFileUrl);
    Q_INVOKABLE void copyImage(const QString& url);
    // Downloads a file message into ~/Downloads/Zalos (once) and opens it with the default app.
    Q_INVOKABLE void openFileMessage(const QString& url, const QString& fileName);
    Q_INVOKABLE QString suggestedMediaFileName(const QString& url) const;
    Q_INVOKABLE void notifyTyping();
    Q_INVOKABLE void acceptFriendRequest(const QString& uid);
    Q_INVOKABLE void rejectFriendRequest(const QString& uid);
    Q_INVOKABLE void refreshFriendRequests();

    // To-Do ("assigned" = assigned to me, "created" = I assigned, "done" = finished)
    Q_INVOKABLE void loadTodos(const QString& tab);
    Q_INVOKABLE void createTodo(const QString& content, qint64 dueDateMs);
    Q_INVOKABLE void setTodoDone(const QString& id, bool done);
    Q_INVOKABLE void deleteTodo(const QString& id);
    Q_INVOKABLE void pinConversation(const QString& id);
    Q_INVOKABLE void muteConversation(const QString& id);
    // Mute for `seconds` (3600, 14400, ...), -1 = until turned back on, -2 = until 8 AM; 0 unmutes.
    Q_INVOKABLE void muteConversationFor(const QString& id, int seconds);
    // Toggles the server-side "marked as unread" flag (sidebar context menu).
    Q_INVOKABLE void toggleUnreadMark(const QString& id);
    Q_INVOKABLE bool isMarkedUnread(const QString& id) const { return m_markedUnread.contains(id); }
    // Deletes the conversation for this account only (server deleteconver, onlyMe).
    Q_INVOKABLE void deleteConversation(const QString& id);

    // Quote reply: the next sendMessage() replies to this message until cleared.
    Q_INVOKABLE void setReplyTarget(const QString& msgId);
    Q_INVOKABLE void clearReplyTarget();

    // Scheduled send (local: delivered by this app while it is running and signed in).
    Q_INVOKABLE bool scheduleMessage(const QString& text, qint64 sendAtMs);
    Q_INVOKABLE void cancelScheduledMessage(const QString& id);

    // Call and Screenshot APIs resolving Issue #147 and #156
#ifdef ZALOS_TESTING
    // Drives the incoming-call popup in headless tests; never reachable from the shipped UI.
    void triggerSimulatedIncomingCall(const QString& callerName = "Người gọi Zalo");
#endif
    Q_INVOKABLE void answerCall();
    Q_INVOKABLE void declineCall();
    // Interactive screenshot through xdg-desktop-portal; the picture is sent to the open conversation.
    Q_INVOKABLE void requestScreenshot();
    Q_SLOT void onScreenshotResponse(uint response, const QVariantMap& results);

    // Native wrappers from zalo-for-linux (file-utils, file-utilities, zimage, mp4thumb, offline)
    Q_INVOKABLE QVariantMap getDiskUsage(const QString& path = QString());
    Q_INVOKABLE QVariantMap getDirectorySize(const QString& path);
    Q_INVOKABLE bool generateThumbnail(const QString& inputPath, const QString& outputPath, int width = 256, int height = 256);
    Q_INVOKABLE bool extractVideoThumbnail(const QString& videoPath, const QString& outputPath);
    Q_INVOKABLE QString recognizeImageText(const QString& imagePath);
    Q_INVOKABLE void syncOfflineData();

    // Reverse-engineered protocol decoders from zalo-for-linux (db-cross-v4 & zcall)
    Q_INVOKABLE QVariantMap parseBinNetPayload(const QByteArray& payload);
    Q_INVOKABLE bool extractZdbBackup(const QString& zdbPath, const QString& outputDir, const QString& privateKey);
    Q_INVOKABLE QVariantMap deriveSrtpKeys(const QString& sessId);

signals:
    // Bring the main window to the front (a notification was clicked); token = xdg-activation token or empty.
    void windowActivationRequested(const QString& activationToken);
    void syncPromptChanged();
    void groupMembersChanged(const QString& groupId);
    void quickMessagesChanged();
    void voicePlaybackChanged();
    void loginStateChanged();
    void restoringSessionChanged();
    void userProfileChanged();
    void offlineModeChanged();
    void loginStatusChanged();
    void qrCodeChanged();
    void qrTimerTick();
    void qrMatrixChanged();
    void qrLoginStepChanged();

    void currentConversationChanged();
    void appLanguageChanged();
    void connectionStateChanged(bool connected);
    void messageSent(const QString& text);
    void e2eeVerified(bool success, const QString& detail);
    void incomingCallStateChanged(bool active);
    void screenshotCaptured(const QString& uri);

    void settingsModalChanged();
    void userProfileModalChanged();
    void currentProfileChanged();
    void addFriendModalChanged();
    void createGroupModalChanged();
    void conversationInfoChanged();
    void forwardModalChanged();
    void createPollModalChanged();
    void pinnedMessageChanged();
    void mobileSyncStateChanged();
    void mobileSyncProgressChanged();
    void navigationTabChanged();
    void settingsChanged();
    void privacySettingsChanged();
    void soundSettingsChanged();
    void previewPlayingChanged();
    void historyLoadingChanged();
    void syncStateChanged();
    void userSearchFinished(bool ok, const QVariantMap& profile, const QString& message);
    void friendRequestFinished(bool ok, const QString& message);
    void groupCreateFinished(bool ok, const QString& message);
    // Transient, user-facing notice (already localized).
    void notice(const QString& message);
    void conversationStatusChanged();
    void attachmentStateChanged();
    // [{id, cateId, type, url}] for the sticker picker.
    void stickersFound(const QVariantList& stickers);
    void todosChanged();
    void replyTargetChanged();
    void scheduledMessagesChanged();

private:
    struct Profile {
        QString name;
        QString avatar;
        int memberCount{0};
    };

    void notifyIncomingCallDbus(const QString& callerName);
    void saveSession();
    void loadSession();
    void generateQrMatrix();
    QString L(const QString& vi, const QString& en) const { return m_appLanguage == "en" ? en : vi; }
    void setLoginStatus(const QString& message, bool isError);
    // Turns internal error codes from ZaloSession into a short localized reason.
    // `code` is the server error_code when known (see 01-business-domain error table).
    QString friendlyError(const QString& error, int code = 0) const;

    // Live session lifecycle
    void onSessionLoggedIn();
    void onSessionLoginFailed(const QString& reason);
    void onSessionLoginUnavailable(const QString& reason);
    void loadAccountData();
    // Recent threads + read points (get-last-msgs) so the sidebar is complete before any socket push.
    void preloadRecentThreads();
    // Cross-device sync (01-business-domain §sync): controls from phone/web patch local state, never write back.
    void handleSyncControl(const QString& actType, const QString& act, const QJsonValue& data);
    void handleGroupControl(const QString& act, const QJsonObject& data);
    void handleRemoteDeletes(const QJsonArray& entries, bool isGroup);
    void addSystemNotice(const QString& threadId, bool isGroup, const QString& text, qint64 ts);
    void loadAliases(int page = 1, QHash<QString, QString> collected = {});
    void applyAliases(const QHash<QString, QString>& complete);
    void applyGroupInfo(const QJsonObject& gridInfoMap);
    void callLogItem(const QJsonObject& obj, bool missed, Models::MessageItem* item, QString* label) const;
    void resolveGroupMembers(const QString& groupId);
    QHash<QString, QJsonArray> deliveredCandidates(const QJsonArray& msgs, bool isGroup) const;
    void sendDeliveredReceipts(const QJsonArray& msgs, bool isGroup);
    void loadUnreadMarks();
    void resyncAfterReconnect();
    QString displayNameOf(const QString& uid) const;
    // Offline cache (ZaloOfflineStore): fills models from the last session's data without the network.
    bool loadOfflineCache();
    void mergeCachedThread(const QString& convId);
    void persistMessage(const QString& convId, const QString& msgId);
    // Memory bound: only threads somebody looks at keep their messages in RAM; the rest live in the cache.
    bool cacheUsable() const;
    bool isThreadResident(const QString& convId) const;
    void touchResidentThread(const QString& convId);
    void evictColdThreads();
    void rememberMessageId(const QString& msgId);
    void releaseFreedMemory();
    void fetchBytes(const QString& url, std::function<void(const QByteArray& data, const QString& error)> done);
    void persistProfile(const QString& id, bool isGroup);
    void applyReadPoints(const QJsonArray& clearUnreads);
    bool isThreadMuted(const QString& threadId);
    void showMessageNotification(const QString& threadId, const QString& title, const QString& body);
    void onNotificationAction(const QString& key, const QString& action, const QString& activationToken);
    // Opens a thread by id (name/avatar from the conversation list), e.g. from a notification.
    void openConversationById(const QString& id);
    // Sends seen receipts for `convId` (no need to open it) and clears its unread badge.
    void markThreadRead(const QString& convId);
    void loadDrafts();
    void sendSeenFor(const QString& convId, bool isGroup);
    // Composer send path for any thread: local echo, conversation preview, then the API (or "failed" offline).
    void sendTextTo(const QString& convId, bool isGroup, const QString& cleanText,
                    const QVector<Models::MessageMention>& localMentions, const QJsonArray& mentions,
                    const QJsonObject& quoted, const QJsonArray& styles = QJsonArray());
    void sendTextNow(const QString& convId, bool isGroup, const QString& text, const QJsonArray& mentions,
                     const QJsonObject& quoted,
                     const QString& localId, const QString& cliMsgId, const QJsonArray& styles = QJsonArray());
    void loadGroupInfo(const QStringList& groupIds);
    // Groups without a photo show their first members 2×2 (Zalo composite avatar): fills
    // ConversationModel::memberAvatars and looks up the profiles of those members it does not know yet.
    void updateGroupComposite(const QString& groupId);
    void lookupCompositeMembers();
    void resolveUnknownUsers();
    void handleIncomingMessages(const QJsonArray& messages, bool isGroup, bool isOld);
    Models::MessageItem toMessageItem(const QJsonObject& msg, bool isGroup, QString* threadId, QString* preview) const;
    void ensureConversation(const QString& threadId, bool isGroup);
    void applyProfileToConversation(const QString& id);
    void rebuildContacts();
    void setSyncing(bool syncing);

    void handleReactions(const QJsonArray& reactions, bool isGroup);
    void refreshReactionSummary(const QString& convId, const QString& msgId);
    void handleTyping(const QString& threadId, const QString& userId, bool isGroup);
    void handleSeen(const QString& threadId, const QString& msgId, const QString& userId, bool isGroup);
    void sendSeenForCurrent();
    void refreshPresence();
    void loadLabels();
    void resolveSticker(const QString& convId, const QString& msgId, qint64 stickerId);
    QString threadOfMessage(const QString& msgId) const;
    bool isGroupThread(const QString& threadId) const;
    void handleMobileSyncControl(const QJsonObject& content);
    void setMobileSyncState(int state, const QString& status, int progress = 0);

    QHash<QString, QJsonObject> m_rawMessages;   // msgId -> raw TMessage (needed by reactions/recall/seen)
    QHash<QString, QString> m_messageThread;     // msgId -> thread id
    // msgId -> reaction events (one per tap), cache of offline.db reaction_events
    QHash<QString, QVector<Offline::ZaloOfflineStore::ReactionEvent>> m_reactions;
    QSet<QString> m_reactedIds;   // message ids with stored reactions (offline.db), loaded once per account
    bool m_reactedIdsLoaded{false};
    QVector<Offline::ZaloOfflineStore::ReactionEvent>& eventsOf(const QString& msgId); // RAM, else offline.db
    bool hasReactions(const QString& msgId);
    void addReactionEvent(const QString& msgId, const QString& uid, const QString& icon, const QString& eventId, qint64 ts);
    void dropPendingReaction(const QString& msgId, const QString& icon);
    void clearReactionsOf(const QString& msgId, const QString& uid);
    QSet<QString> m_onlineIds;
    QSet<QString> m_seenSent;
    QHash<QString, qint64> m_typingUntil;        // thread id -> epoch ms
    QHash<QString, QList<QPair<QString, QString>>> m_stickerWaiters; // sticker id -> (conv, msg)
    QStringList m_stickerQueue;     // sticker ids waiting for a detail request
    QHash<QString, int> m_stickerAttempts; // failed sticker_detail lookups (retried up to 3 times)
    int m_stickerInFlight{0};       // bounded so history imports never flood the API host
    bool m_userLookupInFlight{false};
    void pumpStickerQueue();
    qint64 m_currentLastOnline{0};
    qint64 m_lastTypingSent{0};
    int m_pendingAttachments{0};
    quint64 m_localSeq{0}; // makes local echo ids unique within a millisecond
    QTimer* m_presenceTimer{nullptr};
    QTimer* m_typingTimer{nullptr};
    QVector<Models::ContactItem> m_friendRequests;
    QVariantList m_todoItems;
    QString m_todoTab{"assigned"};
    bool m_loadingTodos{false};
    Platform::SoundEngine* m_sound{nullptr};
    Call::CallBridge* m_calls{nullptr};

    QHash<QString, Profile> m_users;   // uid -> profile (friends and resolved strangers)
    QHash<QString, Profile> m_groups;  // group id -> group profile
    QSet<QString> m_friendIds;
    QSet<QString> m_groupIds;
    QSet<QString> m_pendingUserLookups;
    QSet<QString> m_mutedIds;
    QSet<QString> m_historyLoaded;
    QString m_myUid;
    bool m_restoringSession{false};
    int m_restoreAttempts{0};
    qint64 m_lastClientId{0};
    QString m_cachedUid;            // uid of the last signed-in account (QSettings auth/uid)
    QSet<QString> m_cacheMerged;    // threads whose cached messages were merged into the model
    QStringList m_residentThreads;  // threads with messages in RAM, least recently used first
    QSet<QString> m_recentIds;      // recent message ids (dedupe for threads that are not in RAM)
    QStringList m_recentIdOrder;
    QTimer* m_trimTimer{nullptr};
    QHash<QString, qint64> m_muteUntil; // threadId -> epoch ms; 0 = until turned back on
    QSet<QString> m_markedUnread;
    struct GroupRoster {
        QStringList members; // uids, server order
        QSet<QString> admins;
        QString creator;
    };
    QHash<QString, GroupRoster> m_groupRosters;
    QSet<QString> m_memberLookupInFlight;   // group ids
    QSet<QString> m_compositeLookups;       // member uids wanted by composite group avatars
    QSet<QString> m_compositeAsked;         // asked once already (never loop on unknown ids)
    QSet<QString> m_unresolvedMembers;      // uids /api/social/group/members did not return
    QHash<qint64, qint64> m_pollFetchedAt;   // poll id -> ms of the last detail request
    QVariantList m_quickMessages;
    QMediaPlayer* m_voicePlayer{nullptr}; // created on first use
    QString m_playingVoiceId;
    QHash<QString, QString> m_aliases; // friend uid -> nickname set on any device (alias list)
    bool m_realtimeWasUp{false};
    QTimer* m_labelDebounce{nullptr};
    QTimer* m_profileRefreshTimer{nullptr};
    QSet<QString> m_historyExhausted;
    QVariantMap m_replyTarget;
    class MessageScheduler* m_scheduler{nullptr}; // scheduled sends (message_scheduler.h)
    Platform::DesktopNotifications* m_notifications{nullptr}; // one popup per thread ("thread:<id>") + "call"
    Media::VoiceRecorder* m_voiceRecorder{nullptr};
    Platform::ShortcutMap* m_shortcuts{nullptr};
    Platform::AppLock* m_appLock{nullptr};
    Platform::StorageManager* m_storage{nullptr};
    GroupAdmin* m_groupAdmin{nullptr};
    LabelManager* m_labelManager{nullptr};
    StickerStore* m_stickerStore{nullptr};
    PinBoard* m_pinBoard{nullptr};
    QString m_screenshotRequest;  // portal Request object path awaiting its Response
    QString m_screenshotTarget;   // conversation the screenshot goes to
    Models::RichComposer* m_composer{nullptr};
    bool m_showSyncPrompt{false};
    QString m_voiceTarget; // thread the current recording belongs to
    QTimer* m_restoreRetryTimer{nullptr};
    bool m_loadingHistory{false};
    bool m_syncing{false};
    int m_pendingSyncJobs{0};
    QTimer* m_userLookupTimer{nullptr};

    // Auth state
    bool m_isLoggedIn{false};
    bool m_isLoggingIn{false};
    bool m_hasLoginError{false};
    bool m_isOfflineMode{false};
    QString m_userName{""};
    QString m_userPhone{""};
    QString m_userAvatar{""};
    QString m_loginStatusMessage;
    QString m_qrImageUrl{""};
    QString m_qrCodeData{""};
    int m_qrSecondsRemaining{120};
    int m_qrSize{0};
    int m_qrLoginStep{0}; // 0: WaitingScan, 1: ScannedWaitingConfirm, 2: Confirmed
    QString m_scannedUserName;
    QString m_scannedUserAvatar;
    QVariantList m_qrMatrix;
    QTimer* m_qrTimer{nullptr};

    // Chat state
    QString m_currentConvId{""}; // Empty means welcome screen
    QString m_currentConvName;
    QString m_currentConvAvatar;
    bool m_isConnected{false};

    bool m_incomingCallActive{false};
    QString m_incomingCallerName;

    // Modals
    bool m_showSettingsModal{false};
    QString m_settingsInitialTab{"appearance"};
    bool m_showUserProfileModal{false};
    struct UserProfileData {
        QString userId;
        QString displayName;
        QString zaloName;
        QString avatar;
        QString cover;
        int gender{0}; // 0: Nam, 1: Nữ
        qint64 dob{0};
        QString sdob;
        QString status; // bio
        QString phoneNumber;
        bool isFriend{false};
        bool isBlocked{false};
    };
    UserProfileData m_myProfile;
    UserProfileData m_activeProfile;
    bool m_profileLoading{false};
    bool m_showAddFriendModal{false};
    bool m_showCreateGroupModal{false};
    bool m_showConversationInfo{false};
    bool m_showForwardModal{false};
    bool m_showCreatePollModal{false};
    QString m_forwardSourceMsgId;
    QString m_forwardSourcePreview;
    QString m_forwardMode{"message"};
    QString m_todoUnavailable;
    QString m_forwardCardUid;
    QString m_currentNavigationTab{"messages"};


    // Mobile E2EE Sync state
    int m_mobileSyncState{0};
    QString m_mobileSyncStatus;
    int m_mobileSyncProgress{0};
    Sync::MobileHistorySync* m_historySync{nullptr};

    // Settings
    bool m_notifyNewMessage{true};
    bool m_notifySound{false};
    bool m_antiPeeking{false};
    bool m_useAvatarAsChatBackground{false};
    bool m_startHiddenInTray{false};
    bool m_closeToTray{true};
    bool m_launchHidden{false};

    // ZaPrivacy
    bool m_blockTyping{false};
    bool m_blockDelivered{false};
    bool m_blockSeen{false};

    // ZaSounds
    QString m_selectedRingtone{"zalo"};
    QString m_selectedNotiSound{"pop"};
    bool m_isPreviewPlaying{false};
    QString m_currentPreviewSound{""};
    QTimer* m_previewTimer{nullptr};
    QString m_appLanguage{"vi"};

    Models::ConversationModel* m_conversationModel{nullptr};
    Models::MessageModel* m_messageModel{nullptr};
    Models::ContactModel* m_contactModel{nullptr};
    Call::ZCallEngine* m_callEngine{nullptr};
    Offline::ZaloOfflineStore* m_offlineStore{nullptr};
    ZaloApiClient* m_apiClient{nullptr};
};

} // namespace Zalos::Core::Network

