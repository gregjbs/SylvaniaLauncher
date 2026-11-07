#include "InstallProgress.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QStringList>
#include <QtConcurrent>

namespace InstallProgress {

static QString nomEtape(int etape) {
    switch (etape) {
    case Telechargement: return QCoreApplication::translate("InstallProgress", "Téléchargement");
    case Verification:   return QCoreApplication::translate("InstallProgress", "Vérification de l'intégrité");
    case Decompression:  return QCoreApplication::translate("InstallProgress", "Décompression");
    default:             return QCoreApplication::translate("InstallProgress", "Installation");
    }
}

QString titre(int etape) {
    return QCoreApplication::translate("InstallProgress", "Étape %1/%2 · %3")
        .arg(etape + 1).arg(kNombreEtapes).arg(nomEtape(etape));
}

QString liste(int etapeCourante) {
    QStringList morceaux;
    for (int i = 0; i < kNombreEtapes; ++i) {
        QString couleur, marque;
        if (i < etapeCourante)       { couleur = "#5fd94a"; marque = "✓"; }
        else if (i == etapeCourante) { couleur = "#d4af37"; marque = "▶"; }
        else                         { couleur = "#777777"; marque = "○"; }
        morceaux << QStringLiteral("<span style='color:%1'>%2 %3</span>")
                        .arg(couleur, marque, nomEtape(i).toHtmlEscaped());
    }
    return morceaux.join(QStringLiteral("&nbsp;&nbsp;&nbsp;"));
}

Empreinte calculerSha256(const QString& chemin) {
    Empreinte e;
    e.total = QFileInfo(chemin).size();
    auto lus = e.lus;
    e.resultat = QtConcurrent::run([chemin, lus]() {
        QFile f(chemin);
        if (!f.open(QFile::ReadOnly)) return QByteArray();
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QByteArray bloc;
        while (!(bloc = f.read(8 * 1024 * 1024)).isEmpty()) {
            hash.addData(bloc);
            lus->fetch_add(bloc.size());
        }
        return hash.result();
    });
    return e;
}

}  // namespace InstallProgress

SuiviDecompression::SuiviDecompression(const QString& dossier, const QString& aIgnorer,
                                       qint64 totalAttendu, QObject* parent)
    : QObject(parent), m_dossier(dossier), m_aIgnorer(aIgnorer), m_total(totalAttendu) {
    m_minuteur.setInterval(1500);
    connect(&m_minuteur, &QTimer::timeout, this, &SuiviDecompression::mesurer);
}

void SuiviDecompression::demarrer() {
    // Photographie de ce qui existe déjà : seul le nouveau sera mesuré.
    const QStringList avant = QDir(m_dossier).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    m_existants = QSet<QString>(avant.begin(), avant.end());
    m_minuteur.start();
}

void SuiviDecompression::arreter() { m_minuteur.stop(); }

void SuiviDecompression::mesurer() {
    if (m_enCours->exchange(true)) return;  // la mesure précédente n'est pas finie
    const QString dossier = m_dossier, aIgnorer = m_aIgnorer;
    const QSet<QString> existants = m_existants;
    auto enCours = m_enCours;
    QPointer<SuiviDecompression> moi(this);
    const qint64 total = m_total;
    QFuture<qint64> f = QtConcurrent::run([dossier, aIgnorer, existants]() {
        qint64 octets = 0;
        const QFileInfoList entrees = QDir(dossier).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QFileInfo& e : entrees) {
            if (existants.contains(e.fileName()) || e.fileName() == aIgnorer) continue;
            if (e.isDir()) {
                QDirIterator it(e.absoluteFilePath(), QDir::Files | QDir::Hidden,
                                QDirIterator::Subdirectories);
                while (it.hasNext()) { it.next(); octets += it.fileInfo().size(); }
            } else {
                octets += e.size();
            }
        }
        return octets;
    });
    f.then(this, [moi, enCours, total](qint64 octets) {
        enCours->store(false);
        if (moi) emit moi->avancement(octets, total);
    });
}
