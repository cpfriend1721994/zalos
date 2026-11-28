#pragma once

#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <functional>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <memory>

class QMediaPlayer;
class QNetworkAccessManager;

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
#include "models/emoji_catalog.h"
#include "platform/sound_engine.h"

#include "media/voice_recorder.h"
#include "platform/app_lock.h"
#include "platform/shortcut_map.h"
#include "platform/storage_manager.h"

namespace Zalos::Core::Platform { class DesktopNotifications; class SystemTrayController; }

namespace Zalos::Core::Sync { class MobileHistorySync; }

namespace Zalos::Core::Network {

constexpr int kMaxTrackedIds = 2000;

struct BoundedUnit {};

template <typename Key, typename T>
class BoundedHash {
public:
    using const_iterator = typename QHash<Key, T>::const_iterator;
    using iterator = typename QHash<Key, T>::iterator;

    qsizetype size() const { return m_map.size(); }
    bool isEmpty() const { return m_map.isEmpty(); }
    bool contains(const Key& key) const { return m_map.contains(key); }
    T value(const Key& key) const { return m_map.value(key); }
    T value(const Key& key, const T& fallback) const { return m_map.value(key, fallback); }
    QList<Key> keys() const { return m_order; }

    void insert(const Key& key, const T& mapped = T()) {
        if (m_map.contains(key)) {
            m_map.insert(key, mapped);
            return;
        }
        if (m_map.size() >= kMaxTrackedIds) removeOldest();
        m_map.insert(key, mapped);
        m_order.append(key);
    }

    T& operator[](const Key& key) {
        if (!m_map.contains(key)) insert(key);
        return m_map[key];
    }

    qsizetype remove(const Key& key) {
        if (!m_map.contains(key)) return 0;
        m_order.removeOne(key);
        return m_map.remove(key);
    }

    T take(const Key& key) {
        if (!m_map.contains(key)) return T();
        m_order.removeOne(key);
        return m_map.take(key);
    }

    void clear() {
        m_map.clear();
        m_order.clear();
    }

    void removeOldest() {
        if (m_order.isEmpty()) return;
        m_map.remove(m_order.takeFirst());
    }

