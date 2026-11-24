#include "browser_session_reader.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <openssl/evp.h>

namespace Zalos::Core::Platform {

namespace {

struct BrowserDef {
    const char* displayName;
    const char* profileRoot;
    const char* appAttr;
    const char* kwalletFolder;
};

constexpr int kDbusTimeoutMs = 1500;

bool serviceRegistered(const QDBusConnection& bus, const QString& service) {
    return bus.isConnected() && bus.interface() && bus.interface()->isServiceRegistered(service).value();
}

QByteArray secretServiceLookup(const QString& application) {
    const QString service = QStringLiteral("org.freedesktop.secrets");
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!serviceRegistered(bus, service)) return {};
    qDBusRegisterMetaType<QMap<QString, QString>>();
    QDBusInterface secrets(service, QStringLiteral("/org/freedesktop/secrets"),
                           QStringLiteral("org.freedesktop.Secret.Service"), bus);
    secrets.setTimeout(kDbusTimeoutMs);
    const QDBusMessage opened = secrets.call(QStringLiteral("OpenSession"), QStringLiteral("plain"),
                                             QVariant::fromValue(QDBusVariant(QString())));
    if (opened.type() != QDBusMessage::ReplyMessage || opened.arguments().size() < 2) return {};
    const QDBusObjectPath session = qdbus_cast<QDBusObjectPath>(opened.arguments().at(1));
    QByteArray secret;
    const QMap<QString, QString> attributes{{QStringLiteral("application"), application}};
    const QDBusMessage found = secrets.call(QStringLiteral("SearchItems"), QVariant::fromValue(attributes));
    if (found.type() == QDBusMessage::ReplyMessage && !found.arguments().isEmpty()) {
        const auto unlocked = qdbus_cast<QList<QDBusObjectPath>>(found.arguments().at(0));
        for (const QDBusObjectPath& item : unlocked) {
            QDBusInterface entry(service, item.path(), QStringLiteral("org.freedesktop.Secret.Item"), bus);
            entry.setTimeout(kDbusTimeoutMs);
            const QDBusMessage reply = entry.call(QStringLiteral("GetSecret"), QVariant::fromValue(session));
            if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) continue;
            const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
            QDBusObjectPath owner;
            QByteArray parameters;
            QByteArray value;
            QString contentType;
            arg.beginStructure();
            arg >> owner >> parameters >> value >> contentType;
            arg.endStructure();
            if (!value.isEmpty()) {
                secret = value;
                break;
            }
        }
    }
    QDBusInterface(service, session.path(), QStringLiteral("org.freedesktop.Secret.Session"), bus)
        .call(QDBus::NoBlock, QStringLiteral("Close"));
    return secret.trimmed();
}

QByteArray kwalletLookup(const QString& folder, const QString& key) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString appId = QStringLiteral("Zalos");
    for (const auto& [service, path] : {std::pair{"org.kde.kwalletd6", "/modules/kwalletd6"},
                                        std::pair{"org.kde.kwalletd5", "/modules/kwalletd5"}}) {
        if (!serviceRegistered(bus, QString::fromLatin1(service))) continue;
        QDBusInterface wallet(QString::fromLatin1(service), QString::fromLatin1(path), QStringLiteral("org.kde.KWallet"), bus);
        wallet.setTimeout(kDbusTimeoutMs);
        const QDBusReply<QString> name = wallet.call(QStringLiteral("networkWallet"));
        const QDBusReply<int> handle = wallet.call(QStringLiteral("open"),
                                                   name.isValid() && !name.value().isEmpty() ? name.value()
                                                                                             : QStringLiteral("kdewallet"),
                                                   qlonglong(0), appId);
        if (!handle.isValid() || handle.value() < 0) continue;
        const QDBusReply<QString> password = wallet.call(QStringLiteral("readPassword"), handle.value(), folder, key, appId);
        wallet.call(QDBus::NoBlock, QStringLiteral("close"), handle.value(), false, appId);
        if (password.isValid() && !password.value().isEmpty()) return password.value().toUtf8();
    }
    return {};
}

const BrowserDef kBrowsers[] = {
    {"Thorium", "thorium", "thorium", "Thorium"},
    {"Chromium", "chromium", "chromium", "Chromium"},
    {"Google Chrome", "google-chrome", "chrome", "Chrome"},
    {"Brave", "BraveSoftware/Brave-Browser", "brave", "Brave"},
    {"Microsoft Edge", "microsoft-edge", "chromium", "Chromium"},
    {"Vivaldi", "vivaldi", "chrome", "Chrome"},
};

const char* kDefaultUserAgent =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

}

