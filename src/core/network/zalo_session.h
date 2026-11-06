#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QWebSocket;

namespace Zalos::Core::Network {

class SessionCookieJar : public QNetworkCookieJar {
    Q_OBJECT
public:
    using QNetworkCookieJar::QNetworkCookieJar;
    QList<QNetworkCookie> cookies() const { return allCookies(); }
    void replaceCookies(const QList<QNetworkCookie>& cookies) { setAllCookies(cookies); }
};

class ZaloSession : public QObject {
    Q_OBJECT

public:
    static constexpr int kApiType = 30;
    static constexpr int kApiVersion = 685;
    static const QString kDefaultUserAgent;

    using Callback = std::function<void(bool ok, const QJsonValue& data, const QString& error, int errorCode)>;

    explicit ZaloSession(QNetworkAccessManager* nam, SessionCookieJar* jar, QObject* parent = nullptr);

    void login(const QString& imei, const QString& userAgent, const QString& language);
    void reset();

    bool isReady() const { return !m_secretKey.isEmpty(); }
    QString uid() const { return m_uid; }
    QString imei() const { return m_imei; }
    QString userAgent() const { return m_userAgent; }
    QString send2meId() const { return m_loginInfo.value("send2me_id").toString(); }
    QJsonObject loginInfo() const { return m_loginInfo; }
    QString phonebookVersion() const;

    bool saveCredentials(const QString& path) const;
    static bool loadCredentials(const QString& path, SessionCookieJar* jar, QString* imei, QString* userAgent);
    static void importCookies(SessionCookieJar* jar, const QJsonArray& cookies);

    void apiGet(const QString& service, const QString& path, const QJsonObject& params, Callback cb,
                bool alwaysSendParams = false);
    void apiPost(const QString& service, const QString& path, const QJsonObject& params, Callback cb,
                 const QHash<QString, QString>& extraQuery = {});

    void fetchAccountInfo(Callback cb);
    void fetchFriends(Callback cb);
    void fetchGroupIds(Callback cb);
    void fetchGroupInfo(const QStringList& groupIds, Callback cb);
    void fetchPinnedConversations(Callback cb);
    void fetchUserInfo(const QStringList& userIds, Callback cb);
    void fetchGroupHistory(const QString& groupId, int count, Callback cb);
    void fetchLastMessages(const QStringList& threadIds, const QSet<QString>& groupIds, Callback cb);
    void fetchCloudHistory(const QString& threadId, bool isMyCloud, const QString& beforeMsgId, int count,
                           Callback cb);
    void sendText(const QString& threadId, bool isGroup, const QString& text, Callback cb,
                  const QString& cliMsgId = QString(), const QJsonArray& styles = QJsonArray());
    void sendQuote(const QString& threadId, bool isGroup, const QString& text, const QJsonObject& quoted, Callback cb,
                   const QString& cliMsgId = QString(), const QJsonArray& styles = QJsonArray());
    void setUnreadMark(const QString& threadId, bool isGroup, bool unread, Callback cb);
    void deleteConversation(const QString& threadId, bool isGroup, const QJsonObject& lastMsg, Callback cb);
    void setMute(const QString& threadId, bool isGroup, qint64 seconds, Callback cb);
    void findUserByPhone(const QString& phone, Callback cb);
    void sendFriendRequest(const QString& userId, const QString& message, Callback cb);
    void createGroup(const QString& name, const QStringList& members, Callback cb);

    void addReaction(const QString& threadId, bool isGroup, const QJsonObject& msg, const QString& rIcon, int rType,
                     Callback cb);
    void undoMessage(const QString& threadId, bool isGroup, const QJsonObject& msg, Callback cb);
    void deleteMessage(const QString& threadId, bool isGroup, const QJsonObject& msg, bool onlyMe, Callback cb);
    void sendSeen(const QString& threadId, bool isGroup, const QJsonArray& msgs, Callback cb);
    void sendTyping(const QString& threadId, bool isGroup, Callback cb);

