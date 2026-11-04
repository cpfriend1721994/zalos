#include "zalo_api_client.h"
#include "zalo_crypto.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace Zalos::Core::Network {

namespace {

constexpr auto kTagAttr = QNetworkRequest::Attribute(QNetworkRequest::User + 1);
constexpr auto kGenAttr = QNetworkRequest::Attribute(QNetworkRequest::User + 2);

const QString kRefererPc = QStringLiteral("https://id.zalo.me/account?continue=https%3A%2F%2Fzalo.me%2Fpc");
const QString kRefererChat = QStringLiteral("https://id.zalo.me/account?continue=https%3A%2F%2Fchat.zalo.me%2F");

void applyIdHeaders(QNetworkRequest& req, const QString& referer, bool navigate = false) {
    req.setHeader(QNetworkRequest::UserAgentHeader, ZaloSession::kDefaultUserAgent);
    req.setRawHeader("Accept", navigate ? "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8" : "*/*");
    req.setRawHeader("Accept-Language", "vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7");
    req.setRawHeader("sec-ch-ua", "\"Chromium\";v=\"130\", \"Google Chrome\";v=\"130\", \"Not?A_Brand\";v=\"99\"");
    req.setRawHeader("sec-ch-ua-mobile", "?0");
    req.setRawHeader("sec-ch-ua-platform", "\"Windows\"");
    req.setRawHeader("sec-fetch-dest", navigate ? "document" : "empty");
    req.setRawHeader("sec-fetch-mode", navigate ? "navigate" : "cors");
    req.setRawHeader("sec-fetch-site", "same-origin");
    req.setRawHeader("Referer", referer.toUtf8());
}

}

ZaloApiClient::ZaloApiClient(QObject* parent)
    : QObject(parent) {
    m_netManager = new QNetworkAccessManager(this);
    m_jar = new SessionCookieJar(this);
    m_netManager->setCookieJar(m_jar);
    m_jar->setParent(this);
    m_session = new ZaloSession(m_netManager, m_jar, this);
    connect(m_netManager, &QNetworkAccessManager::finished, this, &ZaloApiClient::onReplyFinished);
}

QString ZaloApiClient::credentialsPath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/session.json";
}

QString ZaloApiClient::browserExportScriptPath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/../share/zalos/export_browser_cookies.py",
        appDir + "/../scripts/export_browser_cookies.py",
#ifdef ZALOS_SOURCE_DIR
        QStringLiteral(ZALOS_SOURCE_DIR) + "/scripts/export_browser_cookies.py",
#endif
    };
    for (const QString& path : candidates) {
        if (QFile::exists(path)) return QDir::cleanPath(path);
    }
    return {};
}

void ZaloApiClient::pingConnectivity() {
    m_networkStatus = "CHECKING";
    emit networkStatusChanged(m_networkStatus);

    QNetworkRequest request(QUrl("https://chat.zalo.me"));
    request.setHeader(QNetworkRequest::UserAgentHeader, ZaloSession::kDefaultUserAgent);
    request.setAttribute(kTagAttr, "ping");
    m_netManager->head(request);
}

void ZaloApiClient::generateQrToken() {
    cancelQrLogin();
    ++m_qrGeneration;

    QNetworkRequest request(QUrl("https://id.zalo.me/account?continue=https%3A%2F%2Fchat.zalo.me%2F"));
    applyIdHeaders(request, "https://chat.zalo.me/", true);
    request.setRawHeader("sec-fetch-site", "same-site");
    request.setAttribute(kTagAttr, "qr_step1_load_page");
    request.setAttribute(kGenAttr, m_qrGeneration);
    m_netManager->get(request);
}

void ZaloApiClient::cancelQrLogin() {
    m_isPollingScan = false;
    m_isPollingConfirm = false;
}

