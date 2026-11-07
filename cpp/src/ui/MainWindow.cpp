#include "MainWindow.h"
#include "NotesWindow.h"
#include "RealmlistWindow.h"
#include "DownloadDialog.h"
#include "ErraiNetwork.h"
#include "SettingsDialog.h"
#include "AddonsWindow.h"
#include "ButtonStyles.h"
#include "WowButton.h"
#include "ConfigManager.h"
#include "SoundManager.h"
#include "NotesManager.h"
#include "ReminderService.h"
#include "GameEdition.h"
#include "BuildInfoPatcher.h"
#include "PathUtils.h"
#include "HdPatchManager.h"
#include "Constants.h"

#include <QApplication>
#include <QPainter>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QCloseEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QScreen>
#include <QGuiApplication>
#include <QSystemTrayIcon>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// Per-background palette: panel chrome + the two button gradients consumed by
// the central ButtonStyles templates. This is theme DATA (dynamic backgrounds
// kept from v2.7); the button *templates* live in ButtonStyles only.
struct Palette {
    QString panelBg, panelBorder, textPrimary, textSecondary;
    QString p1, p2, p3, pBorder;   // primary gradient
    QString s1, s2, s3, sBorder;   // secondary gradient
};

Palette paletteFor(const QString& bgName) {
    Palette pal;
    if (bgName == "Alliance") {
        pal = {"rgba(0, 20, 50, 180)", "#15479a", "#ffffff", "#7ec8e3",
               "#1e5ab8", "#15479a", "#0f326a", "#3a7de4",
               "#3a4a5a", "#2a3a4a", "#1a2a3a", "#5a6a7a"};
    } else if (bgName == "Horde") {
        pal = {"rgba(40, 0, 0, 180)", "#6a0dad", "#e0e0e0", "#ff6b6b",
               "#8b0000", "#660000", "#440000", "#b22222",
               "#4a2d2d", "#3a1d1d", "#2a0d0d", "#6a4d4d"};
    } else if (bgName == "Arbre de Vie") {
        pal = {"rgba(10, 40, 10, 180)", "#2e8b57", "#f0fff0", "#98fb98",
               "#2e8b57", "#228b22", "#006400", "#3cb371",
               "#5a4a2d", "#4a3a1d", "#3a2a0d", "#7a6a4d"};
    } else if (bgName == "Ilidan") {
        pal = {"rgba(20, 0, 30, 180)", "#32cd32", "#e6e6fa", "#7fff00",
               "#32cd32", "#228b22", "#006400", "#7fff00",
               "#3a2d4a", "#2a1d3a", "#1a0d2a", "#5a4d6a"};
    } else if (bgName == "Lich King") {
        pal = {"rgba(0, 10, 20, 180)", "#4682b4", "#f0f8ff", "#87cefa",
               "#4682b4", "#5f9ea0", "#1e90ff", "#87ceeb",
               "#4a5a6a", "#3a4a5a", "#2a3a4a", "#6a7a8a"};
    } else if (bgName == "Ragnaros") {
        pal = {"rgba(30, 10, 0, 180)", "#ff4500", "#ffebcd", "#ff8c00",
               "#ff4500", "#cd5c5c", "#8b0000", "#ff6347",
               "#5a2d1d", "#4a1d0d", "#3a0d00", "#7a4d3d"};
    } else if (bgName == "Taverne") {
        pal = {"rgba(0, 0, 0, 150)", "#5a4a2d", "#ffffff", "#7ec8e3",
               "#4a7c3f", "#3a6a2f", "#2a5a1f", "#5a8c4f",
               "#5a4a3d", "#4a3a2d", "#3a2a1d", "#6a5a4d"};
    } else if (bgName == "Legion") {
        // Fel-green Legion theme (fixed background, edition accent).
        pal = {"rgba(8, 24, 8, 190)", "#2f7a1e", "#eaffea", "#9cf07a",
               "#3c9a26", "#2f7a1e", "#1f5512", "#76e84c",
               "#2d3a2a", "#22301f", "#172414", "#4a6a40"};
    } else { // Azeroth or Default
        pal = {"rgba(230, 210, 170, 200)", "#5a4a2d", "#3a2a1d", "#5a4a2d",
               "#4a7c3f", "#3a6a2f", "#2a5a1f", "#5a8c4f",
               "#5a4a3d", "#4a3a2d", "#3a2a1d", "#6a5a4d"};
    }
    return pal;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_config(std::make_unique<ConfigManager>(this))
    , m_soundManager(std::make_unique<SoundManager>(this))
    , m_notesManager(std::make_unique<NotesManager>(this))
{
    // The window is responsive: content lives in Qt layouts and the window is
    // freely resizable down to a usable minimum. Geometry is restored below.
    setMinimumSize(SylvaniaConstants::kMainWindowMinWidth,
                   SylvaniaConstants::kMainWindowMinHeight);

    // Restore the persisted sound preference: SoundManager starts unmuted, so
    // without this an "off" choice would silently re-enable on each launch.
    m_soundManager->setEnabled(m_config->isSoundEnabled());
    ErraiNetwork::setIgnoreErraiSslErrors(m_config->isErraiSslCheckIgnored());

    setupUi();
    connectSignals();

    // Tray icon: hosts reminder toasts and restores the window on click.
    // Created before applyEdition() so the edition icon lands on it too.
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setToolTip(QStringLiteral("Sylvania Launcher"));
    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            showNormal();
            raise();
            activateWindow();
        }
    });
    m_trayIcon->show();

    // Reminders (Lot 3): poll due notes, fire native notification + sound.
    m_reminderService = std::make_unique<ReminderService>(
        m_notesManager.get(), m_soundManager.get(), m_trayIcon, this);
    m_reminderService->start();

    // Setup play time tracking timer
    m_playTimeTimer = new QTimer(this);
    connect(m_playTimeTimer, &QTimer::timeout, this, &MainWindow::checkWowProcess);
    m_playTimeTimer->start(SylvaniaConstants::kPlayTimeCheckMs);

    loadStats();

    // Initial theme and language setup
    QString currentLang = m_config->getLanguage();
    if (currentLang.isEmpty()) currentLang = "fr";
    changeLanguage(currentLang, true); // true means initial, so we DON'T check for enUS yet

    // Background on launch: if the random toggle is ON, pick a fresh one from
    // the active edition's pool (different from last time); otherwise keep the
    // saved per-edition background. Applied by applyEdition() just below.
    if (m_config->isRandomBackgroundEnabled()) {
        m_config->setBackground(pickRandomBackground(m_config->getBackground()));
    }

    applyEdition();

    // Restore the last window size/position (or center at the default size).
    restoreWindowGeometry();
}

