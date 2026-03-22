#include <QtTest>

#include <QTemporaryDir>

#include "storage/DatabaseManager.h"

using qodex::storage::DatabaseManager;
using qodex::storage::ViewStateRecord;
using qodex::storage::WindowStateRecord;

class DatabaseManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void persistsWindowAndViewState();
    void persistsSettings();
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

QTEST_GUILESS_MAIN(DatabaseManagerTest)

#include "DatabaseManagerTest.moc"
