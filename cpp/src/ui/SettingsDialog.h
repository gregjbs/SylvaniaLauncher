#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <vector>


class ConfigManager;
class SoundManager;

/**
 * @brief Settings dialog for Sylvania Launcher
 *
 * Contains:
 * - WoW installation path configuration
 * - Cache clearing option
 * - Audio settings (enable/disable sounds)
 * - Appearance (background selector + random pick)
 * - HD patch management (master ON/OFF + per-feature ON/OFF toggles)
 */
class SettingsDialog : public QDialog {
  Q_OBJECT

public:
  explicit SettingsDialog(ConfigManager *config, SoundManager *soundManager,
                          QWidget *parent = nullptr);

signals:
  void settingsChanged();
  void backgroundChanged(const QString &bgName);

private slots:
  void onBrowseClicked();
  void onBackgroundChanged(const QString &bgName);
  void onRandomBgToggled(bool enabled);
  void onClearCacheClicked();
  void onOpenAddonsClicked();
  void onDownloadEnUsClicked();
  void onMasterToggled(bool enabled);
  void onOkClicked();
  void onCancelClicked();

private:
  void setupUi();
  void setupPatchUI(QVBoxLayout *mainLayout);
  void loadSettings();
  void saveSettings();
  void updateButtonsState();

  // A HD feature = a set of MPQ files (root + the same letter across locales),
  // toggled together. Letters map to real files per the technical README (§4/§5/§6).
  struct PatchFeature {
    QString label;
    QStringList rootLetters;    // Data/patch-<letter>.mpq
    QStringList localeLetters;  // Data/<loc>/patch-<loc>-<letter>.mpq (all locales)
    bool isDxvk = false;        // special: d3d9.dll (+ dxvk.conf)
    bool dangerous = false;     // embeds a .dbc -> risk of detection/crash
    bool defaultOn = false;     // recommended default state (README)
    QString exclusiveWith;      // label of a mutually exclusive feature
  };

  // Toggle button helpers
  QPushButton *createToggleButton();
  void setToggleState(QPushButton *btn, bool on);
  void updateMasterButton();

  // Patch state helpers (operate on real files, never delete -> rename only)
  QString rootPatchPath(const QString &letter) const;
  QString localePatchPath(const QString &locale, const QString &letter) const;
  bool isFeaturePresent(const PatchFeature &f) const;
  bool isFeatureEnabled(const PatchFeature &f) const;
  void applyFeatureToggle(const PatchFeature &f, bool enabled);
  void toggleMpqFile(const QString &activePath, bool enabled);

  std::vector<PatchFeature> m_features;
  QMap<QString, QPushButton *> m_featureToggles;
  QStringList m_locales;

  ConfigManager *m_config;
  SoundManager *m_soundManager;

  QLineEdit *m_pathEdit = nullptr;
  QCheckBox *m_soundCheckbox = nullptr;
  QCheckBox *m_erraiSslCheckbox = nullptr;
  QPushButton *m_browseButton = nullptr;
  QComboBox *m_backgroundCombo = nullptr;
  QPushButton *m_randomBgButton = nullptr;
  QPushButton *m_clearCacheButton = nullptr;
  QPushButton *m_openAddonsButton = nullptr;
  QPushButton *m_downloadEnUsButton = nullptr;
  QPushButton *m_masterToggle = nullptr;
  QPushButton *m_okButton = nullptr;
  QPushButton *m_cancelButton = nullptr;
};
