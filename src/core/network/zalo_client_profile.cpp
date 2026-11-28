#include "zalo_client_internal.h"
#include "platform/background_portal.h"

#include <QCollator>

namespace Zalos::Core::Network {

using namespace Detail;

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
    else if (profile.contains("type") || profile.contains("oaInfo") || profile.contains("bizPkg")) m_officialIds.remove(uid);
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
    m_profileRelatedGroups.clear();
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
        if (userId != myCloudId()) {
            m_apiClient->session()->fetchRelatedFriendGroups({userId}, [this, userId](bool ok, const QJsonValue& data, const QString&, int) {
                if (!ok || !m_showUserProfileModal || m_activeProfile.userId != userId) return;
                const QJsonObject relMap = data.toObject().value("groupRelateds").toObject();
                const QJsonArray arr = relMap.value(userId).toArray();
                QVariantList groups;
                for (const QJsonValue& v : arr) {
                    const QString gid = v.isObject() ? v.toObject().value("grid").toString() : v.toString();
                    if (gid.isEmpty()) continue;
                    const Profile gp = m_groups.value(gid);
                    groups.append(QVariantMap{
                        {"id", gid},
                        {"name", gp.name.isEmpty() ? gid : gp.name},
                        {"avatar", gp.avatar},
                        {"memberCount", gp.memberCount},
                    });
                }
                m_profileRelatedGroups = groups;
                emit currentProfileChanged();
            });
        }
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
                m_groupMembersCache.clear();
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
    if (!m_isLoggedIn || !m_apiClient->session()) {
        emit notice(L("Không thể cập nhật hồ sơ: ", "Failed to update profile: ") + friendlyError(QStringLiteral("not_logged_in")));
        return;
    }
    const QString cleanName = name.trimmed();
    const QString cleanBio = bio.trimmed();
    const bool bioChanged = cleanBio != m_myProfile.status;
    auto left = std::make_shared<int>(bioChanged ? 2 : 1);
    auto failed = std::make_shared<bool>(false);
    const auto settle = [this, left, failed]() {
        if (!m_isLoggedIn || --*left > 0 || *failed) return;
        emit notice(L("Đã cập nhật thông tin cá nhân", "Profile updated successfully"));
    };

    auto* session = m_apiClient->session();
    session->updateProfile(cleanName, gender, dob, [this, left, failed, settle, cleanName, gender, dob](bool ok, const QJsonValue&, const QString& err, int) {
        if (!m_isLoggedIn) return;
        if (ok) {
            m_myProfile.displayName = cleanName;
            m_myProfile.gender = gender;
            m_myProfile.sdob = dob;
            m_userName = cleanName;
            m_activeProfile = m_myProfile;
            emit userProfileChanged();
            emit currentProfileChanged();
            saveSession();
        } else {
            *failed = true;
            emit notice(L("Không thể cập nhật hồ sơ: ", "Failed to update profile: ") + friendlyError(err));
        }
        settle();
    });
    if (bioChanged) {
        session->updateProfileBio(cleanBio, [this, left, failed, settle, cleanBio](bool ok, const QJsonValue&, const QString& err, int) {
            if (!m_isLoggedIn) return;
            if (ok) {
                m_myProfile.status = cleanBio;
                if (m_activeProfile.userId == m_myUid || m_activeProfile.userId.isEmpty()) m_activeProfile.status = cleanBio;
                emit userProfileChanged();
                emit currentProfileChanged();
                saveSession();
            } else {
                *failed = true;
                emit notice(L("Không thể cập nhật trạng thái: ", "Failed to update status: ") + friendlyError(err));
            }
            settle();
        });
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
            if (ok) {
                refreshBlockedUsers();
                return;
            }
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
            if (ok) {
                refreshBlockedUsers();
                return;
            }
            m_blockedIds.insert(userId);
            if (m_activeProfile.userId == userId) m_activeProfile.isBlocked = true;
            emit currentProfileChanged();
            emit composerStateChanged();
            emit notice(L("Không thể bỏ chặn: ", "Failed to unblock: ") + friendlyError(err));
        });
    }
}

void ZaloClient::refreshBlockedUsers() {
    if (!m_isLoggedIn || !m_apiClient->session()) return;
    constexpr int kCount = 100;
    constexpr int kMaxPages = 100;
    const quint64 gen = ++m_blockedFetchGen;
    m_blockedPending.clear();
    m_blockedPendingIds.clear();
    auto requestPage = std::make_shared<std::function<void(int)>>();
    const std::weak_ptr<std::function<void(int)>> weakRequest = requestPage;
    *requestPage = [this, gen, weakRequest](int page) {
        if (!m_isLoggedIn || !m_apiClient->session()) return;
        m_apiClient->session()->fetchBlockedUsers(page, kCount, [this, gen, page, keep = weakRequest.lock()](bool ok, const QJsonValue& data, const QString&, int) {
            if (!m_isLoggedIn || gen != m_blockedFetchGen) return;
            const QJsonObject obj = data.toObject();
            const bool wellFormed = ok && (obj.contains("profiles") || data.isArray());
            if (!wellFormed) {
                m_blockedPending.clear();
                m_blockedPendingIds.clear();
                return;
            }
            const QJsonArray arr = obj.contains("profiles") ? obj.value("profiles").toArray() : data.toArray();
            for (const QJsonValue& v : arr) {
                const QJsonObject u = v.toObject();
                const QString uid = idOf(u.value("userId"));
                if (uid.isEmpty() || m_blockedPendingIds.contains(uid)) continue;
                m_blockedPendingIds.insert(uid);
                const QString name = pickName(u);
                const QString avatar = pickAvatar(u);
                if (!name.isEmpty() || !avatar.isEmpty()) {
                    Profile p = m_users.value(uid);
                    if (!name.isEmpty()) p.name = name;
                    if (!avatar.isEmpty()) p.avatar = avatar;
                    m_users.insert(uid, p);
                }
                m_blockedPending.append(QVariantMap{
                    {"userId", uid},
                    {"name", !name.isEmpty() ? name : displayNameOf(uid)},
                    {"avatar", !avatar.isEmpty() ? avatar : avatarOf(uid)},
                });
            }
            if (arr.size() >= kCount && page < kMaxPages && keep) {
                (*keep)(page + 1);
                return;
            }
            if (m_blockedIds != m_blockedPendingIds) {
                m_blockedIds = m_blockedPendingIds;
                emit composerStateChanged();
                emit currentProfileChanged();
            }
            m_blockedUsers = m_blockedPending;
            m_blockedPending.clear();
            m_blockedPendingIds.clear();
            emit blockedUsersChanged();
        });
    };
    (*requestPage)(1);
}

void ZaloClient::removeFriend(const QString& userId) {
    if (userId.isEmpty() || userId == m_myUid) return;
    if (!m_isLoggedIn || !m_apiClient->session()) {
        emit notice(L("Không thể xóa bạn bè: ", "Failed to remove friend: ") + friendlyError(QStringLiteral("not_logged_in")));
        return;
    }
    m_apiClient->session()->removeFriend(userId, [this, userId](bool ok, const QJsonValue&, const QString& err, int) {
        if (!m_isLoggedIn) return;
        if (!ok) {
            emit notice(L("Không thể xóa bạn bè: ", "Failed to remove friend: ") + friendlyError(err));
            return;
        }
        m_friendIds.remove(userId);
        persistProfile(userId, false);
        if (m_activeProfile.userId == userId) m_activeProfile.isFriend = false;
        rebuildContacts();
        emit currentProfileChanged();
        emit notice(L("Đã xóa khỏi danh sách bạn bè", "Removed from friends list"));
    });
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

QVariantList ZaloClient::friendsForCard(const QString& query) const {
    const QStringList terms = Models::foldTerms(query.trimmed());
    struct Row {
        QCollatorSortKey key;
        QVariantMap map;
    };
    QCollator collator;
    QVector<Row> rows;
    rows.reserve(m_friendIds.size());
    for (const QString& id : m_friendIds) {
        const QString name = displayNameOf(id);
        if (name.isEmpty()) continue;
        if (!terms.isEmpty() && !Models::containsAllTerms(Models::foldForSearch(name), terms)) continue;
        rows.append(Row{collator.sortKey(name), QVariantMap{{"id", id}, {"name", name}, {"avatar", m_users.value(id).avatar}}});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.key < b.key; });
    QVariantList out;
    out.reserve(rows.size());
    for (const Row& row : rows) out.append(row.map);
    return out;
}

QVariantList ZaloClient::conversationsForForward(const QString& query) const {
    const QString trimmed = query.trimmed();
    const QStringList terms = Models::foldTerms(trimmed);
    const bool revealHidden = m_conversationModel->hiddenPinReveal();
    QVector<const Models::ConversationItem*> matched;
    const auto& items = m_conversationModel->conversations();
    matched.reserve(items.size());
    for (const auto& c : items) {
        if (!revealHidden && m_hiddenThreads.contains(c.id)) continue;
        if (!terms.isEmpty()) {
            const QString foldedName = c.foldedName.isEmpty() ? Models::foldForSearch(c.name) : c.foldedName;
            if (!Models::containsAllTerms(foldedName, terms)) continue;
        }
        matched.append(&c);
    }
    std::sort(matched.begin(), matched.end(), [](const Models::ConversationItem* a, const Models::ConversationItem* b) {
        if (a->isPinned != b->isPinned) return a->isPinned;
        return a->lastTs > b->lastTs;
    });
    QVariantList out;
    out.reserve(matched.size());
    for (const auto* c : matched) {
        out.append(QVariantMap{
            {QStringLiteral("id"), c->id},
            {QStringLiteral("name"), c->name},
            {QStringLiteral("avatarUrl"), c->avatarUrl},
            {QStringLiteral("isGroup"), c->isGroup},
            {QStringLiteral("memberCount"), c->memberCount},
            {QStringLiteral("memberAvatars"), m_conversationModel->memberAvatars(c->id)},
            {QStringLiteral("lastMessage"), c->lastMessage},
        });
    }
    return out;
}

