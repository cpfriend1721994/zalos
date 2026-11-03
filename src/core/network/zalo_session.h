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

// Cookie jar that can be exported/imported for session persistence.
class SessionCookieJar : public QNetworkCookieJar {
    Q_OBJECT
public:
    using QNetworkCookieJar::QNetworkCookieJar;
    QList<QNetworkCookie> cookies() const { return allCookies(); }
    void replaceCookies(const QList<QNetworkCookie>& cookies) { setAllCookies(cookies); }
};

// Authenticated Zalo Web session: login handshake, encrypted REST calls and the realtime socket.
// Protocol reference: zca-js (src/apis/login.ts, utils.ts, apis/listen.ts).
class ZaloSession : public QObject {
    Q_OBJECT

public:
    static constexpr int kApiType = 30;
    static constexpr int kApiVersion = 685;
    static const QString kDefaultUserAgent;

    using Callback = std::function<void(bool ok, const QJsonValue& data, const QString& error, int errorCode)>;

    explicit ZaloSession(QNetworkAccessManager* nam, SessionCookieJar* jar, QObject* parent = nullptr);

    // Starts getLoginInfo + getServerInfo using cookies already present in the jar.
    void login(const QString& imei, const QString& userAgent, const QString& language);
    void reset();

    bool isReady() const { return !m_secretKey.isEmpty(); }
    QString uid() const { return m_uid; }
    QString imei() const { return m_imei; }
    QString userAgent() const { return m_userAgent; }
    QString send2meId() const { return m_loginInfo.value("send2me_id").toString(); }
    QJsonObject loginInfo() const { return m_loginInfo; }
    QString phonebookVersion() const;

    // Credentials persistence (cookies + imei + user agent), stored with 0600 permissions.
    bool saveCredentials(const QString& path) const;
    static bool loadCredentials(const QString& path, SessionCookieJar* jar, QString* imei, QString* userAgent);
    static void importCookies(SessionCookieJar* jar, const QJsonArray& cookies);

    // Encrypted REST helpers. `service` is a key of zpw_service_map_v3 (e.g. "profile", "group").
    // `alwaysSendParams`: send `params` even when empty (some endpoints require the encrypted "{}").
    void apiGet(const QString& service, const QString& path, const QJsonObject& params, Callback cb,
                bool alwaysSendParams = false);
    void apiPost(const QString& service, const QString& path, const QJsonObject& params, Callback cb,
                 const QHash<QString, QString>& extraQuery = {});

    // High level API wrappers.
    void fetchAccountInfo(Callback cb);
    void fetchFriends(Callback cb);
    void fetchGroupIds(Callback cb);
    void fetchGroupInfo(const QStringList& groupIds, Callback cb);
    void fetchPinnedConversations(Callback cb);
    void fetchUserInfo(const QStringList& userIds, Callback cb);
    void fetchGroupHistory(const QString& groupId, int count, Callback cb);
    // Recent thread previews + read points (Zalo PC getLastMessagesForPreview):
    // conversation:/api/preloadconvers/get-last-msgs. Empty `threadIds` asks for every recent thread.
    void fetchLastMessages(const QStringList& threadIds, const QSet<QString>& groupIds, Callback cb);
    // Cloud message history for groups and My Cloud (group_cloud_message:/api/cm/getoldv2).
    // `beforeMsgId` is the oldest numeric id already shown; empty starts from the newest message.
    void fetchCloudHistory(const QString& threadId, bool isMyCloud, const QString& beforeMsgId, int count,
                           Callback cb);
    // `cliMsgId` lets the caller bridge its local echo to the server copy (it comes back in TMessage).
    // `styles` [{start, len, st}] go out as textProperties {styles, ver: 0} (rich text, zca-js TextStyle).
    void sendText(const QString& threadId, bool isGroup, const QString& text, Callback cb,
                  const QString& cliMsgId = QString(), const QJsonArray& styles = QJsonArray());
    // Reply to `quoted` (raw TMessage) via /api/message/quote or /api/group/quote.
    void sendQuote(const QString& threadId, bool isGroup, const QString& text, const QJsonObject& quoted, Callback cb,
                   const QString& cliMsgId = QString(), const QJsonArray& styles = QJsonArray());
    // Conversation actions (Zalo PC: unread marks, delete conversation, mute with a duration).
    void setUnreadMark(const QString& threadId, bool isGroup, bool unread, Callback cb);
    void deleteConversation(const QString& threadId, bool isGroup, const QJsonObject& lastMsg, Callback cb);
    // `seconds` < 0 mutes until turned back on; 0 unmutes.
    void setMute(const QString& threadId, bool isGroup, qint64 seconds, Callback cb);
    void findUserByPhone(const QString& phone, Callback cb);
    void sendFriendRequest(const QString& userId, const QString& message, Callback cb);
    void createGroup(const QString& name, const QStringList& members, Callback cb);

