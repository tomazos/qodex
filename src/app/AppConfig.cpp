#include "app/AppConfig.h"

#include <QCoreApplication>
#include <QStandardPaths>

namespace qodex::app {

AppConfig AppConfig::loadDefault(const AppPaths &paths) {
    Q_UNUSED(paths);

    AppConfig config;
    config.codexProgram = QStandardPaths::findExecutable(QStringLiteral("codex"));

    const QString applicationVersion = QCoreApplication::applicationVersion();
    if (!applicationVersion.isEmpty()) {
        config.clientVersion = applicationVersion;
    }

    return config;
}

}  // namespace qodex::app
