#include "RealmlistWindow.h"
#include "ConfigManager.h"
#include "GameEdition.h"

#include <QLineEdit>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFrame>
#include <QScrollArea>

RealmlistWindow::RealmlistWindow(ConfigManager* config, QWidget* parent)
    : QDialog(parent)
    , m_config(config)
    , m_buttonGroup(new QButtonGroup(this))
{
    setWindowTitle(tr("🌐 Gestion des Serveurs - Sylvania Launcher"));
    setMinimumSize(500, 400);
    resize(600, 500);
    
    setupUi();
    applyStyle();
    loadRealmlistEntries();
}

void RealmlistWindow::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // Current server label
    m_currentServerLabel = new QLabel(tr("Serveur actuel: Chargement..."), this);
    m_currentServerLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_currentServerLabel);
    
    // Servers scroll area
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    
    m_serversContainer = new QWidget();
    m_serversLayout = new QVBoxLayout(m_serversContainer);
    m_serversLayout->setSpacing(10);
    m_serversLayout->addStretch();
    
    scrollArea->setWidget(m_serversContainer);
    mainLayout->addWidget(scrollArea, 1);
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    m_addButton = new QPushButton(tr("➕ Ajouter"), this);
    m_editButton = new QPushButton(tr("✏️ Modifier"), this);
    m_deleteButton = new QPushButton(tr("🗑️ Supprimer"), this);
    m_applyButton = new QPushButton(tr("✅ Appliquer"), this);
    
    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_editButton);
    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_applyButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connect signals
    connect(m_addButton, &QPushButton::clicked, this, &RealmlistWindow::onAddServer);
    connect(m_editButton, &QPushButton::clicked, this, &RealmlistWindow::onEditServer);
    connect(m_deleteButton, &QPushButton::clicked, this, &RealmlistWindow::onDeleteServer);
    connect(m_applyButton, &QPushButton::clicked, this, &RealmlistWindow::onApplyChanges);
    connect(m_buttonGroup, &QButtonGroup::buttonClicked, this, &RealmlistWindow::onServerSelected);
}

void RealmlistWindow::applyStyle() {
    setStyleSheet(R"(
        QDialog {
            background-color: #1a1a1a;
        }
        QLabel {
            color: #d4af37;
            font-size: 14px;
        }
        QScrollArea {
            background-color: transparent;
        }
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a4a2d, stop:0.5 #3a2a1d, stop:1 #2a1a0d);
            color: #d4af37;
            border: 1px solid #7a6a4d;
            border-radius: 5px;
            padding: 10px 20px;
            font-weight: bold;
            min-width: 80px;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #6a5a3d, stop:0.5 #4a3a2d, stop:1 #3a2a1d);
        }
        QRadioButton {
            color: #ffffff;
            font-size: 14px;
        }
        QRadioButton::indicator {
            width: 18px;
            height: 18px;
        }
        QGroupBox {
            background-color: #2a2a2a;
            border: 1px solid #5a4a2d;
            border-radius: 8px;
            padding: 15px;
            margin-top: 5px;
        }
        QGroupBox::title {
            color: #d4af37;
        }
    )");
}

void RealmlistWindow::loadRealmlistEntries() {
    // Clear existing widgets
    while (m_serversLayout->count() > 1) { // Keep the stretch
        QLayoutItem* item = m_serversLayout->takeAt(0);
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }
    
    // Clear button group
    for (QAbstractButton* btn : m_buttonGroup->buttons()) {
        m_buttonGroup->removeButton(btn);
    }
    
    auto entries = m_config->getRealmlistEntries();
    int activeIndex = m_config->getActiveRealmlistIndex();
    
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        QWidget* widget = createServerWidget(entries[i], i, i == activeIndex);
        m_serversLayout->insertWidget(m_serversLayout->count() - 1, widget);
    }
    
    m_selectedIndex = activeIndex;
    updateCurrentServerLabel();
}

