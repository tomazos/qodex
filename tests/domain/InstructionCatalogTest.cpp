#include <QtTest>

#include <QTemporaryDir>

#include "domain/InstructionCatalog.h"
#include "storage/DatabaseManager.h"

using qodex::domain::InstructionCatalog;
using qodex::storage::DatabaseManager;

class InstructionCatalogTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesBundledCodexDefault();
    void duplicatesCodexDefaultIntoEditableInstruction();
    void refusesToRenameCodexDefault();
};

void InstructionCatalogTest::exposesBundledCodexDefault() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString databasePath = temporaryDir.filePath(QStringLiteral("qodex.sqlite3"));
    QString errorMessage;

    DatabaseManager databaseManager(databasePath);
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));

    InstructionCatalog catalog(&databaseManager);
    const QList<qodex::domain::InstructionDocumentSummary> summaries = catalog.instructionSummaries(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY(!summaries.isEmpty());
    QCOMPARE(summaries.first().key, InstructionCatalog::codexDefaultInstructionKey());
    QCOMPARE(summaries.first().name, QStringLiteral("Codex Default"));
    QVERIFY(summaries.first().isDefault);
    QVERIFY(!summaries.first().isRenamable);
    QVERIFY(!summaries.first().isEditable);

    const auto defaultDocument = catalog.instructionByKey(InstructionCatalog::codexDefaultInstructionKey(), &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY(defaultDocument.has_value());
    QCOMPARE(defaultDocument->name, QStringLiteral("Codex Default"));
    QVERIFY(defaultDocument->content.startsWith(QStringLiteral("You are a coding agent running in the Codex CLI")));
    QVERIFY(!defaultDocument->isEditable);
}

void InstructionCatalogTest::duplicatesCodexDefaultIntoEditableInstruction() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString databasePath = temporaryDir.filePath(QStringLiteral("qodex.sqlite3"));
    QString errorMessage;

    DatabaseManager databaseManager(databasePath);
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));

    InstructionCatalog catalog(&databaseManager);

    QSignalSpy instructionsChangedSpy(&catalog, &InstructionCatalog::instructionsChanged);
    const auto duplicated = catalog.duplicateInstruction(InstructionCatalog::codexDefaultInstructionKey(), &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY(duplicated.has_value());
    QCOMPARE(duplicated->name, QStringLiteral("Codex Default Copy"));
    QVERIFY(!duplicated->isDefault);
    QVERIFY(duplicated->isEditable);
    QCOMPARE(instructionsChangedSpy.size(), 1);

    const auto storedDuplicate = catalog.instructionByKey(duplicated->key, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY(storedDuplicate.has_value());
    QCOMPARE(storedDuplicate->content, duplicated->content);
    QVERIFY(storedDuplicate->isEditable);
}

void InstructionCatalogTest::refusesToRenameCodexDefault() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString databasePath = temporaryDir.filePath(QStringLiteral("qodex.sqlite3"));
    QString errorMessage;

    DatabaseManager databaseManager(databasePath);
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));

    InstructionCatalog catalog(&databaseManager);
    QVERIFY(!catalog.renameInstruction(
        InstructionCatalog::codexDefaultInstructionKey(),
        QStringLiteral("Renamed"),
        &errorMessage
    ));
    QVERIFY(errorMessage.contains(QStringLiteral("cannot be renamed")));
}

QTEST_GUILESS_MAIN(InstructionCatalogTest)

#include "InstructionCatalogTest.moc"
