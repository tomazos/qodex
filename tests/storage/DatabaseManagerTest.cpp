#include <QtTest>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "storage/DatabaseManager.h"

using qodex::storage::ApiLogRecord;
using qodex::storage::DatabaseManager;
using qodex::storage::ViewStateRecord;
using qodex::storage::WindowStateRecord;

class DatabaseManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void persistsWindowAndViewState();
    void persistsSettings();
    void appendsApiLogRows();
};

void DatabaseManagerTest::persistsWindowAndViewState() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString databasePath = temporaryDir.filePath(QStringLiteral("qodex.sqlite3"));
    QString errorMessage;

    {
        DatabaseManager databaseManager(databasePath);
        QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));

        const QList<WindowStateRecord> windowStates{
            {
                QStringLiteral("qodex.MainWindow.Primary"),
                QByteArray::fromHex("010203"),
                QByteArray::fromHex("0a0b0c"),
            },
            {
                QStringLiteral("qodex.MainWindow.2"),
                QByteArray::fromHex("040506"),
                QByteArray{},
            },
        };
        QVERIFY2(databaseManager.replaceWindowStates(windowStates, &errorMessage), qPrintable(errorMessage));

        const QList<ViewStateRecord> viewStates{
            {
                QStringLiteral("qodex.MainWindow.Primary/thread-list-header"),
                QByteArray::fromHex("a1b2c3"),
            },
        };
        QVERIFY2(databaseManager.replaceViewStates(viewStates, &errorMessage), qPrintable(errorMessage));
    }

    {
        DatabaseManager databaseManager(databasePath);
        QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));

        const QList<WindowStateRecord> windowStates = databaseManager.loadWindowStates(&errorMessage);
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
        QCOMPARE(windowStates.size(), 2);
        QCOMPARE(windowStates.at(0).windowKey, QStringLiteral("qodex.MainWindow.2"));
        QCOMPARE(windowStates.at(0).geometry, QByteArray::fromHex("040506"));
        QCOMPARE(windowStates.at(0).layout, QByteArray{});
        QCOMPARE(windowStates.at(1).windowKey, QStringLiteral("qodex.MainWindow.Primary"));
        QCOMPARE(windowStates.at(1).geometry, QByteArray::fromHex("010203"));
        QCOMPARE(windowStates.at(1).layout, QByteArray::fromHex("0a0b0c"));

        const auto viewState =
            databaseManager.loadViewState(QStringLiteral("qodex.MainWindow.Primary/thread-list-header"), &errorMessage);
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
        QVERIFY(viewState.has_value());
        QCOMPARE(*viewState, QByteArray::fromHex("a1b2c3"));
    }
}

void DatabaseManagerTest::persistsSettings() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString databasePath = temporaryDir.filePath(QStringLiteral("qodex.sqlite3"));
    QString errorMessage;

    DatabaseManager databaseManager(databasePath);
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));
    QVERIFY2(
        databaseManager.saveSetting(QStringLiteral("ui.theme"), QStringLiteral("\"system\""), &errorMessage),
        qPrintable(errorMessage)
    );

    const auto setting = databaseManager.loadSetting(QStringLiteral("ui.theme"), &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY(setting.has_value());
    QCOMPARE(*setting, QStringLiteral("\"system\""));
}

void DatabaseManagerTest::appendsApiLogRows() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString databasePath = temporaryDir.filePath(QStringLiteral("qodex.sqlite3"));
    QString errorMessage;

    {
        DatabaseManager databaseManager(databasePath);
        QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));
        QVERIFY2(
            databaseManager.appendApiLog(
                ApiLogRecord{
                    .sessionId = QStringLiteral("session-1"),
                    .direction = QStringLiteral("outbound"),
                    .messageKind = QStringLiteral("request"),
                    .method = QStringLiteral("initialize"),
                    .jsonrpcId = QStringLiteral("client-1"),
                    .correlationId = QStringLiteral("client-1"),
                    .threadId = QStringLiteral("thread-1"),
                    .success = true,
                    .latencyMs = 42,
                    .payloadJson = QStringLiteral(R"({"jsonrpc":"2.0","method":"initialize"})"),
                    .summaryText = QStringLiteral("outbound request initialize id=client-1 ok 42ms"),
                },
                &errorMessage
            ),
            qPrintable(errorMessage)
        );
    }

    const QString connectionName =
        QStringLiteral("DatabaseManagerTest.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY2(database.open(), qPrintable(database.lastError().text()));

        QSqlQuery query(database);
        QVERIFY2(
            query.exec(QStringLiteral(
                "SELECT session_id, direction, message_kind, method, jsonrpc_id, thread_id, success, latency_ms, "
                "payload_json, summary_text FROM api_log;"
            )),
            qPrintable(query.lastError().text())
        );
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("session-1"));
        QCOMPARE(query.value(1).toString(), QStringLiteral("outbound"));
        QCOMPARE(query.value(2).toString(), QStringLiteral("request"));
        QCOMPARE(query.value(3).toString(), QStringLiteral("initialize"));
        QCOMPARE(query.value(4).toString(), QStringLiteral("client-1"));
        QCOMPARE(query.value(5).toString(), QStringLiteral("thread-1"));
        QCOMPARE(query.value(6).toInt(), 1);
        QCOMPARE(query.value(7).toLongLong(), 42);
        QCOMPARE(query.value(8).toString(), QStringLiteral(R"({"jsonrpc":"2.0","method":"initialize"})"));
        QCOMPARE(query.value(9).toString(), QStringLiteral("outbound request initialize id=client-1 ok 42ms"));
        QVERIFY(!query.next());

        QSqlQuery ftsQuery(database);
        QVERIFY2(
            ftsQuery.exec(QStringLiteral("SELECT rowid FROM api_log_fts WHERE api_log_fts MATCH 'initialize';")),
            qPrintable(ftsQuery.lastError().text())
        );
        QVERIFY(ftsQuery.next());
        QCOMPARE(ftsQuery.value(0).toInt(), 1);
        QVERIFY(!ftsQuery.next());

        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

QTEST_GUILESS_MAIN(DatabaseManagerTest)

#include "DatabaseManagerTest.moc"
