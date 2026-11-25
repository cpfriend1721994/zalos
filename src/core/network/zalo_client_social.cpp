#include "zalo_client_internal.h"

namespace Zalos::Core::Network {

using namespace Detail;

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
    const bool isGif = ext == "gif";
    const bool isImage = isGif || ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp";
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
    if (isGif) local.extra.insert(QStringLiteral("animatedUrl"), local.mediaUrl);
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
                QString reason = friendlyError(error, code);
                if (code == -2) reason = L("tệp quá lớn", "the file is too large");
                else if (code == -3) reason = L("định dạng tệp bị Zalo chặn", "Zalo blocks this file type");
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
    local.text = L("[Sticker]", "[Sticker]");
    local.mediaUrl = stickerThumb(QString::number(stickerId));
    local.ts = now;
    local.timeStr = QDateTime::fromMSecsSinceEpoch(now).toString("HH:mm");
    local.isMine = true;
    local.senderId = m_myUid;
    local.senderName = m_userName;
    local.status = QStringLiteral("sending");
    m_messageModel->mergeMessages(convId, {local});
    m_conversationModel->recordMessage(convId, L("[Sticker]", "[Sticker]"), L("Bạn: ", "You: "), now, false);

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
                emit notice(L("Không gửi được sticker: ", "Couldn't send the sticker: ") + friendlyError(error, code));
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
        sendForward(tid, msgId, false);
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

void ZaloClient::loadSystemSoundsAsync() {
    if (m_systemSoundsLoaded || m_systemSoundsLoading) return;
    m_systemSoundsLoading = true;
    QPointer<ZaloClient> self(this);
    QThreadPool::globalInstance()->start([self]() {
        const QVariantList sounds = Platform::SoundEngine::systemSounds();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, sounds]() {
            if (!self) return;
            self->m_cachedSystemSounds = sounds;
            self->m_systemSoundsLoaded = true;
            self->m_systemSoundsLoading = false;
            emit self->systemSoundsChanged();
        }, Qt::QueuedConnection);
    });
}

bool ZaloClient::isMediaTooLarge(const QString& url) const {
    return !url.isEmpty() && Platform::AvatarImageProvider::isTooLarge(url);
}

QVariantList ZaloClient::reactionList() const {
    QVariantList out;
    for (const auto& r : kReactions)
        out.append(QVariantMap{{"key", QString::fromLatin1(r.key)}, {"emoji", QString::fromUtf8(r.emoji)}});
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

void ZaloClient::handleReactions(const QJsonArray& reactions, bool isGroup, bool live) {
    if (reactions.isEmpty()) return;
    if (m_offlineStore) m_offlineStore->beginBatch();
    QHash<QString, QString> dirty;
    bool unreadChanged = false;
    QHash<QString, QPair<QString, QString>> toNotify;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
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
            const QString cliMsgId = idOf(tObj.value("cMsgID"));
            const bool usedCliId = msgId.isEmpty() || msgId == QLatin1String("0");
            if (usedCliId) msgId = cliMsgId;
            if (msgId.isEmpty() || msgId == QLatin1String("0")) continue;
            const QString convId = m_messageThread.value(msgId, thread == "0" ? m_myUid : thread);
            if (usedCliId) {
                QString resolved;
                for (const auto& item : m_messageModel->threadOf(convId)) {
                    if (item.cliMsgId == cliMsgId) {
                        resolved = item.id;
                        break;
                    }
                }
                if (resolved.isEmpty() && m_offlineStore) {
                    resolved = m_offlineStore->resolveMessageIdByCliId(convId, cliMsgId);
                }
                if (!resolved.isEmpty()) msgId = resolved;
            }
            if (removal) {
                clearReactionsOf(msgId, uidFrom, ts);
                if (!self && m_offlineStore && m_offlineStore->removeUnreadReactions(msgId, uidFrom)) unreadChanged = true;
            } else {
                const QString id = eventId.isEmpty()
                    ? QStringLiteral("anon:%1:%2:%3").arg(uidFrom, icon).arg(ts) : eventId;
                if (self) dropPendingReaction(msgId, icon);
                const bool fresh = addReactionEvent(msgId, uidFrom, icon, id, ts);
                const int ownership = !self ? messageOwnership(convId, msgId) : 0;
                const bool canNotifyOwn = (ownership == 1) || (!isGroup && ownership == -1);
                if (fresh && !self && m_offlineStore && !m_hiddenThreads.contains(convId) && canNotifyOwn
                    && m_offlineStore->addUnreadReaction(convId, msgId, id, uidFrom, icon, ts)) {
                    unreadChanged = true;
                    if (live && (ts <= 0 || now - ts < kReactionNotifyWindowMs)) toNotify.insert(msgId, {convId, uidFrom + QLatin1Char('|') + icon});
                }
            }
            dirty.insert(msgId, convId);
        }
    }
    if (m_offlineStore) m_offlineStore->endBatch();
    for (auto it = dirty.cbegin(); it != dirty.cend(); ++it) {
        refreshReactionSummary(it.value(), it.key());
    }
    for (auto it = toNotify.cbegin(); it != toNotify.cend(); ++it) {
        const QString who = it.value().second.section(QLatin1Char('|'), 0, 0);
        const QString icon = it.value().second.section(QLatin1Char('|'), 1);
        showReactionNotification(it.value().first, it.key(), who, icon);
    }
    if (unreadChanged) refreshUnreadReactions();
}