void ZaloClient::openForwardModal(const QString& msgId, const QString& previewText) {
    if (!(m_messageModel->getMessageById(msgId).value("actions").toInt() & Models::MessageModel::ActionForward)) {
        emit notice(L("Không thể chuyển tiếp tin nhắn này", "This message can't be forwarded"));
        return;
    }
    m_forwardMode = QStringLiteral("message");
    m_forwardCardUid.clear();
    m_forwardSourceMsgId = msgId;
    m_forwardSourcePreview = previewText;
    m_showForwardModal = true;
    emit forwardModalChanged();
}

void ZaloClient::closeForwardModal() {
    m_showForwardModal = false;
    m_forwardSourceMsgIds.clear();
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

void ZaloClient::createPoll(const QString& groupId, const QString& question, const QStringList& options,
                            const QVariantMap& settings) {
    if (groupId.isEmpty()) return;
    const QString problem = pollDraftError(question, options);
    if (!problem.isEmpty()) {
        emit notice(problem);
        return;
    }
    if (!m_isLoggedIn) {
        emit notice(L("Cần kết nối để tạo bình chọn", "Creating a poll needs a connection"));
        return;
    }
    QStringList clean;
    for (const QString& o : options) {
        if (!o.trimmed().isEmpty()) clean.append(o.trimmed());
    }
    m_apiClient->session()->createPoll(groupId, question.trimmed(), clean, settings,
        [this, groupId](bool ok, const QJsonValue& data, const QString& err, int code) {
            if (!ok) {
                emit notice(pollErrorText(L("Tạo bình chọn thất bại: ", "Couldn't create the poll: "), err, code));
                return;
            }
            const QJsonObject detail = data.toObject();
            const qint64 pollId = detail.value("poll_id").toVariant().toLongLong();
            if (pollId > 0) {
                m_pollGroups.insert(pollId, groupId);
                storePollDetail(pollId, detail);
            }
            closeCreatePollModal();
        });
}

void ZaloClient::openPollDialog(qint64 pollId, const QString& groupId, bool voters) {
    if (pollId <= 0) return;
    if (!groupId.isEmpty()) m_pollGroups.insert(pollId, groupId);
    m_pollDialogId = pollId;
    m_pollDialogGroupId = groupId.isEmpty() ? m_pollGroups.value(pollId) : groupId;
    m_pollDialogVoters = voters;
    emit pollDialogChanged();
    refreshPoll(pollId, true);
}

void ZaloClient::closePollDialog() {
    if (m_pollDialogId == 0) return;
    m_pollDialogId = 0;
    m_pollDialogVoters = false;
    emit pollDialogChanged();
}

void ZaloClient::submitPollChoices(qint64 pollId, const QVariantList& selectedIds, const QStringList& newOptions,
                                   const QVariantList& newVoted) {
    if (pollId <= 0) return;
    if (!m_isLoggedIn) {
        emit notice(L("Cần kết nối để bình chọn", "Voting needs a connection"));
        return;
    }
    const QJsonObject detail = m_pollDetails.value(pollId);
    const bool multi = detail.value("allow_multi_choices").toBool();
    QList<int> ids;
    for (const QVariant& v : selectedIds) {
        const int id = v.toInt();
        if (id > 0 && !ids.contains(id)) ids.append(id);
    }
    QSet<QString> existing;
    const QJsonArray currentOptions = detail.value("options").toArray();
    for (const QJsonValue& v : currentOptions)
        existing.insert(v.toObject().value("content").toString().simplified().toCaseFolded());
    QJsonArray added;
    int newVotedCount = 0;
    for (int i = 0; i < newOptions.size(); ++i) {
        const QString text = newOptions.at(i).trimmed();
        if (text.isEmpty()) continue;
        const QString key = text.simplified().toCaseFolded();
        if (text.size() > 120) {
            emit notice(L("Mỗi phương án tối đa 120 ký tự.", "Each option can be at most 120 characters."));
            return;
        }
        if (existing.contains(key)) {
            emit notice(L("Phương án được thêm đã tồn tại", "That option already exists"));
            return;
        }
        existing.insert(key);
        const bool voted = i < newVoted.size() && newVoted.at(i).toBool();
        if (voted) ++newVotedCount;
        added.append(QJsonObject{{"voted", voted}, {"content", text}});
    }
    if (currentOptions.size() + added.size() > 30) {
        emit notice(L("Bạn đã tạo quá số lựa chọn cho phép (30).", "A poll can have at most 30 options."));
        return;
    }
    if (!multi && ids.size() + newVotedCount > 1) {
        emit notice(L("Bình chọn này chỉ cho chọn một phương án.", "This poll allows only one choice."));
        return;
    }
    const bool wasJoined = detail.value("joined").toBool();
    if (ids.isEmpty() && added.isEmpty() && !wasJoined) {
        emit notice(selectedIds.isEmpty()
                        ? L("Hãy chọn một phương án trước khi bình chọn", "Choose an option before voting")
                        : L("Không xác định được phương án đã chọn, vui lòng thử lại", "Couldn't identify the selected option, please try again"));
        return;
    }
    auto done = [this, pollId, ids](bool ok, const QJsonValue& data, const QString& err, int code) {
        if (!ok) {
            emit notice(pollErrorText(L("Bình chọn thất bại: ", "Vote failed: "), err, code));
            refreshPoll(pollId, true);
            return;
        }
        const QJsonArray options = data.toObject().value("options").toArray();
        if (m_pollDetails.contains(pollId)) {
            QJsonObject merged = m_pollDetails.value(pollId);
            if (!options.isEmpty()) {
                merged["options"] = options;
            } else {
                QJsonArray marked;
                for (const QJsonValue& v : merged.value("options").toArray()) {
                    QJsonObject o = v.toObject();
                    o["voted"] = ids.contains(o.value("option_id").toInt());
                    marked.append(o);
                }
                merged["options"] = marked;
            }
            bool joined = false;
            for (const QJsonValue& v : merged.value("options").toArray()) joined = joined || v.toObject().value("voted").toBool();
            merged["joined"] = joined;
            storePollDetail(pollId, merged);
        }
        refreshPoll(pollId, true);
        QTimer::singleShot(2000, this, [this, pollId]() { refreshPoll(pollId, true); });
    };
    if (!added.isEmpty()) m_apiClient->session()->addPollOptions(pollId, added, ids, done);
    else m_apiClient->session()->votePoll(pollId, ids, done);
}

void ZaloClient::sharePoll(qint64 pollId) {
    if (pollId <= 0 || !m_isLoggedIn) return;
    m_apiClient->session()->sharePoll(pollId, [this](bool ok, const QJsonValue&, const QString& err, int code) {
        if (!ok) {
            emit notice(L("Không chia sẻ được bình chọn: ", "Failed to share poll: ") + friendlyError(err, code));
            return;
        }
        emit notice(L("Đã chia sẻ bình chọn vào nhóm", "Poll shared to the group"));
    });
}

void ZaloClient::lockPoll(qint64 pollId) {
    if (pollId <= 0 || !m_isLoggedIn) return;
    if (m_pollDetails.contains(pollId) && !canManagePoll(pollId, m_pollDetails.value(pollId))) {
        emit notice(L("Bạn không có quyền khóa bình chọn này", "You can't close this poll"));
        return;
    }
    m_apiClient->session()->lockPoll(pollId, [this, pollId](bool ok, const QJsonValue&, const QString& err, int code) {
        if (!ok) {
            emit notice(pollErrorText(L("Không khóa được bình chọn: ", "Couldn't close the poll: "), err, code));
            return;
        }
        refreshPoll(pollId, true);
        emit notice(L("Đã khóa bình chọn", "Poll closed"));
    });
}

void ZaloClient::changeGroupName(const QString& groupId, const QString& newName) {
    if (groupId.isEmpty() || newName.trimmed().isEmpty()) return;
    const QString cleanName = newName.trimmed();
    if (!m_isLoggedIn || !m_apiClient->session()) {
        emit notice(L("Không đổi được tên nhóm: ", "Failed to rename group: ") + friendlyError(QStringLiteral("not_logged_in")));
        return;
    }
    m_apiClient->session()->changeGroupName(groupId, cleanName, [this, groupId, cleanName](bool ok, const QJsonValue&, const QString& err, int) {
        if (!m_isLoggedIn) return;
        if (!ok) {
            emit notice(L("Không đổi được tên nhóm: ", "Failed to rename group: ") + friendlyError(err));
            return;
        }
        if (m_currentConvId == groupId) {
            m_currentConvName = cleanName;
            emit currentConversationChanged();
        }
        m_conversationModel->updateProfile(groupId, cleanName, QString());
        if (m_groups.contains(groupId)) {
            m_groups[groupId].name = cleanName;
            persistProfile(groupId, true);
        }
        if (m_offlineStore) m_offlineStore->saveConversation(m_conversationModel->conversation(groupId));
        emit notice(L("Đã đổi tên nhóm thành công", "Group name updated successfully"));
    });
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

bool ZaloClient::pasteClipboardToDraft() {
    QClipboard* clip = QGuiApplication::clipboard();
    if (!clip) return false;
    const QMimeData* mime = clip->mimeData();
    if (!mime) return false;

    if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            const QString tempPath = QString("%1/zalos_paste_%2_%3.png")
                                         .arg(tempDir)
                                         .arg(QDateTime::currentMSecsSinceEpoch())
                                         .arg(++m_localSeq);
            QPointer<ZaloClient> self(this);
            const QString convId = m_currentConvId;
            QThreadPool::globalInstance()->start([self, img, tempPath, convId]() {
                if (!img.save(tempPath, "PNG")) return;
                QObject* context = self.data();
                if (!context) {
                    QFile::remove(tempPath);
                    return;
                }
                QMetaObject::invokeMethod(context, [self, tempPath, convId]() {
                    if (!self || self->m_currentConvId != convId) {
                        QFile::remove(tempPath);
                        if (self) emit self->notice(self->L("Đã hủy dán ảnh vì bạn đã chuyển cuộc trò chuyện",
                                                            "Paste cancelled because you switched conversations"));
                        return;
                    }
                    emit self->clipboardAttachmentsReady({self->fileInfoForUrl(tempPath)});
                }, Qt::QueuedConnection);
            });
            return true;
        }
    }

    if (mime->hasUrls()) {
        QVariantList list;
        for (const QUrl& url : mime->urls()) {
            const QString path = url.toLocalFile();
            if (path.isEmpty() || !QFileInfo::exists(path)) continue;
            list.append(fileInfoForUrl(path));
        }
        if (!list.isEmpty()) {
            emit clipboardAttachmentsReady(list);
            return true;
        }
    }
    return false;
}

