#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// Fork Errai : téléchargements servis par errai.minotux.com.
//
// 1. Certificat : le serveur Errai présente un certificat auto-signé. Si l'option
//    « ignorer le certificat d'Errai » est active (Réglages, activée par défaut),
//    les erreurs TLS sont ignorées pour cet hôte UNIQUEMENT ; tout autre hôte
//    (Sylvania pour les add-ons…) reste vérifié normalement.
//
// 2. Manifeste : client.json, à côté des archives, donne pour chacune son adresse,
//    sa taille et son SHA-256. Avec la taille, le client se télécharge par morceaux
//    (reprise après coupure, espace disque vérifié) ; avec le SHA-256, l'archive est
//    vérifiée avant extraction. Sans manifeste, on retombe sur l'adresse par défaut
//    de Constants.h, sans taille ni empreinte.
namespace ErraiNetwork {

void setIgnoreErraiSslErrors(bool ignore);
bool ignoreErraiSslErrors();

// À appeler sur chaque QNetworkReply : branche le traitement des erreurs TLS.
void watch(QNetworkReply* reply);

struct Archive {
    QString url;
    qint64 size = 0;      // 0 = inconnue
    QString sha256;       // vide = inconnue (minuscules)
};

}  // namespace ErraiNetwork

class ErraiManifest : public QObject {
    Q_OBJECT
public:
    static ErraiManifest& instance();

    // Télécharge client.json si ce n'est pas déjà fait. Bloque au plus timeoutMs
    // (boucle d'événements locale) : appelé juste avant une installation.
    bool ensureLoaded(int timeoutMs = 8000);

    // Archive « client », « hd » ou « enus » ; repli sur fallbackUrl sans taille ni
    // empreinte si le manifeste est absent ou incomplet.
    ErraiNetwork::Archive archive(const QString& key, const QString& fallbackUrl) const;

private:
    ErraiManifest() = default;
    QJsonObject m_data;
    bool m_loaded = false;
    QNetworkAccessManager* m_network = nullptr;
};
