#pragma once

#include <QString>

namespace qodex::storage {

class MigrationRunner final {
public:
    [[nodiscard]] static bool migrate(const QString &databasePath, QString *errorMessage);
    [[nodiscard]] static int headVersion();
};

}  // namespace qodex::storage
