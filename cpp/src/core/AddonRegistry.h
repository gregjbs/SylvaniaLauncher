#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <vector>

/**
 * @brief One record in the local addon registry.
 *
 * folders holds the *real* folder names dropped into Interface/AddOns so a
 * multi-folder addon (e.g. ConsolePortLK -> ConsolePort + ConsolePortBar) can
 * be removed cleanly in one go.
 */
struct InstalledAddon {
    QString id;
    QString name;
    QString version;
    QStringList folders;
    QString source;   // "recommended" | "catalog" (informational)
};

/**
 * @brief Registry of addons installed *by the launcher*.
 *
 * Persisted as installed_addons.json in the launcher config directory
 * (%LOCALAPPDATA%/WowErraiLauncher), mirroring the existing config.json /
 * stats.json convention rather than QSettings, because each record carries a
 * structured list of folders. The registry is what makes clean multi-folder
 * uninstallation (§3) and update detection (§5) possible.
 */
class AddonRegistry {
public:
    AddonRegistry();

    void reload();

    bool contains(const QString& id) const;
    InstalledAddon get(const QString& id) const;
    QString installedVersion(const QString& id) const;
    QStringList folders(const QString& id) const;
    std::vector<InstalledAddon> all() const;

    // Insert or replace a record, then persist atomically. Returns false if the
    // file could not be written.
    bool record(const InstalledAddon& addon);
    // Remove a record by id, then persist atomically. No-op (returns true) if
    // the id is absent.
    bool remove(const QString& id);

    static QString filePath();

private:
    bool save() const;
    static InstalledAddon fromJson(const QString& id, const QJsonObject& obj);
    static QJsonObject toJson(const InstalledAddon& addon);

    QJsonObject m_addons;  // map: id -> record object
};
