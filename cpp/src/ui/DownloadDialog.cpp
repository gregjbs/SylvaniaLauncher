#include "DownloadDialog.h"
#include "Constants.h"
#include "GameEdition.h"
#include "ConfigManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QTimer>
#include <QFileInfo>
#include <QProcess>
#include "ErraiNetwork.h"
#include "InstallProgress.h"
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTextStream>
#include <QtConcurrent>
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QDebug>

DownloadDialog::DownloadDialog(QWidget* parent, const QString& destination)
    : QDialog(parent)
    , m_networkManager(std::make_unique<QNetworkAccessManager>(this))
    , m_destination(destination)
    , m_downloadUrl(QString::fromUtf8(SylvaniaConstants::kWotlkClientUrl))
{
    setWindowTitle(tr("Téléchargement du Client WoW"));
    setFixedSize(620, 290);
    setModal(true);

    setupUi();

    // Start download after dialog is shown
    if (!destination.isEmpty()) {
        QTimer::singleShot(100, this, [this]() { startDownload(); });
    }
}

DownloadDialog::~DownloadDialog() {
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void DownloadDialog::configureForEdition(const GameEdition& edition) {
    m_editionId = edition.id;
    m_downloadUrl = edition.clientDownloadUrl;
    m_archiveFormat = edition.archiveFormat;
    m_expectedSize = edition.clientExpectedSize;
    m_expectedSha256 = edition.clientExpectedSha256.toLower();
    m_requireHash = edition.requireHashBeforeExtract;
    m_exeCandidates = edition.clientExeCandidates;
    if (m_realmlistAddress.isEmpty()) {
        m_realmlistAddress = edition.defaultRealmlist;
    }
    // Embedded per-chunk integrity manifest (enables on-the-fly chunk repair).
    loadChunkManifest(edition.chunkManifestResource);
    setWindowTitle(tr("Téléchargement du Client — %1").arg(edition.displayName));
}

QString DownloadDialog::archivePath() const {
    const QString suffix = (m_archiveFormat == QLatin1String("tar.gz"))
                               ? QStringLiteral(".tar.gz")
                               : QStringLiteral(".zip");
    return m_destination + "/ErraiClient_" + m_editionId + "_temp" + suffix;
}

void DownloadDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(SylvaniaConstants::kSpaceMd);
    mainLayout->setContentsMargins(SylvaniaConstants::kSpaceLg, SylvaniaConstants::kSpaceLg,
                                   SylvaniaConstants::kSpaceLg, SylvaniaConstants::kSpaceLg);

    // Les quatre étapes de l'installation
    m_stepsLabel = new QLabel(this);
    m_stepsLabel->setTextFormat(Qt::RichText);
    m_stepsLabel->setAlignment(Qt::AlignCenter);
    m_stepsLabel->setStyleSheet("font-size: 12px;");
    m_stepsLabel->setText(InstallProgress::liste(InstallProgress::Telechargement));
    mainLayout->addWidget(m_stepsLabel);

    // Status
    m_statusLabel = new QLabel(tr("Préparation du téléchargement..."), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: #d4af37; font-size: 14px; font-weight: bold;");
    mainLayout->addWidget(m_statusLabel);

    // Progress bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setStyleSheet(R"(
        QProgressBar {
            background-color: #2a2a4e;
            border: 1px solid #3a3a5e;
            border-radius: 5px;
            height: 25px;
            text-align: center;
            color: #ffffff;
        }
        QProgressBar::chunk {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #d4af37, stop:1 #8a7a2d);
            border-radius: 4px;
        }
    )");
    mainLayout->addWidget(m_progressBar);

    // Info layout
    QHBoxLayout* infoLayout = new QHBoxLayout();

    m_speedLabel = new QLabel(tr("Vitesse: --"), this);
    m_sizeLabel = new QLabel(tr("Taille: --"), this);
    m_etaLabel = new QLabel(tr("Temps restant: --"), this);

    QString labelStyle = "color: #888888; font-size: 12px;";
    m_speedLabel->setStyleSheet(labelStyle);
    m_sizeLabel->setStyleSheet(labelStyle);
    m_etaLabel->setStyleSheet(labelStyle);

    infoLayout->addWidget(m_speedLabel);
    infoLayout->addStretch();
    infoLayout->addWidget(m_sizeLabel);
    infoLayout->addStretch();
    infoLayout->addWidget(m_etaLabel);

    mainLayout->addLayout(infoLayout);
    mainLayout->addStretch();

    // Cancel button
    m_cancelButton = new QPushButton(tr("Annuler"), this);
    m_cancelButton->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a4a2d, stop:0.5 #3a2a1d, stop:1 #2a1a0d);
            color: #d4af37;
            border: 1px solid #7a6a4d;
            border-radius: 5px;
            padding: 10px 30px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #6a5a3d, stop:0.5 #4a3a2d, stop:1 #3a2a1d);
        }
    )");

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    connect(m_cancelButton, &QPushButton::clicked, this, &DownloadDialog::onCancelClicked);

    // Dialog style
    setStyleSheet("QDialog { background-color: #1a1a2e; }");
}

