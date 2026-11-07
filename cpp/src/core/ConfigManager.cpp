#include "ConfigManager.h"
#include "GameEdition.h"
#include "PathUtils.h"
#include "Constants.h"

#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

ConfigManager::ConfigManager(QObject* parent)
    : QObject(parent)
{
    m_configDir = PathUtils::getConfigDir();
    loadConfig();

    // v3.0: move pre-edition keys under editions.wotlk and seed the legion
    // config object. Must run before any per-edition accessor is used.
    bool dirty = migrateToEditionConfig();

    // Migrate any stale realmlist inherited from an older launcher build
    // (e.g. logon.sylvania-wow.com). Done before wiring up the debounce
    // timer so the corrected config is written synchronously and the
    // realmlist.wtf on disk is refreshed immediately.
    if (migrateLegacyRealmlist()) {
        dirty = true;
        const auto entries = getRealmlistEntries();
        const int activeIndex = getActiveRealmlistIndex();
        if (activeIndex >= 0 && activeIndex < static_cast<int>(entries.size())) {
            updateRealmlist(entries[activeIndex].address);
        }
    }

    // Correct the early-v3.0 Legion portal placeholder, if present.
    if (migrateLegionPortal()) {
        dirty = true;
    }

    // Fork Errai: make Errai the selected server once. The realmlist.wtf is
    // rewritten from the active entry at every game launch.
    if (migrateErraiDefault()) {
        dirty = true;
    }

    if (dirty) {
        save();
    }

    // H4: coalesce bursts of setX() calls (e.g. Settings dialog applying many
    // options at once) into a single disk write 200 ms after the last change.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(SylvaniaConstants::kConfigSaveDebounceMs);
    connect(&m_saveTimer, &QTimer::timeout, this, [this]() { save(); });
}

QJsonArray ConfigManager::defaultRealmlistArrayFor(const QString& editionId) const {
    const GameEdition& edition = GameEdition::byId(editionId);
    QJsonArray array;
    array.append(QJsonObject{
        {"name", edition.defaultRealmName},
        {"address", edition.defaultRealmlist},
        {"active", true}
    });
    return array;
}

bool ConfigManager::migrateToEditionConfig() {
    bool migrated = false;

    QJsonObject editions = m_config.value("editions").toObject();

    // Move pre-3.0 top-level keys into editions.wotlk (transparent upgrade).
    if (!m_config.contains("editions") &&
        (m_config.contains("wow_path") || m_config.contains("realmlist_entries"))) {
        QJsonObject wotlk;
        wotlk["wow_path"] = m_config.value("wow_path").toString();
        wotlk["realmlist_entries"] = m_config.value("realmlist_entries").toArray();
        wotlk["active_realmlist_index"] = m_config.value("active_realmlist_index").toInt(0);
        editions["wotlk"] = wotlk;
        m_config.remove("wow_path");
        m_config.remove("realmlist_entries");
        m_config.remove("active_realmlist_index");
        migrated = true;
        qDebug() << "Config migrée vers le schéma par édition (v3.0)";
    }

    // Make sure every known edition has a usable config object.
    for (const QString& id : GameEdition::knownIds()) {
        QJsonObject ed = editions.value(id).toObject();
        bool changed = false;
        if (!ed.contains("wow_path")) {
            ed["wow_path"] = QString();
            changed = true;
        }
        if (!ed.value("realmlist_entries").isArray() ||
            ed.value("realmlist_entries").toArray().isEmpty()) {
            ed["realmlist_entries"] = defaultRealmlistArrayFor(id);
            ed["active_realmlist_index"] = 0;
            changed = true;
        }
        if (!ed.contains("active_realmlist_index")) {
            ed["active_realmlist_index"] = 0;
            changed = true;
        }
        if (changed) {
            editions[id] = ed;
            migrated = true;
        }
    }
    // Migrate the pre-3.0 GLOBAL "background" key into the WotLK edition (it was
    // always a 3.3.5 background). Legion keeps its own default.
    if (m_config.contains("background")) {
        QJsonObject wotlk = editions.value("wotlk").toObject();
        if (!wotlk.contains("background")) {
            wotlk["background"] = m_config.value("background").toString();
            editions["wotlk"] = wotlk;
        }
        m_config.remove("background");
        migrated = true;
    }

    m_config["editions"] = editions;

    // Validated active edition (anything unknown collapses to wotlk).
    const QString active = m_config.value("active_edition").toString();
    if (!GameEdition::isKnownId(active)) {
        m_config["active_edition"] = GameEdition::wotlk().id;
        migrated = true;
    }

    return migrated;
}

