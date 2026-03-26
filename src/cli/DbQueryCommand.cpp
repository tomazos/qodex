#include "cli/DbQueryCommand.h"

#include <sqlite3.h>

#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "cli/CliDispatcher.h"

namespace qodex::cli {

namespace {

QString decodeSqliteError(const char *message) {
    return message == nullptr ? QStringLiteral("Unknown SQLite error.") : QString::fromUtf8(message);
}

bool isWhitespaceOnlyTail(const QString &tail) {
    for (const QChar character : tail) {
        if (!character.isSpace()) {
            return false;
        }
    }
    return true;
}

}  // namespace

QStringList DbQueryCommand::commandPath() const {
    return {QStringLiteral("db"), QStringLiteral("query")};
}

QString DbQueryCommand::summary() const {
    return QStringLiteral("Execute a readonly SQL query against the qodex SQLite database.");
}

QString DbQueryCommand::usage() const {
    return QStringLiteral("[--data FILE] db query --sql <statement>");
}

QString DbQueryCommand::description() const {
    return QStringLiteral(
        "Open the qodex SQLite database in readonly mode and execute a single readonly SQL statement.\n"
        "The result is written as JSON with a `columns` array and a `rows` array."
    );
}

int DbQueryCommand::run(const QStringList &arguments, const CliContext &context) const {
    Q_ASSERT(context.stdoutStream != nullptr);
    Q_ASSERT(context.stderrStream != nullptr);
    Q_ASSERT(context.dispatcher != nullptr);

    if (arguments.contains(QStringLiteral("--help")) || arguments.contains(QStringLiteral("-h"))) {
        *context.stdoutStream << context.dispatcher->renderCommandHelp(*this, context.executableName);
        return 0;
    }

    QCommandLineParser parser;
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    QCommandLineOption sqlOption(
        QStringList{QStringLiteral("sql")},
        QStringLiteral("Readonly SQL statement to execute against the qodex SQLite database."),
        QStringLiteral("statement")
    );
    parser.addOption(sqlOption);

    const QStringList parserArguments = QStringList{QStringLiteral("qodex db query")} + arguments;
    if (!parser.parse(parserArguments)) {
        *context.stderrStream << parser.errorText() << Qt::endl;
        *context.stderrStream << context.dispatcher->renderCommandHelp(*this, context.executableName);
        return 2;
    }

    if (!parser.positionalArguments().isEmpty()) {
        *context.stderrStream << QStringLiteral("Unexpected positional arguments: %1")
                                     .arg(parser.positionalArguments().join(QStringLiteral(" ")))
                              << Qt::endl;
        *context.stderrStream << context.dispatcher->renderCommandHelp(*this, context.executableName);
        return 2;
    }

    const QString sql = parser.value(sqlOption).trimmed();
    if (sql.isEmpty()) {
        *context.stderrStream << QStringLiteral("The --sql option is required.") << Qt::endl;
        *context.stderrStream << context.dispatcher->renderCommandHelp(*this, context.executableName);
        return 2;
    }

    sqlite3 *database = nullptr;
    QString errorMessage;
    if (!openReadOnlyDatabase(context.databasePath, &database, &errorMessage)) {
        *context.stderrStream << errorMessage << Qt::endl;
        return 1;
    }

    sqlite3_stmt *statement = nullptr;
    if (!prepareSingleStatement(database, sql, &statement, &errorMessage)) {
        sqlite3_close(database);
        *context.stderrStream << errorMessage << Qt::endl;
        return 1;
    }

    QJsonObject resultObject;
    QJsonArray columnsArray;
    QJsonArray rowsArray;

    const int columnCount = sqlite3_column_count(statement);
    for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
        const char *columnName = sqlite3_column_name(statement, columnIndex);
        columnsArray.append(columnName == nullptr ? QString() : QString::fromUtf8(columnName));
    }

