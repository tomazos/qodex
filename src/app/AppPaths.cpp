#include "app/AppPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace qodex::app {

AppPaths AppPaths::discover(const QString &databasePathOverride) {
    AppPaths paths;
    paths.homeDir = QDir::homePath();
    paths.appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    paths.appStateDir = QStandardPaths::writableLocation(QStandardPaths::StateLocation);

    if (databasePathOverride.isEmpty()) {
        QString baseDir = paths.appDataDir;
        if (baseDir.isEmpty()) {
            baseDir = paths.homeDir;
        }
        paths.databasePath = QDir(baseDir).filePath(QStringLiteral("qodex.sqlite3"));
    } else {
        const QFileInfo databasePathInfo(databasePathOverride);
        paths.databasePath = databasePathInfo.isRelative() ? QDir::current().absoluteFilePath(databasePathOverride)
                                                           : databasePathInfo.absoluteFilePath();
    }

    paths.appDataDir = QDir::cleanPath(paths.appDataDir);
    paths.appStateDir = QDir::cleanPath(paths.appStateDir);
    paths.databasePath = QDir::cleanPath(paths.databasePath);
    return paths;
}

}  // namespace qodex::app