    // Message actions (zca-js addReaction / undo / deleteMessage / sendSeenEvent / sendTypingEvent).
    // `msg` is the raw TMessage of the target as received from the server.
    void addReaction(const QString& threadId, bool isGroup, const QJsonObject& msg, const QString& rIcon, int rType,
                     Callback cb);
    void undoMessage(const QString& threadId, bool isGroup, const QJsonObject& msg, Callback cb);
    void deleteMessage(const QString& threadId, bool isGroup, const QJsonObject& msg, bool onlyMe, Callback cb);
    void sendSeen(const QString& threadId, bool isGroup, const QJsonArray& msgs, Callback cb);
    void sendTyping(const QString& threadId, bool isGroup, Callback cb);

    // Attachments (zca-js uploadAttachment + sendMessage.handleAttachment). Images go through
    // photo_original/upload; other files through asyncfile/upload and finish on socket cmd 601.
    void sendAttachment(const QString& threadId, bool isGroup, const QString& filePath, const QString& caption,
                        Callback cb);
    // Voice message (Zalo PC uploadMainType Voice): chunks to /api/{message|group}/voice/upload with fileType
    // "aac", the URLs arrive on socket 601 file_done ({aac, amr?}), then /api/{message|group}/forward msgType 3
    // with msgInfo {voiceUrl, m4aUrl, fileSize} (zca-js sendVoice). `filePath` holds ADTS AAC.
    // `clientId` becomes the message's cliMsgId (bridges the local echo to the server copy).
    void sendVoice(const QString& threadId, bool isGroup, const QString& filePath, const QString& clientId, Callback cb);
    void sendSticker(const QString& threadId, bool isGroup, qint64 stickerId, qint64 cateId, int type, Callback cb);
    // Avatars (Zalo PC updateProfileAvatar / updateAvatar, zca-js changeAccountAvatar / changeGroupAvatar):
    // multipart `fileContent` (JPEG, at most 1080 px) to <file>/api/profile/upavatar or /api/group/upavatar,
    // params in the query. The cover photo has no desktop API (phone app only).
    void uploadProfileAvatar(const QString& imagePath, Callback cb);
    void uploadGroupAvatar(const QString& groupId, const QString& imagePath, Callback cb);
    void searchStickers(const QString& keyword, Callback cb);
    void fetchStickerDetail(qint64 stickerId, Callback cb);
    void fetchStickerCategories(Callback cb);
    void fetchPersonalStickerList(Callback cb);
    void fetchCategoryStickers(qint64 cateId, Callback cb);
    void searchStickerIds(const QString& keyword, int limit, Callback cb);

    // Personal To-Do (Zalo PC: /api/board/personal/*, service "boards").
    void todoList(const QJsonObject& query, int offset, int limit, Callback cb);
    void todoCreate(const QString& content, qint64 dueDate, const QStringList& assignees, Callback cb);
    void todoUpdateStatus(const QString& id, int status, Callback cb);
    void todoDelete(const QString& id, Callback cb);
    // One line in logs/realtime.log (feature diagnostics such as To-Do failures).
    void logDiagnostic(const QString& line) const { logRealtime(line); }

