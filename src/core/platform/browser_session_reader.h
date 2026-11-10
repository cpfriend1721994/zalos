#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace Zalos::Core::Platform {

struct BrowserSessionResult {
    bool success = false;
    QString reason;
    QString browser;
    QString imei;
    QString userAgent;
    QJsonArray cookies;
};

class BrowserSessionReader {
public:
    static BrowserSessionResult readSession();

private:
    static QByteArray readSafeStorageKey(const QString& appAttr, const QString& folderName);
    static QByteArray deriveAesKey(const QByteArray& password);
    static QByteArray decryptAesCbc(const QByteArray& key, const QByteArray& ciphertext);
    static QString findImeiInDir(const QString& storageDir);
    static QString extractImeiFromBytes(const QByteArray& data);
};

} // namespace Zalos::Core::Platform

