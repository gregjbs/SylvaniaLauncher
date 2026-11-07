#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <memory>
#include <vector>

/**
 * @brief One entry of the recommended-addons manifest.
 */
struct ManifestAddon {
    QString id;
    QString name;
    QString version;
    QString description;
    QString url;            // download URL (may be a ?file=... PHP endpoint)
    QStringList folders;    // folders expected under Interface/AddOns
    QString sha256;         // fork Errai : empreinte de l'archive (vide = non vérifiée)
};

/**
 * @brief Loads the "recommended addons" manifest of ONE game edition.
 *
 * Fetches the edition's addons.json (GameEdition::addonManifestUrl) and, if
 * that fails or returns nothing usable, falls back to the edition's embedded
 * copy (e.g. :/addons/manifest) so the list always renders — even offline.
 * WotLK and Legion each have their own manifest; the two lists are never
 * mixed. Adding an addon later is a server-side manifest edit; no client
 * rebuild needed.
 */
class AddonManifest : public QObject {
    Q_OBJECT

public:
    // manifestUrl + embeddedResource come from the active GameEdition.
    explicit AddonManifest(const QString& manifestUrl,
                           const QString& embeddedResource,
                           QObject* parent = nullptr);
    ~AddonManifest() override;

    // Asynchronous. Emits loaded() exactly once; never blocks the UI thread.
    void fetch();

    const std::vector<ManifestAddon>& addons() const { return m_addons; }
    // Fork Errai : liste « catalogue » du même manifeste (vide si absente).
    const std::vector<ManifestAddon>& catalogue() const { return m_catalogue; }

    // Parse a manifest JSON document. ok (if provided) reports whether the
    // document was well-formed and contained at least one valid addon.
    static std::vector<ManifestAddon> parse(const QByteArray& json, bool* ok = nullptr,
                                            const QString& key = QStringLiteral("addons"));
    // The manifest bundled in resources, used as offline fallback.
    std::vector<ManifestAddon> embedded() const;

signals:
    // fromRemote = false means the embedded fallback was used.
    void loaded(bool fromRemote);

private:
    QString m_manifestUrl;
    QString m_embeddedResource;
    std::unique_ptr<QNetworkAccessManager> m_network;
    QNetworkReply* m_reply = nullptr;
    std::vector<ManifestAddon> m_addons;
    std::vector<ManifestAddon> m_catalogue;
};
