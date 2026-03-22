#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>

#include "codex/AppServerTransport.h"
#include "storage/DatabaseManager.h"

namespace qodex::codex {

class TrafficLogger final : public QObject {
    Q_OBJECT

public:
    TrafficLogger(
        qodex::storage::DatabaseManager *databaseManager,
        AppServerTransport *transport,
        QObject *parent = nullptr
    );

    [[nodiscard]] QString sessionId() const;

private:
    struct PendingRequestContext {
        QString method;
        QString threadId;
        QElapsedTimer timer;
    };

    void logNotification(
        const QString &direction,
        const JsonRpcNotificationMessage &message
    );
    void logRequest(
        const QString &direction,
        const JsonRpcRequestMessage &message,
        const std::optional<bool> success = std::nullopt,
        const std::optional<qint64> latencyMs = std::nullopt
    );
    void logResponse(
        const QString &direction,
        const JsonRpcResponseMessage &message,
        bool success
    );
    void logErrorResponse(
        const QString &direction,
        const JsonRpcErrorResponseMessage &message
    );
    void logTransportError(const QString &message);
    void writeRecord(const qodex::storage::ApiLogRecord &record);

    [[nodiscard]] static QString serializeObject(const QJsonObject &object);
    [[nodiscard]] static QString buildSummary(
        const QString &direction,
        const QString &messageKind,
        const QString &method,
        const QString &jsonrpcId,
        const QString &threadId,
        const std::optional<bool> success,
        const std::optional<qint64> latencyMs,
        const std::optional<qint64> errorCode = std::nullopt
    );
    [[nodiscard]] static QString extractThreadId(const QJsonValue &value);
    [[nodiscard]] static QString extractThreadId(const QJsonObject &object);

    qodex::storage::DatabaseManager *m_databaseManager = nullptr;
    AppServerTransport *m_transport = nullptr;
    QString m_sessionId;
    QHash<QString, PendingRequestContext> m_outboundRequests;
    QHash<QString, PendingRequestContext> m_inboundRequests;
};

}  // namespace qodex::codex
