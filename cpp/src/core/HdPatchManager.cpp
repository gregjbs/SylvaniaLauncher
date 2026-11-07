#include "HdPatchManager.h"
#include "Constants.h"
#include "ErraiNetwork.h"
#include "InstallProgress.h"

#include <QDir>
#include <QDirIterator>
#include <QTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QTextStream>
#include <QStringList>
#include <QFile>
#include <QSaveFile>
#include <QtConcurrent>
#include <QCryptographicHash>
#include <QFutureWatcher>

HdPatchManager::HdPatchManager(const QString& wowPath, QObject* parent)
    : QObject(parent)
    , m_wowPath(wowPath)
    , m_networkManager(std::make_unique<QNetworkAccessManager>(this))
{
    m_downloadUrl = SylvaniaConstants::kHdPatchUrl;
    m_tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/SylvaniaLauncher_HD";
}

HdPatchManager::~HdPatchManager() {
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
    }
    cleanup();
}

void HdPatchManager::startInstallation() {
    if (m_installing) return;
    m_installing = true;

    // Adresse et empreinte du patch HD publiées par le portail d'Errai.
    ErraiManifest::instance().ensureLoaded();
    const ErraiNetwork::Archive hd = ErraiManifest::instance().archive(
        QStringLiteral("hd"), QString::fromUtf8(SylvaniaConstants::kHdPatchUrl));
    m_downloadUrl = hd.url;
    m_expectedSha256 = hd.sha256;
    m_expectedExtracted = hd.extracted;
    
    QDir dir(m_tempDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            m_installing = false;
            emit finished(false, tr("Erreur de permission : Impossible de créer le dossier temporaire."));
            return;
        }
    }
    
    QFileInfo dirInfo(m_wowPath);
    if (!dirInfo.isWritable()) {
        m_installing = false;
        emit finished(false, tr("Erreur de permission : Impossible d'écrire dans le dossier WoW. Vérifiez vos droits d'administrateur."));
        return;
    }
    
    QString zipPath = m_tempDir + "/patch-hd.zip";
    m_file = std::make_unique<QFile>(zipPath);
    
    if (!m_file->open(QIODevice::WriteOnly)) {
        m_installing = false;
        emit finished(false, tr("Impossible de créer le fichier temporaire"));
        return;
    }
    
    QNetworkRequest request{QUrl(m_downloadUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);

    m_reply = m_networkManager->get(request);
    ErraiNetwork::watch(m_reply);
    
    connect(m_reply, &QNetworkReply::downloadProgress,
            this, &HdPatchManager::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished,
            this, &HdPatchManager::onDownloadFinished);
    connect(m_reply, &QNetworkReply::errorOccurred,
            this, &HdPatchManager::onDownloadError);
            
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_file && m_file->isOpen()) {
            m_file->write(m_reply->readAll());
        }
    });
    
    emit progressChanged(0, tr("%1 du patch HD…").arg(InstallProgress::titre(InstallProgress::Telechargement)));
}

void HdPatchManager::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (bytesTotal > 0) {
        int progress = static_cast<int>((bytesReceived * 100) / bytesTotal);
        emit progressChanged(progress, tr("%1 du patch HD — %2% (%3 / %4)")
            .arg(InstallProgress::titre(InstallProgress::Telechargement)).arg(progress)
            .arg(octets(bytesReceived), octets(bytesTotal)));
    }
}

void HdPatchManager::onDownloadFinished() {
    // The reply is consumed here for BOTH success and error (Qt always emits
    // finished() after errorOccurred()). Detach and schedule its deletion so a
    // failed install never leaks the QNetworkReply: previously m_reply was left
    // dangling and simply overwritten on the next attempt.
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    const bool hadError = !reply || reply->error() != QNetworkReply::NoError;
    if (reply) reply->deleteLater();

    if (m_file) {
        m_file->close();
    }

    if (hadError) {
        // Error already reported by onDownloadError; drop the partial download.
        m_file.reset();
        QFile::remove(m_tempDir + "/patch-hd.zip");
        return;
    }

    emit progressChanged(0, tr("%1 (SHA-256)…").arg(InstallProgress::titre(InstallProgress::Verification)));

    QString zipPath = m_tempDir + "/patch-hd.zip";

    // Asynchronous hash verification
    verifyHash(zipPath);
}

