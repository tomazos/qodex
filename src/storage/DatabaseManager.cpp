#include "storage/DatabaseManager.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "storage/MigrationRunner.h"

namespace qodex::storage {

namespace {

QVariant nullableText(const QString &value) {
    return value.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(value);
}

QVariant nullableBool(const std::optional<bool> value) {
    if (!value.has_value()) {
        return QVariant(QMetaType(QMetaType::Bool));
    }
    return QVariant(*value);
}

QVariant nullableInteger(const std::optional<qint64> value) {
    if (!value.has_value()) {
        return QVariant(QMetaType(QMetaType::LongLong));
    }
    return QVariant::fromValue(*value);
}

QString apiLogSortExpression(const ApiLogSortField sortField) {
    switch (sortField) {
    case ApiLogSortField::TimestampUtc:
        return QStringLiteral("ts_utc");
    case ApiLogSortField::Direction:
        return QStringLiteral("direction");
    case ApiLogSortField::MessageKind:
        return QStringLiteral("message_kind");
    case ApiLogSortField::Method:
        return QStringLiteral("coalesce(method, '')");
    case ApiLogSortField::Success:
        return QStringLiteral("coalesce(success, -1)");
    case ApiLogSortField::LatencyMs:
        return QStringLiteral("coalesce(latency_ms, -1)");
    case ApiLogSortField::SummaryText:
        return QStringLiteral("summary_text");
    case ApiLogSortField::ThreadId:
        return QStringLiteral("coalesce(thread_id, '')");
    case ApiLogSortField::JsonRpcId:
        return QStringLiteral("coalesce(jsonrpc_id, '')");
    case ApiLogSortField::CorrelationId:
        return QStringLiteral("coalesce(correlation_id, '')");
    case ApiLogSortField::SessionId:
        return QStringLiteral("coalesce(session_id, '')");
    case ApiLogSortField::PayloadPreview:
        return QStringLiteral("payload_json");
    }

    return QStringLiteral("ts_utc");
}

QString apiLogOrderByClause(const ApiLogSortField sortField, const Qt::SortOrder sortOrder) {
    const QString direction = sortOrder == Qt::DescendingOrder ? QStringLiteral("DESC") : QStringLiteral("ASC");
    const QString primary = apiLogSortExpression(sortField);
    return QStringLiteral("%1 %2, id %2").arg(primary, direction);
}

}  // namespace

DatabaseManager::DatabaseManager(const QString &databasePath)
    : m_databasePath(databasePath),
      m_connectionName(
          QStringLiteral("qodex.Database.%1")
              .arg(QString::fromLatin1(QCryptographicHash::hash(databasePath.toUtf8(), QCryptographicHash::Sha256).toHex().first(12)))
      ),
      m_database(new QSqlDatabase()) {
    Q_ASSERT(m_database != nullptr);
}

DatabaseManager::~DatabaseManager() {
    close();
    delete m_database;
    m_database = nullptr;
}

bool DatabaseManager::open(QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (isOpen()) {
        return true;
    }

    const QFileInfo databaseInfo(m_databasePath);
    if (!databaseInfo.dir().exists() && !QDir().mkpath(databaseInfo.dir().absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to create database directory %1").arg(databaseInfo.dir().absolutePath());
        }
        return false;
    }

    if (!MigrationRunner::migrate(m_databasePath, errorMessage)) {
        return false;
    }

    return initializeConnection(errorMessage);
}

void DatabaseManager::close() {
    if (m_database == nullptr || !m_database->isValid()) {
        return;
    }

    const QString connectionName = m_connectionName;
    if (m_database->isOpen()) {
        m_database->close();
    }
    *m_database = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

bool DatabaseManager::isOpen() const {
    return m_database != nullptr && m_database->isValid() && m_database->isOpen();
}

QString DatabaseManager::databasePath() const {
    return m_databasePath;
}

QList<WindowStateRecord> DatabaseManager::loadWindowStates(QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    QList<WindowStateRecord> records;
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return records;
    }

    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral(
            "SELECT window_key, geometry, layout FROM window_state ORDER BY window_key ASC;"
        ))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return {};
    }

    while (query.next()) {
        records.append({
            query.value(0).toString(),
            query.value(1).toByteArray(),
            query.value(2).toByteArray(),
        });
    }
    return records;
}

std::optional<QByteArray> DatabaseManager::loadViewState(const QString &viewKey, QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return std::nullopt;
    }

    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("SELECT state_blob FROM view_state WHERE view_key = ?;"));
    query.addBindValue(viewKey);
    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return query.value(0).toByteArray();
}

std::optional<QString> DatabaseManager::loadSetting(const QString &key, QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return std::nullopt;
    }

    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("SELECT value_json FROM settings WHERE key = ?;"));
    query.addBindValue(key);
    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return query.value(0).toString();
}