int ZaloClient::messageOwnership(const QString& convId, const QString& msgId) const {
    for (const auto& item : m_messageModel->threadOf(convId)) {
        if (item.id == msgId || (!item.cliMsgId.isEmpty() && item.cliMsgId == msgId)) {
            return item.isMine ? 1 : 0;
        }
    }
    return m_offlineStore ? m_offlineStore->isMineMessage(msgId) : -1;
}

QString ZaloClient::messageSnippet(const QString& convId, const QString& msgId) const {
    for (const auto& item : m_messageModel->threadOf(convId)) {
        if (item.id == msgId) return item.text.simplified().left(80);
    }
    return m_offlineStore ? m_offlineStore->messageText(msgId).simplified().left(80) : QString();
}

void ZaloClient::showReactionNotification(const QString& convId, const QString& msgId, const QString& uid,
                                          const QString& icon) {
    if (!m_notifyReactions || convId.isEmpty() || isThreadMuted(convId) || m_hiddenThreads.contains(convId)) return;
    const auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    const bool focused = app && app->focusWindow();
    const bool activeHere = focused && convId == m_currentConvId;
    if (activeHere) return;
    const bool callRinging = m_calls
        && (m_calls->phase() == QLatin1String("incoming") || m_calls->phase() == QLatin1String("outgoing"));
    if (m_notifySound && !callRinging) m_sound->play("notification", m_selectedNotiSound);
    const bool hide = m_antiPeeking || m_appLock->locked();
    const bool group = isGroupThread(convId);
    const QString who = displayNameOf(uid);
    Platform::DesktopNotifications::Request request;
    if (hide) {
        request.title = QStringLiteral("Zalos");
        request.body = L("Bạn có tương tác mới", "You have a new interaction");
    } else {
        request.title = group ? m_groups.value(convId).name : who;
        if (request.title.isEmpty()) request.title = QStringLiteral("Zalos");
        const QString snippet = messageSnippet(convId, msgId);
        const QString emoji = reactionEmoji(icon);
        request.body = (who.isEmpty() ? L("Có người", "Someone") : who)
                       + L(" đã bày tỏ cảm xúc ", " reacted ") + emoji
                       + (snippet.isEmpty() ? L(" với tin nhắn bạn gửi", " to your message")
                                            : L(" với tin nhắn: «", " to: «") + snippet + QStringLiteral("»"));
    }
    request.category = QStringLiteral("im.received");
    request.actions = {{QStringLiteral("default"), L("Mở", "Open")}};
    m_notifications->show(QStringLiteral("reaction:") + convId + QLatin1Char(':') + msgId, request);
}

void ZaloClient::refreshUnreadReactions() {
    QStringList ids;
    QString icon;
    if (m_offlineStore && !m_currentConvId.isEmpty()) {
        for (const auto& r : m_offlineStore->unreadReactionsOf(m_currentConvId)) {
            if (!ids.contains(r.msgId)) ids.append(r.msgId);
            if (ids.size() == 1 && r.msgId == ids.first()) icon = r.icon;
        }
    }
    const QString emoji = icon.isEmpty() ? QString() : reactionEmoji(icon);
    if (ids == m_currentUnreadReactionIds && emoji == m_currentUnreadReactionIcon) return;
    m_currentUnreadReactionIds = ids;
    m_currentUnreadReactionIcon = emoji;
    emit unreadReactionsChanged();
}

QString ZaloClient::nextUnreadReactionMsgId() const {
    return m_currentUnreadReactionIds.isEmpty() ? QString() : m_currentUnreadReactionIds.first();
}

