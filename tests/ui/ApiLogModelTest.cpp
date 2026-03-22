#include <QtTest>

#include <QFont>
#include <QTemporaryDir>

#include "storage/DatabaseManager.h"
#include "ui/ApiLogModel.h"

using qodex::storage::ApiLogRecord;
using qodex::storage::DatabaseManager;
using qodex::ui::ApiLogModel;

class ApiLogModelTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesExpectedColumns();
    void usesSlidingCacheAndSorts();
};

void ApiLogModelTest::exposesExpectedColumns() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    QString errorMessage;
    DatabaseManager databaseManager(temporaryDir.filePath(QStringLiteral("qodex.sqlite3")));
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));

    ApiLogModel model(&databaseManager);

    QCOMPARE(model.columnCount(), ApiLogModel::ColumnCount);
    QCOMPARE(model.headerData(ApiLogModel::TimeColumn, Qt::Horizontal).toString(), QStringLiteral("Time (UTC)"));
    QCOMPARE(model.headerData(ApiLogModel::DirectionColumn, Qt::Horizontal).toString(), QStringLiteral("Direction"));
    QCOMPARE(model.headerData(ApiLogModel::KindColumn, Qt::Horizontal).toString(), QStringLiteral("Kind"));
    QCOMPARE(model.headerData(ApiLogModel::MethodColumn, Qt::Horizontal).toString(), QStringLiteral("Method"));
    QCOMPARE(model.headerData(ApiLogModel::SuccessColumn, Qt::Horizontal).toString(), QStringLiteral("Success"));
    QCOMPARE(model.headerData(ApiLogModel::LatencyColumn, Qt::Horizontal).toString(), QStringLiteral("Latency (ms)"));
    QCOMPARE(model.headerData(ApiLogModel::SummaryColumn, Qt::Horizontal).toString(), QStringLiteral("Summary"));
}

void ApiLogModelTest::usesSlidingCacheAndSorts() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    QString errorMessage;
    DatabaseManager databaseManager(temporaryDir.filePath(QStringLiteral("qodex.sqlite3")));
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));

    for (int index = 0; index < 1500; ++index) {
        QVERIFY2(
            databaseManager.appendApiLog(
                ApiLogRecord{
                    .sessionId = QStringLiteral("session"),
                    .direction = index % 2 == 0 ? QStringLiteral("outbound") : QStringLiteral("inbound"),
                    .messageKind = QStringLiteral("request"),
                    .method = QStringLiteral("method-%1").arg(index),
                    .jsonrpcId = QStringLiteral("client-%1").arg(index),
                    .correlationId = QStringLiteral("client-%1").arg(index),
                    .threadId = QStringLiteral("thread-%1").arg(index),
                    .success = index % 3 == 0 ? std::optional<bool>(true) : std::optional<bool>(false),
                    .latencyMs = index,
                    .payloadJson = QStringLiteral("{\"value\":%1}").arg(index),
                    .summaryText = QStringLiteral("summary-%1").arg(index),
                },
                &errorMessage
            ),
            qPrintable(errorMessage)
        );
    }

    ApiLogModel model(&databaseManager);

    QCOMPARE(model.rowCount(), 1500);
    QVERIFY(!model.canFetchMore(QModelIndex{}));
    QCOMPARE(
        model.index(0, ApiLogModel::SummaryColumn).data(Qt::DisplayRole).toString(),
        QStringLiteral("summary-1499")
    );
    QCOMPARE(model.index(0, ApiLogModel::LatencyColumn).data(Qt::DisplayRole).toString(), QStringLiteral("1499"));
    QVERIFY(model.index(1200, ApiLogModel::SummaryColumn).data(Qt::DisplayRole).toString().isEmpty());

    const QFont timeFont = qvariant_cast<QFont>(model.index(0, ApiLogModel::TimeColumn).data(Qt::FontRole));
    const QFont idFont = qvariant_cast<QFont>(model.index(0, ApiLogModel::JsonRpcIdColumn).data(Qt::FontRole));
    QVERIFY(timeFont.fixedPitch());
    QVERIFY(idFont.fixedPitch());

    model.ensureRowsCached(1200, 1230);
    QCOMPARE(
        model.index(1200, ApiLogModel::SummaryColumn).data(Qt::DisplayRole).toString(),
        QStringLiteral("summary-299")
    );
    QVERIFY(model.index(0, ApiLogModel::SummaryColumn).data(Qt::DisplayRole).toString().isEmpty());

    model.sort(ApiLogModel::MethodColumn, Qt::AscendingOrder);
    model.ensureRowsCached(0, 20);
    QCOMPARE(model.index(0, ApiLogModel::MethodColumn).data(Qt::DisplayRole).toString(), QStringLiteral("method-0"));
    QCOMPARE(model.index(0, ApiLogModel::SummaryColumn).data(Qt::DisplayRole).toString(), QStringLiteral("summary-0"));
}

QTEST_GUILESS_MAIN(ApiLogModelTest)

#include "ApiLogModelTest.moc"
