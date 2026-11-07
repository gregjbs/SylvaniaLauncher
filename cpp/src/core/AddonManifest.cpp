#include "AddonManifest.h"
#include "ErraiNetwork.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>

AddonManifest::AddonManifest(const QString& manifestUrl,
                             const QString& embeddedResource,
                             QObject* parent)
    : QObject(parent)
    , m_manifestUrl(manifestUrl)
    , m_embeddedResource(embeddedResource)
    , m_network(std::make_unique<QNetworkAccessManager>(this))
{
}

AddonManifest::~AddonManifest() {
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
    }
}

std::vector<ManifestAddon> AddonManifest::parse(const QByteArray& json, bool* ok, const QString& key) {
    if (ok) *ok = false;
    std::vector<ManifestAddon> result;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return result;
    }

    const QJsonArray array = doc.object().value(key).toArray();
    for (const QJsonValue& v : array) {
        const QJsonObject obj = v.toObject();
        ManifestAddon a;
        a.id = obj.value("id").toString();
        a.name = obj.value("name").toString();
        a.version = obj.value("version").toString();
        a.description = obj.value("description").toString();
        a.url = obj.value("url").toString();
        a.sha256 = obj.value("sha256").toString().toLower();
        const QJsonArray folders = obj.value("folders").toArray();
        for (const QJsonValue& f : folders) {
            const QString folder = f.toString();
            if (!folder.isEmpty()) a.folders << folder;
        }
        // An addon is only usable if it has an id, a download URL and at least
        // one expected folder (needed to verify install and to uninstall).
        if (!a.id.isEmpty() && !a.url.isEmpty() && !a.folders.isEmpty()) {
            result.push_back(a);
        }
    }

    if (ok) *ok = !result.empty();
    return result;
}

std::vector<ManifestAddon> AddonManifest::embedded() const {
    QFile file(m_embeddedResource);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll();
        file.close();
        return parse(data);
    }
    return {};
}

void AddonManifest::fetch() {
    // Guard against overlapping fetches.
    if (m_reply) return;

    QNetworkRequest request{QUrl(m_manifestUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);

    m_reply = m_network->get(request);
    ErraiNetwork::watch(m_reply);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;

        bool fromRemote = false;
        if (reply && reply->error() == QNetworkReply::NoError) {
            bool ok = false;
            const QByteArray data = reply->readAll();
            std::vector<ManifestAddon> remote = parse(data, &ok);
            if (ok) {
                m_addons = std::move(remote);
                m_catalogue = parse(data, nullptr, QStringLiteral("catalogue"));
                fromRemote = true;
            }
        }
        if (reply) reply->deleteLater();

        if (!fromRemote) {
            m_addons = embedded();
            QFile file(m_embeddedResource);
            if (file.open(QIODevice::ReadOnly))
                m_catalogue = parse(file.readAll(), nullptr, QStringLiteral("catalogue"));
        }
        emit loaded(fromRemote);
    });
}
