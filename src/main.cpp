#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QMessageBox>

#include <kddockwidgets/KDDockWidgets.h>

#include "app/AppBootstrap.h"
#include "app/AppPaths.h"
#include "app/SingleInstanceManager.h"
#include "cli/CliDispatcher.h"
#include "debug/DebugLog.h"
#include "storage/DatabaseManager.h"
#include "threadui/ThreadUiIpcServer.h"
#include "ui/ProgressSplashScreen.h"

namespace {

QString applicationVersionString() {
#ifdef QODEX_APP_VERSION
    return QString::fromLatin1(QODEX_APP_VERSION);
#else
    return QStringLiteral("unknown");
#endif
}

void configureApplicationMetadata(QCoreApplication &application) {
    application.setOrganizationName(QStringLiteral("Tomazos.com"));
    application.setApplicationName(QStringLiteral("Qodex"));
    application.setApplicationVersion(applicationVersionString());
}

void enableDebugLoggingIfRequested(const QStringList &arguments) {
    if (!qodex::debug::argumentsContainDebugFlag(arguments)) {
        return;
    }

    qodex::debug::setEnabled(true);
    qodex::debug::installStdoutMessageHandler();
    QODEBUG("Debug logging enabled from command line");
}

QStringList rawArgumentsFromArgv(const int argc, char *argv[]) {
    QStringList arguments;
    arguments.reserve(std::max(0, argc));
    for (int index = 0; index < argc; ++index) {
        arguments.append(QString::fromLocal8Bit(argv[index]));
    }
    return arguments;
}

int runGuiMain(int argc, char *argv[]) {
    QApplication app(argc, argv);
    configureApplicationMetadata(app);
    app.setDesktopFileName(QStringLiteral("com.tomazos.qodex"));
    QIcon applicationIcon;
    applicationIcon.addFile(QStringLiteral(":/images/qodex-icon-16x16.png"), QSize(16, 16));
    applicationIcon.addFile(QStringLiteral(":/images/qodex-icon-20x20.png"), QSize(20, 20));
    applicationIcon.addFile(QStringLiteral(":/images/qodex-icon-22x22.png"), QSize(22, 22));
    applicationIcon.addFile(QStringLiteral(":/images/qodex-icon-24x24.png"), QSize(24, 24));
    applicationIcon.addFile(QStringLiteral(":/images/qodex-icon-32x32.png"), QSize(32, 32));
    applicationIcon.addFile(QStringLiteral(":/images/qodex-icon-48x48.png"), QSize(48, 48));
    applicationIcon.addFile(QStringLiteral(":/images/qodex-icon-64x64.png"), QSize(64, 64));
    applicationIcon.addFile(QStringLiteral(":/images/qodex-icon-128x128.png"), QSize(128, 128));
    applicationIcon.addFile(QStringLiteral(":/images/qodex-icon-256x256.png"), QSize(256, 256));
    applicationIcon.addFile(QStringLiteral(":/images/qodex-icon-512x512.png"), QSize(512, 512));
    applicationIcon.addFile(QStringLiteral(":/images/qodex-icon-618x618.png"), QSize(618, 618));
    app.setWindowIcon(applicationIcon);
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser commandLineParser;
    commandLineParser.setApplicationDescription(QStringLiteral("Qodex"));
    commandLineParser.addHelpOption();
    commandLineParser.addVersionOption();
    const QString defaultDatabasePath = qodex::app::AppPaths::discover().databasePath;
    QCommandLineOption dataOption(
        QStringList{QStringLiteral("data")},
        QStringLiteral("Path to the SQLite database file used by Qodex. Defaults to %1.")
            .arg(defaultDatabasePath),
        QStringLiteral("file")
    );
    QCommandLineOption debugOption(
        QStringList{QStringLiteral("debug")},
        QStringLiteral("Enable timestamped qodex debug logging to stdout.")
    );
    commandLineParser.addOption(dataOption);
    commandLineParser.addOption(debugOption);
    commandLineParser.process(app);

    if (commandLineParser.isSet(debugOption)) {
        qodex::debug::setEnabled(true);
        qodex::debug::installStdoutMessageHandler();
    }

    const qodex::app::AppPaths appPaths = qodex::app::AppPaths::discover(commandLineParser.value(dataOption));
    QODEBUG("Selected GUI mode with database", appPaths.databasePath);

    qodex::ui::ProgressSplashScreen startupSplash(QStringLiteral("Starting Qodex"));
    startupSplash.setStatus(QStringLiteral("Checking for a running Qodex instance..."), 10);
    startupSplash.showCentered();

    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);

    qodex::app::SingleInstanceManager singleInstanceManager(appPaths.databasePath);
    if (!singleInstanceManager.startPrimaryOrActivateExisting()) {
        startupSplash.close();
        return 0;
    }

    startupSplash.setStatus(QStringLiteral("Opening Qodex database..."), 30);
    qodex::storage::DatabaseManager databaseManager(appPaths.databasePath);
    QString databaseError;
    if (!databaseManager.open(&databaseError)) {
        startupSplash.close();
        QMessageBox::critical(
            nullptr,
            QStringLiteral("Qodex"),
            QStringLiteral("Failed to open database %1:\n%2").arg(appPaths.databasePath, databaseError)
        );
        return 1;
    }

    startupSplash.setStatus(QStringLiteral("Starting Thread UI IPC server..."), 50);
    qodex::threadui::ThreadUiIpcServer threadUiIpcServer;
    QString threadUiIpcError;
    if (!threadUiIpcServer.listen(&threadUiIpcError)) {
        startupSplash.close();
        QMessageBox::critical(
            nullptr,
            QStringLiteral("Qodex"),
            QStringLiteral("Failed to start Thread UI IPC server:\n%1").arg(threadUiIpcError)
        );
        return 1;
    }

    startupSplash.setStatus(QStringLiteral("Restoring workspace..."), 60);
    qodex::app::AppBootstrap bootstrap(appPaths, &databaseManager, &threadUiIpcServer);
    QObject::connect(
        &bootstrap,
        &qodex::app::AppBootstrap::startupProgressChanged,
        &app,
        [&startupSplash](const QString &message, const int progress) {
            startupSplash.setStatus(message, progress);
        }
    );
    QObject::connect(&bootstrap, &qodex::app::AppBootstrap::startupFinished, &app, [&startupSplash, &bootstrap] {
        bootstrap.showAllWindows();
        startupSplash.close();
    });
    QObject::connect(
        &singleInstanceManager,
        &qodex::app::SingleInstanceManager::activationRequested,
        &app,
        [&bootstrap] { bootstrap.activate(); }
    );
    if (singleInstanceManager.takePendingActivation()) {
        bootstrap.activate();
    }
    startupSplash.setStatus(QStringLiteral("Showing workspace..."), 68);
    bootstrap.hideAllWindows();
    bootstrap.start();

    return app.exec();
}

}  // namespace

int main(int argc, char *argv[]) {
    const QStringList rawArguments = rawArgumentsFromArgv(argc, argv);
    enableDebugLoggingIfRequested(rawArguments.mid(1));

    qodex::cli::CliDispatcher cliDispatcher;
    if (cliDispatcher.shouldRunCli(rawArguments.mid(1))) {
        QODEBUG("Dispatching to CLI mode");
        return cliDispatcher.run(argc, argv);
    }

    QODEBUG("Dispatching to GUI mode");
    return runGuiMain(argc, argv);
}