    // Presence, labels, friend requests.
    void fetchFriendOnlines(Callback cb);
    void fetchLastOnline(const QString& userId, Callback cb);
    void fetchLabels(Callback cb);
    // Replaces the whole label list (zca-js updateLabels): labelData = JSON array string, version = the one last
    // received (error 230 = someone else changed the labels first). Reply {labelData (string), version, lastUpdateTime}.
    void updateLabels(const QJsonArray& labelData, qint64 version, Callback cb);
    // QR codes of users (zca-js getQR): {uid: qrCodeUrl}; needed by contact cards.
    void fetchUserQr(const QStringList& userIds, Callback cb);
    void fetchFriendRecommendations(Callback cb);
    void acceptFriendRequest(const QString& userId, Callback cb);
    void rejectFriendRequest(const QString& userId, Callback cb);

    // Mobile history sync (Zalo PC SyncMessageController V1; see 01-business-domain §7).
    // pull_mobile_msg asks the phone for a backup encrypted to `publicKey` (base64 SPKI DER RSA-2048).
    void pullMobileMessages(const QString& pcName, const QString& publicKey, qint64 fromSeqId, qint64 minSeqId,
                            bool isRetry, Callback cb);
    void cancelPullMobileMessages(const QString& pcName, const QString& publicKey, Callback cb);
    // Plain (mobile/backup) ids -> the noised ids every other API uses: {fids:[...], gids:[...]}.
    // Cross-device state (refetched on alias/mark_unread controls and after reconnect).
    void fetchAliasList(int page, int count, Callback cb);
    void fetchUnreadMarks(Callback cb);
    void fetchNoisedIds(const QStringList& userIds, const QStringList& groupIds, Callback cb);
    // Plain GET with the session cookies (backup download). Caller owns the reply.
    QNetworkReply* getWithCookies(const QUrl& url);

    // Group polls (zca-js createPoll, votePoll, lockPoll).
    void createPoll(const QString& groupId, const QString& question, const QStringList& options,
                    bool allowMultiChoices, Callback cb);
    void votePoll(qint64 pollId, const QList<int>& optionIds, Callback cb);
    void lockPoll(qint64 pollId, Callback cb);

    // Group management (zca-js updateinfo, invite/v2, kick, leave).
    void changeGroupName(const QString& groupId, const QString& newName, Callback cb);
    void addUserToGroup(const QString& groupId, const QStringList& memberIds, Callback cb);
    void removeUserFromGroup(const QString& groupId, const QString& memberId, Callback cb);
    void leaveGroup(const QString& groupId, bool silent, Callback cb);

    // Friend alias, contact cards, and message pinning.
    void changeFriendAlias(const QString& friendId, const QString& alias, Callback cb);
    // zca-js sendCard: forward msgType 6, msgInfo {contactUid, qrCodeUrl, phone?}; clientId bridges the echo.
    void sendContactCard(const QString& threadId, bool isGroup, const QString& contactId, const QString& qrCodeUrl,
                         const QString& phone, const QString& cliMsgId, Callback cb);
    // Pinned messages (Zalo PC PinTopic). Group board: pin/list {groupId, boardVersion} → {items, boardVersion,
    // pinLimit}; topic/createv2 (pinAct 1); unpinv2 / multi_unpin; pin/update (reorder). Friend board (1-1):
    // friendboard/list {conversationId, version} → {data: [topic JSON strings], version}; create / multi_unpin /
    // reorder with {conversationId, topic(s), version, lang}. Topics to unpin/reorder: [{topicId, topicType}].
    static int clientMessageType(const QString& msgType);
    void fetchGroupPins(const QString& groupId, qint64 boardVersion, Callback cb);
    void createGroupPin(const QString& groupId, const QJsonObject& topic, Callback cb);
    void unpinGroupPins(const QString& groupId, const QJsonArray& topics, qint64 boardVersion, Callback cb);
    void reorderGroupPins(const QString& groupId, const QJsonArray& topics, qint64 boardVersion, Callback cb);
    void fetchFriendPins(const QString& userId, qint64 version, Callback cb);
    void createFriendPin(const QString& userId, const QJsonObject& topic, qint64 version, Callback cb);
    void unpinFriendPins(const QString& userId, const QJsonArray& topics, qint64 version, Callback cb);
    void reorderFriendPins(const QString& userId, const QJsonArray& topics, qint64 version, Callback cb);