    int stepResult = SQLITE_ROW;
    while ((stepResult = sqlite3_step(statement)) == SQLITE_ROW) {
        QJsonArray rowArray;
        for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
            rowArray.append(columnToJson(statement, columnIndex));
        }
        rowsArray.append(rowArray);
    }

    if (stepResult != SQLITE_DONE) {
        errorMessage = decodeSqliteError(sqlite3_errmsg(database));
        sqlite3_finalize(statement);
        sqlite3_close(database);
        *context.stderrStream << errorMessage << Qt::endl;
        return 1;
    }

    resultObject.insert(QStringLiteral("columns"), columnsArray);
    resultObject.insert(QStringLiteral("rows"), rowsArray);

    sqlite3_finalize(statement);
    sqlite3_close(database);

    *context.stdoutStream << QString::fromUtf8(QJsonDocument(resultObject).toJson(QJsonDocument::Indented));
    context.stdoutStream->flush();
    return 0;
}

bool DbQueryCommand::openReadOnlyDatabase(const QString &databasePath, sqlite3 **database, QString *errorMessage) const {
    if (database != nullptr) {
        *database = nullptr;
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QFileInfo databaseInfo(databasePath);
    if (!databaseInfo.exists()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database file does not exist: %1").arg(databasePath);
        }
        return false;
    }

    const QByteArray encodedPath = QFile::encodeName(databasePath);
    sqlite3 *openedDatabase = nullptr;
    const int openResult = sqlite3_open_v2(encodedPath.constData(), &openedDatabase, SQLITE_OPEN_READONLY, nullptr);
    if (openResult != SQLITE_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = decodeSqliteError(sqlite3_errmsg(openedDatabase));
        }
        if (openedDatabase != nullptr) {
            sqlite3_close(openedDatabase);
        }
        return false;
    }

    sqlite3_busy_timeout(openedDatabase, 1000);
    if (database != nullptr) {
        *database = openedDatabase;
    }
    return true;
}

bool DbQueryCommand::prepareSingleStatement(
    sqlite3 *database,
    const QString &sql,
    sqlite3_stmt **statement,
    QString *errorMessage
) const {
    if (statement != nullptr) {
        *statement = nullptr;
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QByteArray sqlUtf8 = sql.toUtf8();
    const char *tail = nullptr;
    sqlite3_stmt *preparedStatement = nullptr;
    const int prepareResult = sqlite3_prepare_v2(database, sqlUtf8.constData(), sqlUtf8.size(), &preparedStatement, &tail);
    if (prepareResult != SQLITE_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = decodeSqliteError(sqlite3_errmsg(database));
        }
        if (preparedStatement != nullptr) {
            sqlite3_finalize(preparedStatement);
        }
        return false;
    }

    if (preparedStatement == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("The SQL statement was empty.");
        }
        return false;
    }

    if (tail != nullptr) {
        const QString tailText = QString::fromUtf8(tail);
        if (!isWhitespaceOnlyTail(tailText)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Only a single SQL statement is supported.");
            }
            sqlite3_finalize(preparedStatement);
            return false;
        }
    }

    if (sqlite3_stmt_readonly(preparedStatement) == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("qodex db query only accepts readonly SQL statements.");
        }
        sqlite3_finalize(preparedStatement);
        return false;
    }

    if (statement != nullptr) {
        *statement = preparedStatement;
    }
    return true;
}

QJsonValue DbQueryCommand::columnToJson(sqlite3_stmt *statement, const int columnIndex) const {
    switch (sqlite3_column_type(statement, columnIndex)) {
    case SQLITE_INTEGER:
        return static_cast<qint64>(sqlite3_column_int64(statement, columnIndex));
    case SQLITE_FLOAT:
        return sqlite3_column_double(statement, columnIndex);
    case SQLITE_TEXT: {
        const unsigned char *text = sqlite3_column_text(statement, columnIndex);
        return text == nullptr ? QJsonValue(QString()) : QJsonValue(QString::fromUtf8(reinterpret_cast<const char *>(text)));
    }
    case SQLITE_BLOB:
        return blobToHexLiteral(sqlite3_column_blob(statement, columnIndex), sqlite3_column_bytes(statement, columnIndex));
    case SQLITE_NULL:
        return QJsonValue(QJsonValue::Null);
    }

    return QJsonValue(QJsonValue::Null);
}

QString DbQueryCommand::blobToHexLiteral(const void *data, const int byteCount) const {
    if (data == nullptr || byteCount <= 0) {
        return QStringLiteral("x''");
    }

    const QByteArray bytes(static_cast<const char *>(data), byteCount);
    return QStringLiteral("x'%1'").arg(QString::fromLatin1(bytes.toHex().toUpper()));
}

}  // namespace qodex::cli