    void sendAttachment(const QString& threadId, bool isGroup, const QString& filePath, const QString& caption,
                        Callback cb);
    void sendVoice(const QString& threadId, bool isGroup, const QString& filePath, const QString& clientId, Callback cb);
    void sendSticker(const QString& threadId, bool isGroup, qint64 stickerId, qint64 cateId, int type, Callback cb);
    void uploadProfileAvatar(const QString& imagePath, Callback cb);
    void uploadGroupAvatar(const QString& groupId, const QString& imagePath, Callback cb);
    void searchStickers(const QString& keyword, Callback cb);
    void fetchStickerDetail(qint64 stickerId, Callback cb);
    void fetchStickerCategories(Callback cb);
    void fetchPersonalStickerList(Callback cb);
    void fetchCategoryStickers(qint64 cateId, Callback cb);
    void searchStickerIds(const QString& keyword, int limit, Callback cb);

    void logDiagnostic(const QString& line) const { logRealtime(line); }

    void fetchFriendOnlines(Callback cb);
    void fetchLastOnline(const QString& userId, Callback cb);
    void fetchProfileRank(const QString& userId, Callback cb);
    void fetchLabels(Callback cb);
    void updateLabels(const QJsonArray& labelData, qint64 version, Callback cb);
    void fetchUserQr(const QStringList& userIds, Callback cb);
    void fetchFriendRecommendations(Callback cb);
    void acceptFriendRequest(const QString& userId, Callback cb);
    void rejectFriendRequest(const QString& userId, Callback cb);
    void fetchSentFriendRequests(Callback cb);
    void undoFriendRequest(const QString& userId, Callback cb);
    void findUserByUsername(const QString& username, Callback cb);
    void fetchAvatarList(Callback cb);
    void reuseAvatar(const QString& photoId, Callback cb);
    void deleteAvatarPhotos(const QStringList& photoIds, Callback cb);

    void pullMobileMessages(const QString& pcName, const QString& publicKey, qint64 fromSeqId, qint64 minSeqId,
                            bool isRetry, Callback cb);
    void cancelPullMobileMessages(const QString& pcName, const QString& publicKey, Callback cb);
    void cleanMobileSync(const QString& publicKey, Callback cb);
    void fetchAliasList(int page, int count, Callback cb);
    void fetchUnreadMarks(Callback cb);
    void fetchNoisedIds(const QStringList& userIds, const QStringList& groupIds, Callback cb);
    QNetworkReply* getWithCookies(const QUrl& url);

    void createPoll(const QString& groupId, const QString& question, const QStringList& options,
                    bool allowMultiChoices, Callback cb);
    void votePoll(qint64 pollId, const QList<int>& optionIds, Callback cb);
    void lockPoll(qint64 pollId, Callback cb);

    void changeGroupName(const QString& groupId, const QString& newName, Callback cb);
    void addUserToGroup(const QString& groupId, const QStringList& memberIds, Callback cb);
    void removeUserFromGroup(const QString& groupId, const QString& memberId, Callback cb);
    void leaveGroup(const QString& groupId, bool silent, Callback cb);

    void changeFriendAlias(const QString& friendId, const QString& alias, Callback cb);
    void sendContactCard(const QString& threadId, bool isGroup, const QString& contactId, const QString& qrCodeUrl,
                         const QString& phone, const QString& cliMsgId, Callback cb);
    static int clientMessageType(const QString& msgType);
    void fetchGroupPins(const QString& groupId, qint64 boardVersion, Callback cb);
    void createGroupPin(const QString& groupId, const QJsonObject& topic, Callback cb);
    void unpinGroupPins(const QString& groupId, const QJsonArray& topics, qint64 boardVersion, Callback cb);
    void reorderGroupPins(const QString& groupId, const QJsonArray& topics, qint64 boardVersion, Callback cb);

