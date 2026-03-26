#pragma once

#include <QString>
#include <QJsonValue>

#include "cli/CliCommand.h"

struct sqlite3;
struct sqlite3_stmt;

namespace qodex::cli {

class DbQueryCommand final : public CliCommand {
public:
    [[nodiscard]] QStringList commandPath() const override;
    [[nodiscard]] QString summary() const override;
    [[nodiscard]] QString usage() const override;
    [[nodiscard]] QString description() const override;
    int run(const QStringList &arguments, const CliContext &context) const override;

private:
    [[nodiscard]] bool openReadOnlyDatabase(const QString &databasePath, sqlite3 **database, QString *errorMessage) const;
    [[nodiscard]] bool prepareSingleStatement(
        sqlite3 *database,
        const QString &sql,
        sqlite3_stmt **statement,
        QString *errorMessage
    ) const;
    [[nodiscard]] QJsonValue columnToJson(sqlite3_stmt *statement, int columnIndex) const;
    [[nodiscard]] QString blobToHexLiteral(const void *data, int byteCount) const;
};

}  // namespace qodex::cli