    const_iterator constFind(const Key& key) const { return m_map.constFind(key); }
    iterator find(const Key& key) { return m_map.find(key); }
    const_iterator cbegin() const { return m_map.cbegin(); }
    const_iterator cend() const { return m_map.cend(); }
    const_iterator begin() const { return m_map.cbegin(); }
    const_iterator end() const { return m_map.cend(); }
    iterator begin() { return m_map.begin(); }
    iterator end() { return m_map.end(); }

private:
    QHash<Key, T> m_map;
    QList<Key> m_order;
};

template <typename Key>
using BoundedSet = BoundedHash<Key, BoundedUnit>;

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
    Q_PROPERTY(Zalos::Core::Models::EmojiCatalog* emojis READ emojis CONSTANT FINAL)
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
    Q_PROPERTY(bool currentConversationIsTyping READ currentConversationIsTyping NOTIFY conversationStatusChanged FINAL)
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
    Q_PROPERTY(qint64 pollDialogId READ pollDialogId NOTIFY pollDialogChanged FINAL)
    Q_PROPERTY(QString pollDialogGroupId READ pollDialogGroupId NOTIFY pollDialogChanged FINAL)
    Q_PROPERTY(bool pollDialogVoters READ pollDialogVoters NOTIFY pollDialogChanged FINAL)
    Q_PROPERTY(int pollRevision READ pollRevision NOTIFY pollRevisionChanged FINAL)
    Q_PROPERTY(bool showBusinessToolsModal READ showBusinessToolsModal WRITE setShowBusinessToolsModal NOTIFY businessToolsModalChanged FINAL)
    Q_PROPERTY(QString businessTab READ businessTab WRITE setBusinessTab NOTIFY businessTabChanged FINAL)
    Q_PROPERTY(bool hasBusinessPackage READ hasBusinessPackage NOTIFY userProfileChanged FINAL)
    Q_PROPERTY(QString businessPackageName READ businessPackageName NOTIFY userProfileChanged FINAL)
    Q_PROPERTY(bool businessLoading READ businessLoading NOTIFY businessDataChanged FINAL)
    Q_PROPERTY(QVariantMap businessProfile READ businessProfile NOTIFY businessDataChanged FINAL)
    Q_PROPERTY(QVariantList businessCatalogs READ businessCatalogs NOTIFY businessDataChanged FINAL)
    Q_PROPERTY(QString selectedCatalogId READ selectedCatalogId NOTIFY businessDataChanged FINAL)
    Q_PROPERTY(QVariantList businessProducts READ businessProducts NOTIFY businessDataChanged FINAL)
    Q_PROPERTY(QVariantList businessAutoReplies READ businessAutoReplies NOTIFY businessDataChanged FINAL)
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
    Q_PROPERTY(bool notifyReactions READ notifyReactions WRITE setNotifyReactions NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QStringList currentUnreadReactionIds READ currentUnreadReactionIds NOTIFY unreadReactionsChanged FINAL)
    Q_PROPERTY(int currentUnreadReactionCount READ currentUnreadReactionCount NOTIFY unreadReactionsChanged FINAL)
    Q_PROPERTY(QString currentUnreadReactionIcon READ currentUnreadReactionIcon NOTIFY unreadReactionsChanged FINAL)
    Q_PROPERTY(bool notifySound READ notifySound WRITE setNotifySound NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool antiPeeking READ antiPeeking WRITE setAntiPeeking NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString defaultReaction READ defaultReaction WRITE setDefaultReaction NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString defaultReactionEmoji READ defaultReactionEmoji NOTIFY settingsChanged FINAL)
    Q_PROPERTY(int interfaceScale READ interfaceScale WRITE setInterfaceScale NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool interfaceScaleRestartRequired READ interfaceScaleRestartRequired NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString chatBackgroundMode READ chatBackgroundMode WRITE setChatBackgroundMode NOTIFY settingsChanged FINAL)
    Q_PROPERTY(qreal chatBackgroundOpacity READ chatBackgroundOpacity WRITE setChatBackgroundOpacity NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString chatBackgroundImage READ chatBackgroundImage NOTIFY chatBackgroundImageChanged FINAL)
    Q_PROPERTY(QString currentConversationBackground READ currentConversationBackground NOTIFY currentConversationBackgroundChanged FINAL)
    Q_PROPERTY(bool showLabelFolders READ showLabelFolders WRITE setShowLabelFolders NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool showBusinessButton READ showBusinessButton WRITE setShowBusinessButton NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool showKikiButton READ showKikiButton WRITE setShowKikiButton NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool hardwareAcceleration READ hardwareAcceleration WRITE setHardwareAcceleration NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool hardwareAccelerationRestartRequired READ hardwareAccelerationRestartRequired NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool hardwareAccelerationFailed READ hardwareAccelerationFailed NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT FINAL)
    Q_PROPERTY(QString qtRuntimeVersion READ qtRuntimeVersion CONSTANT FINAL)
    Q_PROPERTY(QString rendererBackend READ rendererBackend NOTIFY appLanguageChanged FINAL)
    Q_PROPERTY(bool softwareRenderer READ softwareRenderer CONSTANT FINAL)
    Q_PROPERTY(bool multimediaAvailable READ multimediaAvailable CONSTANT FINAL)
    Q_PROPERTY(bool effectsAvailable READ effectsAvailable CONSTANT FINAL)
    Q_PROPERTY(QString platformName READ platformName CONSTANT FINAL)
    Q_PROPERTY(bool checkingUpdate READ checkingUpdate NOTIFY updateCheckChanged FINAL)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateCheckChanged FINAL)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateCheckChanged FINAL)
    Q_PROPERTY(QString updateUrl READ updateUrl NOTIFY updateCheckChanged FINAL)
    Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY updateCheckChanged FINAL)
    Q_PROPERTY(QString packageKind READ packageKind CONSTANT FINAL)
    Q_PROPERTY(bool canInAppUpdate READ canInAppUpdate NOTIFY updateCheckChanged FINAL)
    Q_PROPERTY(bool updatingApp READ updatingApp NOTIFY updateCheckChanged FINAL)
    Q_PROPERTY(qreal updateProgress READ updateProgress NOTIFY updateCheckChanged FINAL)
    Q_PROPERTY(bool updateInstalledRestartRequired READ updateInstalledRestartRequired NOTIFY updateCheckChanged FINAL)
    Q_PROPERTY(bool startHiddenInTray READ startHiddenInTray WRITE setStartHiddenInTray NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool startWithSystem READ startWithSystem WRITE setStartWithSystem NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY trayAvailableChanged FINAL)
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

    Q_PROPERTY(QVariantList groupInvites READ groupInvites NOTIFY groupInvitesChanged FINAL)
    Q_PROPERTY(QVariantList friendRecommendations READ friendRecommendations NOTIFY friendRecommendationsChanged FINAL)
    Q_PROPERTY(QVariantList profileRelatedGroups READ profileRelatedGroups NOTIFY currentProfileChanged FINAL)
    Q_PROPERTY(QVariantList linkedDevices READ linkedDevices NOTIFY linkedDevicesChanged FINAL)
    Q_PROPERTY(QVariantList bankList READ bankList NOTIFY bankDataChanged FINAL)
    Q_PROPERTY(QVariantList bankAccounts READ bankAccounts NOTIFY bankDataChanged FINAL)
    Q_PROPERTY(QVariantList blockedUsers READ blockedUsers NOTIFY blockedUsersChanged FINAL)
    Q_PROPERTY(QVariantList cachedSystemSounds READ cachedSystemSounds NOTIFY systemSoundsChanged FINAL)
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
    bool currentConversationIsTyping() const;
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
    Models::EmojiCatalog* emojis() const { return m_emojiCatalog; }
    PinBoard* pins() const { return m_pinBoard; }
    Models::MessageSearchModel* messageSearch() const { return m_messageSearch; }
    Models::MessageSearchModel* peopleSearch() const { return m_peopleSearch; }
    Models::MessageSearchModel* hiddenSearch() const { return m_hiddenSearch; }
    bool hasHiddenPin() const { return !m_hiddenPinHash.isEmpty(); }
    bool hiddenUnread() const { return m_hiddenUnread; }
    static QString hiddenPinDigest(const QString& pin);
    static bool isReparsedOnLoad(const QString& msgType);
    static bool isGoneError(const QString& error);
    static bool storeOnlyIngest(bool resident, bool cacheUsable, bool phoneImport);
    QString myUid() const { return m_myUid; }
    Models::RichComposer* composer() const { return m_composer; }
    bool showSyncPrompt() const { return m_showSyncPrompt; }
    Q_INVOKABLE void acceptSyncPrompt();
    Q_INVOKABLE void dismissSyncPrompt(bool permanently);
    bool shouldOfferMobileSync() const;
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
    static constexpr int kCacheParseVersion = 7;
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
    Q_INVOKABLE void requestMemoryTrim();
    void setWindowVisible(bool visible);
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
    Q_INVOKABLE QVariantList conversationsForForward(const QString& query = QString()) const;
    Q_INVOKABLE void forwardMessage(const QString& targetThreadId, const QString& msgId);

    bool showCreatePollModal() const { return m_showCreatePollModal; }
    void setShowCreatePollModal(bool show);
    Q_INVOKABLE void openCreatePollModal();
    Q_INVOKABLE void closeCreatePollModal();
    Q_INVOKABLE void createPoll(const QString& groupId, const QString& question, const QStringList& options,
                                const QVariantMap& settings);
    Q_INVOKABLE QString pollDraftError(const QString& question, const QStringList& options) const;
    Q_INVOKABLE void submitPollChoices(qint64 pollId, const QVariantList& selectedIds, const QStringList& newOptions,
                                       const QVariantList& newVoted);
    Q_INVOKABLE void openPollDialog(qint64 pollId, const QString& groupId, bool voters = false);
    Q_INVOKABLE void closePollDialog();
    Q_INVOKABLE QVariantMap pollView(qint64 pollId) const;
    qint64 pollDialogId() const { return m_pollDialogId; }
    QString pollDialogGroupId() const { return m_pollDialogGroupId; }
    bool pollDialogVoters() const { return m_pollDialogVoters; }
    int pollRevision() const { return m_pollRevision; }
    using PersonLookup = std::function<QPair<QString, QString>(const QString& uid)>;
    static QVariantMap buildPollView(const QJsonObject& detail, const QString& myUid, bool canManage, qint64 nowMs,
                                     const PersonLookup& person);
    static QString pollDraftCode(const QString& question, const QStringList& options);
    static QString pollTemplateText(const QString& raw);
    Q_INVOKABLE void sharePoll(qint64 pollId);
    Q_INVOKABLE void lockPoll(qint64 pollId);

    Q_INVOKABLE void changeGroupName(const QString& groupId, const QString& newName);
    Q_INVOKABLE void addMemberToGroup(const QString& groupId, const QString& memberId);
    Q_INVOKABLE void removeMemberFromGroup(const QString& groupId, const QString& memberId);
    Q_INVOKABLE void leaveGroup(const QString& groupId, bool silent = false);

    Q_INVOKABLE bool tryPasteClipboardImage();
    Q_INVOKABLE bool pasteClipboardToDraft();
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
    bool notifyReactions() const { return m_notifyReactions; }
    Q_INVOKABLE void setNotifyReactions(bool on);
    QStringList currentUnreadReactionIds() const { return m_currentUnreadReactionIds; }
    int currentUnreadReactionCount() const { return int(m_currentUnreadReactionIds.size()); }
    QString currentUnreadReactionIcon() const { return m_currentUnreadReactionIcon; }
    Q_INVOKABLE QString nextUnreadReactionMsgId() const;
    Q_INVOKABLE void markReactionsSeen(const QString& msgId);
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
    bool showBusinessButton() const { return m_showBusinessButton; }
    void setShowBusinessButton(bool val);
    bool showKikiButton() const { return m_showKikiButton; }
    void setShowKikiButton(bool val);
    bool hardwareAcceleration() const { return m_hardwareAcceleration; }
    void setHardwareAcceleration(bool val);
    bool hardwareAccelerationRestartRequired() const { return m_hardwareAcceleration != m_startupHardwareAcceleration; }
    bool hardwareAccelerationFailed() const { return m_hardwareAccelerationFailed; }
    Q_INVOKABLE void restartApp();
    QString defaultReaction() const { return m_defaultReaction; }
    QString defaultReactionEmoji() const;
    void setDefaultReaction(const QString& key);
    Q_INVOKABLE QVariantList defaultReactionOptions() const;
    int interfaceScale() const { return m_interfaceScale; }
    void setInterfaceScale(int pct);
    bool interfaceScaleRestartRequired() const { return m_interfaceScale != m_startupInterfaceScale; }
    static int normalizeInterfaceScale(int pct);

    QString appVersion() const;
    QString qtRuntimeVersion() const;
    QString rendererBackend() const;
    bool softwareRenderer() const;
    bool multimediaAvailable() const;
    bool effectsAvailable() const;
    QString platformName() const;
    bool checkingUpdate() const { return m_checkingUpdate; }
    bool updateAvailable() const { return m_updateAvailable; }
    QString latestVersion() const { return m_latestVersion; }
    QString updateUrl() const { return m_updateUrl; }
    QString updateStatus() const;
    QString packageKind() const;
    bool canInAppUpdate() const;
    bool updatingApp() const { return m_updatingApp; }
    qreal updateProgress() const { return m_updateProgress; }
    bool updateInstalledRestartRequired() const { return m_updateInstalledRestartRequired; }
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void installAvailableUpdate();
    static int compareVersions(const QString& a, const QString& b);

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

    int applyCorrectedSenders(const QHash<QString, QString>& senders);
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
    void handleReactionsForTesting(const QJsonArray& reactions, bool isGroup, bool live = false) {
        handleReactions(reactions, isGroup, live);
    }
    Platform::DesktopNotifications* notificationsForTesting() const { return m_notifications; }
    void reloadDraftsForTesting() { loadDrafts(); }
    void resolveUnknownUsersForTesting() { resolveUnknownUsers(); }
    Sync::MobileHistorySync* historySyncForTesting() const { return m_historySync; }
    bool openOfflineCacheForTesting(const QString& uid) {
        m_cachedUid = uid;
        return loadOfflineCache();
    }
    qulonglong readPointForTesting(const QString& convId) { return readPointOf(convId); }
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
    Q_INVOKABLE bool pollCanPick(qint64 pollId) const;
    Q_INVOKABLE bool pollJoined(qint64 pollId) const;
    Q_INVOKABLE bool pollMultiChoice(qint64 pollId) const;
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
    Q_INVOKABLE void loadBusinessProfile();
    Q_INVOKABLE void loadBusinessCatalogs();
    Q_INVOKABLE void selectBusinessCatalog(const QString& catalogId);
    Q_INVOKABLE void createBusinessCatalog(const QString& name);
    Q_INVOKABLE void updateBusinessCatalog(const QString& catalogId, const QString& name);
    Q_INVOKABLE void deleteBusinessCatalog(const QString& catalogId);
    Q_INVOKABLE void createBusinessProduct(const QString& catalogId, const QString& name,
                                             const QString& price, const QString& description);
    Q_INVOKABLE void updateBusinessProduct(const QString& catalogId, const QString& productId,
                                             const QString& name, const QString& price,
                                             const QString& description, qint64 createTime);
    Q_INVOKABLE void deleteBusinessProduct(const QString& catalogId, const QString& productId);
    Q_INVOKABLE void loadBusinessAutoReplies();
    Q_INVOKABLE void createBusinessAutoReply(const QString& content, bool enable, int scope,
                                               qint64 startTime, qint64 endTime);
    Q_INVOKABLE void updateBusinessAutoReply(qint64 id, const QString& content, bool enable, int scope,
                                               qint64 startTime, qint64 endTime);
    Q_INVOKABLE void deleteBusinessAutoReply(qint64 id);
    Q_INVOKABLE void sendProductToCurrentChat(const QString& name, const QString& price, const QString& description, const QString& imageUrl);
    bool showBusinessToolsModal() const { return m_showBusinessToolsModal; }
    QString businessTab() const { return m_businessTab; }
    bool hasBusinessPackage() const { return m_hasBusinessPackage; }
    QString businessPackageName() const { return m_businessPackageName; }
    bool businessLoading() const { return m_businessLoading; }
    QVariantMap businessProfile() const { return m_businessProfile; }
    QVariantList businessCatalogs() const { return m_businessCatalogs; }
    QString selectedCatalogId() const { return m_selectedCatalogId; }
    QVariantList businessProducts() const { return m_businessProducts; }
    QVariantList businessAutoReplies() const { return m_businessAutoReplies; }
    void setBusinessTab(const QString& tab);
    void setShowBusinessToolsModal(bool show);
    Q_INVOKABLE void toggleConversationInfo();
    Q_INVOKABLE void findUserByPhone(const QString& phone);
    Q_INVOKABLE void findUserByUsername(const QString& username);
    Q_INVOKABLE void sendFriendRequest(const QString& uid, const QString& message);
    Q_INVOKABLE void createGroup(const QString& name, const QStringList& members);
    Q_INVOKABLE void openMyCloud();
    Q_INVOKABLE void openMessagesTab();
    QString lastChatId() const { return m_lastChatId; }
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
    Q_INVOKABLE void loadSystemSoundsAsync();
    QVariantList cachedSystemSounds() const { return m_cachedSystemSounds; }
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
    Q_INVOKABLE bool isMediaTooLarge(const QString& url) const;
    Q_INVOKABLE void retryMedia(const QString& url);
    int brokenMediaRevision() const { return m_brokenMediaRevision; }
    Q_INVOKABLE QVariantMap reactionDetails(const QString& msgId) const;
    Q_INVOKABLE void removeMyReaction(const QString& msgId);
    Q_INVOKABLE QString displayNameOf(const QString& uid) const;
    Q_INVOKABLE QString avatarOf(const QString& uid) const;
    Q_INVOKABLE void showNotice(const QString& text) { emit notice(text); }
    Q_INVOKABLE void openLink(const QString& url, const QString& title = QString(), const QString& thumb = QString());
    Q_INVOKABLE bool openExternalUrl(const QString& url);
    Q_INVOKABLE QString linkifyText(const QString& text) const;
    static QUrl externalUrl(const QString& url);
    static QString linkHrefOf(const QJsonObject& obj, const QJsonObject& params);
    static QString uniqueFilePath(const QString& dir, const QString& fileName);
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
    Q_INVOKABLE void downloadFileMessage(const QString& url, const QString& fileName);
    Q_INVOKABLE void prepareVideoPlayback(const QString& url);
    Q_INVOKABLE static bool isVideoFileName(const QString& nameOrUrl);
    Q_INVOKABLE QString suggestedMediaFileName(const QString& url) const;
    Q_INVOKABLE QString suggestedMediaSaveUrl(const QString& url) const;
    Q_INVOKABLE QString annotatedImageTempPath() const;
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
    void setTrayAvailableForTesting(int mode) { m_trayAvailableOverride = mode; emit settingsChanged(); }
