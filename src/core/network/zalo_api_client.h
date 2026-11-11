#pragma once

#include <QtQml/qqmlregistration.h>

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

#include "network/zalo_session.h"

namespace Zalos::Core::Network {

class ZaloApiClient : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(QString networkStatus READ networkStatus NOTIFY networkStatusChanged FINAL)
    Q_PROPERTY(bool isOnline READ isOnline NOTIFY networkStatusChanged FINAL)

public:
    explicit ZaloApiClient(QObject* parent = nullptr);

    QString networkStatus() const { return m_networkStatus; }
    bool isOnline() const { return m_networkStatus == "CONNECTED"; }

    ZaloSession* session() const { return m_session; }
    SessionCookieJar* cookieJar() const { return m_jar; }

    Q_INVOKABLE void pingConnectivity();

    Q_INVOKABLE void generateQrToken();
    Q_INVOKABLE void cancelQrLogin();

    Q_INVOKABLE void syncFromBrowserSession();

    bool restoreSavedSession(const QString& language);
    void startSessionLogin(const QString& imei, const QString& userAgent, const QString& language);
    void forgetSession();
    static QString credentialsPath();

    void setLanguage(const QString& lang) { m_language = lang; }

signals:
    void networkStatusChanged(const QString& status);
    void qrTokenGenerated(const QString& qrUrl, const QString& token);
    void qrScanned(const QString& displayName, const QString& avatarUrl);
    void qrConfirmed();
    void qrDeclined();
    void qrExpired();
    void qrFailed(const QString& reason);
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

}
