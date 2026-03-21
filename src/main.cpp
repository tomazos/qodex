#include <QApplication>

#include <kddockwidgets/KDDockWidgets.h>

#include "app/AppBootstrap.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qodex"));
    app.setApplicationVersion(QStringLiteral(QODEX_APP_VERSION));
    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);

    qodex::app::AppBootstrap bootstrap;
    bootstrap.mainWindow().show();
    bootstrap.start();

    return app.exec();
}
