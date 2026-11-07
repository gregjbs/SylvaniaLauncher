/**
 * @file main.cpp
 * @brief WowErraiLauncher - launcher World of Warcraft 3.3.5a du royaume Errai
 *        (fork GPLv3 de Sylvania Launcher : WotLK 3.3.5a + Legion 7.3.5)
 *
 * Main entry point for the C++ version of Sylvania Launcher.
 */

#include <QApplication>
#include <QIcon>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#endif

#include "core/GameEdition.h"
#include "core/BuildInfoPatcher.h"
#include "ui/MainWindow.h"
#include "ui/DownloadDialog.h"
#include "utils/PathUtils.h"

#include <QObject>
#include <QStringList>
#include <QTextStream>

// Headless self-test for the segmented downloader:
//   SylvaniaLauncher.exe --selftest-download=<url>;<size>;<sha256>;<segBytes>;<dir>
// Downloads the file via the real DownloadDialog code path (segmented + resume
// + integrity), verifies size/hash, prints OK/FAIL and exits. No window shown.
static int runDownloadSelfTest(QApplication& app, const QString& spec) {
    const QStringList p = spec.split(';');
    if (p.size() < 5) { qWarning() << "selftest: spec invalide"; return 2; }

    // Route qDebug/qWarning to a log file (the GUI subsystem has no console).
    static QString s_logPath = p[4] + "/selftest.log";
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& m) {
        QFile lf(s_logPath);
        if (lf.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream(&lf) << m << "\n";
        }
    });

    GameEdition ed = GameEdition::legion();   // start from a real edition
    ed.clientDownloadUrl = p[0];
    ed.clientExpectedSize = p[1].toLongLong();
    ed.clientExpectedSha256 = p[2].toLower();
    ed.requireHashBeforeExtract = true;
    const qint64 seg = p[3].toLongLong();
    const QString dir = p[4];

    auto* dlg = new DownloadDialog(nullptr, QString());  // empty dest = no auto-start
    dlg->configureForEdition(ed);
    dlg->setSegmentSize(seg);
    dlg->setVerifyOnly(true);
    // Optional 6th field: a per-chunk manifest path (tests the chunk-verified
    // download path against a matching slice of the real client).
    if (p.size() >= 6 && !p[5].isEmpty()) {
        dlg->loadChunkManifestFromFile(p[5]);
    }

    int rc = 1;
    QTextStream out(stdout);
    QObject::connect(dlg, &DownloadDialog::downloadFinished, &app,
                     [&](bool ok, const QString& msg) {
        out << (ok ? "SELFTEST OK: " : "SELFTEST FAIL: ") << msg << "\n";
        out.flush();
        rc = ok ? 0 : 1;
        app.quit();
    });
    dlg->startDownload(dir);
    app.exec();
    return rc;
}

// Reads the persisted active edition without spinning up a full ConfigManager,
// so the correct taskbar icon is set before any window exists. The value is
// validated by GameEdition::byId — garbage in config can never select an
// unknown edition or be injected into an asset path.
static const GameEdition& peekActiveEdition() {
    QFile file(PathUtils::getConfigDir() + "/config.json");
    QString id;
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        id = doc.object().value("active_edition").toString();
    }
    return GameEdition::byId(id);
}

int main(int argc, char* argv[]) {
    // High DPI support
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);

    // Set application metadata
    // Fork Errai : le nom d'application fixe le dossier de configuration,
    // %LOCALAPPDATA%\WowErraiLauncher (sans nom d'organisation, volontairement :
    // Qt l'insérerait dans le chemin). Distinct de celui du launcher Sylvania.
    app.setApplicationName("WowErraiLauncher");
    app.setApplicationVersion("3.0");
    app.setOrganizationDomain("errai.minotux.com");

#ifdef Q_OS_WIN
    // Windows: single edition-neutral AppUserModelID for taskbar grouping.
    // The launcher hosts both WotLK and Legion, so the id is no longer tied
    // to 3.3.5; the visible taskbar icon follows the active edition's window
    // icon instead.
    SetCurrentProcessExplicitAppUserModelID(L"errai.wowerrailauncher");
#endif

    // Headless self-tests (developer use; no UI).
    for (const QString& arg : app.arguments()) {
        if (arg.startsWith(QStringLiteral("--selftest-download="))) {
            return runDownloadSelfTest(app, arg.section('=', 1));
        }
        if (arg.startsWith(QStringLiteral("--selftest-buildinfo="))) {
            QString err;
            return patchBuildInfoCdn(arg.section('=', 1), &err) ? 0 : 1;
        }
    }

    // Set application icon from the active edition's theme.
    const GameEdition& edition = peekActiveEdition();
    const QString iconPath = PathUtils::getAssetsDir() + "/" + edition.taskbarIconAsset;
    if (QFile::exists(iconPath)) {
        app.setWindowIcon(QIcon(iconPath));
    } else {
        // Try from resources
        app.setWindowIcon(QIcon(":/icon"));
    }

    // Create and show main window
    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