MainWindow::~MainWindow() = default;

const GameEdition& MainWindow::activeEdition() const {
    return GameEdition::byId(m_config->activeEditionId());
}

QString MainWindow::pickRandomBackground(const QString& exclude) const {
    // Pick from the ACTIVE edition's own background pool (WotLK and Legion each
    // have their own set — never mixed).
    const QStringList& backgrounds = activeEdition().backgrounds;
    if (backgrounds.isEmpty()) return QString();
    if (backgrounds.size() == 1) return backgrounds.first();
    QString chosen = exclude;
    while (chosen == exclude) {
        chosen = backgrounds.at(QRandomGenerator::global()->bounded(backgrounds.size()));
    }
    return chosen;
}

// ---------------------------------------------------------------------------
// Edition handling ("2 launchers en 1")
// ---------------------------------------------------------------------------

void MainWindow::applyEdition() {
    const GameEdition& edition = activeEdition();
    const QString assets = PathUtils::getAssetsDir();

    setWindowTitle(edition.windowTitle);

    // Logo (header)
    const QString logoPath = assets + "/" + edition.logoAsset;
    m_logoImage = QPixmap();
    if (QFile::exists(logoPath)) {
        m_logoImage.load(logoPath);
    }
    if (m_logoImage.isNull()) {
        // Fallback to the embedded WotLK logo so the header never goes blank.
        m_logoImage.load(QStringLiteral(":/logo"));
    }
    if (!m_logoImage.isNull() && m_logoLabel) {
        m_logoLabel->setPixmap(m_logoImage.scaledToHeight(
            m_logoLabel->maximumHeight(), Qt::SmoothTransformation));
    }

    // Window + taskbar + tray icon: on Windows the taskbar follows the
    // top-level window icon, so an in-process swap is enough — no restart.
    const QString iconPath = assets + "/" + edition.taskbarIconAsset;
    QIcon icon = QFile::exists(iconPath) ? QIcon(iconPath) : QIcon(QStringLiteral(":/icon"));
    setWindowIcon(icon);
    qApp->setWindowIcon(icon);
    if (m_trayIcon) m_trayIcon->setIcon(icon);
    applyTaskbarIcon(iconPath);

    // Background: each edition has its own dynamic pool. getBackground() is
    // per-edition, so this restores the edition's last-used background; the
    // image + palette are applied by applyTheme.
    applyTheme(m_config->getBackground());

    // HD patch is a 3.3.5-only feature: gated, not removed. Same slot in the
    // nav bar in both modes — controls never move between editions.
    m_hdButton->setVisible(edition.supportsHdPatch);

    retranslateUi();
}

void MainWindow::applyTaskbarIcon(const QString& icoPath) {
#ifdef Q_OS_WIN
    // The pinned taskbar button is bound to the AppUserModelID and Qt's
    // setWindowIcon() doesn't reliably repaint it on an in-process edition
    // switch. Sending WM_SETICON with a freshly loaded HICON forces Windows to
    // refresh the running button's icon. Previous HICONs are destroyed so we
    // don't leak one per switch.
    static HICON s_big = nullptr;
    static HICON s_small = nullptr;

    if (!QFile::exists(icoPath)) return;
    const std::wstring path = QDir::toNativeSeparators(icoPath).toStdWString();

    HICON big = static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON,
                                              0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
    HICON small = static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON),
                                                GetSystemMetrics(SM_CYSMICON),
                                                LR_LOADFROMFILE));
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (big) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
        if (s_big) DestroyIcon(s_big);
        s_big = big;
    }
    if (small) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
        if (s_small) DestroyIcon(s_small);
        s_small = small;
    }
#else
    Q_UNUSED(icoPath);
#endif
}