bool ZaloClient::tryPasteClipboardImage() {
    if (m_currentConvId.isEmpty()) return false;
    QClipboard* clip = QGuiApplication::clipboard();
    if (!clip) return false;
    const QMimeData* mime = clip->mimeData();
    if (!mime) return false;

    if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            const QString tempPath = QString("%1/zalos_paste_%2_%3.png")
                                         .arg(tempDir)
                                         .arg(QDateTime::currentMSecsSinceEpoch())
                                         .arg(++m_localSeq);
            QPointer<ZaloClient> self(this);
            const QString convId = m_currentConvId;
            QThreadPool::globalInstance()->start([self, img, tempPath, convId]() {
                if (!img.save(tempPath, "PNG")) return;
                QObject* context = self.data();
                if (!context) {
                    QFile::remove(tempPath);
                    return;
                }
                QMetaObject::invokeMethod(context, [self, tempPath, convId]() {
                    if (!self || self->m_currentConvId != convId) {
                        QFile::remove(tempPath);
                        if (self) emit self->notice(self->L("Đã hủy dán ảnh vì bạn đã chuyển cuộc trò chuyện",
                                                            "Paste cancelled because you switched conversations"));
                        return;
                    }
                    self->sendFile(QUrl::fromLocalFile(tempPath).toString(), QString());
                }, Qt::QueuedConnection);
            });
            return true;
        }
    }

    if (mime->hasUrls()) {
        bool sent = false;
        for (const QUrl& url : mime->urls()) {
            const QString path = url.toLocalFile();
            if (path.isEmpty() || !QFileInfo::exists(path)) continue;
            sendFile(QUrl::fromLocalFile(path).toString(), QString());
            sent = true;
        }
        return sent;
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
    if (friendId.isEmpty()) return;
    if (!m_isLoggedIn) {
        emit notice(L("Không đổi được biệt danh: ", "Failed to change alias: ") + friendlyError(QStringLiteral("not_logged_in")));
        return;
    }
    const QString clean = newAlias.trimmed();
    if (clean.isEmpty()) {
        if (!m_aliases.contains(friendId)) return;
        m_apiClient->session()->removeFriendAlias(friendId, [this, friendId](bool ok, const QJsonValue&,
                                                                             const QString& err, int code) {
            if (!ok) {
                emit notice(L("Không xóa được biệt danh: ", "Failed to remove alias: ") + friendlyError(err, code));
                return;
            }
            QHash<QString, QString> next = m_aliases;
            next.remove(friendId);
            m_groupMembersCache.clear();
            applyAliases(next);
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
        QHash<QString, QString> next = m_aliases;
        next.insert(friendId, clean);
        m_groupMembersCache.clear();
        applyAliases(next);
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
    sendForward(targetThreadId, msgId, true);
}

void ZaloClient::sendForward(const QString& targetThreadId, const QString& msgId, bool notifySuccess,
                             std::function<void(bool ok)> done) {
    if (targetThreadId.isEmpty() || msgId.isEmpty()) {
        if (done) done(false);
        return;
    }
    const QVariantMap item = m_messageModel->getMessageById(msgId);
    if (item.isEmpty()) {
        emit notice(L("Không tìm thấy tin nhắn cần chuyển tiếp", "Message to forward not found"));
        if (done) done(false);
        return;
    }
    if (!(item.value("actions").toInt() & Models::MessageModel::ActionForward)) {
        emit notice(L("Không thể chuyển tiếp tin nhắn này", "This message can't be forwarded"));
        if (done) done(false);
        return;
    }

    const QString text = item.value("text").toString();
    const QString href = item.value("href").toString().trimmed();
    const QString msgType = item.value("messageType").toString();
    const QString mediaUrl = item.value("mediaUrl").toString();
    const bool isMediaForward = (msgType == "image" || msgType == "file" || msgType == "video");
    const QString resolvedText = (!isMediaForward && text.isEmpty() && !href.isEmpty()) ? href : text;
    const bool isGroup = isGroupThread(targetThreadId);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastClientId = qMax(now, m_lastClientId + 1);
    const QString cliId = QString::number(m_lastClientId);

    Models::MessageItem local;
    local.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_localSeq);
    local.cliMsgId = cliId;
    local.text = resolvedText;
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

    closeForwardModal();

    if (!m_isLoggedIn || !m_apiClient->session()) {
        m_messageModel->setStatus(targetThreadId, local.id, QStringLiteral("failed"));
        emit notice(L("Không chuyển tiếp được tin nhắn: chưa đăng nhập", "Couldn't forward message: not logged in"));
        if (done) done(false);
        return;
    }

    const QString localId = local.id;
    QPointer<ZaloClient> guard(this);
    auto finish = [guard, targetThreadId, localId, notifySuccess, done](bool ok, const QJsonValue& res, const QString& err) {
        if (!guard) {
            if (done) done(false);
            return;
        }
        guard->m_messageModel->setStatus(targetThreadId, localId, ok ? QStringLiteral("sent") : QStringLiteral("failed"));
        if (ok) {
            const QString serverId = idOf(res.toObject().value("msgId"));
            if (!serverId.isEmpty()) guard->m_messageModel->confirmMessage(targetThreadId, localId, serverId);
            if (notifySuccess) emit guard->notice(guard->L("Đã chuyển tiếp tin nhắn thành công", "Message forwarded successfully"));
        } else {
            emit guard->notice(guard->L("Không chuyển tiếp được tin nhắn: ", "Couldn't forward message: ") + guard->friendlyError(err));
        }
        if (done) done(ok);
    };

    if (msgType == "image" || msgType == "file" || msgType == "video") {
        const QString localPath = item.value("isMine").toBool() ? forwardableLocalPath(mediaUrl) : QString();
        if (!localPath.isEmpty()) {
            m_apiClient->session()->sendAttachment(targetThreadId, isGroup, localPath, text,
                [finish](bool ok, const QJsonValue& data, const QString& err, int) { finish(ok, data, err); },
                cliId);
        } else if (!mediaUrl.isEmpty() && (mediaUrl.startsWith("http://") || mediaUrl.startsWith("https://"))) {
            const QString href = item.value("href").toString();
            const QString sourceUrl = msgType == QLatin1String("file") && href.startsWith(QLatin1String("http")) ? href : mediaUrl;
            QString name = msgType == QLatin1String("file") ? QFileInfo(text.trimmed()).fileName() : QString();
            if (name.isEmpty()) name = suggestedMediaFileName(sourceUrl);
            if (QFileInfo(name).suffix().isEmpty()) {
                name += msgType == QLatin1String("image") ? QStringLiteral(".jpg")
                      : msgType == QLatin1String("video") ? QStringLiteral(".mp4")
                                                          : QString();
            }
            const QString tempDir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                        .filePath(QStringLiteral("zalos_fwd_%1_%2").arg(now).arg(++m_localSeq));
            QDir().mkpath(tempDir);
            const QString tempPath = QDir(tempDir).filePath(name);
            const QString caption = msgType == QLatin1String("file") ? QString() : text;
            downloadToFile(sourceUrl, tempPath, [this, targetThreadId, isGroup, text, caption, mediaUrl, tempDir, tempPath, cliId, finish](const QString& err) {
                if (!err.isEmpty()) {
                    QDir(tempDir).removeRecursively();
                    const QString forwardContent = text.isEmpty() ? mediaUrl : (text + "\n" + mediaUrl);
                    if (!m_apiClient || !m_apiClient->session()) {
                        finish(false, {}, err);
                        return;
                    }
                    m_apiClient->session()->sendText(targetThreadId, isGroup, forwardContent,
                        [finish](bool ok, const QJsonValue& res, const QString& sErr, int) { finish(ok, res, sErr); },
                        cliId);
                    return;
                }
                if (!m_apiClient || !m_apiClient->session()) {
                    QDir(tempDir).removeRecursively();
                    finish(false, {}, QStringLiteral("not_logged_in"));
                    return;
                }
                m_apiClient->session()->sendAttachment(targetThreadId, isGroup, tempPath, caption,
                    [finish, tempDir](bool ok, const QJsonValue& res, const QString& sErr, int) {
                        QDir(tempDir).removeRecursively();
                        finish(ok, res, sErr);
                    },
                    cliId);
            });
        } else {
            m_apiClient->session()->sendText(targetThreadId, isGroup, text.isEmpty() ? mediaUrl : text,
                [finish](bool ok, const QJsonValue& data, const QString& err, int) { finish(ok, data, err); },
                cliId);
        }
        } else {
        const QString href = item.value("href").toString().trimmed();
        QString forwardText = text;
        if (!href.isEmpty() && !forwardText.contains(href)) {
            forwardText = forwardText.isEmpty() ? href : (forwardText + QStringLiteral("\n") + href);
        }
        m_apiClient->session()->sendText(targetThreadId, isGroup, forwardText,
            [finish](bool ok, const QJsonValue& data, const QString& err, int) { finish(ok, data, err); },
            cliId);
    }
}

void ZaloClient::setMobileSyncState(int state, const QString& status, int progress) {
    m_mobileSyncState = state;
    m_mobileSyncStatus = status;
    m_mobileSyncProgress = progress;
    emit mobileSyncStateChanged();
    emit mobileSyncProgressChanged();
}

bool ZaloClient::shouldOfferMobileSync() const {
    if (!m_isLoggedIn || m_myUid.isEmpty() || m_showSyncPrompt) return false;
    QSettings settings;
    if (settings.value(QStringLiteral("mobileSync/done/") + m_myUid).toBool()) return false;
    if (settings.value(QStringLiteral("mobileSync/neverAsk/") + m_myUid).toBool()) return false;
    if (m_mobileSyncState != 0 && m_mobileSyncState != 5) return false;
    if (m_messageModel && m_messageModel->hasAnyMessages()) return false;
    if (m_offlineStore && m_offlineStore->isDatabaseOpen() && m_offlineStore->hasAnyMessages()) return false;

    bool hasNonCloudConversation = false;
    const QString cloudId = myCloudId();
    const auto& convs = m_conversationModel->conversations();
    for (const auto& conv : convs) {
        if (!cloudId.isEmpty() && conv.id == cloudId) continue;
        hasNonCloudConversation = true;
        if (!conv.lastMessage.trimmed().isEmpty() && conv.lastTs > 0) return false;
    }
    return hasNonCloudConversation;
}

void ZaloClient::maybeOfferMobileSync() {
    if (!shouldOfferMobileSync()) return;
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
    m_historySync->handleControl(content);
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

void ZaloClient::setShowBusinessButton(bool val) {
    if (m_showBusinessButton != val) {
        m_showBusinessButton = val;
        if (!val && m_showBusinessToolsModal) {
            setShowBusinessToolsModal(false);
        }
        saveSession();
        emit settingsChanged();
    }
}

void ZaloClient::setShowKikiButton(bool val) {
    if (m_showKikiButton != val) {
        m_showKikiButton = val;
        if (!val && m_currentNavigationTab == QLatin1String("kiki")) {
            setCurrentNavigationTab(QStringLiteral("messages"));
        }
        saveSession();
        emit settingsChanged();
    }
}

void ZaloClient::setHardwareAcceleration(bool val) {
    if (m_hardwareAcceleration == val) return;
    m_hardwareAcceleration = val;
    if (val) m_hardwareAccelerationFailed = false;
    Platform::RenderGuard::setHardwareAccelerationEnabled(val);
    emit settingsChanged();
}

QString ZaloClient::defaultReactionEmoji() const {
    for (const auto& r : kReactions) {
        if (m_defaultReaction == QLatin1String(r.key)) return QString::fromUtf8(r.emoji);
    }
    return QStringLiteral("👍");
}

void ZaloClient::setDefaultReaction(const QString& key) {
    bool valid = false;
    for (const auto& r : kReactions) {
        if (key == QLatin1String(r.key)) {
            valid = true;
            break;
        }
    }
    if (!valid || m_defaultReaction == key) return;
    m_defaultReaction = key;
    saveSession();
    emit settingsChanged();
}

QVariantList ZaloClient::defaultReactionOptions() const {
    static constexpr const char* kCuratedKeys[] = {
        "like", "heart", "haha", "wow", "cry", "angry",
        "ok", "thanks", "love", "kiss", "handclap", "peace"
    };
    QVariantList list;
    for (const char* k : kCuratedKeys) {
        for (const auto& r : kReactions) {
            if (QLatin1String(r.key) == QLatin1String(k)) {
                list.append(QVariantMap{
                    {QStringLiteral("key"), QString::fromLatin1(r.key)},
                    {QStringLiteral("emoji"), QString::fromUtf8(r.emoji)}
                });
                break;
            }
        }
    }
    return list;
}

int ZaloClient::normalizeInterfaceScale(int pct) {
    switch (pct) {
    case 80:
    case 90:
    case 100:
    case 125:
    case 150:
        return pct;
    default:
        return 100;
    }
}

void ZaloClient::setInterfaceScale(int pct) {
    const int normalized = normalizeInterfaceScale(pct);
    if (m_interfaceScale == normalized) return;
    m_interfaceScale = normalized;
    QSettings settings;
    settings.setValue("appearance/interfaceScale", m_interfaceScale);
    emit settingsChanged();
}

void ZaloClient::restartApp() {
    emit restartRequested();
}

QString ZaloClient::appVersion() const {
#ifdef ZALOS_APP_VERSION
    return QStringLiteral(ZALOS_APP_VERSION);
#else
    return QCoreApplication::applicationVersion();
#endif
}

QString ZaloClient::qtRuntimeVersion() const {
    return QString::fromLatin1(qVersion());
}

bool ZaloClient::multimediaAvailable() const {
    return Platform::MultimediaSupport::available();
}

bool ZaloClient::effectsAvailable() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return true;
#else
    return false;
#endif
}

QString ZaloClient::rendererBackend() const {
    switch (QQuickWindow::graphicsApi()) {
    case QSGRendererInterface::Vulkan:
        return QStringLiteral("Vulkan");
    case QSGRendererInterface::OpenGL:
        return QStringLiteral("OpenGL");
    case QSGRendererInterface::Software:
        return L("Phần mềm", "Software");
    default:
        return L("Mặc định", "Default");
    }
}

bool ZaloClient::softwareRenderer() const {
    return QQuickWindow::graphicsApi() == QSGRendererInterface::Software
        || QQuickWindow::sceneGraphBackend().compare(QLatin1String("software"), Qt::CaseInsensitive) == 0
        || qEnvironmentVariable("QT_QUICK_BACKEND").compare(QLatin1String("software"), Qt::CaseInsensitive) == 0;
}

QString ZaloClient::platformName() const {
    return QGuiApplication::platformName();
}

int ZaloClient::compareVersions(const QString& a, const QString& b) {
    auto clean = [](const QString& s) {
        QString t = s.trimmed();
        if (t.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) t = t.mid(1);
        const int dash = t.indexOf(QLatin1Char('-'));
        if (dash >= 0) t.truncate(dash);
        return QVersionNumber::fromString(t);
    };
    return QVersionNumber::compare(clean(a), clean(b));
}

QString ZaloClient::packageKind() const {
    return Platform::packageKindKey(Platform::detectRuntimePackageKind());
}

bool ZaloClient::inAppUpdateSupported() const {
    const auto kind = Platform::detectRuntimePackageKind();
    return kind == Platform::PackageKind::AppImage || kind == Platform::PackageKind::Deb || kind == Platform::PackageKind::Rpm;
}

bool ZaloClient::canInAppUpdate() const {
    const auto kind = Platform::detectRuntimePackageKind();
    return m_updateAvailable
           && !m_updateAssetUrl.isEmpty()
           && (kind == Platform::PackageKind::AppImage
               || kind == Platform::PackageKind::Deb
               || kind == Platform::PackageKind::Rpm);
}

QString ZaloClient::updateStatus() const {
    switch (m_updateState) {
    case UpdateCheckState::Checking:
        return L("Đang kiểm tra bản cập nhật...", "Checking for updates...");
    case UpdateCheckState::NetworkError:
        return L("Không kiểm tra được bản cập nhật. Zalos sẽ tự thử lại sau.",
                 "Couldn't check for updates. Zalos will try again later.");
    case UpdateCheckState::NoRelease:
        return L("Chưa có thông tin bản phát hành mới.", "No release information found.");
    case UpdateCheckState::Available:
        return L("Đã có phiên bản mới: ", "New version available: ") + m_latestVersion;
    case UpdateCheckState::UpToDate:
        return L("Bạn đang dùng phiên bản mới nhất (v", "You are on the latest version (v")
               + appVersion() + QStringLiteral(").");
    case UpdateCheckState::Downloading:
        return L("Đang tải bản cập nhật (%1%)...", "Downloading update (%1%)...")
            .arg(qRound(m_updateProgress * 100.0));
    case UpdateCheckState::Verifying:
        return L("Đang kiểm tra tệp cập nhật...", "Checking the update file...");
    case UpdateCheckState::Installing:
        return L("Đang cài đặt bản cập nhật...", "Installing update...");
    case UpdateCheckState::InstalledRestartRequired:
        return L("Đã cập nhật xong. Khởi động lại Zalos để dùng phiên bản mới.",
                 "Update installed. Restart Zalos to use the new version.");
    case UpdateCheckState::DownloadFailed:
        return L("Tải bản cập nhật thất bại. Vui lòng thử lại hoặc tải từ GitHub.",
                 "Update download failed. Please retry or download from GitHub.");
    case UpdateCheckState::VerifyFailed:
        return L("Tệp cập nhật không khớp với bản phát hành gốc. Đã hủy cập nhật để đảm bảo an toàn.",
                 "The update file doesn't match the official release. Update cancelled for safety.");
    case UpdateCheckState::InstallFailed:
        return L("Không thể tự cài đặt gói cập nhật. File đã tải tại: ",
                 "Could not install automatically. Downloaded file: ") + m_updateSavedPath;
    case UpdateCheckState::SavedToDownloads:
        return L("Không ghi được vào thư mục chứa Zalos. Đã tải bản mới về: ",
                 "Zalos's folder is read-only. Downloaded update to: ") + m_updateSavedPath;
    case UpdateCheckState::Idle:
    default:
        return QString();
    }
}

QNetworkAccessManager* ZaloClient::ensureUpdateNam() {
    if (!m_updateNam) m_updateNam = new QNetworkAccessManager(this);
    return m_updateNam;
}

void ZaloClient::trimMessageCaches() {
    if (m_rawMessages.size() <= 4000) return;
    QStringList removeIds;
    removeIds.reserve(m_rawMessages.size() - 3000);
    for (auto it = m_rawMessages.constBegin(); it != m_rawMessages.constEnd(); ++it) {
        if (m_messageThread.value(it.key()) != m_currentConvId) {
            removeIds.append(it.key());
            if (m_rawMessages.size() - removeIds.size() <= 3000) break;
        }
    }
    for (const QString& id : std::as_const(removeIds)) {
        m_rawMessages.remove(id);
        m_messageThread.remove(id);
        m_reactions.remove(id);
    }
}

void ZaloClient::checkForUpdates() {
    if (m_checkingUpdate || m_updatingApp) return;
    m_checkingUpdate = true;
    m_updateAvailable = false;
    m_updateInstalledRestartRequired = false;
    m_updateProgress = 0.0;
    m_latestVersion.clear();
    m_updateUrl.clear();
    m_updateAssetName.clear();
    m_updateAssetUrl.clear();
    m_updateShaSumsUrl.clear();
    m_updateSavedPath.clear();
    m_updateState = UpdateCheckState::Checking;
    emit updateCheckChanged();

    QNetworkAccessManager* nam = ensureUpdateNam();
    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/cpfriend1721994/zalos/releases/latest")));
    req.setTransferTimeout(15000);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", QByteArray("Zalos/") + appVersion().toUtf8());

    QNetworkReply* reply = nam->get(req);
    const QPointer<ZaloClient> guard(this);
    connect(reply, &QNetworkReply::finished, this, [guard, reply]() {
        const auto err = reply->error();
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (!guard) return;

        guard->m_checkingUpdate = false;
        if (err != QNetworkReply::NoError) {
            guard->m_updateState = UpdateCheckState::NetworkError;
            emit guard->updateCheckChanged();
            return;
        }
        QSettings().setValue(QStringLiteral("settings/updateLastCheck"), QDateTime::currentSecsSinceEpoch());

        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        const QString tag = obj.value(QStringLiteral("tag_name")).toString().trimmed();
        const QString htmlUrl = obj.value(QStringLiteral("html_url")).toString().trimmed();
        if (tag.isEmpty()) {
            guard->m_updateState = UpdateCheckState::NoRelease;
            emit guard->updateCheckChanged();
            return;
        }

        guard->m_latestVersion = tag;
        guard->m_updateUrl = htmlUrl.isEmpty()
                      ? QStringLiteral("https://github.com/cpfriend1721994/zalos/releases")
                      : htmlUrl;
        if (compareVersions(tag, guard->appVersion()) > 0) {
            guard->m_updateAvailable = true;
            guard->m_updateState = UpdateCheckState::Available;
            const auto selected = Platform::selectAssetsForKind(
                Platform::detectRuntimePackageKind(),
                QSysInfo::currentCpuArchitecture(),
                obj.value(QStringLiteral("assets")).toArray());
            guard->m_updateAssetName = selected.package.name;
            guard->m_updateAssetUrl = selected.package.url;
            guard->m_updateShaSumsUrl = selected.sha256Sums.url;
        } else {
            guard->m_updateAvailable = false;
            guard->m_updateState = UpdateCheckState::UpToDate;
        }
        emit guard->updateCheckChanged();
        guard->handleUpdateFound();
    });
}

bool ZaloClient::updateCheckDue(qint64 nowSec, qint64 lastSec, int hours, bool atStartup) {
    if (hours <= 0) return false;
    if (hours == 1) return atStartup;
    return lastSec <= 0 || nowSec - lastSec >= qint64(hours) * 3600;
}

int ZaloClient::updateCheckHours() const {
    return QSettings().value(QStringLiteral("settings/updateCheckHours"), 24).toInt();
}

void ZaloClient::setUpdateCheckHours(int hours) {
    if (hours != 0 && hours != 1 && hours != 24 && hours != 168) return;
    if (hours == updateCheckHours()) return;
    QSettings().setValue(QStringLiteral("settings/updateCheckHours"), hours);
    emit settingsChanged();
    maybeAutoCheckUpdates(false);
}

bool ZaloClient::autoInstallUpdates() const {
    return QSettings().value(QStringLiteral("settings/autoInstallUpdates"), false).toBool();
}

void ZaloClient::setAutoInstallUpdates(bool on) {
    if (on == autoInstallUpdates()) return;
    QSettings().setValue(QStringLiteral("settings/autoInstallUpdates"), on);
    emit settingsChanged();
    if (on) handleUpdateFound();
}

QString ZaloClient::lastUpdateCheckText() const {
    const qint64 last = QSettings().value(QStringLiteral("settings/updateLastCheck"), 0).toLongLong();
    if (last <= 0) return QString();
    return L("Lần kiểm tra gần nhất: ", "Last checked: ")
           + QDateTime::fromSecsSinceEpoch(last).toString(QStringLiteral("HH:mm dd/MM/yyyy"));
}

void ZaloClient::startUpdateSchedule() {
    if (m_updateTimer->isActive()) return;
    m_updateTimer->start();
    QTimer::singleShot(20000, this, [this]() { maybeAutoCheckUpdates(true); });
}

void ZaloClient::maybeAutoCheckUpdates(bool atStartup) {
    if (!m_updateTimer || !m_updateTimer->isActive()) return;
    const qint64 last = QSettings().value(QStringLiteral("settings/updateLastCheck"), 0).toLongLong();
    if (updateCheckDue(QDateTime::currentSecsSinceEpoch(), last, updateCheckHours(), atStartup)) checkForUpdates();
}

void ZaloClient::handleUpdateFound() {
    if (!m_updateAvailable || m_latestVersion.isEmpty() || m_updatingApp || m_updateInstalledRestartRequired) return;
    if (autoInstallUpdates() && canInAppUpdate()) {
        installAvailableUpdate();
        return;
    }
    QSettings settings;
    if (settings.value(QStringLiteral("settings/updateNotifiedVersion")).toString() == m_latestVersion) return;
    settings.setValue(QStringLiteral("settings/updateNotifiedVersion"), m_latestVersion);
    emit notice(L("Đã có Zalos %1. Mở Cài đặt › Về ứng dụng để cập nhật.", "Zalos %1 is available. Open Settings › About to update.")
                    .arg(m_latestVersion));
}

void ZaloClient::installAvailableUpdate() {
    if (m_updatingApp || m_checkingUpdate || !canInAppUpdate()) return;

    const auto kind = Platform::detectRuntimePackageKind();
    const QString appImageTarget = qEnvironmentVariable("APPIMAGE");
    if (m_updateShaSumsUrl.isEmpty()) {
        m_updateState = UpdateCheckState::VerifyFailed;
        emit updateCheckChanged();
        return;
    }
    const QString destPath = Platform::updateFilePath(
        Platform::prepareUpdateDirectory(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)),
        m_updateAssetName);
    if (destPath.isEmpty()) {
        m_updateState = UpdateCheckState::DownloadFailed;
        emit updateCheckChanged();
        return;
    }
    const auto exportUpdate = [](const QString& path) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (dir.isEmpty() || !QDir().mkpath(dir)) return path;
        const QString target = ZaloClient::uniqueFilePath(dir, QFileInfo(path).fileName());
        if (!QFile::copy(path, target)) return path;
        QFile::remove(path);
        return target;
    };

    m_updatingApp = true;
    m_updateInstalledRestartRequired = false;
    m_updateProgress = 0.0;
    m_updateSavedPath.clear();
    m_updateState = UpdateCheckState::Downloading;
    emit updateCheckChanged();

    QNetworkAccessManager* nam = ensureUpdateNam();
    auto* outFile = new QFile(destPath + QStringLiteral(".part"), this);
    if (!outFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        outFile->deleteLater();
        m_updatingApp = false;
        m_updateState = UpdateCheckState::DownloadFailed;
        emit updateCheckChanged();
        return;
    }

    QNetworkRequest req((QUrl(m_updateAssetUrl)));
    req.setTransferTimeout(120000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", QByteArray("Zalos/") + appVersion().toUtf8());

    auto writeFailed = std::make_shared<bool>(false);
    QNetworkReply* reply = nam->get(req);
    const QPointer<ZaloClient> guard(this);
    connect(reply, &QNetworkReply::readyRead, this, [reply, outFile, writeFailed]() {
        const QByteArray chunk = reply->readAll();
        if (!chunk.isEmpty() && !*writeFailed && outFile->write(chunk) != chunk.size()) {
            *writeFailed = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [guard](qint64 recv, qint64 total) {
        if (!guard || total <= 0) return;
        const qreal next = qBound(0.0, qreal(recv) / qreal(total), 1.0);
        if (qAbs(next - guard->m_updateProgress) >= 0.02 || next >= 1.0) {
            guard->m_updateProgress = next;
            emit guard->updateCheckChanged();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [guard, nam, reply, outFile, writeFailed, destPath, kind, appImageTarget,
                                                    exportUpdate]() {
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty() && !*writeFailed && outFile->write(tail) != tail.size()) {
            *writeFailed = true;
        }
        if (!outFile->flush()) *writeFailed = true;
        outFile->close();
        const auto err = reply->error();
        reply->deleteLater();
        if (!guard) {
            outFile->remove();
            outFile->deleteLater();
            return;
        }

        if (*writeFailed || err != QNetworkReply::NoError) {
            outFile->remove();
            outFile->deleteLater();
            guard->m_updatingApp = false;
            guard->m_updateState = UpdateCheckState::DownloadFailed;
            emit guard->updateCheckChanged();
            return;
        }

        QFile::remove(destPath);
        if (!outFile->rename(destPath)) {
            outFile->remove();
            outFile->deleteLater();
            guard->m_updatingApp = false;
            guard->m_updateState = UpdateCheckState::DownloadFailed;
            emit guard->updateCheckChanged();
            return;
        }
        outFile->deleteLater();

        auto applyPackage = [guard, destPath, kind, appImageTarget, exportUpdate]() {
            if (!guard) return;
            if (kind == Platform::PackageKind::AppImage) {
                const QFileInfo targetInfo(appImageTarget);
                if (!targetInfo.dir().exists() || !QFileInfo(targetInfo.dir().absolutePath()).isWritable()) {
                    const QString saved = exportUpdate(destPath);
                    guard->m_updatingApp = false;
                    guard->m_updateSavedPath = saved;
                    guard->m_updateState = UpdateCheckState::SavedToDownloads;
                    emit guard->updateCheckChanged();
                    Fs::FileUtils::openLocalPath(QFileInfo(saved).absolutePath());
                    return;
                }
                guard->m_updateState = UpdateCheckState::Installing;
                emit guard->updateCheckChanged();
                QThreadPool::globalInstance()->start([guard, appImageTarget, destPath, exportUpdate]() {
                    QString replaceErr;
                    const bool replaced = Platform::replaceAppImageFile(appImageTarget, destPath, &replaceErr);
                    if (replaced) QFile::remove(destPath);
                    if (!guard) return;
                    QMetaObject::invokeMethod(guard, [guard, replaced, destPath, exportUpdate]() {
                        if (!guard) return;
                        guard->m_updatingApp = false;
                        if (!replaced) {
                            guard->m_updateSavedPath = exportUpdate(destPath);
                            guard->m_updateState = UpdateCheckState::InstallFailed;
                        } else {
                            guard->m_updateInstalledRestartRequired = true;
                            guard->m_updateState = UpdateCheckState::InstalledRestartRequired;
                        }
                        emit guard->updateCheckChanged();
                    }, Qt::QueuedConnection);
                });
                return;
            }

            guard->m_updateState = UpdateCheckState::Installing;
            emit guard->updateCheckChanged();

            const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
            if (pkexec.isEmpty()) {
                const QString saved = exportUpdate(destPath);
                guard->m_updatingApp = false;
                guard->m_updateSavedPath = saved;
                guard->m_updateState = UpdateCheckState::InstallFailed;
                emit guard->updateCheckChanged();
                Fs::FileUtils::openLocalPath(saved);
                return;
            }

            QStringList args;
            if (kind == Platform::PackageKind::Deb) {
                args = {QStringLiteral("apt-get"), QStringLiteral("install"), QStringLiteral("-y"), destPath};
            } else {
                args = {QStringLiteral("dnf"), QStringLiteral("install"), QStringLiteral("-y"), destPath};
            }

            auto* proc = new QProcess(guard);
            connect(proc, &QProcess::finished, guard, [guard, proc, destPath, exportUpdate](int exitCode, QProcess::ExitStatus status) {
                proc->deleteLater();
                if (!guard) return;
                guard->m_updatingApp = false;
                if (status == QProcess::NormalExit && exitCode == 0) {
                    QFile::remove(destPath);
                    guard->m_updateInstalledRestartRequired = true;
                    guard->m_updateState = UpdateCheckState::InstalledRestartRequired;
                } else {
                    const QString saved = exportUpdate(destPath);
                    guard->m_updateSavedPath = saved;
                    guard->m_updateState = UpdateCheckState::InstallFailed;
                    Fs::FileUtils::openLocalPath(saved);
                }
                emit guard->updateCheckChanged();
            });
            proc->start(pkexec, args);
        };

        if (guard->m_updateShaSumsUrl.isEmpty()) {
            QFile::remove(destPath);
            guard->m_updatingApp = false;
            guard->m_updateState = UpdateCheckState::VerifyFailed;
            emit guard->updateCheckChanged();
            return;
        }

        guard->m_updateState = UpdateCheckState::Verifying;
        emit guard->updateCheckChanged();
        QNetworkRequest sumReq((QUrl(guard->m_updateShaSumsUrl)));
        sumReq.setTransferTimeout(15000);
        sumReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        sumReq.setRawHeader("User-Agent", QByteArray("Zalos/") + guard->appVersion().toUtf8());
        QNetworkReply* sumReply = nam->get(sumReq);
        connect(sumReply, &QNetworkReply::finished, guard, [guard, sumReply, destPath, applyPackage]() {
            const auto sumErr = sumReply->error();
            const QByteArray sumBody = sumReply->readAll();
            sumReply->deleteLater();
            if (!guard) return;

            const QString expected = sumErr == QNetworkReply::NoError
                ? Platform::parseSha256ForFile(sumBody, guard->m_updateAssetName)
                : QString();
            if (expected.isEmpty()) {
                QFile::remove(destPath);
                guard->m_updatingApp = false;
                guard->m_updateState = UpdateCheckState::VerifyFailed;
                emit guard->updateCheckChanged();
                return;
            }
            QThreadPool::globalInstance()->start([guard, destPath, expected, applyPackage]() {
                const bool verified = Platform::verifyFileSha256(destPath, expected);
                if (!verified) QFile::remove(destPath);
                if (!guard) return;
                QMetaObject::invokeMethod(guard, [guard, verified, applyPackage]() {
                    if (!guard) return;
                    if (!verified) {
                        guard->m_updatingApp = false;
                        guard->m_updateState = UpdateCheckState::VerifyFailed;
                        emit guard->updateCheckChanged();
                        return;
                    }
                    applyPackage();
                }, Qt::QueuedConnection);
            });
        });
    });
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
    if (m_trayAvailableOverride >= 0) return m_trayAvailableOverride != 0;
    const auto* tray = Platform::SystemTrayController::s_instance;
    return tray && tray->cachedAvailable();
}

QString ZaloClient::autostartFilePath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/autostart/zalos.desktop";
}

bool ZaloClient::startWithSystem() const {
    if (Platform::BackgroundPortal::inFlatpak()) return QSettings().value("settings/flatpakAutostart", false).toBool();
    return QFile::exists(autostartFilePath());
}

void ZaloClient::setStartWithSystem(bool val) {
    if (Platform::BackgroundPortal::inFlatpak()) {
        if (!m_backgroundPortal) {
            m_backgroundPortal = new Platform::BackgroundPortal(this);
            connect(m_backgroundPortal, &Platform::BackgroundPortal::finished, this, [this](bool ok, bool autostart) {
                const bool wanted = QSettings().value("settings/flatpakAutostartWanted", false).toBool();
                QSettings().setValue("settings/flatpakAutostart", ok && autostart);
                if (wanted && !(ok && autostart)) {
                    emit notice(ok ? L("Hệ thống chưa cho phép Zalos khởi động cùng máy.", "The system didn't allow Zalos to start at login.")
                                   : L("Máy này chưa hỗ trợ cho Zalos khởi động cùng hệ thống.", "Starting Zalos at login isn't available on this computer."));
                }
                emit settingsChanged();
            });
        }
        QSettings().setValue("settings/flatpakAutostartWanted", val);
        m_backgroundPortal->requestAutostart(val, L("Mở Zalos khi đăng nhập để nhận tin nhắn", "Open Zalos at login to receive messages"));
        return;
    }
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
    emit incomingCallStateChanged(true);

    notifyIncomingCallDbus(m_incomingCallerName);
}
#endif

void ZaloClient::answerCall(bool withCamera) {
    m_incomingCallActive = false;
    if (m_calls) m_calls->accept(!withCamera);
    emit incomingCallStateChanged(false);
}

void ZaloClient::declineCall() {
    m_incomingCallActive = false;
    if (m_calls) m_calls->reject();
    emit incomingCallStateChanged(false);
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
    if (m_isLoggedIn) m_apiClient->session()->probeRealtime();
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

    auto runCapture = [this, onFinishedCapture](const QString& program, const QStringList& args) {
        auto* proc = new QProcess(this);
        proc->setProcessEnvironment(Fs::FileUtils::launchEnvironment(QProcessEnvironment::systemEnvironment()));
        auto settled = std::make_shared<bool>(false);
        connect(proc, &QProcess::errorOccurred, this, [proc, onFinishedCapture, settled](QProcess::ProcessError) {
            if (*settled) return;
            *settled = true;
            proc->deleteLater();
            onFinishedCapture(false);
        });
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [proc, onFinishedCapture, settled](int exitCode, QProcess::ExitStatus status) {
            if (*settled) return;
            *settled = true;
            proc->deleteLater();
            onFinishedCapture(status == QProcess::NormalExit && exitCode == 0);
        });
        proc->start(program, args);
    };

    const QString spectacle = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
    if (!spectacle.isEmpty()) {
        runCapture(spectacle, {QStringLiteral("-r"), QStringLiteral("-b"), QStringLiteral("-n"), QStringLiteral("-o"), tempPath});
        return;
    }

    const QString gnomeShot = QStandardPaths::findExecutable(QStringLiteral("gnome-screenshot"));
    if (!gnomeShot.isEmpty()) {
        runCapture(gnomeShot, {QStringLiteral("-a"), QStringLiteral("-f"), tempPath});
        return;
    }

    const QString flameshot = QStandardPaths::findExecutable(QStringLiteral("flameshot"));
    if (!flameshot.isEmpty()) {
        runCapture(flameshot, {QStringLiteral("gui"), QStringLiteral("-r"), QStringLiteral("-p"), tempPath});
        return;
    }

    const QString grim = QStandardPaths::findExecutable(QStringLiteral("grim"));
    const QString slurp = QStandardPaths::findExecutable(QStringLiteral("slurp"));
    if (!grim.isEmpty()) {
        if (!slurp.isEmpty()) {
            runCapture(QStringLiteral("sh"),
                       {QStringLiteral("-c"), QStringLiteral("geom=$(\"$1\") || exit 1; exec \"$2\" -g \"$geom\" \"$3\""),
                        QStringLiteral("sh"), slurp, grim, tempPath});
        } else {
            runCapture(grim, {tempPath});
        }
        return;
    }

    const QString maim = QStandardPaths::findExecutable(QStringLiteral("maim"));
    if (!maim.isEmpty()) {
        runCapture(maim, {QStringLiteral("-s"), tempPath});
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
    if (response == 1) return;
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

namespace {

void loadBusinessForTab(ZaloClient* self, const QString& tab) {
    if (tab == QLatin1String("catalog")) self->loadBusinessCatalogs();
    else if (tab == QLatin1String("autoreply")) self->loadBusinessAutoReplies();
    else self->loadBusinessProfile();
}

}

void ZaloClient::openBusinessTools() {
    m_businessTab = QStringLiteral("profile");
    m_showBusinessToolsModal = true;
    emit businessTabChanged();
    emit businessToolsModalChanged();
    loadBusinessProfile();
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
    loadBusinessCatalogs();
}

void ZaloClient::openBusinessAutoReply() {
    m_businessTab = QStringLiteral("autoreply");
    m_showBusinessToolsModal = true;
    emit businessTabChanged();
    emit businessToolsModalChanged();
    loadBusinessAutoReplies();
}

void ZaloClient::openBusinessPackage() {
    m_businessTab = QStringLiteral("package");
    m_showBusinessToolsModal = true;
    emit businessTabChanged();
    emit businessToolsModalChanged();
    loadBusinessProfile();
}

void ZaloClient::setBusinessTab(const QString& tab) {
    if (tab == m_businessTab) return;
    m_businessTab = tab;
    emit businessTabChanged();
    if (m_showBusinessToolsModal) loadBusinessForTab(this, m_businessTab);
}

void ZaloClient::setShowBusinessToolsModal(bool show) {
    if (show == m_showBusinessToolsModal) return;
    m_showBusinessToolsModal = show;
    emit businessToolsModalChanged();
    if (show) loadBusinessForTab(this, m_businessTab);
}

void ZaloClient::loadBusinessProfile() {
    if (!m_isLoggedIn || m_myUid.isEmpty() || !m_apiClient || !m_apiClient->session()) return;
    m_businessLoading = true;
    emit businessDataChanged();
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->fetchBizAccount(m_myUid, [self](bool ok, const QJsonValue& data, const QString& err, int code) {
        if (!self) return;
        self->m_businessLoading = false;
        if (!ok) {
            if (code != 0) emit self->notice(self->friendlyError(err, code));
            emit self->businessDataChanged();
            return;
        }
        const QJsonObject obj = data.toObject();
        const QJsonObject biz = obj.value(QLatin1String("biz")).toObject();
        const QJsonObject startPage = obj.value(QLatin1String("setting_start_page")).toObject();
        const int pkgId = obj.value(QLatin1String("pkgId")).toVariant().toInt();
        self->m_businessProfile = QVariantMap{
            {"pkgId", pkgId},
            {"cate", biz.value(QLatin1String("cate")).toVariant().toInt()},
            {"addr", biz.value(QLatin1String("addr")).toString()},
            {"website", biz.value(QLatin1String("website")).toString()},
            {"email", biz.value(QLatin1String("email")).toString()},
            {"desc", biz.value(QLatin1String("desc")).toString()},
            {"ctaCatalog", startPage.value(QLatin1String("cta_catalog")).toString()}
        };
        if (pkgId > 0 && !self->m_hasBusinessPackage) {
            self->m_hasBusinessPackage = true;
            emit self->userProfileChanged();
        }
        emit self->businessDataChanged();
    });
}

void ZaloClient::loadBusinessCatalogs() {
    if (!m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
    m_businessLoading = true;
    emit businessDataChanged();
    fetchBusinessCatalogPage(0, {}, ++m_catalogLoadGen);
}

void ZaloClient::fetchBusinessCatalogPage(int page, QVariantList collected, quint64 gen) {
    if (!m_apiClient || !m_apiClient->session()) return;
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->fetchCatalogList(page, kCatalogPageLimit,
        [self, page, collected, gen](bool ok, const QJsonValue& data, const QString& err, int code) mutable {
        if (!self || gen != self->m_catalogLoadGen) return;
        if (!ok) {
            if (page > 0 && !collected.isEmpty()) {
                self->applyBusinessCatalogs(collected);
                return;
            }
            self->m_businessLoading = false;
            emit self->notice(self->friendlyError(err, code));
            emit self->businessDataChanged();
            return;
        }
        const QJsonObject obj = data.toObject();
        QSet<QString> seen;
        for (const QVariant& c : std::as_const(collected)) seen.insert(c.toMap().value(QStringLiteral("id")).toString());
        const QJsonArray items = obj.value(QLatin1String("items")).toArray();
        for (const QJsonValue& v : items) {
            const QJsonObject c = v.toObject();
            const QString id = idOf(c.value(QLatin1String("id")));
            if (id.isEmpty() || seen.contains(id)) continue;
            seen.insert(id);
            collected.append(QVariantMap{
                {"id", id},
                {"name", c.value(QLatin1String("name")).toString()},
                {"isDefault", c.value(QLatin1String("isDefault")).toBool()},
                {"totalProduct", c.value(QLatin1String("totalProduct")).toVariant().toInt()},
                {"path", c.value(QLatin1String("path")).toString()}
            });
        }
        const bool hasMore = obj.value(QLatin1String("has_more")).toVariant().toInt() != 0;
        if (hasMore && !items.isEmpty() && page + 1 < kMaxCatalogPages) {
            self->fetchBusinessCatalogPage(page + 1, std::move(collected), gen);
            return;
        }
        self->applyBusinessCatalogs(collected);
    });
}

void ZaloClient::applyBusinessCatalogs(const QVariantList& cats) {
    m_businessCatalogs = cats;
    QString nextSelected = m_selectedCatalogId;
    bool selectedFound = false;
    for (const QVariant& c : cats) {
        if (c.toMap().value(QStringLiteral("id")).toString() == nextSelected) {
            selectedFound = true;
            break;
        }
    }
    if (!selectedFound) nextSelected = cats.isEmpty() ? QString() : cats.first().toMap().value(QStringLiteral("id")).toString();
    m_selectedCatalogId = nextSelected;
    if (nextSelected.isEmpty()) {
        m_businessProducts.clear();
        m_businessLoading = false;
        emit businessDataChanged();
        return;
    }
    selectBusinessCatalog(nextSelected);
}

void ZaloClient::selectBusinessCatalog(const QString& catalogId) {
    m_selectedCatalogId = catalogId;
    if (catalogId.isEmpty() || !m_isLoggedIn || !m_apiClient || !m_apiClient->session()) {
        m_businessProducts.clear();
        m_businessLoading = false;
        emit businessDataChanged();
        return;
    }
    m_businessLoading = true;
    emit businessDataChanged();
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->fetchProductCatalogList(catalogId, 0, 100, [self, catalogId](bool ok, const QJsonValue& data, const QString& err, int code) {
        if (!self || self->m_selectedCatalogId != catalogId) return;
        self->m_businessLoading = false;
        if (!ok) {
            emit self->notice(self->friendlyError(err, code));
            emit self->businessDataChanged();
            return;
        }
        QVariantList prods;
        const QJsonArray items = data.toObject().value(QLatin1String("items")).toArray();
        for (const QJsonValue& v : items) {
            const QJsonObject p = v.toObject();
            const QString pid = idOf(p.value(QLatin1String("product_id")));
            if (pid.isEmpty()) continue;
            QStringList photos;
            for (const QJsonValue& ph : p.value(QLatin1String("product_photos")).toArray()) {
                const QString u = ph.toString();
                if (!u.isEmpty()) photos.append(u);
            }
            const QString currency = p.value(QLatin1String("currency_unit")).toString(QStringLiteral("₫"));
            const QString price = p.value(QLatin1String("price")).toVariant().toString();
            prods.append(QVariantMap{
                {"productId", pid},
                {"catalogId", idOf(p.value(QLatin1String("catalog_id")))},
                {"name", p.value(QLatin1String("product_name")).toString()},
                {"price", price},
                {"currency", currency},
                {"description", p.value(QLatin1String("description")).toString()},
                {"path", p.value(QLatin1String("path")).toString()},
                {"photos", photos},
                {"image", photos.isEmpty() ? QString() : photos.first()},
                {"createTime", p.value(QLatin1String("create_time")).toVariant().toLongLong()}
            });
        }
        self->m_businessProducts = prods;
        emit self->businessDataChanged();
    });
}

void ZaloClient::createBusinessCatalog(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || !m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->createCatalog(trimmed, [self](bool ok, const QJsonValue& data, const QString& err, int code) {
        if (!self) return;
        if (!ok) {
            emit self->notice(self->friendlyError(err, code));
            return;
        }
        const QString createdId = idOf(data.toObject().value(QLatin1String("item")).toObject().value(QLatin1String("id")));
        if (!createdId.isEmpty()) self->m_selectedCatalogId = createdId;
        emit self->notice(self->L("Đã tạo danh mục sản phẩm", "Catalog created"));
        self->loadBusinessCatalogs();
    });
}

void ZaloClient::updateBusinessCatalog(const QString& catalogId, const QString& name) {
    const QString trimmed = name.trimmed();
    if (catalogId.isEmpty() || trimmed.isEmpty() || !m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->updateCatalog(catalogId, trimmed, [self](bool ok, const QJsonValue&, const QString& err, int code) {
        if (!self) return;
        if (!ok) {
            emit self->notice(self->friendlyError(err, code));
            return;
        }
        emit self->notice(self->L("Đã cập nhật danh mục", "Catalog updated"));
        self->loadBusinessCatalogs();
    });
}

void ZaloClient::deleteBusinessCatalog(const QString& catalogId) {
    if (catalogId.isEmpty() || !m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->deleteCatalog(catalogId, [self, catalogId](bool ok, const QJsonValue&, const QString& err, int code) {
        if (!self) return;
        if (!ok) {
            emit self->notice(self->friendlyError(err, code));
            return;
        }
        if (self->m_selectedCatalogId == catalogId) self->m_selectedCatalogId.clear();
        emit self->notice(self->L("Đã xóa danh mục", "Catalog deleted"));
        self->loadBusinessCatalogs();
    });
}

void ZaloClient::createBusinessProduct(const QString& catalogId, const QString& name,
                                         const QString& price, const QString& description) {
    const QString cid = catalogId.isEmpty() ? m_selectedCatalogId : catalogId;
    if (cid.isEmpty() || name.trimmed().isEmpty() || !m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->createProductCatalog(
        cid, name.trimmed(), price.trimmed(), description.trimmed(), {},
        [self, cid](bool ok, const QJsonValue&, const QString& err, int code) {
            if (!self) return;
            if (!ok) {
                emit self->notice(self->friendlyError(err, code));
                return;
            }
            emit self->notice(self->L("Đã thêm sản phẩm vào danh mục", "Product added to catalog"));
            self->selectBusinessCatalog(cid);
        });
}

void ZaloClient::updateBusinessProduct(const QString& catalogId, const QString& productId,
                                         const QString& name, const QString& price,
                                         const QString& description, qint64 createTime) {
    const QString cid = catalogId.isEmpty() ? m_selectedCatalogId : catalogId;
    if (cid.isEmpty() || productId.isEmpty() || name.trimmed().isEmpty()
        || !m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
    QStringList photos;
    for (const QVariant& item : std::as_const(m_businessProducts)) {
        const QVariantMap m = item.toMap();
        if (m.value(QLatin1String("productId")).toString() == productId) {
            photos = m.value(QLatin1String("photos")).toStringList();
            break;
        }
    }
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->updateProductCatalog(
        cid, productId, name.trimmed(), price.trimmed(), description.trimmed(), photos, createTime,
        [self, cid](bool ok, const QJsonValue&, const QString& err, int code) {
            if (!self) return;
            if (!ok) {
                emit self->notice(self->friendlyError(err, code));
                return;
            }
            emit self->notice(self->L("Đã cập nhật sản phẩm", "Product updated"));
            self->selectBusinessCatalog(cid);
        });
}

void ZaloClient::deleteBusinessProduct(const QString& catalogId, const QString& productId) {
    const QString cid = catalogId.isEmpty() ? m_selectedCatalogId : catalogId;
    if (cid.isEmpty() || productId.isEmpty() || !m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->deleteProductCatalog(
        cid, {productId},
        [self, cid](bool ok, const QJsonValue&, const QString& err, int code) {
            if (!self) return;
            if (!ok) {
                emit self->notice(self->friendlyError(err, code));
                return;
            }
            emit self->notice(self->L("Đã xóa sản phẩm", "Product deleted"));
            self->selectBusinessCatalog(cid);
        });
}

void ZaloClient::loadBusinessAutoReplies() {
    if (!m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
    m_businessLoading = true;
    emit businessDataChanged();
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->fetchAutoReplyList(m_appLanguage, [self](bool ok, const QJsonValue& data, const QString& err, int code) {
        if (!self) return;
        self->m_businessLoading = false;
        if (!ok) {
            emit self->notice(self->friendlyError(err, code));
            emit self->businessDataChanged();
            return;
        }
        QVariantList rules;
        const QJsonArray items = data.toObject().value(QLatin1String("item")).toArray();
        for (const QJsonValue& v : items) {
            const QJsonObject r = v.toObject();
            rules.append(QVariantMap{
                {"id", r.value(QLatin1String("id")).toVariant().toLongLong()},
                {"content", r.value(QLatin1String("content")).toString()},
                {"enable", r.value(QLatin1String("enable")).toBool()},
                {"scope", r.value(QLatin1String("scope")).toInt()},
                {"startTime", r.value(QLatin1String("startTime")).toVariant().toLongLong()},
                {"endTime", r.value(QLatin1String("endTime")).toVariant().toLongLong()},
                {"uids", r.value(QLatin1String("uids")).toVariant().toStringList()}
            });
        }
        self->m_businessAutoReplies = rules;
        emit self->businessDataChanged();
    });
}

void ZaloClient::createBusinessAutoReply(const QString& content, bool enable, int scope,
                                           qint64 startTime, qint64 endTime) {
    const QString trimmed = content.trimmed();
    if (trimmed.isEmpty() || !m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->createAutoReply(
        m_appLanguage, trimmed, enable, startTime, endTime, scope, {},
        [self](bool ok, const QJsonValue&, const QString& err, int code) {
            if (!self) return;
            if (!ok) {
                emit self->notice(self->friendlyError(err, code));
                return;
            }
            emit self->notice(self->L("Đã tạo cấu hình trả lời tự động", "Auto reply created"));
            self->loadBusinessAutoReplies();
        });
}

void ZaloClient::updateBusinessAutoReply(qint64 id, const QString& content, bool enable, int scope,
                                           qint64 startTime, qint64 endTime) {
    const QString trimmed = content.trimmed();
    if (id <= 0 || trimmed.isEmpty() || !m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
    QStringList uids;
    for (const QVariant& v : std::as_const(m_businessAutoReplies)) {
        const QVariantMap m = v.toMap();
        if (m.value(QLatin1String("id")).toLongLong() == id) {
            uids = m.value(QLatin1String("uids")).toStringList();
            break;
        }
    }
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->updateAutoReply(
        m_appLanguage, id, trimmed, enable, startTime, endTime, scope, uids,
        [self](bool ok, const QJsonValue&, const QString& err, int code) {
            if (!self) return;
            if (!ok) {
                emit self->notice(self->friendlyError(err, code));
                return;
            }
            emit self->notice(self->L("Đã cập nhật cấu hình trả lời tự động", "Auto reply updated"));
            self->loadBusinessAutoReplies();
        });
}

void ZaloClient::deleteBusinessAutoReply(qint64 id) {
    if (id <= 0 || !m_isLoggedIn || !m_apiClient || !m_apiClient->session()) return;
    QPointer<ZaloClient> self(this);
    m_apiClient->session()->deleteAutoReply(m_appLanguage, id, [self](bool ok, const QJsonValue&, const QString& err, int code) {
        if (!self) return;
        if (!ok) {
            emit self->notice(self->friendlyError(err, code));
            return;
        }
        emit self->notice(self->L("Đã xóa cấu hình trả lời tự động", "Auto reply deleted"));
        self->loadBusinessAutoReplies();
    });
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
                Fs::FileUtils::openLocalPath(tempPath);
            }
            emit notice(L("Đã gửi lệnh in.", "Sent to printer."));
        }
    });
}

QVariantMap ZaloClient::windowGeometry() const {
    QSettings settings;
    const bool hasPos = settings.contains(QStringLiteral("window/x"))
                        && settings.contains(QStringLiteral("window/y"));
    int x = settings.value(QStringLiteral("window/x"), -1).toInt();
    int y = settings.value(QStringLiteral("window/y"), -1).toInt();
    int w = qMax(480, settings.value(QStringLiteral("window/width"), 1040).toInt());
    int h = qMax(440, settings.value(QStringLiteral("window/height"), 680).toInt());
    const bool maximized = settings.value(QStringLiteral("window/maximized"), false).toBool();

    QScreen* screen = nullptr;
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (hasPos && !screens.isEmpty()) {
        const QRect saved(x, y, w, h);
        screen = QGuiApplication::screenAt(saved.center());
        if (!screen) {
            for (QScreen* s : screens) {
                if (s && s->availableGeometry().intersects(saved)) {
                    screen = s;
                    break;
                }
            }
        }
    }
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect avail = screen->availableGeometry();
        if (avail.isValid()) {
            w = qBound(qMin(480, avail.width()), w, avail.width());
            h = qBound(qMin(440, avail.height()), h, avail.height());
            if (hasPos) {
                x = qBound(avail.left(), x, avail.right() - w + 1);
                y = qBound(avail.top(), y, avail.bottom() - h + 1);
            }
        }
    }

    QVariantMap map;
    map[QStringLiteral("x")] = x;
    map[QStringLiteral("y")] = y;
    map[QStringLiteral("hasPosition")] = hasPos && screen != nullptr;
    map[QStringLiteral("width")] = w;
    map[QStringLiteral("height")] = h;
    map[QStringLiteral("maximized")] = maximized;
    return map;
}

void ZaloClient::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings;
    if (!maximized && width > 0 && height > 0) {
        settings.setValue(QStringLiteral("window/x"), x);
        settings.setValue(QStringLiteral("window/y"), y);
        settings.setValue(QStringLiteral("window/width"), width);
        settings.setValue(QStringLiteral("window/height"), height);
    }
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

void ZaloClient::syncCallTrayState() {
    auto* tray = Platform::SystemTrayController::s_instance;
    if (!tray) return;
    if (m_boundTray != tray) {
        m_boundTray = tray;
        connect(tray, &Platform::SystemTrayController::showCallWindowRequested, this, [this]() {
            emit windowActivationRequested(QString());
        });
        connect(tray, &Platform::SystemTrayController::acceptCallRequested, this, [this](bool noCam) {
            if (m_calls) m_calls->accept(noCam);
        });
        connect(tray, &Platform::SystemTrayController::rejectCallRequested, this, [this]() {
            if (m_calls) m_calls->reject();
        });
        connect(tray, &Platform::SystemTrayController::hangupCallRequested, this, [this]() {
            if (m_calls) m_calls->hangup();
        });
        connect(tray, &Platform::SystemTrayController::toggleCallMuteRequested, this, [this]() {
            if (m_calls) m_calls->setMuted(!m_calls->muted());
        });
        connect(tray, &Platform::SystemTrayController::toggleCallSpeakerRequested, this, [this]() {
            if (m_calls) m_calls->setSpeakerOff(!m_calls->speakerOff());
        });
        connect(tray, &Platform::SystemTrayController::toggleCallCameraRequested, this, [this]() {
            if (m_calls) m_calls->setCamera(!m_calls->cameraOn());
        });
        connect(tray, &Platform::SystemTrayController::toggleCallScreenShareRequested, this, [this]() {
            if (m_calls) m_calls->setScreenShare(!m_calls->screenShareOn());
        });
    }
    if (m_calls) {
        tray->setCallState(m_calls->phase(), m_calls->peerName(), m_calls->isVideo(), m_calls->muted(),
                           m_calls->speakerOff(), m_calls->cameraOn(), m_calls->screenShareOn());
    }
}

void ZaloClient::notifyWindowVisibility(bool visible) {
    setWindowVisible(visible);
    if (Platform::SystemTrayController::s_instance) {
        Platform::SystemTrayController::s_instance->setWindowVisible(visible);
    }
    syncCallTrayState();
}

void ZaloClient::hideWindowToTray() {
    syncCallTrayState();
    emit hideWindowRequested();
}

}
