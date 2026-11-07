#include "NotesWindow.h"
#include "NotesManager.h"
#include "Constants.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QCloseEvent>
#include <QScrollBar>
#include <QGraphicsDropShadowEffect>
#include <QFont>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QTextCharFormat>

NotesWindow::NotesWindow(NotesManager* notesManager, QWidget* parent)
    : QDialog(parent)
    , m_notesManager(notesManager)
{
    setWindowTitle(tr("📝 Notes - WowErraiLauncher"));
    setMinimumSize(1000, 620);
    resize(1100, 680);

    setupUi();
    applyStyle();
    loadNotes();
    refreshCalendarMarks();

    // External changes (reminder fired in background, import...) refresh the view.
    connect(m_notesManager, &NotesManager::notesChanged, this, &NotesWindow::refresh);
}

void NotesWindow::refresh() {
    loadNotes();
    refreshCalendarMarks();
}

void NotesWindow::setupUi() {
    using namespace SylvaniaConstants;

    QHBoxLayout* rootLayout = new QHBoxLayout(this);
    rootLayout->setSpacing(kSpaceMd);
    rootLayout->setContentsMargins(kSpaceMd, kSpaceMd, kSpaceMd, kSpaceMd);

    // ----- Left pane: calendar + day filter (v3.0) -----
    QVBoxLayout* calendarPane = new QVBoxLayout();
    calendarPane->setSpacing(kSpaceSm);

    QLabel* calendarTitle = new QLabel(tr("📅 Calendrier"), this);
    calendarTitle->setStyleSheet("color: #d4af37; font-size: 16px; font-weight: bold; border: none;");
    calendarPane->addWidget(calendarTitle);

    m_calendar = new QCalendarWidget(this);
    m_calendar->setGridVisible(true);
    m_calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    m_calendar->setMaximumWidth(360);
    m_calendar->setStyleSheet(R"(
        QCalendarWidget QWidget { background-color: #242424; color: #e8e8e8; }
        QCalendarWidget QToolButton {
            color: #d4af37; background-color: transparent; font-weight: bold;
        }
        QCalendarWidget QAbstractItemView {
            background-color: #1f1f1f; color: #e8e8e8;
            selection-background-color: #d4af37; selection-color: #1a1a1a;
        }
    )");
    calendarPane->addWidget(m_calendar);

    m_dayInfoLabel = new QLabel(tr("Cliquez sur un jour pour filtrer."), this);
    m_dayInfoLabel->setStyleSheet("color: #888888; font-size: 12px; border: none;");
    m_dayInfoLabel->setWordWrap(true);
    calendarPane->addWidget(m_dayInfoLabel);

    m_showAllButton = new QPushButton(tr("Afficher toutes les notes"), this);
    m_showAllButton->setCursor(Qt::PointingHandCursor);
    m_showAllButton->setMinimumHeight(kButtonHeightSecondary);
    m_showAllButton->setStyleSheet(R"(
        QPushButton {
            background-color: #3a3a3a; color: #d4af37;
            border: 1px solid #5a4a2d; border-radius: 5px; font-weight: bold;
        }
        QPushButton:hover { background-color: #4a4a4a; }
        QPushButton:disabled { color: #777777; }
    )");
    m_showAllButton->setEnabled(false);
    calendarPane->addWidget(m_showAllButton);

    calendarPane->addStretch();
    rootLayout->addLayout(calendarPane);

    // ----- Right pane: header + post-it grid -----
    QVBoxLayout* mainLayout = new QVBoxLayout();
    mainLayout->setSpacing(kSpaceMd);

    // Header with title and add button
    QHBoxLayout* headerLayout = new QHBoxLayout();

    QLabel* titleLabel = new QLabel(tr("📌 Mes Notes"), this);
    titleLabel->setStyleSheet("color: #d4af37; font-size: 24px; font-weight: bold; border: none;");

    m_countLabel = new QLabel(tr("0 notes"), this);
    m_countLabel->setStyleSheet("color: #888888; font-size: 14px; border: none;");

    m_addButton = new QPushButton(tr("+ Nouvelle Note"), this);
    m_addButton->setMinimumSize(150, 45);
    m_addButton->setCursor(Qt::PointingHandCursor);
    m_addButton->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4a7c3f, stop:0.5 #3a6a2f, stop:1 #2a5a1f);
            color: #ffffff;
            border: 2px solid #5a8c4f;
            border-radius: 10px;
            padding: 10px 20px;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a8c4f, stop:0.5 #4a7c3f, stop:1 #3a6a2f);
        }
    )");

    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(m_countLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_addButton);
    mainLayout->addLayout(headerLayout);

    // Scroll area for notes grid
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    m_notesContainer = new QWidget();
    m_notesContainer->setStyleSheet("background: transparent;");
    m_notesGrid = new QGridLayout(m_notesContainer);
    m_notesGrid->setSpacing(kSpaceMd);
    m_notesGrid->setContentsMargins(kSpaceSm, kSpaceSm, kSpaceSm, kSpaceSm);

    m_scrollArea->setWidget(m_notesContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    rootLayout->addLayout(mainLayout, 1);

    // Connect signals
    connect(m_addButton, &QPushButton::clicked, this, &NotesWindow::onAddNote);
    connect(m_calendar, &QCalendarWidget::clicked, this, &NotesWindow::onCalendarDayClicked);
    connect(m_showAllButton, &QPushButton::clicked, this, &NotesWindow::onShowAllClicked);
}

void NotesWindow::applyStyle() {
    setStyleSheet(R"(
        QDialog {
            background-color: #1a1a1a;
        }
    )");
}

void NotesWindow::onCalendarDayClicked(const QDate& date) {
    m_filterDate = date;
    m_showAllButton->setEnabled(true);
    loadNotes();
}

void NotesWindow::onShowAllClicked() {
    m_filterDate = QDate();
    m_showAllButton->setEnabled(false);
    m_dayInfoLabel->setText(tr("Cliquez sur un jour pour filtrer."));
    loadNotes();
}

void NotesWindow::refreshCalendarMarks() {
    // Reset previously decorated dates, then decorate every day carrying a
    // created note and/or a reminder.
    for (const QDate& date : std::as_const(m_markedDates)) {
        m_calendar->setDateTextFormat(date, QTextCharFormat());
    }
    m_markedDates.clear();

    QTextCharFormat createdFormat;
    createdFormat.setFontWeight(QFont::Bold);
    createdFormat.setForeground(QColor("#d4af37"));

    QTextCharFormat reminderFormat;
    reminderFormat.setFontWeight(QFont::Bold);
    reminderFormat.setBackground(QColor("#d4af37"));
    reminderFormat.setForeground(QColor("#1a1a1a"));

    // Created-note days first, reminder days override (stronger highlight).
    const auto allNotes = m_notesManager->getAllNotes();
    for (const Note& note : allNotes) {
        if (note.createdAt.isValid()) {
            const QDate day = note.createdAt.date();
            if (!m_markedDates.contains(day)) {
                m_calendar->setDateTextFormat(day, createdFormat);
                m_markedDates.insert(day);
            }
        }
    }
    for (const Note& note : m_notesManager->notesWithReminders()) {
        const QDate day = note.dueAt.date();
        m_calendar->setDateTextFormat(day, reminderFormat);
        m_markedDates.insert(day);
    }
}

void NotesWindow::loadNotes() {
    // Clear existing post-its
    QLayoutItem* item;
    while ((item = m_notesGrid->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }

    const auto notes = m_filterDate.isValid()
                           ? m_notesManager->notesOnDay(m_filterDate)
                           : m_notesManager->getAllNotes();
    int row = 0, col = 0;
    const int maxCols = 3;

    for (const auto& note : notes) {
        QString color = note.category.isEmpty() ? COLORS[0] : note.category;
        // Validate color or use default
        bool validColor = false;
        for (const char* c : COLORS) {
            if (color == c) { validColor = true; break; }
        }
        if (!validColor) color = COLORS[0];

        createPostIt(note.id, note.title, note.content, color, row, col);

        col++;
        if (col >= maxCols) {
            col = 0;
            row++;
        }
    }

    // Add stretch at bottom
    m_notesGrid->setRowStretch(row + 1, 1);

    if (m_filterDate.isValid()) {
        m_dayInfoLabel->setText(tr("%1 : %2 note(s)")
            .arg(m_filterDate.toString("dd/MM/yyyy"))
            .arg(notes.size()));
        m_countLabel->setText(tr("%1 notes (filtrées)").arg(notes.size()));
    } else {
        m_countLabel->setText(QString(tr("%1 notes")).arg(notes.size()));
    }
}

void NotesWindow::createPostIt(const QString& id, const QString& title,
                               const QString& content, const QString& color, int row, int col) {
    QFrame* postIt = new QFrame(m_notesContainer);
    postIt->setObjectName(id);
    postIt->setFixedSize(260, 240);
    postIt->setCursor(Qt::PointingHandCursor);

    // Calculate text color based on background brightness
    QString textColor = "#333333";

    postIt->setStyleSheet(QString(R"(
        QFrame {
            background-color: %1;
            border-radius: 5px;
            border: none;
        }
        QFrame:hover {
            border: 2px solid #d4af37;
        }
    )").arg(color));

    // Add shadow effect
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(postIt);
    shadow->setBlurRadius(15);
    shadow->setOffset(3, 3);
    shadow->setColor(QColor(0, 0, 0, 100));
    postIt->setGraphicsEffect(shadow);

    QVBoxLayout* layout = new QVBoxLayout(postIt);
    layout->setContentsMargins(SylvaniaConstants::kSpaceMd, SylvaniaConstants::kSpaceSm,
                               SylvaniaConstants::kSpaceMd, SylvaniaConstants::kSpaceSm);
    layout->setSpacing(SylvaniaConstants::kSpaceXs);

    // Title row: title (stretch) + delete button, both in the layout — no
    // absolute positioning so the card reflows with the fixed card size.
    QHBoxLayout* titleRow = new QHBoxLayout();
    titleRow->setSpacing(SylvaniaConstants::kSpaceXs);

    QLabel* titleLabel = new QLabel(title.isEmpty() ? tr("Sans titre") : title, postIt);
    titleLabel->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold; background: transparent; border: none;").arg(textColor));
    titleLabel->setWordWrap(true);
    titleLabel->setMaximumHeight(40);
    titleRow->addWidget(titleLabel, 1);

    // Delete button (small X), top-right via layout alignment.
    QPushButton* deleteBtn = new QPushButton("×", postIt);
    deleteBtn->setFixedSize(25, 25);
    deleteBtn->setCursor(Qt::PointingHandCursor);
    deleteBtn->setToolTip(tr("Supprimer cette note"));
    deleteBtn->setStyleSheet(R"(
        QPushButton {
            background-color: rgba(200, 50, 50, 180);
            color: white;
            border-radius: 12px;
            font-size: 16px;
            font-weight: bold;
            border: none;
        }
        QPushButton:hover {
            background-color: rgba(255, 50, 50, 220);
        }
    )");
    titleRow->addWidget(deleteBtn, 0, Qt::AlignTop);
    layout->addLayout(titleRow);

    // Reminder line (v3.0)
    const auto noteOpt = m_notesManager->getNoteById(id);
    if (noteOpt && noteOpt->hasReminder()) {
        const bool overdue = !noteOpt->reminderDone &&
                             noteOpt->dueAt < QDateTime::currentDateTime();
        QLabel* dueLabel = new QLabel(
            tr("⏰ %1").arg(noteOpt->dueAt.toString("dd/MM/yyyy HH:mm")), postIt);
        dueLabel->setStyleSheet(QString(
            "color: %1; font-size: 10px; font-weight: bold; background: transparent; border: none;")
            .arg(overdue ? QStringLiteral("#b00020") : QStringLiteral("#555555")));
        layout->addWidget(dueLabel);
    }

    // Content preview - show more text
    QString preview = content.left(250);
    if (content.length() > 250) preview += "...";

    QLabel* contentLabel = new QLabel(preview, postIt);
    contentLabel->setStyleSheet(QString("color: %1; font-size: 11px; background: transparent; border: none; line-height: 1.3;").arg(textColor));
    contentLabel->setWordWrap(true);
    contentLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(contentLabel, 1);

    // Bottom row with character count
    QHBoxLayout* bottomLayout = new QHBoxLayout();

    // Character count indicator
    QLabel* countLabel = new QLabel(tr("%1 car.").arg(content.length()), postIt);
    countLabel->setStyleSheet(QString("color: %1; font-size: 9px; background: transparent; border: none;").arg(textColor));
    bottomLayout->addWidget(countLabel);

    bottomLayout->addStretch();

    // Edit button
    QPushButton* editBtn = new QPushButton(tr("✏️ Modifier"), postIt);
    editBtn->setFixedHeight(28);
    editBtn->setCursor(Qt::PointingHandCursor);
    editBtn->setStyleSheet(R"(
        QPushButton {
            background-color: rgba(100, 100, 100, 180);
            color: white;
            border-radius: 5px;
            font-size: 10px;
            padding: 5px 10px;
            border: none;
        }
        QPushButton:hover {
            background-color: rgba(120, 120, 120, 220);
        }
    )");
    bottomLayout->addWidget(editBtn);

    layout->addLayout(bottomLayout);

    // Connect edit button
    connect(editBtn, &QPushButton::clicked, [this, id]() {
        m_editingNoteId = id;
        auto noteOpt = m_notesManager->getNoteById(id);
        if (noteOpt) {
            m_selectedColor = noteOpt->category.isEmpty() ? COLORS[0] : noteOpt->category;
        }
        showEditDialog(id);
    });

    // Connect delete button
    connect(deleteBtn, &QPushButton::clicked, [this, id]() {
        onDeleteNote(id);
    });

    // Make the whole post-it clickable for viewing full content
    postIt->installEventFilter(this);
    postIt->setProperty("noteId", id);

    m_notesGrid->addWidget(postIt, row, col);
}

bool NotesWindow::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        QFrame* frame = qobject_cast<QFrame*>(obj);
        if (frame) {
            QString noteId = frame->property("noteId").toString();
            if (!noteId.isEmpty()) {
                onNoteClicked(noteId);
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}

void NotesWindow::onAddNote() {
    m_editingNoteId.clear();
    m_selectedColor = COLORS[0];
    showEditDialog();
}

void NotesWindow::onNoteClicked(const QString& noteId) {
    auto noteOpt = m_notesManager->getNoteById(noteId);
    if (!noteOpt) return;

    const Note& note = *noteOpt;
    QString color = note.category.isEmpty() ? COLORS[0] : note.category;
    QString textColor = "#333333";

    // Create view dialog
    QDialog* viewDialog = new QDialog(this);
    viewDialog->setWindowTitle("📄 " + note.title);
    viewDialog->setMinimumSize(500, 450);
    viewDialog->setStyleSheet(QString(R"(
        QDialog {
            background-color: %1;
        }
        QLabel {
            color: %2;
            border: none;
        }
        QTextEdit {
            background-color: transparent;
            color: %2;
            border: none;
            font-size: 14px;
        }
    )").arg(color).arg(textColor));

    QVBoxLayout* layout = new QVBoxLayout(viewDialog);
    layout->setSpacing(SylvaniaConstants::kSpaceMd);
    layout->setContentsMargins(SylvaniaConstants::kSpaceLg, SylvaniaConstants::kSpaceLg,
                               SylvaniaConstants::kSpaceLg, SylvaniaConstants::kSpaceLg);

    // Title
    QLabel* titleLabel = new QLabel(note.title, viewDialog);
    titleLabel->setStyleSheet(QString("color: %1; font-size: 22px; font-weight: bold;").arg(textColor));
    titleLabel->setWordWrap(true);
    layout->addWidget(titleLabel);

    // Reminder info (v3.0)
    if (note.hasReminder()) {
        QLabel* dueLabel = new QLabel(
            tr("⏰ Rappel: %1").arg(note.dueAt.toString("dd/MM/yyyy HH:mm")), viewDialog);
        dueLabel->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(textColor));
        layout->addWidget(dueLabel);
    }

    // Separator
    QFrame* separator = new QFrame(viewDialog);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QString("background-color: %1;").arg(textColor));
    separator->setFixedHeight(2);
    layout->addWidget(separator);

    // Content (read-only text edit for scrolling)
    QTextEdit* contentView = new QTextEdit(viewDialog);
    contentView->setPlainText(note.content);
    contentView->setReadOnly(true);
    contentView->setStyleSheet(QString("color: %1; font-size: 14px; background: transparent; border: none;").arg(textColor));
    layout->addWidget(contentView, 1);

    // Info and buttons
    QHBoxLayout* bottomLayout = new QHBoxLayout();

    QLabel* dateLabel = new QLabel(tr("Créée le: %1").arg(note.createdAt.toString("dd/MM/yyyy HH:mm")), viewDialog);
    dateLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(textColor));
    bottomLayout->addWidget(dateLabel);

    bottomLayout->addStretch();

    // Edit button
    QPushButton* editBtnDialog = new QPushButton(tr("✏️ Modifier"), viewDialog);
    editBtnDialog->setMinimumSize(100, 35);
    editBtnDialog->setCursor(Qt::PointingHandCursor);
    editBtnDialog->setStyleSheet(R"(
        QPushButton {
            background-color: rgba(80, 80, 80, 200);
            color: white;
            border-radius: 5px;
            padding: 8px 15px;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: rgba(100, 100, 100, 220);
        }
    )");

    // Close button
    QPushButton* closeBtn = new QPushButton(tr("Fermer"), viewDialog);
    closeBtn->setMinimumSize(80, 35);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(R"(
        QPushButton {
            background-color: rgba(60, 60, 60, 200);
            color: white;
            border-radius: 5px;
            padding: 8px 15px;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: rgba(80, 80, 80, 220);
        }
    )");

    bottomLayout->addWidget(editBtnDialog);
    bottomLayout->addWidget(closeBtn);
    layout->addLayout(bottomLayout);

    // Connect buttons
    connect(closeBtn, &QPushButton::clicked, viewDialog, &QDialog::accept);
    connect(editBtnDialog, &QPushButton::clicked, [this, viewDialog, noteId]() {
        viewDialog->accept();
        m_editingNoteId = noteId;
        auto noteOpt = m_notesManager->getNoteById(noteId);
        if (noteOpt) {
            m_selectedColor = noteOpt->category.isEmpty() ? COLORS[0] : noteOpt->category;
        }
        showEditDialog(noteId);
    });

    viewDialog->exec();
    viewDialog->deleteLater();
}

void NotesWindow::onDeleteNote(const QString& noteId) {
    auto result = QMessageBox::question(this, tr("Supprimer"),
        tr("Supprimer cette note ?"),
        QMessageBox::Yes | QMessageBox::No);

    if (result == QMessageBox::Yes) {
        m_notesManager->deleteNote(noteId);
        refresh();
    }
}

void NotesWindow::showEditDialog(const QString& noteId) {
    m_editDialog = new QDialog(this);
    m_editDialog->setWindowTitle(noteId.isEmpty() ? tr("Nouvelle Note") : tr("Modifier la Note"));
    m_editDialog->setFixedSize(450, 470);
    m_editDialog->setStyleSheet(R"(
        QDialog {
            background-color: #1a1a1a;
        }
        QLabel {
            color: #d4af37;
            font-size: 13px;
            border: none;
        }
        QCheckBox {
            color: #d4af37;
            font-size: 13px;
        }
        QLineEdit {
            background-color: #2a2a2a;
            color: #ffffff;
            border: 1px solid #5a4a2d;
            border-radius: 5px;
            padding: 10px;
            font-size: 14px;
        }
        QTextEdit {
            background-color: #2a2a2a;
            color: #ffffff;
            border: 1px solid #5a4a2d;
            border-radius: 5px;
            padding: 10px;
            font-size: 13px;
        }
        QDateTimeEdit {
            background-color: #2a2a2a;
            color: #ffffff;
            border: 1px solid #5a4a2d;
            border-radius: 5px;
            padding: 6px;
            font-size: 13px;
        }
    )");

    QVBoxLayout* layout = new QVBoxLayout(m_editDialog);
    layout->setSpacing(SylvaniaConstants::kSpaceMd);
    layout->setContentsMargins(SylvaniaConstants::kSpaceLg, SylvaniaConstants::kSpaceLg,
                               SylvaniaConstants::kSpaceLg, SylvaniaConstants::kSpaceLg);

    // Title
    layout->addWidget(new QLabel(tr("Titre:"), m_editDialog));
    m_titleEdit = new QLineEdit(m_editDialog);
    m_titleEdit->setPlaceholderText(tr("Titre de la note..."));
    layout->addWidget(m_titleEdit);

    // Content
    layout->addWidget(new QLabel(tr("Contenu:"), m_editDialog));
    m_contentEdit = new QTextEdit(m_editDialog);
    m_contentEdit->setPlaceholderText(tr("Écrivez votre note ici..."));
    layout->addWidget(m_contentEdit, 1);

    // Reminder (v3.0)
    QHBoxLayout* reminderLayout = new QHBoxLayout();
    m_reminderCheck = new QCheckBox(tr("Rappel:"), m_editDialog);
    m_dueEdit = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600), m_editDialog);
    m_dueEdit->setCalendarPopup(true);
    m_dueEdit->setDisplayFormat("dd/MM/yyyy HH:mm");
    m_dueEdit->setEnabled(false);
    connect(m_reminderCheck, &QCheckBox::toggled, m_dueEdit, &QDateTimeEdit::setEnabled);
    reminderLayout->addWidget(m_reminderCheck);
    reminderLayout->addWidget(m_dueEdit, 1);
    layout->addLayout(reminderLayout);

    // Color selection
    layout->addWidget(new QLabel(tr("Couleur:"), m_editDialog));
    QHBoxLayout* colorLayout = new QHBoxLayout();
    colorLayout->setSpacing(SylvaniaConstants::kSpaceSm);

    m_colorButtons.clear();
    for (const char* color : COLORS) {
        QPushButton* colorBtn = new QPushButton(m_editDialog);
        colorBtn->setFixedSize(35, 35);
        colorBtn->setCursor(Qt::PointingHandCursor);
        colorBtn->setProperty("color", QString(color));

        bool isSelected = (QString(color) == m_selectedColor);
        colorBtn->setStyleSheet(QString(R"(
            QPushButton {
                background-color: %1;
                border-radius: 17px;
                border: %2;
            }
            QPushButton:hover {
                border: 3px solid #d4af37;
            }
        )").arg(color).arg(isSelected ? "3px solid #d4af37" : "2px solid #555555"));

        connect(colorBtn, &QPushButton::clicked, [this, color]() {
            onColorSelected(QString(color));
            // Update all buttons
            for (auto* btn : m_colorButtons) {
                QString btnColor = btn->property("color").toString();
                bool sel = (btnColor == m_selectedColor);
                btn->setStyleSheet(QString(R"(
                    QPushButton {
                        background-color: %1;
                        border-radius: 17px;
                        border: %2;
                    }
                    QPushButton:hover {
                        border: 3px solid #d4af37;
                    }
                )").arg(btnColor).arg(sel ? "3px solid #d4af37" : "2px solid #555555"));
            }
        });

        m_colorButtons.push_back(colorBtn);
        colorLayout->addWidget(colorBtn);
    }
    colorLayout->addStretch();
    layout->addLayout(colorLayout);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    QPushButton* cancelBtn = new QPushButton(tr("Annuler"), m_editDialog);
    cancelBtn->setMinimumSize(100, 40);
    cancelBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #3a3a3a;
            color: #ffffff;
            border: 1px solid #5a5a5a;
            border-radius: 5px;
            padding: 10px 25px;
            font-size: 13px;
        }
        QPushButton:hover {
            background-color: #4a4a4a;
        }
    )");

    QPushButton* saveBtn = new QPushButton(tr("Sauvegarder"), m_editDialog);
    saveBtn->setMinimumSize(120, 40);
    saveBtn->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a4a2d, stop:0.5 #3a2a1d, stop:1 #2a1a0d);
            color: #d4af37;
            border: 1px solid #7a6a4d;
            border-radius: 5px;
            padding: 10px 25px;
            font-weight: bold;
            font-size: 13px;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #6a5a3d, stop:0.5 #4a3a2d, stop:1 #3a2a1d);
        }
    )");

    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    layout->addLayout(buttonLayout);

    // Load existing note data
    if (!noteId.isEmpty()) {
        auto noteOpt = m_notesManager->getNoteById(noteId);
        if (noteOpt) {
            m_titleEdit->setText(noteOpt->title);
            m_contentEdit->setPlainText(noteOpt->content);
            if (noteOpt->hasReminder()) {
                m_reminderCheck->setChecked(true);
                m_dueEdit->setDateTime(noteOpt->dueAt);
            }
        }
    }

    connect(cancelBtn, &QPushButton::clicked, m_editDialog, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &NotesWindow::onSaveNote);

    m_editDialog->exec();
    m_editDialog->deleteLater();
    m_editDialog = nullptr;
}

