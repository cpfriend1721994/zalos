#pragma once

#include <QtQml/qqmlregistration.h>

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

#include "network/zalo_session.h"

namespace Zalos::Core::Network {

// Authentication front-end: QR device linking (id.zalo.me) and browser-session import.
// Once cookies are available the authenticated work is delegated to ZaloSession.
class ZaloApiClient : public QObject {
    Q_OBJECT
    QML_ANONYMOUS // typed for the QML compiler (reached through ZaloClient properties)

    Q_PROPERTY(QString networkStatus READ networkStatus NOTIFY networkStatusChanged FINAL)
    Q_PROPERTY(bool isOnline READ isOnline NOTIFY networkStatusChanged FINAL)

public:
    explicit ZaloApiClient(QObject* parent = nullptr);

    QString networkStatus() const { return m_networkStatus; }
    bool isOnline() const { return m_networkStatus == "CONNECTED"; }

    ZaloSession* session() const { return m_session; }
    SessionCookieJar* cookieJar() const { return m_jar; }

    Q_INVOKABLE void pingConnectivity();

    // QR login (zca-js apis/loginQR.ts)
    Q_INVOKABLE void generateQrToken();
    Q_INVOKABLE void cancelQrLogin();

    // Imports cookies/imei/user agent of a desktop browser already signed in to chat.zalo.me.
    Q_INVOKABLE void syncFromBrowserSession();

    // Restores credentials saved by a previous run. Returns false if none exist.
    bool restoreSavedSession(const QString& language);
    void startSessionLogin(const QString& imei, const QString& userAgent, const QString& language);
    void forgetSession();
    static QString credentialsPath();
    static QString browserExportScriptPath();

    void setLanguage(const QString& lang) { m_language = lang; }

signals:
    void networkStatusChanged(const QString& status);
    void qrTokenGenerated(const QString& qrUrl, const QString& token);
    void qrScanned(const QString& displayName, const QString& avatarUrl);
    void qrConfirmed();
    void qrDeclined();
    void qrExpired();
    void qrFailed(const QString& reason);
    // Emitted when cookies were obtained but before the session handshake.
    void credentialsObtained();
    void credentialsUnavailable(const QString& reason);

private:
    void postIdForm(const QString& url, const QList<QPair<QString, QString>>& form, const QString& tag,
                    const QString& referer);
    void onReplyFinished(QNetworkReply* reply);
    void pollWaitingScan();
    void pollWaitingConfirm();
    void checkSession();

    QNetworkAccessManager* m_netManager{nullptr};
    SessionCookieJar* m_jar{nullptr};
    ZaloSession* m_session{nullptr};
    QString m_networkStatus{"CONNECTED"};
    QString m_language{"vi"};
    QString m_qrVersion;
    QString m_qrCode;
    QString m_qrImageUrl;
    int m_qrGeneration{0};
    bool m_isPollingScan{false};
    bool m_isPollingConfirm{false};
};

} // namespace Zalos::Core::Network
