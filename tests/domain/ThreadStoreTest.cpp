#include <QtTest>

#include "domain/ThreadStore.h"

using qodex::domain::ThreadStore;
using qodex::domain::ThreadSummary;

class ThreadStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void replacesAndSortsThreadList();
    void mergesActiveAndArchivedReplacements();
    void clearsSelectionWhenSelectedThreadDisappears();
};

void ThreadStoreTest::replacesAndSortsThreadList() {
    ThreadStore store;

    QList<ThreadSummary> summaries{
        ThreadSummary{.id = QStringLiteral("older"), .title = QStringLiteral("Older"), .updatedAt = 10},
        ThreadSummary{.id = QStringLiteral("newer"), .title = QStringLiteral("Newer"), .updatedAt = 20},
    };

    store.replaceThreadSummaries(summaries);

    const QList<ThreadSummary> snapshot = store.threadSummaries();
    QCOMPARE(snapshot.size(), 2);
    QCOMPARE(snapshot.at(0).id, QStringLiteral("newer"));
    QCOMPARE(snapshot.at(1).id, QStringLiteral("older"));
}

void ThreadStoreTest::mergesActiveAndArchivedReplacements() {
    ThreadStore store;

    store.replaceThreadSummaries({
        ThreadSummary{.id = QStringLiteral("active"), .title = QStringLiteral("Active"), .updatedAt = 20},
    });
    store.replaceThreadSummaries({
        ThreadSummary{.id = QStringLiteral("archived"), .title = QStringLiteral("Archived"), .updatedAt = 10},
    }, true);

    const QList<ThreadSummary> snapshot = store.threadSummaries();
    QCOMPARE(snapshot.size(), 2);
    QCOMPARE(snapshot.at(0).id, QStringLiteral("active"));
    QCOMPARE(snapshot.at(0).archived, false);
    QCOMPARE(snapshot.at(1).id, QStringLiteral("archived"));
    QCOMPARE(snapshot.at(1).archived, true);
}

void ThreadStoreTest::clearsSelectionWhenSelectedThreadDisappears() {
    ThreadStore store;

    store.replaceThreadSummaries({
        ThreadSummary{.id = QStringLiteral("a"), .title = QStringLiteral("A"), .updatedAt = 1},
        ThreadSummary{.id = QStringLiteral("b"), .title = QStringLiteral("B"), .updatedAt = 2},
    });
    store.setSelectedThreadId(QStringLiteral("a"));
    QCOMPARE(store.selectedThreadId(), QStringLiteral("a"));

    store.replaceThreadSummaries({
        ThreadSummary{.id = QStringLiteral("b"), .title = QStringLiteral("B"), .updatedAt = 2},
    });

    QVERIFY(store.selectedThreadId().isEmpty());
}

QTEST_GUILESS_MAIN(ThreadStoreTest)

#include "ThreadStoreTest.moc"
