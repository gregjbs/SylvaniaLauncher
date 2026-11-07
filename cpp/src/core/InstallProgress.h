#pragma once

#include <QFuture>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <atomic>
#include <memory>

// Fork Errai : montrer au joueur ce qui se passe pendant l'installation d'une grosse
// archive (client 20 Go, patch HD 12 Go, pack anglais 3 Go), en quatre étapes :
// téléchargement, vérification SHA-256, décompression, installation.
namespace InstallProgress {

enum Etape { Telechargement = 0, Verification = 1, Decompression = 2, Installation = 3 };
constexpr int kNombreEtapes = 4;

// « Étape 2/4 · Vérification de l'intégrité »
QString titre(int etape);

// Liste des quatre étapes en texte riche : ✓ faites, ▶ en cours, ○ à venir.
QString liste(int etapeCourante);

// SHA-256 calculé hors du fil de l'interface, avec l'avancement lisible à tout moment.
struct Empreinte {
    QFuture<QByteArray> resultat;
    std::shared_ptr<std::atomic<qint64>> lus = std::make_shared<std::atomic<qint64>>(0);
    qint64 total = 0;
};
Empreinte calculerSha256(const QString& chemin);

}  // namespace InstallProgress

// Mesure l'avancement d'une décompression en additionnant la taille des seuls
// éléments APPARUS dans le dossier depuis le début (jamais tout le dossier : le
// joueur peut installer à la racine d'un disque). Calcul en arrière-plan, une
// mesure à la fois, toutes les 1,5 s.
class SuiviDecompression : public QObject {
    Q_OBJECT
public:
    SuiviDecompression(const QString& dossier, const QString& aIgnorer, qint64 totalAttendu,
                       QObject* parent = nullptr);
    void demarrer();
    void arreter();
    qint64 totalAttendu() const { return m_total; }

signals:
    void avancement(qint64 octets, qint64 total);

private:
    void mesurer();

    QString m_dossier;
    QString m_aIgnorer;      // nom de l'archive elle-même, souvent dans le même dossier
    qint64 m_total = 0;
    QSet<QString> m_existants;
    QTimer m_minuteur;
    std::shared_ptr<std::atomic<bool>> m_enCours = std::make_shared<std::atomic<bool>>(false);
};