QByteArray BrowserSessionReader::deriveAesKey(const QByteArray& password) {
    QByteArray key(16, Qt::Uninitialized);
    const unsigned char salt[] = "saltysalt";
    if (PKCS5_PBKDF2_HMAC_SHA1(password.constData(), password.length(),
                               salt, sizeof(salt) - 1,
                               1, key.size(),
                               reinterpret_cast<unsigned char*>(key.data())) == 1) {
        return key;
    }
    return {};
}

QByteArray BrowserSessionReader::decryptAesCbc(const QByteArray& key, const QByteArray& ciphertext) {
    if (ciphertext.size() < 16) return {};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    QByteArray iv(16, ' ');
    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    QByteArray plaintext(ciphertext.size() + 16, Qt::Uninitialized);
    int outLen1 = 0;
    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plaintext.data()), &outLen1,
                          reinterpret_cast<const unsigned char*>(ciphertext.constData()),
                          ciphertext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    int outLen2 = 0;
    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plaintext.data()) + outLen1, &outLen2) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(outLen1 + outLen2);
    return plaintext;
}

QByteArray BrowserSessionReader::readSafeStorageKey(const QString& appAttr, const QString& folderName) {
    if (const QByteArray viaDbus = secretServiceLookup(appAttr); !viaDbus.isEmpty()) return viaDbus;
    {
        QProcess proc;
        proc.start("secret-tool", {"lookup", "application", appAttr});
        if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
            QByteArray out = proc.readAllStandardOutput().trimmed();
            if (!out.isEmpty()) return out;
        }
    }

    if (const QByteArray viaDbus = kwalletLookup(folderName + " Keys", folderName + " Safe Storage"); !viaDbus.isEmpty())
        return viaDbus;
    {
        QProcess proc;
        proc.start("kwallet-query", {"-f", folderName + " Keys", "-r", folderName + " Safe Storage", "kdewallet"});
        if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
            QByteArray out = proc.readAllStandardOutput().trimmed();
            if (!out.isEmpty()) return out;
        }
    }

    return "peanuts";
}

