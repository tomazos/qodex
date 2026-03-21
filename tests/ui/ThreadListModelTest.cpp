#include <QtTest>

#include "domain/ThreadStore.h"
#include "ui/ThreadListModel.h"

using qodex::domain::ThreadStore;
using qodex::domain::ThreadSummary;
using qodex::ui::ThreadListModel;

class ThreadListModelTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesExpectedColumns();
    void groupsThreadsByCwd();
    void placesArchivedGroupAtEnd();
    void sortsRowsByClickedColumnWithinGroup();
};

void ThreadListModelTest::exposesExpectedColumns() {
    ThreadStore store;
    store.replaceThreadSummaries({
        ThreadSummary{
            .id = QStringLiteral("a"),
            .title = QStringLiteral("Alpha"),
            .cwd = QStringLiteral("/tmp"),
            .createdAt = 100,
            .updatedAt = 200,
        },
    });

    ThreadListModel model(&store);

    QCOMPARE(model.columnCount(), ThreadListModel::ColumnCount);
    QCOMPARE(model.headerData(ThreadListModel::ThreadColumn, Qt::Horizontal).toString(), QStringLiteral("Thread"));
    QCOMPARE(model.headerData(ThreadListModel::CreatedColumn, Qt::Horizontal).toString(), QStringLiteral("Created"));
    QCOMPARE(model.headerData(ThreadListModel::ModifiedColumn, Qt::Horizontal).toString(), QStringLiteral("Modified"));
}

void ThreadListModelTest::groupsThreadsByCwd() {
    ThreadStore store;
    store.replaceThreadSummaries({
        ThreadSummary{
            .id = QStringLiteral("b"),
            .title = QStringLiteral("Beta"),
            .cwd = QStringLiteral("/home/zos/Downloads"),
            .createdAt = 200,
            .updatedAt = 300,
        },
        ThreadSummary{
            .id = QStringLiteral("a"),
            .title = QStringLiteral("Alpha"),
            .cwd = QStringLiteral("/home/zos"),
            .createdAt = 100,
            .updatedAt = 400,
        },
    });

    ThreadListModel model(&store);

    QCOMPARE(model.rowCount(), 3);

    const QModelIndex homeGroup = model.index(0, ThreadListModel::ThreadColumn);
    QCOMPARE(homeGroup.data(Qt::DisplayRole).toString(), QStringLiteral("/home/zos"));
    QCOMPARE(model.rowCount(homeGroup), 1);
    QCOMPARE(
        model.index(0, ThreadListModel::ThreadColumn, homeGroup).data(ThreadListModel::IdRole).toString(),
        QStringLiteral("a")
    );

    const QModelIndex downloadsGroup = model.index(1, ThreadListModel::ThreadColumn);
    QCOMPARE(downloadsGroup.data(Qt::DisplayRole).toString(), QStringLiteral("/home/zos/Downloads"));
    QCOMPARE(model.rowCount(downloadsGroup), 1);
    QCOMPARE(
        model.index(0, ThreadListModel::ThreadColumn, downloadsGroup).data(ThreadListModel::IdRole).toString(),
        QStringLiteral("b")
    );

    const QModelIndex archivedGroup = model.index(2, ThreadListModel::ThreadColumn);
    QCOMPARE(archivedGroup.data(Qt::DisplayRole).toString(), QStringLiteral("Archived"));
    QCOMPARE(model.rowCount(archivedGroup), 0);
}

void ThreadListModelTest::placesArchivedGroupAtEnd() {
    ThreadStore store;
    store.replaceThreadSummaries({
        ThreadSummary{
            .id = QStringLiteral("active"),
            .title = QStringLiteral("Active"),
            .cwd = QStringLiteral("/home/zos"),
            .createdAt = 100,
            .updatedAt = 200,
        },
    });
    store.replaceThreadSummaries({
        ThreadSummary{
            .id = QStringLiteral("archived"),
            .title = QStringLiteral("Archived Thread"),
            .cwd = QStringLiteral("/home/zos/archive"),
            .createdAt = 50,
            .updatedAt = 75,
        },
    }, true);

    ThreadListModel model(&store);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.index(0, ThreadListModel::ThreadColumn).data(Qt::DisplayRole).toString(), QStringLiteral("/home/zos"));

    const QModelIndex archivedRoot = model.index(1, ThreadListModel::ThreadColumn);
    QCOMPARE(archivedRoot.data(Qt::DisplayRole).toString(), QStringLiteral("Archived"));
    QCOMPARE(model.rowCount(archivedRoot), 1);

    const QModelIndex archivedCwd = model.index(0, ThreadListModel::ThreadColumn, archivedRoot);
    QCOMPARE(archivedCwd.data(Qt::DisplayRole).toString(), QStringLiteral("/home/zos/archive"));
    QCOMPARE(model.rowCount(archivedCwd), 1);
    QCOMPARE(
        model.index(0, ThreadListModel::ThreadColumn, archivedCwd).data(ThreadListModel::IdRole).toString(),
        QStringLiteral("archived")
    );
}

void ThreadListModelTest::sortsRowsByClickedColumnWithinGroup() {
    ThreadStore store;
    store.replaceThreadSummaries({
        ThreadSummary{
            .id = QStringLiteral("c"),
            .title = QStringLiteral("Gamma"),
            .cwd = QStringLiteral("/home/zos"),
            .createdAt = 300,
            .updatedAt = 150,
        },
        ThreadSummary{
            .id = QStringLiteral("a"),
            .title = QStringLiteral("Alpha"),
            .cwd = QStringLiteral("/home/zos"),
            .createdAt = 100,
            .updatedAt = 250,
        },
        ThreadSummary{
            .id = QStringLiteral("b"),
            .title = QStringLiteral("Beta"),
            .cwd = QStringLiteral("/home/zos"),
            .createdAt = 200,
            .updatedAt = 50,
        },
    });

    ThreadListModel model(&store);
    const QModelIndex groupIndex = model.index(0, ThreadListModel::ThreadColumn);

    QCOMPARE(groupIndex.data(Qt::DisplayRole).toString(), QStringLiteral("/home/zos"));
    QCOMPARE(model.index(0, ThreadListModel::ThreadColumn, groupIndex).data(ThreadListModel::IdRole).toString(), QStringLiteral("a"));

    model.sort(ThreadListModel::CreatedColumn, Qt::AscendingOrder);
    QCOMPARE(model.index(0, ThreadListModel::ThreadColumn, groupIndex).data(ThreadListModel::IdRole).toString(), QStringLiteral("a"));
    QCOMPARE(model.index(1, ThreadListModel::ThreadColumn, groupIndex).data(ThreadListModel::IdRole).toString(), QStringLiteral("b"));
    QCOMPARE(model.index(2, ThreadListModel::ThreadColumn, groupIndex).data(ThreadListModel::IdRole).toString(), QStringLiteral("c"));

    model.sort(ThreadListModel::ThreadColumn, Qt::DescendingOrder);
    QCOMPARE(model.index(0, ThreadListModel::ThreadColumn, groupIndex).data(ThreadListModel::IdRole).toString(), QStringLiteral("c"));
    QCOMPARE(model.index(1, ThreadListModel::ThreadColumn, groupIndex).data(ThreadListModel::IdRole).toString(), QStringLiteral("b"));
    QCOMPARE(model.index(2, ThreadListModel::ThreadColumn, groupIndex).data(ThreadListModel::IdRole).toString(), QStringLiteral("a"));
}

QTEST_GUILESS_MAIN(ThreadListModelTest)

#include "ThreadListModelTest.moc"
