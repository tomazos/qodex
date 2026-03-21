#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QString>

namespace qodex::codex {

struct JsonRpcId {
    QJsonValue value = QJsonValue(QJsonValue::Undefined);

    [[nodiscard]] bool isValid() const {
        return !value.isUndefined();
    }

    [[nodiscard]] QString toKey() const {
        if (value.isString()) {
            return QStringLiteral("s:") + value.toString();
        }
        if (value.isDouble()) {
            return QStringLiteral("n:") + QString::number(value.toInteger());
        }
        if (value.isNull()) {
            return QStringLiteral("null");
        }
        if (value.isArray()) {
            return QStringLiteral("a:")
                + QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
        }
        if (value.isObject()) {
            return QStringLiteral("o:")
                + QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
        }
        if (value.isBool()) {
            return value.toBool() ? QStringLiteral("b:true") : QStringLiteral("b:false");
        }
        return QStringLiteral("undefined");
    }

    [[nodiscard]] QString toDisplayString() const {
        if (value.isString()) {
            return value.toString();
        }
        if (value.isDouble()) {
            return QString::number(value.toInteger());
        }
        if (value.isNull()) {
            return QStringLiteral("null");
        }
        return toKey();
    }
};

struct JsonRpcErrorObject {
    qint64 code = 0;
    QString message;
    QJsonValue data = QJsonValue(QJsonValue::Undefined);
};

struct JsonRpcNotificationMessage {
    QString method;
    QJsonValue params = QJsonValue(QJsonValue::Undefined);
    QJsonObject raw;
};

struct JsonRpcRequestMessage {
    JsonRpcId id;
    QString method;
    QJsonValue params = QJsonValue(QJsonValue::Undefined);
    QJsonObject raw;
};

struct JsonRpcResponseMessage {
    JsonRpcId id;
    QJsonValue result = QJsonValue(QJsonValue::Undefined);
    QJsonObject raw;
};

struct JsonRpcErrorResponseMessage {
    JsonRpcId id;
    JsonRpcErrorObject error;
    QJsonObject raw;
};

}  // namespace qodex::codex

Q_DECLARE_METATYPE(qodex::codex::JsonRpcId)
Q_DECLARE_METATYPE(qodex::codex::JsonRpcErrorObject)
Q_DECLARE_METATYPE(qodex::codex::JsonRpcNotificationMessage)
Q_DECLARE_METATYPE(qodex::codex::JsonRpcRequestMessage)
Q_DECLARE_METATYPE(qodex::codex::JsonRpcResponseMessage)
Q_DECLARE_METATYPE(qodex::codex::JsonRpcErrorResponseMessage)