int DatabaseManager::apiLogRowCount(QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return 0;
    }

    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM api_log;"))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return 0;
    }
    if (!query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

QList<ApiLogListRecord> DatabaseManager::loadApiLogPage(
    const int offset,
    const int limit,
    const ApiLogSortField sortField,
    const Qt::SortOrder sortOrder,
    QString *errorMessage
) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return {};
    }

    const int boundedOffset = std::max(0, offset);
    const int boundedLimit = std::max(0, limit);
    QList<ApiLogListRecord> rows;
    if (boundedLimit <= 0) {
        return rows;
    }

    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral(
        "SELECT id, ts_utc, session_id, direction, message_kind, method, jsonrpc_id, correlation_id, "
        "thread_id, success, latency_ms, summary_text, "
        "CASE "
        "    WHEN length(payload_json) > 160 THEN substr(payload_json, 1, 157) || '...' "
        "    ELSE payload_json "
        "END AS payload_preview "
        "FROM api_log "
        "ORDER BY %1 "
        "LIMIT ? OFFSET ?;"
    ).arg(apiLogOrderByClause(sortField, sortOrder)));
    query.addBindValue(boundedLimit);
    query.addBindValue(boundedOffset);

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return {};
    }

    rows.reserve(boundedLimit);
    while (query.next()) {
        rows.append(ApiLogListRecord{
            .id = query.value(0).toLongLong(),
            .timestampUtc = query.value(1).toString(),
            .sessionId = query.value(2).toString(),
            .direction = query.value(3).toString(),
            .messageKind = query.value(4).toString(),
            .method = query.value(5).toString(),
            .jsonrpcId = query.value(6).toString(),
            .correlationId = query.value(7).toString(),
            .threadId = query.value(8).toString(),
            .success = query.value(9).isNull() ? std::nullopt
                                               : std::optional<bool>(query.value(9).toInt() != 0),
            .latencyMs = query.value(10).isNull() ? std::nullopt
                                                  : std::optional<qint64>(query.value(10).toLongLong()),
            .summaryText = query.value(11).toString(),
            .payloadPreview = query.value(12).toString(),
        });
    }

    return rows;
}

std::optional<ApiLogDetailRecord> DatabaseManager::loadApiLogDetail(const qint64 id, QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return std::nullopt;
    }

    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral(
        "SELECT id, ts_utc, session_id, direction, message_kind, method, jsonrpc_id, correlation_id, "
        "thread_id, success, latency_ms, summary_text, payload_json "
        "FROM api_log "
        "WHERE id = ?;"
    ));
    query.addBindValue(id);
    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }

    if (!query.next()) {
        return std::nullopt;
    }

    return ApiLogDetailRecord{
        .id = query.value(0).toLongLong(),
        .timestampUtc = query.value(1).toString(),
        .sessionId = query.value(2).toString(),
        .direction = query.value(3).toString(),
        .messageKind = query.value(4).toString(),
        .method = query.value(5).toString(),
        .jsonrpcId = query.value(6).toString(),
        .correlationId = query.value(7).toString(),
        .threadId = query.value(8).toString(),
        .success = query.value(9).isNull() ? std::nullopt
                                           : std::optional<bool>(query.value(9).toInt() != 0),
        .latencyMs = query.value(10).isNull() ? std::nullopt
                                              : std::optional<qint64>(query.value(10).toLongLong()),
        .summaryText = query.value(11).toString(),
        .payloadJson = query.value(12).toString(),
    };
}

bool DatabaseManager::replaceWindowStates(const QList<WindowStateRecord> &windowStates, QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return false;
    }

    if (!m_database->transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_database->lastError().text();
        }
        return false;
    }

    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("DELETE FROM window_state;"))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        m_database->rollback();
        return false;
    }

    query.prepare(QStringLiteral(
        "INSERT INTO window_state(window_key, geometry, layout, updated_at) "
        "VALUES (?, ?, ?, CURRENT_TIMESTAMP);"
    ));
    for (const WindowStateRecord &record : windowStates) {
        query.bindValue(0, record.windowKey);
        query.bindValue(1, record.geometry);
        query.bindValue(2, record.layout);
        if (!query.exec()) {
            if (errorMessage != nullptr) {
                *errorMessage = query.lastError().text();
            }
            m_database->rollback();
            return false;
        }
        query.finish();
    }

    if (!m_database->commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_database->lastError().text();
        }
        return false;
    }
    return true;
}