    // Profile updates and relationship management (zca-js updateProfile, updateProfileBio, blockUser, removeFriend).
    void updateProfile(const QString& name, int gender, const QString& dob, Callback cb);
    void updateProfileBio(const QString& status, Callback cb);
    void blockUser(const QString& userId, Callback cb);
    void unblockUser(const QString& userId, Callback cb);
    void removeFriend(const QString& userId, Callback cb);

    // Batch 1 (zca-js getGroupMembersInfo, getPollDetail, parseLink, sendLink, sendDeliveredEvent,
    // getQuickMessageList, add/removeGroupDeputy, changeGroupOwner, disperseGroup, removeFriendAlias).
    void fetchGroupMembersInfo(const QStringList& memberIds, Callback cb);
    void fetchPollDetail(qint64 pollId, Callback cb);
    void parseLink(const QString& link, Callback cb);
    void sendLink(const QString& threadId, bool isGroup, const QString& text, const QJsonObject& preview,
                  Callback cb);
    // mentions: [{pos, uid, len}] (uid "-1" = @All); groups only, plain sendText otherwise.
    void sendMention(const QString& groupId, const QString& text, const QJsonArray& mentions, Callback cb,
                     const QString& cliMsgId = QString(), const QJsonArray& styles = QJsonArray());
    static QString textPropertiesJson(const QJsonArray& styles);
    void sendDelivered(const QString& threadId, bool isGroup, const QJsonArray& msgs, Callback cb);
    void fetchQuickMessages(Callback cb);
    void setGroupDeputy(const QString& groupId, const QString& memberId, bool add, Callback cb);
    void changeGroupOwner(const QString& groupId, const QString& memberId, Callback cb);
    void disperseGroup(const QString& groupId, Callback cb);
    void removeFriendAlias(const QString& friendId, Callback cb);

    // Group management (zca-js enable/disableGroupLink, getGroupLinkDetail, getGroupLinkInfo, joinGroupLink,
    // getPendingGroupMembers, reviewPendingMemberRequest, updateGroupSettings, add/remove/getGroupBlockedMember,
    // inviteUserToGroups). All GET on the "group" service with encrypted params.
    void fetchGroupLink(const QString& groupId, Callback cb);          // {link?, expiration_date?, enabled}
    void newGroupLink(const QString& groupId, Callback cb);            // {link, expiration_date, enabled}
    void disableGroupLink(const QString& groupId, Callback cb);
    void fetchGroupLinkInfo(const QString& link, int memberPage, Callback cb);
    void joinGroupLink(const QString& link, Callback cb);
    void fetchPendingMembers(const QString& groupId, Callback cb);     // {time, users:[{uid, dpn, avatar}]}
    void reviewPendingMembers(const QString& groupId, const QStringList& memberIds, bool approve, Callback cb);
    // `setting` uses the GroupSetting keys as 0/1 ints (blockName, signAdminMsg, setTopicOnly, enableMsgHistory,
    // joinAppr, lockCreatePost, lockCreatePoll, lockSendMsg, lockViewMember, bannFeature, dirtyMedia, banDuration).
    void updateGroupSettings(const QString& groupId, const QJsonObject& setting, Callback cb);
    void fetchBlockedMembers(const QString& groupId, int page, int count, Callback cb);
    void setMembersBlocked(const QString& groupId, const QStringList& memberIds, bool blocked, Callback cb);
    void inviteToGroups(const QString& userId, const QStringList& groupIds, Callback cb);

    // Realtime websocket.
    void connectRealtime();
    void disconnectRealtime();
    bool isRealtimeConnected() const { return m_realtimeReady; }
    void requestOldMessages(bool isGroup, const QString& lastMsgId = QString());
    void requestOldReactions(bool isGroup);
    QList<QPair<int, int>> serverQueues() const { return m_serverQueues; }

