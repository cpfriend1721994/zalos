#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace Zalos::Core::Platform {

class BackgroundPortal : public QObject {
    Q_OBJECT

public:
    explicit BackgroundPortal(QObject* parent = nullptr);

    static bool inFlatpak();
    void requestAutostart(bool enable, const QString& reason);

signals:
    void finished(bool ok, bool autostart);

private slots:
    void onResponse(uint response, const QVariantMap& results);

private:
    void clearRequest();

    QString m_requestPath;
    quint64 m_seq{0};
};

}