void HdPatchManager::verifyHash(const QString& filePath) {
    QFutureWatcher<QByteArray>* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher, filePath]() {
        QByteArray hash = watcher->result();
        watcher->deleteLater();
        
        qDebug() << "HD Patch downloaded file SHA-256:" << hash.toHex();

        if (!m_expectedSha256.isEmpty() && QString::fromLatin1(hash.toHex()) != m_expectedSha256) {
            QFile::remove(filePath);
            m_installing = false;
            emit finished(false, tr("Le patch HD téléchargé est corrompu (empreinte SHA-256 différente). "
                                    "Relancez l'installation."));
            return;
        }
        
        emit progressChanged(0, tr("%1…").arg(InstallProgress::titre(InstallProgress::Decompression)));
        
        // Slight delay to ensure file handle is released before PowerShell extracts
        QTimer::singleShot(SylvaniaConstants::kHashVerifyDelayMs, this, [this, filePath]() {
            extractPatch(filePath);
        });
    });
    
    const InstallProgress::Empreinte empreinte = InstallProgress::calculerSha256(filePath);
    QTimer* suivi = new QTimer(watcher);
    suivi->setInterval(500);
    connect(suivi, &QTimer::timeout, this, [this, empreinte]() {
        if (empreinte.total <= 0) return;
        const qint64 lus = empreinte.lus->load();
        const int pct = static_cast<int>(lus * 100 / empreinte.total);
        emit progressChanged(pct, tr("%1 (SHA-256) — %2% (%3 / %4)")
            .arg(InstallProgress::titre(InstallProgress::Verification)).arg(pct)
            .arg(octets(lus), octets(empreinte.total)));
    });
    suivi->start();
    watcher->setFuture(empreinte.resultat);
}

void HdPatchManager::extractPatch(const QString& zipPath) {
    QString extractPath = m_tempDir + "/extracted";
    QDir().mkpath(extractPath);

    QProcess* process = new QProcess(this);

    // Safe extraction: paths flow via environment variables, never interpolated
    // into the PowerShell command string -> no command-injection surface.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("SYL_SRC", QDir::toNativeSeparators(zipPath));
    env.insert("SYL_DST", QDir::toNativeSeparators(extractPath));
    process->setProcessEnvironment(env);

    // The extraction runs in a child process. Show a busy/indeterminate state
    // (progress = -1) instead of recursively scanning the growing extraction
    // tree on the UI thread, which froze the launcher on large clients.
    emit progressChanged(0, tr("%1…").arg(InstallProgress::titre(InstallProgress::Decompression)));

    // Avancement : taille de ce qui apparaît dans le dossier d'extraction (vide au départ).
    const qint64 attendu = m_expectedExtracted > 0 ? m_expectedExtracted : QFileInfo(zipPath).size();
    auto* suiviDecomp = new SuiviDecompression(extractPath, QString(), attendu, process);
    connect(suiviDecomp, &SuiviDecompression::avancement, this, [this](qint64 lus, qint64 total) {
        if (total <= 0) return;
        const int pct = static_cast<int>(qMin<qint64>(99, lus * 100 / total));
        emit progressChanged(pct, tr("%1 — %2% (%3 / %4)")
            .arg(InstallProgress::titre(InstallProgress::Decompression)).arg(pct)
            .arg(octets(lus), octets(total)));
    });
    suiviDecomp->demarrer();

    process->start("powershell", SylvaniaConstants::extractArchiveArgs());

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, extractPath, zipPath](int exitCode, QProcess::ExitStatus exitStatus) {
        process->deleteLater();

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            emit progressChanged(0, tr("%1…").arg(InstallProgress::titre(InstallProgress::Installation)));

            // Locate the client root inside the extracted tree. The archive may
            // wrap "Le Client WoW HD" in extra folders (e.g. "Patch-HD/...") or
            // ship its contents directly, so search rather than assume a path.
            QString sourceDir = findExtractedRoot(extractPath);
            if (!sourceDir.isEmpty()) {
                migrateFiles(sourceDir);
            } else {
                emit finished(false, tr("Contenu du Patch HD introuvable dans l'archive."));
            }
        } else {
            emit finished(false, tr("Échec de l'extraction: ") + process->readAllStandardError());
        }
    });
}

