#include "domain/InstructionCatalog.h"

#include <QDateTime>
#include <QFile>
#include <QSet>

#include "storage/DatabaseManager.h"

namespace qodex::domain {

namespace {

constexpr auto kCodexDefaultInstructionKey = "codex-default";

QString trimmedInstructionName(const QString &name) {
    const QString trimmed = name.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("Untitled") : trimmed;
}

}  // namespace

InstructionCatalog::InstructionCatalog(qodex::storage::DatabaseManager *databaseManager, QObject *parent)
    : QObject(parent),
      m_databaseManager(databaseManager) {}

QList<InstructionDocumentSummary> InstructionCatalog::instructionSummaries(QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QList<InstructionDocumentSummary> summaries;
    summaries.append(InstructionDocumentSummary{
        .key = codexDefaultInstructionKey(),
        .name = QStringLiteral("Codex Default"),
        .isDefault = true,
        .isRenamable = false,
        .isEditable = false,
        .isDuplicable = true,
    });

    if (m_databaseManager == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Instruction database is not available.");
        }
        return summaries;
    }

    const QList<qodex::storage::InstructionRecord> records = m_databaseManager->loadInstructions(errorMessage);
    if (errorMessage != nullptr && !errorMessage->isEmpty()) {
        return summaries;
    }

    for (const qodex::storage::InstructionRecord &record : records) {
        summaries.append(summaryFromRecord(record));
    }
    return summaries;
}

std::optional<InstructionDocument> InstructionCatalog::instructionByKey(const QString &key, QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (isCodexDefaultInstructionKey(key)) {
        const QString content = defaultInstructionContent(errorMessage);
        if (errorMessage != nullptr && !errorMessage->isEmpty()) {
            return std::nullopt;
        }
        return InstructionDocument{
            .key = codexDefaultInstructionKey(),
            .name = QStringLiteral("Codex Default"),
            .content = content,
            .isDefault = true,
            .isRenamable = false,
            .isEditable = false,
            .isDuplicable = true,
        };
    }

    if (m_databaseManager == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Instruction database is not available.");
        }
        return std::nullopt;
    }

    const auto instructionId = instructionIdFromKey(key);
    if (!instructionId.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unknown instruction key %1.").arg(key);
        }
        return std::nullopt;
    }

    const auto record = m_databaseManager->loadInstruction(*instructionId, errorMessage);
    if (!record.has_value()) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("Instruction %1 was not found.").arg(key);
        }
        return std::nullopt;
    }

    return documentFromRecord(*record);
}

bool InstructionCatalog::renameInstruction(const QString &key, const QString &newName, QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (isCodexDefaultInstructionKey(key)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Codex Default cannot be renamed.");
        }
        return false;
    }

    if (m_databaseManager == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Instruction database is not available.");
        }
        return false;
    }

    const auto instructionId = instructionIdFromKey(key);
    if (!instructionId.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unknown instruction key %1.").arg(key);
        }
        return false;
    }

    const QString trimmedName = newName.trimmed();
    if (trimmedName.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Instruction name cannot be empty.");
        }
        return false;
    }

    if (!m_databaseManager->renameInstruction(*instructionId, trimmedName, errorMessage)) {
        return false;
    }

    emit instructionsChanged();
    return true;
}

bool InstructionCatalog::updateInstructionContent(const QString &key, const QString &content, QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (isCodexDefaultInstructionKey(key)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Codex Default is immutable.");
        }
        return false;
    }

    if (m_databaseManager == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Instruction database is not available.");
        }
        return false;
    }

    const auto instructionId = instructionIdFromKey(key);
    if (!instructionId.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unknown instruction key %1.").arg(key);
        }
        return false;
    }

    return m_databaseManager->updateInstructionContent(*instructionId, content, errorMessage);
}

std::optional<InstructionDocument> InstructionCatalog::duplicateInstruction(const QString &key, QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (m_databaseManager == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Instruction database is not available.");
        }
        return std::nullopt;
    }

    const auto sourceDocument = instructionByKey(key, errorMessage);
    if (!sourceDocument.has_value()) {
        return std::nullopt;
    }

    QString loadErrorMessage;
    const QList<qodex::storage::InstructionRecord> existingRecords = m_databaseManager->loadInstructions(&loadErrorMessage);
    if (!loadErrorMessage.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = loadErrorMessage;
        }
        return std::nullopt;
    }

    const QString duplicateName = duplicateNameFor(sourceDocument->name, existingRecords);
    const auto createdRecord = m_databaseManager->createInstruction(duplicateName, sourceDocument->content, errorMessage);
    if (!createdRecord.has_value()) {
        return std::nullopt;
    }

    emit instructionsChanged();
    return documentFromRecord(*createdRecord);
}

QString InstructionCatalog::codexDefaultInstructionKey() {
    return QString::fromLatin1(kCodexDefaultInstructionKey);
}

QString InstructionCatalog::defaultInstructionContent(QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QFile file(QStringLiteral(":/codex/prompt.md"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to load bundled Codex default instructions.");
        }
        return {};
    }

    return QString::fromUtf8(file.readAll());
}

bool InstructionCatalog::isCodexDefaultInstructionKey(const QString &key) {
    return key == codexDefaultInstructionKey();
}

QString InstructionCatalog::databaseInstructionKey(const qint64 id) {
    return QStringLiteral("instruction:%1").arg(id);
}

std::optional<qint64> InstructionCatalog::instructionIdFromKey(const QString &key) {
    static const QString prefix = QStringLiteral("instruction:");
    if (!key.startsWith(prefix)) {
        return std::nullopt;
    }

    bool ok = false;
    const qint64 id = key.mid(prefix.size()).toLongLong(&ok);
    if (!ok || id <= 0) {
        return std::nullopt;
    }
    return id;
}

InstructionDocumentSummary InstructionCatalog::summaryFromRecord(const qodex::storage::InstructionRecord &record) {
    return InstructionDocumentSummary{
        .key = databaseInstructionKey(record.id),
        .name = record.name,
        .isDefault = false,
        .isRenamable = true,
        .isEditable = true,
        .isDuplicable = true,
    };
}

InstructionDocument InstructionCatalog::documentFromRecord(const qodex::storage::InstructionRecord &record) {
    return InstructionDocument{
        .key = databaseInstructionKey(record.id),
        .name = record.name,
        .content = record.content,
        .isDefault = false,
        .isRenamable = true,
        .isEditable = true,
        .isDuplicable = true,
    };
}

QString InstructionCatalog::duplicateNameFor(
    const QString &sourceName,
    const QList<qodex::storage::InstructionRecord> &existingRecords
) const {
    const QString baseName = trimmedInstructionName(sourceName);
    QSet<QString> existingNames;
    for (const qodex::storage::InstructionRecord &record : existingRecords) {
        existingNames.insert(record.name.trimmed());
    }

    const QString firstAttempt = QStringLiteral("%1 Copy").arg(baseName);
    if (!existingNames.contains(firstAttempt)) {
        return firstAttempt;
    }

    for (int copyNumber = 2; copyNumber < 1000; ++copyNumber) {
        const QString candidate = QStringLiteral("%1 Copy %2").arg(baseName).arg(copyNumber);
        if (!existingNames.contains(candidate)) {
            return candidate;
        }
    }

    return QStringLiteral("%1 Copy %2").arg(baseName).arg(QDateTime::currentSecsSinceEpoch());
}

}  // namespace qodex::domain