    void createReminder(const QString& threadId, bool isGroup, const QString& title, const QString& emoji,
                        qint64 startTime, int repeat, Callback cb);
    void listReminders(const QString& threadId, bool isGroup, Callback cb);
    void removeReminder(const QString& threadId, bool isGroup, const QString& reminderId, Callback cb);
    void createNote(const QString& groupId, const QString& title, bool pin, Callback cb);
    void fetchFriendPins(const QString& userId, qint64 version, Callback cb);
    void createFriendPin(const QString& userId, const QJsonObject& topic, qint64 version, Callback cb);
    void unpinFriendPins(const QString& userId, const QJsonArray& topics, qint64 version, Callback cb);
    void reorderFriendPins(const QString& userId, const QJsonArray& topics, qint64 version, Callback cb);

    void updateProfile(const QString& name, int gender, const QString& dob, Callback cb);
    void updateProfileBio(const QString& status, Callback cb);
    void blockUser(const QString& userId, Callback cb);
    void unblockUser(const QString& userId, Callback cb);
    void removeFriend(const QString& userId, Callback cb);

    void fetchGroupMembersInfo(const QStringList& memberIds, Callback cb);
    void fetchPollDetail(qint64 pollId, Callback cb);
    void parseLink(const QString& link, Callback cb);
    void sendLink(const QString& threadId, bool isGroup, const QString& text, const QJsonObject& preview,
                  Callback cb);
    void sendMention(const QString& groupId, const QString& text, const QJsonArray& mentions, Callback cb,
                     const QString& cliMsgId = QString(), const QJsonArray& styles = QJsonArray());
    static QString textPropertiesJson(const QJsonArray& styles);
    void sendDelivered(const QString& threadId, bool isGroup, const QJsonArray& msgs, Callback cb);
    void fetchQuickMessages(Callback cb);
    void createQuickMessage(const QString& keyword, const QString& title, Callback cb);
    void updateQuickMessage(qint64 itemId, const QString& keyword, const QString& title, Callback cb);
    void deleteQuickMessage(qint64 itemId, Callback cb);
    void getSettings(Callback cb);
    void updateSetting(const QString& type, int value, Callback cb);
    void fetchHiddenConversations(Callback cb);
    void setHiddenConversation(const QString& threadId, bool isGroup, bool hide, Callback cb);
    void updateHiddenConversPin(const QString& pinMd5, Callback cb);
    void resetHiddenConversPin(Callback cb);
    void fetchArchivedChats(Callback cb);
    void setArchivedChat(const QString& threadId, bool isGroup, bool archive, Callback cb);
    void fetchAutoDeleteChats(Callback cb);
    void setAutoDeleteChat(const QString& threadId, bool isGroup, qint64 ttl, Callback cb);
    void setGroupDeputy(const QString& groupId, const QString& memberId, bool add, Callback cb);
    void changeGroupOwner(const QString& groupId, const QString& memberId, Callback cb);
    void disperseGroup(const QString& groupId, Callback cb);
    void removeFriendAlias(const QString& friendId, Callback cb);

    void fetchGroupLink(const QString& groupId, Callback cb);
    void newGroupLink(const QString& groupId, Callback cb);
    void disableGroupLink(const QString& groupId, Callback cb);
    void fetchGroupLinkInfo(const QString& link, int memberPage, Callback cb);
    void joinGroupLink(const QString& link, Callback cb);
    void fetchPendingMembers(const QString& groupId, Callback cb);
    void reviewPendingMembers(const QString& groupId, const QStringList& memberIds, bool approve, Callback cb);
    void updateGroupSettings(const QString& groupId, const QJsonObject& setting, Callback cb);
    void fetchBlockedMembers(const QString& groupId, int page, int count, Callback cb);
    void setMembersBlocked(const QString& groupId, const QStringList& memberIds, bool blocked, Callback cb);
    void inviteToGroups(const QString& userId, const QStringList& groupIds, Callback cb);