bool ConfigManager::migrateLegacyRealmlist() {
    // Domains used by previous launcher builds that must be rewritten to
    // the current production address. The match is case-insensitive and
    // substring-based so any historical spelling is caught.
    static const QStringList kLegacyHosts = {
        QStringLiteral("logon.sylvania-wow.com"),
        QStringLiteral("sylvania-wow.com"),
        QStringLiteral("logon.sylvania.com"),
    };
    static const QString kCanonicalAddress =
        QStringLiteral("set realmlist sylvania-servergame.com");

    QJsonObject editions = m_config.value("editions").toObject();
    bool migrated = false;

    for (const QString& editionId : editions.keys()) {
        QJsonObject ed = editions.value(editionId).toObject();
        QJsonArray array = ed.value("realmlist_entries").toArray();
        bool edChanged = false;

        for (int i = 0; i < array.size(); ++i) {
            QJsonObject obj = array[i].toObject();
            const QString address = obj.value("address").toString();
            bool isLegacy = false;
            for (const QString& legacy : kLegacyHosts) {
                if (address.contains(legacy, Qt::CaseInsensitive)) {
                    isLegacy = true;
                    break;
                }
            }
            if (!isLegacy) continue;

            obj["address"] = kCanonicalAddress;
            array[i] = obj;
            edChanged = true;
            qWarning() << "Realmlist legacy migré:" << address << "->" << kCanonicalAddress;
        }

        if (edChanged) {
            ed["realmlist_entries"] = array;
            editions[editionId] = ed;
            migrated = true;
        }
    }

    if (migrated) {
        m_config["editions"] = editions;
    }
    return migrated;
}

bool ConfigManager::migrateLegionPortal() {
    // Scoped to the Legion edition only — WotLK's "set realmlist
    // sylvania-servergame.com" is legitimate and must never be rewritten.
    const QString placeholder = QString::fromUtf8(SylvaniaConstants::kLegionPlaceholderPortal);
    const QString canonical = QString::fromUtf8(SylvaniaConstants::kLegionDefaultPortal);

    QJsonObject editions = m_config.value("editions").toObject();
    if (!editions.contains("legion")) return false;

    QJsonObject legion = editions.value("legion").toObject();
    QJsonArray entries = legion.value("realmlist_entries").toArray();
    bool changed = false;

    for (int i = 0; i < entries.size(); ++i) {
        QJsonObject e = entries[i].toObject();
        if (e.value("address").toString() == placeholder) {
            e["address"] = canonical;
            entries[i] = e;
            changed = true;
            qWarning() << "Portal Legion placeholder migré:" << placeholder << "->" << canonical;
        }
    }

    if (changed) {
        legion["realmlist_entries"] = entries;
        editions["legion"] = legion;
        m_config["editions"] = editions;
    }
    return changed;
}