void MainWindow::onEditionToggleClicked() {
    if (m_downloadInProgress ||
        (m_hdPatchManager && m_hdPatchManager->isInstalling())) {
        QMessageBox::information(this, tr("Changement d'édition"),
            tr("Impossible de changer d'édition pendant une installation."));
        return;
    }

    m_soundManager->play("toggle");

    // Close the running play session against the edition that owns it.
    if (m_wowRunning && m_sessionStartTime.isValid()) {
        m_totalPlayTime += m_sessionStartTime.secsTo(QDateTime::currentDateTime());
        m_wowRunning = false;
        m_sessionStartTime = QDateTime();
    }
    saveStats();  // persist stats of the edition we're leaving

    const QString next = (m_config->activeEditionId() == GameEdition::wotlk().id)
                             ? GameEdition::legion().id
                             : GameEdition::wotlk().id;
    m_config->setActiveEditionId(next);
    m_config->save();

    // Edition-bound child windows reload their data lazily on next open.
    if (m_addonsWindow) {
        m_addonsWindow->close();
        m_addonsWindow->deleteLater();
        m_addonsWindow = nullptr;
    }
    if (m_realmlistWindow) {
        m_realmlistWindow->close();
        m_realmlistWindow->deleteLater();
        m_realmlistWindow = nullptr;
    }
    m_hdPatchManager.reset();

    loadStats();
    applyEdition();  // theme + icon + realmlist + stats + primary state
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

ButtonStyles::ThemeTokens MainWindow::themeTokensFor(const QString& bgName) const {
    const Palette pal = paletteFor(bgName);
    const GameEdition& edition = activeEdition();

    ButtonStyles::ThemeTokens t;
    t.primary1 = pal.p1; t.primary2 = pal.p2; t.primary3 = pal.p3;
    t.primaryBorder = pal.pBorder;
    t.secondary1 = pal.s1; t.secondary2 = pal.s2; t.secondary3 = pal.s3;
    t.secondaryBorder = pal.sBorder;
    t.primaryText = QStringLiteral("#ffffff");
    t.secondaryText = (bgName == QLatin1String("Azeroth")) ? QStringLiteral("#ffffff")
                                                           : edition.accentColor;
    t.accent = edition.accentColor;
    return t;
}

void MainWindow::applyTheme(const QString& bgName) {
    // Load the chosen background image from the ACTIVE edition's pool. Tries
    // .jpg then .png, falling back to the first background of the edition, then
    // to the embedded resource, so the window never paints blank.
    const GameEdition& edition = activeEdition();
    const QString bgRoot = PathUtils::getAssetsDir() + "/" + edition.backgroundDir + "/";
    QString bgPath = bgRoot + bgName + ".jpg";
    if (!QFile::exists(bgPath)) bgPath = bgRoot + bgName + ".png";
    if (!QFile::exists(bgPath) && !edition.backgrounds.isEmpty()) {
        bgPath = bgRoot + edition.backgrounds.first() + ".jpg";
        if (!QFile::exists(bgPath)) bgPath = bgRoot + edition.backgrounds.first() + ".png";
    }
    if (QFile::exists(bgPath)) {
        m_backgroundImage.load(bgPath);
    } else {
        m_backgroundImage.load(QStringLiteral(":/background"));
    }
    update(); // Trigger repaint

    // Palette: Legion forces one fel palette for every background; WotLK picks
    // per background name.
    const QString paletteKey = edition.backgroundPaletteKey.isEmpty()
                                   ? bgName : edition.backgroundPaletteKey;
    const Palette pal = paletteFor(paletteKey);
    const ButtonStyles::ThemeTokens tokens = themeTokensFor(paletteKey);

    // Panels
    const QString panelStyle = QString(
        "QFrame { background-color: %1; border: 2px solid %2; border-radius: 10px; }")
        .arg(pal.panelBg, pal.panelBorder);
    if (m_serverPanel) m_serverPanel->setStyleSheet(panelStyle);
    if (m_statsPanel) m_statsPanel->setStyleSheet(panelStyle);

    // Labels
    const QString statBg = (bgName == QLatin1String("Azeroth"))
                               ? QStringLiteral("rgba(255, 255, 255, 100)")
                               : QStringLiteral("rgba(0, 0, 0, 100)");
    m_serverNameLabel->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: bold; border: none;").arg(pal.textPrimary));
    m_realmlistLabel->setStyleSheet(QString("color: %1; font-size: 12px; border: none;").arg(pal.textSecondary));
    m_statusLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; padding: %2px; }")
                                     .arg(pal.textPrimary).arg(SylvaniaConstants::kSpaceXs));
    const QString statStyle = QString(
        "QLabel { background-color: %1; color: %2; font-size: 13px; padding: %3px; "
        "border: 2px solid %4; border-radius: 8px; }")
        .arg(statBg, pal.textPrimary).arg(SylvaniaConstants::kSpaceSm).arg(pal.panelBorder);
    m_playTimeLabel->setStyleSheet(statStyle);
    m_launchCountLabel->setStyleSheet(statStyle);
    m_lastSessionLabel->setStyleSheet(statStyle);

    const QString titleStyle = QString("color: %1; font-size: 18px; font-weight: bold; border: none;")
                                   .arg(tokens.accent);
    if (m_serverTitleLabel) m_serverTitleLabel->setStyleSheet(titleStyle);
    if (m_statsTitleLabel) m_statsTitleLabel->setStyleSheet(titleStyle);
    if (m_footerLabel) m_footerLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pal.textSecondary));
    if (m_progressLabel) m_progressLabel->setStyleSheet(QString("color: %1; font-size: 11px; border: none;").arg(pal.textSecondary));
    if (m_progressBar) {
        m_progressBar->setStyleSheet(QString(R"(
            QProgressBar { background-color: rgba(0,0,0,120); border: 1px solid %1; border-radius: 5px; }
            QProgressBar::chunk { background-color: %2; border-radius: 4px; }
        )").arg(pal.panelBorder, tokens.accent));
    }

    // Buttons — ONE template per level, applied from the central system.
    ButtonStyles::applyPrimary(m_playButton, tokens);
    for (QPushButton* btn : {m_editionButton, m_hdButton, m_settingsButton,
                             m_notesButton, m_addonsButton, m_serversButton,
                             m_quitButton}) {
        ButtonStyles::applySecondary(btn, tokens);
    }
    ButtonStyles::applyTertiary(m_langButton, tokens);
}

// ---------------------------------------------------------------------------
// Stats (per edition)
// ---------------------------------------------------------------------------

void MainWindow::loadStats() {
    m_launchCount = 0;
    m_totalPlayTime = 0;
    m_lastSession = QDateTime();

    QFile file(PathUtils::getDataDir() + "/stats.json");
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    QJsonObject stats;
    if (root.contains("editions")) {
        stats = root.value("editions").toObject()
                    .value(m_config->activeEditionId()).toObject();
    } else if (m_config->activeEditionId() == GameEdition::wotlk().id) {
        // Pre-3.0 flat stats.json belongs to WotLK (transparent migration:
        // the next save rewrites the nested per-edition format).
        stats = root;
    }

    m_launchCount = stats.value("launch_count").toInt(0);
    m_totalPlayTime = stats.value("total_play_time").toInteger(0);
    m_lastSession = QDateTime::fromString(stats.value("last_session").toString(), Qt::ISODate);
}

