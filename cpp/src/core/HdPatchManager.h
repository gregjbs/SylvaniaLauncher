#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <memory>

/**
 * @brief Manager for the WoW HD Patch installation
 * 
 * Handles:
 * 1. Downloading the ZIP patch
 * 2. Background extraction via PowerShell
 * 3. Moving files to client directory (overwriting)
 * 4. Cleaning up temp files
 */
class HdPatchManager : public QObject {
    Q_OBJECT

public:
    explicit HdPatchManager(const QString& wowPath, QObject* parent = nullptr);
    ~HdPatchManager() override;

    void startInstallation();
    bool isInstalling() const { return m_installing; }

    static bool isInstalled(const QString& wowPath);

signals:
    void progressChanged(int percentage, const QString& status);
    void finished(bool success, const QString& message);

private slots:
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();
    void onDownloadError(QNetworkReply::NetworkError error);

private:
    void extractPatch(const QString& zipPath);
    QString findExtractedRoot(const QString& extractPath) const;
    void migrateFiles(const QString& sourcePath);
    void generateConfigWtf();
    void cleanup();
    
    QString formatBytes(qint64 bytes) const;
    void verifyHash(const QString& filePath);

    QString m_wowPath;
    QString m_tempDir;
    QString m_downloadUrl;
    QString m_expectedSha256;   // vide = non vérifié
    qint64 m_expectedExtracted = 0;
    static QString octets(qint64 n);
    
    std::unique_ptr<QNetworkAccessManager> m_networkManager;
    QNetworkReply* m_reply = nullptr;
    std::unique_ptr<QFile> m_file;
    
    bool m_installing = false;
    qint64 m_totalBytes = 0;
};