QWidget* RealmlistWindow::createServerWidget(const RealmlistEntry& entry, int index, bool isActive) {
    QFrame* frame = new QFrame(this);
    frame->setObjectName("serverFrame");
    frame->setStyleSheet(QString(R"(
        QFrame#serverFrame {
            background-color: %1;
            border: 2px solid %2;
            border-radius: 10px;
            padding: 10px;
        }
    )").arg(isActive ? "#3a3a3a" : "#2a2a2a")
       .arg(isActive ? "#d4af37" : "#5a4a2d"));
    
    QHBoxLayout* layout = new QHBoxLayout(frame);
    
    QRadioButton* radio = new QRadioButton(this);
    radio->setChecked(isActive);
    m_buttonGroup->addButton(radio, index);
    
    QVBoxLayout* infoLayout = new QVBoxLayout();
    
    QLabel* nameLabel = new QLabel(entry.name, this);
    nameLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #d4af37;");
    
    QLabel* addressLabel = new QLabel(entry.address, this);
    addressLabel->setStyleSheet("font-size: 12px; color: #888888;");
    
    infoLayout->addWidget(nameLabel);
    infoLayout->addWidget(addressLabel);
    
    layout->addWidget(radio);
    layout->addLayout(infoLayout, 1);
    
    if (isActive) {
        QLabel* activeLabel = new QLabel(tr("✓ Actif"), this);
        activeLabel->setStyleSheet("color: #4CAF50; font-weight: bold;");
        layout->addWidget(activeLabel);
    }
    
    return frame;
}

void RealmlistWindow::updateCurrentServerLabel() {
    auto entries = m_config->getRealmlistEntries();
    int activeIndex = m_config->getActiveRealmlistIndex();
    
    if (activeIndex >= 0 && activeIndex < static_cast<int>(entries.size())) {
        m_currentServerLabel->setText(tr("Serveur actuel: %1").arg(entries[activeIndex].name));
    } else {
        m_currentServerLabel->setText(tr("Aucun serveur configuré"));
    }
}

void RealmlistWindow::onServerSelected(QAbstractButton* button) {
    m_selectedIndex = m_buttonGroup->id(button);
}

void RealmlistWindow::onAddServer() {
    const bool legion = GameEdition::byId(m_config->activeEditionId()).id == QLatin1String("legion");
    ServerEditDialog dialog(this, QString(), QString(), legion);
    
    if (dialog.exec() == QDialog::Accepted) {
        QString name = dialog.getName();
        QString address = dialog.getAddress();
        
        if (!name.isEmpty() && !address.isEmpty()) {
            auto entries = m_config->getRealmlistEntries();
            entries.push_back({name, address, false});
            m_config->setRealmlistEntries(entries);
            loadRealmlistEntries();
        }
    }
}

void RealmlistWindow::onEditServer() {
    if (m_selectedIndex < 0) {
        QMessageBox::warning(this, tr("Erreur"), tr("Veuillez sélectionner un serveur à modifier."));
        return;
    }
    
    auto entries = m_config->getRealmlistEntries();
    if (m_selectedIndex >= static_cast<int>(entries.size())) return;
    
    const auto& entry = entries[m_selectedIndex];
    const bool legion = GameEdition::byId(m_config->activeEditionId()).id == QLatin1String("legion");
    ServerEditDialog dialog(this, entry.name, entry.address, legion);
    
    if (dialog.exec() == QDialog::Accepted) {
        entries[m_selectedIndex].name = dialog.getName();
        entries[m_selectedIndex].address = dialog.getAddress();
        m_config->setRealmlistEntries(entries);
        loadRealmlistEntries();
    }
}