#endif
    Q_INVOKABLE void answerCall(bool withCamera = true);
    Q_INVOKABLE void declineCall();
    Q_INVOKABLE void requestScreenshot(bool hideWindow = false);
    Q_INVOKABLE void printMedia(const QString& url);
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height, bool maximized);
    Q_INVOKABLE void notifyWindowVisibility(bool visible);
    Q_INVOKABLE void hideWindowToTray();
    Q_SLOT void onScreenshotResponse(uint response, const QVariantMap& results);

    Q_INVOKABLE bool generateThumbnail(const QString& inputPath, const QString& outputPath, int width = 256, int height = 256);
    Q_INVOKABLE bool extractVideoThumbnail(const QString& videoPath, const QString& outputPath);
    Q_INVOKABLE QString recognizeImageText(const QString& imagePath);
    Q_INVOKABLE void syncOfflineData();

    Q_INVOKABLE QVariantMap parseBinNetPayload(const QByteArray& payload);
    Q_INVOKABLE bool extractZdbBackup(const QString& zdbPath, const QString& outputDir, const QString& privateKey);
    Q_INVOKABLE QVariantMap deriveSrtpKeys(const QString& sessId);

    QVariantList groupInvites() const { return m_groupInvites; }
    QVariantList friendRecommendations() const { return m_friendRecommendations; }
    QVariantList profileRelatedGroups() const { return m_profileRelatedGroups; }
    QVariantList linkedDevices() const { return m_linkedDevices; }
    QVariantList bankList() const { return m_bankList; }
    QVariantList bankAccounts() const { return m_bankAccounts; }
    QVariantList blockedUsers() const { return m_blockedUsers; }
    Q_INVOKABLE void refreshGroupInvites();
    Q_INVOKABLE void joinGroupInvite(const QString& groupId);
    Q_INVOKABLE void deleteGroupInvite(const QString& groupId, bool blockFuture = false);
    Q_INVOKABLE void refreshFriendRecommendations();
    Q_INVOKABLE void refreshCloseFriends();
    Q_INVOKABLE bool isCloseFriend(const QString& uid) const { return m_closeFriendIds.contains(uid); }
    Q_INVOKABLE void refreshLinkedDevices();
    Q_INVOKABLE void refreshBlockedUsers();
    Q_INVOKABLE void scanAndOpenUrl(const QString& url);
    Q_INVOKABLE void reportAbuse(const QString& threadId, bool isGroup, int reason = 0, const QString& content = QString());
    Q_INVOKABLE void upgradeGroupToCommunity(const QString& groupId);
    Q_INVOKABLE void refreshBankAccounts();
    Q_INVOKABLE void createBankAccount(int binBank, const QString& numAccBank, const QString& nameAccBank);
    Q_INVOKABLE void sendBankCard(int binBank, const QString& numAccBank, const QString& nameAccBank);
    Q_INVOKABLE void fetchConversationSharedMedia(const QString& convId);
    Q_INVOKABLE QVariantList sharedItemsForConversation(const QString& convId, const QString& kind, int limit = 60);
    static QString bankShortNameForBin(int bin);