void ZaloClient::markReactionsSeen(const QString& msgId) {
    if (msgId.isEmpty() || !m_offlineStore || m_currentConvId.isEmpty()) return;
    QStringList eventIds;
    for (const auto& r : m_offlineStore->unreadReactionsOf(m_currentConvId)) {
        if (r.msgId == msgId) eventIds.append(r.eventId);
    }
    if (!m_offlineStore->removeUnreadReactions(msgId)) return;
    m_notifications->close(QStringLiteral("reaction:") + m_currentConvId + QLatin1Char(':') + msgId);
    refreshUnreadReactions();
    if (!m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
    QStringList serverIds;
    for (const QString& e : std::as_const(eventIds)) {
        if (!e.startsWith(QLatin1String("anon:")) && !e.startsWith(QLatin1String("legacy:"))) serverIds.append(e);
    }
    if (!serverIds.isEmpty()) {
        m_apiClient->session()->sendReactSeen(m_currentConvId, isGroupThread(m_currentConvId), serverIds,
                                              [](bool, const QJsonValue&, const QString&, int) {});
    }
}

void ZaloClient::setNotifyReactions(bool on) {
    if (on == m_notifyReactions) return;
    m_notifyReactions = on;
    saveSession();
    emit settingsChanged();
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

bool ZaloClient::addReactionEvent(const QString& msgId, const QString& uid, const QString& icon, const QString& eventId,
                                  qint64 ts) {
    if (ts > 0 && ts < m_reactionClearedTs.value(msgId + QLatin1Char('|') + uid, 0)) return false;
    auto& events = eventsOf(msgId);
    for (const auto& e : std::as_const(events)) {
        if (e.eventId == eventId) return false;
    }
    events.append({uid, icon, eventId, ts});
    m_reactedIds.insert(msgId);
    if (m_offlineStore) m_offlineStore->addReactionEvent(msgId, uid, icon, eventId, ts);
    return true;
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
    const qint64 clearedAt = clearTs > 0 ? clearTs : QDateTime::currentMSecsSinceEpoch();
    qint64& mark = m_reactionClearedTs[msgId + QLatin1Char('|') + uid];
    mark = qMax(mark, clearedAt);
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
    auto removeLocal = [this, convId, msgId]() {
        m_messageModel->removeMessage(convId, msgId);
        m_rawMessages.remove(msgId);
        m_messageThread.remove(msgId);
        m_reactions.remove(msgId);
        m_reactedIds.remove(msgId);
        if (m_offlineStore) m_offlineStore->deleteMessage(msgId);
        refreshUnreadReactions();
    };
    if (raw.isEmpty() || !m_isLoggedIn) {
        removeLocal();
        return;
    }
    m_apiClient->session()->deleteMessage(convId, isGroupThread(convId), raw, true,
        [this, removeLocal](bool ok, const QJsonValue&, const QString& error, int) {
            if (!ok) {
                emit notice(L("Không xóa được tin nhắn: ", "Couldn't delete the message: ") + friendlyError(error));
                return;
            }
            removeLocal();
        });
}

QStringList ZaloClient::searchConversationMessages(const QString& query) {
    QStringList ids;
    const QString convId = m_currentConvId;
    if (convId.isEmpty() || query.trimmed().isEmpty()) return ids;
    QSet<QString> seen;
    QHash<QString, qint64> tsOf;
    if (cacheUsable()) {
        const auto hits = m_offlineStore->searchMessages(query, convId, 500);
        for (auto it = hits.crbegin(); it != hits.crend(); ++it) {
            if (!seen.contains(it->msgId)) {
                seen.insert(it->msgId);
                ids << it->msgId;
                tsOf.insert(it->msgId, it->ts);
            }
        }
    }
    const QList<int> rows = m_messageModel->searchMessages(query);
    QStringList extra;
    for (int row : rows) {
        const QModelIndex idx = m_messageModel->index(row, 0);
        const QString id = m_messageModel->data(idx, Models::MessageModel::IdRole).toString();
        if (!id.isEmpty() && !seen.contains(id)) {
            seen.insert(id);
            extra << id;
            tsOf.insert(id, m_messageModel->data(idx, Models::MessageModel::TimestampRole).toLongLong());
        }
    }
    if (extra.isEmpty()) return ids;
    ids << extra;
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
    if (userId == m_myUid) {
        if (isGroup && !threadId.isEmpty() && !msgId.isEmpty())
            applyReadPoints({QJsonObject{{"idTo", threadId}, {"lastMsgId", msgId}, {"isGroup", 1}}});
        return;
    }
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
    if (m_seenSent.size() > 5000) m_seenSent.clear();
    QJsonArray unseen;
    const auto& thread = m_messageModel->threadOf(convId);
    for (auto it = thread.crbegin(); it != thread.crend(); ++it) {
        if (unseen.size() >= 50) break;
        if (it->isMine || it->isRecalled || m_seenSent.contains(it->id)) continue;
        if (it->senderId == QLatin1String("0") || it->senderId == m_myUid) continue;
        bool ok = false;
        const qulonglong idNum = it->id.toULongLong(&ok);
        if (!ok || idNum == 0) continue;
        if (!m_rawMessages.isEmpty()) {
            const QJsonObject raw = m_rawMessages.value(it->id);
            if (!raw.isEmpty()) {
                unseen.append(raw);
                m_seenSent.insert(it->id);
                continue;
            }
        }
        QJsonObject item{
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
        unseen.append(item);
        m_seenSent.insert(it->id);
    }
    if (unseen.isEmpty()) return;
    m_apiClient->session()->sendSeen(convId, isGroup, unseen, [](bool, const QJsonValue&, const QString&, int) {});
}

void ZaloClient::refreshPresence() {
    if (!m_isLoggedIn || !m_windowVisible) return;
    m_apiClient->session()->fetchFriendOnlines([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok || !m_windowVisible) return;
        QSet<QString> online;
        for (const QJsonValue& v : data.toObject().value("onlines").toArray()) {
            online.insert(idOf(v.toObject().value("userId")));
        }
        if (m_onlineIds != online) {
            m_onlineIds = online;
            m_conversationModel->setOnlineIds(online);
            m_contactModel->setOnlineIds(online);
            emit conversationStatusChanged();
        }

        if (!m_currentConvId.isEmpty() && !currentConversationIsGroup() && !m_onlineIds.contains(m_currentConvId)) {
            m_apiClient->session()->fetchLastOnline(m_currentConvId, [this, convId = m_currentConvId](bool ok2, const QJsonValue& data2, const QString&, int) {
                if (!ok2 || convId != m_currentConvId) return;
                const QJsonObject obj = data2.toObject();
                if (!obj.value("settings").toObject().value("show_online_status").toBool(true)) return;
                qint64 ts = obj.value("lastOnline").toVariant().toLongLong();
                if (ts > 0 && ts < 10000000000LL) ts *= 1000;
                const qint64 nextLast = ts > 0 && ts < kMinPlausibleEpochMs ? -1 : ts;
                if (m_currentLastOnline != nextLast) {
                    m_currentLastOnline = nextLast;
                    emit conversationStatusChanged();
                }
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

bool ZaloClient::currentConversationIsTyping() const {
    if (m_currentConvId.isEmpty() || currentConversationIsCloud()) return false;
    return m_typingUntil.contains(m_currentConvId);
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

void ZaloClient::noticeCallUnavailable(const QString& reason) {
    const QString why = reason == QLatin1String("engine_missing")
        ? L("thiếu thành phần gọi đi kèm Zalos (hãy cài lại Zalos)",
            "a bundled component needed for calls is missing (reinstall Zalos)")
        : L("không tạo được khóa truyền tin nội bộ cho cuộc gọi", "the local call transport key could not be created");
    emit notice(L("Chưa gọi được: ", "Calls unavailable: ") + why);
}

void ZaloClient::startVoiceCall() {
    if (m_currentConvId.isEmpty() || currentConversationIsGroup() || !currentConversationCanCall()) return;
    m_calls->refreshAvailability();
    if (!m_calls->isAvailable()) {
        noticeCallUnavailable(m_calls->unavailableReason());
        return;
    }
    m_calls->startVoiceCall(m_currentConvId, m_currentConvName, m_currentConvAvatar);
}

void ZaloClient::startVideoCall() {
    if (m_currentConvId.isEmpty() || currentConversationIsGroup() || !currentConversationCanCall()) return;
    m_calls->refreshAvailability();
    if (!m_calls->isAvailable()) {
        noticeCallUnavailable(m_calls->unavailableReason());
        return;
    }
    m_calls->startVideoCall(m_currentConvId, m_currentConvName, m_currentConvAvatar);
}

void ZaloClient::startGroupCall(bool isVideo) {
    if (m_currentConvId.isEmpty() || !currentConversationIsGroup()) return;
    m_calls->refreshAvailability();
    if (!m_calls->isAvailable()) {
        noticeCallUnavailable(m_calls->unavailableReason());
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
        emit updateCheckChanged();
        emit composerStateChanged();
    }
}

void ZaloClient::refreshGroupInvites() {
    if (!m_isLoggedIn || !m_apiClient->session()) return;
    m_apiClient->session()->fetchGroupInviteBoxList(0, 20, [this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        const QJsonArray arr = data.toObject().value("invitations").toArray();
        QVariantList out;
        for (const QJsonValue& v : arr) {
            const QJsonObject inv = v.toObject();
            const QJsonObject g = inv.value("groupInfo").toObject();
            const QJsonObject inviter = inv.value("inviterInfo").toObject();
            const QString gid = g.value("groupId").toString(inv.value("grid").toString());
            if (gid.isEmpty()) continue;
            out.append(QVariantMap{
                {"groupId", gid},
                {"name", g.value("name").toString(gid)},
                {"avatar", g.value("avt").toString()},
                {"memberCount", g.value("totalMember").toInt()},
                {"inviterName", pickName(inviter)},
                {"expiredTs", inv.value("expiredTs").toString()},
            });
        }
        m_groupInvites = out;
        emit groupInvitesChanged();
    });
}

void ZaloClient::joinGroupInvite(const QString& groupId) {
    if (!m_isLoggedIn || !m_apiClient->session() || groupId.isEmpty()) return;
    m_apiClient->session()->joinGroupInviteBox(groupId, [this, groupId](bool ok, const QJsonValue&, const QString& err, int code) {
        if (!ok) {
            emit notice(L("Không thể tham gia nhóm: ", "Could not join group: ") + friendlyError(err, code));
            return;
        }
        for (int i = m_groupInvites.size() - 1; i >= 0; --i) {
            if (m_groupInvites.at(i).toMap().value("groupId").toString() == groupId) {
                m_groupInvites.removeAt(i);
            }
        }
        emit groupInvitesChanged();
        loadAccountData();
        emit notice(L("Đã tham gia nhóm", "Joined group"));
    });
}

void ZaloClient::deleteGroupInvite(const QString& groupId, bool blockFuture) {
    if (!m_isLoggedIn || !m_apiClient->session() || groupId.isEmpty()) return;
    m_apiClient->session()->deleteGroupInviteBox({groupId}, blockFuture, [this, groupId](bool ok, const QJsonValue&, const QString& err, int code) {
        if (!ok) {
            emit notice(L("Không thể xóa lời mời nhóm: ", "Could not delete group invite: ") + friendlyError(err, code));
            return;
        }
        for (int i = m_groupInvites.size() - 1; i >= 0; --i) {
            if (m_groupInvites.at(i).toMap().value("groupId").toString() == groupId) {
                m_groupInvites.removeAt(i);
            }
        }
        emit groupInvitesChanged();
        emit notice(L("Đã xóa lời mời nhóm", "Group invite removed"));
    });
}

void ZaloClient::refreshFriendRecommendations() {
    if (!m_isLoggedIn || !m_apiClient->session()) return;
    m_apiClient->session()->fetchFriendRecommendations([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        const QJsonArray arr = data.toObject().value("recommItems").toArray();
        QVariantList out;
        for (const QJsonValue& v : arr) {
            const QJsonObject item = v.toObject();
            const QJsonObject info = item.value("dataInfo").toObject();
            const QString uid = info.value("userId").toString();
            if (uid.isEmpty() || uid == m_myUid || m_friendIds.contains(uid)) continue;
            out.append(QVariantMap{
                {"id", uid},
                {"name", pickName(info)},
                {"avatar", info.value("avatar").toString()},
                {"message", item.value("recommInfo").toObject().value("message").toString()},
                {"isSent", info.value("isSeenFriendReq").toBool()},
            });
        }
        m_friendRecommendations = out;
        emit friendRecommendationsChanged();
    });
}

void ZaloClient::refreshCloseFriends() {
    if (!m_isLoggedIn || !m_apiClient->session()) return;
    m_apiClient->session()->fetchCloseFriends([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        QSet<QString> ids;
        const QJsonArray arr = data.isArray() ? data.toArray() : data.toObject().value("data").toArray();
        for (const QJsonValue& v : arr) {
            const QString uid = v.isObject() ? v.toObject().value("uid").toString() : v.toVariant().toString();
            if (!uid.isEmpty()) ids.insert(uid);
        }
        m_closeFriendIds = ids;
        emit friendRecommendationsChanged();
    });
}

void ZaloClient::refreshLinkedDevices() {
    if (!m_isLoggedIn || !m_apiClient->session()) return;
    m_apiClient->session()->fetchLinkedDevices([this](bool ok, const QJsonValue& data, const QString& err, int code) {
        if (!ok) {
            emit notice(L("Không lấy được danh sách thiết bị: ", "Could not load linked devices: ") + friendlyError(err, code));
            return;
        }
        const QJsonValue devsVal = data.toObject().value("devices");
        QVariantList out;
        auto appendDevice = [&out](const QJsonObject& d, bool isCompanion) {
            const qint64 ts = d.value("lastUpdateTs").toVariant().toLongLong();
            const QString id = d.value("masterId").toString(d.value("deviceId").toString(d.value("id").toString()));
            if (id.isEmpty() && d.isEmpty()) return;
            out.append(QVariantMap{
                {"id", id.isEmpty() ? QStringLiteral("Zalo") : id},
                {"isCompanion", isCompanion},
                {"lastUpdateTs", ts},
                {"timeText", ts > 0 ? QDateTime::fromMSecsSinceEpoch(ts < 10000000000LL ? ts * 1000 : ts).toString("dd/MM/yyyy HH:mm") : QString()},
                {"companionsCount", d.value("companions").toArray().size()},
            });
        };
        if (devsVal.isObject()) {
            const QJsonObject master = devsVal.toObject();
            appendDevice(master, false);
            for (const QJsonValue& c : master.value("companions").toArray()) {
                if (c.isObject()) appendDevice(c.toObject(), true);
                else if (!c.toString().isEmpty()) {
                    out.append(QVariantMap{{"id", c.toString()}, {"isCompanion", true}, {"timeText", QString()}});
                }
            }
        } else if (devsVal.isArray()) {
            for (const QJsonValue& v : devsVal.toArray()) {
                if (v.isObject()) appendDevice(v.toObject(), false);
            }
        }
        m_linkedDevices = out;
        emit linkedDevicesChanged();
    });
}

void ZaloClient::scanAndOpenUrl(const QString& url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) return;
    if (!m_isLoggedIn || !m_apiClient->session()) {
        openExternalUrl(trimmed);
        return;
    }
    auto settled = std::make_shared<bool>(false);
    QPointer<ZaloClient> self(this);
    QTimer::singleShot(kScanUrlWaitMs, this, [this, settled, trimmed]() {
        if (*settled) return;
        *settled = true;
        openExternalUrl(trimmed);
    });
    m_apiClient->session()->scanUrl(trimmed, [self, settled, trimmed](bool ok, const QJsonValue& data, const QString&, int) {
        if (!self || *settled) return;
        *settled = true;
        if (ok && data.isObject() && data.toObject().contains("isSafe") && !data.toObject().value("isSafe").toBool(true)) {
            emit self->notice(self->L("Cảnh báo: Zalo đánh dấu liên kết này có thể không an toàn.",
                                      "Warning: Zalo flagged this link as potentially unsafe."));
            return;
        }
        self->openExternalUrl(trimmed);
    });
}

void ZaloClient::reportAbuse(const QString& threadId, bool isGroup, int reason, const QString& content) {
    if (!m_isLoggedIn || !m_apiClient->session() || threadId.isEmpty()) return;
    m_apiClient->session()->sendReport(threadId, isGroup, reason, content,
        [this](bool ok, const QJsonValue&, const QString& err, int code) {
            if (!ok) {
                emit notice(L("Gửi báo cáo thất bại: ", "Report failed: ") + friendlyError(err, code));
                return;
            }
            emit notice(L("Đã gửi báo cáo vi phạm", "Abuse report submitted"));
        });
}

void ZaloClient::upgradeGroupToCommunity(const QString& groupId) {
    if (!m_isLoggedIn || !m_apiClient->session() || groupId.isEmpty()) return;
    m_apiClient->session()->upgradeGroupToCommunity(groupId,
        [this, groupId](bool ok, const QJsonValue&, const QString& err, int code) {
            if (!ok) {
                emit notice(L("Không thể nâng cấp lên Cộng đồng: ", "Could not upgrade to Community: ") + friendlyError(err, code));
                return;
            }
            loadGroupInfo({groupId});
            emit notice(L("Đã nâng cấp nhóm lên Cộng đồng", "Group upgraded to Community"));
        });
}

void ZaloClient::refreshBankAccounts() {
    if (!m_isLoggedIn || !m_apiClient->session()) return;
    m_apiClient->session()->fetchBankList([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        const QJsonArray arr = data.toObject().value("banks").toArray();
        QVariantList banks;
        for (const QJsonValue& v : arr) {
            const QJsonObject b = v.toObject();
            banks.append(QVariantMap{
                {"bin", b.value("bin").toInt()},
                {"name", b.value("shortName").toString(b.value("name").toString())},
                {"fullName", b.value("name").toString()},
            });
        }
        m_bankList = banks;
        emit bankDataChanged();
    });
    m_apiClient->session()->fetchBankAccounts(0, 20, [this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        const QJsonArray arr = data.toObject().value("myBanks").toArray();
        QVariantList accounts;
        for (const QJsonValue& v : arr) {
            const QJsonObject a = v.toObject();
            accounts.append(QVariantMap{
                {"binBank", a.value("binBank").toInt()},
                {"numAccBank", a.value("numAccBank").toString()},
                {"nameAccBank", a.value("nameAccBank").toString()},
                {"bankName", a.value("bankName").toString()},
            });
        }
        m_bankAccounts = accounts;
        emit bankDataChanged();
    });
}

void ZaloClient::createBankAccount(int binBank, const QString& numAccBank, const QString& nameAccBank) {
    if (!m_isLoggedIn || !m_apiClient->session() || binBank <= 0 || numAccBank.trimmed().isEmpty()) return;
    m_apiClient->session()->createBankAccount(binBank, numAccBank.trimmed(), nameAccBank.trimmed(),
        [this](bool ok, const QJsonValue&, const QString& err, int code) {
            if (!ok) {
                emit notice(L("Không thể thêm tài khoản ngân hàng: ", "Could not add bank account: ") + friendlyError(err, code));
                return;
            }
            refreshBankAccounts();
            emit notice(L("Đã thêm tài khoản ngân hàng", "Bank account added"));
        });
}

void ZaloClient::sendBankCard(int binBank, const QString& numAccBank, const QString& nameAccBank) {
    if (!m_isLoggedIn || !m_apiClient->session() || m_currentConvId.isEmpty() || binBank <= 0 || numAccBank.trimmed().isEmpty()) return;
    const QString convId = m_currentConvId;
    const bool isGroup = currentConversationIsGroup();
    const QString accNum = numAccBank.trimmed();
    const QString accHolder = nameAccBank.trimmed().toUpper();
    QString bankName = bankShortNameForBin(binBank);
    for (const QVariant& bv : std::as_const(m_bankList)) {
        const QVariantMap bm = bv.toMap();
        if (bm.value("bin").toInt() == binBank && !bm.value("name").toString().isEmpty()) {
            bankName = bm.value("name").toString();
            break;
        }
    }
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
    local.messageType = QStringLiteral("bankcard");
    local.text = !bankName.isEmpty() ? QStringLiteral("%1 · %2").arg(bankName, accNum) : accNum;
    local.extra = {{"binBank", binBank},
                   {"bankName", bankName},
                   {"accountNumber", accNum},
                   {"accountHolder", accHolder},
                   {"desc", accHolder}};
    m_messageModel->mergeMessages(convId, {local});
    m_conversationModel->recordMessage(convId, L("[Chuyển khoản] ", "[Bank transfer] ") + local.text,
                                       L("Bạn: ", "You: "), now, false);
    const QString localId = local.id;
    m_apiClient->session()->sendBankCard(convId, isGroup, binBank, accNum, nameAccBank.trimmed(),
        [this, convId, localId](bool ok, const QJsonValue&, const QString& err, int code) {
            if (!ok) {
                m_messageModel->setStatus(convId, localId, QStringLiteral("failed"));
                emit notice(L("Gửi thẻ ngân hàng thất bại: ", "Failed to send bank card: ") + friendlyError(err, code));
                return;
            }
            m_messageModel->setStatus(convId, localId, QStringLiteral("sent"));
            emit notice(L("Đã gửi thông tin tài khoản ngân hàng", "Bank card sent"));
        }, local.cliMsgId);
}

QString ZaloClient::bankShortNameForBin(int bin) {
    switch (bin) {
    case 970425: return QStringLiteral("ABBank");
    case 970416: return QStringLiteral("ACB");
    case 970405: return QStringLiteral("Agribank");
    case 970418: return QStringLiteral("BIDV");
    case 963666: return QStringLiteral("BNP Paribas HCM");
    case 963668: return QStringLiteral("BNP Paribas HN");
    case 970454: return QStringLiteral("BVBank");
    case 970409: return QStringLiteral("Bac A Bank");
    case 970438: return QStringLiteral("BaoViet Bank");
    case 546034: return QStringLiteral("CAKE");
    case 168999: return QStringLiteral("Cathay United");
    case 970444: return QStringLiteral("VCBNeo");
    case 422589: return QStringLiteral("CIMB");
    case 970446: return QStringLiteral("Co-opBank");
    case 796500: return QStringLiteral("DBS");
    case 970406: return QStringLiteral("DongA Bank");
    case 970431: return QStringLiteral("Eximbank");
    case 533948: return QStringLiteral("Citibank");
    case 970408: return QStringLiteral("GPBank");
    case 970437: return QStringLiteral("HDBank");
    case 458761: return QStringLiteral("HSBC");
    case 970442: return QStringLiteral("Hong Leong");
    case 970456: return QStringLiteral("IBK HCM");
    case 970455: return QStringLiteral("IBK HN");
    case 970434: return QStringLiteral("Indovina");
    case 668888: return QStringLiteral("KBank");
    case 970452: return QStringLiteral("KienlongBank");
    case 970463: return QStringLiteral("Kookmin HCM");
    case 970462: return QStringLiteral("Kookmin HN");
    case 963369: return QStringLiteral("Liobank");
    case 970449: return QStringLiteral("LPBank");
    case 970422: return QStringLiteral("MB Bank");
    case 970426: return QStringLiteral("MSB");
    case 971025: return QStringLiteral("MoMo");
    case 970419: return QStringLiteral("NCB");
    case 970428: return QStringLiteral("Nam A Bank");
    case 801011: return QStringLiteral("NongHyup");
    case 970448: return QStringLiteral("OCB");
    case 970414: return QStringLiteral("OceanBank");
    case 970430: return QStringLiteral("PGBank");
    case 970412: return QStringLiteral("PVcomBank");
    case 970439: return QStringLiteral("Public Bank");
    case 970429: return QStringLiteral("SCB");
    case 970443: return QStringLiteral("SHB");
    case 970403: return QStringLiteral("Sacombank");
    case 970400: return QStringLiteral("Saigonbank");
    case 970440: return QStringLiteral("SeABank");
    case 970424: return QStringLiteral("Shinhan Bank");
    case 970410: return QStringLiteral("Standard Chartered");
    case 963326: return QStringLiteral("TNEX");
    case 970423: return QStringLiteral("TPBank");
    case 970407: return QStringLiteral("Techcombank");
    case 963388: return QStringLiteral("Timo");
    case 546035: return QStringLiteral("UBank");
    case 970458: return QStringLiteral("UOB Vietnam");
    case 970441: return QStringLiteral("VIB");
    case 970432: return QStringLiteral("VPBank");
    case 970421: return QStringLiteral("VRB");
    case 970427: return QStringLiteral("VietABank");
    case 970433: return QStringLiteral("VietBank");
    case 970436: return QStringLiteral("Vietcombank");
    case 970415: return QStringLiteral("VietinBank");
    case 970457: return QStringLiteral("Woori Bank");
    default: return {};
    }
}

void ZaloClient::resolveZInstantBankCard(const QString& convId, const QString& msgId, const QString& dataUrl) {
    if (!m_isLoggedIn || convId.isEmpty() || msgId.isEmpty() || dataUrl.isEmpty()) return;
    const QUrl url(dataUrl);
    if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) return;
    const QString key = convId + QLatin1Char(':') + msgId;
    if (m_bankCardFetching.contains(key)) return;
    dropOldestIfFull(m_bankCardFetching);
    m_bankCardFetching.insert(key);
    QPointer<ZaloClient> guard(this);
    fetchBytes(dataUrl, [guard, convId, msgId](const QByteArray& bytes, const QString& err) {
        if (!guard || !err.isEmpty() || bytes.isEmpty()) return;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes);
        const QString textFallback = QString::fromUtf8(bytes);
        const BankCardFields card = extractBankCardFields(
            doc.isNull() ? QJsonValue() : (doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array())),
            textFallback);
        if (card.binBank <= 0 && card.accountNumber.isEmpty() && card.accountHolder.isEmpty()) return;
        QString bankName = card.bankName;
        if (bankName.isEmpty() && card.binBank > 0) bankName = bankShortNameForBin(card.binBank);
        QVariantMap extra{
            {"binBank", card.binBank},
            {"bankName", bankName},
            {"accountNumber", card.accountNumber},
            {"accountHolder", card.accountHolder},
        };
        if (!card.accountHolder.isEmpty()) extra.insert("desc", card.accountHolder);
        QString displayText;
        if (!bankName.isEmpty() && !card.accountNumber.isEmpty()) {
            displayText = QStringLiteral("%1 · %2").arg(bankName, card.accountNumber);
        } else if (!card.accountNumber.isEmpty()) {
            displayText = card.accountNumber;
        }
        guard->m_messageModel->setMessageExtra(convId, msgId, displayText, extra);
    });
}

void ZaloClient::fetchConversationSharedMedia(const QString& convId) {
    if (!m_isLoggedIn || !m_apiClient->session() || convId.isEmpty()) return;
    const bool isSendToMe = (convId == myCloudId());
    QPointer<ZaloClient> guard(this);
    m_apiClient->session()->fetchMediaStorePreview(convId, isSendToMe, 50,
        [guard, convId](bool ok, const QJsonValue& data, const QString&, int) {
            if (!guard || !ok) return;
            QJsonArray arr;
            if (data.isArray()) arr = data.toArray();
            else if (data.isObject()) {
                const QJsonObject obj = data.toObject();
                arr = obj.contains("items") ? obj.value("items").toArray() : obj.value("data").toArray();
            }
            QVariantList mediaList;
            QVariantList fileList;
            QVariantList linkList;
            for (const QJsonValue& v : arr) {
                const QJsonObject item = v.toObject();
                const QString rawType = item.value("type").toString();
                const QString msgId = idOf(item.value("global_msg_id").isUndefined() ? item.value("msgId") : item.value("global_msg_id"));
                const QString ownerId = idOf(item.value("owner_id").isUndefined() ? item.value("fromUid") : item.value("owner_id"));
                qint64 ts = item.value("created_time").toVariant().toLongLong();
                if (ts <= 0) ts = item.value("sendDttm").toVariant().toLongLong();
                if (ts > 0 && ts < 10000000000LL) ts *= 1000;
                const QString sender = ownerId == guard->m_myUid ? guard->m_userName : guard->displayNameOf(ownerId);
                const QString srcUrl = item.value("src_url").toString(item.value("href").toString(item.value("url").toString()));
                const QString thumb = item.value("thumb_url").toString(item.value("thumb").toString(item.value("previewThumb").toString()));
                const QString title = item.value("file_name").toString(item.value("title").toString());
                const qint64 fileSize = item.value("file_size").toVariant().toLongLong();
                QVariantMap entry{{"msgId", msgId}, {"senderName", sender}, {"ts", ts}};
                if (rawType == QLatin1String("media") || rawType == QLatin1String("image")) {
                    const int subType = item.value("sub_type").toInt(item.value("subType").toInt(1));
                    const QString mediaUrl = srcUrl.isEmpty() ? thumb : srcUrl;
                    if (mediaUrl.isEmpty() && thumb.isEmpty()) continue;
                    entry["thumb"] = thumb.isEmpty() ? mediaUrl : thumb;
                    entry["url"] = mediaUrl;
                    entry["isVideo"] = (subType == 2);
                    mediaList.append(entry);
                } else if (rawType == QLatin1String("file")) {
                    if (srcUrl.isEmpty() && title.isEmpty()) continue;
                    entry["title"] = title.isEmpty() ? QStringLiteral("Tệp đính kèm") : title;
                    entry["url"] = srcUrl;
                    entry["fileSize"] = fileSize;
                    fileList.append(entry);
                } else if (rawType == QLatin1String("link")) {
                    if (srcUrl.isEmpty()) continue;
                    entry["title"] = title.isEmpty() ? srcUrl : title;
                    entry["url"] = srcUrl;
                    linkList.append(entry);
                }
            }
            QHash<QString, QVariantList> store;
            store.insert(QStringLiteral("media"), mediaList);
            store.insert(QStringLiteral("file"), fileList);
            store.insert(QStringLiteral("link"), linkList);
            guard->m_serverSharedMedia.insert(convId, store);
            emit guard->sharedMediaUpdated(convId);
        });
}

QVariantList ZaloClient::sharedItemsForConversation(const QString& convId, const QString& kind, int limit) {
    QVariantList out = (!convId.isEmpty() && convId == m_currentConvId && m_messageModel)
                           ? m_messageModel->sharedItems(kind, limit)
                           : QVariantList{};
    const QVariantList server = m_serverSharedMedia.value(convId).value(kind);
    if (server.isEmpty()) return out;

    QSet<QString> seenIds;
    QSet<QString> seenUrls;
    for (const QVariant& v : std::as_const(out)) {
        const QVariantMap m = v.toMap();
        const QString mid = m.value("msgId").toString();
        const QString u = m.value("url").toString();
        if (!mid.isEmpty()) seenIds.insert(mid);
        if (!u.isEmpty()) seenUrls.insert(u);
    }
    for (const QVariant& v : server) {
        if (out.size() >= limit) break;
        const QVariantMap m = v.toMap();
        const QString mid = m.value("msgId").toString();
        const QString u = m.value("url").toString();
        if ((!mid.isEmpty() && seenIds.contains(mid)) || (!u.isEmpty() && seenUrls.contains(u))) continue;
        if (!mid.isEmpty()) seenIds.insert(mid);
        if (!u.isEmpty()) seenUrls.insert(u);
        out.append(m);
    }
    std::stable_sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("ts").toLongLong() > b.toMap().value("ts").toLongLong();
    });
    if (out.size() > limit) out.resize(limit);
    return out;
}

}
