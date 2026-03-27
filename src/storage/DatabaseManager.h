#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariant>
#include <Qt>

#include <optional>

class QSqlDatabase;

namespace qodex::storage {

struct WindowStateRecord {
    QString windowKey;
    QByteArray geometry;
    QByteArray layout;
};

struct ViewStateRecord {
    QString viewKey;
    QByteArray state;
};

struct ApiLogRecord {
    QString sessionId;
    QString direction;
    QString messageKind;
    QString method;
    QString jsonrpcId;
    QString correlationId;
    QString threadId;
    std::optional<bool> success;
    std::optional<qint64> latencyMs;
    QString payloadJson;
    QString summaryText;
};

enum class ApiLogSortField {
    TimestampUtc,
    Direction,
    MessageKind,
    Method,
    Success,
    LatencyMs,
    SummaryText,
    ThreadId,
    JsonRpcId,
    CorrelationId,
    SessionId,
    PayloadPreview,
};

struct ApiLogListRecord {
    qint64 id = 0;
    QString timestampUtc;
    QString sessionId;
    QString direction;
    QString messageKind;
    QString method;
    QString jsonrpcId;
    QString correlationId;
    QString threadId;
    std::optional<bool> success;
    std::optional<qint64> latencyMs;
    QString summaryText;
    QString payloadPreview;
};

struct ApiLogDetailRecord {
    qint64 id = 0;
    QString timestampUtc;
    QString sessionId;
    QString direction;
    QString messageKind;
    QString method;
    QString jsonrpcId;
    QString correlationId;
    QString threadId;
    std::optional<bool> success;
    std::optional<qint64> latencyMs;
    QString summaryText;
    QString payloadJson;
};

class DatabaseManager final {
public:
    explicit DatabaseManager(const QString &databasePath);
    ~DatabaseManager();

    [[nodiscard]] bool open(QString *errorMessage);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString databasePath() const;

    [[nodiscard]] QList<WindowStateRecord> loadWindowStates(QString *errorMessage) const;
    [[nodiscard]] std::optional<QByteArray> loadViewState(const QString &viewKey, QString *errorMessage) const;
    [[nodiscard]] std::optional<QString> loadSetting(const QString &key, QString *errorMessage) const;
    [[nodiscard]] int apiLogRowCount(QString *errorMessage) const;
    [[nodiscard]] QList<ApiLogListRecord> loadApiLogPage(
        int offset,
        int limit,
        ApiLogSortField sortField,
        Qt::SortOrder sortOrder,
        QString *errorMessage
    ) const;
    [[nodiscard]] std::optional<ApiLogDetailRecord> loadApiLogDetail(qint64 id, QString *errorMessage) const;

    [[nodiscard]] bool replaceWindowStates(const QList<WindowStateRecord> &windowStates, QString *errorMessage);
    [[nodiscard]] bool replaceViewStates(const QList<ViewStateRecord> &viewStates, QString *errorMessage);
    [[nodiscard]] bool saveSetting(const QString &key, const QString &valueJson, QString *errorMessage);
    [[nodiscard]] std::optional<ApiLogListRecord> appendApiLog(const ApiLogRecord &record, QString *errorMessage);

private:
    [[nodiscard]] bool initializeConnection(QString *errorMessage);
    [[nodiscard]] bool executeStatement(const QString &sql, const QList<QVariant> &bindings, QString *errorMessage) const;
    [[nodiscard]] std::optional<ApiLogListRecord> loadApiLogListRecordById(qint64 id, QString *errorMessage) const;

    QString m_databasePath;
    QString m_connectionName;
    QSqlDatabase *m_database = nullptr;
};

}  // namespace qodex::storage

Q_DECLARE_METATYPE(qodex::storage::ApiLogListRecord)
