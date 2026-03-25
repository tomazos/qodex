#include "ui/ModelsModel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardItem>

#include "CodexProtocol.h"
#include "app/SessionController.h"

namespace qodex::ui {

namespace {

QString escapeAndElide(QString value) {
    value.replace(QStringLiteral("\r"), QStringLiteral("\\r"));
    value.replace(QStringLiteral("\n"), QStringLiteral("\\n"));
    value.replace(QStringLiteral("\t"), QStringLiteral("\\t"));

    constexpr int kMaxLength = 120;
    if (value.size() <= kMaxLength) {
        return value;
    }

    const QString prefix = value.left(52);
    const QString suffix = value.right(52);
    return QStringLiteral("%1 ... %2").arg(prefix, suffix);
}

QString scalarToString(const QJsonValue &value) {
    if (value.isString()) {
        return escapeAndElide(value.toString());
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    if (value.isNull()) {
        return QStringLiteral("null");
    }
    if (value.isUndefined()) {
        return QStringLiteral("undefined");
    }
    return QStringLiteral("<non-scalar>");
}

QList<QStandardItem *> makeRow(const QString &name, const QString &value = QString{}) {
    auto *nameItem = new QStandardItem(name);
    auto *valueItem = new QStandardItem(value);
    nameItem->setEditable(false);
    valueItem->setEditable(false);
    return {nameItem, valueItem};
}

void appendJsonNode(QStandardItem *parent, const QString &name, const QJsonValue &value) {
    if (parent == nullptr) {
        return;
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        auto row = makeRow(name, object.isEmpty() ? QStringLiteral("{}") : QStringLiteral("{…}"));
        QStandardItem *node = row.at(0);
        parent->appendRow(row);
        for (auto it = object.begin(); it != object.end(); ++it) {
            appendJsonNode(node, it.key(), it.value());
        }
        return;
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        auto row = makeRow(name, QStringLiteral("[%1]").arg(array.size()));
        QStandardItem *node = row.at(0);
        parent->appendRow(row);
        for (qsizetype index = 0; index < array.size(); ++index) {
            appendJsonNode(node, QStringLiteral("[%1]").arg(index), array.at(index));
        }
        return;
    }

    parent->appendRow(makeRow(name, scalarToString(value)));
}

QString modelTitle(const qodex::codex::Model &model) {
    const QString displayName = model.displayName.trimmed();
    if (!displayName.isEmpty()) {
        return displayName;
    }
    const QString id = model.id.trimmed();
    if (!id.isEmpty()) {
        return id;
    }
    return model.model;
}

QString modelSummary(const qodex::codex::Model &model) {
    return QStringLiteral("id=%1 model=%2 hidden=%3")
        .arg(model.id, model.model, model.hidden ? QStringLiteral("true") : QStringLiteral("false"));
}

}  // namespace

ModelsModel::ModelsModel(QObject *parent)
    : QStandardItemModel(parent) {
    setHorizontalHeaderLabels({QStringLiteral("Model"), QStringLiteral("Value")});
}

void ModelsModel::setSessionController(qodex::app::SessionController *sessionController) {
    if (m_sessionController == sessionController) {
        return;
    }

    if (m_sessionController != nullptr) {
        disconnect(m_sessionController, nullptr, this, nullptr);
    }

    m_sessionController = sessionController;
    if (m_sessionController != nullptr) {
        connect(m_sessionController, &qodex::app::SessionController::modelsChanged, this, &ModelsModel::refreshFromController);
    }

    refreshFromController();
}

void ModelsModel::refreshFromController() {
    clear();
    setHorizontalHeaderLabels({QStringLiteral("Model"), QStringLiteral("Value")});

    if (m_sessionController == nullptr) {
        emit treeRebuilt();
        return;
    }

    const QList<qodex::codex::Ref<qodex::codex::Model>> models = m_sessionController->models();
    for (const qodex::codex::Ref<qodex::codex::Model> &model : models) {
        if (!model) {
            continue;
        }

        auto modelRow = makeRow(modelTitle(*model), modelSummary(*model));
        QStandardItem *modelItem = modelRow.at(0);
        invisibleRootItem()->appendRow(modelRow);

        const QJsonObject object = qodex::codex::toJson(*model).toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            appendJsonNode(modelItem, it.key(), it.value());
        }
    }

    emit treeRebuilt();
}

}  // namespace qodex::ui
