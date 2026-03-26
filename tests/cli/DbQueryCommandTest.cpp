#include <QtTest>

#include <sqlite3.h>

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTextStream>

#include "cli/CliDispatcher.h"

namespace {

bool executeSql(sqlite3 *database, const char *sql, QString *errorMessage = nullptr) {
    char *sqliteError = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &sqliteError);
    if (result == SQLITE_OK) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = sqliteError == nullptr ? QStringLiteral("Unknown SQLite error.") : QString::fromUtf8(sqliteError);
    }
    sqlite3_free(sqliteError);
    return false;
}

}  // namespace

class DbQueryCommandTest final : public QObject {
    Q_OBJECT

private slots:
    void executesReadonlyQueryAndPrintsJson();
    void rejectsWritableStatements();
};

void DbQueryCommandTest::executesReadonlyQueryAndPrintsJson() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString databasePath = QDir(temporaryDir.path()).filePath(QStringLiteral("qodex.sqlite3"));
    sqlite3 *database = nullptr;
    QCOMPARE(sqlite3_open(databasePath.toUtf8().constData(), &database), SQLITE_OK);
    const auto closeDatabase = qScopeGuard([&database] {
        if (database != nullptr) {
            sqlite3_close(database);
            database = nullptr;
        }
    });

    QString errorMessage;
    QVERIFY2(executeSql(database, "CREATE TABLE demo(id INTEGER PRIMARY KEY, name TEXT, note BLOB);", &errorMessage), qPrintable(errorMessage));
    QVERIFY2(
        executeSql(database, "INSERT INTO demo(id, name, note) VALUES (1, 'Alice', X'414243'), (2, NULL, NULL);", &errorMessage),
        qPrintable(errorMessage)
    );

    const qodex::cli::CliDispatcher dispatcher;
    const qodex::cli::CliCommand *command = dispatcher.findCommand({QStringLiteral("db"), QStringLiteral("query")});
    QVERIFY(command != nullptr);

    QByteArray stdoutBytes;
    QBuffer stdoutBuffer(&stdoutBytes);
    QVERIFY(stdoutBuffer.open(QIODevice::WriteOnly));
    QTextStream stdoutStream(&stdoutBuffer);

    QByteArray stderrBytes;
    QBuffer stderrBuffer(&stderrBytes);
    QVERIFY(stderrBuffer.open(QIODevice::WriteOnly));
    QTextStream stderrStream(&stderrBuffer);

    const int exitCode = command->run(
        {QStringLiteral("--sql"), QStringLiteral("SELECT id, name, note FROM demo ORDER BY id ASC;")},
        qodex::cli::CliContext{
            .executableName = QStringLiteral("qodex"),
            .databasePath = databasePath,
            .stdoutStream = &stdoutStream,
            .stderrStream = &stderrStream,
            .dispatcher = &dispatcher,
        }
    );

    stdoutStream.flush();
    stderrStream.flush();

    QCOMPARE(exitCode, 0);
    QVERIFY(QString::fromUtf8(stderrBytes).isEmpty());

    const QJsonDocument jsonDocument = QJsonDocument::fromJson(stdoutBytes);
    QVERIFY(jsonDocument.isObject());

    const QJsonObject root = jsonDocument.object();
    const QJsonArray columns = root.value(QStringLiteral("columns")).toArray();
    const QJsonArray rows = root.value(QStringLiteral("rows")).toArray();

    QCOMPARE(columns.size(), 3);
    QCOMPARE(columns.at(0).toString(), QStringLiteral("id"));
    QCOMPARE(columns.at(1).toString(), QStringLiteral("name"));
    QCOMPARE(columns.at(2).toString(), QStringLiteral("note"));

    QCOMPARE(rows.size(), 2);
    const QJsonArray firstRow = rows.at(0).toArray();
    QCOMPARE(firstRow.at(0).toInteger(), qint64{1});
    QCOMPARE(firstRow.at(1).toString(), QStringLiteral("Alice"));
    QCOMPARE(firstRow.at(2).toString(), QStringLiteral("x'414243'"));

    const QJsonArray secondRow = rows.at(1).toArray();
    QCOMPARE(secondRow.at(0).toInteger(), qint64{2});
    QVERIFY(secondRow.at(1).isNull());
    QVERIFY(secondRow.at(2).isNull());
}

void DbQueryCommandTest::rejectsWritableStatements() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString databasePath = QDir(temporaryDir.path()).filePath(QStringLiteral("qodex.sqlite3"));
    sqlite3 *database = nullptr;
    QCOMPARE(sqlite3_open(databasePath.toUtf8().constData(), &database), SQLITE_OK);
    const auto closeDatabase = qScopeGuard([&database] {
        if (database != nullptr) {
            sqlite3_close(database);
            database = nullptr;
        }
    });

    QString errorMessage;
    QVERIFY2(executeSql(database, "CREATE TABLE demo(id INTEGER PRIMARY KEY, name TEXT);", &errorMessage), qPrintable(errorMessage));
    QVERIFY2(executeSql(database, "INSERT INTO demo(id, name) VALUES (1, 'Alice');", &errorMessage), qPrintable(errorMessage));

    const qodex::cli::CliDispatcher dispatcher;
    const qodex::cli::CliCommand *command = dispatcher.findCommand({QStringLiteral("db"), QStringLiteral("query")});
    QVERIFY(command != nullptr);

    QByteArray stdoutBytes;
    QBuffer stdoutBuffer(&stdoutBytes);
    QVERIFY(stdoutBuffer.open(QIODevice::WriteOnly));
    QTextStream stdoutStream(&stdoutBuffer);

    QByteArray stderrBytes;
    QBuffer stderrBuffer(&stderrBytes);
    QVERIFY(stderrBuffer.open(QIODevice::WriteOnly));
    QTextStream stderrStream(&stderrBuffer);

    const int exitCode = command->run(
        {QStringLiteral("--sql"), QStringLiteral("UPDATE demo SET name = 'Bob' WHERE id = 1;")},
        qodex::cli::CliContext{
            .executableName = QStringLiteral("qodex"),
            .databasePath = databasePath,
            .stdoutStream = &stdoutStream,
            .stderrStream = &stderrStream,
            .dispatcher = &dispatcher,
        }
    );

    stdoutStream.flush();
    stderrStream.flush();

    QCOMPARE(exitCode, 1);
    QVERIFY(QString::fromUtf8(stdoutBytes).isEmpty());
    QVERIFY(QString::fromUtf8(stderrBytes).contains(QStringLiteral("only accepts readonly SQL")));

    sqlite3_stmt *verifyStatement = nullptr;
    QCOMPARE(
        sqlite3_prepare_v2(database, "SELECT name FROM demo WHERE id = 1;", -1, &verifyStatement, nullptr),
        SQLITE_OK
    );
    const auto finalizeVerifyStatement = qScopeGuard([&verifyStatement] {
        if (verifyStatement != nullptr) {
            sqlite3_finalize(verifyStatement);
            verifyStatement = nullptr;
        }
    });

    QCOMPARE(sqlite3_step(verifyStatement), SQLITE_ROW);
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(verifyStatement, 0))), QStringLiteral("Alice"));
}

QTEST_GUILESS_MAIN(DbQueryCommandTest)

#include "DbQueryCommandTest.moc"