signals:
    void windowActivationRequested(const QString& activationToken);
    void hideWindowRequested();
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
    void unreadReactionsChanged();
    void reactionJumpRequested(const QString& msgId);
    void composerStateChanged();
    void appLanguageChanged();
    void connectionStateChanged(bool connected);
    void stickerRevisionChanged();
    void brokenMediaChanged();
    void videoPlaybackReady(const QString& originalUrl, const QString& localFileUrl, const QString& error);
    void revealRequested(const QString& msgId);
    void messageSent(const QString& text);
    void e2eeVerified(bool success, const QString& detail);
    void incomingCallStateChanged(bool active);
    void screenshotCaptured(const QString& uri);
    void clipboardAttachmentsReady(const QVariantList& items);
    void systemSoundsChanged();

    void settingsModalChanged();
    void userProfileModalChanged();
    void currentProfileChanged();
    void addFriendModalChanged();
    void createGroupModalChanged();
    void conversationInfoChanged();
    void forwardModalChanged();
    void createPollModalChanged();
    void pollDialogChanged();
    void pollRevisionChanged();
    void businessToolsModalChanged();
    void businessTabChanged();
    void businessDataChanged();
    void pinnedMessageChanged();
    void mobileSyncStateChanged();
    void mobileSyncProgressChanged();
    void navigationTabChanged();
    void settingsChanged();
    void trayAvailableChanged();
    void updateCheckChanged();
    void restartRequested();
    void memoryTrimRequested();
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
    void groupInvitesChanged();
    void friendRecommendationsChanged();
    void linkedDevicesChanged();
    void bankDataChanged();
    void blockedUsersChanged();
    void sharedMediaUpdated(const QString& convId);