    void connectRealtime();
    void disconnectRealtime();
    bool isRealtimeConnected() const { return m_realtimeReady; }
    void requestOldMessages(bool isGroup, const QString& lastMsgId = QString());
    void requestOldReactions(bool isGroup);
    QList<QPair<int, int>> serverQueues() const { return m_serverQueues; }

    static QList<QPair<int, int>> queuesFromAuth(const QJsonObject& auth);
    static QJsonObject nextPullPage(const QJsonObject& data, int cmd, int subCmd);
    static bool isFailoverError(int networkError, int httpStatus);

signals:
    void loggedIn();
    void loginFailed(const QString& reason);
    void loginUnavailable(const QString& reason);
    void realtimeStateChanged(bool connected);
    void realtimeReplaced();
    void messagesReceived(const QJsonArray& messages, bool isGroup, bool isOld);
    void reactionsReceived(const QJsonArray& reactions, bool isGroup);
    void typingReceived(const QString& threadId, const QString& userId, bool isGroup);
    void seenReceived(const QString& threadId, const QString& msgId, const QString& userId, bool isGroup);
    void readPointsReceived(const QJsonArray& clearUnreads);
    void friendEventReceived(const QString& action, const QJsonValue& data);
    void voipControlReceived(const QJsonObject& content);
    void mobileSyncControlReceived(const QJsonObject& content);
    void controlReceived(const QString& actType, const QString& act, const QJsonValue& data);

public:
    QStringList serviceUrls(const QString& service) const;
    void setLoginInfoForTesting(const QJsonObject& info) { m_loginInfo = info; }
    QJsonArray splitReactionMessages(const QJsonArray& msgs, bool isGroup);

private:
    QString serviceUrl(const QString& service) const;
    void sendApi(const QString& service, const QString& path, bool post, const QJsonObject& params, Callback cb,
                 const QHash<QString, QString>& extraQuery, int hostIndex);
    void pullServerQueues();
    void logRealtime(const QString& line) const;
    void handleControls(const QJsonArray& controls);
    void emitReactionArrays(const QJsonObject& data, bool users, bool groups);
    void emitMessageBuckets(const QJsonObject& data, bool isOld);
    QUrl makeUrl(const QString& base, const QList<QPair<QString, QString>>& query, bool withVersion = true) const;
    void applyHeaders(QNetworkRequest& req) const;
    void handleApiReply(QNetworkReply* reply, Callback cb);
    void postMultipartFile(const QString& url, const QJsonObject& params, const QByteArray& content, Callback cb);
    void requestServerInfo();
    void sendWs(int cmd, int subCmd, QJsonObject data, const QString& reqIdKey = QStringLiteral("req_id"));
    void onWsBinary(const QByteArray& frame);
    enum class UploadKind { Photo, File, Voice };
    void uploadChunks(const QString& threadId, bool isGroup, const QByteArray& content, const QString& fileName,
                      UploadKind kind, int width, int height, const QString& caption, Callback cb,
                      const QString& sendClientId = QString());
    int sharefileSetting(const char* key, int fallback) const;

    QNetworkAccessManager* m_nam{nullptr};
    SessionCookieJar* m_jar{nullptr};
    QString m_imei;
    QString m_userAgent;
    QString m_language{"vi"};
    QString m_secretKey;
    QString m_uid;
    QJsonObject m_loginInfo;
    QJsonObject m_serverSettings;
    QJsonObject m_extraVer;

    QWebSocket* m_ws{nullptr};
    QTimer m_pingTimer;
    QTimer m_pongWatchdog;
    QString m_cipherKey;
    bool m_realtimeReady{false};
    int m_wsUrlIndex{0};
    int m_wsRequestId{0};
    QList<QPair<int, int>> m_serverQueues;
    QHash<int, int> m_pullPages;
    QHash<QString, int> m_preferredHost;
    QHash<QString, std::function<void(const QJsonObject& fileData)>> m_uploadWaiters;
    QHash<QString, QJsonObject> m_earlyFileDone;
};

}
