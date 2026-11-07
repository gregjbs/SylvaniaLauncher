#include "ErraiNetwork.h"
#include "SettingsDialog.h"
#include "ConfigManager.h"
#include "GameEdition.h"
#include "Constants.h"
#include "SoundManager.h"
#include "PathUtils.h"
#include "HdPatchManager.h"
#include "DownloadDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <QEvent>
#include <QRandomGenerator>

class WheelEventFilter : public QObject {
public:
    explicit WheelEventFilter(QObject* parent = nullptr) : QObject(parent) {}
protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::Wheel) {
            return true; // Ignore wheel event
        }
        return QObject::eventFilter(obj, event);
    }
};

SettingsDialog::SettingsDialog(ConfigManager* config, SoundManager* soundManager, QWidget* parent)
    : QDialog(parent)
    , m_config(config)
    , m_soundManager(soundManager)
{
    setWindowTitle(tr("Réglages"));
    setModal(true);
    setFixedSize(620, 770);

    // Locales shipped by the HD pack (Windows FS is case-insensitive).
    m_locales = {"frFR", "enUS", "deDE", "esES", "ruRU"};

    // Feature catalogue rebuilt from the technical README (§4/§5/§6):
    // a letter does NOT mean the same feature between root and locale, so each
    // feature explicitly lists the exact files it toggles.
    const QString mop = tr("Création de perso (Mists of Pandaria)");
    const QString sl  = tr("Création de perso (Shadowlands)");
    m_features = {
        //                 label                          root        locale     dxvk  danger default exclusive
        {tr("DXVK 'API Vulkan'"),                  {},          {},        true,  false, false, ""},
        {tr("Personnages HD"),                     {"d"},       {},        false, false, true,  ""},
        {tr("Modèles HD et PNJ"),                  {"f"},       {"f"},     false, true,  true,  ""},
        {tr("Textures du monde HD"),               {"6"},       {},        false, false, true,  ""},
        {tr("Nouvelle eau"),                       {"w"},       {"w"},     false, true,  true,  ""},
        {tr("Textures du ciel"),                   {"x"},       {"x"},     false, true,  true,  ""},
        {tr("Nouveaux sorts"),                     {"s"},       {},        false, true,  true,  ""},
        {tr("Écrans, serveurs, comptes"),          {"c"},       {"c"},     false, false, true,  ""},
        {tr("Écrans de chargement"),               {},          {"q"},     false, true,  true,  ""},
        {tr("Curseur et Fenêtres d'interface"),    {},          {"d"},     false, false, true,  ""},
        {tr("Cartes des donjons"),                 {},          {"m"},     false, true,  true,  ""},
        {tr("Addons (ACP)"),                       {"5"},       {"9"},     false, false, true,  ""},
        {sl,                                       {"k"},       {"k"},     false, true,  true,  mop},
        // --- OFF by default (heavier / "dangerous" / optional) ---
        {tr("Armures HD"),                         {"7"},       {},        false, true,  false, ""},
        {tr("Armes HD"),                           {"8"},       {},        false, false, false, ""},
        {tr("Nouvelles icônes"),                   {"i"},       {},        false, false, false, ""},
        {tr("Nouveaux arbres"),                    {"t"},       {},        false, false, false, ""},
        {tr("Mort-vivant sans os"),                {"u"},       {"u", "a"},false, true,  false, ""},
        {mop,                                      {"j"},       {"j"},     false, true,  false, sl},
        {tr("Cartes des mini-donjons"),            {},          {"n"},     false, true,  false, ""},
        {tr("Sang"),                               {"b"},       {},        false, true,  false, ""},
        // ARAC = All Races All Classes (root patch-a.mpq). Independent of the HD
        // pack; modifies race/class .dbc tables. Distinct from the locale "-a"
        // used by "Mort-vivant sans os" (patch-<loc>-a).
        {tr("ARAC (toutes races / classes)"),      {"a"},       {},        false, true,  false, ""},
    };

    setupUi();
    loadSettings();
}

void SettingsDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // Installation de WoW section
    QLabel* installTitle = new QLabel(tr("Installation de WoW"), this);
    installTitle->setAlignment(Qt::AlignCenter);
    installTitle->setStyleSheet("color: #d4af37; font-size: 14px; font-weight: bold;");
    mainLayout->addWidget(installTitle);

    // Path row
    QHBoxLayout* pathLayout = new QHBoxLayout();
    pathLayout->setSpacing(10);

    QLabel* pathLabel = new QLabel(tr("Dossier d'installation:"), this);
    pathLabel->setStyleSheet("color: #ffffff; font-size: 12px;");
    pathLabel->setMinimumWidth(130);

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setReadOnly(true);
    m_pathEdit->setStyleSheet(R"(
        QLineEdit {
            background-color: #2a2a2a;
            color: #d4af37;
            border: 1px solid #5a4a2d;
            border-radius: 5px;
            padding: 8px;
            font-size: 11px;
        }
    )");

    m_browseButton = new QPushButton(tr("Parcourir"), this);
    m_browseButton->setStyleSheet(R"(
        QPushButton {
            background-color: #3a3a3a;
            color: #ffffff;
            border: 1px solid #5a5a5a;
            border-radius: 5px;
            padding: 8px 15px;
        }
        QPushButton:hover {
            background-color: #4a4a4a;
        }
    )");

    pathLayout->addWidget(pathLabel);
    pathLayout->addWidget(m_pathEdit, 1);
    pathLayout->addWidget(m_browseButton);
    mainLayout->addLayout(pathLayout);

    // Clear cache button
    m_clearCacheButton = new QPushButton(tr("Vider le cache"), this);
    m_clearCacheButton->setStyleSheet(R"(
        QPushButton {
            background-color: #3a3a3a;
            color: #ffffff;
            border: 1px solid #5a5a5a;
            border-radius: 5px;
            padding: 8px;
        }
        QPushButton:hover {
            background-color: #4a4a4a;
        }
    )");
    mainLayout->addWidget(m_clearCacheButton);

    // AddOns section
    QLabel* addonsTitle = new QLabel(tr("AddOns"), this);
    addonsTitle->setAlignment(Qt::AlignCenter);
    addonsTitle->setStyleSheet("color: #d4af37; font-size: 14px; font-weight: bold;");
    mainLayout->addWidget(addonsTitle);

    m_openAddonsButton = new QPushButton(tr("Ouvrir le dossier AddOns"), this);
    m_openAddonsButton->setMinimumHeight(40);
    m_openAddonsButton->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4a6a4a, stop:0.5 #3a5a3a, stop:1 #2a4a2a);
            color: #ffffff;
            border: 1px solid #5a7a5a;
            border-radius: 5px;
            padding: 10px 20px;
            font-weight: bold;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a7a5a, stop:0.5 #4a6a4a, stop:1 #3a5a3a);
        }
    )");
    mainLayout->addWidget(m_openAddonsButton);

    // Download enUS section
    m_downloadEnUsButton = new QPushButton(tr("Télécharger le Pack Anglais (enUS)"), this);
    m_downloadEnUsButton->setMinimumHeight(40);
    m_downloadEnUsButton->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #2a3a4a, stop:0.5 #1e2a3a, stop:1 #151a2a);
            color: #7ec8e3;
            border: 1px solid #3a4a5a;
            border-radius: 5px;
            padding: 10px 20px;
            font-weight: bold;
            font-size: 12px;
            margin-top: 5px;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #3a4a5a, stop:0.5 #2a3a4a, stop:1 #1e2a3a);
        }
    )");
    mainLayout->addWidget(m_downloadEnUsButton);
    // The enUS language pack is a 3.3.5-only asset; hide the button for
    // editions that don't ship it (e.g. Legion), mirroring the HD-patch gating.
    m_downloadEnUsButton->setVisible(
        GameEdition::byId(m_config->activeEditionId()).supportsEnUsPack);

    // Appearance section
    QLabel* appearanceTitle = new QLabel(tr("Apparence"), this);
    appearanceTitle->setAlignment(Qt::AlignCenter);
    appearanceTitle->setStyleSheet("color: #d4af37; font-size: 14px; font-weight: bold; margin-top: 10px;");
    mainLayout->addWidget(appearanceTitle);

    QHBoxLayout* appearanceLayout = new QHBoxLayout();
    QLabel* bgLabel = new QLabel(tr("Arrière-plan:"), this);
    bgLabel->setStyleSheet("color: #ffffff; font-size: 12px;");

    m_backgroundCombo = new QComboBox(this);
    // Populate from the ACTIVE edition's background pool (WotLK and Legion each
    // have their own set). The base name doubles as the display label.
    for (const QString& bg : GameEdition::byId(m_config->activeEditionId()).backgrounds) {
        m_backgroundCombo->addItem(bg, bg);
    }
    m_backgroundCombo->setStyleSheet(R"(
        QComboBox {
            background-color: #2a2a2a;
            color: #ffffff;
            border: 1px solid #5a4a2d;
            border-radius: 4px;
            padding: 5px;
            min-width: 150px;
        }
        QComboBox::drop-down { border: none; }
    )");
    m_backgroundCombo->installEventFilter(new WheelEventFilter(m_backgroundCombo));

    // Random background toggle: ON = a new random background on each launch,
    // OFF = always keep the selected background.
    m_randomBgButton = new QPushButton(this);
    m_randomBgButton->setCheckable(true);
    m_randomBgButton->setCursor(Qt::PointingHandCursor);
    m_randomBgButton->setToolTip(tr(
        "ON : change l'arrière-plan à chaque ouverture du launcher.\n"
        "OFF : garde toujours le même arrière-plan."));
    m_randomBgButton->setStyleSheet(R"(
        QPushButton {
            border-radius: 4px;
            padding: 5px 12px;
            font-weight: bold;
            font-size: 11px;
        }
        QPushButton:checked {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4a7c3f, stop:1 #2a5a1f);
            color: #ffffff;
            border: 1px solid #5a8c4f;
        }
        QPushButton:!checked {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a4a2d, stop:1 #2a1a0d);
            color: #d4af37;
            border: 1px solid #7a6a4d;
        }
    )");

    appearanceLayout->addWidget(bgLabel);
    appearanceLayout->addWidget(m_backgroundCombo);
    appearanceLayout->addWidget(m_randomBgButton);
    appearanceLayout->addStretch();
    mainLayout->addLayout(appearanceLayout);

    // Audio section
    QLabel* audioTitle = new QLabel(tr("Audio"), this);
    audioTitle->setAlignment(Qt::AlignCenter);
    audioTitle->setStyleSheet("color: #d4af37; font-size: 14px; font-weight: bold;");
    mainLayout->addWidget(audioTitle);

    m_soundCheckbox = new QCheckBox(tr("Activer les sons du launcher"), this);
    m_soundCheckbox->setStyleSheet(R"(
        QCheckBox {
            color: #ffffff;
            font-size: 12px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
        }
        QCheckBox::indicator:checked {
            background-color: #d4af37;
            border: 2px solid #d4af37;
            border-radius: 4px;
        }
        QCheckBox::indicator:unchecked {
            background-color: #2a2a2a;
            border: 2px solid #5a4a2d;
            border-radius: 4px;
        }
    )");
    mainLayout->addWidget(m_soundCheckbox);

    // Fork Errai : le portail présente un certificat auto-signé.
    m_erraiSslCheckbox = new QCheckBox(tr("Ignorer le certificat du serveur Errai (auto-signé)"), this);
    m_erraiSslCheckbox->setToolTip(tr(
        "Ne vérifie pas le certificat HTTPS de errai.minotux.com pour les téléchargements "
        "(client, patch HD, pack anglais). Les autres serveurs restent vérifiés."));
    m_erraiSslCheckbox->setStyleSheet(m_soundCheckbox->styleSheet());
    mainLayout->addWidget(m_erraiSslCheckbox);

    // Patch Management Section: only for editions that support the HD patch
    // (WotLK 3.3.5). Legion 7.3.5 already embeds those assets natively, so the
    // whole section — master ON/OFF + per-feature toggles — is hidden there.
    if (GameEdition::byId(m_config->activeEditionId()).supportsHdPatch
        && !m_config->getWowPath().isEmpty()) {
        setupPatchUI(mainLayout);
    }

    mainLayout->addStretch();

    // Buttons row
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_okButton = new QPushButton(tr("OK"), this);
    m_okButton->setMinimumSize(100, 40);
    m_okButton->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a4a2d, stop:0.5 #3a2a1d, stop:1 #2a1a0d);
            color: #d4af37;
            border: 1px solid #7a6a4d;
            border-radius: 5px;
            padding: 10px 30px;
            font-weight: bold;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #6a5a3d, stop:0.5 #4a3a2d, stop:1 #3a2a1d);
        }
    )");

    m_cancelButton = new QPushButton(tr("Annuler"), this);
    m_cancelButton->setMinimumSize(100, 40);
    m_cancelButton->setStyleSheet(R"(
        QPushButton {
            background-color: #3a3a3a;
            color: #ffffff;
            border: 1px solid #5a5a5a;
            border-radius: 5px;
            padding: 10px 25px;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: #4a4a4a;
        }
    )");

    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonLayout);

    // Connect signals
    connect(m_browseButton, &QPushButton::clicked, this, &SettingsDialog::onBrowseClicked);
    connect(m_clearCacheButton, &QPushButton::clicked, this, &SettingsDialog::onClearCacheClicked);
    connect(m_openAddonsButton, &QPushButton::clicked, this, &SettingsDialog::onOpenAddonsClicked);
    connect(m_downloadEnUsButton, &QPushButton::clicked, this, &SettingsDialog::onDownloadEnUsClicked);
    connect(m_randomBgButton, &QPushButton::toggled, this, &SettingsDialog::onRandomBgToggled);
    connect(m_pathEdit, &QLineEdit::textChanged, this, &SettingsDialog::updateButtonsState);
    connect(m_okButton, &QPushButton::clicked, this, &SettingsDialog::onOkClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &SettingsDialog::onCancelClicked);
    connect(m_soundCheckbox, &QCheckBox::clicked, this, [this]() {
        m_soundManager->play("button");
    });
    connect(m_backgroundCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        onBackgroundChanged(m_backgroundCombo->itemData(index).toString());
    });

    // Dialog style
    setStyleSheet("QDialog { background-color: #1a1a1a; }");
}