private Q_SLOTS:
    void onPrepareForSleep(bool sleeping);

private:
    struct Profile {
        QString name;
        QString avatar;
        int memberCount{0};
    };

    struct ProfileIngestOptions {
        enum class Conversations { Skip, IfPresent, Always };
        Conversations conversations{Conversations::Skip};
        bool stripDeviceSuffix{true};
        bool requireName{true};
        bool skipExisting{false};
        bool myDocumentsAlias{false};
    };

    void notifyIncomingCallDbus(const QString& callerName);
    void executeScreenshotCapture();
    void saveSession();
    void loadSession();
    void generateQrMatrix();
    QString L(const QString& vi, const QString& en) const { return m_appLanguage == "en" ? en : vi; }
    void noticeCallUnavailable(const QString& reason);
    bool openLocalFile(const QString& path);
    QString nextOpenFailureFile();
    void watchOpenFailure(const QString& failureFile, const QUrl& url);
    void reportOpenFailure(const QUrl& url);
    quint64 m_openSeq = 0;
    static constexpr int kScanUrlWaitMs = 2500;
    void setLoginStatus(const QString& message, bool isError);
    QString friendlyError(const QString& error, int code = 0) const;

    void onSessionLoggedIn();
    void onSessionLoginFailed(const QString& reason);
    void onSessionLoginUnavailable(const QString& reason);
    void watchConnectivity();
    void scheduleReconnectProbe();
    void loadAccountData();
    void preloadRecentThreads();
    void handleSyncControl(const QString& actType, const QString& act, const QJsonValue& data);
    void handleGroupControl(const QString& act, const QJsonObject& data);
    void markThreadReadOnly(const QString& id, const QString& reason, qint64 ts);
    void purgeExpiredMessages();
    void migrateCachedParses();
    void noteAccountFlags(const QString& uid, const QJsonObject& profile);
    void openRemoteFile(const QString& url, const QString& fileName, const QString& goneText, bool openAfter);
    void downloadToFile(const QString& url, const QString& path, std::function<void(const QString& error)> done);
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
    void fetchBusinessCatalogPage(int page, QVariantList collected, quint64 gen);
    void applyBusinessCatalogs(const QVariantList& cats);
    static constexpr int kCatalogPageLimit = 20;
    static constexpr int kMaxCatalogPages = 25;
    quint64 m_catalogLoadGen = 0;
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
    void refreshPhoneImportedThreads();
    void startIdleMemoryTrim();
    void fetchBytes(const QString& url, std::function<void(const QByteArray& data, const QString& error)> done);
    void applyStrangerRank(const QString& id, const QJsonObject& data);
    void persistProfile(const QString& id, bool isGroup);
    QSet<QString> ingestProfiles(const QJsonObject& profiles, const ProfileIngestOptions& options);
    void applyReadPoints(const QJsonArray& clearUnreads);
    qulonglong readPointOf(const QString& convId);
    bool advanceReadPoint(const QString& convId, const QString& msgId);
    void resetReadPoints();
    void scheduleReadPointSync(int delayMs = 4000);
    void syncReadPoints();
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
    void sendForward(const QString& targetThreadId, const QString& msgId, bool notifySuccess,
                     std::function<void(bool ok)> done = {});
    void updateGroupComposite(const QString& groupId);
    void lookupCompositeMembers();
    void resolveUnknownUsers();
    void handleIncomingMessages(const QJsonArray& messages, bool isGroup, bool isOld);
    Models::MessageItem toMessageItem(const QJsonObject& msg, bool isGroup, QString* threadId, QString* preview) const;
    QString fillHighlights(const QString& text, const QJsonArray& highlights) const;
    static bool isQuietMessageType(const QString& msgType);
    void ensureConversation(const QString& threadId, bool isGroup);
    void applyProfileToConversation(const QString& id);
    QString resolvedSenderName(const Models::MessageItem& item) const;
    QString previewPrefixFor(const Models::MessageItem& item, bool isGroup) const;
    void refreshGroupSenderPrefixes(const QSet<QString>& uids);
    void rebuildContacts();
    void rebuildContactsNow();
    void setSyncing(bool syncing);

    void handleReactions(const QJsonArray& reactions, bool isGroup, bool live = false);
    QString messageSnippet(const QString& convId, const QString& msgId) const;
    void showReactionNotification(const QString& convId, const QString& msgId, const QString& uid, const QString& icon);
    void refreshUnreadReactions();
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
    void resolveZInstantBankCard(const QString& convId, const QString& msgId, const QString& dataUrl);
    QString threadOfMessage(const QString& msgId) const;
    bool isGroupThread(const QString& threadId) const;
    void handleMobileSyncControl(const QJsonObject& content);
    void setMobileSyncState(int state, const QString& status, int progress = 0);
    QNetworkAccessManager* ensureUpdateNam();
    void trimMessageCaches();

    QHash<QString, QJsonObject> m_rawMessages;
    QHash<QString, QString> m_messageThread;
    QHash<QString, QVector<Offline::ZaloOfflineStore::ReactionEvent>> m_reactions;
    QSet<QString> m_reactedIds;
    QHash<QString, qint64> m_reactionClearedTs;
    QHash<QString, int> m_ingestDrops;
    bool m_phoneImporting{false};
    bool m_reactedIdsLoaded{false};
    QVector<Offline::ZaloOfflineStore::ReactionEvent>& eventsOf(const QString& msgId);
    bool hasReactions(const QString& msgId);
    bool addReactionEvent(const QString& msgId, const QString& uid, const QString& icon, const QString& eventId, qint64 ts);
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
    quint64 m_stickerGen{0};
    int m_stickerRevision{0};
    QSet<QString> m_stickerPrefetching;
    bool m_userLookupInFlight{false};
    void pumpStickerQueue();
    qint64 m_currentLastOnline{0};
    qint64 m_lastTypingSent{0};
    int m_pendingAttachments{0};
    quint64 m_localSeq{0};
    QTimer* m_presenceTimer{nullptr};
    bool m_windowVisible{true};
    QTimer* m_typingTimer{nullptr};
    QVector<Models::ContactItem> m_friendRequests;
    QVariantList m_sentFriendRequests;
    QVariantList m_myAvatarHistory;
    Platform::SoundEngine* m_sound{nullptr};
    Call::CallBridge* m_calls{nullptr};
    QPointer<Platform::SystemTrayController> m_boundTray;
    QString m_lastCallPhase;
    void syncCallTrayState();

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
    BoundedHash<QString, StrangerRank> m_strangerRanks;
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
    QSet<QString> m_phoneImportedResident;
    QHash<QString, qint64> m_cacheFloor;
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
    QHash<QString, QString> m_groupVersions;
    mutable QHash<QString, QVariantList> m_groupMembersCache;
    QSet<QString> m_memberLookupInFlight;
    QSet<QString> m_compositeLookups;
    QTimer* m_compositeLookupTimer{nullptr};
    bool m_compositeLookupInFlight{false};
    QHash<QString, int> m_compositeRetries;
    BoundedSet<QString> m_compositeAsked;
    BoundedSet<QString> m_unresolvedMembers;
    BoundedHash<qint64, qint64> m_pollFetchedAt;
    BoundedHash<qint64, QJsonObject> m_pollDetails;
    BoundedHash<qint64, QString> m_pollGroups;
    qint64 m_pollDialogId{0};
    QString m_pollDialogGroupId;
    bool m_pollDialogVoters{false};
    int m_pollRevision{0};
    void storePollDetail(qint64 pollId, const QJsonObject& detail);
    bool canManagePoll(qint64 pollId, const QJsonObject& detail) const;
    QString pollErrorText(const QString& prefix, const QString& err, int code) const;
    QVariantList m_quickMessages;
    QHash<QString, int> m_serverSettings;
    QVariantList m_reminders;
    QString m_remindersConvId;
    bool m_loadingReminders{false};
    QMediaPlayer* m_voicePlayer{nullptr};
    QString m_playingVoiceId;
    qreal m_voicePlaybackRate{1.0};
    BoundedSet<QString> m_voiceErrors;
    QHash<QString, QString> m_aliases;
    bool m_realtimeWasUp{false};
    QTimer* m_labelDebounce{nullptr};
    QTimer* m_readPointSyncTimer{nullptr};
    bool m_readPointSyncPending{false};
    QElapsedTimer m_readPointSyncFirstAsk;
    QTimer* m_memoryTrimTimer{nullptr};
    QHash<QString, qulonglong> m_readPoints;
    bool m_readPointsLoaded{false};
    int m_readPointSyncInFlight{0};
    int m_readPointSyncRetries{0};
    QTimer* m_profileRefreshTimer{nullptr};
    QTimer* m_friendEventDebounce{nullptr};
    QTimer* m_rebuildContactsTimer{nullptr};
    QSet<QString> m_historyExhausted;
    struct CloudPageState {
        QString lastMsgId;
        bool useOld{false};
        bool initialized{false};
    };
    QHash<QString, CloudPageState> m_cloudPageState;
    void fetchInitialConversationHistory(const QString& id);
    void fetchCloudHistoryPage(const QString& convId, bool isGroup, bool isMyCloud, const QString& cursor,
                               bool useOld, bool initialOpen, bool allowOldFollowup);
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
    Models::EmojiCatalog* m_emojiCatalog{nullptr};
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
    bool m_businessLoading{false};
    QVariantMap m_businessProfile;
    QVariantList m_businessCatalogs;
    QString m_selectedCatalogId;
    QVariantList m_businessProducts;
    QVariantList m_businessAutoReplies;
    Models::RichComposer* m_composer{nullptr};
    bool m_showSyncPrompt{false};
    QString m_voiceTarget;
    QTimer* m_restoreRetryTimer{nullptr};
    QTimer* m_reconnectProbeTimer{nullptr};
    QSet<QString> m_downloadsInFlight;
    bool m_loadingHistory{false};
    bool m_syncing{false};
    quint64 m_accountLoadGen{0};
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
    QString m_lastChatId;
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
    bool m_notifyReactions{true};
    QStringList m_currentUnreadReactionIds;
    QString m_currentUnreadReactionIcon;
    static constexpr qint64 kReactionNotifyWindowMs = 120000;
    bool m_notifySound{false};
    bool m_antiPeeking{false};
    QString m_chatBackgroundMode{QStringLiteral("none")};
    qreal m_chatBackgroundOpacity{0.15};
    QString m_chatBackgroundImage;
    bool m_showLabelFolders{false};
    bool m_showBusinessButton{true};
    bool m_showKikiButton{true};
    bool m_hardwareAcceleration{false};
    bool m_startupHardwareAcceleration{false};
    bool m_hardwareAccelerationFailed{false};
    QString m_defaultReaction{QStringLiteral("like")};
    int m_interfaceScale{100};
    int m_startupInterfaceScale{100};
    bool m_checkingUpdate{false};
    bool m_updateAvailable{false};
    bool m_updatingApp{false};
    bool m_updateInstalledRestartRequired{false};
    qreal m_updateProgress{0.0};
    QString m_latestVersion;
    QString m_updateUrl;
    QString m_updateAssetName;
    QString m_updateAssetUrl;
    QString m_updateShaSumsUrl;
    QString m_updateSavedPath;
    enum class UpdateCheckState {
        Idle,
        Checking,
        NetworkError,
        NoRelease,
        Available,
        UpToDate,
        Downloading,
        Verifying,
        Installing,
        InstalledRestartRequired,
        DownloadFailed,
        VerifyFailed,
        InstallFailed,
        SavedToDownloads
    };
    UpdateCheckState m_updateState{UpdateCheckState::Idle};
    QVariantList m_groupInvites;
    QVariantList m_friendRecommendations;
    QVariantList m_profileRelatedGroups;
    QVariantList m_linkedDevices;
    QVariantList m_bankList;
    QVariantList m_bankAccounts;
    QVariantList m_blockedUsers;
    quint64 m_blockedFetchGen{0};
    QVariantList m_blockedPending;
    QSet<QString> m_blockedPendingIds;
    QVariantList m_cachedSystemSounds;
    bool m_systemSoundsLoaded{false};
    bool m_systemSoundsLoading{false};
    QHash<QString, QHash<QString, QVariantList>> m_serverSharedMedia;
    struct LastGroupSender {
        QString uid;
        qint64 ts{0};
    };
    QHash<QString, LastGroupSender> m_lastGroupSender;
    QSet<QString> m_bankCardFetching;
    QSet<QString> m_closeFriendIds;
    bool m_startHiddenInTray{false};
    bool m_closeToTray{true};
    bool m_launchHidden{false};
    int m_trayAvailableOverride{-1};

    bool m_blockTyping{false};
    bool m_blockDelivered{false};
    bool m_blockSeen{false};

    QString m_selectedRingtone{"zalo"};
    QString m_selectedNotiSound{"pop"};
    bool m_isPreviewPlaying{false};
    QString m_currentPreviewSound{""};
    QTimer* m_previewTimer{nullptr};
    QNetworkAccessManager* m_updateNam{nullptr};
    QString m_appLanguage{"en"};

    Models::ConversationModel* m_conversationModel{nullptr};
    Models::MessageModel* m_messageModel{nullptr};
    Models::ContactModel* m_contactModel{nullptr};
    Call::ZCallEngine* m_callEngine{nullptr};
    Offline::ZaloOfflineStore* m_offlineStore{nullptr};
    ZaloApiClient* m_apiClient{nullptr};
};

}
