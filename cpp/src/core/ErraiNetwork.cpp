#include "ErraiNetwork.h"
#include "Constants.h"

#include <QDebug>
#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QTimer>
#include <QUrl>
#include <atomic>

namespace {
std::atomic<bool> g_ignoreErraiSsl{true};
}

namespace ErraiNetwork {

void setIgnoreErraiSslErrors(bool ignore) { g_ignoreErraiSsl = ignore; }
bool ignoreErraiSslErrors() { return g_ignoreErraiSsl; }

void watch(QNetworkReply* reply) {
    if (!reply) return;
    QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError>& errors) {
        const QString host = reply->url().host();
        if (g_ignoreErraiSsl && host.compare(QString::fromUtf8(SylvaniaConstants::kErraiHost),
                                             Qt::CaseInsensitive) == 0) {
            reply->ignoreSslErrors(errors);
            return;
        }
        for (const QSslError& e : errors)
            qWarning() << "Erreur TLS" << host << ":" << e.errorString();
    });
}

}  // namespace ErraiNetwork

ErraiManifest& ErraiManifest::instance() {
    static ErraiManifest m;
    return m;
}

bool ErraiManifest::ensureLoaded(int timeoutMs) {
    if (m_loaded) return true;
    if (!m_network) m_network = new QNetworkAccessManager(this);

    QNetworkRequest request{QUrl(QString::fromUtf8(SylvaniaConstants::kErraiManifestUrl))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(timeoutMs);
    QNetworkReply* reply = m_network->get(request);
    ErraiNetwork::watch(reply);

    QEventLoop loop;
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject()) {
            m_data = doc.object();
            m_loaded = true;
        }
    } else {
        qWarning() << "Manifeste Errai indisponible :" << reply->errorString();
        reply->abort();
    }
    reply->deleteLater();
    return m_loaded;
}

ErraiNetwork::Archive ErraiManifest::archive(const QString& key, const QString& fallbackUrl) const {
    ErraiNetwork::Archive a;
    a.url = fallbackUrl;
    const QJsonObject o = m_data.value(key).toObject();
    const QString url = o.value("url").toString();
    if (!url.isEmpty()) {
        // Adresse relative au manifeste (ex. "WoW-3.3.5.zip") ou absolue.
        a.url = QUrl(QString::fromUtf8(SylvaniaConstants::kErraiManifestUrl)).resolved(QUrl(url)).toString();
        a.size = static_cast<qint64>(o.value("taille").toDouble(0));
        a.sha256 = o.value("sha256").toString().toLower();
        a.extracted = static_cast<qint64>(o.value("taille_decompressee").toDouble(0));
    }
    return a;
}