void MainWindow::saveStats() {
    const QString statsPath = PathUtils::getDataDir() + "/stats.json";
    QDir().mkpath(PathUtils::getDataDir());

    // Read-modify-write so each edition keeps its own counters.
    QJsonObject root;
    {
        QFile in(statsPath);
        if (in.open(QIODevice::ReadOnly)) {
            root = QJsonDocument::fromJson(in.readAll()).object();
        }
    }
    QJsonObject editions = root.value("editions").toObject();
    if (editions.isEmpty() && root.contains("launch_count")) {
        // Capture pre-3.0 flat values as WotLK before they are dropped.
        QJsonObject legacy;
        legacy["launch_count"] = root.value("launch_count");
        legacy["total_play_time"] = root.value("total_play_time");
        legacy["last_session"] = root.value("last_session");
        editions[GameEdition::wotlk().id] = legacy;
    }

    QJsonObject mine;
    mine["launch_count"] = m_launchCount;
    mine["total_play_time"] = m_totalPlayTime;
    mine["last_session"] = m_lastSession.toString(Qt::ISODate);
    editions[m_config->activeEditionId()] = mine;

    // C3: atomic write, consistent with config.json / notes.json.
    QSaveFile file(statsPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{"editions", editions}}).toJson());
        file.commit();
    }
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void MainWindow::paintEvent(QPaintEvent* event) {
    QMainWindow::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Draw background (logo now lives in the header layout, not painted).
    if (!m_backgroundImage.isNull()) {
        QPixmap scaled = m_backgroundImage.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        int x = (width() - scaled.width()) / 2;
        int y = (height() - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);
    }
}

// ---------------------------------------------------------------------------
// UI construction — pure Qt layouts, spacing from the shared scale, zero
// absolute pixel positioning. Skeleton identical in both editions.
// ---------------------------------------------------------------------------

void MainWindow::setupUi() {
    using namespace SylvaniaConstants;

    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(kSpaceLg, kSpaceMd, kSpaceLg, kSpaceMd);
    mainLayout->setSpacing(kSpaceMd);

    // 1. Header: logo left, edition + language switches right (fixed spots).
    mainLayout->addWidget(createHeader());

    // 2. Info panels.
    QHBoxLayout* panelsLayout = new QHBoxLayout();
    panelsLayout->setSpacing(kSpaceLg);
    panelsLayout->addWidget(createServerPanel(), 1);
    panelsLayout->addWidget(createStatsPanel(), 1);
    mainLayout->addLayout(panelsLayout, 1);

    // 3. Anchored primary action zone (one primary button per screen).
    mainLayout->addWidget(createActionZone());

    // 4. Status line.
    m_statusLabel = new QLabel(tr("Chargement..."), this);
    m_statusLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    mainLayout->addWidget(m_statusLabel);

    // 5. Secondary navigation bar (grouped, consistent placement).
    mainLayout->addWidget(createNavBar());

    // 6. Footer.
    m_footerLabel = new QLabel(this);
    m_footerLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_footerLabel);

    // Logical keyboard order: primary action first, then the nav bar
    // left-to-right, then header controls.
    setTabOrder(m_playButton, m_settingsButton);
    setTabOrder(m_settingsButton, m_addonsButton);
    setTabOrder(m_addonsButton, m_notesButton);
    setTabOrder(m_notesButton, m_serversButton);
    setTabOrder(m_serversButton, m_hdButton);
    setTabOrder(m_hdButton, m_quitButton);
    setTabOrder(m_quitButton, m_editionButton);
    setTabOrder(m_editionButton, m_langButton);
}

QWidget* MainWindow::createHeader() {
    using namespace SylvaniaConstants;

    QWidget* header = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kSpaceMd);

    m_logoLabel = new QLabel(header);
    m_logoLabel->setMaximumHeight(kLogoSizePx - kSpaceLg * 2);  // compact header logo
    m_logoLabel->setStyleSheet("background: transparent; border: none;");
    layout->addWidget(m_logoLabel);

    layout->addStretch();

    // Edition toggle: same recognizable spot in both modes.
    m_editionButton = ButtonStyles::makeSecondary(QString(), header);
    layout->addWidget(m_editionButton);

    m_langButton = ButtonStyles::makeTertiary("Lang: FR", header);
    m_langButton->setToolTip(tr("Changer la langue (FR/EN)"));
    layout->addWidget(m_langButton);

    return header;
}

QWidget* MainWindow::createServerPanel() {
    using namespace SylvaniaConstants;

    m_serverPanel = new QFrame(this);

    QVBoxLayout* layout = new QVBoxLayout(m_serverPanel);
    layout->setContentsMargins(kSpaceLg, kSpaceMd, kSpaceLg, kSpaceMd);
    layout->setSpacing(kSpaceSm);

    m_serverTitleLabel = new QLabel(tr("Serveur"), this);
    m_serverTitleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_serverTitleLabel);

    m_serverNameLabel = new QLabel(this);
    layout->addWidget(m_serverNameLabel);

    m_realmlistLabel = new QLabel(this);
    layout->addWidget(m_realmlistLabel);

    layout->addStretch();

    return m_serverPanel;
}

QWidget* MainWindow::createStatsPanel() {
    using namespace SylvaniaConstants;

    m_statsPanel = new QFrame(this);

    QVBoxLayout* layout = new QVBoxLayout(m_statsPanel);
    layout->setContentsMargins(kSpaceLg, kSpaceMd, kSpaceLg, kSpaceMd);
    layout->setSpacing(kSpaceSm);

    m_statsTitleLabel = new QLabel(tr("Statistiques de Jeu"), this);
    m_statsTitleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statsTitleLabel);

    m_playTimeLabel = new QLabel(tr("Temps de jeu: 0h 0min"), this);
    m_playTimeLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_playTimeLabel);

    m_launchCountLabel = new QLabel(tr("Lancements: 0"), this);
    m_launchCountLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_launchCountLabel);

    m_lastSessionLabel = new QLabel(tr("Dernière session: --"), this);
    m_lastSessionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_lastSessionLabel);

    layout->addStretch();

    return m_statsPanel;
}

