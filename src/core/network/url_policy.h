#pragma once

#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace Zalos::Core::Network {

class UrlPolicy {
public:
    static bool isWebUrl(const QUrl& url);
    static bool isWebUrl(const QString& url);

    static QString sanitizeRemoteReference(const QString& value);

    static bool isLocalReference(const QString& value);

    static void sanitizeRemoteReferences(QString* mediaUrl, QString* href, QVariantMap* extra);

    static QUrl upgradeToHttps(const QUrl& url);
};

}
