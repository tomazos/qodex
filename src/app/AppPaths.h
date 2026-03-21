#pragma once

#include <QString>

namespace qodex::app {

struct AppPaths {
    QString homeDir;
    QString appDataDir;

    [[nodiscard]] static AppPaths discover();
};

}  // namespace qodex::app