QWidget* MainWindow::createActionZone() {
    using namespace SylvaniaConstants;

    QWidget* zone = new QWidget(this);
    zone->setStyleSheet("background: transparent;");
    QVBoxLayout* layout = new QVBoxLayout(zone);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kSpaceSm);

    // The ONE primary button: Installer / Téléchargement / Jouer.
    m_playButton = new WowButton(zone);
    m_playButton->setMinimumHeight(kButtonHeightPrimary);
    m_playButton->setMinimumWidth(kMainWindowMinWidth / 3);
    m_playButton->setFocusPolicy(Qt::StrongFocus);

    QHBoxLayout* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    buttonRow->addWidget(m_playButton);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    // Shared progress row: client download AND HD patch report here.
    m_progressLabel = new QLabel(zone);
    m_progressLabel->setAlignment(Qt::AlignCenter);
    m_progressLabel->hide();
    layout->addWidget(m_progressLabel);

    m_progressBar = new QProgressBar(zone);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(10);
    m_progressBar->hide();
    layout->addWidget(m_progressBar);

    return zone;
}

QWidget* MainWindow::createNavBar() {
    using namespace SylvaniaConstants;

    QWidget* bar = new QWidget(this);
    bar->setStyleSheet("background: transparent;");
    QHBoxLayout* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kSpaceSm);

    m_settingsButton = ButtonStyles::makeSecondary(tr("RÉGLAGES"), bar);
    m_addonsButton = ButtonStyles::makeSecondary(tr("ADDONS"), bar);
    m_notesButton = ButtonStyles::makeSecondary(tr("NOTES"), bar);
    m_serversButton = ButtonStyles::makeSecondary(tr("SERVEURS"), bar);
    m_hdButton = ButtonStyles::makeSecondary(tr("PATCH HD"), bar);  // WotLK only
    m_quitButton = ButtonStyles::makeSecondary(tr("QUITTER"), bar);

    layout->addWidget(m_settingsButton);
    layout->addWidget(m_addonsButton);
    layout->addWidget(m_notesButton);
    layout->addWidget(m_serversButton);
    layout->addWidget(m_hdButton);
    layout->addStretch();
    layout->addWidget(m_quitButton);

    return bar;
}

void MainWindow::connectSignals() {
    connect(m_playButton, &QPushButton::clicked, this, &MainWindow::onPrimaryButtonClicked);
    connect(m_editionButton, &QPushButton::clicked, this, &MainWindow::onEditionToggleClicked);
    connect(m_hdButton, &QPushButton::clicked, this, &MainWindow::onHdButtonClicked);
    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::onSettingsButtonClicked);
    connect(m_notesButton, &QPushButton::clicked, this, &MainWindow::onNotesButtonClicked);
    connect(m_addonsButton, &QPushButton::clicked, this, &MainWindow::onAddonsButtonClicked);
    connect(m_quitButton, &QPushButton::clicked, this, &MainWindow::onQuitButtonClicked);
    connect(m_serversButton, &QPushButton::clicked, this, &MainWindow::onServersButtonClicked);
    connect(m_langButton, &QPushButton::clicked, this, &MainWindow::onLangButtonClicked);
}

// ---------------------------------------------------------------------------
// Server / stats display
// ---------------------------------------------------------------------------

void MainWindow::updateServerInfo() {
    auto entries = m_config->getRealmlistEntries();
    int activeIndex = m_config->getActiveRealmlistIndex();

    if (activeIndex >= 0 && activeIndex < static_cast<int>(entries.size())) {
        m_serverNameLabel->setText(tr("Serveur: %1").arg(entries[activeIndex].name));
        const QString host = ConfigManager::hostFromAddress(entries[activeIndex].address);
        // Legion connects via a "portal"; WotLK via a "realmlist".
        m_realmlistLabel->setText(
            activeEdition().id == QLatin1String("legion")
                ? tr("Portal: %1").arg(host)
                : tr("Realmlist: %1").arg(host));
    }
}

void MainWindow::updateStats() {
    // Format play time
    int hours = m_totalPlayTime / 3600;
    int minutes = (m_totalPlayTime % 3600) / 60;
    m_playTimeLabel->setText(tr("Temps de jeu: %1h %2min").arg(hours).arg(minutes));

    // Launch count
    m_launchCountLabel->setText(tr("Lancements: %1").arg(m_launchCount));

    // Last session
    if (m_lastSession.isValid()) {
        m_lastSessionLabel->setText(tr("Dernière session: ") + m_lastSession.toString(tr("dd/MM/yyyy hh:mm")));
    } else {
        m_lastSessionLabel->setText(tr("Dernière session: --"));
    }
}

// ---------------------------------------------------------------------------
// Primary 3-state button (Lot 2)
// ---------------------------------------------------------------------------

QString MainWindow::clientExePath() const {
    const QString wowPath = m_config->getWowPath();
    if (wowPath.isEmpty()) return QString();
    for (const QString& exe : activeEdition().clientExeCandidates) {
        const QString path = wowPath + "/" + exe;
        if (QFile::exists(path)) return path;
    }
    return QString();
}

void MainWindow::refreshPrimaryState() {
    if (m_downloadInProgress) {
        m_playButton->setState(WowButton::State::Downloading);
        return;
    }

    const GameEdition& edition = activeEdition();
    const QString exePath = clientExePath();

    if (exePath.isEmpty()) {
        m_playButton->setState(WowButton::State::Install);
        m_statusLabel->setText(tr("Client %1 non détecté — cliquez sur INSTALLER.")
                                   .arg(edition.displayName));
        return;
    }

    m_playButton->setState(WowButton::State::Play);

    auto entries = m_config->getRealmlistEntries();
    int activeIndex = m_config->getActiveRealmlistIndex();
    QString serverName = (activeIndex >= 0 && activeIndex < static_cast<int>(entries.size()))
        ? entries[activeIndex].name : "Sylvania";

    m_statusLabel->setText(tr("%1 est installé. Prêt à jouer sur %2!")
                               .arg(edition.displayName, serverName));

    updateStats();
}

void MainWindow::onPrimaryButtonClicked() {
    switch (m_playButton->state()) {
    case WowButton::State::Play:
        m_soundManager->play("play");
        playGame();
        break;
    case WowButton::State::Install:
        m_soundManager->play("button");
        startClientInstall();
        break;
    case WowButton::State::Downloading:
        break;  // unreachable: the button is disabled in this state
    }
}

