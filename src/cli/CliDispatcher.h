#pragma once

#include <memory>
#include <vector>

#include <QString>
#include <QStringList>
#include "cli/CliCommand.h"

namespace qodex::cli {

class CliDispatcher {
public:
    CliDispatcher();
    ~CliDispatcher();

    [[nodiscard]] bool shouldRunCli(const QStringList &arguments) const;
    [[nodiscard]] const CliCommand *findCommand(const QStringList &path) const;
    [[nodiscard]] QString renderGeneralHelp(const QString &programName, const QString &defaultDatabasePath) const;
    [[nodiscard]] QString renderCommandHelp(const CliCommand &command, const QString &programName) const;

    int run(int argc, char *argv[]) const;

private:
    struct GlobalOptions {
        QString databasePathOverride;
        bool debugEnabled = false;
    };

    [[nodiscard]] QStringList extractLeadingPositionalArguments(const QStringList &arguments) const;
    [[nodiscard]] bool parseGlobalOptions(
        const QStringList &arguments,
        GlobalOptions *options,
        QStringList *remainingArguments,
        QString *errorMessage
    ) const;
    [[nodiscard]] const CliCommand *matchCommand(
        const QStringList &remainingArguments,
        int *consumedArgumentCount = nullptr
    ) const;
    [[nodiscard]] QStringList rootCommandWords() const;

    std::vector<std::unique_ptr<CliCommand>> m_commands;
};

}  // namespace qodex::cli
