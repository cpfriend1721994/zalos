#include "message_search_model.h"

namespace Zalos::Core::Models {

MessageSearchModel::MessageSearchModel(Provider provider, QObject* parent)
    : QAbstractListModel(parent), m_provider(std::move(provider)) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, &MessageSearchModel::run);
}

int MessageSearchModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int(m_hits.size());
}

QVariant MessageSearchModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_hits.size()) return {};
    const Hit& h = m_hits.at(index.row());
    switch (role) {
    case MsgIdRole: return h.msgId;
    case ConvIdRole: return h.convId;
    case ConvNameRole: return h.convName;
    case AvatarRole: return h.avatar;
    case IsGroupRole: return h.isGroup;
    case SenderNameRole: return h.senderName;
    case TextRole: return h.text;
    case TsRole: return h.ts;
    default: return {};
    }
}

QHash<int, QByteArray> MessageSearchModel::roleNames() const {
    return {{MsgIdRole, "msgId"}, {ConvIdRole, "convId"}, {ConvNameRole, "convName"}, {AvatarRole, "avatar"},
            {IsGroupRole, "isGroup"}, {SenderNameRole, "senderName"}, {TextRole, "text"}, {TsRole, "ts"}};
}

void MessageSearchModel::setQuery(const QString& query) {
    m_query = query.trimmed();
    if (m_query.size() < 2) {
        m_debounce.stop();
        clear();
        return;
    }
    m_debounce.start();
}

void MessageSearchModel::clear() {
    if (m_hits.isEmpty()) return;
    beginResetModel();
    m_hits.clear();
    endResetModel();
    emit countChanged();
}

void MessageSearchModel::runNow() {
    m_debounce.stop();
    run();
}

void MessageSearchModel::run() {
    QVector<Hit> hits = (m_provider && m_query.size() >= 2) ? m_provider(m_query) : QVector<Hit>();
    beginResetModel();
    m_hits = std::move(hits);
    endResetModel();
    emit countChanged();
}

}
