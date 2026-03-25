#include "ui/ApiLogInspectorPane.h"

#include <QDataStream>
#include <QFont>
#include <QHeaderView>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "storage/DatabaseManager.h"

namespace qodex::ui {

namespace {

QString optionalBoolText(const std::optional<bool> &value) {
    if (!value.has_value()) {
        return QStringLiteral("—");
    }
    return *value ? QStringLiteral("true") : QStringLiteral("false");
}

QString optionalIntegerText(const std::optional<qint64> &value) {
    if (!value.has_value()) {
        return QStringLiteral("—");
    }
    return QString::number(*value);
}

QFont monospaceFont() {
    QFont font(QStringLiteral("monospace"));
    font.setStyleHint(QFont::TypeWriter);
    font.setFixedPitch(true);
    return font;
}

QString prettyPrintedJson(const QString &text) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && !document.isNull()) {
        return QString::fromUtf8(document.toJson(QJsonDocument::Indented));
    }
    return text;
}

void addField(QTreeWidget *tree, const QString &name, const QString &value) {
    if (tree == nullptr) {
        return;
    }

    auto *item = new QTreeWidgetItem();
    item->setText(0, name);
    item->setText(1, value);
    tree->addTopLevelItem(item);
}

}  // namespace

ApiLogInspectorPane::ApiLogInspectorPane(qodex::storage::DatabaseManager *databaseManager, QWidget *parent)
    : QWidget(parent),
      m_databaseManager(databaseManager) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_titleLabel);

    m_splitter = new QSplitter(Qt::Vertical, this);

    m_fieldsTree = new QTreeWidget(m_splitter);
    m_fieldsTree->setColumnCount(2);
    m_fieldsTree->setAlternatingRowColors(true);
    m_fieldsTree->setRootIsDecorated(false);
    m_fieldsTree->setItemsExpandable(false);
    m_fieldsTree->setUniformRowHeights(true);
    m_fieldsTree->setAllColumnsShowFocus(true);
    m_fieldsTree->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    m_fieldsTree->header()->setSectionsMovable(true);
    m_fieldsTree->header()->setStretchLastSection(false);

    m_payloadEdit = new QPlainTextEdit(m_splitter);
    m_payloadEdit->setReadOnly(true);
    m_payloadEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_payloadEdit->setFont(monospaceFont());

    m_splitter->addWidget(m_fieldsTree);
    m_splitter->addWidget(m_payloadEdit);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    layout->addWidget(m_splitter, 1);

    clearInspector(QStringLiteral("No API log selected."));
}

void ApiLogInspectorPane::inspectApiLog(const qint64 apiLogId) {
    m_currentApiLogId = apiLogId;
    if (m_databaseManager == nullptr) {
        clearInspector(QStringLiteral("API log database is not available."));
        return;
    }

    QString errorMessage;
    const std::optional<qodex::storage::ApiLogDetailRecord> detail = m_databaseManager->loadApiLogDetail(apiLogId, &errorMessage);
    if (!errorMessage.isEmpty()) {
        clearInspector(QStringLiteral("Failed to load API log %1: %2").arg(apiLogId).arg(errorMessage));
        return;
    }
    if (!detail.has_value()) {
        clearInspector(QStringLiteral("API log %1 was not found.").arg(apiLogId));
        return;
    }

    m_titleLabel->setText(
        QStringLiteral("API Log %1  %2  %3 %4")
            .arg(QString::number(detail->id), detail->timestampUtc, detail->direction, detail->messageKind)
    );

    m_fieldsTree->clear();
    addField(m_fieldsTree, QStringLiteral("id"), QString::number(detail->id));
    addField(m_fieldsTree, QStringLiteral("timestampUtc"), detail->timestampUtc);
    addField(m_fieldsTree, QStringLiteral("sessionId"), detail->sessionId);
    addField(m_fieldsTree, QStringLiteral("direction"), detail->direction);
    addField(m_fieldsTree, QStringLiteral("messageKind"), detail->messageKind);
    addField(m_fieldsTree, QStringLiteral("method"), detail->method);
    addField(m_fieldsTree, QStringLiteral("success"), optionalBoolText(detail->success));
    addField(m_fieldsTree, QStringLiteral("latencyMs"), optionalIntegerText(detail->latencyMs));
    addField(m_fieldsTree, QStringLiteral("threadId"), detail->threadId);
    addField(m_fieldsTree, QStringLiteral("jsonrpcId"), detail->jsonrpcId);
    addField(m_fieldsTree, QStringLiteral("correlationId"), detail->correlationId);
    addField(m_fieldsTree, QStringLiteral("summaryText"), detail->summaryText);

    m_fieldsTree->resizeColumnToContents(0);
    m_fieldsTree->resizeColumnToContents(1);
    m_payloadEdit->setPlainText(prettyPrintedJson(detail->payloadJson));
}

QByteArray ApiLogInspectorPane::saveViewState() const {
    if (m_fieldsTree == nullptr || m_fieldsTree->header() == nullptr || m_splitter == nullptr) {
        return {};
    }

    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream << m_fieldsTree->header()->saveState();
    stream << m_splitter->saveState();
    return state;
}

bool ApiLogInspectorPane::restoreViewState(const QByteArray &state) {
    if (state.isEmpty() || m_fieldsTree == nullptr || m_fieldsTree->header() == nullptr || m_splitter == nullptr) {
        return false;
    }

    QByteArray headerState;
    QByteArray splitterState;
    QDataStream stream(state);
    stream >> headerState;
    stream >> splitterState;
    if (stream.status() != QDataStream::Ok) {
        return false;
    }

    const bool headerRestored = m_fieldsTree->header()->restoreState(headerState);
    const bool splitterRestored = m_splitter->restoreState(splitterState);
    return headerRestored && splitterRestored;
}

void ApiLogInspectorPane::clearInspector(const QString &message) {
    m_titleLabel->setText(message);
    if (m_fieldsTree != nullptr) {
        m_fieldsTree->clear();
    }
    if (m_payloadEdit != nullptr) {
        m_payloadEdit->clear();
    }
}

}  // namespace qodex::ui