void DownloadDialog::setEtape(int etape) {
    m_stepsLabel->setText(InstallProgress::liste(etape));
}

bool DownloadDialog::checkDiskSpace(qint64 requiredBytes) {
    if (requiredBytes <= 0) return true;
    const QStorageInfo storage(m_destination);
    const qint64 available = storage.bytesAvailable();
    if (available >= 0 && available < requiredBytes) {
        failDownload(tr("Espace disque insuffisant: %1 libres, %2 requis (archive + extraction).")
                         .arg(formatBytes(available), formatBytes(requiredBytes)));
        return false;
    }
    return true;
}

void DownloadDialog::failDownload(const QString& message, const QString& fileToRemove) {
    // Stop a still-running transfer first; m_cancelled suppresses the error
    // handler so the user sees exactly one failure message and one
    // downloadFinished(false) signal.
    if (m_reply && m_reply->isRunning()) {
        m_cancelled = true;
        m_reply->abort();
    }
    if (m_file) {
        m_file->close();
        m_file.reset();
    }
    if (!fileToRemove.isEmpty()) {
        QFile::remove(fileToRemove);
    }
    QMessageBox::critical(this, tr("Erreur de téléchargement"), message);
    emit downloadFinished(false, message);
    reject();
}

void DownloadDialog::startDownload(const QString& directory) {
    if (!directory.isEmpty()) {
        m_destination = directory;
    }

    if (m_destination.isEmpty()) {
        emit downloadFinished(false, tr("Destination non spécifiée"));
        reject();
        return;
    }

    // Ensure destination directory exists and is writable
    QDir dir(m_destination);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            failDownload(tr("Impossible de créer le dossier de destination.\nVérifiez vos droits d'administrateur."));
            return;
        }
    }

    QFileInfo dirInfo(m_destination);
    if (!dirInfo.isWritable()) {
        failDownload(tr("Impossible d'écrire dans le dossier de destination.\nVérifiez vos droits d'administrateur."));
        return;
    }

    // Free-space gate before anything is transferred: archive + extracted
    // tree coexist during installation, hence the multiplier.
    if (m_expectedSize > 0) {
        m_diskSpaceChecked = true;
        if (!checkDiskSpace(static_cast<qint64>(
                m_expectedSize * SylvaniaConstants::kClientDiskSpaceFactor))) {
            return;
        }
    }

    m_speedTimer.start();

    // Sized clients use the robust, resumable segmented downloader; endpoints
    // of unknown size (e.g. the WotLK enUS pack) use a single stream.
    if (m_expectedSize > 0) {
        startSegmentedDownload();
    } else {
        startSingleStream();
    }
}