void NotesWindow::onColorSelected(const QString& color) {
    m_selectedColor = color;
}

void NotesWindow::onSaveNote() {
    QString title = m_titleEdit->text().trimmed();
    QString content = m_contentEdit->toPlainText().trimmed();

    if (title.isEmpty() && content.isEmpty()) {
        QMessageBox::warning(this, tr("Erreur"), tr("La note ne peut pas être vide."));
        return;
    }

    if (title.isEmpty()) {
        title = content.left(30);
        if (content.length() > 30) title += "...";
    }

    const QDateTime dueAt = m_reminderCheck->isChecked() ? m_dueEdit->dateTime()
                                                         : QDateTime();

    if (m_editingNoteId.isEmpty()) {
        // Create new note - use color as category to store it
        m_notesManager->createNote(title, content, m_selectedColor, {}, dueAt);
    } else {
        // Update existing note
        m_notesManager->updateNote(m_editingNoteId, title, content, m_selectedColor);
        // Only touch the reminder when it actually changed, so an unrelated
        // edit never re-arms an already-notified reminder.
        const auto existing = m_notesManager->getNoteById(m_editingNoteId);
        if (existing && existing->dueAt != dueAt) {
            m_notesManager->setNoteDueDate(m_editingNoteId, dueAt);
        }
    }

    if (m_editDialog) {
        m_editDialog->accept();
    }
    refresh();
}

void NotesWindow::onCancelEdit() {
    if (m_editDialog) {
        m_editDialog->reject();
    }
}

void NotesWindow::closeEvent(QCloseEvent* event) {
    QDialog::closeEvent(event);
}