void MainWindow::playGame() {
    const QString exePath = clientExePath();

    if (exePath.isEmpty()) {
        QMessageBox::warning(this, tr("Erreur"),
            tr("Exécutable du jeu non trouvé!\nVeuillez vérifier le chemin d'installation."));
        refreshPrimaryState();
        return;
    }

    // Update realmlist (or Legion portal) before launching
    auto entries = m_config->getRealmlistEntries();
    int activeIndex = m_config->getActiveRealmlistIndex();
    if (activeIndex >= 0 && activeIndex < static_cast<int>(entries.size())) {
        m_config->updateRealmlist(entries[activeIndex].address);
    }

    // Update stats
    m_launchCount++;
    m_lastSession = QDateTime::currentDateTime();
    saveStats();
    updateStats();

    const QString workDir = QFileInfo(exePath).absolutePath();

    // Legion (CASC 7.3.5): Blizzard's CDN is dead, so repoint the client's
    // .build.info to our mirror before launch — otherwise missing files fail to
    // stream (CASC init failure / crash #132). No-op for WotLK (no .build.info).
    // Non-blocking: a failure is logged/shown but never prevents launching.
    if (activeEdition().id == QLatin1String("legion")) {
        QString cdnErr;
        if (!patchBuildInfoCdn(workDir, &cdnErr)) {
            qWarning() << "Patch CDN .build.info:" << cdnErr;
            m_statusLabel->setText(tr("Attention : CDN du client non reconfiguré (%1).").arg(cdnErr));
        }
    }

    // Launch the game
    bool started = QProcess::startDetached(exePath, {}, workDir);

    if (started) {
        m_statusLabel->setText(tr("World of Warcraft lancé! Bon jeu!"));
        // Get out of the way while the game runs; the launcher restores itself
        // when the game process exits (see handleWowRunningState).
        showMinimized();
    } else {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible de lancer World of Warcraft."));
    }
}

void MainWindow::startClientInstall() {
    const GameEdition& edition = activeEdition();

    QString dir = QFileDialog::getExistingDirectory(this,
        tr("Choisir l'emplacement d'installation — %1").arg(edition.displayName),
        QDir::homePath());

    if (dir.isEmpty()) {
        return;
    }

    m_downloadInProgress = true;
    m_playButton->setState(WowButton::State::Downloading);
    m_statusLabel->setText(tr("Installation de %1 en cours...").arg(edition.displayName));

    // Client servi par Errai : adresse, taille et SHA-256 depuis client.json.
    // Lu AVANT de créer la fenêtre, qui démarre le téléchargement 100 ms après
    // sa création (ensureLoaded fait tourner la boucle d'événements).
    ErraiNetwork::Archive client;
    if (edition.id == QLatin1String("wotlk")) {
        ErraiManifest::instance().ensureLoaded();
        client = ErraiManifest::instance().archive(QStringLiteral("client"), edition.clientDownloadUrl);
    }

    m_downloadDialog = new DownloadDialog(this, dir);
    m_downloadDialog->configureForEdition(edition);
    if (!client.url.isEmpty()) {
        m_downloadDialog->setDownloadUrl(client.url);
        m_downloadDialog->setExpected(client.size, client.sha256);
        m_downloadDialog->setExpectedExtractedSize(client.extracted);
    }
    m_downloadDialog->setLanguage(m_config->getLanguage());

    const auto entries = m_config->getRealmlistEntries();
    const int activeIndex = m_config->getActiveRealmlistIndex();
    if (activeIndex >= 0 && activeIndex < static_cast<int>(entries.size())) {
        m_downloadDialog->setRealmlistAddress(entries[activeIndex].address);
    }

    connect(m_downloadDialog, &DownloadDialog::downloadFinished,
            this, &MainWindow::onDownloadComplete);
    connect(m_downloadDialog, &DownloadDialog::progressChanged,
            m_playButton, &WowButton::setDownloadProgress);

    m_downloadDialog->exec();
    m_downloadDialog->deleteLater();
    m_downloadDialog = nullptr;

    m_downloadInProgress = false;
    refreshPrimaryState();  // INSTALL → DOWNLOADING → PLAY happens here
}

void MainWindow::onDownloadComplete(bool success, const QString& message) {
    if (success) {
        QMessageBox::information(this, tr("Téléchargement terminé"), message);
        if (m_downloadDialog) {
            const QString downloadPath = m_downloadDialog->getDestination();

            // Search recursively for the edition's exe (handles subfolders
            // inside the archive).
            QDirIterator it(downloadPath, activeEdition().clientExeCandidates,
                            QDir::Files, QDirIterator::Subdirectories);
            if (it.hasNext()) {
                const QString foundExe = it.next();
                m_config->setWowPath(QFileInfo(foundExe).absolutePath());
            }
        }
    } else {
        QMessageBox::warning(this, tr("Erreur de téléchargement"), message);
    }
}

// ---------------------------------------------------------------------------
// HD patch (WotLK only, gated by GameEdition::supportsHdPatch)
// ---------------------------------------------------------------------------