void SettingsDialog::setupPatchUI(QVBoxLayout* mainLayout) {
    QLabel* patchesTitle = new QLabel(tr("Gestion des Patchs HD"), this);
    patchesTitle->setAlignment(Qt::AlignCenter);
    patchesTitle->setStyleSheet("color: #d4af37; font-size: 14px; font-weight: bold; margin-top: 10px;");
    mainLayout->addWidget(patchesTitle);

    // Master ON/OFF: toggles the whole HD pack at once.
    m_masterToggle = new QPushButton(this);
    m_masterToggle->setCheckable(true);
    m_masterToggle->setCursor(Qt::PointingHandCursor);
    m_masterToggle->setMinimumHeight(40);
    m_masterToggle->setToolTip(tr(
        "ACTIVÉ : active tous les patchs présents.\n"
        "DÉSACTIVÉ : éteint tous les patchs (client d'origine)."));
    m_masterToggle->setStyleSheet(R"(
        QPushButton {
            border-radius: 6px;
            padding: 10px 20px;
            font-weight: bold;
            font-size: 13px;
        }
        QPushButton:checked {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4a7c3f, stop:0.5 #3a6a2f, stop:1 #2a5a1f);
            color: #ffffff;
            border: 2px solid #5a8c4f;
        }
        QPushButton:!checked {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #6a2a2a, stop:0.5 #5a1a1a, stop:1 #4a1010);
            color: #e0c0c0;
            border: 2px solid #8a3a3a;
        }
    )");
    mainLayout->addWidget(m_masterToggle);
    connect(m_masterToggle, &QPushButton::toggled, this, &SettingsDialog::onMasterToggled);

    QLabel* hint = new QLabel(
        tr("Videz le cache après tout changement de patch (certains modifient des tables .dbc)."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #c08a4a; font-size: 10px;");
    mainLayout->addWidget(hint);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: 1px solid #5a4a2d; border-radius: 5px; background: transparent; }");
    scrollArea->setMaximumHeight(300);

    QWidget* scrollContent = new QWidget();
    QGridLayout* gridLayout = new QGridLayout(scrollContent);
    gridLayout->setSpacing(10);
    gridLayout->setColumnStretch(0, 1);
    gridLayout->setColumnStretch(1, 0);

    int row = 0;
    for (const auto& feature : m_features) {
        QLabel* label = new QLabel(feature.label, this);
        label->setStyleSheet("color: #d4af37; font-size: 11px; font-weight: bold;");
        if (feature.dangerous) {
            label->setToolTip(tr("Patch « Dangereux » : il remplace une table .dbc "
                                 "(risque de détection « unlike check » ou de crash selon le serveur)."));
        }

        QPushButton* toggle = createToggleButton();
        m_featureToggles[feature.label] = toggle;

        const QString label_ = feature.label;
        const QString exclusive = feature.exclusiveWith;
        connect(toggle, &QPushButton::toggled, this, [this, label_, exclusive](bool on) {
            if (QPushButton* b = m_featureToggles.value(label_, nullptr)) {
                b->setText(on ? tr("ACTIVÉ") : tr("ÉTEINT"));
            }
            m_soundManager->play("button");
            // Mutually exclusive features (e.g. char-creation MoP vs SL)
            if (on && !exclusive.isEmpty()) {
                if (QPushButton* other = m_featureToggles.value(exclusive, nullptr)) {
                    if (other->isChecked()) setToggleState(other, false);
                }
            }
        });

        gridLayout->addWidget(label, row, 0);
        gridLayout->addWidget(toggle, row, 1);
        row++;
    }

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea);
}

QPushButton* SettingsDialog::createToggleButton() {
    QPushButton* btn = new QPushButton(this);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedWidth(95);
    btn->setStyleSheet(R"(
        QPushButton {
            border-radius: 5px;
            padding: 6px 10px;
            font-weight: bold;
            font-size: 11px;
        }
        QPushButton:checked {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4a7c3f, stop:1 #2a5a1f);
            color: #ffffff;
            border: 1px solid #5a8c4f;
        }
        QPushButton:!checked {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a2a2a, stop:1 #3a1414);
            color: #ddbbbb;
            border: 1px solid #7a3a3a;
        }
        QPushButton:disabled {
            background-color: #2a2a2a;
            color: #666666;
            border: 1px solid #3a3a3a;
        }
    )");
    btn->setText(tr("ÉTEINT"));
    return btn;
}

void SettingsDialog::setToggleState(QPushButton* btn, bool on) {
    if (!btn) return;
    btn->blockSignals(true);
    btn->setChecked(on);
    btn->setText(on ? tr("ACTIVÉ") : tr("ÉTEINT"));
    btn->blockSignals(false);
}

void SettingsDialog::updateMasterButton() {
    if (!m_masterToggle) return;
    m_masterToggle->setText(m_masterToggle->isChecked()
        ? tr("Patch HD : ACTIVÉ")
        : tr("Patch HD : DÉSACTIVÉ"));
}

void SettingsDialog::onMasterToggled(bool enabled) {
    m_soundManager->play("button");
    // ON -> enable every present patch; OFF -> disable everything.
    // Only exception: mutually-exclusive features (MoP vs SL character
    // creation) cannot both be active, so on "ON" we keep only the
    // preferred side (defaultOn) and leave the other off.
    for (const auto& feature : m_features) {
        QPushButton* toggle = m_featureToggles.value(feature.label, nullptr);
        if (!toggle || !toggle->isEnabled()) continue;
        bool target = enabled;
        if (enabled && !feature.exclusiveWith.isEmpty() && !feature.defaultOn) {
            target = false;
        }
        setToggleState(toggle, target);
    }
    updateMasterButton();
}

void SettingsDialog::loadSettings() {
    m_pathEdit->setText(m_config->getWowPath());
    m_soundCheckbox->setChecked(!m_soundManager->isMuted());
    m_erraiSslCheckbox->setChecked(m_config->isErraiSslCheckIgnored());

    int bgIndex = m_backgroundCombo->findData(m_config->getBackground());
    if (bgIndex != -1) {
        m_backgroundCombo->blockSignals(true);
        m_backgroundCombo->setCurrentIndex(bgIndex);
        m_backgroundCombo->blockSignals(false);
    }

    // Random-background-on-launch toggle
    bool randomBg = m_config->isRandomBackgroundEnabled();
    m_randomBgButton->blockSignals(true);
    m_randomBgButton->setChecked(randomBg);
    m_randomBgButton->blockSignals(false);
    m_randomBgButton->setText(randomBg ? tr("🎲 Aléatoire : ON")
                                       : tr("🎲 Aléatoire : OFF"));
    m_backgroundCombo->setEnabled(!randomBg);

    // Load patch states from the real files on disk.
    bool anyEnabled = false;
    for (const auto& feature : m_features) {
        QPushButton* toggle = m_featureToggles.value(feature.label, nullptr);
        if (!toggle) continue;

        bool present = isFeaturePresent(feature);
        if (!present) {
            toggle->setEnabled(false);
            toggle->setText(tr("Absent"));
            toggle->setToolTip(tr("Ce patch n'est pas présent dans votre installation."));
            continue;
        }
        bool enabled = isFeatureEnabled(feature);
        setToggleState(toggle, enabled);
        if (enabled) anyEnabled = true;
    }

    if (m_masterToggle) {
        m_masterToggle->blockSignals(true);
        m_masterToggle->setChecked(anyEnabled);
        m_masterToggle->blockSignals(false);
        updateMasterButton();
    }

    updateButtonsState();
}

void SettingsDialog::updateButtonsState() {
    QString wowPath = m_pathEdit->text();
    bool exists = !wowPath.isEmpty() && QFile::exists(wowPath + "/Wow.exe");

    m_downloadEnUsButton->setEnabled(exists);
    m_clearCacheButton->setEnabled(exists);
    m_openAddonsButton->setEnabled(exists);

    // Optional: add a tooltip to explain why it's disabled
    if (!exists) {
        m_downloadEnUsButton->setToolTip(tr("Veuillez d'abord configurer le dossier WoW (Wow.exe)"));
    } else {
        m_downloadEnUsButton->setToolTip("");
    }
}

void SettingsDialog::saveSettings() {
    m_config->setWowPath(m_pathEdit->text());
    // Apply to the running SoundManager AND persist to config.json — otherwise
    // the choice is lost and sound re-enables itself on the next launch.
    const bool soundOn = m_soundCheckbox->isChecked();
    m_soundManager->setMuted(!soundOn);
    m_config->setSoundEnabled(soundOn);
    m_config->setBackground(m_backgroundCombo->currentData().toString());
    m_config->setRandomBackgroundEnabled(m_randomBgButton->isChecked());
    m_config->setErraiSslCheckIgnored(m_erraiSslCheckbox->isChecked());
    ErraiNetwork::setIgnoreErraiSslErrors(m_erraiSslCheckbox->isChecked());

    // Apply each feature's toggle to its real file set (root + all locales).
    for (const auto& feature : m_features) {
        QPushButton* toggle = m_featureToggles.value(feature.label, nullptr);
        if (!toggle || !toggle->isEnabled()) continue; // skip absent patches
        applyFeatureToggle(feature, toggle->isChecked());
    }

    m_config->save();
}

QString SettingsDialog::rootPatchPath(const QString& letter) const {
    return m_config->getWowPath() + "/Data/patch-" + letter + ".mpq";
}

QString SettingsDialog::localePatchPath(const QString& locale, const QString& letter) const {
    return m_config->getWowPath() + "/Data/" + locale + "/patch-" + locale + "-" + letter + ".mpq";
}

bool SettingsDialog::isFeaturePresent(const PatchFeature& f) const {
    QString wowPath = m_config->getWowPath();
    if (f.isDxvk) {
        return QFile::exists(wowPath + "/d3d9.dll")
            || QFile::exists(wowPath + "/d3d9.dll.disabled");
    }
    if (!f.rootLetters.isEmpty()) {
        QString p = rootPatchPath(f.rootLetters.first());
        return QFile::exists(p) || QFile::exists(p + ".disabled");
    }
    if (!f.localeLetters.isEmpty()) {
        for (const QString& loc : m_locales) {
            QString p = localePatchPath(loc, f.localeLetters.first());
            if (QFile::exists(p) || QFile::exists(p + ".disabled")) return true;
        }
    }
    return false;
}

bool SettingsDialog::isFeatureEnabled(const PatchFeature& f) const {
    QString wowPath = m_config->getWowPath();
    if (f.isDxvk) {
        return QFile::exists(wowPath + "/d3d9.dll");
    }
    if (!f.rootLetters.isEmpty()) {
        return QFile::exists(rootPatchPath(f.rootLetters.first()));
    }
    if (!f.localeLetters.isEmpty()) {
        for (const QString& loc : m_locales) {
            if (QFile::exists(localePatchPath(loc, f.localeLetters.first()))) return true;
        }
    }
    return false;
}

void SettingsDialog::toggleMpqFile(const QString& activePath, bool enabled) {
    QString disabledPath = activePath + ".disabled";
    if (enabled) {
        if (!QFile::exists(activePath) && QFile::exists(disabledPath)) {
            QFile::rename(disabledPath, activePath);
        }
    } else {
        if (QFile::exists(activePath)) {
            if (QFile::exists(disabledPath)) QFile::remove(disabledPath);
            QFile::rename(activePath, disabledPath);
        }
    }
}

void SettingsDialog::applyFeatureToggle(const PatchFeature& f, bool enabled) {
    QString wowPath = m_config->getWowPath();

    if (f.isDxvk) {
        // DXVK = d3d9.dll + dxvk.conf, toggled together.
        const QStringList files = {"d3d9.dll", "dxvk.conf"};
        for (const QString& file : files) {
            toggleMpqFile(wowPath + "/" + file, enabled);
        }
        return;
    }

    for (const QString& letter : f.rootLetters) {
        toggleMpqFile(rootPatchPath(letter), enabled);
    }
    for (const QString& letter : f.localeLetters) {
        for (const QString& loc : m_locales) {
            if (!QDir(wowPath + "/Data/" + loc).exists()) continue;
            toggleMpqFile(localePatchPath(loc, letter), enabled);
        }
    }
}

void SettingsDialog::onRandomBgToggled(bool enabled) {
    m_soundManager->play("button");
    m_randomBgButton->setText(enabled ? tr("🎲 Aléatoire : ON")
                                      : tr("🎲 Aléatoire : OFF"));
    // When random-on-launch is ON, the fixed selector no longer applies.
    m_backgroundCombo->setEnabled(!enabled);
}

void SettingsDialog::onBrowseClicked() {
    m_soundManager->play("button");
    QString dir = QFileDialog::getExistingDirectory(this,
        tr("Sélectionner le dossier World of Warcraft"),
        m_pathEdit->text().isEmpty() ? QDir::homePath() : m_pathEdit->text());

    if (dir.isEmpty()) {
        return;
    }

    QString wowExe = dir + "/Wow.exe";
    if (!QFile::exists(wowExe)) {
        auto result = QMessageBox::question(this, tr("Confirmation"),
            tr("Wow.exe n'a pas été trouvé dans ce dossier.\n\n"
               "Voulez-vous utiliser cet emplacement (par exemple pour une nouvelle installation) ?"),
            QMessageBox::Yes | QMessageBox::No);

        if (result == QMessageBox::No) {
            return;
        }
    }

    m_pathEdit->setText(dir);
}

void SettingsDialog::onClearCacheClicked() {
    m_soundManager->play("button");
    QString wowPath = m_pathEdit->text();
    if (wowPath.isEmpty()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Aucun dossier WoW configuré."));
        return;
    }

    QString cachePath = wowPath + "/Cache";
    QDir cacheDir(cachePath);

    if (!cacheDir.exists()) {
        QMessageBox::information(this, tr("Info"), tr("Aucun cache à vider."));
        return;
    }

    auto result = QMessageBox::question(this, tr("Confirmer"),
        tr("Êtes-vous sûr de vouloir vider le cache WoW?\n"
           "Cela supprimera le dossier Cache."),
        QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) return;

    if (cacheDir.removeRecursively()) {
        QMessageBox::information(this, tr("Succès"), tr("Cache vidé avec succès!"));
    } else {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible de vider le cache."));
    }
}

void SettingsDialog::onOpenAddonsClicked() {
    m_soundManager->play("button");
    QString wowPath = m_pathEdit->text();
    if (wowPath.isEmpty()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Aucun dossier WoW configuré."));
        return;
    }

    QString addonsPath = wowPath + "/Interface/AddOns";
    QDir addonsDir(addonsPath);

    // Create the folder if it doesn't exist
    if (!addonsDir.exists()) {
        addonsDir.mkpath(".");
    }

    // Open in file explorer
    QDesktopServices::openUrl(QUrl::fromLocalFile(addonsPath));
}

void SettingsDialog::onDownloadEnUsClicked() {
    m_soundManager->play("button");
    QString wowPath = m_pathEdit->text();
    if (wowPath.isEmpty()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Aucun dossier WoW configuré."));
        return;
    }

    // Manifeste lu avant de créer la fenêtre (elle démarre seule après 100 ms).
    ErraiManifest::instance().ensureLoaded();
    const ErraiNetwork::Archive enus = ErraiManifest::instance().archive(
        QStringLiteral("enus"), QString::fromUtf8(SylvaniaConstants::kEnUsPackUrl));
    DownloadDialog* dlDialog = new DownloadDialog(this, wowPath + "/Data");
    dlDialog->setDownloadUrl(enus.url);
    dlDialog->setExpected(enus.size, enus.sha256);
    dlDialog->setSkipConfigWtf(true);
    dlDialog->setWindowTitleText(tr("Téléchargement du Pack Anglais"));

    connect(dlDialog, &DownloadDialog::downloadFinished, this, [this](bool success, const QString& message) {
        if (success) {
            QMessageBox::information(this, tr("Succès"), tr("Pack Anglais installé avec succès !"));
        }
    });

    dlDialog->show();
}

void SettingsDialog::onOkClicked() {
    // Apply settings (including the new sound mute state) FIRST, then play the
    // confirmation click: re-enabling sound is now audibly confirmed instead of
    // being swallowed by the still-muted state at click time.
    saveSettings();
    m_soundManager->play("button");
    emit settingsChanged();
    accept();
}

void SettingsDialog::onCancelClicked() {
    m_soundManager->play("button");

    // If user cancels, we might need to revert background if they changed it
    // But since it emits live, we restore the original if they cancel.
    emit backgroundChanged(m_config->getBackground());

    reject();
}

void SettingsDialog::onBackgroundChanged(const QString& bgName) {
    emit backgroundChanged(bgName);
}