// ---------------------------------------------------------------------------
// Single streamed download (unknown size)
// ---------------------------------------------------------------------------
void DownloadDialog::startSingleStream() {
    // Written straight to disk as it arrives, never buffered in memory.
    m_file = std::make_unique<QFile>(archivePath());
    if (!m_file->open(QIODevice::WriteOnly)) {
        failDownload(tr("Impossible de créer le fichier de téléchargement"));
        return;
    }

    setEtape(InstallProgress::Telechargement);
    m_statusLabel->setText(tr("Connexion au serveur..."));

    QNetworkRequest request{QUrl(m_downloadUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);

    m_reply = m_networkManager->get(request);
    ErraiNetwork::watch(m_reply);

    connect(m_reply, &QNetworkReply::downloadProgress,
            this, &DownloadDialog::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished,
            this, &DownloadDialog::onDownloadFinished);
    connect(m_reply, &QNetworkReply::errorOccurred,
            this, &DownloadDialog::onDownloadError);

    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_file && m_file->isOpen()) {
            m_file->write(m_reply->readAll());
        }
    });
}

// ---------------------------------------------------------------------------
// Segmented, resumable download (known size + server byte-range support)
// ---------------------------------------------------------------------------
void DownloadDialog::loadChunkManifest(const QString& resourcePath) {
    m_haveChunks = false;
    m_chunkHashes.clear();
    m_chunkSize = 0;
    if (resourcePath.isEmpty()) return;

    QFile f(resourcePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QList<QByteArray> lines = f.readAll().split('\n');
    if (lines.isEmpty() || lines.first().trimmed() != "SYLCHUNKS1") return;

    qint64 size = 0, chunk = 0;
    int i = 1;
    for (; i < lines.size(); ++i) {
        const QByteArray l = lines[i].trimmed();
        if (l.startsWith("size=")) size = l.mid(5).toLongLong();
        else if (l.startsWith("chunk=")) chunk = l.mid(6).toLongLong();
        else break;  // first non-header line = first hash
    }
    if (chunk <= 0) return;

    QStringList hashes;
    for (; i < lines.size(); ++i) {
        const QByteArray l = lines[i].trimmed();
        if (l.size() == 64) hashes << QString::fromLatin1(l).toLower();
    }

    // The manifest must describe exactly the client we expect.
    if (size != m_expectedSize) {
        qWarning() << "Manifeste de blocs ignoré: taille" << size << "!=" << m_expectedSize;
        return;
    }
    const qint64 expectedCount = (size + chunk - 1) / chunk;
    if (hashes.size() != expectedCount) {
        qWarning() << "Manifeste de blocs ignoré: " << hashes.size()
                   << "blocs au lieu de" << expectedCount;
        return;
    }

    m_chunkSize = chunk;
    m_chunkHashes = hashes;
    m_segmentSize = chunk;    // one download segment == one verifiable chunk
    m_haveChunks = true;
    qDebug() << "Manifeste de blocs chargé:" << hashes.size() << "blocs de" << chunk << "octets";
}

void DownloadDialog::startSegmentedDownload() {
    qint64 existing = 0;
    const QFileInfo fi(archivePath());
    if (fi.exists()) existing = fi.size();
    if (existing > m_expectedSize) existing = 0;  // garbage -> restart fresh
    // Resume must land on a whole-chunk boundary so each segment maps to one
    // chunk hash; drop any partial trailing chunk from a previous run.
    if (m_haveChunks && m_chunkSize > 0 && existing < m_expectedSize) {
        existing = (existing / m_chunkSize) * m_chunkSize;
    }
    m_downloadOffset = existing;

    // ReadWrite keeps already-downloaded (and previously verified) bytes; we
    // then trim to the aligned offset and append from there.
    m_file = std::make_unique<QFile>(archivePath());
    if (!m_file->open(QIODevice::ReadWrite)) {
        failDownload(tr("Impossible de créer le fichier de téléchargement"));
        return;
    }
    if (!m_file->resize(m_downloadOffset) || !m_file->seek(m_downloadOffset)) {
        failDownload(tr("Impossible de préparer le fichier de téléchargement"));
        return;
    }

    if (m_downloadOffset >= m_expectedSize) {
        onDownloadAssembled();
        return;
    }

    m_segmentRetries = 0;
    m_segBuffer.clear();
    // Seed the speed baseline at the resume offset so the first second isn't a
    // spike of "all bytes already on disk".
    m_lastReceivedBytes = m_downloadOffset;
    m_speedTimer.restart();
    setEtape(InstallProgress::Telechargement);
    m_statusLabel->setText(tr("Connexion au serveur..."));
    requestNextSegment();
}

void DownloadDialog::requestNextSegment() {
    if (m_cancelled) return;

    if (m_downloadOffset >= m_expectedSize) {
        onDownloadAssembled();
        return;
    }

    const qint64 segSize = m_segmentSize > 0 ? m_segmentSize
                                             : SylvaniaConstants::kDownloadSegmentBytes;
    const qint64 start = m_downloadOffset;
    const qint64 end = qMin(start + segSize, m_expectedSize) - 1;  // inclusive

    m_segBuffer.clear();
    m_segBuffer.reserve(end - start + 1);

    QNetworkRequest request{QUrl(m_downloadUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setRawHeader("Range",
        QStringLiteral("bytes=%1-%2").arg(start).arg(end).toUtf8());

    m_reply = m_networkManager->get(request);
    ErraiNetwork::watch(m_reply);

    // Buffer the segment in memory; it is committed to disk only after passing
    // its chunk hash. The UI tracks committed + in-flight bytes.
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_reply) { m_segBuffer.append(m_reply->readAll()); updateDownloadUi(); }
    });
    connect(m_reply, &QNetworkReply::finished, this, &DownloadDialog::onSegmentFinished);
}

