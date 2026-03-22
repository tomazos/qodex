#include <QApplication>
#include <QCommandLineParser>

#include <kddockwidgets/KDDockWidgets.h>

#include "app/AppBootstrap.h"
#include "app/SingleInstanceManager.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Tomazos.com"));
    app.setApplicationName(QStringLiteral("Qodex"));
    app.setApplicationVersion(QStringLiteral(QODEX_APP_VERSION));

    QCommandLineParser commandLineParser;
    commandLineParser.setApplicationDescription(QStringLiteral("Qodex"));
    commandLineParser.addHelpOption();
    commandLineParser.addVersionOption();
    commandLineParser.process(app);

    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);

    qodex::app::SingleInstanceManager singleInstanceManager;
    if (!singleInstanceManager.startPrimaryOrActivateExisting()) {
        return 0;
    }

    qodex::app::AppBootstrap bootstrap;
    QObject::connect(
        &singleInstanceManager,
        &qodex::app::SingleInstanceManager::activationRequested,
        &app,
        [&bootstrap] { bootstrap.activate(); }
    );
    if (singleInstanceManager.takePendingActivation()) {
        bootstrap.activate();
    }
    bootstrap.mainWindow().show();
    bootstrap.start();

    return app.exec();
}