    // Protocol helpers, public for unit tests.
    // Queues listed by the auth frame (data.ctrl_qCmds + data.qCmds), de-duplicated, in server order.
    static QList<QPair<int, int>> queuesFromAuth(const QJsonObject& auth);
    // Follow-up pull for a reply whose data.more == 1; empty object when the queue is drained.
    static QJsonObject nextPullPage(const QJsonObject& data, int cmd, int subCmd);
    // True for transport failures / missing routes where another service host may answer.
    static bool isFailoverError(int networkError, int httpStatus);

signals:
    void loggedIn();
    // The server rejected the session (error_code != 0): saved credentials are no longer valid.
    void loginFailed(const QString& reason);
    // The server could not be reached or answered 5xx/429: credentials are kept and the caller may retry.
    void loginUnavailable(const QString& reason);
    void realtimeStateChanged(bool connected);
    void realtimeReplaced(); // Another Zalo Web/PC client took over the socket.
    // `messages` are raw TMessage objects; `isOld` marks history replies (cmd 510/511).
    void messagesReceived(const QJsonArray& messages, bool isGroup, bool isOld);
    // Raw TReaction objects (cmd 612 live, 610/611 history).
    void reactionsReceived(const QJsonArray& reactions, bool isGroup);
    // Someone is typing: threadId is the user id (1-1) or group id.
    void typingReceived(const QString& threadId, const QString& userId, bool isGroup);
    // The peer has seen our messages up to `msgId` (1-1) / group seen by `userId`.
    void seenReceived(const QString& threadId, const QString& msgId, const QString& userId, bool isGroup);
    // Read points from another device of ours (clearUnreads: {idTo, lastMsgId, isGroup}).
    void readPointsReceived(const QJsonArray& clearUnreads);
    // Friend list changed on the server (request received / accepted / removed).
    void friendEventReceived(const QString& action, const QJsonValue& data);
    // Call signalling control (cmd 601, act_type "voip"): incoming call, answer, hang-up, ...
    void voipControlReceived(const QJsonObject& content);
    // Mobile message sync control (cmd 601, act_type "syncmsgmb" or "transfer_msg").
    void mobileSyncControlReceived(const QJsonObject& content);
    // Any other cmd 601/603 control (act_type group, alias, profile, mute, pinned_convers, label_convers,
    // mark_unread, ...): changes made on the phone/web. `data` is already parsed when it was a JSON string.
    void controlReceived(const QString& actType, const QString& act, const QJsonValue& data);

public:
    // Hosts of a zpw_service_map_v3 service (with Zalo PC fallbacks: "boards" → "board" → board-wpa default).
    QStringList serviceUrls(const QString& service) const;
    void setLoginInfoForTesting(const QJsonObject& info) { m_loginInfo = info; }
    // Message lists: "chat.reaction" items become reactionsReceived; the rest is returned.
    QJsonArray splitReactionMessages(const QJsonArray& msgs, bool isGroup);

private:
    QString serviceUrl(const QString& service) const;
    // Sends one encrypted REST request, retrying the next zpw_service_map_v3 host on failover errors.
    void sendApi(const QString& service, const QString& path, bool post, const QJsonObject& params, Callback cb,
                 const QHash<QString, QString>& extraQuery, int hostIndex);
    void pullServerQueues();
    void logRealtime(const QString& line) const;
    void handleControls(const QJsonArray& controls);
    // reacts / reactGroups arrays of any socket payload → reactionsReceived.
    void emitReactionArrays(const QJsonObject& data, bool users, bool groups);
    void emitMessageBuckets(const QJsonObject& data, bool isOld);
    QUrl makeUrl(const QString& base, const QList<QPair<QString, QString>>& query, bool withVersion = true) const;
    void applyHeaders(QNetworkRequest& req) const;
    void handleApiReply(QNetworkReply* reply, Callback cb);
    void postMultipartFile(const QString& url, const QJsonObject& params, const QByteArray& content, Callback cb);
    void requestServerInfo();
    // `reqIdKey` is "req_id" (Zalo Web) or "reqId" (Zalo PC queue pulls); empty sends no request id.
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
    QHash<int, int> m_pullPages; // (cmd << 8 | subCmd) -> follow-up pages sent this connection
    QHash<QString, int> m_preferredHost; // service -> index of the host that last answered
    // Pending asyncfile uploads: fileId -> continuation receiving the final file URL.
    QHash<QString, std::function<void(const QJsonObject& fileData)>> m_uploadWaiters; // file_done data by fileId
};

} // namespace Zalos::Core::Network
