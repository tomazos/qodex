#pragma once

#include <QString>

namespace qodex::app {

struct AppPaths {
    QString homeDir;
    QString appDataDir;
    QString appStateDir;
    QString databasePath;

    [[nodiscard]] static AppPaths discover(const QString &databasePathOverride = {});
};

}  // namespace qodex::app