QString HdPatchManager::findExtractedRoot(const QString& extractPath) const {
    auto looksLikeRoot = [](const QString& dir) {
        return QDir(dir + "/Data").exists()
            || QFile::exists(dir + "/WoW.exe")
            || QFile::exists(dir + "/wow.exe");
    };

    // Breadth-first, depth-limited search. The canonical root is the folder
    // "Le Client WoW HD", but the archive may wrap it (e.g. "Patch-HD/Le
    // Client WoW HD/") or ship its contents directly. We stop as soon as we
    // find the marker, so we never descend into the multi-GB Data tree.
    const int maxDepth = 4;
    QList<QPair<QString, int>> queue;
    queue.append(qMakePair(extractPath, 0));

    while (!queue.isEmpty()) {
        const QString dir = queue.first().first;
        const int depth = queue.first().second;
        queue.removeFirst();

        if (QDir(dir + "/Le Client WoW HD").exists())
            return dir + "/Le Client WoW HD";
        if (looksLikeRoot(dir))
            return dir;

        if (depth >= maxDepth)
            continue;
        const QStringList subs = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& sub : subs)
            queue.append(qMakePair(dir + "/" + sub, depth + 1));
    }
    return QString();
}

void HdPatchManager::migrateFiles(const QString& sourcePath) {
    // Supprimer PatchMenu.exe s'il existe déjà (nettoyage)
    QString oldPatchMenu = m_wowPath + "/PatchMenu.exe";
    if (QFile::exists(oldPatchMenu)) {
        QFile::remove(oldPatchMenu);
    }

    QStringList itemsToMigrate = {
        "Data", "Interface",                                      // Folders
        "World of Warcraft.app", "d3d9.dll",
        "dxvk.conf", "WoW.exe"           // Files
    };
    
    QDir sourceDir(sourcePath);
    QDir destDir(m_wowPath);
    
    int total = itemsToMigrate.size();
    int current = 0;
    
    for (const QString& item : itemsToMigrate) {
        QString srcName = item;
        QString destName = item;

        // Force enable DXVK during migration
        if (item == "d3d9.dll" || item == "dxvk.conf") {
            srcName = item + ".disabled";
            destName = item;
            
            // If the source .disabled doesn't exist, try the enabled name as fallback
            if (!QFile::exists(sourcePath + "/" + srcName)) {
                srcName = item;
            }
        }

        QString srcItemPath = sourcePath + "/" + srcName;
        QString destItemPath = m_wowPath + "/" + destName;
        
        QFileInfo info(srcItemPath);
        if (!info.exists()) {
            continue;
        }

        if (info.isDir()) {
            // Use robocopy for reliable folder migration with overwrites
            QProcess::execute("robocopy", {srcItemPath, destItemPath, "/E", "/IS", "/MOVE"});
        } else if (info.isFile()) {
            if (QFile::exists(destItemPath)) {
                QFile::remove(destItemPath);
            }
            QFile::copy(srcItemPath, destItemPath);
        }
        
        current++;
        QCoreApplication::processEvents(); // Prevent freeze during migration
        emit progressChanged(static_cast<int>((current * 100) / total),
            tr("%1 — %2").arg(InstallProgress::titre(InstallProgress::Installation), item));
    }
    
    generateConfigWtf();
    
    cleanup();
    m_installing = false;
    emit finished(true, tr("Patch HD installé avec succès."));
}

void HdPatchManager::cleanup() {
    QDir dir(m_tempDir);
    if (dir.exists()) {
        dir.removeRecursively();
    }
}