bool DownloadDialog::commitSegmentBuffer() {
    if (m_segBuffer.isEmpty()) return true;
    if (!m_file || !m_file->isOpen()) return false;
    const qint64 written = m_file->write(m_segBuffer);
    if (written != m_segBuffer.size()) {
        failDownload(tr("Erreur d'écriture sur le disque (espace insuffisant ?). "
                        "Le téléchargement a été interrompu."));
        return false;
    }
    m_downloadOffset += written;
    m_segBuffer.clear();
    return true;
}

void DownloadDialog::onSegmentFinished() {
    if (m_cancelled || !m_reply) return;

    QNetworkReply* reply = m_reply;
    m_reply = nullptr;

    const bool ok = reply->error() == QNetworkReply::NoError;
    if (ok) m_segBuffer.append(reply->readAll());  // drain any remaining bytes
    const QString netError = reply->errorString();
    reply->deleteLater();
    if (m_cancelled) return;

    // Re-fetch the SAME chunk on failure/corruption (bounded retries). Because
    // unverified bytes are never written, a retry simply re-requests the chunk
    // from m_downloadOffset — no duplicated or skipped bytes.
    auto retryOrFail = [this](const QString& why) {
        m_segBuffer.clear();
        if (++m_segmentRetries > SylvaniaConstants::kDownloadSegmentRetries) {
            failDownload(why);
            return;
        }
        m_statusLabel->setText(tr("Reprise du téléchargement…"));
        QTimer::singleShot(1500, this, [this]() { if (!m_cancelled) requestNextSegment(); });
    };

    if (!ok) {
        retryOrFail(tr("Erreur réseau pendant le téléchargement : %1").arg(netError));
        return;
    }

    // Verify this segment against its expected chunk hash (the core fix: a
    // corrupted chunk is caught and re-fetched immediately instead of failing
    // the whole multi-GB download at the very end).
    if (m_haveChunks) {
        const qint64 idx = m_downloadOffset / m_chunkSize;
        const QString got = QString::fromLatin1(
            QCryptographicHash::hash(m_segBuffer, QCryptographicHash::Sha256).toHex());
        const QString exp = idx < m_chunkHashes.size() ? m_chunkHashes.at(idx) : QString();
        if (idx >= m_chunkHashes.size() || got != exp) {
            qWarning() << "Bloc" << idx << "invalide (tentative" << m_segmentRetries
                       << "): reçu" << got.left(12) << "attendu" << exp.left(12);
            retryOrFail(tr("Bloc %1 corrompu après plusieurs tentatives "
                           "(le manifeste est peut-être périmé).").arg(idx));
            return;
        }
    }

    if (!commitSegmentBuffer()) return;  // disk error already reported
    m_segmentRetries = 0;
    updateDownloadUi();
    requestNextSegment();
}

