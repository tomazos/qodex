#pragma once

#include <QString>
#include <QStringList>

#include "app/AppPaths.h"

namespace qodex::app {

struct AppConfig {
    QString codexProgram;
    QStringList codexArguments{QStringLiteral("app-server")};
    QString clientName{QStringLiteral("qodex")};
    QString clientVersion{QStringLiteral("0.1.0")};

    [[nodiscard]] static AppConfig loadDefault(const AppPaths &paths);
};

}  // namespace qodex::app
