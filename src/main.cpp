#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>

#include <kddockwidgets/KDDockWidgets.h>

#include "app/AppBootstrap.h"
#include "app/AppPaths.h"
#include "app/SingleInstanceManager.h"
#include "storage/DatabaseManager.h"
#include "ui/ProgressSplashScreen.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Tomazos.com"));
    app.setApplicationName(QStringLiteral("Qodex"));
    app.setApplicationVersion(QStringLiteral(QODEX_APP_VERSION));
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
    commandLineParser.addOption(dataOption);
    commandLineParser.process(app);

    const qodex::app::AppPaths appPaths = qodex::app::AppPaths::discover(commandLineParser.value(dataOption));

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

    startupSplash.setStatus(QStringLiteral("Restoring workspace..."), 50);
    qodex::app::AppBootstrap bootstrap(appPaths, &databaseManager);
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
    startupSplash.setStatus(QStringLiteral("Showing workspace..."), 60);
    bootstrap.hideAllWindows();
    bootstrap.start();

    return app.exec();
}
