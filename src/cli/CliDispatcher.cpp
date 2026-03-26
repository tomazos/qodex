#include "cli/CliDispatcher.h"

#include <algorithm>

#include <QCoreApplication>
#include <QFileInfo>

#include "app/AppPaths.h"
#include "cli/DbQueryCommand.h"
#include "cli/HelpCommand.h"
#include "debug/DebugLog.h"

namespace qodex::cli {

namespace {

QStringList rawArgumentsFromArgv(const int argc, char *argv[]) {
    QStringList arguments;
    arguments.reserve(std::max(0, argc));
    for (int index = 0; index < argc; ++index) {
        arguments.append(QString::fromLocal8Bit(argv[index]));
    }
    return arguments;
}

QString applicationVersionString() {
#ifdef QODEX_APP_VERSION
    return QString::fromLatin1(QODEX_APP_VERSION);
#else
    return QStringLiteral("unknown");
#endif
}

void configureApplicationMetadata(QCoreApplication &application) {
    application.setOrganizationName(QStringLiteral("Tomazos.com"));
    application.setApplicationName(QStringLiteral("Qodex"));
    application.setApplicationVersion(applicationVersionString());
}

bool isOptionWithValue(const QString &argument) {
    return argument == QStringLiteral("--data");
}

bool isInlineDataOption(const QString &argument) {
    return argument.startsWith(QStringLiteral("--data="));
}

bool isDebugOption(const QString &argument) {
    return argument == QStringLiteral("--debug");
}

}  // namespace

CliDispatcher::CliDispatcher() {
    m_commands.push_back(std::make_unique<HelpCommand>());
    m_commands.push_back(std::make_unique<DbQueryCommand>());
}

CliDispatcher::~CliDispatcher() = default;

bool CliDispatcher::shouldRunCli(const QStringList &arguments) const {
    const QStringList leadingPositionalArguments = extractLeadingPositionalArguments(arguments);
    if (leadingPositionalArguments.isEmpty()) {
        return false;
    }

    const QString rootCommandWord = leadingPositionalArguments.first();
    const QStringList knownRoots = rootCommandWords();
    return knownRoots.contains(rootCommandWord);
}

const CliCommand *CliDispatcher::findCommand(const QStringList &path) const {
    for (const std::unique_ptr<CliCommand> &command : m_commands) {
        if (command != nullptr && command->commandPath() == path) {
            return command.get();
        }
    }

    return nullptr;
}

QString CliDispatcher::renderGeneralHelp(const QString &programName, const QString &defaultDatabasePath) const {
    QStringList lines{
        QStringLiteral("Qodex CLI"),
        QString(),
        QStringLiteral("Usage:"),
        QStringLiteral("  %1 help [command...]").arg(programName),
        QStringLiteral("  %1 [--debug] [--data FILE] db query --sql <statement>").arg(programName),
        QString(),
        QStringLiteral("Global options:"),
        QStringLiteral("  --debug      Enable timestamped qodex debug logging to stdout"),
        QStringLiteral("  --data FILE  Path to the SQLite database file. Defaults to %1").arg(defaultDatabasePath),
        QString(),
        QStringLiteral("Commands:"),
    };

    for (const std::unique_ptr<CliCommand> &command : m_commands) {
        if (command == nullptr) {
            continue;
        }

        lines.append(
            QStringLiteral("  %1  %2")
                .arg(command->commandPath().join(QStringLiteral(" ")).leftJustified(12, QChar(u' ')), command->summary())
        );
    }

    lines.append(QString());
    lines.append(QStringLiteral("GUI:"));
    lines.append(QStringLiteral("  %1  Launch the Qodex desktop application").arg(programName));

    return lines.join(QStringLiteral("\n")) + QStringLiteral("\n");
}

QString CliDispatcher::renderCommandHelp(const CliCommand &command, const QString &programName) const {
    QStringList lines{
        QStringLiteral("Usage:"),
        QStringLiteral("  %1 %2").arg(programName, command.usage()),
        QString(),
        command.description(),
    };
    return lines.join(QStringLiteral("\n")) + QStringLiteral("\n");
}

int CliDispatcher::run(const int argc, char *argv[]) const {
    int applicationArgc = argc;
    QCoreApplication application(applicationArgc, argv);
    configureApplicationMetadata(application);

    QTextStream stdoutStream(stdout, QIODevice::WriteOnly);
    QTextStream stderrStream(stderr, QIODevice::WriteOnly);
    const QStringList rawArguments = rawArgumentsFromArgv(argc, argv).mid(1);

    GlobalOptions options;
    QStringList remainingArguments;
    QString errorMessage;
    if (!parseGlobalOptions(rawArguments, &options, &remainingArguments, &errorMessage)) {
        stderrStream << errorMessage << Qt::endl;
        stderrStream << renderGeneralHelp(QFileInfo(application.applicationFilePath()).fileName(), app::AppPaths::discover().databasePath);
        return 2;
    }

    if (options.debugEnabled) {
        qodex::debug::setEnabled(true);
        qodex::debug::installStdoutMessageHandler();
    }

    const app::AppPaths appPaths = app::AppPaths::discover(options.databasePathOverride);
    const QString programName = QFileInfo(application.applicationFilePath()).fileName();
    QODEBUG("CLI global options parsed", "databasePathOverride=", options.databasePathOverride, "debug=", options.debugEnabled);

    if (remainingArguments.isEmpty()) {
        stdoutStream << renderGeneralHelp(programName, appPaths.databasePath);
        return 0;
    }

    int consumedArgumentCount = 0;
    const CliCommand *command = matchCommand(remainingArguments, &consumedArgumentCount);
    if (command == nullptr) {
        stderrStream << QStringLiteral("Unknown qodex command: %1").arg(remainingArguments.join(QStringLiteral(" "))) << Qt::endl;
        stderrStream << QStringLiteral("See `%1 help`.").arg(programName) << Qt::endl;
        return 2;
    }

    QODEBUG("Dispatching CLI command", command->commandPath().join(QStringLiteral(" ")));

    return command->run(
        remainingArguments.mid(consumedArgumentCount),
        CliContext{
            .executableName = programName,
            .databasePath = appPaths.databasePath,
            .stdoutStream = &stdoutStream,
            .stderrStream = &stderrStream,
            .dispatcher = this,
        }
    );
}

QStringList CliDispatcher::extractLeadingPositionalArguments(const QStringList &arguments) const {
    QStringList positionalArguments;

    for (int index = 0; index < arguments.size(); ++index) {
        const QString &argument = arguments.at(index);
        if (isOptionWithValue(argument)) {
            ++index;
            continue;
        }
        if (isInlineDataOption(argument)) {
            continue;
        }
        if (isDebugOption(argument)) {
            continue;
        }
        if (argument == QStringLiteral("--")) {
            positionalArguments = arguments.mid(index + 1);
            break;
        }
        if (argument.startsWith(QLatin1Char('-'))) {
            break;
        }

        positionalArguments = arguments.mid(index);
        break;
    }

    return positionalArguments;
}

bool CliDispatcher::parseGlobalOptions(
    const QStringList &arguments,
    GlobalOptions *options,
    QStringList *remainingArguments,
    QString *errorMessage
) const {
    if (options != nullptr) {
        *options = GlobalOptions{};
    }
    if (remainingArguments != nullptr) {
        remainingArguments->clear();
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QStringList trailingArguments;

    for (int index = 0; index < arguments.size(); ++index) {
        const QString &argument = arguments.at(index);
        if (argument == QStringLiteral("--")) {
            trailingArguments = arguments.mid(index + 1);
            break;
        }
        if (argument == QStringLiteral("--data")) {
            if (index + 1 >= arguments.size()) {
                if (errorMessage != nullptr) {
                    *errorMessage = QStringLiteral("Missing value for --data.");
                }
                return false;
            }
            if (options != nullptr) {
                options->databasePathOverride = arguments.at(index + 1);
            }
            ++index;
            continue;
        }
        if (argument.startsWith(QStringLiteral("--data="))) {
            if (options != nullptr) {
                options->databasePathOverride = argument.sliced(QStringLiteral("--data=").size());
            }
            continue;
        }
        if (argument == QStringLiteral("--debug")) {
            if (options != nullptr) {
                options->debugEnabled = true;
            }
            continue;
        }

        trailingArguments = arguments.mid(index);
        break;
    }

    if (remainingArguments != nullptr) {
        *remainingArguments = trailingArguments;
    }
    return true;
}

const CliCommand *CliDispatcher::matchCommand(const QStringList &remainingArguments, int *consumedArgumentCount) const {
    const CliCommand *matchedCommand = nullptr;
    int matchedLength = 0;

    for (const std::unique_ptr<CliCommand> &command : m_commands) {
        if (command == nullptr) {
            continue;
        }

        const QStringList path = command->commandPath();
        if (path.size() > remainingArguments.size()) {
            continue;
        }

        bool matches = true;
        for (int index = 0; index < path.size(); ++index) {
            if (remainingArguments.at(index) != path.at(index)) {
                matches = false;
                break;
            }
        }

        if (matches && path.size() > matchedLength) {
            matchedCommand = command.get();
            matchedLength = path.size();
        }
    }

    if (consumedArgumentCount != nullptr) {
        *consumedArgumentCount = matchedLength;
    }
    return matchedCommand;
}

QStringList CliDispatcher::rootCommandWords() const {
    QStringList roots;
    for (const std::unique_ptr<CliCommand> &command : m_commands) {
        if (command == nullptr || command->commandPath().isEmpty()) {
            continue;
        }
        const QString root = command->commandPath().first();
        if (!roots.contains(root)) {
            roots.append(root);
        }
    }
    return roots;
}

}  // namespace qodex::cli
