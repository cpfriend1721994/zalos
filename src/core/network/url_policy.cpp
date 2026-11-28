#include "url_policy.h"

namespace Zalos::Core::Network {

namespace {

bool startsLikePath(const QString& t) {
    return t.startsWith(QLatin1Char('/')) || t.startsWith(QLatin1Char('\\')) || t.startsWith(QLatin1Char('~'))
        || t.startsWith(QLatin1Char('.'));
}

bool isInternalScheme(const QString& scheme) {
    return scheme == QLatin1String("file") || scheme == QLatin1String("qrc") || scheme == QLatin1String("image")
        || scheme.size() == 1;
}

}

bool UrlPolicy::isWebUrl(const QUrl& url) {
    if (!url.isValid() || url.host().isEmpty()) return false;
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

bool UrlPolicy::isWebUrl(const QString& url) {
    return isWebUrl(QUrl(url.trimmed(), QUrl::StrictMode));
}

QString UrlPolicy::sanitizeRemoteReference(const QString& value) {
    const QString t = value.trimmed();
    if (t.isEmpty()) return {};
    if (startsLikePath(t)) return {};
    const QUrl u(t);
    const QString scheme = u.scheme().toLower();
    if (scheme.isEmpty()) {
        const QUrl guessed = QUrl::fromUserInput(t);
        if (isWebUrl(guessed) && guessed.host().contains(QLatin1Char('.'))) return guessed.toString();
        return {};
    }
    if (!u.isValid() || u.isLocalFile() || isInternalScheme(scheme)) return {};
    return t;
}

bool UrlPolicy::isLocalReference(const QString& value) {
    const QString t = value.trimmed();
    return !t.isEmpty() && sanitizeRemoteReference(t).isEmpty();
}

void UrlPolicy::sanitizeRemoteReferences(QString* mediaUrl, QString* href, QVariantMap* extra) {
    if (mediaUrl) *mediaUrl = sanitizeRemoteReference(*mediaUrl);
    if (href) *href = sanitizeRemoteReference(*href);
    if (!extra) return;
    const QString animatedKey = QStringLiteral("animatedUrl");
    if (!extra->contains(animatedKey)) return;
    const QString animated = sanitizeRemoteReference(extra->value(animatedKey).toString());
    if (animated.isEmpty()) extra->remove(animatedKey);
    else extra->insert(animatedKey, animated);
}

QUrl UrlPolicy::upgradeToHttps(const QUrl& url) {
    if (url.scheme().compare(QLatin1String("http"), Qt::CaseInsensitive) != 0) return url;
    QUrl out(url);
    out.setScheme(QStringLiteral("https"));
    if (out.port() == 80) out.setPort(-1);
    return out;
}

}
