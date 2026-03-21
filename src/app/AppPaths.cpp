#include "app/AppPaths.h"

#include <QDir>
#include <QStandardPaths>

namespace qodex::app {

AppPaths AppPaths::discover() {
    AppPaths paths;
    paths.homeDir = QDir::homePath();
    paths.appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return paths;
}

}  // namespace qodex::app
