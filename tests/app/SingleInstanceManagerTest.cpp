#include <QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>

#include "app/SingleInstanceManager.h"

using qodex::app::SingleInstanceManager;

class SingleInstanceManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void activatesExistingPrimaryInstance();
};

void SingleInstanceManagerTest::activatesExistingPrimaryInstance() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString serverName = temporaryDir.filePath(QStringLiteral("single-instance.sock"));

    SingleInstanceManager primary(serverName);
    QVERIFY(primary.startPrimaryOrActivateExisting());
    QVERIFY(primary.isPrimary());

    QSignalSpy activationSpy(&primary, &SingleInstanceManager::activationRequested);

    SingleInstanceManager secondary(serverName);
    QVERIFY(!secondary.startPrimaryOrActivateExisting());
    QVERIFY(!secondary.isPrimary());

    QVERIFY(activationSpy.wait(1000));
    QCOMPARE(activationSpy.count(), 1);
}

QTEST_GUILESS_MAIN(SingleInstanceManagerTest)

#include "SingleInstanceManagerTest.moc"