void DownloadDialog::onDownloadAssembled() {
    if (m_file) { m_file->flush(); m_file->close(); }

    // Cheap final size sanity (the per-chunk hashes already guarantee content).
    if (m_expectedSize > 0 && QFileInfo(archivePath()).size() != m_expectedSize) {
        failDownload(tr("Fichier corrompu: taille inattendue. Le fichier a été supprimé."),
                     archivePath());
        return;
    }

    if (m_haveChunks) {
        // Every chunk passed its hash → the whole file is correct by
        // construction; skip the slow multi-GB re-hash and go to extraction.
        if (m_verifyOnly) {
            emit downloadFinished(true, tr("Intégrité vérifiée."));
            accept();
            return;
        }
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(100);
        emit progressChanged(100);
        m_statusLabel->setText(tr("Intégrité vérifiée (par blocs). Extraction…"));
        QCoreApplication::processEvents();
        QTimer::singleShot(200, this, [this]() { extractArchive(archivePath()); });
        return;
    }

    // No chunk manifest → fall back to a whole-file hash check.
    showVerifyingState();
    verifyIntegrity(archivePath());
}

void DownloadDialog::updateDownloadUi() {
    // updateSpeed() derives the ETA from m_totalBytes; the segmented path uses
    // the known expected size as the total. "live" = committed-on-disk bytes
    // plus the segment still buffered in memory.
    m_totalBytes = m_expectedSize;
    const qint64 live = m_downloadOffset + m_segBuffer.size();
    const int progress = m_expectedSize > 0
        ? static_cast<int>((live * 100) / m_expectedSize) : 0;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(progress);
    emit progressChanged(progress);
    m_statusLabel->setText(tr("%1 — %2%").arg(InstallProgress::titre(InstallProgress::Telechargement)).arg(progress));
    m_sizeLabel->setText(tr("Taille: %1 / %2")
        .arg(formatBytes(live), formatBytes(m_expectedSize)));
    updateSpeed(live);
}

void DownloadDialog::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    m_receivedBytes = bytesReceived;
    m_totalBytes = bytesTotal;

    // If the expected size was unknown, gate on the server-announced size as
    // soon as it is known (done once).
    if (!m_diskSpaceChecked && bytesTotal > 0) {
        m_diskSpaceChecked = true;
        if (!checkDiskSpace(static_cast<qint64>(
                bytesTotal * SylvaniaConstants::kClientDiskSpaceFactor))) {
            if (m_reply) m_reply->abort();
            return;
        }
    }

    if (bytesTotal > 0) {
        int progress = static_cast<int>((bytesReceived * 100) / bytesTotal);
        m_progressBar->setValue(progress);
        emit progressChanged(progress);

        m_statusLabel->setText(tr("%1 — %2%").arg(InstallProgress::titre(InstallProgress::Telechargement)).arg(progress));
        m_sizeLabel->setText(tr("Taille: %1 / %2")
            .arg(formatBytes(bytesReceived))
            .arg(formatBytes(bytesTotal)));
    }

    updateSpeed(bytesReceived);
}

void DownloadDialog::updateSpeed(qint64 bytesReceived) {
    qint64 elapsed = m_speedTimer.elapsed();

    if (elapsed > 1000) { // Update every second
        qint64 bytesPerSecond = ((bytesReceived - m_lastReceivedBytes) * 1000) / elapsed;
        m_lastReceivedBytes = bytesReceived;
        m_speedTimer.restart();

        m_speedLabel->setText(tr("Vitesse: %1/s").arg(formatBytes(bytesPerSecond)));

        // Calculate ETA
        if (bytesPerSecond > 0 && m_totalBytes > 0) {
            qint64 remaining = m_totalBytes - bytesReceived;
            qint64 secondsRemaining = remaining / bytesPerSecond;
            m_etaLabel->setText(tr("Temps restant: %1").arg(formatDuration(secondsRemaining)));
        }
    }
}