bool ConfigManager::migrateErraiDefault() {
    if (m_config.value("errai_default_applied").toBool()) return false;

    const QString name = QString::fromUtf8(SylvaniaConstants::kErraiRealmName);
    const QString address = QString::fromUtf8(SylvaniaConstants::kErraiRealmlist);
    const QString host = QString::fromUtf8(SylvaniaConstants::kErraiHost);

    QJsonObject editions = m_config.value("editions").toObject();
    QJsonObject wotlk = editions.value("wotlk").toObject();
    QJsonArray entries = wotlk.value("realmlist_entries").toArray();

    // Reuse an existing Errai entry (the player may have added it by hand).
    int index = -1;
    for (int i = 0; i < entries.size(); ++i) {
        if (hostFromAddress(entries[i].toObject().value("address").toString())
                .compare(host, Qt::CaseInsensitive) == 0) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        entries.prepend(QJsonObject{{"name", name}, {"address", address}, {"active", true}});
        index = 0;
    }
    for (int i = 0; i < entries.size(); ++i) {
        QJsonObject e = entries[i].toObject();
        e["active"] = (i == index);
        entries[i] = e;
    }

    wotlk["realmlist_entries"] = entries;
    wotlk["active_realmlist_index"] = index;
    editions["wotlk"] = wotlk;
    m_config["editions"] = editions;
    m_config["errai_default_applied"] = true;
    qWarning() << "Serveur Errai sélectionné par défaut (entrée" << index << ")";
    return true;
}

ConfigManager::~ConfigManager() {
    // Flush any pending write before tear-down so we never lose the last edit.
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        save();
    }
}

void ConfigManager::scheduleSave() {
    m_saveTimer.start();  // restart restarts the timer if already running
}

void ConfigManager::loadConfig() {
    QString configPath = getConfigPath();
    QFile file(configPath);

    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        file.close();

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(data, &error);

        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            m_config = doc.object();
            qDebug() << "Configuration chargée depuis:" << configPath;
            return;
        } else {
            qWarning() << "Erreur de parsing JSON:" << error.errorString();
        }
    }

    // Create default config if not found or invalid
    createDefaultConfig();
}

void ConfigManager::createDefaultConfig() {
    m_config = QJsonObject{
        {"sound_enabled", true},
        {"download_method", "direct"},
        {"max_upload_rate", 100},
        {"max_download_rate", 0},
        {"first_run", true},
        {"auto_update", true},
        {"language", "fr"},
        {"active_edition", "wotlk"}
    };
    // Per-edition objects are seeded by migrateToEditionConfig() right after.

    save();
    qDebug() << "Configuration par défaut créée";
}

QString ConfigManager::getConfigPath() const {
    return m_configDir + "/config.json";
}