void HdPatchManager::generateConfigWtf() {
    QString wtfPath = m_wowPath + "/WTF";
    QDir dir(wtfPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QSaveFile configFile(wtfPath + "/Config.wtf");
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        out << "SET locale \"frFR\"\n";
        out << "SET hwDetect \"0\"\n";
        out << "SET gxRefresh \"60\"\n";
        out << "SET gxMultisampleQuality \"0.000000\"\n";
        out << "SET gxWindow \"1\"\n";
        out << "SET videoOptionsVersion \"3\"\n";
        out << "SET movie \"0\"\n";
        out << "SET Gamma \"1.000000\"\n";
        out << "SET readTOS \"1\"\n";
        out << "SET readEULA \"1\"\n";
        out << "SET readTerminationWithoutNotice \"1\"\n";
        out << "SET showToolsUI \"1\"\n";
        out << "SET Sound_OutputDriverName \"System Default\"\n";
        out << "SET Sound_MusicVolume \"0.40000000596046\"\n";
        out << "SET Sound_AmbienceVolume \"0.60000002384186\"\n";
        out << "SET componentTextureLevel \"9\"\n";
        out << "SET farclip \"1277\"\n";
        out << "SET projectedTextures \"1\"\n";
        out << "SET weatherDensity \"3\"\n";
        out << "SET accounttype \"LK\"\n";
        out << "SET mouseSpeed \"1.2000000476837\"\n";
        out << "SET gameTip \"16\"\n";
        out << "SET uiScale \"0.75999999046326\"\n";
        out << "SET useUiScale \"1\"\n";
        out << "SET checkAddonVersion \"0\"\n";
        out << "SET Sound_VoiceChatInputDriverName \"RÃ©glage du systÃ¨me\"\n";
        out << "SET Sound_VoiceChatOutputDriverName \"RÃ©glage du systÃ¨me\"\n";
        out << "SET movieSubtitle \"1\"\n";
        out << "SET Sound_SFXVolume \"0.5\"\n";
        out << "SET dbCompress \"0\"\n";
        out << "SET gxResolution \"1920x1080\"\n";
        out << "SET textureFilteringMode \"5\"\n";
        out << "SET groundEffectDist \"140\"\n";
        out << "SET environmentDetail \"1.5\"\n";
        out << "SET timingTestError \"0\"\n";
        out << "SET windowResizeLock \"1\"\n";
        out << "SET particleDensity \"1\"\n";
        out << "SET mapShadows \"0\"\n";
        out << "SET vertexShaders \"0\"\n";
        out << "SET pixelShaders \"0\"\n";
        out << "SET M2UseShaders \"0\"\n";
        out << "SET M2Faster \"3\"\n";
        out << "SET shadowinstancing \"0\"\n";
        out << "SET objectFade \"0\"\n";
        out << "SET timingMethod \"2\"\n";
        out << "SET ffxSpecial \"0\"\n";
        out << "SET groundEffectDensity \"64\"\n";
        out << "SET realmList \"sylvania-servergame.com\"\n";
        out << "SET realmName \"The Kingdom of Sylvania\"\n";
        out << "SET specular \"1\"\n";
        out << "SET Sound_MasterVolume \"0.60000002384186\"\n";
        out << "SET Sound_ZoneMusicNoDelay \"1\"\n";
        out << "SET gxTripleBuffer \"1\"\n";
        out << "SET gxMultisample \"8\"\n";
        out << "SET shadowLevel \"0\"\n";
        out << "SET extShadowQuality \"5\"\n";
        out.flush();
        configFile.commit();  // atomic rename, avoid half-written Config.wtf
    }
}

void HdPatchManager::onDownloadError(QNetworkReply::NetworkError error) {
    QString errorMsg;
    switch (error) {
        case QNetworkReply::ConnectionRefusedError:
            errorMsg = tr("Connexion refusée par le serveur");
            break;
        case QNetworkReply::HostNotFoundError:
            errorMsg = tr("Serveur introuvable");
            break;
        case QNetworkReply::TimeoutError:
            errorMsg = tr("Délai de connexion dépassé");
            break;
        default:
            errorMsg = tr("Erreur réseau: ") + m_reply->errorString();
    }
    
    m_installing = false;
    emit finished(false, errorMsg);
}

bool HdPatchManager::isInstalled(const QString& wowPath) {
    if (wowPath.isEmpty()) return false;

    // Détection basée sur la présence de fichiers clés du Patch HD
    // patch-w.mpq (arbres) et patch-5.mpq (eau/ciel/sorts) sont les plus représentatifs
    QStringList checks = {
        "/Data/patch-w.mpq", 
        "/Data/patch-w.mpq.disabled",
        "/Data/patch-5.mpq", 
        "/Data/patch-5.mpq.disabled"
    };

    for (const QString& file : checks) {
        if (QFile::exists(wowPath + file)) {
            return true;
        }
    }

    return false;
}

QString HdPatchManager::octets(qint64 n) {
    const double go = n / 1e9;
    return go >= 1 ? QString::number(go, 'f', 1).replace('.', ',') + " Go"
                   : QString::number(n / 1e6, 'f', 0) + " Mo";
}

QString HdPatchManager::formatBytes(qint64 bytes) const {
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024 && unitIndex < 3) {
        size /= 1024;
        unitIndex++;
    }

    QString unit;
    switch(unitIndex) {
        case 0: unit = tr("o"); break;
        case 1: unit = tr("Ko"); break;
        case 2: unit = tr("Mo"); break;
        case 3: unit = tr("Go"); break;
    }
    return QString::number(size, 'f', 2) + " " + unit;
}