void DownloadDialog::onDownloadFinished() {
    if (m_cancelled) return;

    if (m_reply->error() != QNetworkReply::NoError) {
        return; // Error handler will take care of it
    }

    // Close the file
    if (m_file) {
        m_file->close();
    }

    showVerifyingState();
    verifyIntegrity(archivePath());
}

void DownloadDialog::showVerifyingState() {
    setEtape(InstallProgress::Verification);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    emit progressChanged(0);
    m_statusLabel->setText(tr("%1 (SHA-256)…").arg(InstallProgress::titre(InstallProgress::Verification)));
    m_speedLabel->setText(tr("On s'assure que l'archive est intacte avant de l'ouvrir."));
    m_sizeLabel->setText("");
    m_etaLabel->setText("");
    QCoreApplication::processEvents();   // repaint before the long hash starts
}

void DownloadDialog::verifyIntegrity(const QString& filePath) {
    // 1) Expected size, when known. Checked before hashing — cheap and final.
    if (m_expectedSize > 0) {
        const qint64 actualSize = QFileInfo(filePath).size();
        if (actualSize != m_expectedSize) {
            failDownload(tr("Fichier corrompu: taille inattendue (%1 au lieu de %2). "
                            "Le fichier a été supprimé.")
                             .arg(formatBytes(actualSize), formatBytes(m_expectedSize)),
                         filePath);
            return;
        }
    }

    // 2) SHA-256 — REQUIRED for editions flagged requireHashBeforeExtract:
    // if the expected hash is not configured, the archive is never extracted
    // (no silent degradation to "pas de vérif").
    if (m_expectedSha256.isEmpty()) {
        if (m_requireHash) {
            failDownload(tr("Le hash SHA-256 attendu du client n'est pas encore configuré: "
                            "extraction refusée par sécurité. Le fichier téléchargé a été "
                            "supprimé. Mettez le launcher à jour."),
                         filePath);
            return;
        }
        // Legacy WotLK endpoint publishes no hash; log and continue.
        qWarning() << "Aucun hash attendu configuré pour" << m_editionId
                   << "- extraction sans comparaison.";
    }

    QFutureWatcher<QByteArray>* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher, filePath]() {
        const QByteArray hash = watcher->result();
        watcher->deleteLater();
        if (m_cancelled) return;

        const QString actualHex = QString::fromLatin1(hash.toHex()).toLower();
        qDebug() << "Downloaded file SHA-256:" << actualHex;

        if (!m_expectedSha256.isEmpty()) {
            if (hash.isEmpty() || actualHex != m_expectedSha256) {
                failDownload(tr("Fichier corrompu: le hash SHA-256 ne correspond pas. "
                                "Le fichier a été supprimé."),
                             filePath);
                return;
            }
        }

        // Self-test: integrity confirmed, stop before extraction.
        if (m_verifyOnly) {
            emit downloadFinished(true, tr("Intégrité vérifiée."));
            accept();
            return;
        }

        // Setup extraction progress indication
        m_statusLabel->setText(tr("Extraction des fichiers en cours..."));
        m_speedLabel->setText(tr("Veuillez patienter..."));
        m_sizeLabel->setText("");
        m_etaLabel->setText("");

        QCoreApplication::processEvents();

        // Extract in a timer to allow UI update
        QTimer::singleShot(200, this, [this, filePath]() {
            extractArchive(filePath);
        });
    });

    // SHA-256 hors du fil de l'interface, avec un pourcentage réel.
    setEtape(InstallProgress::Verification);
    const InstallProgress::Empreinte empreinte = InstallProgress::calculerSha256(filePath);
    QTimer* suivi = new QTimer(watcher);
    suivi->setInterval(300);
    connect(suivi, &QTimer::timeout, this, [this, empreinte]() {
        if (empreinte.total <= 0) return;
        const qint64 lus = empreinte.lus->load();
        const int pct = static_cast<int>(lus * 100 / empreinte.total);
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(pct);
        emit progressChanged(pct);
        m_statusLabel->setText(tr("%1 (SHA-256) — %2%")
            .arg(InstallProgress::titre(InstallProgress::Verification)).arg(pct));
        m_sizeLabel->setText(tr("%1 / %2").arg(formatBytes(lus), formatBytes(empreinte.total)));
    });
    suivi->start();

    watcher->setFuture(empreinte.resultat);
}