bool ConfigManager::save() {
    QDir dir(m_configDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // C3: atomic write — QSaveFile writes to a temp file and renames on commit(),
    // preventing TOCTOU and partial-write corruption if the process is killed.
    QSaveFile file(getConfigPath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Impossible de sauvegarder la configuration";
        return false;
    }

    file.write(QJsonDocument(m_config).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        qWarning() << "Échec du commit atomique de la configuration";
        return false;
    }
    return true;
}

QVariant ConfigManager::get(const QString& key, const QVariant& defaultValue) const {
    if (m_config.contains(key)) {
        return m_config.value(key).toVariant();
    }
    return defaultValue;
}

void ConfigManager::set(const QString& key, const QVariant& value) {
    m_config[key] = QJsonValue::fromVariant(value);
    scheduleSave();
    emit configChanged(key);
}

// --- Editions --------------------------------------------------------------

QString ConfigManager::activeEditionId() const {
    const QString id = m_config.value("active_edition").toString();
    return GameEdition::isKnownId(id) ? id : GameEdition::wotlk().id;
}

void ConfigManager::setActiveEditionId(const QString& id) {
    if (!GameEdition::isKnownId(id)) {
        qWarning() << "Édition inconnue ignorée:" << id;
        return;
    }
    if (activeEditionId() == id) return;
    m_config["active_edition"] = id;
    scheduleSave();
    emit editionChanged(id);
}

QJsonObject ConfigManager::editionConfig(const QString& editionId) const {
    const QString id = editionId.isEmpty() ? activeEditionId()
                                           : GameEdition::byId(editionId).id;
    return m_config.value("editions").toObject().value(id).toObject();
}

void ConfigManager::setEditionConfig(const QString& editionId, const QJsonObject& obj) {
    const QString id = editionId.isEmpty() ? activeEditionId()
                                           : GameEdition::byId(editionId).id;
    QJsonObject editions = m_config.value("editions").toObject();
    editions[id] = obj;
    m_config["editions"] = editions;
}

QString ConfigManager::getWowPath(const QString& editionId) const {
    return editionConfig(editionId).value("wow_path").toString();
}

void ConfigManager::setWowPath(const QString& path) {
    setWowPathFor(QString(), path);
}

void ConfigManager::setWowPathFor(const QString& editionId, const QString& path) {
    QJsonObject ed = editionConfig(editionId);
    ed["wow_path"] = path;
    setEditionConfig(editionId, ed);
    scheduleSave();
    emit configChanged("wow_path");
}

// --- Global settings ---------------------------------------------------------

bool ConfigManager::isFirstRun() const {
    return m_config.value("first_run").toBool(true);
}

void ConfigManager::setFirstRun(bool firstRun) {
    m_config["first_run"] = firstRun;
    scheduleSave();
}

bool ConfigManager::isSoundEnabled() const {
    return m_config.value("sound_enabled").toBool(true);
}

void ConfigManager::setSoundEnabled(bool enabled) {
    m_config["sound_enabled"] = enabled;
    scheduleSave();
    emit configChanged("sound_enabled");
}

QString ConfigManager::getLanguage() const {
    return get("language", "fr").toString();
}

void ConfigManager::setLanguage(const QString& lang) {
    set("language", lang);
}

QString ConfigManager::getBackground() const {
    // Per-edition: each edition remembers its own last background. Default to
    // the edition's declared default when unset.
    const GameEdition& edition = GameEdition::byId(activeEditionId());
    return editionConfig(QString()).value("background")
        .toString(edition.defaultBackground);
}

void ConfigManager::setBackground(const QString& bgName) {
    QJsonObject ed = editionConfig(QString());
    ed["background"] = bgName;
    setEditionConfig(QString(), ed);
    scheduleSave();
    emit configChanged("background");
}

bool ConfigManager::isRandomBackgroundEnabled() const {
    return get("random_bg_on_launch", true).toBool();
}

void ConfigManager::setRandomBackgroundEnabled(bool enabled) {
    set("random_bg_on_launch", enabled);
}

QByteArray ConfigManager::getWindowGeometry() const {
    return QByteArray::fromBase64(get("window_geometry", "").toString().toLatin1());
}

void ConfigManager::setWindowGeometry(const QByteArray& geometry) {
    set("window_geometry", QString::fromLatin1(geometry.toBase64()));
}

// --- Realmlist (active edition) ----------------------------------------------

std::vector<RealmlistEntry> ConfigManager::getRealmlistEntries() const {
    std::vector<RealmlistEntry> entries;

    QJsonArray array = editionConfig(QString()).value("realmlist_entries").toArray();
    for (const QJsonValue& val : array) {
        QJsonObject obj = val.toObject();
        RealmlistEntry entry;
        entry.name = obj.value("name").toString();
        entry.address = obj.value("address").toString();
        entry.active = obj.value("active").toBool(false);
        entries.push_back(entry);
    }

    return entries;
}

void ConfigManager::setRealmlistEntries(const std::vector<RealmlistEntry>& entries) {
    QJsonArray array;
    for (const auto& entry : entries) {
        QJsonObject obj{
            {"name", entry.name},
            {"address", entry.address},
            {"active", entry.active}
        };
        array.append(obj);
    }
    QJsonObject ed = editionConfig(QString());
    ed["realmlist_entries"] = array;
    setEditionConfig(QString(), ed);
    scheduleSave();
    emit realmlistChanged();
}

void ConfigManager::setRealmlistEntry(int index, const QString& name, const QString& address, bool active) {
    QJsonObject ed = editionConfig(QString());
    QJsonArray array = ed.value("realmlist_entries").toArray();

    QJsonObject obj{
        {"name", name},
        {"address", address},
        {"active", active}
    };
    if (index >= 0 && index < array.size()) {
        array[index] = obj;
    } else if (index == array.size()) {
        array.append(obj);
    }

    ed["realmlist_entries"] = array;
    setEditionConfig(QString(), ed);
    scheduleSave();
    emit realmlistChanged();
}

int ConfigManager::getActiveRealmlistIndex() const {
    return editionConfig(QString()).value("active_realmlist_index").toInt(0);
}

void ConfigManager::setActiveRealmlistIndex(int index) {
    QJsonObject ed = editionConfig(QString());
    ed["active_realmlist_index"] = index;

    // Update active flags in entries
    QJsonArray array = ed.value("realmlist_entries").toArray();
    for (int i = 0; i < array.size(); ++i) {
        QJsonObject obj = array[i].toObject();
        obj["active"] = (i == index);
        array[i] = obj;
    }
    ed["realmlist_entries"] = array;
    setEditionConfig(QString(), ed);

    scheduleSave();
    emit realmlistChanged();
}

bool ConfigManager::switchRealmlist(int index) {
    auto entries = getRealmlistEntries();
    if (index < 0 || index >= static_cast<int>(entries.size())) {
        return false;
    }

    setActiveRealmlistIndex(index);
    return updateRealmlist(entries[index].address);
}

QString ConfigManager::hostFromAddress(const QString& address) {
    QString host = address.trimmed();
    // Strip the classic prefixes and any quotes so both "set realmlist x" and
    // a bare host normalise to just the host.
    static const QRegularExpression prefix(
        QStringLiteral("^set\\s+(realmlist|portal)\\s+"),
        QRegularExpression::CaseInsensitiveOption);
    host.remove(prefix);
    host.remove(QLatin1Char('"'));
    return host.trimmed();
}

bool ConfigManager::updateRealmlist(const QString& realmlistText) {
    QString wowPath = getWowPath();
    if (wowPath.isEmpty()) {
        qWarning() << "Chemin WoW non défini";
        return false;
    }

    const GameEdition& edition = GameEdition::byId(activeEditionId());

    if (edition.id == QLatin1String("legion")) {
        // Legion has no realmlist.wtf: the login server is the SET portal
        // line of WTF/Config.wtf.
        const QString host = hostFromAddress(realmlistText);
        if (host.isEmpty()) {
            qWarning() << "Portal Legion vide, écriture ignorée";
            return false;
        }

        const QString wtfDir = wowPath + "/WTF";
        QDir().mkpath(wtfDir);
        const QString wtfPath = wtfDir + "/Config.wtf";

        QString content;
        QFile in(wtfPath);
        if (in.open(QIODevice::ReadOnly | QIODevice::Text)) {
            content = QString::fromUtf8(in.readAll());
            in.close();
        }

        static const QRegularExpression portalLine(
            QStringLiteral("^SET\\s+portal\\s+.*$"),
            QRegularExpression::MultilineOption);
        const QString newLine = QStringLiteral("SET portal \"%1\"").arg(host);
        if (content.contains(portalLine)) {
            content.replace(portalLine, newLine);
        } else {
            if (!content.isEmpty() && !content.endsWith(QLatin1Char('\n')))
                content += QLatin1Char('\n');
            content += newLine + QLatin1Char('\n');
        }

        QSaveFile out(wtfPath);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            out.write(content.toUtf8());
            if (out.commit()) {
                qDebug() << "Portal Legion mis à jour:" << wtfPath;
                return true;
            }
        }
        return false;
    }

    // WotLK: find and update all realmlist.wtf files
    QStringList realmlistPaths = {
        wowPath + "/realmlist.wtf",
        wowPath + "/Data/frFR/realmlist.wtf",
        wowPath + "/Data/enUS/realmlist.wtf",
        wowPath + "/Data/enGB/realmlist.wtf"
    };

    bool success = false;
    for (const QString& path : realmlistPaths) {
        QFileInfo fileInfo(path);

        // Skip locales that don't exist on disk.
        if (!fileInfo.dir().exists()) {
            continue;
        }

        // C3: atomic write avoids leaving a half-written realmlist.wtf if WoW
        // launches concurrently or the launcher crashes mid-write.
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << realmlistText << "\n";
            stream.flush();
            if (file.commit()) {
                qDebug() << "Realmlist mis à jour:" << path;
                success = true;
            }
        }
    }

    return success;
}
