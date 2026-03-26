#include "cli/HelpCommand.h"

#include "cli/CliDispatcher.h"

namespace qodex::cli {

QStringList HelpCommand::commandPath() const {
    return {QStringLiteral("help")};
}

QString HelpCommand::summary() const {
    return QStringLiteral("Show qodex CLI help.");
}

QString HelpCommand::usage() const {
    return QStringLiteral("help [command...]");
}

QString HelpCommand::description() const {
    return QStringLiteral("Show general qodex CLI help, or help for a specific command path.");
}

int HelpCommand::run(const QStringList &arguments, const CliContext &context) const {
    Q_ASSERT(context.stdoutStream != nullptr);
    Q_ASSERT(context.stderrStream != nullptr);
    Q_ASSERT(context.dispatcher != nullptr);

    if (arguments.isEmpty()) {
        *context.stdoutStream << context.dispatcher->renderGeneralHelp(context.executableName, context.databasePath);
        return 0;
    }

    const CliCommand *command = context.dispatcher->findCommand(arguments);
    if (command == nullptr) {
        *context.stderrStream << QStringLiteral("Unknown qodex command path: %1").arg(arguments.join(QStringLiteral(" ")))
                              << Qt::endl;
        *context.stderrStream << QStringLiteral("See `%1 help`.").arg(context.executableName) << Qt::endl;
        return 2;
    }

    *context.stdoutStream << context.dispatcher->renderCommandHelp(*command, context.executableName);
    return 0;
}

}  // namespace qodex::cli