void DownloadDialog::extractArchive(const QString& archivePath) {
    if (m_archiveFormat == QLatin1String("tar.gz")) {
        // Single pass: bsdtar is safe-by-default (no -P), so we don't pre-list
        // the archive — that would decompress a multi-GB client twice.
        extractTarGz(archivePath);
    } else {
        extractZip(archivePath);
    }
}

void DownloadDialog::extractZip(const QString& archivePath) {
    runExtractionProcess(archivePath, SylvaniaConstants::extractArchiveArgs());
}

void DownloadDialog::extractTarGz(const QString& archivePath) {
    runExtractionProcess(archivePath, SylvaniaConstants::extractTarGzArgs());
}

void DownloadDialog::runExtractionProcess(const QString& archivePath, const QStringList& args) {
    QProcess* process = new QProcess(this);

    // C1: paths flow via environment variables instead of being interpolated
    // into the PowerShell command string -> no shell-injection surface even
    // when the destination path contains quotes, $() or backticks.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("SYL_SRC", QDir::toNativeSeparators(archivePath));
    env.insert("SYL_DST", QDir::toNativeSeparators(m_destination));
    process->setProcessEnvironment(env);

    // The extraction runs in a child process. Show a busy/indeterminate bar
    // instead of recursively scanning the destination on the UI thread every
    // few seconds: that scan froze the UI on large clients, and could walk an
    // entire drive when installing to a drive root (e.g. D:\).
    // Fork Errai : avancement mesuré sur les seuls dossiers créés par la
    // décompression (jamais tout le dossier de destination), en arrière-plan.
    setEtape(InstallProgress::Decompression);
    const qint64 attendu = m_expectedExtracted > 0 ? m_expectedExtracted
                                                   : QFileInfo(archivePath).size();
    const bool estime = m_expectedExtracted <= 0;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("%1…").arg(InstallProgress::titre(InstallProgress::Decompression)));
    m_speedLabel->setText(tr("Les fichiers du jeu sont extraits de l'archive."));
    m_sizeLabel->setText("");
    m_etaLabel->setText("");
    emit progressChanged(0);

    auto* suivi = new SuiviDecompression(m_destination, QFileInfo(archivePath).fileName(), attendu, process);
    connect(suivi, &SuiviDecompression::avancement, this, [this, estime](qint64 octets, qint64 total) {
        if (total <= 0) return;
        const int pct = static_cast<int>(qMin<qint64>(99, octets * 100 / total));
        m_progressBar->setValue(pct);
        emit progressChanged(pct);
        m_statusLabel->setText(tr("%1 — %2%")
            .arg(InstallProgress::titre(InstallProgress::Decompression)).arg(pct));
        m_sizeLabel->setText(tr("%1 / %2%3").arg(formatBytes(octets), estime ? tr("≈ ") : QString(),
                                                 formatBytes(total)));
    });
    suivi->demarrer();

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, archivePath](int exitCode, QProcess::ExitStatus exitStatus) {
        process->deleteLater();

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            onExtractionSucceeded(archivePath);
        } else {
            // Surface the real reason: tar's stderr (disk space, unreadable
            // entry, path too long...) plus the exit code. The archive is kept
            // so the user can retry the extraction without re-downloading.
            QString detail = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
            if (detail.isEmpty())
                detail = QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed();
            if (detail.isEmpty())
                detail = tr("code de sortie %1").arg(exitCode);
            emit downloadFinished(false, tr("Erreur lors de l'extraction : %1").arg(detail));
            reject();
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        process->deleteLater();
        emit downloadFinished(false, tr("Erreur lors de l'extraction: impossible de lancer PowerShell"));
        reject();
    });

    process->start("powershell", args);
}