QString BrowserSessionReader::extractImeiFromBytes(const QByteArray& data) {
    static const QRegularExpression imeiRegex(
        QStringLiteral("([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}-[0-9a-f]{32})"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = imeiRegex.match(QString::fromLatin1(data));
    if (match.hasMatch()) {
        return match.captured(1);
    }
    return {};
}

QString BrowserSessionReader::findImeiInDir(const QString& storageDir) {
    if (storageDir.isEmpty() || !QDir(storageDir).exists()) return {};

    QDir dir(storageDir);
    const QStringList files = dir.entryList({"*.log", "*.ldb", "*.ldb*", "CURRENT", "LOG*"}, QDir::Files);
    for (const QString& f : files) {
        QFile file(dir.filePath(f));
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray content = file.read(2 * 1024 * 1024);
            const QString imei = extractImeiFromBytes(content);
            if (!imei.isEmpty()) return imei;
        }
    }
    return {};
}

BrowserSessionResult BrowserSessionReader::readSession() {
    BrowserSessionResult result;
    const QString home = QDir::homePath();
    const QString configRoot = qEnvironmentVariable("XDG_CONFIG_HOME", home + "/.config");

    bool sawAnyBrowser = false;
    bool sawAnyZalo = false;

    for (const auto& b : kBrowsers) {
        const QString browserRoot = configRoot + "/" + b.profileRoot;
        if (!QDir(browserRoot).exists()) continue;

        sawAnyBrowser = true;
        QDir rootDir(browserRoot);
        QStringList profiles = {"Default"};
        const QStringList extraDirs = rootDir.entryList({"Profile *"}, QDir::Dirs | QDir::NoDotAndDotDot);
        profiles.append(extraDirs);

        for (const QString& prof : profiles) {
            const QString profDir = browserRoot + "/" + prof;
            QString cookiePath = profDir + "/Cookies";
            if (!QFile::exists(cookiePath)) {
                cookiePath = profDir + "/Network/Cookies";
            }
            if (!QFile::exists(cookiePath)) continue;

            QTemporaryDir tmpDir;
            if (!tmpDir.isValid()) continue;

            const QString tmpCookie = tmpDir.path() + "/Cookies";
            if (!QFile::copy(cookiePath, tmpCookie)) continue;
            for (const char* suffix : {"-wal", "-shm"}) {
                const QString aux = cookiePath + suffix;
                if (QFile::exists(aux)) QFile::copy(aux, tmpCookie + suffix);
            }

            const QString connName = QStringLiteral("browser_session_probe_%1").arg(QRandomGenerator::global()->generate());

            struct RawCookie {
                QString host;
                QString name;
                QString value;
                QByteArray encrypted;
                QString path;
                qint64 expiresUtc = 0;
            };
            QVector<RawCookie> rawCookies;
            int dbVersion = 0;

            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
                db.setDatabaseName(tmpCookie);
                if (db.open()) {
                    {
                        QSqlQuery verQuery(db);
                        if (verQuery.exec("SELECT value FROM meta WHERE key='version'") && verQuery.next()) {
                            dbVersion = verQuery.value(0).toInt();
                        }
                    }
                    {
                        QSqlQuery query(db);
                        if (query.exec("SELECT host_key, name, value, encrypted_value, path, expires_utc FROM cookies WHERE host_key LIKE '%zalo.me'")) {
                            while (query.next()) {
                                rawCookies.push_back({
                                    query.value(0).toString(),
                                    query.value(1).toString(),
                                    query.value(2).toString(),
                                    query.value(3).toByteArray(),
                                    query.value(4).toString(),
                                    query.value(5).toLongLong()
                                });
                            }
                        }
                    }
                    db.close();
                }
            }
            QSqlDatabase::removeDatabase(connName);

            if (rawCookies.isEmpty()) continue;
            sawAnyZalo = true;

            const QByteArray secret = readSafeStorageKey(b.appAttr, b.kwalletFolder);
            const QByteArray keyringKey = deriveAesKey(secret);
            const QByteArray peanutsKey = deriveAesKey("peanuts");
            if (keyringKey.isEmpty() && peanutsKey.isEmpty()) continue;

            QJsonArray cookieArray;
            bool decryptFailed = false;

            for (const auto& rc : rawCookies) {
                QString plainVal = rc.value;
                if (plainVal.isEmpty() && !rc.encrypted.isEmpty()) {
                    if (rc.encrypted.startsWith("v10") || rc.encrypted.startsWith("v11")) {
                        const QByteArray cipher = rc.encrypted.mid(3);
                        const bool preferPeanuts = rc.encrypted.startsWith("v10");
                        const QByteArray& firstKey = preferPeanuts ? peanutsKey : keyringKey;
                        const QByteArray& secondKey = preferPeanuts ? keyringKey : peanutsKey;
                        QByteArray rawDecrypted;
                        bool decryptedOk = false;
                        if (!firstKey.isEmpty()) {
                            rawDecrypted = decryptAesCbc(firstKey, cipher);
                            if (!rawDecrypted.isEmpty() || cipher.size() == 16) decryptedOk = true;
                        }
                        if (!decryptedOk && !secondKey.isEmpty() && secondKey != firstKey) {
                            rawDecrypted = decryptAesCbc(secondKey, cipher);
                            if (!rawDecrypted.isEmpty() || cipher.size() == 16) decryptedOk = true;
                        }
                        if (!decryptedOk) {
                            decryptFailed = true;
                            break;
                        }
                        if (dbVersion >= 24 && rawDecrypted.size() >= 32) {
                            rawDecrypted = rawDecrypted.mid(32);
                        }
                        plainVal = QString::fromUtf8(rawDecrypted);
                    }
                }

                QJsonObject cObj;
                cObj["name"] = rc.name;
                cObj["value"] = plainVal;
                cObj["domain"] = rc.host;
                cObj["path"] = rc.path.isEmpty() ? "/" : rc.path;
                if (rc.expiresUtc > 0) {
                    cObj["expires"] = static_cast<qint64>(rc.expiresUtc / 1000000.0 - 11644473600ULL);
                }
                cookieArray.append(cObj);
            }

            if (decryptFailed || cookieArray.isEmpty()) continue;

            QString imei;
            const QStringList storageCandidates = {
                profDir + "/Local Storage/leveldb",
                profDir + "/Storage/ext/https_chat.zalo.me_0/def/LocalStorage",
                profDir + "/Storage/ext/https_chat.zalo.me_0"
            };
            for (const QString& sPath : storageCandidates) {
                imei = findImeiInDir(sPath);
                if (!imei.isEmpty()) break;
            }

            if (imei.isEmpty()) {
                result.reason = "zalo_not_signed_in";
                continue;
            }

            result.success = true;
            result.browser = b.displayName;
            result.cookies = cookieArray;
            result.imei = imei;
            result.userAgent = kDefaultUserAgent;
            return result;
        }
    }

    if (!sawAnyBrowser) {
        result.reason = "no_supported_browser";
    } else if (!sawAnyZalo) {
        result.reason = "zalo_not_visited";
    } else if (result.reason.isEmpty()) {
        result.reason = "no_browser_session";
    }
    return result;
}

}