void ZaloApiClient::postIdForm(const QString& url, const QList<QPair<QString, QString>>& form, const QString& tag,
                               const QString& referer) {
    QByteArray body;
    for (const auto& item : form) {
        if (!body.isEmpty()) body.append('&');
        body.append(QUrl::toPercentEncoding(item.first) + "=" + QUrl::toPercentEncoding(item.second));
    }
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    applyIdHeaders(req, referer);
    req.setAttribute(kTagAttr, tag);
    req.setAttribute(kGenAttr, m_qrGeneration);
    m_netManager->post(req, body);
}

void ZaloApiClient::pollWaitingScan() {
    if (!m_isPollingScan || m_qrCode.isEmpty()) return;
    postIdForm("https://id.zalo.me/account/authen/qr/waiting-scan",
               {{"code", m_qrCode}, {"continue", "https://chat.zalo.me/"}, {"v", m_qrVersion}},
               "qr_step5_waiting_scan", kRefererChat);
}

void ZaloApiClient::pollWaitingConfirm() {
    if (!m_isPollingConfirm || m_qrCode.isEmpty()) return;
    postIdForm("https://id.zalo.me/account/authen/qr/waiting-confirm",
               {{"code", m_qrCode}, {"gToken", ""}, {"gAction", "CONFIRM_QR"},
                {"continue", "https://chat.zalo.me/"}, {"v", m_qrVersion}},
               "qr_step6_waiting_confirm", kRefererChat);
}

void ZaloApiClient::checkSession() {
    QNetworkRequest req(QUrl("https://id.zalo.me/account/checksession?continue=https%3A%2F%2Fchat.zalo.me%2Findex.html"));
    applyIdHeaders(req, kRefererChat, true);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(kTagAttr, "qr_step7_check_session");
    req.setAttribute(kGenAttr, m_qrGeneration);
    m_netManager->get(req);
}

void ZaloApiClient::startSessionLogin(const QString& imei, const QString& userAgent, const QString& language) {
    m_language = language;
    m_session->login(imei, userAgent, language);
}

bool ZaloApiClient::restoreSavedSession(const QString& language) {
    QString imei;
    QString userAgent;
    if (!ZaloSession::loadCredentials(credentialsPath(), m_jar, &imei, &userAgent)) return false;
    startSessionLogin(imei, userAgent, language);
    return true;
}

void ZaloApiClient::forgetSession() {
    cancelQrLogin();
    m_session->reset();
    m_jar->replaceCookies({});
    QFile::remove(credentialsPath());
}

void ZaloApiClient::syncFromBrowserSession() {
    cancelQrLogin();

    const QString scriptPath = browserExportScriptPath();
    if (scriptPath.isEmpty()) {
        emit credentialsUnavailable("script_missing");
        return;
    }

    auto* proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int exitCode, QProcess::ExitStatus status) {
        proc->deleteLater();
        const QJsonObject root = QJsonDocument::fromJson(proc->readAllStandardOutput()).object();
        if (status != QProcess::NormalExit || exitCode != 0 || !root.value("success").toBool(false)) {
            emit credentialsUnavailable(root.value("reason").toString("no_browser_session"));
            return;
        }
        const QJsonArray cookies = root.value("cookies").toArray();
        const QString imei = root.value("imei").toString();
        if (cookies.isEmpty() || imei.isEmpty()) {
            emit credentialsUnavailable("incomplete_session");
            return;
        }
        m_jar->replaceCookies({});
        ZaloSession::importCookies(m_jar, cookies);
        emit credentialsObtained();
        startSessionLogin(imei, root.value("userAgent").toString(ZaloSession::kDefaultUserAgent), m_language);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        proc->deleteLater();
        emit credentialsUnavailable("python_unavailable");
    });
    proc->start("python3", {"-I", scriptPath});
}

