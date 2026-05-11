#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <QUuid>

#include "codex/AppServerTransport.h"
#include "codex/TrafficLogger.h"
#include "storage/DatabaseManager.h"

using qodex::codex::AppServerTransport;
using qodex::codex::JsonRpcErrorResponseMessage;
using qodex::codex::JsonRpcResponseMessage;
using qodex::codex::TrafficLogger;
using qodex::storage::DatabaseManager;

namespace {

struct ApiLogRow {
    qint64 id = 0;
    QString sessionId;
    QString direction;
    QString messageKind;
    QString method;
    QString jsonrpcId;
    QString correlationId;
    QString threadId;
    QVariant success;
    QVariant latencyMs;
    QString payloadJson;
    QString summaryText;
};

QString describeRows(const QList<ApiLogRow> &rows, const QString &errorMessage) {
    QStringList lines;
    lines.append(QStringLiteral("error=%1 rows=%2").arg(errorMessage, QString::number(rows.size())));
    for (const ApiLogRow &row : rows) {
        lines.append(QStringLiteral("#%1 %2 %3 %4 id=%5")
            .arg(QString::number(row.id), row.direction, row.messageKind, row.method, row.jsonrpcId));
    }
    return lines.join(QLatin1Char('\n'));
}

int findRowIndex(
    const QList<ApiLogRow> &rows,
    const QString &direction,
    const QString &messageKind,
    const QString &method,
    const QString &jsonrpcId = QString(),
    bool matchJsonrpcId = false
) {
    for (qsizetype index = 0; index < rows.size(); ++index) {
        const ApiLogRow &row = rows.at(index);
        if (row.direction != direction || row.messageKind != messageKind || row.method != method) {
            continue;
        }
        if (matchJsonrpcId && row.jsonrpcId != jsonrpcId) {
            continue;
        }
        return static_cast<int>(index);
    }
    return -1;
}

QString findCodexExecutable() {
    return QStandardPaths::findExecutable(QStringLiteral("codex"));
}

QList<ApiLogRow> loadApiLogRows(const QString &databasePath, QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QString connectionName =
        QStringLiteral("TrafficLoggerTest.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    QList<ApiLogRow> rows;
    bool opened = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        opened = database.open();
        if (!opened) {
            if (errorMessage != nullptr) {
                *errorMessage = database.lastError().text();
            }
        } else {
            QSqlQuery query(database);
            if (!query.exec(QStringLiteral(
                    "SELECT id, session_id, direction, message_kind, method, jsonrpc_id, correlation_id, thread_id, "
                    "success, latency_ms, payload_json, summary_text FROM api_log ORDER BY id ASC;"
                ))) {
                if (errorMessage != nullptr) {
                    *errorMessage = query.lastError().text();
                }
            } else {
                while (query.next()) {
                    rows.append(ApiLogRow{
                        .id = query.value(0).toLongLong(),
                        .sessionId = query.value(1).toString(),
                        .direction = query.value(2).toString(),
                        .messageKind = query.value(3).toString(),
                        .method = query.value(4).toString(),
                        .jsonrpcId = query.value(5).toString(),
                        .correlationId = query.value(6).toString(),
                        .threadId = query.value(7).toString(),
                        .success = query.value(8),
                        .latencyMs = query.value(9),
                        .payloadJson = query.value(10).toString(),
                        .summaryText = query.value(11).toString(),
                    });
                }
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return rows;
}

int countApiLogFtsMatches(const QString &databasePath, const QString &matchExpression, QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QString connectionName =
        QStringLiteral("TrafficLoggerTestFts.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    int count = -1;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (!database.open()) {
            if (errorMessage != nullptr) {
                *errorMessage = database.lastError().text();
            }
        } else {
            QSqlQuery query(database);
            query.prepare(QStringLiteral("SELECT COUNT(*) FROM api_log_fts WHERE api_log_fts MATCH ?;"));
            query.addBindValue(matchExpression);
            if (!query.exec()) {
                if (errorMessage != nullptr) {
                    *errorMessage = query.lastError().text();
                }
            } else if (query.next()) {
                count = query.value(0).toInt();
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return count;
}

}  // namespace

class TrafficLoggerTest final : public QObject {
    Q_OBJECT

private slots:
    void logsInboundAndOutboundJsonRpcTraffic();
};

void TrafficLoggerTest::logsInboundAndOutboundJsonRpcTraffic() {
    const QString codex = findCodexExecutable();
    if (codex.isEmpty()) {
        QSKIP("codex executable was not found on PATH");
    }

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString databasePath = temporaryDir.filePath(QStringLiteral("qodex.sqlite3"));
    QString errorMessage;

    DatabaseManager databaseManager(databasePath);
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));

    AppServerTransport transport;
    TrafficLogger trafficLogger(&databaseManager, &transport);
    QSignalSpy startedSpy(&transport, &AppServerTransport::started);
    QSignalSpy transportErrorSpy(&transport, &AppServerTransport::transportError);

    transport.start(codex, QStringList{QStringLiteral("app-server")});
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 3000);

    bool initializeSuccessCalled = false;
    bool initializeErrorCalled = false;
    JsonRpcResponseMessage initializeResponse;

    const auto initializeId = transport.sendRequest(
        QStringLiteral("initialize"),
        QJsonObject{
            {QStringLiteral("clientInfo"), QJsonObject{
                {QStringLiteral("name"), QStringLiteral("qodex-test")},
                {QStringLiteral("version"), QStringLiteral("0.1")},
            }},
            {QStringLiteral("capabilities"), QJsonObject{
                {QStringLiteral("experimentalApi"), true},
            }},
        },
        &transport,
        [&](const JsonRpcResponseMessage &response) {
            initializeSuccessCalled = true;
            initializeResponse = response;
        },
        [&](const JsonRpcErrorResponseMessage &) {
            initializeErrorCalled = true;
        }
    );

    QVERIFY(initializeId.isValid());
    QTRY_VERIFY_WITH_TIMEOUT(initializeSuccessCalled, 5000);
    QVERIFY(!initializeErrorCalled);
    QCOMPARE(initializeResponse.id.toKey(), initializeId.toKey());

    QVERIFY(transport.sendNotification(QStringLiteral("initialized")));

    bool mockSuccessCalled = false;
    bool mockErrorCalled = false;
    JsonRpcResponseMessage mockResponse;

    const auto mockRequestId = transport.sendRequest(
        QStringLiteral("mock/experimentalMethod"),
        QJsonObject{
            {QStringLiteral("value"), QStringLiteral("hello")},
        },
        &transport,
        [&](const JsonRpcResponseMessage &response) {
            mockSuccessCalled = true;
            mockResponse = response;
        },
        [&](const JsonRpcErrorResponseMessage &) {
            mockErrorCalled = true;
        }
    );

    QVERIFY(mockRequestId.isValid());
    QTRY_VERIFY_WITH_TIMEOUT(mockSuccessCalled, 5000);
    QVERIFY(!mockErrorCalled);
    QCOMPARE(
        mockResponse.result.toObject().value(QStringLiteral("echoed")).toString(),
        QStringLiteral("hello")
    );

    QList<ApiLogRow> rows;
    const bool expectedRowsLogged = QTest::qWaitFor([&]() {
        rows = loadApiLogRows(databasePath, &errorMessage);
        return errorMessage.isEmpty() &&
            findRowIndex(
                rows,
                QStringLiteral("inbound"),
                QStringLiteral("response"),
                QStringLiteral("mock/experimentalMethod"),
                QStringLiteral("client-2"),
                true
            ) >= 0;
    }, 5000);
    QVERIFY2(expectedRowsLogged, qPrintable(describeRows(rows, errorMessage)));

    QCOMPARE(transportErrorSpy.count(), 0);
    QVERIFY2(rows.size() >= 5, qPrintable(describeRows(rows, errorMessage)));

    const QString sessionId = trafficLogger.sessionId();
    QVERIFY(!sessionId.isEmpty());
    for (const ApiLogRow &row : rows) {
        QCOMPARE(row.sessionId, sessionId);
    }

    const int initializeRequestIndex = findRowIndex(
        rows,
        QStringLiteral("outbound"),
        QStringLiteral("request"),
        QStringLiteral("initialize"),
        QStringLiteral("client-1"),
        true
    );
    const int initializeResponseIndex = findRowIndex(
        rows,
        QStringLiteral("inbound"),
        QStringLiteral("response"),
        QStringLiteral("initialize"),
        QStringLiteral("client-1"),
        true
    );
    const int initializedNotificationIndex = findRowIndex(
        rows,
        QStringLiteral("outbound"),
        QStringLiteral("notification"),
        QStringLiteral("initialized")
    );
    const int mockRequestIndex = findRowIndex(
        rows,
        QStringLiteral("outbound"),
        QStringLiteral("request"),
        QStringLiteral("mock/experimentalMethod"),
        QStringLiteral("client-2"),
        true
    );
    const int mockResponseIndex = findRowIndex(
        rows,
        QStringLiteral("inbound"),
        QStringLiteral("response"),
        QStringLiteral("mock/experimentalMethod"),
        QStringLiteral("client-2"),
        true
    );

    QVERIFY2(initializeRequestIndex >= 0, qPrintable(describeRows(rows, errorMessage)));
    QVERIFY2(initializeResponseIndex >= 0, qPrintable(describeRows(rows, errorMessage)));
    QVERIFY2(initializedNotificationIndex >= 0, qPrintable(describeRows(rows, errorMessage)));
    QVERIFY2(mockRequestIndex >= 0, qPrintable(describeRows(rows, errorMessage)));
    QVERIFY2(mockResponseIndex >= 0, qPrintable(describeRows(rows, errorMessage)));
    QVERIFY(initializeRequestIndex < initializeResponseIndex);
    QVERIFY(initializeResponseIndex < mockRequestIndex);
    QVERIFY(mockRequestIndex < mockResponseIndex);

    const ApiLogRow &initializeRequest = rows.at(initializeRequestIndex);
    QVERIFY(initializeRequest.success.isNull());
    QVERIFY(initializeRequest.latencyMs.isNull());

    const ApiLogRow &initializeResponseRow = rows.at(initializeResponseIndex);
    QCOMPARE(initializeResponseRow.correlationId, QStringLiteral("client-1"));
    QCOMPARE(initializeResponseRow.success.toInt(), 1);
    QVERIFY(initializeResponseRow.latencyMs.isValid());
    QVERIFY(initializeResponseRow.latencyMs.toLongLong() >= 0);

    const ApiLogRow &initializedNotification = rows.at(initializedNotificationIndex);
    QVERIFY(initializedNotification.jsonrpcId.isEmpty());

    const ApiLogRow &mockRequest = rows.at(mockRequestIndex);
    QVERIFY(mockRequest.payloadJson.contains(QStringLiteral("\"value\":\"hello\"")));

    const ApiLogRow &mockResponseRow = rows.at(mockResponseIndex);
    QCOMPARE(mockResponseRow.success.toInt(), 1);
    QVERIFY(mockResponseRow.payloadJson.contains(QStringLiteral("\"echoed\":\"hello\"")));
    QVERIFY(mockResponseRow.summaryText.contains(QStringLiteral("ok")));

    const int ftsMatches = countApiLogFtsMatches(databasePath, QStringLiteral("hello"), &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(ftsMatches, 2);

    transport.stop();
}

QTEST_GUILESS_MAIN(TrafficLoggerTest)

#include "TrafficLoggerTest.moc"