void RealmlistWindow::onDeleteServer() {
    if (m_selectedIndex < 0) {
        QMessageBox::warning(this, tr("Erreur"), tr("Veuillez sélectionner un serveur à supprimer."));
        return;
    }
    
    auto entries = m_config->getRealmlistEntries();
    if (entries.size() <= 1) {
        QMessageBox::warning(this, tr("Erreur"), tr("Vous devez garder au moins un serveur."));
        return;
    }
    
    auto result = QMessageBox::question(this, tr("Confirmer"),
        tr("Êtes-vous sûr de vouloir supprimer ce serveur?"),
        QMessageBox::Yes | QMessageBox::No);
    
    if (result != QMessageBox::Yes) return;
    
    entries.erase(entries.begin() + m_selectedIndex);
    
    // Adjust active index if needed
    int activeIndex = m_config->getActiveRealmlistIndex();
    if (m_selectedIndex <= activeIndex && activeIndex > 0) {
        m_config->setActiveRealmlistIndex(activeIndex - 1);
    }
    
    m_config->setRealmlistEntries(entries);
    m_selectedIndex = -1;
    loadRealmlistEntries();
}

void RealmlistWindow::onApplyChanges() {
    if (m_selectedIndex >= 0) {
        m_config->switchRealmlist(m_selectedIndex);
        emit realmlistChanged();
        loadRealmlistEntries();
        QMessageBox::information(this, tr("Succès"), tr("Serveur modifié avec succès!"));
    }
    close();
}

// ServerEditDialog implementation
ServerEditDialog::ServerEditDialog(QWidget* parent, const QString& name,
                                   const QString& address, bool legionPortal)
    : QDialog(parent)
{
    setWindowTitle(tr("Configuration du Serveur"));
    setModal(true);
    setFixedSize(450, 240);

    setupUi(name, address, legionPortal);
    
    setStyleSheet(R"(
        QDialog {
            background-color: #1a1a1a;
        }
        QLabel {
            color: #d4af37;
            font-weight: bold;
            font-size: 13px;
        }
        QLineEdit {
            background-color: #2a2a2a;
            color: #ffffff;
            border: 1px solid #5a4a2d;
            border-radius: 5px;
            padding: 10px;
            font-size: 12px;
            min-height: 20px;
        }
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a4a2d, stop:0.5 #3a2a1d, stop:1 #2a1a0d);
            color: #d4af37;
            border: 1px solid #7a6a4d;
            border-radius: 5px;
            padding: 10px 30px;
            font-weight: bold;
            font-size: 12px;
            min-width: 90px;
            min-height: 35px;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #6a5a3d, stop:0.5 #4a3a2d, stop:1 #3a2a1d);
        }
    )");
}

void ServerEditDialog::setupUi(const QString& name, const QString& address, bool legionPortal) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(25, 20, 25, 20);

    // Name
    layout->addWidget(new QLabel(tr("Nom du serveur:"), this));
    m_nameEdit = new QLineEdit(name, this);
    m_nameEdit->setPlaceholderText(tr("Ex: Sylvania"));
    layout->addWidget(m_nameEdit);

    // Address — label + hint depend on the edition. Legion 7.3.5 uses a
    // "portal" host (written as SET portal in Config.wtf); WotLK uses a
    // classic "set realmlist" line.
    layout->addWidget(new QLabel(
        legionPortal ? tr("Adresse (portal):") : tr("Adresse (realmlist):"), this));
    m_addressEdit = new QLineEdit(address, this);
    m_addressEdit->setPlaceholderText(
        legionPortal ? tr("Ex: 164.132.43.2")
                     : tr("Ex: set realmlist errai.minotux.com"));
    layout->addWidget(m_addressEdit);
    
    layout->addStretch();
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(15);
    QPushButton* cancelButton = new QPushButton(tr("Annuler"), this);
    QPushButton* okButton = new QPushButton(tr("OK"), this);
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);
    
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(okButton, &QPushButton::clicked, this, &ServerEditDialog::validate);
}

QString ServerEditDialog::getName() const {
    return m_nameEdit->text().trimmed();
}

QString ServerEditDialog::getAddress() const {
    return m_addressEdit->text().trimmed();
}

void ServerEditDialog::validate() {
    if (getName().isEmpty() || getAddress().isEmpty()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Tous les champs sont requis."));
        return;
    }
    accept();
}
