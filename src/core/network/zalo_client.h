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
#include "models/message_search_model.h"
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

    Q_PROPERTY(Zalos::Core::Call::ZCallEngine* callEngine READ callEngine CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Offline::ZaloOfflineStore* offlineStore READ offlineStore CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Network::ZaloApiClient* apiClient READ apiClient CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Call::CallBridge* calls READ calls CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Media::VoiceRecorder* voiceRecorder READ voiceRecorder CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Platform::ShortcutMap* shortcuts READ shortcuts CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Platform::AppLock* appLock READ appLock CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Platform::StorageManager* storage READ storage CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Network::GroupAdmin* groupAdmin READ groupAdmin CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Network::LabelManager* labels READ labels CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Network::StickerStore* stickers READ stickers CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Network::PinBoard* pins READ pins CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Models::MessageSearchModel* messageSearch READ messageSearch CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Models::MessageSearchModel* peopleSearch READ peopleSearch CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Models::MessageSearchModel* hiddenSearch READ hiddenSearch CONSTANT FINAL)
    Q_PROPERTY(bool hasHiddenPin READ hasHiddenPin NOTIFY hiddenPinChanged FINAL)
    Q_PROPERTY(bool hiddenUnread READ hiddenUnread NOTIFY hiddenUnreadChanged FINAL)
    Q_PROPERTY(Zalos::Core::Models::RichComposer* composer READ composer CONSTANT FINAL)
    Q_PROPERTY(bool showSyncPrompt READ showSyncPrompt NOTIFY syncPromptChanged FINAL)

    Q_PROPERTY(bool isLoggedIn READ isLoggedIn NOTIFY loginStateChanged FINAL)
    Q_PROPERTY(bool isRestoringSession READ isRestoringSession NOTIFY restoringSessionChanged FINAL)
    Q_PROPERTY(QString currentUserName READ currentUserName NOTIFY userProfileChanged FINAL)
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

    Q_PROPERTY(QString currentConversationId READ currentConversationId NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(QString currentConversationName READ currentConversationName NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(QString currentConversationAvatar READ currentConversationAvatar NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationIsGroup READ currentConversationIsGroup NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationIsCloud READ currentConversationIsCloud NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationIsOfficial READ currentConversationIsOfficial NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationCanCall READ currentConversationCanCall NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationCanCreateGroup READ currentConversationCanCreateGroup NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationCanShareContact READ currentConversationCanShareContact NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(QString composerLockReason READ composerLockReason NOTIFY composerStateChanged FINAL)
    Q_PROPERTY(bool composerLockDeletable READ composerLockDeletable NOTIFY composerStateChanged FINAL)
    Q_PROPERTY(bool currentConversationIsStranger READ currentConversationIsStranger NOTIFY composerStateChanged FINAL)
    Q_PROPERTY(int strangerRank READ strangerRank NOTIFY composerStateChanged FINAL)
    Q_PROPERTY(QString strangerBannerTitle READ strangerBannerTitle NOTIFY composerStateChanged FINAL)
    Q_PROPERTY(QString strangerBannerText READ strangerBannerText NOTIFY composerStateChanged FINAL)
    Q_PROPERTY(int currentConversationMemberCount READ currentConversationMemberCount NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationPinned READ currentConversationPinned NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationMuted READ currentConversationMuted NOTIFY currentConversationChanged FINAL)
    Q_PROPERTY(bool currentConversationIsOnline READ currentConversationIsOnline NOTIFY conversationStatusChanged FINAL)
    Q_PROPERTY(bool isLoadingHistory READ isLoadingHistory NOTIFY historyLoadingChanged FINAL)
    Q_PROPERTY(bool isSyncing READ isSyncing NOTIFY syncStateChanged FINAL)
    Q_PROPERTY(QString myCloudId READ myCloudId NOTIFY userProfileChanged FINAL)
    Q_PROPERTY(QString currentConversationStatus READ currentConversationStatus NOTIFY conversationStatusChanged FINAL)
    Q_PROPERTY(bool currentConversationHasPresence READ currentConversationHasPresence NOTIFY conversationStatusChanged FINAL)
    Q_PROPERTY(bool isSendingAttachment READ isSendingAttachment NOTIFY attachmentStateChanged FINAL)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStateChanged FINAL)
    Q_PROPERTY(int brokenMediaRevision READ brokenMediaRevision NOTIFY brokenMediaChanged FINAL)
    Q_PROPERTY(int stickerRevision READ stickerRevision NOTIFY stickerRevisionChanged FINAL)
    Q_PROPERTY(bool isE2EESupported READ isE2EESupported CONSTANT FINAL)
    Q_PROPERTY(bool isIncomingCallActive READ isIncomingCallActive NOTIFY incomingCallStateChanged FINAL)
    Q_PROPERTY(QString incomingCallerName READ incomingCallerName NOTIFY incomingCallStateChanged FINAL)
    Q_PROPERTY(Zalos::Core::Models::ConversationModel* conversationModel READ conversationModel CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Models::MessageModel* messageModel READ messageModel CONSTANT FINAL)
    Q_PROPERTY(Zalos::Core::Models::ContactModel* contactModel READ contactModel CONSTANT FINAL)
    Q_PROPERTY(QString appLanguage READ appLanguage WRITE setAppLanguage NOTIFY appLanguageChanged FINAL)

    Q_PROPERTY(bool showSettingsModal READ showSettingsModal WRITE setShowSettingsModal NOTIFY settingsModalChanged FINAL)
    Q_PROPERTY(QString settingsInitialTab READ settingsInitialTab NOTIFY settingsModalChanged FINAL)
    Q_PROPERTY(bool showUserProfileModal READ showUserProfileModal WRITE setShowUserProfileModal NOTIFY userProfileModalChanged FINAL)
    Q_PROPERTY(bool showAddFriendModal READ showAddFriendModal WRITE setShowAddFriendModal NOTIFY addFriendModalChanged FINAL)
    Q_PROPERTY(bool showCreateGroupModal READ showCreateGroupModal WRITE setShowCreateGroupModal NOTIFY createGroupModalChanged FINAL)
    Q_PROPERTY(bool showConversationInfo READ showConversationInfo WRITE setShowConversationInfo NOTIFY conversationInfoChanged FINAL)
    Q_PROPERTY(bool showForwardModal READ showForwardModal WRITE setShowForwardModal NOTIFY forwardModalChanged FINAL)
    Q_PROPERTY(QString forwardSourceMsgId READ forwardSourceMsgId NOTIFY forwardModalChanged FINAL)
    Q_PROPERTY(QString forwardSourcePreview READ forwardSourcePreview NOTIFY forwardModalChanged FINAL)
    Q_PROPERTY(QString forwardMode READ forwardMode NOTIFY forwardModalChanged FINAL)
    Q_PROPERTY(QString forwardCardUid READ forwardCardUid NOTIFY forwardModalChanged FINAL)
    Q_PROPERTY(bool showCreatePollModal READ showCreatePollModal WRITE setShowCreatePollModal NOTIFY createPollModalChanged FINAL)
    Q_PROPERTY(bool showBusinessToolsModal READ showBusinessToolsModal WRITE setShowBusinessToolsModal NOTIFY businessToolsModalChanged FINAL)
    Q_PROPERTY(QString businessTab READ businessTab WRITE setBusinessTab NOTIFY businessTabChanged FINAL)
    Q_PROPERTY(bool hasBusinessPackage READ hasBusinessPackage NOTIFY userProfileChanged FINAL)
    Q_PROPERTY(QString businessPackageName READ businessPackageName NOTIFY userProfileChanged FINAL)
    Q_PROPERTY(QString currentNavigationTab READ currentNavigationTab WRITE setCurrentNavigationTab NOTIFY navigationTabChanged FINAL)

    Q_PROPERTY(bool hasPinnedMessage READ hasPinnedMessage NOTIFY pinnedMessageChanged FINAL)
    Q_PROPERTY(QString currentPinnedMessageId READ currentPinnedMessageId NOTIFY pinnedMessageChanged FINAL)
    Q_PROPERTY(QString currentPinnedMessageText READ currentPinnedMessageText NOTIFY pinnedMessageChanged FINAL)
    Q_PROPERTY(QString currentPinnedMessageSender READ currentPinnedMessageSender NOTIFY pinnedMessageChanged FINAL)

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
    Q_PROPERTY(bool profileIsOfficial READ profileIsOfficial NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(bool profileIsBlocked READ profileIsBlocked NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(bool profileIsInactive READ profileIsInactive NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(bool currentConversationBlocked READ currentConversationBlocked NOTIFY composerStateChanged FINAL)
    Q_PROPERTY(bool profileLoading READ profileLoading NOTIFY currentProfileChanged FINAL)

    Q_PROPERTY(int mobileSyncState READ mobileSyncState NOTIFY mobileSyncStateChanged FINAL)
    Q_PROPERTY(QString mobileSyncStatus READ mobileSyncStatus NOTIFY mobileSyncStateChanged FINAL)
    Q_PROPERTY(int mobileSyncProgress READ mobileSyncProgress NOTIFY mobileSyncProgressChanged FINAL)

    Q_PROPERTY(QVariantMap replyTarget READ replyTarget NOTIFY replyTargetChanged FINAL)
    Q_PROPERTY(QVariantList scheduledMessages READ scheduledMessages NOTIFY scheduledMessagesChanged FINAL)
    Q_PROPERTY(bool historyExhausted READ historyExhausted NOTIFY historyLoadingChanged FINAL)
    Q_PROPERTY(bool notifyNewMessage READ notifyNewMessage WRITE setNotifyNewMessage NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool notifySound READ notifySound WRITE setNotifySound NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool antiPeeking READ antiPeeking WRITE setAntiPeeking NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString chatBackgroundMode READ chatBackgroundMode WRITE setChatBackgroundMode NOTIFY settingsChanged FINAL)
    Q_PROPERTY(qreal chatBackgroundOpacity READ chatBackgroundOpacity WRITE setChatBackgroundOpacity NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString chatBackgroundImage READ chatBackgroundImage NOTIFY chatBackgroundImageChanged FINAL)
    Q_PROPERTY(QString currentConversationBackground READ currentConversationBackground NOTIFY currentConversationBackgroundChanged FINAL)
    Q_PROPERTY(bool showLabelFolders READ showLabelFolders WRITE setShowLabelFolders NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool startHiddenInTray READ startHiddenInTray WRITE setStartHiddenInTray NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool startWithSystem READ startWithSystem WRITE setStartWithSystem NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT FINAL)
    Q_PROPERTY(bool launchHidden READ launchHidden CONSTANT FINAL)

    Q_PROPERTY(bool blockTyping READ blockTyping WRITE setBlockTyping NOTIFY privacySettingsChanged FINAL)
    Q_PROPERTY(bool blockDelivered READ blockDelivered WRITE setBlockDelivered NOTIFY privacySettingsChanged FINAL)
    Q_PROPERTY(bool blockSeen READ blockSeen WRITE setBlockSeen NOTIFY privacySettingsChanged FINAL)

    Q_PROPERTY(QString selectedRingtone READ selectedRingtone WRITE setSelectedRingtone NOTIFY soundSettingsChanged FINAL)
    Q_PROPERTY(QString selectedNotiSound READ selectedNotiSound WRITE setSelectedNotiSound NOTIFY soundSettingsChanged FINAL)
    Q_PROPERTY(bool isPreviewPlaying READ isPreviewPlaying NOTIFY previewPlayingChanged FINAL)
    Q_PROPERTY(QString currentPreviewSound READ currentPreviewSound NOTIFY previewPlayingChanged FINAL)

    Q_PROPERTY(QVariantList quickMessages READ quickMessages NOTIFY quickMessagesChanged FINAL)
    Q_PROPERTY(QString playingVoiceId READ playingVoiceId NOTIFY voicePlaybackChanged FINAL)
    Q_PROPERTY(qint64 voicePosition READ voicePosition NOTIFY voicePlaybackChanged FINAL)
    Q_PROPERTY(qint64 voiceDuration READ voiceDuration NOTIFY voicePlaybackChanged FINAL)
    Q_PROPERTY(qreal voicePlaybackRate READ voicePlaybackRate WRITE setVoicePlaybackRate NOTIFY voicePlaybackChanged FINAL)

    Q_PROPERTY(bool showOnlineStatus READ showOnlineStatus NOTIFY serverSettingsChanged FINAL)
    Q_PROPERTY(bool displaySeenStatus READ displaySeenStatus NOTIFY serverSettingsChanged FINAL)
    Q_PROPERTY(bool kikiAvailable READ kikiAvailable NOTIFY kikiStateChanged FINAL)
    Q_PROPERTY(QString kikiUrl READ kikiUrl NOTIFY kikiStateChanged FINAL)
    Q_PROPERTY(bool displayOnRecommendFriend READ displayOnRecommendFriend NOTIFY serverSettingsChanged FINAL)
    Q_PROPERTY(bool addFriendViaPhone READ addFriendViaPhone NOTIFY serverSettingsChanged FINAL)
    Q_PROPERTY(bool addFriendViaQr READ addFriendViaQr NOTIFY serverSettingsChanged FINAL)
    Q_PROPERTY(bool addFriendViaGroup READ addFriendViaGroup NOTIFY serverSettingsChanged FINAL)
    Q_PROPERTY(bool receiveStrangerMessage READ receiveStrangerMessage NOTIFY serverSettingsChanged FINAL)
    Q_PROPERTY(bool acceptStrangerCall READ acceptStrangerCall NOTIFY serverSettingsChanged FINAL)
    Q_PROPERTY(int viewBirthday READ viewBirthday NOTIFY serverSettingsChanged FINAL)
    Q_PROPERTY(QVariantList reminders READ reminders NOTIFY remindersChanged FINAL)
    Q_PROPERTY(bool isLoadingReminders READ isLoadingReminders NOTIFY remindersChanged FINAL)
    Q_PROPERTY(QVariantList sentFriendRequests READ sentFriendRequests NOTIFY sentFriendRequestsChanged FINAL)
    Q_PROPERTY(QVariantList myAvatarHistory READ myAvatarHistory NOTIFY myAvatarHistoryChanged FINAL)

public:
    QVariantList sentFriendRequests() const { return m_sentFriendRequests; }
    QVariantList myAvatarHistory() const { return m_myAvatarHistory; }
    static inline ZaloClient* s_instance = nullptr;
    explicit ZaloClient(QObject* parent = nullptr);

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

    QString currentConversationId() const { return m_currentConvId; }
    QString currentConversationName() const { return m_currentConvName; }
    QString currentConversationAvatar() const { return m_currentConvAvatar; }
    QString currentConversationBackground() const;
    bool currentConversationIsGroup() const;
    bool currentConversationIsCloud() const;
    bool currentConversationIsOfficial() const;
    bool currentConversationCanCall() const;
    bool currentConversationCanCreateGroup() const;
    bool currentConversationCanShareContact() const;
    bool isOfficialOrSystemAccount(const QString& id, const QString& name) const;
    QString composerLockReason() const;
    bool composerLockDeletable() const { return m_readOnlyThreads.contains(m_currentConvId); }
    bool currentConversationIsStranger() const;
    int strangerRank() const { return m_strangerRanks.value(m_currentConvId).rank; }
    QString strangerBannerTitle() const;
    QString strangerBannerText() const;
    bool isStrangerLocked(const QString& id) const;
    bool noteSendFailure(const QString& convId, int code, const QJsonValue& data);
    bool isThreadReadOnly(const QString& id) const { return m_readOnlyThreads.contains(id); }
    int currentConversationMemberCount() const;
    bool currentConversationPinned() const;
    bool currentConversationMuted() const;
    bool currentConversationIsOnline() const;
    bool isLoadingHistory() const { return m_loadingHistory; }
    bool isSyncing() const { return m_syncing; }
    QString myCloudId() const;
    QString currentConversationStatus() const;
    bool currentConversationHasPresence() const;
    bool isSendingAttachment() const { return m_pendingAttachments > 0; }
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
    Models::MessageSearchModel* messageSearch() const { return m_messageSearch; }
    Models::MessageSearchModel* peopleSearch() const { return m_peopleSearch; }
    Models::MessageSearchModel* hiddenSearch() const { return m_hiddenSearch; }
    bool hasHiddenPin() const { return !m_hiddenPinHash.isEmpty(); }
    bool hiddenUnread() const { return m_hiddenUnread; }
    static QString hiddenPinDigest(const QString& pin);
    static bool isReparsedOnLoad(const QString& msgType);
    static bool isGoneError(const QString& error);
    QString myUid() const { return m_myUid; }
    Models::RichComposer* composer() const { return m_composer; }
    bool showSyncPrompt() const { return m_showSyncPrompt; }
    Q_INVOKABLE void acceptSyncPrompt();
    Q_INVOKABLE void dismissSyncPrompt(bool permanently);
    void maybeOfferMobileSync();
    Q_INVOKABLE void selectAdjacentConversation(int delta);
    Q_INVOKABLE void openPinnedConversation(int n);
    void sendVoiceMessage(const QString& convId, const QString& filePath, int durationMs);
    Q_INVOKABLE void startVoiceCall();
    Q_INVOKABLE void startVideoCall();
    Q_INVOKABLE void startGroupCall(bool isVideo = false);
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
    static constexpr int kCacheParseVersion = 4;
    ZaloApiClient* apiClient() const { return m_apiClient; }

    bool showSettingsModal() const { return m_showSettingsModal; }
    void setShowSettingsModal(bool show);
    QString settingsInitialTab() const { return m_settingsInitialTab; }

    bool showUserProfileModal() const { return m_showUserProfileModal; }
    void setShowUserProfileModal(bool show);

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
    bool profileIsOfficial() const;
    static bool isOfficialAccountForTesting(const QJsonObject& user);
    bool isOfficialIdForTesting(const QString& id) const { return m_officialIds.contains(id); }
    bool profileIsBlocked() const;
    bool profileIsInactive() const;
    bool currentConversationBlocked() const;
    static QString undoTargetOf(const QJsonObject& msg);
    static QString backgroundOf(const QJsonObject& profile);
    void noteAccountFlagsForTesting(const QString& uid, const QJsonObject& profile) { noteAccountFlags(uid, profile); }
    int residentThreadCountForTesting() const { return int(m_residentThreads.size()); }
    int rawMessageCountForTesting() const { return int(m_rawMessages.size()); }
    Q_INVOKABLE void trimMemoryForBackground();
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
    Q_INVOKABLE QVariantList friendsForCard() const;
    Q_INVOKABLE void forwardMessage(const QString& targetThreadId, const QString& msgId);

    bool showCreatePollModal() const { return m_showCreatePollModal; }
    void setShowCreatePollModal(bool show);
    Q_INVOKABLE void openCreatePollModal();
    Q_INVOKABLE void closeCreatePollModal();
    Q_INVOKABLE void createPoll(const QString& groupId, const QString& question, const QStringList& options, bool allowMultiChoices);
    Q_INVOKABLE void votePoll(qint64 pollId, int optionId);

    Q_INVOKABLE void changeGroupName(const QString& groupId, const QString& newName);
    Q_INVOKABLE void addMemberToGroup(const QString& groupId, const QString& memberId);
    Q_INVOKABLE void removeMemberFromGroup(const QString& groupId, const QString& memberId);
    Q_INVOKABLE void leaveGroup(const QString& groupId, bool silent = false);

    Q_INVOKABLE bool tryPasteClipboardImage();
    Q_INVOKABLE QVariantList getClipboardAttachments();
    Q_INVOKABLE QVariantMap fileInfoForUrl(const QString& fileUrlOrPath);

    bool hasPinnedMessage() const;
    QString currentPinnedMessageId() const;
    QString currentPinnedMessageText() const;
    QString currentPinnedMessageSender() const;
    Q_INVOKABLE void pinMessage(const QString& msgId);
    Q_INVOKABLE void unpinMessage();

    Q_INVOKABLE void changeFriendAlias(const QString& friendId, const QString& newAlias);
    Q_INVOKABLE void sendContactCard(const QString& targetThreadId, const QString& contactId, const QString& contactName, const QString& phone = QString());

    int mobileSyncState() const { return m_mobileSyncState; }
    QString mobileSyncStatus() const { return m_mobileSyncStatus; }
    int mobileSyncProgress() const { return m_mobileSyncProgress; }
    Q_INVOKABLE void requestMobileSync();
    Q_INVOKABLE void cancelMobileSync();

    QString currentNavigationTab() const { return m_currentNavigationTab; }
    Q_INVOKABLE void setCurrentNavigationTab(const QString& tab);

    bool notifyNewMessage() const { return m_notifyNewMessage; }
    void setNotifyNewMessage(bool val);

    bool notifySound() const { return m_notifySound; }
    bool antiPeeking() const { return m_antiPeeking; }
    Q_INVOKABLE void setAntiPeeking(bool val);
    QVariantMap replyTarget() const { return m_replyTarget; }
    QVariantList scheduledMessages() const;
    bool historyExhausted() const { return m_historyExhausted.contains(m_currentConvId); }
    void setNotifySound(bool val);

    QString chatBackgroundMode() const { return m_chatBackgroundMode; }
    void setChatBackgroundMode(const QString& mode);
    qreal chatBackgroundOpacity() const { return m_chatBackgroundOpacity; }
    void setChatBackgroundOpacity(qreal opacity);

    QString chatBackgroundImage() const { return m_chatBackgroundImage; }
    Q_INVOKABLE void setChatBackgroundImage(const QString& urlOrPath);
    Q_INVOKABLE void clearChatBackgroundImage();
    bool showLabelFolders() const { return m_showLabelFolders; }
    void setShowLabelFolders(bool val);

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

    bool blockTyping() const { return m_blockTyping; }
    void setBlockTyping(bool val);

    bool blockDelivered() const { return m_blockDelivered; }
    void setBlockDelivered(bool val);

    bool blockSeen() const { return m_blockSeen; }
    void setBlockSeen(bool val);

    QString selectedRingtone() const { return m_selectedRingtone; }
    void setSelectedRingtone(const QString& val);

    QString selectedNotiSound() const { return m_selectedNotiSound; }
    void setSelectedNotiSound(const QString& val);

    bool isPreviewPlaying() const { return m_isPreviewPlaying; }
    QString currentPreviewSound() const { return m_currentPreviewSound; }

    Q_INVOKABLE void playSoundPreview(const QString& soundType, const QString& soundKey);
    Q_INVOKABLE void stopSoundPreview();

    Q_INVOKABLE void loginWithPhone(const QString& phone, const QString& password, bool remember = true);
    Q_INVOKABLE void syncFromBrowserSession();
    Q_INVOKABLE void cancelQrScan();
    Q_INVOKABLE void refreshQrCode();
    Q_INVOKABLE void clearLoginError();
    Q_INVOKABLE void logout();

#ifdef ZALOS_TESTING
    void applyAliasesForTesting(const QHash<QString, QString>& aliases) { applyAliases(aliases); }
    void applyGroupInfoForTesting(const QJsonObject& gridInfoMap) { applyGroupInfo(gridInfoMap); }
    Models::MessageItem toMessageItemForTesting(const QJsonObject& msg, bool isGroup) const {
        QString thread, preview;
        return toMessageItem(msg, isGroup, &thread, &preview);
    }
    QHash<QString, QJsonArray> deliveredCandidatesForTesting(const QJsonArray& msgs, bool isGroup) const {
        return deliveredCandidates(msgs, isGroup);
    }
    void setMyUidForTesting(const QString& uid) { m_myUid = uid; }
    void setHiddenThreadsForTesting(const QSet<QString>& ids) { m_hiddenThreads = ids; m_conversationModel->setHiddenThreads(ids); }
    void setHiddenPinHashForTesting(const QString& hash) { m_hiddenPinHash = hash; emit hiddenPinChanged(); }
    void handleGroupControlForTesting(const QString& act, const QJsonObject& data) { handleGroupControl(act, data); }
    void reloadReadOnlyForTesting() { loadReadOnlyThreads(); }
    void setFriendsForTesting(const QSet<QString>& ids) { m_friendIds = ids; m_friendsKnown = true; syncFriendContext(); }
    void setStrangerRankForTesting(const QString& id, const QJsonObject& data);
    QString friendlyErrorForTesting(const QString& error, int code) const { return friendlyError(error, code); }
    void purgeExpiredForTesting() { purgeExpiredMessages(); }
    void setUserForTesting(const QString& uid, const QString& name, const QString& avatar) {
        m_users[uid] = {name, avatar, 0};
        m_messageModel->refreshSenders();
    }
    void setGroupForTesting(const QString& gid, const QString& name, const QString& avatar) {
        m_groups[gid] = {name, avatar, 0};
        m_groupIds.insert(gid);
    }
    bool isUserLookupPendingForTesting(const QString& uid) const { return m_pendingUserLookups.contains(uid); }
    void handleSyncControlForTesting(const QString& actType, const QString& act, const QJsonValue& data) {
        handleSyncControl(actType, act, data);
    }
    void handleReactionsForTesting(const QJsonArray& reactions, bool isGroup) { handleReactions(reactions, isGroup); }
    Platform::DesktopNotifications* notificationsForTesting() const { return m_notifications; }
    void reloadDraftsForTesting() { loadDrafts(); }
    bool openOfflineCacheForTesting(const QString& uid) {
        m_cachedUid = uid;
        return loadOfflineCache();
    }
    void setLoggedInForTesting(bool loggedIn) {
        m_isLoggedIn = loggedIn;
        m_restoringSession = false;
        emit restoringSessionChanged();
        emit loginStateChanged();
    }
    void setOnlineIdsForTesting(const QSet<QString>& ids) {
        m_onlineIds = ids;
        m_conversationModel->setOnlineIds(ids);
        m_contactModel->setOnlineIds(ids);
        emit conversationStatusChanged();
    }
#endif

    Q_INVOKABLE QVariantList groupMembers(const QString& groupId);
    Q_INVOKABLE QString myGroupRole(const QString& groupId) const;
    Q_INVOKABLE void setGroupDeputy(const QString& groupId, const QString& memberId, bool add);
    Q_INVOKABLE void transferGroupOwner(const QString& groupId, const QString& memberId);
    Q_INVOKABLE void disperseGroup(const QString& groupId);
    Q_INVOKABLE void refreshPoll(qint64 pollId, bool force = false);
    Q_INVOKABLE void sendMessageWithMentions(const QString& text, const QVariantList& mentions);
    Q_INVOKABLE void sendComposedMessage(const QString& text, const QVariantList& mentions, const QVariantList& styles);
    QVariantList quickMessages() const { return m_quickMessages; }
    Q_INVOKABLE void loadQuickMessages();
    Q_INVOKABLE void createQuickMessage(const QString& keyword, const QString& title);
    Q_INVOKABLE void updateQuickMessage(qint64 id, const QString& keyword, const QString& title);
    Q_INVOKABLE void deleteQuickMessage(qint64 id);

    bool showOnlineStatus() const { return m_serverSettings.value("show_online_status", 1) != 0; }
    bool displaySeenStatus() const { return m_serverSettings.value("display_seen_status", 1) != 0; }
    bool displayOnRecommendFriend() const { return m_serverSettings.value("display_on_recommend_friend", 1) != 0; }
    bool addFriendViaPhone() const { return m_serverSettings.value("add_friend_via_phone", 1) != 0; }
    bool addFriendViaQr() const { return m_serverSettings.value("add_friend_via_qr", 1) != 0; }
    bool addFriendViaGroup() const { return m_serverSettings.value("add_friend_via_group", 1) != 0; }
    bool receiveStrangerMessage() const { return m_serverSettings.value("receive_message", 1) == 1; }
    bool acceptStrangerCall() const { return m_serverSettings.value("accept_stranger_call", 3) == 3; }
    int viewBirthday() const { return m_serverSettings.value("view_birthday", 1); }
    bool kikiAvailable() const;
    QString kikiUrl() const;
    Q_INVOKABLE void loadServerSettings();
    Q_INVOKABLE void setServerSetting(const QString& type, int value);

    QVariantList reminders() const { return m_reminders; }
    bool isLoadingReminders() const { return m_loadingReminders; }
    Q_INVOKABLE void loadReminders();
    Q_INVOKABLE void createReminder(const QString& title, qint64 startTimeMs, const QString& emoji, int repeat);
    Q_INVOKABLE void removeReminder(const QString& reminderId);
    Q_INVOKABLE void createNote(const QString& title);
    QString playingVoiceId() const { return m_playingVoiceId; }
    qint64 voicePosition() const;
    qint64 voiceDuration() const;
    qreal voicePlaybackRate() const { return m_voicePlaybackRate; }
    Q_INVOKABLE void toggleVoice(const QString& msgId, const QString& url);
    Q_INVOKABLE void seekVoice(qint64 positionMs);
    Q_INVOKABLE void setVoicePlaybackRate(qreal rate);
    Q_INVOKABLE bool isVoiceFailed(const QString& msgId) const { return m_voiceErrors.contains(msgId); }

    Q_INVOKABLE void openWelcomeScreen();
    Q_INVOKABLE void selectConversation(const QString& id, const QString& name, const QString& avatar);
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
    Q_INVOKABLE void updateMyStatus(const QString& status);
    Q_INVOKABLE void blockUser(const QString& userId);
    Q_INVOKABLE void unblockUser(const QString& userId);
    Q_INVOKABLE void removeFriend(const QString& userId);

    Q_INVOKABLE void openAddFriend();
    Q_INVOKABLE void closeAddFriend();
    Q_INVOKABLE void openCreateGroup();
    Q_INVOKABLE void closeCreateGroup();
    Q_INVOKABLE void openBusinessTools();
    Q_INVOKABLE void closeBusinessTools();
    Q_INVOKABLE void openBusinessCatalog();
    Q_INVOKABLE void openBusinessAutoReply();
    Q_INVOKABLE void openBusinessPackage();
    Q_INVOKABLE void sendProductToCurrentChat(const QString& name, const QString& price, const QString& description, const QString& imageUrl);
    bool showBusinessToolsModal() const { return m_showBusinessToolsModal; }
    QString businessTab() const { return m_businessTab; }
    bool hasBusinessPackage() const { return m_hasBusinessPackage; }
    QString businessPackageName() const { return m_businessPackageName; }
    void setBusinessTab(const QString& tab);
    void setShowBusinessToolsModal(bool show);
    Q_INVOKABLE void toggleConversationInfo();
    Q_INVOKABLE void findUserByPhone(const QString& phone);
    Q_INVOKABLE void findUserByUsername(const QString& username);
    Q_INVOKABLE void sendFriendRequest(const QString& uid, const QString& message);
    Q_INVOKABLE void createGroup(const QString& name, const QStringList& members);
    Q_INVOKABLE void openMyCloud();
    Q_INVOKABLE QVariantList cloudSharedItems(const QString& kind = QStringLiteral("all")) const;
    Q_INVOKABLE void loadOlderMessages();
    Q_INVOKABLE void refreshContacts();

    Q_INVOKABLE void sendFile(const QString& fileUrlOrPath, const QString& caption = QString());
    Q_INVOKABLE void searchStickers(const QString& keyword);
    Q_INVOKABLE void sendStickerItem(qint64 stickerId, qint64 cateId, int type);
    Q_INVOKABLE QString stickerThumb(const QString& stickerId) const;
    Q_INVOKABLE void prefetchStickers(const QVariantList& stickers);
    int stickerRevision() const { return m_stickerRevision; }
    Q_INVOKABLE void reactToMessage(const QString& msgId, const QString& reactionKey);
    Q_INVOKABLE void changeMyAvatar(const QString& fileUrlOrPath);
    Q_INVOKABLE void changeGroupAvatar(const QString& groupId, const QString& fileUrlOrPath);
    Q_INVOKABLE QVariantList reactionList() const;
    Q_INVOKABLE QVariantList systemSounds() const;
    Q_INVOKABLE bool canRecall(const QString& msgId) const;
    Q_INVOKABLE void recallMessage(const QString& msgId);
    Q_INVOKABLE void deleteMessageForMe(const QString& msgId);
    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE QStringList searchConversationMessages(const QString& query);
    Q_INVOKABLE bool revealMessage(const QString& msgId);
    Q_INVOKABLE void openMessageFromSearch(const QString& convId, const QString& msgId);
    Q_INVOKABLE void openSearchResult(const QString& id, const QString& name, const QString& avatar, bool isGroup);
    Q_INVOKABLE void markMediaBroken(const QString& url);
    Q_INVOKABLE bool isMediaBroken(const QString& url) const { return !url.isEmpty() && m_brokenMedia.contains(url); }
    Q_INVOKABLE void retryMedia(const QString& url);
    int brokenMediaRevision() const { return m_brokenMediaRevision; }
    Q_INVOKABLE QVariantMap reactionDetails(const QString& msgId) const;
    Q_INVOKABLE void removeMyReaction(const QString& msgId);
    Q_INVOKABLE QString displayNameOf(const QString& uid) const;
    Q_INVOKABLE QString avatarOf(const QString& uid) const;
    Q_INVOKABLE void showNotice(const QString& text) { emit notice(text); }
    Q_INVOKABLE void openLink(const QString& url, const QString& title = QString(), const QString& thumb = QString());
    Q_INVOKABLE QVariantMap qrModules(const QString& text) const;
    static bool isAppOnlyLink(const QString& url);
    Q_INVOKABLE QVariantList groupsForInvite(const QString& userId) const;
    Q_INVOKABLE QVariantList groupComposite(const QString& groupId, int revision = 0) const {
        Q_UNUSED(revision);
        return m_conversationModel->memberAvatars(groupId);
    }
    Q_INVOKABLE void saveMediaTo(const QString& url, const QString& destinationFileUrl);
    Q_INVOKABLE void copyImage(const QString& url);
    Q_INVOKABLE void openFileMessage(const QString& url, const QString& fileName);
    Q_INVOKABLE QString suggestedMediaFileName(const QString& url) const;
    Q_INVOKABLE void notifyTyping();
    Q_INVOKABLE void acceptFriendRequest(const QString& uid);
    Q_INVOKABLE void rejectFriendRequest(const QString& uid);
    Q_INVOKABLE void refreshFriendRequests();
    Q_INVOKABLE void refreshSentFriendRequests();
    Q_INVOKABLE void cancelSentFriendRequest(const QString& uid);
    Q_INVOKABLE void forwardMessageToMany(const QStringList& targetThreadIds, const QString& msgId);
    Q_INVOKABLE void loadMyAvatarHistory();
    Q_INVOKABLE void reuseMyAvatar(const QString& photoId);
    Q_INVOKABLE void deleteMyAvatar(const QString& photoId);

    Q_INVOKABLE void pinConversation(const QString& id);
    Q_INVOKABLE void muteConversation(const QString& id);
    Q_INVOKABLE void muteConversationFor(const QString& id, int seconds);
    Q_INVOKABLE void toggleUnreadMark(const QString& id);
    Q_INVOKABLE bool isMarkedUnread(const QString& id) const { return m_markedUnread.contains(id); }
    Q_INVOKABLE void deleteConversation(const QString& id);
    Q_INVOKABLE void hideConversation(const QString& id, bool hide);
    Q_INVOKABLE bool isConversationHidden(const QString& id) const { return m_hiddenThreads.contains(id); }
    Q_INVOKABLE void archiveConversation(const QString& id, bool archive);
    Q_INVOKABLE bool verifyHiddenPin(const QString& pin) const;
    Q_INVOKABLE void setHiddenPin(const QString& pin, const QString& thenHideId = QString());
    Q_INVOKABLE void resetHiddenPin();
    Q_INVOKABLE bool canHideConversation(const QString& id) const;
    Q_INVOKABLE bool needsHiddenPin(const QString& id) const;
    Q_INVOKABLE void openHiddenConversation(const QString& id, const QString& msgId = QString());
    Q_INVOKABLE void requestHiddenFlow(const QString& flow, const QString& id = QString(), bool bypass = false);
    Q_INVOKABLE void dismissHiddenUnread();
    Q_INVOKABLE bool isConversationArchived(const QString& id) const { return m_archivedThreads.contains(id); }
    Q_INVOKABLE void setConversationAutoDelete(const QString& id, qint64 ttl);
    Q_INVOKABLE qint64 conversationAutoDelete(const QString& id) const { return m_autoDeleteTtl.value(id, 0); }

    Q_INVOKABLE void setReplyTarget(const QString& msgId);
    Q_INVOKABLE void clearReplyTarget();

    Q_INVOKABLE bool scheduleMessage(const QString& text, qint64 sendAtMs);
    Q_INVOKABLE void cancelScheduledMessage(const QString& id);

#ifdef ZALOS_TESTING
    void triggerSimulatedIncomingCall(const QString& callerName = "Người gọi Zalo");
#endif
    Q_INVOKABLE void answerCall(bool withCamera = true);
    Q_INVOKABLE void declineCall();
    Q_INVOKABLE void requestScreenshot(bool hideWindow = false);
    Q_INVOKABLE void printMedia(const QString& url);
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height, bool maximized);
    Q_INVOKABLE void notifyWindowVisibility(bool visible);
    Q_SLOT void onScreenshotResponse(uint response, const QVariantMap& results);

    Q_INVOKABLE QVariantMap getDiskUsage(const QString& path = QString());
    Q_INVOKABLE QVariantMap getDirectorySize(const QString& path);
    Q_INVOKABLE bool generateThumbnail(const QString& inputPath, const QString& outputPath, int width = 256, int height = 256);
    Q_INVOKABLE bool extractVideoThumbnail(const QString& videoPath, const QString& outputPath);
    Q_INVOKABLE QString recognizeImageText(const QString& imagePath);
    Q_INVOKABLE void syncOfflineData();

    Q_INVOKABLE QVariantMap parseBinNetPayload(const QByteArray& payload);
    Q_INVOKABLE bool extractZdbBackup(const QString& zdbPath, const QString& outputDir, const QString& privateKey);
    Q_INVOKABLE QVariantMap deriveSrtpKeys(const QString& sessId);

signals:
    void windowActivationRequested(const QString& activationToken);
    void syncPromptChanged();
    void groupMembersChanged(const QString& groupId);
    void quickMessagesChanged();
    void serverSettingsChanged();
    void kikiStateChanged();
    void sentFriendRequestsChanged();
    void myAvatarHistoryChanged();
    void conversationFlagsChanged();
    void hiddenPinChanged();
    void hiddenUnreadChanged();
    void hiddenOpFinished(const QString& op, bool ok);
    void hiddenFlowRequested(const QString& flow, const QString& convId, const QString& convName, bool bypass);
    void remindersChanged();
    void voicePlaybackChanged();
    void loginStateChanged();
    void restoringSessionChanged();
    void userProfileChanged();
    void offlineModeChanged();
    void loginStatusChanged();
    void qrCodeChanged();
    void qrTimerTick();
    void qrMatrixChanged();
    void appOnlyLinkRequested(const QString& url, const QString& title, const QString& thumb);
    void qrLoginStepChanged();

    void currentConversationChanged();
    void composerStateChanged();
    void appLanguageChanged();
    void connectionStateChanged(bool connected);
    void stickerRevisionChanged();
    void brokenMediaChanged();
    void revealRequested(const QString& msgId);
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
    void businessToolsModalChanged();
    void businessTabChanged();
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
    void notice(const QString& message);
    void conversationStatusChanged();
    void currentConversationBackgroundChanged();
    void chatBackgroundImageChanged();
    void attachmentStateChanged();
    void stickersFound(const QVariantList& stickers);
    void replyTargetChanged();
    void scheduledMessagesChanged();
    void requestHideForScreenshot();
    void requestRestoreAfterScreenshot();

private:
    struct Profile {
        QString name;
        QString avatar;
        int memberCount{0};
    };

    void notifyIncomingCallDbus(const QString& callerName);
    void executeScreenshotCapture();
    void saveSession();
    void loadSession();
    void generateQrMatrix();
    QString L(const QString& vi, const QString& en) const { return m_appLanguage == "en" ? en : vi; }
    void noticeCallUnavailable(const QString& reason);
    void setLoginStatus(const QString& message, bool isError);
    QString friendlyError(const QString& error, int code = 0) const;

    void onSessionLoggedIn();
    void onSessionLoginFailed(const QString& reason);
    void onSessionLoginUnavailable(const QString& reason);
    void loadAccountData();
    void preloadRecentThreads();
    void handleSyncControl(const QString& actType, const QString& act, const QJsonValue& data);
    void handleGroupControl(const QString& act, const QJsonObject& data);
    void markThreadReadOnly(const QString& id, const QString& reason, qint64 ts);
    void purgeExpiredMessages();
    void migrateCachedParses();
    void noteAccountFlags(const QString& uid, const QJsonObject& profile);
    void openRemoteFile(const QString& url, const QString& fileName, const QString& goneText);
    void queueUnknownSenders(int first, int last);
    void scheduleExpiry();
    void loadReadOnlyThreads();
    void saveReadOnlyThreads() const;
    void saveStrangerLocks() const;
    void scheduleStrangerUnlock();
    void refreshStrangerRank(const QString& convId);
    void syncFriendContext();
    QString ttlText(qint64 ttlMs) const;
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
    bool loadOfflineCache();
    void mergeCachedThread(const QString& convId);
    void mergeCacheRows(const QString& convId, QVector<Models::MessageItem> rows, const QHash<QString, QJsonObject>& raws);
    bool loadOlderFromCache(const QString& convId);
    static constexpr int kCachePageRows = 200;
    QJsonObject rawMessage(const QString& msgId) const;
    void persistMessage(const QString& convId, const QString& msgId);
    bool cacheUsable() const;
    bool isThreadResident(const QString& convId) const;
    void touchResidentThread(const QString& convId);
    void evictColdThreads(int maxResident = 12);
    void rememberMessageId(const QString& msgId);
    void releaseFreedMemory();
    void startIdleMemoryTrim();
    void fetchBytes(const QString& url, std::function<void(const QByteArray& data, const QString& error)> done);
    void applyStrangerRank(const QString& id, const QJsonObject& data);
    void persistProfile(const QString& id, bool isGroup);
    void applyReadPoints(const QJsonArray& clearUnreads);
    bool isThreadMuted(const QString& threadId);
    void showMessageNotification(const QString& threadId, const QString& title, const QString& body);
    void onNotificationAction(const QString& key, const QString& action, const QString& activationToken);
    void openConversationById(const QString& id);
    void markThreadRead(const QString& convId);
    void loadDrafts();
    void sendSeenFor(const QString& convId, bool isGroup);
    void sendTextTo(const QString& convId, bool isGroup, const QString& cleanText,
                    const QVector<Models::MessageMention>& localMentions, const QJsonArray& mentions,
                    const QJsonObject& quoted, const QJsonArray& styles = QJsonArray());
    void sendTextNow(const QString& convId, bool isGroup, const QString& text, const QJsonArray& mentions,
                     const QJsonObject& quoted,
                     const QString& localId, const QString& cliMsgId, const QJsonArray& styles = QJsonArray());
    void loadGroupInfo(const QStringList& groupIds);
    void updateGroupComposite(const QString& groupId);
    void lookupCompositeMembers();
    void resolveUnknownUsers();
    void handleIncomingMessages(const QJsonArray& messages, bool isGroup, bool isOld);
    Models::MessageItem toMessageItem(const QJsonObject& msg, bool isGroup, QString* threadId, QString* preview) const;
    QString fillHighlights(const QString& text, const QJsonArray& highlights) const;
    static bool isQuietMessageType(const QString& msgType);
    void ensureConversation(const QString& threadId, bool isGroup);
    void applyProfileToConversation(const QString& id);
    void rebuildContacts();
    void setSyncing(bool syncing);

    void handleReactions(const QJsonArray& reactions, bool isGroup);
    void refreshReactionSummary(const QString& convId, const QString& msgId);
    void handleTyping(const QString& threadId, const QString& userId, bool isGroup);
    void handleSeen(const QString& threadId, const QString& msgId, const QString& userId, bool isGroup);
    void handleDeliveredReceipt(const QString& threadId, const QString& msgId, bool isGroup);
    void sendSeenForCurrent();
    void refreshPresence();
    void loadLabels();
    void loadHiddenConversations();
    void applyHiddenSet(const QSet<QString>& ids);
    void dropHiddenConversationsLocally(const QSet<QString>& ids);
    void markHiddenUnread(qint64 ts);
    void loadArchivedChats();
    void loadAutoDeleteSettings();
    void resolveSticker(const QString& convId, const QString& msgId, qint64 stickerId);
    QString threadOfMessage(const QString& msgId) const;
    bool isGroupThread(const QString& threadId) const;
    void handleMobileSyncControl(const QJsonObject& content);
    void setMobileSyncState(int state, const QString& status, int progress = 0);

    QHash<QString, QJsonObject> m_rawMessages;
    QHash<QString, QString> m_messageThread;
    QHash<QString, QVector<Offline::ZaloOfflineStore::ReactionEvent>> m_reactions;
    QSet<QString> m_reactedIds;
    bool m_reactedIdsLoaded{false};
    QVector<Offline::ZaloOfflineStore::ReactionEvent>& eventsOf(const QString& msgId);
    bool hasReactions(const QString& msgId);
    void addReactionEvent(const QString& msgId, const QString& uid, const QString& icon, const QString& eventId, qint64 ts);
    void dropPendingReaction(const QString& msgId, const QString& icon);
    void clearReactionsOf(const QString& msgId, const QString& uid, qint64 clearTs = 0);
    QSet<QString> m_brokenMedia;
    int m_brokenMediaRevision{0};
    QSet<QString> m_onlineIds;
    QSet<QString> m_officialIds;
    QSet<QString> m_inactiveIds;
    QSet<QString> m_blockedIds;
    static constexpr qint64 kMinPlausibleEpochMs = 946684800000LL;
    QSet<QString> m_seenSent;
    QHash<QString, qint64> m_typingUntil;
    QHash<QString, QString> m_typingUser;
    QHash<QString, QList<QPair<QString, QString>>> m_stickerWaiters;
    QStringList m_stickerQueue;
    QHash<QString, int> m_stickerAttempts;
    int m_stickerInFlight{0};
    int m_stickerRevision{0};
    QSet<QString> m_stickerPrefetching;
    bool m_userLookupInFlight{false};
    void pumpStickerQueue();
    qint64 m_currentLastOnline{0};
    qint64 m_lastTypingSent{0};
    int m_pendingAttachments{0};
    quint64 m_localSeq{0};
    QTimer* m_presenceTimer{nullptr};
    QTimer* m_typingTimer{nullptr};
    QVector<Models::ContactItem> m_friendRequests;
    QVariantList m_sentFriendRequests;
    QVariantList m_myAvatarHistory;
    Platform::SoundEngine* m_sound{nullptr};
    Call::CallBridge* m_calls{nullptr};

    QHash<QString, Profile> m_users;
    QHash<QString, Profile> m_groups;
    QSet<QString> m_friendIds;
    QSet<QString> m_groupIds;
    QSet<QString> m_pendingUserLookups;
    QSet<QString> m_mutedIds;
    QSet<QString> m_hiddenThreads;
    QHash<QString, QString> m_readOnlyThreads;
    struct StrangerRank {
        int rank{0};
        QString title;
        QString text;
    };
    QHash<QString, StrangerRank> m_strangerRanks;
    QHash<QString, qint64> m_strangerLocks;
    QTimer* m_strangerUnlockTimer{nullptr};
    bool m_friendsKnown{false};
    QString m_hiddenPinHash;
    QString m_hiddenUnlockedId;
    bool m_hiddenUnread{false};
    qint64 m_hiddenDismissedAt{0};
    QTimer* m_hiddenRefreshTimer{nullptr};
    QTimer* m_expiryTimer{nullptr};
    QSet<QString> m_archivedThreads;
    QHash<QString, qint64> m_autoDeleteTtl;
    QSet<QString> m_historyLoaded;
    QString m_myUid;
    bool m_restoringSession{false};
    int m_restoreAttempts{0};
    qint64 m_lastClientId{0};
    QString m_cachedUid;
    QSet<QString> m_cacheMerged;
    QStringList m_residentThreads;
    QSet<QString> m_recentIds;
    QStringList m_recentIdOrder;
    QTimer* m_trimTimer{nullptr};
    QTimer* m_idleTrimTimer{nullptr};
    QHash<QString, qint64> m_muteUntil;
    QSet<QString> m_markedUnread;
    struct GroupRoster {
        QStringList members;
        QSet<QString> admins;
        QString creator;
    };
    QHash<QString, GroupRoster> m_groupRosters;
    QSet<QString> m_memberLookupInFlight;
    QSet<QString> m_compositeLookups;
    QSet<QString> m_compositeAsked;
    QSet<QString> m_unresolvedMembers;
    QHash<qint64, qint64> m_pollFetchedAt;
    QVariantList m_quickMessages;
    QHash<QString, int> m_serverSettings;
    QVariantList m_reminders;
    bool m_loadingReminders{false};
    QMediaPlayer* m_voicePlayer{nullptr};
    QString m_playingVoiceId;
    qreal m_voicePlaybackRate{1.0};
    QSet<QString> m_voiceErrors;
    QHash<QString, QString> m_aliases;
    bool m_realtimeWasUp{false};
    QTimer* m_labelDebounce{nullptr};
    QTimer* m_profileRefreshTimer{nullptr};
    QSet<QString> m_historyExhausted;
    QVariantMap m_replyTarget;
    class MessageScheduler* m_scheduler{nullptr};
    Platform::DesktopNotifications* m_notifications{nullptr};
    Media::VoiceRecorder* m_voiceRecorder{nullptr};
    Platform::ShortcutMap* m_shortcuts{nullptr};
    Platform::AppLock* m_appLock{nullptr};
    Platform::StorageManager* m_storage{nullptr};
    GroupAdmin* m_groupAdmin{nullptr};
    LabelManager* m_labelManager{nullptr};
    StickerStore* m_stickerStore{nullptr};
    PinBoard* m_pinBoard{nullptr};
    Models::MessageSearchModel* m_messageSearch{nullptr};
    Models::MessageSearchModel* m_peopleSearch{nullptr};
    Models::MessageSearchModel* m_hiddenSearch{nullptr};
    QString m_screenshotRequest;
    QString m_screenshotTarget;
    bool m_showBusinessToolsModal{false};
    QString m_businessTab{QStringLiteral("profile")};
    bool m_hasBusinessPackage{false};
    QString m_businessPackageName;
    Models::RichComposer* m_composer{nullptr};
    bool m_showSyncPrompt{false};
    QString m_voiceTarget;
    QTimer* m_restoreRetryTimer{nullptr};
    bool m_loadingHistory{false};
    bool m_syncing{false};
    int m_pendingSyncJobs{0};
    QTimer* m_userLookupTimer{nullptr};

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
    int m_qrLoginStep{0};
    QString m_scannedUserName;
    QString m_scannedUserAvatar;
    QVariantList m_qrMatrix;
    QTimer* m_qrTimer{nullptr};

    QString m_currentConvId{""};
    QString m_currentConvName;
    QString m_currentConvAvatar;
    bool m_isConnected{false};

    bool m_incomingCallActive{false};
    QString m_incomingCallerName;

    bool m_showSettingsModal{false};
    QString m_settingsInitialTab{"appearance"};
    bool m_showUserProfileModal{false};
    struct UserProfileData {
        QString userId;
        QString displayName;
        QString zaloName;
        QString avatar;
        QString cover;
        int gender{0};
        qint64 dob{0};
        QString sdob;
        QString status;
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
    QString m_forwardCardUid;
    QString m_currentNavigationTab{"messages"};

    int m_mobileSyncState{0};
    QString m_mobileSyncStatus;
    int m_mobileSyncProgress{0};
    Sync::MobileHistorySync* m_historySync{nullptr};

    bool m_notifyNewMessage{true};
    bool m_notifySound{false};
    bool m_antiPeeking{false};
    QString m_chatBackgroundMode{QStringLiteral("none")};
    qreal m_chatBackgroundOpacity{0.15};
    QString m_chatBackgroundImage;
    bool m_showLabelFolders{false};
    bool m_startHiddenInTray{false};
    bool m_closeToTray{true};
    bool m_launchHidden{false};

    bool m_blockTyping{false};
    bool m_blockDelivered{false};
    bool m_blockSeen{false};

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

}
