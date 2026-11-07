#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <vector>

/**
 * @brief Realmlist entry structure
 */
struct RealmlistEntry {
    QString name;
    QString address;
    bool active = false;
};

/**
 * @brief Configuration manager for Sylvania Launcher
 *
 * Handles reading/writing configuration settings to JSON file
 * stored in %LOCALAPPDATA%/SylvaniaLauncher/config.json
 *
 * v3.0: the config is edition-aware. Global settings (language, sound,
 * background, window geometry) stay top-level; everything client-related
 * (path, realmlist entries) lives in one object per edition under
 * "editions": {"wotlk": {...}, "legion": {...}}. Pre-3.0 top-level keys
 * are migrated transparently into editions.wotlk on first load.
 */
class ConfigManager : public QObject {
    Q_OBJECT

public:
    explicit ConfigManager(QObject* parent = nullptr);
    ~ConfigManager() override;

    // Generic config access
    QVariant get(const QString& key, const QVariant& defaultValue = QVariant()) const;
    void set(const QString& key, const QVariant& value);
    bool save();             // synchronous write
    void scheduleSave();     // H4: coalesce bursts of setter calls into one disk write

    // --- Active game edition ("2 launchers en 1") -------------------------
    // The id is validated against GameEdition::isKnownId; unknown values are
    // ignored on write and fall back to wotlk on read.
    QString activeEditionId() const;
    void setActiveEditionId(const QString& id);

    // --- Per-edition client path. Empty editionId = active edition. -------
    QString getWowPath(const QString& editionId = QString()) const;
    void setWowPath(const QString& path);                          // active edition
    void setWowPathFor(const QString& editionId, const QString& path);

    bool isFirstRun() const;
    void setFirstRun(bool firstRun);

    bool isSoundEnabled() const;
    void setSoundEnabled(bool enabled);

    QString getLanguage() const;
    void setLanguage(const QString& lang);

    QString getBackground() const;
    void setBackground(const QString& bgName);

    bool isRandomBackgroundEnabled() const;
    void setRandomBackgroundEnabled(bool enabled);

    // Fork Errai : ignorer le certificat (auto-signé) du serveur de
    // téléchargement Errai. Activé par défaut. Voir ErraiNetwork.
    bool isErraiSslCheckIgnored() const;
    void setErraiSslCheckIgnored(bool ignored);

    // Main window geometry persistence (saveGeometry/restoreGeometry payload,
    // stored base64-encoded in config.json). Empty = use the default size.
    QByteArray getWindowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);

    // Realmlist management — always operates on the ACTIVE edition's entries.
    std::vector<RealmlistEntry> getRealmlistEntries() const;
    void setRealmlistEntries(const std::vector<RealmlistEntry>& entries);
    void setRealmlistEntry(int index, const QString& name, const QString& address, bool active = false);
    int getActiveRealmlistIndex() const;
    void setActiveRealmlistIndex(int index);
    bool switchRealmlist(int index);
    // Edition-aware: wotlk rewrites realmlist.wtf; legion rewrites the
    // SET portal line of WTF/Config.wtf.
    bool updateRealmlist(const QString& realmlistText);

    // Strips "set realmlist"/"set portal"/quotes from a stored address,
    // leaving just the host for Config.wtf-style writes.
    static QString hostFromAddress(const QString& address);

signals:
    void configChanged(const QString& key);
    void realmlistChanged();
    void editionChanged(const QString& editionId);

private:
    void loadConfig();
    void createDefaultConfig();
    // v3.0: move pre-edition top-level keys (wow_path, realmlist_entries,
    // active_realmlist_index) into editions.wotlk and make sure every known
    // edition has a config object with default realmlist entries. Idempotent.
    bool migrateToEditionConfig();
    // Rewrites stale realmlist entries inherited from earlier launcher
    // versions (logon.sylvania-wow.com, sylvania-wow.com, etc.) to the
    // current canonical address. Returns true if at least one entry was
    // rewritten. Runs once on construction and is idempotent on subsequent
    // launches.
    bool migrateLegacyRealmlist();
    // v3.0 shipped a placeholder Legion portal before the real auth server was
    // known; rewrite any Legion entry still carrying it to the canonical
    // portal. Legion-scoped and idempotent. Returns true if anything changed.
    bool migrateLegionPortal();
    // Fork Errai : sur une installation existante, ajoute le serveur Errai en
    // tête de la liste WotLK (s'il n'y est pas) et le sélectionne. Une seule
    // fois (clé errai_default_applied) : un choix ultérieur du joueur est
    // respecté. Returns true if anything changed.
    bool migrateErraiDefault();
    QString getConfigPath() const;

    // Per-edition sub-object helpers.
    QJsonObject editionConfig(const QString& editionId) const;
    void setEditionConfig(const QString& editionId, const QJsonObject& obj);
    QJsonArray defaultRealmlistArrayFor(const QString& editionId) const;

    QJsonObject m_config;
    QString m_configDir;
    QTimer m_saveTimer;
};