void DownloadDialog::onExtractionSucceeded(const QString& archivePath) {
    setEtape(InstallProgress::Installation);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(100);
    m_statusLabel->setText(tr("%1 — configuration du client…")
        .arg(InstallProgress::titre(InstallProgress::Installation)));
    m_speedLabel->setText(tr("Suppression de l'archive temporaire, réglages du jeu."));
    m_sizeLabel->setText("");
    emit progressChanged(100);
    QCoreApplication::processEvents();

    // Remove temp archive
    QFile::remove(archivePath);

    // Generate the launch config if not skipped
    if (!m_skipConfigWtf) {
        generateConfigWtf();
    }

    setEtape(InstallProgress::kNombreEtapes);   // tout coché
    emit downloadFinished(true, tr("Installation terminée : téléchargement, vérification, "
                                   "décompression et installation réussis."));
    accept();
}

void DownloadDialog::onDownloadError(QNetworkReply::NetworkError error) {
    if (m_cancelled) return;

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

    if (m_file) {
        m_file->close();
        m_file->remove();
        m_file.reset();
    }

    QMessageBox::critical(this, tr("Erreur de téléchargement"), errorMsg);
    emit downloadFinished(false, errorMsg);
    reject();
}

void DownloadDialog::onCancelClicked() {
    m_cancelled = true;

    if (m_reply) {
        m_reply->abort();
    }

    if (m_file) {
        m_file->close();
        m_file->remove();
        m_file.reset();
    }

    emit downloadFinished(false, tr("Téléchargement annulé"));
    reject();
}

QString DownloadDialog::formatBytes(qint64 bytes) const {
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

QString DownloadDialog::formatDuration(qint64 seconds) const {
    if (seconds < 60) {
        return QString("%1s").arg(seconds);
    } else if (seconds < 3600) {
        return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60);
    } else {
        return QString("%1h %2m").arg(seconds / 3600).arg((seconds % 3600) / 60);
    }
}

void DownloadDialog::generateConfigWtf() {
    QString targetPath = m_destination;

    // Search for the actual client directory (containing the edition's exe).
    QDirIterator it(m_destination, m_exeCandidates, QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        targetPath = QFileInfo(it.next()).absolutePath();
    }

    QString wtfPath = targetPath + "/WTF";
    QDir dir(wtfPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    const QString locale = (m_language == "en") ? QStringLiteral("enUS") : QStringLiteral("frFR");
    const QString host = ConfigManager::hostFromAddress(m_realmlistAddress);

    // C3: atomic write so a crash mid-write can't leave a half-written Config.wtf.
    QSaveFile configFile(wtfPath + "/Config.wtf");
    if (!configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    QTextStream out(&configFile);

    if (m_editionId == QLatin1String("legion")) {
        // Legion (7.3.5) launch config: the login server is SET portal; the
        // 3.3.5 realmList/realmName keys don't apply.
        out << "SET portal \"" << host << "\"\n";
        out << "SET textLocale \"" << locale << "\"\n";
        out << "SET audioLocale \"" << locale << "\"\n";
        out << "SET agentUILocale \"" << locale << "\"\n";
        out << "SET readTOS \"1\"\n";
        out << "SET readEULA \"1\"\n";
        out << "SET gxWindow \"1\"\n";
        out << "SET gxMaximize \"1\"\n";
    } else {
        out << "SET locale \"" << locale << "\"\n";
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
        out << "SET Sound_VoiceChatInputDriverName \"Réglage du système\"\n";
        out << "SET Sound_VoiceChatOutputDriverName \"Réglage du système\"\n";
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
        out << "SET realmList \"" << host << "\"\n";
        out << "SET realmName \"Errai\"\n";
        out << "SET specular \"1\"\n";
        out << "SET Sound_MasterVolume \"0.60000002384186\"\n";
        out << "SET Sound_ZoneMusicNoDelay \"1\"\n";
        out << "SET gxTripleBuffer \"1\"\n";
        out << "SET gxMultisample \"8\"\n";
        out << "SET shadowLevel \"0\"\n";
        out << "SET extShadowQuality \"5\"\n";
    }
    out.flush();
    configFile.commit();
}
