#pragma once

#include <QTextStream>
#include <QString>
#include <QStringList>

namespace qodex::cli {

class CliDispatcher;

struct CliContext {
    QString executableName;
    QString databasePath;
    QTextStream *stdoutStream = nullptr;
    QTextStream *stderrStream = nullptr;
    const CliDispatcher *dispatcher = nullptr;
};

class CliCommand {
public:
    virtual ~CliCommand() = default;

    [[nodiscard]] virtual QStringList commandPath() const = 0;
    [[nodiscard]] virtual QString summary() const = 0;
    [[nodiscard]] virtual QString usage() const = 0;
    [[nodiscard]] virtual QString description() const = 0;

    virtual int run(const QStringList &arguments, const CliContext &context) const = 0;
};

}  // namespace qodex::cli
