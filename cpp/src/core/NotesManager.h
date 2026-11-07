#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <vector>
#include <optional>

/**
 * @brief Note data structure
 *
 * v3.0: a note can carry a reminder (dueAt). The JSON schema is backward
 * compatible — pre-3.0 notes simply have no "due_at"/"reminder_done" keys and
 * load with an invalid dueAt (no reminder).
 */
struct Note {
    QString id;
    QString title;
    QString content;
    QString category;
    QStringList tags;
    QDateTime createdAt;
    QDateTime updatedAt;
    bool isFavorite = false;
    bool isArchived = false;
    QDateTime dueAt;            // invalid = no reminder
    bool reminderDone = false;  // true once the reminder was notified

    bool hasReminder() const { return dueAt.isValid(); }

    QJsonObject toJson() const;
    static Note fromJson(const QJsonObject& obj);
};

/**
 * @brief Category data structure
 */
struct NoteCategory {
    QString id;
    QString name;
    QString color;
    
    QJsonObject toJson() const;
    static NoteCategory fromJson(const QJsonObject& obj);
};

/**
 * @brief Notes statistics
 */
struct NotesStats {
    int totalNotes = 0;
    int activeNotes = 0;
    int favoriteNotes = 0;
    int archivedNotes = 0;
    QMap<QString, int> categoryStats;
};

/**
 * @brief Notes manager for local JSON-based note storage
 * 
 * Handles CRUD operations for notes stored in %LOCALAPPDATA%/WowErraiLauncher/data/
 */
class NotesManager : public QObject {
    Q_OBJECT

public:
    explicit NotesManager(QObject* parent = nullptr);
    ~NotesManager() override = default;

    // CRUD operations
    std::optional<QString> createNote(const QString& title, const QString& content,
                                       const QString& category = "general",
                                       const QStringList& tags = {},
                                       const QDateTime& dueAt = QDateTime());
    bool updateNote(const QString& noteId,
                    const QString& title = QString(),
                    const QString& content = QString(),
                    const QString& category = QString(),
                    const QStringList& tags = {});
    bool deleteNote(const QString& noteId);

    // --- Reminders (v3.0) -------------------------------------------------
    // Sets/clears the due date (invalid QDateTime = remove the reminder).
    // Resets reminderDone so a re-scheduled note notifies again.
    bool setNoteDueDate(const QString& noteId, const QDateTime& dueAt);
    bool markReminderDone(const QString& noteId);
    // Notes whose reminder is due (dueAt <= now, not yet notified, not archived).
    std::vector<Note> dueReminders(const QDateTime& now) const;
    // Notes carrying any reminder (for calendar decoration).
    std::vector<Note> notesWithReminders() const;
    // Notes "belonging" to a calendar day: created OR due that day.
    std::vector<Note> notesOnDay(const QDate& day) const;
    
    // Retrieval
    std::optional<Note> getNoteById(const QString& noteId) const;
    std::vector<Note> getAllNotes(bool includeArchived = false) const;
    std::vector<Note> getNotesByCategory(const QString& category, bool includeArchived = false) const;
    std::vector<Note> getFavoriteNotes() const;
    std::vector<Note> searchNotes(const QString& query, bool includeArchived = false) const;
    
    // Status toggles
    bool toggleFavorite(const QString& noteId);
    bool toggleArchive(const QString& noteId);
    
    // Categories
    const std::vector<NoteCategory>& getCategories() const;
    bool addCategory(const QString& id, const QString& name, const QString& color = "#4CAF50");
    
    // Statistics
    NotesStats getStats() const;
    
    // Import/Export
    QString exportNotes(const QString& exportPath = QString());
    bool importNotes(const QString& importPath, bool merge = true);

signals:
    void notesChanged();
    void noteCreated(const QString& noteId);
    void noteUpdated(const QString& noteId);
    void noteDeleted(const QString& noteId);

private:
    void loadNotes();
    bool saveNotes();
    void loadCategories();
    bool saveCategories();
    void createDefaultCategories();
    QString getNotesFilePath() const;
    QString getCategoriesFilePath() const;
    void migrateOldNotes();

    // H2: cached lowercase form, parallel to m_notes, rebuilt on every mutation.
    // Avoids recomputing toLower() for every note × every keystroke during search.
    struct SearchIndexEntry {
        QString titleLower;
        QString contentLower;
        QStringList tagsLower;
    };
    void rebuildSearchIndex();
    void appendSearchIndexFor(const Note& note);

    std::vector<Note> m_notes;
    std::vector<NoteCategory> m_categories;
    std::vector<SearchIndexEntry> m_searchIndex;
    QString m_dataDir;
};
