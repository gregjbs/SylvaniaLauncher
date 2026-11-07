#pragma once

#include <QDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <memory>
#include <vector>

#include "AddonRegistry.h"

class ConfigManager;
class AddonManifest;
struct ManifestAddon;
class QScrollArea;
class QVBoxLayout;
class QTabWidget;
class QListWidget;

// One quick-install entry of the bundled catalogue (single-folder addons).
struct AddonInfo {
    QString name;
    QString description;
    QString downloadFileName;
    QString folderName;
    // Fork Errai : renseignés quand l'entrée vient du manifeste.
    QString url;            // vide = kAddonDownloadEndpoint + downloadFileName
    QStringList folders;    // vide = { folderName }
    QString sha256;
    QString id;             // vide = folderName
};

class AddonsWindow : public QDialog {
    Q_OBJECT

public:
    explicit AddonsWindow(ConfigManager* config, QWidget* parent = nullptr);
    ~AddonsWindow() override;

private:
    // A unified description of something the launcher can download + install,
    // built from either a manifest entry (recommended) or a catalogue entry.
    struct InstallTarget {
        QString id;
        QString name;
        QString version;     // empty if unknown (catalogue)
        QString url;         // full download URL
        QStringList folders; // folders expected under Interface/AddOns
        QString source;      // "recommended" | "catalog"
        QString sha256;      // vide = non vérifiée
    };

    // Per-row widgets so a card can be refreshed after install/uninstall.
    struct AddonCard {
        QPushButton* primaryBtn = nullptr;   // Install / Update
        QPushButton* uninstallBtn = nullptr; // Uninstall (recommended only)
        QProgressBar* progressBar = nullptr;
        QLabel* statusLabel = nullptr;
        QLabel* versionLabel = nullptr;
        InstallTarget target;
    };

private slots:
    void onManifestLoaded(bool fromRemote);
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();
    void onDownloadError(QNetworkReply::NetworkError error);

private:
    void setupUi();
    QWidget* buildRecommendedTab();
    QWidget* buildCatalogTab();
    void populateCatalogCards();   // (re)construit les cartes du catalogue
    QWidget* buildInstalledTab();
    void populateCatalog();
    void populateRecommendedCards();
    void refreshInstalledList();
    void refreshCard(const QString& id);

    // Install pipeline (shared by recommended + catalogue).
    void startInstall(const InstallTarget& target, AddonCard* card);
    void extractAndDeploy(const QString& zipPath, AddonCard* card);
    void finishInstall(bool success, const QString& message, AddonCard* card);
    void uninstallById(const QString& id);   // registry-aware (all folders)
    bool isTargetInstalled(const InstallTarget& target) const;

    // Filesystem helpers.
    static bool isZipFile(const QString& path);
    static bool moveDirInto(const QString& srcDir, const QString& destParent);
    static bool copyDirRecursive(const QString& src, const QString& dst);
    static QString findFolderIn(const QString& root, const QString& folderName, int maxDepth = 4);
    static QString readTocValue(const QString& tocPath, const QString& key);
    static qint64 dirSize(const QString& path);
    QString formatSize(qint64 bytes) const;

    bool ensureWowReady();   // checks WoW path + AddOns dir, shows i18n errors

    ConfigManager* m_config;
    AddonRegistry m_registry;
    AddonManifest* m_manifest = nullptr;
    std::unique_ptr<QNetworkAccessManager> m_networkManager;
    QNetworkReply* m_currentReply = nullptr;

    // UI
    QTabWidget* m_tabs = nullptr;
    QVBoxLayout* m_recommendedLayout = nullptr;
    QLabel* m_recommendedStatus = nullptr;
    QVBoxLayout* m_catalogLayout = nullptr;
    QListWidget* m_installedList = nullptr;

    // In-flight install context.
    QString m_activeZipPath;
    QString m_activeTempDir;
    AddonCard* m_activeCard = nullptr;

    std::vector<AddonInfo> m_catalog;
    std::vector<AddonCard*> m_cards;   // recommended + catalogue cards, by id
};
