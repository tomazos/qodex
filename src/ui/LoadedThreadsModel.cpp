#include "ui/LoadedThreadsModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardItem>

#include "app/LoadedThread.h"
#include "app/SessionController.h"
#include "domain/threadmodel/AbstractItem.h"
#include "domain/threadmodel/Turn.h"

namespace qodex::ui {

namespace {

QString kindName(const qodex::codex::ThreadItem::Kind kind) {
    switch (kind) {
    case qodex::codex::ThreadItem::Kind::UserMessage:
        return QStringLiteral("UserMessage");
    case qodex::codex::ThreadItem::Kind::AgentMessage:
        return QStringLiteral("AgentMessage");
    case qodex::codex::ThreadItem::Kind::Plan:
        return QStringLiteral("Plan");
    case qodex::codex::ThreadItem::Kind::Reasoning:
        return QStringLiteral("Reasoning");
    case qodex::codex::ThreadItem::Kind::CommandExecution:
        return QStringLiteral("CommandExecution");
    case qodex::codex::ThreadItem::Kind::FileChange:
        return QStringLiteral("FileChange");
    case qodex::codex::ThreadItem::Kind::McpToolCall:
        return QStringLiteral("McpToolCall");
    case qodex::codex::ThreadItem::Kind::DynamicToolCall:
        return QStringLiteral("DynamicToolCall");
    case qodex::codex::ThreadItem::Kind::CollabAgentToolCall:
        return QStringLiteral("CollabAgentToolCall");
    case qodex::codex::ThreadItem::Kind::WebSearch:
        return QStringLiteral("WebSearch");
    case qodex::codex::ThreadItem::Kind::ImageView:
        return QStringLiteral("ImageView");
    case qodex::codex::ThreadItem::Kind::ImageGeneration:
        return QStringLiteral("ImageGeneration");
    case qodex::codex::ThreadItem::Kind::EnteredReviewMode:
        return QStringLiteral("EnteredReviewMode");
    case qodex::codex::ThreadItem::Kind::ExitedReviewMode:
        return QStringLiteral("ExitedReviewMode");
    case qodex::codex::ThreadItem::Kind::ContextCompaction:
        return QStringLiteral("ContextCompaction");
    }

    return QStringLiteral("Unknown");
}

QString turnStatusName(const qodex::codex::TurnStatus status) {
    switch (status) {
    case qodex::codex::TurnStatus::Completed:
        return QStringLiteral("Completed");
    case qodex::codex::TurnStatus::Interrupted:
        return QStringLiteral("Interrupted");
    case qodex::codex::TurnStatus::Failed:
        return QStringLiteral("Failed");
    case qodex::codex::TurnStatus::InProgress:
        return QStringLiteral("InProgress");
    }

    return QStringLiteral("Unknown");
}

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
    return escapeAndElide(QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)));
}

void flattenJson(
    const QString &path,
    const QJsonValue &value,
    QList<QPair<QString, QString>> *properties
) {
    if (properties == nullptr) {
        return;
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.isEmpty()) {
            properties->append({path, QStringLiteral("{}")});
            return;
        }

        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString childPath = path.isEmpty() ? it.key() : QStringLiteral("%1.%2").arg(path, it.key());
            flattenJson(childPath, it.value(), properties);
        }
        return;
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.isEmpty()) {
            properties->append({path, QStringLiteral("[]")});
            return;
        }

        for (qsizetype index = 0; index < array.size(); ++index) {
            flattenJson(QStringLiteral("%1[%2]").arg(path).arg(index), array.at(index), properties);
        }
        return;
    }

    properties->append({path, scalarToString(value)});
}

QList<QPair<QString, QString>> itemProperties(const qodex::domain::threadmodel::AbstractItem &item) {
    QList<QPair<QString, QString>> properties;
    properties.append({QStringLiteral("id"), item.id()});
    properties.append({QStringLiteral("kind"), kindName(item.kind())});
    properties.append({QStringLiteral("state"), item.isCompleted() ? QStringLiteral("Completed")
                                                                   : QStringLiteral("InProgress")});
    flattenJson(QString{}, item.properties(), &properties);
    return properties;
}

QList<QStandardItem *> makeRow(const QString &name, const QString &value = QString{}) {
    auto *nameItem = new QStandardItem(name);
    auto *valueItem = new QStandardItem(value);
    nameItem->setEditable(false);
    valueItem->setEditable(false);
    return {nameItem, valueItem};
}

}  // namespace

LoadedThreadsModel::LoadedThreadsModel(QObject *parent)
    : QStandardItemModel(parent) {
    setHorizontalHeaderLabels({QStringLiteral("Loaded Thread"), QStringLiteral("Value")});
}

void LoadedThreadsModel::setSessionController(qodex::app::SessionController *sessionController) {
    if (m_sessionController == sessionController) {
        return;
    }

    if (m_sessionController != nullptr) {
        disconnect(m_sessionController, nullptr, this, nullptr);
    }

    m_sessionController = sessionController;
    if (m_sessionController != nullptr) {
        connect(m_sessionController, &qodex::app::SessionController::loadedThreadsChanged, this, &LoadedThreadsModel::refreshFromController);
    }

    refreshFromController();
}

void LoadedThreadsModel::refreshFromController() {
    clear();
    setHorizontalHeaderLabels({QStringLiteral("Loaded Thread"), QStringLiteral("Value")});

    if (m_sessionController == nullptr) {
        emit treeRebuilt();
        return;
    }

    const QList<const qodex::app::LoadedThread *> loadedThreads = m_sessionController->loadedThreads();
    for (const qodex::app::LoadedThread *loadedThread : loadedThreads) {
        if (loadedThread == nullptr) {
            continue;
        }

        const QString threadTitle = loadedThread->title().isEmpty() ? loadedThread->threadId() : loadedThread->title();
        auto threadRow = makeRow(
            QStringLiteral("Thread %1").arg(threadTitle),
            QStringLiteral("id=%1 activeTurn=%2")
                .arg(loadedThread->threadId(), loadedThread->activeTurnId().isEmpty() ? QStringLiteral("—")
                                                                                      : loadedThread->activeTurnId())
        );
        QStandardItem *threadItem = threadRow.at(0);

        for (const qodex::domain::threadmodel::Turn *turn : loadedThread->orderedTurns()) {
            if (turn == nullptr) {
                continue;
            }

            auto turnRow = makeRow(
                QStringLiteral("Turn %1").arg(turn->id()),
                QStringLiteral("status=%1").arg(turnStatusName(turn->status()))
            );
            QStandardItem *turnItem = turnRow.at(0);

            for (const qodex::domain::threadmodel::AbstractItem *item : turn->orderedItems()) {
                if (item == nullptr) {
                    continue;
                }

                auto itemRow = makeRow(
                    QStringLiteral("%1 %2").arg(kindName(item->kind()), item->id()),
                    item->isCompleted() ? QStringLiteral("Completed") : QStringLiteral("InProgress")
                );
                QStandardItem *itemTreeItem = itemRow.at(0);

                const QList<QPair<QString, QString>> properties = itemProperties(*item);
                for (const auto &property : properties) {
                    itemTreeItem->appendRow(makeRow(property.first, property.second));
                }

                turnItem->appendRow(itemRow);
            }

            threadItem->appendRow(turnRow);
        }

        invisibleRootItem()->appendRow(threadRow);
    }

    emit treeRebuilt();
}

}  // namespace qodex::ui