void ZaloApiClient::onReplyFinished(QNetworkReply* reply) {
    const QString tag = reply->request().attribute(kTagAttr).toString();
    if (tag.isEmpty()) return;
    reply->deleteLater();

    if (tag == "ping") {
        m_networkStatus = reply->error() == QNetworkReply::NoError ? "CONNECTED" : "DISCONNECTED";
        emit networkStatusChanged(m_networkStatus);
        return;
    }

    if (reply->request().attribute(kGenAttr).toInt() != m_qrGeneration) return;

    if (tag == "qr_step7_check_session") {
        if (reply->error() == QNetworkReply::NoError || reply->error() == QNetworkReply::UnknownContentError) {
            emit credentialsObtained();
            startSessionLogin(ZaloCrypto::generateImei(ZaloSession::kDefaultUserAgent), ZaloSession::kDefaultUserAgent,
                              m_language);
        } else {
            emit qrFailed(reply->errorString());
        }
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        if (tag == "qr_step5_waiting_scan" || tag == "qr_step6_waiting_confirm") {
            cancelQrLogin();
        }
        emit qrFailed(reply->errorString());
        return;
    }

    if (!reply->isOpen()) {
        emit qrFailed("connection_closed");
        return;
    }

    if (tag == "qr_step1_load_page") {
        const QString html = QString::fromUtf8(reply->readAll());
        static const QRegularExpression re(R"(https://stc-zlogin\.zdn\.vn/main-([\d.]+)\.js)");
        const QRegularExpressionMatch match = re.match(html);
        if (!match.hasMatch()) {
            emit qrFailed("version_not_found");
            return;
        }
        m_qrVersion = match.captured(1);
        postIdForm("https://id.zalo.me/account/logininfo",
                   {{"continue", "https://zalo.me/pc"}, {"v", m_qrVersion}}, "qr_step2_login_info", kRefererPc);
        return;
    }

    if (tag == "qr_step2_login_info") {
        postIdForm("https://id.zalo.me/account/verify-client",
                   {{"type", "device"}, {"continue", "https://zalo.me/pc"}, {"v", m_qrVersion}},
                   "qr_step3_verify_client", kRefererPc);
        return;
    }

    if (tag == "qr_step3_verify_client") {
        postIdForm("https://id.zalo.me/account/authen/qr/generate",
                   {{"continue", "https://zalo.me/pc"}, {"v", m_qrVersion}}, "qr_step4_generate", kRefererPc);
        return;
    }

    const QJsonObject rootObj = QJsonDocument::fromJson(reply->readAll()).object();
    const int errorCode = rootObj.value("error_code").toInt(-1);

    if (tag == "qr_step4_generate") {
        if (errorCode != 0) {
            emit qrFailed(rootObj.value("error_message").toString());
            return;
        }
        const QJsonObject dataObj = rootObj.value("data").toObject();
        m_qrCode = dataObj.value("code").toString();
        m_qrImageUrl = dataObj.value("image").toString();
        if (!m_qrImageUrl.startsWith("data:")) {
            m_qrImageUrl = "data:image/png;base64," + m_qrImageUrl;
        }
        emit qrTokenGenerated(m_qrImageUrl, dataObj.value("token").toString());

        m_isPollingScan = true;
        pollWaitingScan();
        return;
    }

    if (tag == "qr_step5_waiting_scan") {
        if (!m_isPollingScan) return;
        if (errorCode == 8) {
            pollWaitingScan();
            return;
        }
        m_isPollingScan = false;
        if (errorCode == 0) {
            const QJsonObject dataObj = rootObj.value("data").toObject();
            emit qrScanned(dataObj.value("display_name").toString(), dataObj.value("avatar").toString());
            m_isPollingConfirm = true;
            pollWaitingConfirm();
            return;
        }
        emit qrExpired();
        return;
    }

    if (tag == "qr_step6_waiting_confirm") {
        if (!m_isPollingConfirm) return;
        if (errorCode == 8) {
            pollWaitingConfirm();
            return;
        }
        m_isPollingConfirm = false;
        if (errorCode == 0) {
            emit qrConfirmed();
            checkSession();
        } else if (errorCode == -13) {
            emit qrDeclined();
        } else {
            emit qrExpired();
        }
        return;
    }
}

}
