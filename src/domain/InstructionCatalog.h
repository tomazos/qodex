#pragma once

#include <QObject>
#include <QList>
#include <QString>

#include <optional>

namespace qodex::storage {
class DatabaseManager;
struct InstructionRecord;
}

namespace qodex::domain {

struct InstructionDocumentSummary {
    QString key;
    QString name;
    bool isDefault = false;
    bool isRenamable = false;
    bool isEditable = false;
    bool isDuplicable = false;
};

struct InstructionDocument {
    QString key;
    QString name;
    QString content;
    bool isDefault = false;
    bool isRenamable = false;
    bool isEditable = false;
    bool isDuplicable = false;
};

class InstructionCatalog final : public QObject {
    Q_OBJECT

public:
    explicit InstructionCatalog(qodex::storage::DatabaseManager *databaseManager, QObject *parent = nullptr);

    [[nodiscard]] QList<InstructionDocumentSummary> instructionSummaries(QString *errorMessage = nullptr) const;
    [[nodiscard]] std::optional<InstructionDocument> instructionByKey(
        const QString &key,
        QString *errorMessage = nullptr
    ) const;

    [[nodiscard]] bool renameInstruction(const QString &key, const QString &newName, QString *errorMessage = nullptr);
    [[nodiscard]] bool updateInstructionContent(
        const QString &key,
        const QString &content,
        QString *errorMessage = nullptr
    );
    [[nodiscard]] std::optional<InstructionDocument> duplicateInstruction(
        const QString &key,
        QString *errorMessage = nullptr
    );

    [[nodiscard]] static QString codexDefaultInstructionKey();

signals:
    void instructionsChanged();

private:
    [[nodiscard]] QString defaultInstructionContent(QString *errorMessage) const;
    [[nodiscard]] static bool isCodexDefaultInstructionKey(const QString &key);
    [[nodiscard]] static QString databaseInstructionKey(qint64 id);
    [[nodiscard]] static std::optional<qint64> instructionIdFromKey(const QString &key);
    [[nodiscard]] static InstructionDocumentSummary summaryFromRecord(const qodex::storage::InstructionRecord &record);
    [[nodiscard]] static InstructionDocument documentFromRecord(const qodex::storage::InstructionRecord &record);
    [[nodiscard]] QString duplicateNameFor(
        const QString &sourceName,
        const QList<qodex::storage::InstructionRecord> &existingRecords
    ) const;

    qodex::storage::DatabaseManager *m_databaseManager = nullptr;
};

}  // namespace qodex::domain