bool DatabaseManager::replaceViewStates(const QList<ViewStateRecord> &viewStates, QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return false;
    }

    if (!m_database->transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_database->lastError().text();
        }
        return false;
    }

    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("DELETE FROM view_state;"))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        m_database->rollback();
        return false;
    }

    query.prepare(QStringLiteral(
        "INSERT INTO view_state(view_key, state_blob, updated_at) "
        "VALUES (?, ?, CURRENT_TIMESTAMP);"
    ));
    for (const ViewStateRecord &record : viewStates) {
        query.bindValue(0, record.viewKey);
        query.bindValue(1, record.state);
        if (!query.exec()) {
            if (errorMessage != nullptr) {
                *errorMessage = query.lastError().text();
            }
            m_database->rollback();
            return false;
        }
        query.finish();
    }

    if (!m_database->commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_database->lastError().text();
        }
        return false;
    }
    return true;
}

bool DatabaseManager::saveSetting(const QString &key, const QString &valueJson, QString *errorMessage) {
    return executeStatement(
        QStringLiteral(
            "INSERT INTO settings(key, value_json) VALUES (?, ?) "
            "ON CONFLICT(key) DO UPDATE SET value_json = excluded.value_json;"
        ),
        {key, valueJson},
        errorMessage
    );
}

std::optional<ApiLogListRecord> DatabaseManager::appendApiLog(const ApiLogRecord &record, QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return std::nullopt;
    }

    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO api_log("
        "session_id, direction, message_kind, method, jsonrpc_id, correlation_id, "
        "thread_id, success, latency_ms, payload_json, summary_text"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"
    ));
    query.addBindValue(nullableText(record.sessionId));
    query.addBindValue(record.direction);
    query.addBindValue(record.messageKind);
    query.addBindValue(nullableText(record.method));
    query.addBindValue(nullableText(record.jsonrpcId));
    query.addBindValue(nullableText(record.correlationId));
    query.addBindValue(nullableText(record.threadId));
    query.addBindValue(nullableBool(record.success));
    query.addBindValue(nullableInteger(record.latencyMs));
    query.addBindValue(record.payloadJson);
    query.addBindValue(record.summaryText);

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }

    const QVariant insertedIdVariant = query.lastInsertId();
    if (!insertedIdVariant.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to determine inserted api_log row id.");
        }
        return std::nullopt;
    }

    return loadApiLogListRecordById(insertedIdVariant.toLongLong(), errorMessage);
}

bool DatabaseManager::initializeConnection(QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    *m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database->setDatabaseName(m_databasePath);
    if (!m_database->open()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_database->lastError().text();
        }
        *m_database = QSqlDatabase();
        return false;
    }

    if (!executeStatement(QStringLiteral("PRAGMA foreign_keys = ON;"), {}, errorMessage)) {
        close();
        return false;
    }

    if (!executeStatement(QStringLiteral("PRAGMA busy_timeout = 1000;"), {}, errorMessage)) {
        close();
        return false;
    }

    return true;
}

bool DatabaseManager::executeStatement(const QString &sql, const QList<QVariant> &bindings, QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return false;
    }

    QSqlQuery query(*m_database);
    query.prepare(sql);
    for (const QVariant &binding : bindings) {
        query.addBindValue(binding);
    }
    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

std::optional<ApiLogListRecord> DatabaseManager::loadApiLogListRecordById(const qint64 id, QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return std::nullopt;
    }

    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral(
        "SELECT id, ts_utc, session_id, direction, message_kind, method, jsonrpc_id, correlation_id, "
        "thread_id, success, latency_ms, summary_text, "
        "CASE "
        "    WHEN length(payload_json) > 160 THEN substr(payload_json, 1, 157) || '...' "
        "    ELSE payload_json "
        "END AS payload_preview "
        "FROM api_log WHERE id = ?;"
    ));
    query.addBindValue(id);
    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }
    if (!query.next()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Inserted api_log row %1 could not be reloaded.").arg(id);
        }
        return std::nullopt;
    }

    return ApiLogListRecord{
        .id = query.value(0).toLongLong(),
        .timestampUtc = query.value(1).toString(),
        .sessionId = query.value(2).toString(),
        .direction = query.value(3).toString(),
        .messageKind = query.value(4).toString(),
        .method = query.value(5).toString(),
        .jsonrpcId = query.value(6).toString(),
        .correlationId = query.value(7).toString(),
        .threadId = query.value(8).toString(),
        .success = query.value(9).isNull() ? std::nullopt
                                           : std::optional<bool>(query.value(9).toInt() != 0),
        .latencyMs = query.value(10).isNull() ? std::nullopt
                                              : std::optional<qint64>(query.value(10).toLongLong()),
        .summaryText = query.value(11).toString(),
        .payloadPreview = query.value(12).toString(),
    };
}

}  // namespace qodex::storage
