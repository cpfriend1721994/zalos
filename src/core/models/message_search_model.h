#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QTimer>
#include <QVector>

#include <functional>

namespace Zalos::Core::Models {

class MessageSearchModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    struct Hit {
        QString msgId;
        QString convId;
        QString convName;
        QString avatar;
        bool isGroup{false};
        QString senderName;
        QString text;
        qint64 ts{0};
    };
    enum Roles { MsgIdRole = Qt::UserRole + 1, ConvIdRole, ConvNameRole, AvatarRole, IsGroupRole, SenderNameRole,
                 TextRole, TsRole };

    using Provider = std::function<QVector<Hit>(const QString& query)>;
    explicit MessageSearchModel(Provider provider, QObject* parent = nullptr, int minLength = 2);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_hits.size()); }

    Q_INVOKABLE void setQuery(const QString& query);
    Q_INVOKABLE void clear();
    void runNow();

signals:
    void countChanged();

private:
    void run();
    Provider m_provider;
    QVector<Hit> m_hits;
    QString m_query;
    QTimer m_debounce;
    int m_minLength{2};
};

}