void MainWindow::onHdButtonClicked() {
    m_soundManager->play("button");

    if (!activeEdition().supportsHdPatch) {
        return;  // button is hidden in Legion; double safety
    }

    QString wowPath = m_config->getWowPath();
    if (wowPath.isEmpty() || clientExePath().isEmpty()) {
        QMessageBox::warning(this, tr("Patch HD"),
            tr("Vous devez d'abord installer ou configurer l'emplacement du client World of Warcraft "
               "avant d'installer le Patch HD."));
        return;
    }

    if (m_hdPatchManager && m_hdPatchManager->isInstalling()) {
        QMessageBox::information(this, tr("Patch HD"), tr("L'installation du Patch HD est déjà en cours."));
        return;
    }

    // Détection si déjà installé
    if (HdPatchManager::isInstalled(wowPath)) {
        auto result = QMessageBox::question(this, tr("Patch HD Détecté"),
            tr("Le Patch HD semble déjà installé sur votre client.\n\n"
               "Voulez-vous tout de même le réinstaller ou forcer une mise à jour ?"),
            QMessageBox::Yes | QMessageBox::No);

        if (result == QMessageBox::No) {
            return;
        }
    }

    // Initialize manager if needed
    if (!m_hdPatchManager) {
        m_hdPatchManager = std::make_unique<HdPatchManager>(wowPath, this);

        connect(m_hdPatchManager.get(), &HdPatchManager::progressChanged, this, [this](int progress, const QString& status) {
            m_progressBar->show();
            m_progressLabel->show();
            if (progress < 0) {
                m_progressBar->setRange(0, 0); // indeterminate / busy
            } else {
                m_progressBar->setRange(0, 100);
                m_progressBar->setValue(progress);
            }
            m_progressLabel->setText(status);
        });

        connect(m_hdPatchManager.get(), &HdPatchManager::finished, this, [this](bool success, const QString& message) {
            m_progressBar->hide();
            m_progressLabel->hide();

            if (success) {
                QMessageBox::information(this, tr("Patch HD"), tr("Patch HD installé avec succès."));
            } else {
                QMessageBox::warning(this, tr("Patch HD"), tr("Erreur lors de l'installation : ") + message);
            }

            // Re-enable actions
            m_hdButton->setEnabled(true);
            m_editionButton->setEnabled(true);
            refreshPrimaryState();
        });
    }

    // Disable conflicting actions during install
    m_playButton->setEnabled(false);
    m_hdButton->setEnabled(false);
    m_editionButton->setEnabled(false);

    m_progressLabel->setText(tr("Préparation de l'installation..."));
    m_progressLabel->show();
    m_progressBar->setValue(0);
    m_progressBar->show();

    m_hdPatchManager->startInstallation();
}

// ---------------------------------------------------------------------------
// Secondary windows
// ---------------------------------------------------------------------------

void MainWindow::onSettingsButtonClicked() {
    m_soundManager->play("button");

    SettingsDialog* dialog = new SettingsDialog(m_config.get(), m_soundManager.get(), this);
    connect(dialog, &SettingsDialog::settingsChanged, this, &MainWindow::refreshPrimaryState);
    connect(dialog, &SettingsDialog::backgroundChanged, this, [this](const QString& bgName) {
        // Both editions have dynamic backgrounds; apply the chosen one live.
        applyTheme(bgName);
    });
    dialog->exec();
    dialog->deleteLater();
}

void MainWindow::onNotesButtonClicked() {
    m_soundManager->play("button");

    if (!m_notesWindow) {
        m_notesWindow = new NotesWindow(m_notesManager.get(), this);
    }

    m_notesWindow->show();
    m_notesWindow->raise();
    m_notesWindow->activateWindow();
}

void MainWindow::onServersButtonClicked() {
    m_soundManager->play("button");

    if (!m_realmlistWindow) {
        m_realmlistWindow = new RealmlistWindow(m_config.get(), this);
        connect(m_realmlistWindow, &RealmlistWindow::realmlistChanged,
                this, &MainWindow::updateServerInfo);
        connect(m_realmlistWindow, &RealmlistWindow::realmlistChanged,
                this, &MainWindow::refreshPrimaryState);
    }

    m_realmlistWindow->show();
    m_realmlistWindow->raise();
    m_realmlistWindow->activateWindow();
}

void MainWindow::onQuitButtonClicked() {
    m_soundManager->play("button");
    close();
}

void MainWindow::onAddonsButtonClicked() {
    m_soundManager->play("button");

    if (!m_addonsWindow) {
        m_addonsWindow = new AddonsWindow(m_config.get(), this);
    }

    m_addonsWindow->show();
    m_addonsWindow->raise();
    m_addonsWindow->activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Stop tracking if WoW was running
    if (m_wowRunning && m_sessionStartTime.isValid()) {
        qint64 sessionTime = m_sessionStartTime.secsTo(QDateTime::currentDateTime());
        m_totalPlayTime += sessionTime;
    }

    if (m_notesWindow) {
        m_notesWindow->close();
    }
    if (m_addonsWindow) {
        m_addonsWindow->close();
    }
    if (m_realmlistWindow) {
        m_realmlistWindow->close();
    }
    if (m_trayIcon) {
        m_trayIcon->hide();
    }

    // Persist the current window size/position so it is restored next launch.
    m_config->setWindowGeometry(saveGeometry());

    m_config->save();
    saveStats();

    QMainWindow::closeEvent(event);
}

void MainWindow::restoreWindowGeometry() {
    const QByteArray geometry = m_config->getWindowGeometry();
    if (!geometry.isEmpty() && restoreGeometry(geometry)) {
        // A saved geometry may now be off-screen (monitor unplugged / changed
        // resolution); pull it back into a visible area.
        clampToAvailableScreen();
        return;
    }

    // First launch (or invalid saved data): default size, centered.
    resize(SylvaniaConstants::kMainWindowWidth, SylvaniaConstants::kMainWindowHeight);
    QScreen* screen = this->screen() ? this->screen() : QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        move(available.center() - rect().center());
    }
}

void MainWindow::clampToAvailableScreen() {
    QScreen* screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen) screen = this->screen();
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    const QRect available = screen->availableGeometry();

    // Never larger than the available area.
    QSize s = size();
    s.setWidth(qMin(s.width(), available.width()));
    s.setHeight(qMin(s.height(), available.height()));
    if (s != size()) resize(s);

    // Keep the whole frame inside the available area.
    QRect frame = frameGeometry();
    int x = qBound(available.left(), frame.left(), available.right() - frame.width() + 1);
    int y = qBound(available.top(), frame.top(), available.bottom() - frame.height() + 1);
    move(x, y);
}

