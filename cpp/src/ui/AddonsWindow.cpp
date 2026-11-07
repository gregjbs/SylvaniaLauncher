#include "AddonsWindow.h"
#include "ConfigManager.h"
#include "AddonManifest.h"
#include "GameEdition.h"
#include "Constants.h"
#include "ErraiNetwork.h"
#include <QCryptographicHash>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QTabWidget>
#include <QListWidget>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QTimer>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QFrame>
#include <QStandardPaths>
#include <QSet>

// ---------------------------------------------------------------------------
// Small style snippets (kept consistent with the launcher's dark/gold theme).
// ---------------------------------------------------------------------------
namespace {

QString primaryButtonStyle() {
    return QStringLiteral(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4a7c3f, stop:0.5 #3a6a2f, stop:1 #2a5a1f);
            color: #ffffff; border: 1px solid #5a8c4f; border-radius: 5px; font-weight: bold;
        }
        QPushButton:hover { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a8c4f, stop:0.5 #4a7c3f, stop:1 #3a6a2f); }
        QPushButton:disabled { background-color: #555555; color: #888888; border: 1px solid #444444; }
    )");
}

QString updateButtonStyle() {
    return QStringLiteral(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2a6dd4, stop:0.5 #1e5ab8, stop:1 #15479a);
            color: #ffffff; border: 1px solid #3a7de4; border-radius: 5px; font-weight: bold;
        }
        QPushButton:hover { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3a7de4, stop:0.5 #2a6dd4, stop:1 #1e5ab8); }
        QPushButton:disabled { background-color: #555555; color: #888888; border: 1px solid #444444; }
    )");
}

QString uninstallButtonStyle() {
    return QStringLiteral(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #8a3a3a, stop:0.5 #6a2a2a, stop:1 #4a1a1a);
            color: #f0c0c0; border: 1px solid #9a4a4a; border-radius: 5px; font-weight: bold;
        }
        QPushButton:hover { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #9a4a4a, stop:0.5 #8a3a3a, stop:1 #6a2a2a); color: #ffffff; }
        QPushButton:disabled { background-color: #555555; color: #888888; border: 1px solid #444444; }
    )");
}

QString progressBarStyle() {
    return QStringLiteral(R"(
        QProgressBar { background-color: #1a1a2e; border: none; border-radius: 2px; }
        QProgressBar::chunk { background-color: #d4af37; border-radius: 2px; }
    )");
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
AddonsWindow::AddonsWindow(ConfigManager* config, QWidget* parent)
    : QDialog(parent)
    , m_config(config)
    , m_networkManager(std::make_unique<QNetworkAccessManager>(this))
{
    const GameEdition& edition = GameEdition::byId(m_config->activeEditionId());
    setWindowTitle(tr("Gestionnaire d'Addons - %1").arg(edition.displayName));
    setMinimumSize(600, 640);
    resize(640, 720);
    setStyleSheet("QDialog { background-color: #1a1a2e; }");

    populateCatalog();
    setupUi();

    // Fetch the active edition's recommended manifest asynchronously; the tab
    // shows a loading hint until it arrives (remote or embedded fallback).
    // WotLK and Legion each have their own manifest — never mixed.
    m_manifest = new AddonManifest(edition.addonManifestUrl,
                                   edition.embeddedManifestResource, this);
    connect(m_manifest, &AddonManifest::loaded, this, &AddonsWindow::onManifestLoaded);
    m_manifest->fetch();
}

AddonsWindow::~AddonsWindow() {
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }
    if (!m_activeTempDir.isEmpty()) {
        QDir(m_activeTempDir).removeRecursively();
    }
    for (AddonCard* card : m_cards) delete card;  // the widgets are owned by Qt
    m_cards.clear();
}

// ---------------------------------------------------------------------------
// Catalogue data (bundled single-folder quick-install addons)
// ---------------------------------------------------------------------------
void AddonsWindow::populateCatalog() {
    // The bundled quick-install catalogue is 100% WotLK 3.3.5 addons; Legion
    // only uses its own remote manifest (recommended tab).
    if (GameEdition::byId(m_config->activeEditionId()).id != QLatin1String("wotlk")) {
        m_catalog.clear();
        return;
    }
    m_catalog = {
        {"ArkInventory", tr("Gestion d'inventaire avancée avec catégories automatiques."), "ArkInventory.zip", "ArkInventory"},
        {"AtlasLoot", tr("Base de données complète des butins de donjons et raids."), "AtlasLoot.zip", "AtlasLoot"},
        {"Auctionator", tr("Améliore l'interface de l'Hôtel des Ventes."), "Auctionator.zip", "Auctionator"},
        {"Bagnon", tr("Regroupe tous vos sacs en une seule fenêtre épurée."), "Bagnon.zip", "Bagnon"},
        {"Bartender4", tr("Personnalisation totale de vos barres d'actions."), "Bartender4.zip", "Bartender4"},
        {"Deadly Boss Mods (DBM)", tr("Alertes et chronomètres indispensables pour les raids."), "DBM.zip", "DBM-Core"},
        {"GatherMate2", tr("Mémorise l'emplacement des plantes et minerais sur la carte."), "GatherMate2.zip", "GatherMate2"},
        {"GearScore", tr("Évalue le niveau d'équipement global des joueurs."), "GearScore.zip", "GearScore"},
        {"HandyNotes", tr("Ajoute des notes personnalisées et des points d'intérêt."), "HandyNotes.zip", "HandyNotes"},
        {"Healium", tr("Interface de soin avec boutons d'accès rapide aux sorts."), "Healium.zip", "Healium"},
        {"Immersion", tr("Modernise l'affichage des textes de quêtes et dialogues."), "Immersion.zip", "Immersion"},
        {"Mapster", tr("Améliore la visibilité et les fonctions de la carte du monde."), "Mapster.zip", "Mapster"},
        {"NPCScan", tr("Détecte les créatures rares à proximité et vous alerte avec un signal sonore."), "NPCScan.zip", "_NPCScan"},
        {"NPCScan-Overlay", tr("Affiche les trajets et zones d'apparition des créatures rares sur la carte."), "NPCScan-Overlay.zip", "_NPCScan-Overlay"},
        {"Postal", tr("Améliore radicalement la gestion de la boîte aux lettres."), "Postal.zip", "Postal"},
        {"Prat", tr("Personnalisation poussée de la fenêtre de discussion."), "Prat-3.0.zip", "Prat-3.0"},
        {"Quartz", tr("Barres d'incantation modulaires et ultra-précises."), "Quartz.zip", "Quartz"},
        {"QuestHelper", tr("Le guide classique pour optimiser vos itinéraires de quêtes."), "QuestHelper.zip", "QuestHelper"},
        {"Scrap", tr("Vente et réparation automatique chez les marchands."), "Scrap.zip", "Scrap"},
        {"SexyMap", tr("Donne un look moderne et personnalisable à votre mini-carte."), "SexyMap.zip", "SexyMap"},
        {"TomTom", tr("Navigation par coordonnées GPS et flèche directionnelle."), "TomTom.zip", "TomTom"},
        {"WIM", tr("Gère vos chuchotements dans des fenêtres séparées."), "WIM.zip", "WIM"},
        {"WeakAuras", tr("L'outil ultime pour créer vos propres alertes visuelles."), "WeakAuras.zip", "WeakAuras2"},
        {"XPerl", tr("Refonte visuelle des portraits de joueurs (Style Classique)."), "XPerl.zip", "XPerl"},
        {"Details!", tr("Le compteur de combat le plus précis et détaillé."), "details.zip", "Details"},
        {"ElvUI", tr("Une interface utilisateur complète, moderne et minimaliste."), "elvui.zip", "ElvUI"},
        {"Shadowed Unit Frames", tr("Portraits de joueurs épurés et hautement configurables."), "shadowedunitframes.zip", "ShadowedUnitFrames"},
        {"TotalRP3", tr("L'addon de référence pour l'immersion et le jeu de rôle."), "totalRP3.zip", "TotalRP3"},
        {"VuhDo", tr("Interface de raid et de soin extrêmement personnalisable."), "vuhdo.zip", "VuhDo"}
    };
}

// ---------------------------------------------------------------------------
// UI scaffolding
// ---------------------------------------------------------------------------
void AddonsWindow::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    QLabel* titleLabel = new QLabel(tr("Gestionnaire d'Addons"), this);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("color: #d4af37; font-size: 20px; font-weight: bold;");
    mainLayout->addWidget(titleLabel);

    m_tabs = new QTabWidget(this);
    m_tabs->setStyleSheet(R"(
        QTabWidget::pane { border: 1px solid #3a3a5e; border-radius: 5px; background-color: #151525; }
        QTabBar::tab { background: #2a2a4e; color: #aaaaaa; padding: 8px 16px; border-top-left-radius: 5px; border-top-right-radius: 5px; }
        QTabBar::tab:selected { background: #3a3a5e; color: #d4af37; font-weight: bold; }
    )");
    m_tabs->addTab(buildRecommendedTab(), tr("Recommandés"));
    m_tabs->addTab(buildCatalogTab(), tr("Catalogue"));
    m_tabs->addTab(buildInstalledTab(), tr("Installés"));
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 2) refreshInstalledList();
    });
    mainLayout->addWidget(m_tabs, 1);

    QPushButton* closeBtn = new QPushButton(tr("Fermer"), this);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedSize(120, 38);
    closeBtn->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a4a3d, stop:0.5 #4a3a2d, stop:1 #3a2a1d);
            color: #d4af37; border: 1px solid #6a5a4d; border-radius: 5px; font-weight: bold;
        }
        QPushButton:hover { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6a5a4d, stop:0.5 #5a4a3d, stop:1 #4a3a2d); }
    )");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();
    bottomLayout->addWidget(closeBtn);
    bottomLayout->addStretch();
    mainLayout->addLayout(bottomLayout);
}

// Build one install card (recommended or catalogue) and register it.
static QFrame* makeCardFrame(QWidget* parent) {
    QFrame* frame = new QFrame(parent);
    frame->setStyleSheet("QFrame { background-color: #2a2a4e; border-radius: 5px; } QFrame:hover { background-color: #32325c; }");
    return frame;
}

QWidget* AddonsWindow::buildRecommendedTab() {
    QWidget* page = new QWidget(this);
    QVBoxLayout* outer = new QVBoxLayout(page);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(8);

    QLabel* info = new QLabel(tr("Installez en un clic des addons recommandés par Sylvania."), page);
    info->setWordWrap(true);
    info->setAlignment(Qt::AlignCenter);
    info->setStyleSheet("color: #aaaaaa; font-size: 12px;");
    outer->addWidget(info);

    m_recommendedStatus = new QLabel(tr("Chargement de la liste..."), page);
    m_recommendedStatus->setAlignment(Qt::AlignCenter);
    m_recommendedStatus->setStyleSheet("color: #7ec8e3; font-size: 12px;");
    outer->addWidget(m_recommendedStatus);

    QScrollArea* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    QWidget* content = new QWidget(scroll);
    content->setStyleSheet("background: transparent;");
    m_recommendedLayout = new QVBoxLayout(content);
    m_recommendedLayout->setSpacing(10);
    m_recommendedLayout->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    return page;
}

QWidget* AddonsWindow::buildCatalogTab() {
    QWidget* page = new QWidget(this);
    QVBoxLayout* outer = new QVBoxLayout(page);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(8);

    QLabel* info = new QLabel(tr("Sélectionnez les addons que vous souhaitez installer. Ils seront automatiquement placés dans le dossier de votre jeu."), page);
    info->setWordWrap(true);
    info->setAlignment(Qt::AlignCenter);
    info->setStyleSheet("color: #aaaaaa; font-size: 12px;");
    outer->addWidget(info);

    QScrollArea* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    QWidget* content = new QWidget(scroll);
    content->setStyleSheet("background: transparent;");
    m_catalogLayout = new QVBoxLayout(content);
    m_catalogLayout->setSpacing(10);

    populateCatalogCards();

    m_catalogLayout->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    return page;
}

void AddonsWindow::populateCatalogCards() {
    if (!m_catalogLayout) return;

    // Retire les cartes existantes du catalogue (reconstruction après lecture du manifeste).
    for (auto it = m_cards.begin(); it != m_cards.end();) {
        if ((*it)->target.source == QLatin1String("catalog")) { delete *it; it = m_cards.erase(it); }
        else ++it;
    }
    while (QLayoutItem* item = m_catalogLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    for (const AddonInfo& addon : m_catalog) {
        InstallTarget target;
        target.id = addon.id.isEmpty() ? addon.folderName : addon.id;
        target.name = addon.name;
        target.version = QString();
        target.url = !addon.url.isEmpty()
            ? addon.url
            : QString::fromUtf8(SylvaniaConstants::kAddonDownloadEndpoint) + addon.downloadFileName;
        target.folders = addon.folders.isEmpty() ? QStringList{addon.folderName} : addon.folders;
        target.source = "catalog";
        target.sha256 = addon.sha256;

        QFrame* frame = makeCardFrame(m_catalogLayout->parentWidget());
        QHBoxLayout* row = new QHBoxLayout(frame);
        row->setContentsMargins(15, 12, 15, 12);

        QVBoxLayout* textCol = new QVBoxLayout();
        QLabel* nameLabel = new QLabel(addon.name, frame);
        nameLabel->setStyleSheet("color: #ffffff; font-size: 15px; font-weight: bold;");
        QLabel* descLabel = new QLabel(addon.description, frame);
        descLabel->setStyleSheet("color: #aaaaaa; font-size: 12px;");
        descLabel->setWordWrap(true);

        AddonCard* card = new AddonCard();
        card->target = target;
        card->versionLabel = new QLabel(frame);
        card->versionLabel->setStyleSheet("color: #7ec8e3; font-size: 11px;");
        card->progressBar = new QProgressBar(frame);
        card->progressBar->setRange(0, 100);
        card->progressBar->setTextVisible(false);
        card->progressBar->setFixedHeight(5);
        card->progressBar->setStyleSheet(progressBarStyle());
        card->progressBar->hide();
        card->statusLabel = new QLabel(frame);
        card->statusLabel->setStyleSheet("color: #7ec8e3; font-size: 11px;");
        card->statusLabel->hide();

        textCol->addWidget(nameLabel);
        textCol->addWidget(descLabel);
        textCol->addWidget(card->versionLabel);
        textCol->addWidget(card->progressBar);
        textCol->addWidget(card->statusLabel);
        row->addLayout(textCol, 1);

        card->primaryBtn = new QPushButton(frame);
        card->primaryBtn->setCursor(Qt::PointingHandCursor);
        card->primaryBtn->setFixedSize(110, 34);
        card->primaryBtn->setStyleSheet(primaryButtonStyle());
        connect(card->primaryBtn, &QPushButton::clicked, this, [this, card]() {
            startInstall(card->target, card);
        });
        row->addWidget(card->primaryBtn);

        m_catalogLayout->addWidget(frame);
        m_cards.push_back(card);
        refreshCard(target.id);
    }

    m_catalogLayout->addStretch();
}

QWidget* AddonsWindow::buildInstalledTab() {
    QWidget* page = new QWidget(this);
    QVBoxLayout* outer = new QVBoxLayout(page);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(8);

    QLabel* info = new QLabel(tr("Addons présents dans Interface/AddOns. Sélectionnez-en un ou plusieurs (Ctrl/Maj) puis supprimez."), page);
    info->setWordWrap(true);
    info->setStyleSheet("color: #aaaaaa; font-size: 12px;");
    outer->addWidget(info);

    m_installedList = new QListWidget(page);
    m_installedList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_installedList->setStyleSheet(R"(
        QListWidget { background-color: #151525; color: #ffffff; border: 1px solid #3a3a5e; border-radius: 5px; }
        QListWidget::item { padding: 8px; border-bottom: 1px solid #2a2a4e; }
        QListWidget::item:selected { background-color: #3a3a5e; color: #d4af37; }
    )");
    outer->addWidget(m_installedList, 1);

    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* refreshBtn = new QPushButton(tr("Actualiser"), page);
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setFixedHeight(34);
    refreshBtn->setStyleSheet("QPushButton { background-color: #3a3a5e; color: #ffffff; border: 1px solid #5a5a7e; border-radius: 5px; padding: 6px 16px; } QPushButton:hover { background-color: #4a4a6e; }");
    connect(refreshBtn, &QPushButton::clicked, this, &AddonsWindow::refreshInstalledList);

    QPushButton* deleteBtn = new QPushButton(tr("Supprimer la sélection"), page);
    deleteBtn->setCursor(Qt::PointingHandCursor);
    deleteBtn->setFixedHeight(34);
    deleteBtn->setStyleSheet(uninstallButtonStyle());
    connect(deleteBtn, &QPushButton::clicked, this, [this]() {
        const QList<QListWidgetItem*> selected = m_installedList->selectedItems();
        if (selected.isEmpty()) {
            QMessageBox::information(this, tr("Suppression d'addons"),
                                     tr("Veuillez sélectionner au moins un addon à supprimer."));
            return;
        }

        QStringList names;
        for (QListWidgetItem* item : selected) names << item->data(Qt::UserRole + 2).toString();

        const auto answer = QMessageBox::question(this, tr("Confirmer la suppression"),
            tr("Supprimer définitivement %1 addon(s) ?\n\n%2").arg(selected.size()).arg(names.join("\n")),
            QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes) return;

        const QString addonsDir = m_config->getWowPath() + "/Interface/AddOns";
        QStringList failures;
        for (QListWidgetItem* item : selected) {
            const QStringList folders = item->data(Qt::UserRole).toStringList();
            const QString id = item->data(Qt::UserRole + 1).toString();
            for (const QString& folder : folders) {
                QDir d(addonsDir + "/" + folder);
                if (d.exists() && !d.removeRecursively()) {
                    failures << folder;
                }
            }
            if (!id.isEmpty()) m_registry.remove(id);
        }

        refreshInstalledList();
        for (AddonCard* c : m_cards) refreshCard(c->target.id);

        if (!failures.isEmpty()) {
            QMessageBox::warning(this, tr("Suppression partielle"),
                tr("Certains dossiers n'ont pas pu être supprimés (droits insuffisants ou fichiers verrouillés) :\n%1")
                    .arg(failures.join("\n")));
        } else {
            QMessageBox::information(this, tr("Suppression d'addons"),
                                     tr("Addon(s) supprimé(s) avec succès."));
        }
    });

    actions->addWidget(refreshBtn);
    actions->addStretch();
    actions->addWidget(deleteBtn);
    outer->addLayout(actions);

    return page;
}

// ---------------------------------------------------------------------------
// Recommended cards (built once the manifest is available)
// ---------------------------------------------------------------------------
void AddonsWindow::onManifestLoaded(bool fromRemote) {
    populateRecommendedCards();
    if (!m_manifest->catalogue().empty() && !m_activeCard) {
        m_catalog.clear();
        for (const ManifestAddon& a : m_manifest->catalogue()) {
            AddonInfo info;
            info.name = a.name;
            info.description = a.description;
            info.folderName = a.folders.value(0);
            info.url = a.url;
            info.folders = a.folders;
            info.sha256 = a.sha256;
            info.id = a.id;
            m_catalog.push_back(info);
        }
        populateCatalogCards();
    }
    if (m_recommendedStatus) {
        if (m_manifest->addons().empty()) {
            m_recommendedStatus->setText(tr("Aucun addon recommandé disponible."));
            m_recommendedStatus->show();
        } else if (!fromRemote) {
            m_recommendedStatus->setText(tr("Liste hors-ligne (le serveur est injoignable)."));
            m_recommendedStatus->show();
        } else {
            m_recommendedStatus->hide();
        }
    }
}

void AddonsWindow::populateRecommendedCards() {
    if (!m_recommendedLayout) return;

    for (const ManifestAddon& addon : m_manifest->addons()) {
        InstallTarget target;
        target.id = addon.id;
        target.name = addon.name;
        target.version = addon.version;
        target.url = addon.url;
        target.folders = addon.folders;
        target.source = "recommended";
        target.sha256 = addon.sha256;

        QFrame* frame = makeCardFrame(m_recommendedLayout->parentWidget());
        QHBoxLayout* row = new QHBoxLayout(frame);
        row->setContentsMargins(15, 12, 15, 12);

        QVBoxLayout* textCol = new QVBoxLayout();
        QString title = addon.name;
        if (!addon.version.isEmpty()) title += "  v" + addon.version;
        QLabel* nameLabel = new QLabel(title, frame);
        nameLabel->setStyleSheet("color: #ffffff; font-size: 15px; font-weight: bold;");
        QLabel* descLabel = new QLabel(addon.description, frame);
        descLabel->setStyleSheet("color: #aaaaaa; font-size: 12px;");
        descLabel->setWordWrap(true);

        AddonCard* card = new AddonCard();
        card->target = target;
        card->versionLabel = new QLabel(frame);
        card->versionLabel->setStyleSheet("color: #7ec8e3; font-size: 11px;");
        card->progressBar = new QProgressBar(frame);
        card->progressBar->setRange(0, 100);
        card->progressBar->setTextVisible(false);
        card->progressBar->setFixedHeight(5);
        card->progressBar->setStyleSheet(progressBarStyle());
        card->progressBar->hide();
        card->statusLabel = new QLabel(frame);
        card->statusLabel->setStyleSheet("color: #7ec8e3; font-size: 11px;");
        card->statusLabel->hide();

        textCol->addWidget(nameLabel);
        textCol->addWidget(descLabel);
        textCol->addWidget(card->versionLabel);
        textCol->addWidget(card->progressBar);
        textCol->addWidget(card->statusLabel);
        row->addLayout(textCol, 1);

        QVBoxLayout* btnCol = new QVBoxLayout();
        card->primaryBtn = new QPushButton(frame);
        card->primaryBtn->setCursor(Qt::PointingHandCursor);
        card->primaryBtn->setFixedSize(120, 32);
        connect(card->primaryBtn, &QPushButton::clicked, this, [this, card]() {
            startInstall(card->target, card);
        });
        card->uninstallBtn = new QPushButton(tr("Désinstaller"), frame);
        card->uninstallBtn->setCursor(Qt::PointingHandCursor);
        card->uninstallBtn->setFixedSize(120, 30);
        card->uninstallBtn->setStyleSheet(uninstallButtonStyle());
        connect(card->uninstallBtn, &QPushButton::clicked, this, [this, card]() {
            uninstallById(card->target.id);
        });
        btnCol->addWidget(card->primaryBtn);
        btnCol->addWidget(card->uninstallBtn);
        row->addLayout(btnCol);

        // Insert before the trailing stretch.
        m_recommendedLayout->insertWidget(m_recommendedLayout->count() - 1, frame);
        m_cards.push_back(card);
        refreshCard(target.id);
    }
}

// ---------------------------------------------------------------------------
// Card state
// ---------------------------------------------------------------------------
void AddonsWindow::refreshCard(const QString& id) {
    AddonCard* card = nullptr;
    for (AddonCard* c : m_cards) {
        if (c->target.id == id) { card = c; break; }
    }
    if (!card) return;

    const InstallTarget& t = card->target;
    const bool inRegistry = m_registry.contains(t.id);
    const QString installedVer = inRegistry ? m_registry.installedVersion(t.id) : QString();

    // Disk presence (covers manual installs not in the registry).
    const QString addonsDir = m_config->getWowPath() + "/Interface/AddOns";
    bool onDisk = false;
    for (const QString& f : t.folders) {
        if (!f.isEmpty() && QDir(addonsDir + "/" + f).exists()) { onDisk = true; break; }
    }

    if (t.source == "recommended") {
        const bool updateAvail = inRegistry && !t.version.isEmpty()
                                 && !installedVer.isEmpty() && installedVer != t.version;
        if (updateAvail) {
            card->versionLabel->setText(tr("Mise à jour disponible (v%1 → v%2)").arg(installedVer, t.version));
            card->versionLabel->setStyleSheet("color: #d4af37; font-size: 11px; font-weight: bold;");
            card->primaryBtn->setText(tr("Mettre à jour"));
            card->primaryBtn->setStyleSheet(updateButtonStyle());
            card->primaryBtn->show();
            if (card->uninstallBtn) card->uninstallBtn->show();
        } else if (inRegistry || onDisk) {
            const QString shown = !installedVer.isEmpty() ? installedVer : t.version;
            card->versionLabel->setText(shown.isEmpty() ? tr("Installé") : tr("Installé (v%1)").arg(shown));
            card->versionLabel->setStyleSheet("color: #55ff55; font-size: 11px; font-weight: bold;");
            card->primaryBtn->setText(tr("Réinstaller"));
            card->primaryBtn->setStyleSheet(primaryButtonStyle());
            card->primaryBtn->show();
            if (card->uninstallBtn) card->uninstallBtn->show();
        } else {
            card->versionLabel->setText(tr("Non installé"));
            card->versionLabel->setStyleSheet("color: #aaaaaa; font-size: 11px;");
            card->primaryBtn->setText(tr("Installer"));
            card->primaryBtn->setStyleSheet(primaryButtonStyle());
            card->primaryBtn->show();
            if (card->uninstallBtn) card->uninstallBtn->hide();
        }
    } else { // catalogue
        if (inRegistry || onDisk) {
            card->versionLabel->setText(tr("Déjà installé"));
            card->versionLabel->setStyleSheet("color: #55ff55; font-size: 11px;");
            card->primaryBtn->setText(tr("Réinstaller"));
        } else {
            card->versionLabel->setText(QString());
            card->primaryBtn->setText(tr("Installer"));
        }
        card->primaryBtn->setStyleSheet(primaryButtonStyle());
    }

    // Never re-enable the card currently being installed.
    if (card != m_activeCard) {
        card->primaryBtn->setEnabled(true);
        if (card->uninstallBtn) card->uninstallBtn->setEnabled(true);
    }
}

// ---------------------------------------------------------------------------
// Installed-list scanning
// ---------------------------------------------------------------------------
void AddonsWindow::refreshInstalledList() {
    if (!m_installedList) return;
    m_installedList->clear();
    m_registry.reload();

    const QString addonsDir = m_config->getWowPath() + "/Interface/AddOns";
    QDir dir(addonsDir);
    if (m_config->getWowPath().isEmpty() || !dir.exists()) {
        QListWidgetItem* item = new QListWidgetItem(tr("Aucun dossier AddOns trouvé. Configurez WoW dans les réglages."), m_installedList);
        item->setFlags(Qt::NoItemFlags);
        return;
    }

    const QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QSet<QString> covered;

    // 1) Addons installed by the launcher (registry) — grouped, deleted as one.
    for (const InstalledAddon& a : m_registry.all()) {
        QStringList present;
        qint64 size = 0;
        for (const QString& f : a.folders) {
            if (QDir(addonsDir + "/" + f).exists()) {
                present << f;
                size += dirSize(addonsDir + "/" + f);
            }
        }
        if (present.isEmpty()) continue;
        for (const QString& f : present) covered.insert(f);

        QString text = a.name.isEmpty() ? a.id : a.name;
        if (!a.version.isEmpty()) text += "  v" + a.version;
        text += "   (" + formatSize(size) + ")";
        if (present.size() > 1) text += "   [" + present.join(", ") + "]";
        text += "   — " + tr("installé par le launcher");

        QListWidgetItem* item = new QListWidgetItem(text, m_installedList);
        item->setData(Qt::UserRole, present);          // folders to delete
        item->setData(Qt::UserRole + 1, a.id);         // registry id
        item->setData(Qt::UserRole + 2, a.name.isEmpty() ? a.id : a.name);
    }

    // 2) Remaining folders — manual installs, deleted folder by folder.
    for (const QString& sub : subDirs) {
        if (covered.contains(sub)) continue;

        const QString tocPath = addonsDir + "/" + sub + "/" + sub + ".toc";
        QString title = readTocValue(tocPath, "Title");
        const QString version = readTocValue(tocPath, "Version");
        if (title.isEmpty()) title = sub;

        QString text = title;
        if (!version.isEmpty()) text += "  v" + version;
        text += "   (" + formatSize(dirSize(addonsDir + "/" + sub)) + ")";

        QListWidgetItem* item = new QListWidgetItem(text, m_installedList);
        item->setData(Qt::UserRole, QStringList{sub});
        item->setData(Qt::UserRole + 1, QString());
        item->setData(Qt::UserRole + 2, title);
    }

    if (m_installedList->count() == 0) {
        QListWidgetItem* item = new QListWidgetItem(tr("Aucun addon installé."), m_installedList);
        item->setFlags(Qt::NoItemFlags);
    }
}

// ---------------------------------------------------------------------------
// Install pipeline
// ---------------------------------------------------------------------------
bool AddonsWindow::ensureWowReady() {
    const QString wowPath = m_config->getWowPath();
    if (wowPath.isEmpty() || !QFile::exists(wowPath + "/Wow.exe")) {
        QMessageBox::warning(this, tr("Erreur"),
            tr("Veuillez d'abord configurer le chemin d'installation de World of Warcraft dans les réglages."));
        return false;
    }
    QDir addonsDir(wowPath + "/Interface/AddOns");
    if (!addonsDir.exists() && !addonsDir.mkpath(".")) {
        QMessageBox::warning(this, tr("Erreur"),
            tr("Impossible de créer le dossier Interface/AddOns (droits insuffisants ?)."));
        return false;
    }
    return true;
}

bool AddonsWindow::isTargetInstalled(const InstallTarget& target) const {
    if (m_registry.contains(target.id)) return true;
    const QString addonsDir = m_config->getWowPath() + "/Interface/AddOns";
    for (const QString& f : target.folders) {
        if (!f.isEmpty() && QDir(addonsDir + "/" + f).exists()) return true;
    }
    return false;
}

void AddonsWindow::startInstall(const InstallTarget& target, AddonCard* card) {
    if (!ensureWowReady()) return;

    if (m_currentReply || m_activeCard) {
        QMessageBox::information(this, tr("Information"),
            tr("Une installation est déjà en cours. Veuillez patienter."));
        return;
    }

    // Only download from the active edition's trusted addon host (the manifest
    // could be tampered with or an old embedded copy). WotLK addons live on
    // sylvania-servergame.com, Legion's on legendesylvania.com.
    const QString trustedHost = GameEdition::byId(m_config->activeEditionId()).addonDownloadHost;
    const QUrl url(target.url);
    if (url.scheme() != "https" || url.host() != trustedHost) {
        QMessageBox::warning(this, tr("Erreur"), tr("URL de téléchargement de l'addon invalide."));
        return;
    }

    // Sanitised id for temp file/folder names.
    QString safeId = target.id;
    safeId.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    if (safeId.isEmpty()) safeId = "addon";

    m_activeTempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                      + "/SylvaniaLauncher_Addons/" + safeId;
    QDir().mkpath(m_activeTempDir);
    m_activeZipPath = m_activeTempDir + "/" + safeId + ".zip";

    QFile* file = new QFile(m_activeZipPath, this);
    if (!file->open(QIODevice::WriteOnly)) {
        file->deleteLater();
        QDir(m_activeTempDir).removeRecursively();
        m_activeTempDir.clear();
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible de créer le fichier temporaire."));
        return;
    }

    m_activeCard = card;
    card->target = target;  // keep latest
    card->primaryBtn->setEnabled(false);
    if (card->uninstallBtn) card->uninstallBtn->setEnabled(false);
    card->progressBar->setRange(0, 100);
    card->progressBar->setValue(0);
    card->progressBar->show();
    card->statusLabel->setText(tr("Téléchargement..."));
    card->statusLabel->setStyleSheet("color: #7ec8e3; font-size: 11px;");
    card->statusLabel->show();

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    m_currentReply = m_networkManager->get(request);
    ErraiNetwork::watch(m_currentReply);
    m_currentReply->setProperty("file", QVariant::fromValue(file));

    connect(m_currentReply, &QNetworkReply::downloadProgress, this, &AddonsWindow::onDownloadProgress);
    connect(m_currentReply, &QNetworkReply::finished, this, &AddonsWindow::onDownloadFinished);
    connect(m_currentReply, &QNetworkReply::errorOccurred, this, &AddonsWindow::onDownloadError);
    connect(m_currentReply, &QNetworkReply::readyRead, this, [this, file]() {
        if (file && file->isOpen()) file->write(m_currentReply->readAll());
    });
}

void AddonsWindow::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (!m_activeCard) return;
    if (bytesTotal > 0) {
        const int pct = static_cast<int>((bytesReceived * 100) / bytesTotal);
        m_activeCard->progressBar->setValue(pct);
        m_activeCard->statusLabel->setText(tr("Téléchargement... %1%").arg(pct));
    }
}

void AddonsWindow::onDownloadError(QNetworkReply::NetworkError /*error*/) {
    if (!m_currentReply) return;
    QFile* file = m_currentReply->property("file").value<QFile*>();
    if (file) { file->close(); file->remove(); file->deleteLater(); }
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
    finishInstall(false, tr("Erreur réseau lors du téléchargement de l'addon."), m_activeCard);
}

void AddonsWindow::onDownloadFinished() {
    if (!m_currentReply) return;  // error path already handled

    QFile* file = m_currentReply->property("file").value<QFile*>();
    if (file) { file->close(); file->deleteLater(); }

    const bool hadError = m_currentReply->error() != QNetworkReply::NoError;
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
    if (hadError) return;  // handled by onDownloadError

    // Validate the response really is a ZIP before handing it to PowerShell.
    if (!isZipFile(m_activeZipPath)) {
        finishInstall(false, tr("Le fichier téléchargé n'est pas une archive ZIP valide."), m_activeCard);
        return;
    }

    // Fork Errai : empreinte publiée dans le manifeste (archives de quelques Mo).
    if (m_activeCard && !m_activeCard->target.sha256.isEmpty()) {
        QFile zip(m_activeZipPath);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!zip.open(QIODevice::ReadOnly) || !hash.addData(&zip)
            || QString::fromLatin1(hash.result().toHex()) != m_activeCard->target.sha256) {
            finishInstall(false, tr("L'archive de l'addon est corrompue (empreinte SHA-256 différente)."),
                          m_activeCard);
            return;
        }
    }

    extractAndDeploy(m_activeZipPath, m_activeCard);
}

void AddonsWindow::extractAndDeploy(const QString& zipPath, AddonCard* card) {
    if (!card) { finishInstall(false, tr("Erreur interne."), nullptr); return; }

    card->statusLabel->setText(tr("Extraction en cours..."));
    card->progressBar->setRange(0, 0);  // indeterminate

    const QString extractRoot = m_activeTempDir + "/extracted";
    QDir().mkpath(extractRoot);

    QProcess* process = new QProcess(this);
    // Paths flow via env vars (never interpolated) -> no shell-injection surface.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("SYL_SRC", QDir::toNativeSeparators(zipPath));
    env.insert("SYL_DST", QDir::toNativeSeparators(extractRoot));
    process->setProcessEnvironment(env);

    const InstallTarget target = card->target;
    const QString addonsDir = m_config->getWowPath() + "/Interface/AddOns";

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, card, target, addonsDir, extractRoot](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();

        if (status != QProcess::NormalExit || exitCode != 0) {
            finishInstall(false, tr("Erreur lors de l'extraction de l'archive."), card);
            return;
        }

        // Locate the directory that actually holds the addon folders: use a
        // declared folder as a hint (so archives that wrap everything in an
        // extra top folder still work), then deploy EVERY top-level folder it
        // contains. Addon zips put one folder per component at that level, so
        // deploying them all is what installs multi-folder addons completely
        // (DBM ships 13 folders, Details 9, ElvUI 7...). Relying only on the
        // declared list would silently drop components the catalogue doesn't
        // model.
        QString root = extractRoot;
        for (const QString& folder : target.folders) {
            const QString found = findFolderIn(extractRoot, folder);
            if (!found.isEmpty()) { root = QFileInfo(found).absolutePath(); break; }
        }
        QStringList deployed;
        const QStringList topDirs = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& sub : topDirs) {
            if (moveDirInto(root + "/" + sub, addonsDir)) deployed << sub;
        }

        if (deployed.isEmpty()) {
            finishInstall(false, tr("Aucun dossier d'addon n'a été trouvé dans l'archive."), card);
            return;
        }

        InstalledAddon record;
        record.id = target.id;
        record.name = target.name;
        record.version = target.version;
        record.folders = deployed;
        record.source = target.source;
        m_registry.record(record);

        finishInstall(true, tr("Installé avec succès !"), card);
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, card](QProcess::ProcessError) {
        process->deleteLater();
        finishInstall(false, tr("Erreur lors de l'extraction : impossible de lancer PowerShell."), card);
    });

    process->start("powershell", SylvaniaConstants::extractArchiveArgs());
}

void AddonsWindow::finishInstall(bool success, const QString& message, AddonCard* card) {
    // Drop the staging directory (folders were moved out; only the zip/leftovers remain).
    if (!m_activeTempDir.isEmpty()) {
        QDir(m_activeTempDir).removeRecursively();
        m_activeTempDir.clear();
    }
    m_activeZipPath.clear();
    m_activeCard = nullptr;

    if (card) {
        card->progressBar->setRange(0, 100);
        card->progressBar->setValue(success ? 100 : 0);
        card->progressBar->setVisible(false);
        card->statusLabel->setText(message);
        card->statusLabel->setStyleSheet(success ? "color: #55ff55; font-size: 11px;"
                                                  : "color: #ff5555; font-size: 11px;");
        card->statusLabel->show();
        card->primaryBtn->setEnabled(true);
        if (card->uninstallBtn) card->uninstallBtn->setEnabled(true);
        refreshCard(card->target.id);
    }

    refreshInstalledList();
}

void AddonsWindow::uninstallById(const QString& id) {
    AddonCard* card = nullptr;
    for (AddonCard* c : m_cards) {
        if (c->target.id == id) { card = c; break; }
    }
    if (m_activeCard) {
        QMessageBox::information(this, tr("Information"),
            tr("Une installation est déjà en cours. Veuillez patienter."));
        return;
    }

    const QString addonsDir = m_config->getWowPath() + "/Interface/AddOns";

    // Prefer the registry's recorded folders (covers multi-folder addons); fall
    // back to the target's declared folders for manual installs.
    QStringList folders = m_registry.contains(id) ? m_registry.folders(id)
                          : (card ? card->target.folders : QStringList());
    folders.removeAll(QString());
    if (folders.isEmpty()) return;

    const QString name = card ? card->target.name : id;
    const auto answer = QMessageBox::question(this, tr("Confirmer la désinstallation"),
        tr("Désinstaller %1 ?\nDossiers supprimés : %2").arg(name, folders.join(", ")),
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    QStringList failures;
    for (const QString& folder : folders) {
        QDir d(addonsDir + "/" + folder);
        if (d.exists() && !d.removeRecursively()) failures << folder;
    }
    m_registry.remove(id);

    if (card) {
        card->statusLabel->setText(failures.isEmpty() ? tr("Désinstallé.") : tr("Désinstallation partielle."));
        card->statusLabel->setStyleSheet(failures.isEmpty() ? "color: #aaaaaa; font-size: 11px;"
                                                             : "color: #ff5555; font-size: 11px;");
        card->statusLabel->show();
        refreshCard(id);
    }
    refreshInstalledList();

    if (!failures.isEmpty()) {
        QMessageBox::warning(this, tr("Suppression partielle"),
            tr("Certains dossiers n'ont pas pu être supprimés (droits insuffisants ou fichiers verrouillés) :\n%1")
                .arg(failures.join("\n")));
    }
}

// ---------------------------------------------------------------------------
// Filesystem helpers
// ---------------------------------------------------------------------------
bool AddonsWindow::isZipFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray magic = f.read(2);
    f.close();
    // All ZIP variants start with "PK".
    return magic.size() == 2 && magic[0] == 'P' && magic[1] == 'K';
}

QString AddonsWindow::findFolderIn(const QString& root, const QString& folderName, int maxDepth) {
    // Breadth-first, depth-limited search for a directory named folderName.
    QList<QPair<QString, int>> queue;
    queue.append(qMakePair(root, 0));
    while (!queue.isEmpty()) {
        const QString dir = queue.first().first;
        const int depth = queue.first().second;
        queue.removeFirst();

        const QString candidate = dir + "/" + folderName;
        if (QDir(candidate).exists()) return candidate;

        if (depth >= maxDepth) continue;
        const QStringList subs = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& sub : subs) queue.append(qMakePair(dir + "/" + sub, depth + 1));
    }
    return QString();
}

bool AddonsWindow::moveDirInto(const QString& srcDir, const QString& destParent) {
    const QString dest = destParent + "/" + QFileInfo(srcDir).fileName();

    // Replace any existing copy.
    QDir existing(dest);
    if (existing.exists()) existing.removeRecursively();

    // Fast path: rename works when src and dest share a volume.
    if (QDir().rename(srcDir, dest)) return true;

    // Cross-volume fallback (temp dir on a different drive than WoW): copy then drop the source.
    if (copyDirRecursive(srcDir, dest)) {
        QDir(srcDir).removeRecursively();
        return true;
    }
    return false;
}

bool AddonsWindow::copyDirRecursive(const QString& src, const QString& dst) {
    QDir().mkpath(dst);
    const QFileInfoList entries = QDir(src).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo& fi : entries) {
        const QString target = dst + "/" + fi.fileName();
        if (fi.isDir()) {
            if (!copyDirRecursive(fi.absoluteFilePath(), target)) return false;
        } else {
            if (QFile::exists(target)) QFile::remove(target);
            if (!QFile::copy(fi.absoluteFilePath(), target)) return false;
        }
    }
    return true;
}

QString AddonsWindow::readTocValue(const QString& tocPath, const QString& key) {
    QFile f(tocPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    const QString prefix = "## " + key + ":";
    QString value;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine());
        if (line.trimmed().startsWith(prefix, Qt::CaseInsensitive)) {
            value = line.trimmed().mid(prefix.length()).trimmed();
            break;
        }
    }
    f.close();
    // Strip WoW colour codes: |cAARRGGBB ... |r
    value.remove(QRegularExpression("\\|c[0-9a-fA-F]{8}"));
    value.remove(QStringLiteral("|r"));
    return value.trimmed();
}

qint64 AddonsWindow::dirSize(const QString& path) {
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QString AddonsWindow::formatSize(qint64 bytes) const {
    double size = static_cast<double>(bytes);
    int unit = 0;
    while (size >= 1024.0 && unit < 3) { size /= 1024.0; ++unit; }
    QString suffix;
    switch (unit) {
        case 0: suffix = tr("o"); break;
        case 1: suffix = tr("Ko"); break;
        case 2: suffix = tr("Mo"); break;
        default: suffix = tr("Go"); break;
    }
    return QString::number(size, 'f', size < 10 ? 1 : 0) + " " + suffix;
}
