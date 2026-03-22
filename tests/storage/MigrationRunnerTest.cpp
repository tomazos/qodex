#include <QtTest>

#include <sqlite3.h>

#include <QTemporaryDir>

#include "storage/MigrationRunner.h"

using qodex::storage::MigrationRunner;

namespace {

bool tableExists(sqlite3 *database, const QString &tableName) {
    sqlite3_stmt *statement = nullptr;
    const QByteArray sql = "SELECT 1 FROM sqlite_master WHERE type IN ('table', 'view') AND name = ? LIMIT 1;";
    if (sqlite3_prepare_v2(database, sql.constData(), -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(statement, 1, tableName.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    const bool exists = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return exists;
}

int querySingleInt(sqlite3 *database, const QString &sql) {
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(database, sql.toUtf8().constData(), -1, &statement, nullptr) != SQLITE_OK) {
        return -1;
    }

    int value = -1;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        value = sqlite3_column_int(statement, 0);
    }
    sqlite3_finalize(statement);
    return value;
}

}  // namespace

class MigrationRunnerTest final : public QObject {
    Q_OBJECT

private slots:
    void migratesFreshDatabaseToHead();
    void rerunAtHeadIsNoOp();
};

void MigrationRunnerTest::migratesFreshDatabaseToHead() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString databasePath = temporaryDir.filePath(QStringLiteral("qodex.sqlite3"));
    QString errorMessage;
    QVERIFY2(MigrationRunner::migrate(databasePath, &errorMessage), qPrintable(errorMessage));

    sqlite3 *database = nullptr;
    QCOMPARE(sqlite3_open_v2(databasePath.toUtf8().constData(), &database, SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    QVERIFY(database != nullptr);

    QCOMPARE(querySingleInt(database, QStringLiteral("PRAGMA user_version;")), MigrationRunner::headVersion());
    QCOMPARE(
        querySingleInt(database, QStringLiteral("SELECT COUNT(*) FROM schema_migrations;")),
        MigrationRunner::headVersion()
    );
    QVERIFY(tableExists(database, QStringLiteral("settings")));
    QVERIFY(tableExists(database, QStringLiteral("window_state")));
    QVERIFY(tableExists(database, QStringLiteral("view_state")));
    QVERIFY(tableExists(database, QStringLiteral("thread_metadata")));
    QVERIFY(tableExists(database, QStringLiteral("api_log")));
    QVERIFY(tableExists(database, QStringLiteral("api_log_fts")));

    sqlite3_close(database);
}

void MigrationRunnerTest::rerunAtHeadIsNoOp() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString databasePath = temporaryDir.filePath(QStringLiteral("qodex.sqlite3"));
    QString errorMessage;
    QVERIFY2(MigrationRunner::migrate(databasePath, &errorMessage), qPrintable(errorMessage));

    errorMessage.clear();
    QVERIFY2(MigrationRunner::migrate(databasePath, &errorMessage), qPrintable(errorMessage));

    sqlite3 *database = nullptr;
    QCOMPARE(sqlite3_open_v2(databasePath.toUtf8().constData(), &database, SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    QVERIFY(database != nullptr);

    QCOMPARE(
        querySingleInt(database, QStringLiteral("SELECT COUNT(*) FROM schema_migrations;")),
        MigrationRunner::headVersion()
    );

    sqlite3_close(database);
}

QTEST_GUILESS_MAIN(MigrationRunnerTest)

#include "MigrationRunnerTest.moc"