void MainWindow::checkWowProcess() {
#ifdef Q_OS_WIN
    // Query tasklist ASYNCHRONOUSLY. The previous waitForFinished(3000) ran on
    // the UI thread every 5 s and could freeze the launcher for up to 3 s each
    // tick. Skip this tick if a previous check is still running.
    if (m_wowCheckProcess) return;

    // Capture the local QProcess* in the lambdas instead of the member: both
    // finished() and errorOccurred() may fire for the same process (e.g. a
    // crash), so reading the member after the first handler nulled it would
    // dereference null. Each handler guards on identity to delete exactly once.
    QProcess* proc = new QProcess(this);
    m_wowCheckProcess = proc;
    const QStringList candidates = activeEdition().clientExeCandidates;
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, candidates](int, QProcess::ExitStatus) {
        if (m_wowCheckProcess != proc) return;
        const QString output = QString::fromLocal8Bit(proc->readAllStandardOutput());
        bool running = false;
        for (const QString& exe : candidates) {
            if (output.contains(exe, Qt::CaseInsensitive)) { running = true; break; }
        }
        m_wowCheckProcess = nullptr;
        proc->deleteLater();
        handleWowRunningState(running);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        if (m_wowCheckProcess != proc) return;
        m_wowCheckProcess = nullptr;
        proc->deleteLater();
    });
    // No IMAGENAME filter: the edition may probe several exe names (Wow.exe,
    // Wow-64.exe), and tasklist /FI filters combine as AND, not OR.
    proc->start("tasklist", QStringList() << "/NH");
#endif
}

void MainWindow::handleWowRunningState(bool running) {
    if (running && !m_wowRunning) {
        // WoW just started
        m_wowRunning = true;
        m_sessionStartTime = QDateTime::currentDateTime();
        m_statusLabel->setText(tr("World of Warcraft en cours d'exécution..."));
    }
    else if (!running && m_wowRunning) {
        // WoW just stopped
        m_wowRunning = false;

        if (m_sessionStartTime.isValid()) {
            qint64 sessionTime = m_sessionStartTime.secsTo(QDateTime::currentDateTime());
            m_totalPlayTime += sessionTime;
            saveStats();
            updateStats();

            int mins = sessionTime / 60;
            m_statusLabel->setText(tr("Session terminée! Temps joué: %1 min").arg(mins));
        }

        m_sessionStartTime = QDateTime();

        // Bring the launcher back once the game has closed (mirrors the
        // minimize-on-launch in playGame).
        showNormal();
        raise();
        activateWindow();
    }
    else if (running && m_wowRunning) {
        // Update live play time display
        if (m_sessionStartTime.isValid()) {
            qint64 sessionTime = m_sessionStartTime.secsTo(QDateTime::currentDateTime());
            qint64 totalWithSession = m_totalPlayTime + sessionTime;
            int hours = totalWithSession / 3600;
            int mins = (totalWithSession % 3600) / 60;
            m_playTimeLabel->setText(tr("Temps de jeu: %1h %2min").arg(hours).arg(mins));
        }
    }
}

// ---------------------------------------------------------------------------
// Language
// ---------------------------------------------------------------------------

void MainWindow::onLangButtonClicked() {
    m_soundManager->play("button");
    QString currentLang = m_config->getLanguage();
    QString newLang = (currentLang == "fr") ? "en" : "fr";
    changeLanguage(newLang, false); // false means NOT initial, so we DO check for enUS
}

void MainWindow::changeLanguage(const QString& lang, bool initial) {
    m_config->setLanguage(lang);
    m_config->save();

    // Load Qt translator
    qApp->removeTranslator(&m_translator);
    if (lang == "en") {
        if (m_translator.load("SylvaniaLauncher_en", PathUtils::getAssetsDir() + "/translations")) {
            qApp->installTranslator(&m_translator);
        }
    }

    if (lang == "en") {
        m_langButton->setText("Lang: EN");
    } else {
        m_langButton->setText("Lang: FR");
    }

    retranslateUi();

    // Update Config.wtf locale (3.3.5 key; harmless no-op for Legion files)
    QString wtfPath = m_config->getWowPath() + "/WTF/Config.wtf";
    if (QFile::exists(wtfPath)) {
        QString content;
        QFile wtfFile(wtfPath);
        if (wtfFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            content = QString::fromUtf8(wtfFile.readAll());
            wtfFile.close();
        }
        if (!content.isEmpty()) {
            if (lang == "en") {
                if (content.contains("SET locale")) {
                    content.replace("SET locale \"frFR\"", "SET locale \"enUS\"");
                } else {
                    content += "\nSET locale \"enUS\"\n";
                }
            } else {
                content.replace("SET locale \"enUS\"", "SET locale \"frFR\"");
            }
            // Atomic rewrite: never leave a half-written Config.wtf if the
            // launcher crashes or WoW reads the file mid-write.
            QSaveFile out(wtfPath);
            if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
                out.write(content.toUtf8());
                out.commit();
            }
        }
    }
}

void MainWindow::retranslateUi() {
    const GameEdition& edition = activeEdition();

    m_playButton->retranslate();
    m_hdButton->setText(tr("PATCH HD"));
    m_settingsButton->setText(tr("RÉGLAGES"));
    m_notesButton->setText(tr("NOTES"));
    m_quitButton->setText(tr("QUITTER"));
    m_addonsButton->setText(tr("ADDONS"));
    m_serversButton->setText(tr("SERVEURS"));
    m_editionButton->setText(edition.id == GameEdition::wotlk().id
                                 ? tr("⇄ Passer à Legion")
                                 : tr("⇄ Passer à WotLK"));
    m_editionButton->setToolTip(tr("Basculer entre WoW 3.3.5a et WoW Legion 7.x"));

    if (m_serverTitleLabel) m_serverTitleLabel->setText(tr("Serveur"));
    if (m_statsTitleLabel) m_statsTitleLabel->setText(tr("Statistiques de Jeu"));
    // Note: m_serverNameLabel is updated via updateServerInfo below

    if (m_footerLabel) {
        m_footerLabel->setText(tr("© 2026 Sylvania Launcher v3.0 — %1").arg(edition.displayName));
    }

    refreshPrimaryState();
    updateStats();
    updateServerInfo();
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}
