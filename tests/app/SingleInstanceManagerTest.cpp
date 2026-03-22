#include <QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>

#include "app/SingleInstanceManager.h"

using qodex::app::SingleInstanceManager;

class SingleInstanceManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void activatesExistingPrimaryInstance();
    void allowsIndependentScopes();
};

void SingleInstanceManagerTest::activatesExistingPrimaryInstance() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString scopeKey = temporaryDir.filePath(QStringLiteral("qodex.sqlite3"));

    SingleInstanceManager primary(scopeKey);
    QVERIFY(primary.startPrimaryOrActivateExisting());
    QVERIFY(primary.isPrimary());

    QSignalSpy activationSpy(&primary, &SingleInstanceManager::activationRequested);

    SingleInstanceManager secondary(scopeKey);
    QVERIFY(!secondary.startPrimaryOrActivateExisting());
    QVERIFY(!secondary.isPrimary());

    QVERIFY(activationSpy.wait(1000));
    QCOMPARE(activationSpy.count(), 1);
}

void SingleInstanceManagerTest::allowsIndependentScopes() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString scopeKeyA = temporaryDir.filePath(QStringLiteral("profile-a.sqlite3"));
    const QString scopeKeyB = temporaryDir.filePath(QStringLiteral("profile-b.sqlite3"));

    SingleInstanceManager primaryA(scopeKeyA);
    QVERIFY(primaryA.startPrimaryOrActivateExisting());
    QVERIFY(primaryA.isPrimary());

    QSignalSpy activationSpyA(&primaryA, &SingleInstanceManager::activationRequested);

    SingleInstanceManager primaryB(scopeKeyB);
    QVERIFY(primaryB.startPrimaryOrActivateExisting());
    QVERIFY(primaryB.isPrimary());

    QSignalSpy activationSpyB(&primaryB, &SingleInstanceManager::activationRequested);

    SingleInstanceManager secondaryA(scopeKeyA);
    QVERIFY(!secondaryA.startPrimaryOrActivateExisting());
    QVERIFY(!secondaryA.isPrimary());

    QVERIFY(activationSpyA.wait(1000));
    QCOMPARE(activationSpyA.count(), 1);
    QCOMPARE(activationSpyB.count(), 0);
}

QTEST_GUILESS_MAIN(SingleInstanceManagerTest)

#include "SingleInstanceManagerTest.moc"
