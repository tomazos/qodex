#include "app/SessionController.h"

#include <algorithm>

#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDebug>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

#include "CodexClient.h"
#include "CodexProtocol.h"
#include "app/AppConfig.h"
#include "app/LoadedThread.h"
#include "app/ThreadUiProcess.h"
#include "app/ThreadUiProcessManager.h"
#include "codex/AppServerTransport.h"
#include "domain/InstructionCatalog.h"
#include "domain/ThreadStore.h"
#include "storage/DatabaseManager.h"
#include "ui/MainWindow.h"
#include "ui/ThreadListPane.h"

namespace qodex::app {

using qodex::codex::ClientInfo;
using qodex::codex::CodexClient;
using qodex::codex::EmptyObject;
using qodex::codex::InitializeResponse;
using qodex::codex::InitializeCapabilities;
using qodex::codex::JsonRpcErrorObject;
using qodex::codex::JsonRpcId;
using qodex::codex::Model;
using qodex::codex::ModelListResponse;
using qodex::codex::ModelReroutedNotificationParams;
using qodex::codex::Nullable;
using qodex::codex::Ref;
using qodex::codex::SessionSource;
using qodex::codex::SubAgentSource;
using qodex::codex::Thread;
using qodex::codex::ThreadArchivedNotificationParams;
using qodex::codex::ThreadClosedNotificationParams;
using qodex::codex::ThreadForkResponse;
using qodex::codex::ThreadListResponse;
using qodex::codex::ThreadNameUpdatedNotificationParams;
using qodex::codex::ThreadStartResponse;
using qodex::codex::ThreadSortKey;
using qodex::codex::ThreadSourceKind;
using qodex::codex::ThreadStartedNotificationParams;
using qodex::codex::ThreadStatus;
using qodex::codex::ThreadStatusChangedNotificationParams;
using qodex::codex::ThreadUnarchiveResponse;
using qodex::codex::ThreadUnsubscribeResponse;
using qodex::codex::ThreadUnsubscribeStatus;
using qodex::codex::ThreadUnarchivedNotificationParams;
using qodex::codex::ItemAgentMessageDeltaNotificationParams;
using qodex::codex::ItemCommandExecutionOutputDeltaNotificationParams;
using qodex::codex::ItemCommandExecutionTerminalInteractionNotificationParams;
using qodex::codex::ItemCompletedNotificationParams;
using qodex::codex::ItemFileChangeOutputDeltaNotificationParams;
using qodex::codex::ItemMcpToolCallProgressNotificationParams;
using qodex::codex::ItemPlanDeltaNotificationParams;
using qodex::codex::ItemReasoningSummaryPartAddedNotificationParams;
using qodex::codex::ItemReasoningSummaryTextDeltaNotificationParams;
using qodex::codex::ItemReasoningTextDeltaNotificationParams;
using qodex::codex::ItemStartedNotificationParams;
using qodex::codex::TurnCompletedNotificationParams;
using qodex::codex::TurnStartedNotificationParams;

namespace {

QString threadSettingsWindowTitle(const qodex::ui::ThreadSettingsDialog::Mode mode) {
    switch (mode) {
    case qodex::ui::ThreadSettingsDialog::Mode::Create:
        return QStringLiteral("Create New Thread");
    case qodex::ui::ThreadSettingsDialog::Mode::Edit:
        return QStringLiteral("Edit Thread Settings");
    case qodex::ui::ThreadSettingsDialog::Mode::Fork:
        return QStringLiteral("Fork Thread");
    }

    return QStringLiteral("Thread Settings");
}

std::optional<QString> optionalTrimmedString(const QString &value) {
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? std::nullopt : std::optional<QString>(trimmed);
}

QString normalizedReasoningEffort(QString effort) {
    return effort.trimmed().toLower();
}

std::optional<QString> reasoningEffortKey(const qodex::codex::Nullable<QString> &reasoningEffort) {
    if (!reasoningEffort.hasValue()) {
        return std::nullopt;
    }

    return normalizedReasoningEffort(reasoningEffort.value());
}

std::optional<QString> reasoningEffortKey(const QString &reasoningEffort) {
    const QString normalized = normalizedReasoningEffort(reasoningEffort);
    return normalized.isEmpty() ? std::nullopt : std::optional<QString>(normalized);
}

std::optional<QString> reasoningEffortFromKey(const std::optional<QString> &key) {
    if (!key.has_value() || key->trimmed().isEmpty()) {
        return std::nullopt;
    }

    return normalizedReasoningEffort(*key);
}

bool modelOptionSupportsReasoningEffort(
    const qodex::ui::ThreadSettingsDialog::ModelOption &option,
    const QString &effort
) {
    const QString normalized = normalizedReasoningEffort(effort);
    for (const Ref<qodex::codex::ReasoningEffortOption> &supportedEffort : option.supportedReasoningEfforts) {
        if (supportedEffort && normalizedReasoningEffort(supportedEffort->reasoningEffort) == normalized) {
            return true;
        }
    }

    return false;
}

QString preferredCreateReasoningEffort(
    const qodex::ui::ThreadSettingsDialog::ModelOption &option
) {
    return modelOptionSupportsReasoningEffort(option, QStringLiteral("xhigh"))
        ? QStringLiteral("xhigh")
        : normalizedReasoningEffort(option.defaultReasoningEffort);
}

void applyCreateModelDefaults(
    qodex::ui::ThreadSettingsDialog::Selection &selection,
    const qodex::ui::ThreadSettingsDialog::ModelOption &option
) {
    selection.model = option.model;
    selection.reasoningEffort = preferredCreateReasoningEffort(option);
}

}  // namespace

SessionController::SessionController(
    const AppConfig &config,
    qodex::codex::AppServerTransport *transport,
    CodexClient *client,
    qodex::storage::DatabaseManager *databaseManager,
    qodex::domain::ThreadStore *threadStore,
    ThreadUiProcessManager *threadUiProcessManager,
    qodex::domain::InstructionCatalog *instructionCatalog,
    qodex::ui::MainWindow *mainWindow,
    QObject *parent
)
    : QObject(parent),
      m_config(config),
      m_transport(transport),
      m_client(client),
      m_databaseManager(databaseManager),
      m_threadStore(threadStore),
      m_threadUiProcessManager(threadUiProcessManager),
      m_instructionCatalog(instructionCatalog),
      m_mainWindow(mainWindow) {
    Q_ASSERT(m_transport != nullptr);
    Q_ASSERT(m_client != nullptr);
    Q_ASSERT(m_databaseManager != nullptr);
    Q_ASSERT(m_threadStore != nullptr);
    Q_ASSERT(m_threadUiProcessManager != nullptr);
    Q_ASSERT(m_instructionCatalog != nullptr);
    Q_ASSERT(m_mainWindow != nullptr);

    connect(m_transport, &qodex::codex::AppServerTransport::started, this, &SessionController::onTransportStarted);

    connect(m_client, &CodexClient::initializeSucceeded, this, &SessionController::onInitializeSucceeded);
    connect(m_client, &CodexClient::initializeFailed, this, &SessionController::onInitializeFailed);
    connect(m_client, &CodexClient::modelListSucceeded, this, &SessionController::onModelListSucceeded);
    connect(m_client, &CodexClient::modelListFailed, this, &SessionController::onModelListFailed);
    connect(m_client, &CodexClient::threadListSucceeded, this, &SessionController::onThreadListSucceeded);
    connect(m_client, &CodexClient::threadListFailed, this, &SessionController::onThreadListFailed);
    connect(m_client, &CodexClient::threadStartSucceeded, this, &SessionController::onThreadStartSucceeded);
    connect(m_client, &CodexClient::threadStartFailed, this, &SessionController::onThreadStartFailed);
    connect(m_client, &CodexClient::threadResumeSucceeded, this, &SessionController::onThreadResumeSucceeded);
    connect(m_client, &CodexClient::threadResumeFailed, this, &SessionController::onThreadResumeFailed);
    connect(m_client, &CodexClient::threadNameSetSucceeded, this, &SessionController::onThreadNameSetSucceeded);
    connect(m_client, &CodexClient::threadNameSetFailed, this, &SessionController::onThreadNameSetFailed);
    connect(m_client, &CodexClient::threadArchiveSucceeded, this, &SessionController::onThreadArchiveSucceeded);
    connect(m_client, &CodexClient::threadArchiveFailed, this, &SessionController::onThreadArchiveFailed);
    connect(m_client, &CodexClient::threadUnsubscribeSucceeded, this, &SessionController::onThreadUnsubscribeSucceeded);
    connect(m_client, &CodexClient::threadUnsubscribeFailed, this, &SessionController::onThreadUnsubscribeFailed);
    connect(m_client, &CodexClient::threadForkSucceeded, this, &SessionController::onThreadForkSucceeded);
    connect(m_client, &CodexClient::threadForkFailed, this, &SessionController::onThreadForkFailed);
    connect(m_client, &CodexClient::threadUnarchiveSucceeded, this, &SessionController::onThreadUnarchiveSucceeded);
    connect(m_client, &CodexClient::threadUnarchiveFailed, this, &SessionController::onThreadUnarchiveFailed);
    connect(
        m_client,
        &CodexClient::threadStartedNotificationReceived,
        this,
        &SessionController::onThreadStartedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::threadClosedNotificationReceived,
        this,
        &SessionController::onThreadClosedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::threadNameUpdatedNotificationReceived,
        this,
        &SessionController::onThreadNameUpdatedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::threadStatusChangedNotificationReceived,
        this,
        &SessionController::onThreadStatusChangedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::threadArchivedNotificationReceived,
        this,
        &SessionController::onThreadArchivedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::threadUnarchivedNotificationReceived,
        this,
        &SessionController::onThreadUnarchivedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::modelReroutedNotificationReceived,
        this,
        &SessionController::onModelReroutedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::turnStartedNotificationReceived,
        this,
        &SessionController::onTurnStartedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::turnCompletedNotificationReceived,
        this,
        &SessionController::onTurnCompletedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::itemStartedNotificationReceived,
        this,
        &SessionController::onItemStartedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::itemCompletedNotificationReceived,
        this,
        &SessionController::onItemCompletedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::itemAgentMessageDeltaNotificationReceived,
        this,
        &SessionController::onItemAgentMessageDeltaNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::itemCommandExecutionOutputDeltaNotificationReceived,
        this,
        &SessionController::onItemCommandExecutionOutputDeltaNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::itemCommandExecutionTerminalInteractionNotificationReceived,
        this,
        &SessionController::onItemCommandExecutionTerminalInteractionNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::itemFileChangeOutputDeltaNotificationReceived,
        this,
        &SessionController::onItemFileChangeOutputDeltaNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::itemMcpToolCallProgressNotificationReceived,
        this,
        &SessionController::onItemMcpToolCallProgressNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::itemPlanDeltaNotificationReceived,
        this,
        &SessionController::onItemPlanDeltaNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::itemReasoningSummaryPartAddedNotificationReceived,
        this,
        &SessionController::onItemReasoningSummaryPartAddedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::itemReasoningSummaryTextDeltaNotificationReceived,
        this,
        &SessionController::onItemReasoningSummaryTextDeltaNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::itemReasoningTextDeltaNotificationReceived,
        this,
        &SessionController::onItemReasoningTextDeltaNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::transportErrorOccurred,
        this,
        &SessionController::onTransportErrorOccurred
    );
    connect(
        m_client,
        &CodexClient::transportProcessExited,
        this,
        &SessionController::onTransportProcessExited
    );

    attachWindow(m_mainWindow);
    connect(
        m_threadStore,
        &qodex::domain::ThreadStore::threadListChanged,
        this,
        &SessionController::refreshSelectedThreadUi
    );
    connect(
        m_threadStore,
        &qodex::domain::ThreadStore::selectedThreadChanged,
        this,
        &SessionController::refreshSelectedThreadUi
    );
    connect(
        m_threadUiProcessManager,
        &ThreadUiProcessManager::threadUiProcessExited,
        this,
        &SessionController::onThreadUiProcessExited
    );
}

void SessionController::attachWindow(qodex::ui::MainWindow *window) {
    if (window == nullptr || m_windows.contains(window)) {
        return;
    }

    m_windows.append(window);

    if (qodex::ui::ThreadListPane *pane = window->threadListPane()) {
        connect(
            pane,
            &qodex::ui::ThreadListPane::createThreadRequested,
            this,
            &SessionController::onCreateThreadRequested
        );
        connect(
            pane,
            &qodex::ui::ThreadListPane::refreshRequested,
            this,
            &SessionController::onRefreshRequested
        );
        connect(
            pane,
            &qodex::ui::ThreadListPane::threadSelected,
            this,
            &SessionController::onThreadSelected
        );
        connect(
            pane,
            &qodex::ui::ThreadListPane::resumeThreadRequested,
            this,
            &SessionController::onResumeThreadRequested
        );
        connect(
            pane,
            &qodex::ui::ThreadListPane::editThreadSettingsRequested,
            this,
            &SessionController::onEditThreadSettingsRequested
        );
        connect(
            pane,
            &qodex::ui::ThreadListPane::renameThreadRequested,
            this,
            &SessionController::onRenameThreadRequested
        );
        connect(
            pane,
            &qodex::ui::ThreadListPane::closeThreadsRequested,
            this,
            &SessionController::onCloseThreadsRequested
        );
        connect(
            pane,
            &qodex::ui::ThreadListPane::forkThreadRequested,
            this,
            &SessionController::onForkThreadRequested
        );
        connect(
            pane,
            &qodex::ui::ThreadListPane::archiveThreadsRequested,
            this,
            &SessionController::onArchiveThreadsRequested
        );
        connect(
            pane,
            &qodex::ui::ThreadListPane::unarchiveThreadsRequested,
            this,
            &SessionController::onUnarchiveThreadsRequested
        );
        pane->setCurrentThreadId(m_threadStore->selectedThreadId());
    }
}

QList<const LoadedThread *> SessionController::loadedThreads() const {
    QList<const LoadedThread *> threads;
    threads.reserve(m_loadedThreads.size());

    for (LoadedThread *loadedThread : m_loadedThreads) {
        if (loadedThread != nullptr) {
            threads.append(loadedThread);
        }
    }

    std::sort(
        threads.begin(),
        threads.end(),
        [](const LoadedThread *left, const LoadedThread *right) {
            if (left == nullptr || right == nullptr) {
                return left < right;
            }

            const QString &leftTitle = left->title();
            const QString &rightTitle = right->title();
            if (leftTitle != rightTitle) {
                return leftTitle < rightTitle;
            }
            return left->threadId() < right->threadId();
        }
    );

    return threads;
}

QList<Ref<Model>> SessionController::models() const {
    return m_models;
}

QList<qodex::ui::ThreadSettingsDialog::ModelOption> SessionController::threadSettingsModelOptions() const {
    QList<qodex::ui::ThreadSettingsDialog::ModelOption> options;
    options.reserve(m_models.size());

    for (const Ref<Model> &model : m_models) {
        if (!model) {
            continue;
        }

        const QString modelName = !model->model.trimmed().isEmpty() ? model->model.trimmed() : model->id.trimmed();
        if (modelName.isEmpty()) {
            continue;
        }

        options.append(qodex::ui::ThreadSettingsDialog::ModelOption{
            .model = modelName,
            .displayName = model->displayName.trimmed(),
            .isDefault = model->isDefault,
            .defaultReasoningEffort = model->defaultReasoningEffort,
            .supportedReasoningEfforts = model->supportedReasoningEfforts,
        });
    }

    return options;
}

QList<qodex::ui::ThreadSettingsDialog::InstructionOption> SessionController::threadSettingsInstructionOptions(
    QString *errorMessage
) const {
    QList<qodex::ui::ThreadSettingsDialog::InstructionOption> options;
    if (m_instructionCatalog == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Instruction catalog is unavailable.");
        }
        return options;
    }

    const QList<qodex::domain::InstructionDocumentSummary> summaries = m_instructionCatalog->instructionSummaries(
        errorMessage
    );
    options.reserve(summaries.size());
    for (const qodex::domain::InstructionDocumentSummary &summary : summaries) {
        options.append(qodex::ui::ThreadSettingsDialog::InstructionOption{
            .key = summary.key,
            .name = summary.name,
            .isDefault = summary.isDefault,
        });
    }

    return options;
}

qodex::ui::ThreadSettingsDialog::Selection SessionController::defaultCreateThreadSettingsSelection() const {
    qodex::ui::ThreadSettingsDialog::Selection selection;
    selection.workingDirectory = QDir::homePath();

    const QList<qodex::ui::ThreadSettingsDialog::ModelOption> modelOptions = threadSettingsModelOptions();
    for (const qodex::ui::ThreadSettingsDialog::ModelOption &option : modelOptions) {
        if (option.model == QStringLiteral("gpt-5.5")) {
            applyCreateModelDefaults(selection, option);
            break;
        }
    }
    if (selection.model.isEmpty()) {
        for (const qodex::ui::ThreadSettingsDialog::ModelOption &option : modelOptions) {
            if (option.isDefault) {
                applyCreateModelDefaults(selection, option);
                break;
            }
        }
    }
    if (selection.model.isEmpty() && !modelOptions.isEmpty()) {
        applyCreateModelDefaults(selection, modelOptions.constFirst());
    }

    QString errorMessage;
    const QList<qodex::ui::ThreadSettingsDialog::InstructionOption> instructionOptions =
        threadSettingsInstructionOptions(&errorMessage);
    selection.instructionKey = qodex::domain::InstructionCatalog::codexDefaultInstructionKey();
    for (const qodex::ui::ThreadSettingsDialog::InstructionOption &option : instructionOptions) {
        if (option.key == qodex::domain::InstructionCatalog::codexDefaultInstructionKey()) {
            selection.instructionKey = option.key;
            break;
        }
    }
    if (selection.instructionKey.isEmpty() && !instructionOptions.isEmpty()) {
        selection.instructionKey = instructionOptions.constFirst().key;
    }

    return selection;
}

qodex::ui::ThreadSettingsDialog::Selection SessionController::defaultForkThreadSettingsSelection(
    const qodex::domain::ThreadSummary &summary
) const {
    qodex::ui::ThreadSettingsDialog::Selection selection = defaultCreateThreadSettingsSelection();
    selection.threadName.clear();
    selection.workingDirectory = !summary.cwd.trimmed().isEmpty() ? summary.cwd.trimmed() : selection.workingDirectory;

    QString errorMessage;
    const auto storedSettings = loadStoredThreadSettings(summary.id, &errorMessage);
    if (errorMessage.isEmpty() && storedSettings.has_value()) {
        if (storedSettings->cwd.has_value()) {
            selection.workingDirectory = *storedSettings->cwd;
        }
        if (storedSettings->model.has_value()) {
            selection.model = *storedSettings->model;
        }
        if (const auto reasoningEffort = reasoningEffortFromKey(storedSettings->reasoningEffort); reasoningEffort.has_value()) {
            selection.reasoningEffort = *reasoningEffort;
        }
        if (storedSettings->instructionKey.has_value()) {
            selection.instructionKey = *storedSettings->instructionKey;
        }
    }
    return selection;
}

qodex::ui::ThreadSettingsDialog::Selection SessionController::defaultEditThreadSettingsSelection(
    const qodex::domain::ThreadSummary &summary
) const {
    qodex::ui::ThreadSettingsDialog::Selection selection = defaultForkThreadSettingsSelection(summary);
    selection.threadName = summary.title;
    return selection;
}

std::optional<qodex::ui::ThreadSettingsDialog::Selection> SessionController::runThreadSettingsDialog(
    const qodex::ui::ThreadSettingsDialog::Mode mode,
    const qodex::ui::ThreadSettingsDialog::Selection &selection,
    const QString &helperText
) {
    const QList<qodex::ui::ThreadSettingsDialog::ModelOption> modelOptions = threadSettingsModelOptions();
    if (modelOptions.isEmpty()) {
        QMessageBox::warning(
            m_mainWindow,
            threadSettingsWindowTitle(mode),
            QStringLiteral("No models are available yet. Refresh the thread list after model/list succeeds.")
        );
        return std::nullopt;
    }

    QString errorMessage;
    const QList<qodex::ui::ThreadSettingsDialog::InstructionOption> instructionOptions =
        threadSettingsInstructionOptions(&errorMessage);
    if (!errorMessage.isEmpty()) {
        QMessageBox::warning(m_mainWindow, threadSettingsWindowTitle(mode), errorMessage);
        return std::nullopt;
    }
    if (instructionOptions.isEmpty()) {
        QMessageBox::warning(
            m_mainWindow,
            threadSettingsWindowTitle(mode),
            QStringLiteral("No instructions are available.")
        );
        return std::nullopt;
    }

    qodex::ui::ThreadSettingsDialog dialog(mode, m_mainWindow);
    dialog.setModelOptions(modelOptions);
    dialog.setInstructionOptions(instructionOptions);
    dialog.setInitialSelection(selection);
    if (!helperText.trimmed().isEmpty()) {
        dialog.setHelperText(helperText);
    }

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    return dialog.selection();
}

Nullable<QString> SessionController::baseInstructionsFromInstructionKey(
    const std::optional<QString> &instructionKey,
    QString *errorMessage
) const {
    if (!instructionKey.has_value()) {
        return missing<QString>();
    }
    if (m_instructionCatalog == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Instruction catalog is unavailable.");
        }
        return missing<QString>();
    }

    const auto document = m_instructionCatalog->instructionByKey(*instructionKey, errorMessage);
    if (!document.has_value()) {
        return missing<QString>();
    }

    return Nullable<QString>::fromValue(document->content);
}

Nullable<QMap<QString, QJsonValue>> SessionController::configForReasoningEffort(
    const QString &reasoningEffort
) const {
    QMap<QString, QJsonValue> config;
    config.insert(QStringLiteral("modelReasoningEffort"), QJsonValue(normalizedReasoningEffort(reasoningEffort)));
    return Nullable<QMap<QString, QJsonValue>>::fromValue(config);
}

std::optional<qodex::storage::ThreadSettingsRecord> SessionController::loadStoredThreadSettings(
    const QString &threadId,
    QString *errorMessage
) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (m_databaseManager == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Thread settings database is unavailable.");
        }
        return std::nullopt;
    }

    return m_databaseManager->loadThreadSettings(threadId, errorMessage);
}

bool SessionController::saveStoredThreadSettings(const qodex::storage::ThreadSettingsRecord &record) {
    if (m_databaseManager == nullptr) {
        return false;
    }

    QString errorMessage;
    if (m_databaseManager->saveThreadSettings(record, &errorMessage)) {
        return true;
    }

    qWarning().noquote() << QStringLiteral("Failed to save thread settings for %1: %2").arg(record.threadId, errorMessage);
    return false;
}

bool SessionController::queueThreadRenameRequest(const QString &threadId, const QString &newName) {
    const JsonRpcId requestId = m_client->sendThreadNameSetRequest(newName, threadId);
    if (!requestId.isValid()) {
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/name/set request."));
        return false;
    }

    m_pendingRenameRequests.insert(requestId.toKey(), {threadId, newName});
    return true;
}

void SessionController::start() {
    if (m_startRequested) {
        return;
    }
    m_startRequested = true;

    if (m_config.codexProgram.isEmpty()) {
        const QString message = QStringLiteral("codex executable not found on PATH.");
        m_mainWindow->setStatusMessage(message);
        finishStartup(message);
        return;
    }

    const QString message = QStringLiteral("Starting codex app-server...");
    m_mainWindow->setStatusMessage(message);
    emit startupProgressChanged(message, 70);
    m_transport->start(m_config.codexProgram, m_config.codexArguments);
}

void SessionController::onTransportStarted() {
    auto clientInfo = Ref<ClientInfo>::create();
    clientInfo->name = m_config.clientName;
    clientInfo->version = m_config.clientVersion;
    auto capabilities = Ref<InitializeCapabilities>::create();
    capabilities->experimentalApi = true;

    const QString message = QStringLiteral("Connected. Initializing Codex session...");
    m_mainWindow->setStatusMessage(message);
    emit startupProgressChanged(message, 82);
    const JsonRpcId requestId = m_client->sendInitializeRequest(
        Nullable<Ref<InitializeCapabilities>>::fromValue(capabilities),
        clientInfo
    );
    if (!requestId.isValid()) {
        const QString failureMessage = QStringLiteral("Failed to send initialize request.");
        m_mainWindow->setStatusMessage(failureMessage);
        finishStartup(failureMessage);
    }
}

void SessionController::onInitializeSucceeded(const JsonRpcId &id, const InitializeResponse &response) {
    Q_UNUSED(id);
    Q_UNUSED(response);

    if (!m_client->sendInitializedNotification()) {
        const QString message = QStringLiteral("Failed to send initialized notification.");
        m_mainWindow->setStatusMessage(message);
        finishStartup(message);
        return;
    }

    requestModels();
    emit startupProgressChanged(QStringLiteral("Loading thread list..."), 92);
    requestThreadLists();
}

void SessionController::onInitializeFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    Q_UNUSED(id);
    const QString message = QStringLiteral("Initialize failed: %1").arg(error.message);
    m_mainWindow->setStatusMessage(message);
    finishStartup(message);
}

void SessionController::onModelListSucceeded(const JsonRpcId &id, const ModelListResponse &response) {
    if (id.toKey() != m_modelListRequestKey) {
        return;
    }

    for (const Ref<Model> &model : response.data) {
        if (model) {
            m_pendingModels.append(model);
        }
    }

    if (response.nextCursor.hasValue()) {
        requestModelListPage(Nullable<QString>::fromValue(response.nextCursor.value()));
        return;
    }

    m_modelListRequestInFlight = false;
    m_modelListRequestKey.clear();
    m_models = m_pendingModels;
    m_pendingModels.clear();
    emit modelsChanged();
}

void SessionController::onModelListFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    if (id.toKey() != m_modelListRequestKey) {
        return;
    }

    m_modelListRequestInFlight = false;
    m_modelListRequestKey.clear();
    m_pendingModels.clear();
    m_mainWindow->setStatusMessage(QStringLiteral("model/list failed: %1").arg(error.message));
}

void SessionController::onThreadListSucceeded(const JsonRpcId &id, const ThreadListResponse &response) {
    const QString requestKey = id.toKey();
    const bool isArchivedRequest = requestKey == m_archivedThreadListRequestKey;
    const bool isActiveRequest = requestKey == m_activeThreadListRequestKey;
    if (!isArchivedRequest && !isActiveRequest) {
        return;
    }

    QList<qodex::domain::ThreadSummary> summaries;
    summaries.reserve(response.data.size());
    for (const Ref<Thread> &thread : response.data) {
        if (!thread) {
            continue;
        }
        reconcileLocalUnloadOverride(thread->id, thread->status.data());
        summaries.append(projectThreadSummary(*thread, isArchivedRequest));
    }

    if (isArchivedRequest) {
        m_archivedThreadListRequestInFlight = false;
        m_archivedThreadListRequestKey.clear();
    } else {
        m_activeThreadListRequestInFlight = false;
        m_activeThreadListRequestKey.clear();
    }

    m_threadStore->replaceThreadSummaries(std::move(summaries), isArchivedRequest);

    if (!m_activeThreadListRequestInFlight && !m_archivedThreadListRequestInFlight) {
        const QList<qodex::domain::ThreadSummary> allThreads = m_threadStore->threadSummaries();
        const qsizetype archivedCount = static_cast<qsizetype>(std::count_if(
            allThreads.begin(),
            allThreads.end(),
            [](const qodex::domain::ThreadSummary &summary) { return summary.archived; }
        ));
        const qsizetype activeCount = allThreads.size() - archivedCount;
        const QString message =
            QStringLiteral("Loaded %1 threads (%2 active, %3 archived).")
                .arg(allThreads.size())
                .arg(activeCount)
                .arg(archivedCount);
        m_mainWindow->setStatusMessage(message);
        finishStartup(message);
    }
}

void SessionController::onThreadListFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    const QString requestKey = id.toKey();
    if (requestKey == m_archivedThreadListRequestKey) {
        m_archivedThreadListRequestInFlight = false;
        m_archivedThreadListRequestKey.clear();
    } else if (requestKey == m_activeThreadListRequestKey) {
        m_activeThreadListRequestInFlight = false;
        m_activeThreadListRequestKey.clear();
    } else {
        return;
    }

    const QString message = QStringLiteral("thread/list failed: %1").arg(error.message);
    m_mainWindow->setStatusMessage(message);
    if (!m_activeThreadListRequestInFlight && !m_archivedThreadListRequestInFlight) {
        finishStartup(message);
    }
}

void SessionController::onThreadStartSucceeded(const JsonRpcId &id, const ThreadStartResponse &response) {
    const PendingThreadStartRequest pendingRequest = m_pendingThreadStartRequests.take(id.toKey());
    if (!response.thread) {
        requestThreadList(false);
        m_mainWindow->setStatusMessage(QStringLiteral("Thread created."));
        return;
    }
    m_locallyUnloadedThreadIds.remove(response.thread->id);

    const QString desiredName = pendingRequest.desiredName.trimmed();
    const QString title = desiredName.isEmpty() ? threadDisplayTitle(*response.thread) : desiredName;
    const qodex::domain::ThreadSummary summary = projectThreadSummary(*response.thread, false);
    m_threadStore->upsertThreadSummary(summary);
    if (!desiredName.isEmpty()) {
        m_threadStore->updateThreadTitle(response.thread->id, title);
        queueThreadRenameRequest(response.thread->id, pendingRequest.desiredName);
    }
    m_threadStore->setSelectedThreadId(response.thread->id);
    saveStoredThreadSettings(qodex::storage::ThreadSettingsRecord{
        .threadId = response.thread->id,
        .cwd = response.cwd,
        .model = response.model,
        .reasoningEffort = reasoningEffortKey(response.reasoningEffort),
        .instructionKey = pendingRequest.instructionKey,
    });

    LoadedThread *loadedThread = ensureLoadedThread(response.thread->id, title);
    if (loadedThread == nullptr) {
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to launch Thread UI for %1.").arg(title));
        return;
    }

    loadedThread->load(title, response.thread, response.model, response.reasoningEffort);
    m_mainWindow->setStatusMessage(QStringLiteral("Created thread %1.").arg(title));
}

void SessionController::onThreadStartFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    if (m_pendingThreadStartRequests.remove(id.toKey()) == 0) {
        return;
    }

    m_mainWindow->setStatusMessage(QStringLiteral("thread/start failed: %1").arg(error.message));
}

void SessionController::onThreadResumeSucceeded(const JsonRpcId &id, const qodex::codex::ThreadResumeResponse &response) {
    const PendingThreadResumeRequest pendingRequest = m_pendingThreadResumeRequests.take(id.toKey());
    if (pendingRequest.threadId.isEmpty() || !response.thread) {
        return;
    }
    m_locallyUnloadedThreadIds.remove(pendingRequest.threadId);
    m_threadStore->updateThreadStatusText(pendingRequest.threadId, projectedThreadStatusText(*response.thread));

    const auto existingSettings = loadStoredThreadSettings(pendingRequest.threadId);
    saveStoredThreadSettings(qodex::storage::ThreadSettingsRecord{
        .threadId = pendingRequest.threadId,
        .cwd = response.cwd,
        .model = response.model,
        .reasoningEffort = reasoningEffortKey(response.reasoningEffort),
        .instructionKey = pendingRequest.instructionKey.has_value()
            ? pendingRequest.instructionKey
            : (existingSettings.has_value() ? existingSettings->instructionKey : std::nullopt),
    });

    const QString title = threadDisplayTitle(*response.thread);
    LoadedThread *loadedThread = ensureLoadedThread(pendingRequest.threadId, title);
    if (loadedThread == nullptr) {
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to launch Thread UI for %1.").arg(title));
        return;
    }

    loadedThread->resume(title, response);
    m_mainWindow->setStatusMessage(QStringLiteral("Resumed thread %1.").arg(title));
}

void SessionController::onThreadResumeFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    if (m_pendingThreadResumeRequests.remove(id.toKey()) == 0) {
        return;
    }

    m_mainWindow->setStatusMessage(QStringLiteral("thread/resume failed: %1").arg(error.message));
}

void SessionController::onCreateThreadRequested() {
    const auto selection = runThreadSettingsDialog(
        qodex::ui::ThreadSettingsDialog::Mode::Create,
        defaultCreateThreadSettingsSelection()
    );
    if (!selection.has_value()) {
        return;
    }

    QString errorMessage;
    const Nullable<QString> baseInstructions =
        baseInstructionsFromInstructionKey(optionalTrimmedString(selection->instructionKey), &errorMessage);
    if (!errorMessage.isEmpty()) {
        QMessageBox::warning(m_mainWindow, QStringLiteral("Create New Thread"), errorMessage);
        return;
    }

    const JsonRpcId requestId = m_client->sendThreadStartRequest(
        std::nullopt,
        missing<std::variant<qodex::codex::AskForApprovalEnum, Ref<qodex::codex::AskForApprovalGranular>>>(),
        missing<qodex::codex::ApprovalsReviewer>(),
        baseInstructions,
        configForReasoningEffort(selection->reasoningEffort),
        Nullable<QString>::fromValue(selection->workingDirectory),
        missing<QString>(),
        missing<QList<Ref<qodex::codex::DynamicToolSpec>>>(),
        missing<QList<Ref<qodex::codex::TurnEnvironmentParams>>>(),
        missing<bool>(),
        std::nullopt,
        missing<qodex::codex::ThreadHistoryMode>(),
        missing<QString>(),
        Nullable<QString>::fromValue(selection->model),
        missing<QString>(),
        missing<Ref<qodex::codex::MultiAgentMode>>(),
        missing<QString>(),
        missing<qodex::codex::Personality>(),
        missing<QList<QString>>(),
        missing<qodex::codex::SandboxMode>(),
        missing<QList<Ref<qodex::codex::SelectedCapabilityRoot>>>(),
        missing<QString>(),
        missing<QString>(),
        missing<qodex::codex::ThreadStartSource>(),
        missing<QString>()
    );
    if (!requestId.isValid()) {
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/start request."));
        return;
    }

    m_pendingThreadStartRequests.insert(requestId.toKey(), PendingThreadStartRequest{
        .desiredName = selection->threadName,
        .instructionKey = optionalTrimmedString(selection->instructionKey),
    });
    m_mainWindow->setStatusMessage(QStringLiteral("Creating thread..."));
}

void SessionController::onRefreshRequested() {
    requestModels();
    requestThreadLists();
}

void SessionController::onThreadSelected(const QString &threadId) {
    m_threadStore->setSelectedThreadId(threadId);
}

void SessionController::onResumeThreadRequested(const QString &threadId) {
    const auto summary = m_threadStore->threadSummaryById(threadId);
    if (!summary.has_value()) {
        return;
    }

    if (loadedThreadForId(threadId) != nullptr) {
        ThreadUiProcess *threadUiProcess = m_threadUiProcessManager->threadUiProcessForThread(threadId);
        if (threadUiProcess != nullptr && threadUiProcess->isRunning()) {
            QString errorMessage;
            if (m_threadUiProcessManager->activateThreadUiForThread(threadId, &errorMessage)) {
                m_mainWindow->setStatusMessage(QStringLiteral("Activating %1...").arg(summary->title));
                return;
            }

            m_mainWindow->setStatusMessage(errorMessage);
            return;
        }
    }

    const JsonRpcId requestId = m_client->sendThreadResumeRequest(
        missing<std::variant<qodex::codex::AskForApprovalEnum, Ref<qodex::codex::AskForApprovalGranular>>>(),
        missing<qodex::codex::ApprovalsReviewer>(),
        missing<QString>(),
        missing<QMap<QString, QJsonValue>>(),
        missing<QString>(),
        missing<QString>(),
        std::nullopt,
        missing<QList<Ref<qodex::codex::ResponseItem>>>(),
        missing<Ref<qodex::codex::ThreadResumeInitialTurnsPageParams>>(),
        missing<QString>(),
        missing<QString>(),
        missing<QString>(),
        missing<QString>(),
        missing<qodex::codex::Personality>(),
        missing<QList<QString>>(),
        missing<qodex::codex::SandboxMode>(),
        missing<QString>(),
        threadId
    );
    if (!requestId.isValid()) {
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/resume request."));
        return;
    }

    const auto existingSettings = loadStoredThreadSettings(threadId);
    m_pendingThreadResumeRequests.insert(requestId.toKey(), PendingThreadResumeRequest{
        .threadId = threadId,
        .instructionKey = existingSettings.has_value() ? existingSettings->instructionKey : std::nullopt,
    });
    m_mainWindow->setStatusMessage(QStringLiteral("Resuming thread..."));
}

void SessionController::onEditThreadSettingsRequested(const QString &threadId) {
    const auto summary = m_threadStore->threadSummaryById(threadId);
    if (!summary.has_value()) {
        return;
    }

    QString helperText = QStringLiteral("Current settings are shown below. Unchanged values will be preserved.");
    if (summary->statusText != QStringLiteral("Not Loaded")) {
        helperText.append(
            QStringLiteral(" This thread is currently loaded, so non-name overrides will not be applied until it is closed.")
        );
    }

    const auto selection = runThreadSettingsDialog(
        qodex::ui::ThreadSettingsDialog::Mode::Edit,
        defaultEditThreadSettingsSelection(*summary),
        helperText
    );
    if (!selection.has_value()) {
        return;
    }

    const bool nameChanged = selection->threadName != summary->title;
    const bool overridesRequested = !selection->workingDirectoryUnchanged || !selection->modelUnchanged ||
        !selection->reasoningEffortUnchanged || !selection->instructionUnchanged;

    if (nameChanged) {
        if (!queueThreadRenameRequest(threadId, selection->threadName)) {
            return;
        }
        m_mainWindow->setStatusMessage(QStringLiteral("Updating thread name..."));
    }

    if (!overridesRequested) {
        return;
    }

    if (summary->statusText != QStringLiteral("Not Loaded")) {
        QMessageBox::information(
            m_mainWindow,
            QStringLiteral("Edit Thread Settings"),
            QStringLiteral(
                "Model, reasoning effort, working directory, and instructions can only be changed while the thread is not loaded. Close the thread first, then try again."
            )
        );
        return;
    }

    QString errorMessage;
    const Nullable<QString> baseInstructions = selection->instructionUnchanged
        ? missing<QString>()
        : baseInstructionsFromInstructionKey(selection->instructionKey, &errorMessage);
    if (!errorMessage.isEmpty()) {
        QMessageBox::warning(m_mainWindow, QStringLiteral("Edit Thread Settings"), errorMessage);
        return;
    }

    const JsonRpcId requestId = m_client->sendThreadResumeRequest(
        missing<std::variant<qodex::codex::AskForApprovalEnum, Ref<qodex::codex::AskForApprovalGranular>>>(),
        missing<qodex::codex::ApprovalsReviewer>(),
        baseInstructions,
        selection->reasoningEffortUnchanged ? missing<QMap<QString, QJsonValue>>()
                                            : configForReasoningEffort(selection->reasoningEffort),
        selection->workingDirectoryUnchanged ? missing<QString>()
                                            : Nullable<QString>::fromValue(selection->workingDirectory),
        missing<QString>(),
        std::nullopt,
        missing<QList<Ref<qodex::codex::ResponseItem>>>(),
        missing<Ref<qodex::codex::ThreadResumeInitialTurnsPageParams>>(),
        selection->modelUnchanged ? missing<QString>() : Nullable<QString>::fromValue(selection->model),
        missing<QString>(),
        missing<QString>(),
        missing<QString>(),
        missing<qodex::codex::Personality>(),
        missing<QList<QString>>(),
        missing<qodex::codex::SandboxMode>(),
        missing<QString>(),
        threadId
    );
    if (!requestId.isValid()) {
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/resume request."));
        return;
    }

    const auto existingSettings = loadStoredThreadSettings(threadId);
    m_pendingThreadResumeRequests.insert(requestId.toKey(), PendingThreadResumeRequest{
        .threadId = threadId,
        .instructionKey = selection->instructionUnchanged
            ? (existingSettings.has_value() ? existingSettings->instructionKey : std::nullopt)
            : optionalTrimmedString(selection->instructionKey),
    });
    m_mainWindow->setStatusMessage(QStringLiteral("Applying thread settings..."));
}

void SessionController::onRenameThreadRequested(const QString &threadId) {
    const auto summary = m_threadStore->threadSummaryById(threadId);
    if (!summary.has_value()) {
        return;
    }

    const QString currentTitle = summary->title;
    QDialog dialog(
        m_mainWindow,
        Qt::Dialog
            | Qt::FramelessWindowHint
            | Qt::CustomizeWindowHint
    );
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setObjectName(QStringLiteral("renameThreadDialog"));
    dialog.setStyleSheet(
        QStringLiteral(
            "#renameThreadDialog {"
            "  background: palette(window);"
            "  border: 1px solid palette(mid);"
            "  border-radius: 8px;"
            "}"
        )
    );

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);

    auto *label = new QLabel(QStringLiteral("Thread name:"), &dialog);
    auto *lineEdit = new QLineEdit(currentTitle, &dialog);
    lineEdit->selectAll();

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton *renameButton = buttonBox->addButton(QStringLiteral("Rename"), QDialogButtonBox::AcceptRole);
    renameButton->setDefault(true);

    layout->addWidget(label);
    layout->addWidget(lineEdit);
    layout->addWidget(buttonBox);

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->setSizeConstraint(QLayout::SetFixedSize);
    dialog.adjustSize();
    if (m_mainWindow != nullptr) {
        const QPoint center = m_mainWindow->geometry().center();
        dialog.move(center.x() - dialog.width() / 2, center.y() - dialog.height() / 2);
    }
    lineEdit->setFocus();

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString newName = lineEdit->text();
    if (newName == currentTitle) {
        return;
    }

    if (!queueThreadRenameRequest(threadId, newName)) {
        return;
    }
    m_mainWindow->setStatusMessage(QStringLiteral("Renaming thread..."));
}

void SessionController::onForkThreadRequested(const QString &threadId) {
    const auto summary = m_threadStore->threadSummaryById(threadId);
    if (!summary.has_value()) {
        return;
    }

    const auto selection = runThreadSettingsDialog(
        qodex::ui::ThreadSettingsDialog::Mode::Fork,
        defaultForkThreadSettingsSelection(*summary)
    );
    if (!selection.has_value()) {
        return;
    }

    QString errorMessage;
    const Nullable<QString> baseInstructions = selection->instructionUnchanged
        ? missing<QString>()
        : baseInstructionsFromInstructionKey(selection->instructionKey, &errorMessage);
    if (!errorMessage.isEmpty()) {
        QMessageBox::warning(m_mainWindow, QStringLiteral("Fork Thread"), errorMessage);
        return;
    }

    const auto sourceSettings = loadStoredThreadSettings(threadId);

    const JsonRpcId requestId = m_client->sendThreadForkRequest(
        missing<std::variant<qodex::codex::AskForApprovalEnum, Ref<qodex::codex::AskForApprovalGranular>>>(),
        missing<qodex::codex::ApprovalsReviewer>(),
        baseInstructions,
        missing<QString>(),
        selection->reasoningEffortUnchanged ? missing<QMap<QString, QJsonValue>>()
                                            : configForReasoningEffort(selection->reasoningEffort),
        selection->workingDirectoryUnchanged ? missing<QString>()
                                            : Nullable<QString>::fromValue(selection->workingDirectory),
        std::nullopt,
        missing<QString>(),
        std::nullopt,
        std::nullopt,
        missing<QString>(),
        selection->modelUnchanged ? missing<QString>() : Nullable<QString>::fromValue(selection->model),
        missing<QString>(),
        missing<QString>(),
        missing<QString>(),
        missing<QList<QString>>(),
        missing<qodex::codex::SandboxMode>(),
        missing<QString>(),
        threadId,
        missing<QString>()
    );
    if (!requestId.isValid()) {
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/fork request."));
        return;
    }

    m_pendingForkRequests.insert(requestId.toKey(), PendingThreadForkRequest{
        .sourceThreadId = threadId,
        .desiredName = selection->threadName,
        .instructionKey = selection->instructionUnchanged
            ? (sourceSettings.has_value() ? sourceSettings->instructionKey : std::nullopt)
            : optionalTrimmedString(selection->instructionKey),
    });
    m_mainWindow->setStatusMessage(QStringLiteral("Forking thread..."));
}

void SessionController::onCloseThreadsRequested(const QStringList &threadIds) {
    QStringList queuedThreadIds;
    queuedThreadIds.reserve(threadIds.size());

    for (const QString &threadId : threadIds) {
        const auto summary = m_threadStore->threadSummaryById(threadId);
        if (!summary.has_value() || summary->statusText == QStringLiteral("Not Loaded")) {
            continue;
        }

        const JsonRpcId requestId = m_client->sendThreadUnsubscribeRequest(threadId);
        if (!requestId.isValid()) {
            m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/unsubscribe request."));
            continue;
        }

        m_pendingUnsubscribeRequests.insert(requestId.toKey(), threadId);
        queuedThreadIds.append(threadId);
    }

    if (!queuedThreadIds.isEmpty()) {
        m_mainWindow->setStatusMessage(
            queuedThreadIds.size() == 1 ? QStringLiteral("Closing thread...")
                                        : QStringLiteral("Closing %1 threads...").arg(queuedThreadIds.size())
        );
    }
}

void SessionController::onArchiveThreadsRequested(const QStringList &threadIds) {
    QStringList queuedThreadIds;
    queuedThreadIds.reserve(threadIds.size());

    for (const QString &threadId : threadIds) {
        const auto summary = m_threadStore->threadSummaryById(threadId);
        if (!summary.has_value() || summary->archived) {
            continue;
        }

        const JsonRpcId requestId = m_client->sendThreadArchiveRequest(threadId);
        if (!requestId.isValid()) {
            m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/archive request."));
            continue;
        }

        m_pendingArchiveRequests.insert(requestId.toKey(), threadId);
        queuedThreadIds.append(threadId);
    }

    if (!queuedThreadIds.isEmpty()) {
        m_mainWindow->setStatusMessage(
            queuedThreadIds.size() == 1 ? QStringLiteral("Archiving thread...")
                                        : QStringLiteral("Archiving %1 threads...").arg(queuedThreadIds.size())
        );
    }
}

void SessionController::onUnarchiveThreadsRequested(const QStringList &threadIds) {
    QStringList queuedThreadIds;
    queuedThreadIds.reserve(threadIds.size());

    for (const QString &threadId : threadIds) {
        const auto summary = m_threadStore->threadSummaryById(threadId);
        if (!summary.has_value() || !summary->archived) {
            continue;
        }

        const JsonRpcId requestId = m_client->sendThreadUnarchiveRequest(threadId);
        if (!requestId.isValid()) {
            m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/unarchive request."));
            continue;
        }

        m_pendingUnarchiveRequests.insert(requestId.toKey(), threadId);
        queuedThreadIds.append(threadId);
    }

    if (!queuedThreadIds.isEmpty()) {
        m_mainWindow->setStatusMessage(
            queuedThreadIds.size() == 1 ? QStringLiteral("Unarchiving thread...")
                                        : QStringLiteral("Unarchiving %1 threads...").arg(queuedThreadIds.size())
        );
    }
}

void SessionController::onTransportErrorOccurred(const QString &message) {
    m_mainWindow->setStatusMessage(message);
    if (!m_startupFinished && !m_transport->isRunning()) {
        finishStartup(message);
    }
}

void SessionController::onTransportProcessExited(const int exitCode, const QProcess::ExitStatus exitStatus) {
    const QString statusText = exitStatus == QProcess::NormalExit
        ? QStringLiteral("exited")
        : QStringLiteral("crashed");
    const QString message = QStringLiteral("codex app-server %1 (code %2).").arg(statusText).arg(exitCode);
    m_mainWindow->setStatusMessage(message);
    if (!m_startupFinished) {
        finishStartup(message);
    }
}

void SessionController::onThreadNameSetSucceeded(const JsonRpcId &id, EmptyObject response) {
    Q_UNUSED(response);

    const auto request = m_pendingRenameRequests.take(id.toKey());
    if (request.first.isEmpty()) {
        return;
    }

    m_threadStore->updateThreadTitle(request.first, request.second);
    m_mainWindow->setStatusMessage(QStringLiteral("Thread renamed."));
}

void SessionController::onThreadNameSetFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    const auto request = m_pendingRenameRequests.take(id.toKey());
    if (request.first.isEmpty()) {
        return;
    }

    m_mainWindow->setStatusMessage(
        QStringLiteral("Failed to rename thread %1: %2").arg(request.first, error.message)
    );
}

void SessionController::onThreadArchiveSucceeded(const JsonRpcId &id, EmptyObject response) {
    Q_UNUSED(response);

    const QString requestKey = id.toKey();
    const QString threadId = m_pendingArchiveRequests.take(requestKey);
    if (threadId.isEmpty()) {
        return;
    }

    if (!m_threadStore->setThreadArchived(threadId, true)) {
        requestThreadList(true);
    }
}

void SessionController::onThreadArchiveFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    const QString threadId = m_pendingArchiveRequests.take(id.toKey());
    if (!threadId.isEmpty()) {
        m_mainWindow->setStatusMessage(
            QStringLiteral("Failed to archive thread %1: %2").arg(threadId, error.message)
        );
    }
}

void SessionController::onThreadUnsubscribeSucceeded(const JsonRpcId &id, const ThreadUnsubscribeResponse &response) {
    const QString threadId = m_pendingUnsubscribeRequests.take(id.toKey());
    if (threadId.isEmpty()) {
        return;
    }
    const bool wasThreadUiClose = m_pendingThreadUiCloseUnsubscribes.remove(threadId);

    if (response.status == ThreadUnsubscribeStatus::Unsubscribed
        || response.status == ThreadUnsubscribeStatus::NotLoaded
        || response.status == ThreadUnsubscribeStatus::NotSubscribed) {
        markThreadLocallyUnloaded(threadId, response.status != ThreadUnsubscribeStatus::NotLoaded);
        QString message;
        if (response.status == ThreadUnsubscribeStatus::Unsubscribed) {
            message = QStringLiteral("Closed thread.");
        } else if (response.status == ThreadUnsubscribeStatus::NotLoaded) {
            message = QStringLiteral("Thread was already closed.");
        } else if (wasThreadUiClose) {
            message = QStringLiteral("Thread UI closed.");
        } else {
            message = QStringLiteral("Thread was already not subscribed.");
        }
        m_mainWindow->setStatusMessage(message);
    }

    requestThreadLists();
}

void SessionController::onThreadUnsubscribeFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    const QString threadId = m_pendingUnsubscribeRequests.take(id.toKey());
    if (!threadId.isEmpty()) {
        const bool wasThreadUiClose = m_pendingThreadUiCloseUnsubscribes.remove(threadId);
        if (wasThreadUiClose) {
            markThreadLocallyUnloaded(threadId, true);
        }
        m_mainWindow->setStatusMessage(wasThreadUiClose
            ? QStringLiteral("Thread UI closed, but thread/unsubscribe failed for %1: %2").arg(threadId, error.message)
            : QStringLiteral("Failed to close thread %1: %2").arg(threadId, error.message)
        );
    }
}

void SessionController::onThreadForkSucceeded(const JsonRpcId &id, const ThreadForkResponse &response) {
    const PendingThreadForkRequest pendingRequest = m_pendingForkRequests.take(id.toKey());
    if (pendingRequest.sourceThreadId.isEmpty()) {
        return;
    }

    if (!response.thread) {
        requestThreadList(false);
        m_mainWindow->setStatusMessage(QStringLiteral("Thread forked."));
        return;
    }

    const qodex::domain::ThreadSummary summary = projectThreadSummary(*response.thread, false);
    m_threadStore->upsertThreadSummary(summary);
    const QString desiredName = pendingRequest.desiredName.trimmed();
    if (!desiredName.isEmpty()) {
        m_threadStore->updateThreadTitle(response.thread->id, desiredName);
        queueThreadRenameRequest(response.thread->id, pendingRequest.desiredName);
    }
    m_threadStore->setSelectedThreadId(summary.id);
    saveStoredThreadSettings(qodex::storage::ThreadSettingsRecord{
        .threadId = response.thread->id,
        .cwd = response.cwd,
        .model = response.model,
        .reasoningEffort = reasoningEffortKey(response.reasoningEffort),
        .instructionKey = pendingRequest.instructionKey,
    });
    m_mainWindow->setStatusMessage(QStringLiteral("Thread forked."));
}

void SessionController::onThreadForkFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    const PendingThreadForkRequest pendingRequest = m_pendingForkRequests.take(id.toKey());
    if (!pendingRequest.sourceThreadId.isEmpty()) {
        m_mainWindow->setStatusMessage(
            QStringLiteral("Failed to fork thread %1: %2").arg(pendingRequest.sourceThreadId, error.message)
        );
    }
}

void SessionController::onThreadUnarchiveSucceeded(const JsonRpcId &id, const ThreadUnarchiveResponse &response) {
    const QString requestKey = id.toKey();
    const QString requestedThreadId = m_pendingUnarchiveRequests.take(requestKey);
    if (!response.thread) {
        if (!requestedThreadId.isEmpty()) {
            requestThreadList(false);
        }
        return;
    }

    m_threadStore->upsertThreadSummary(projectThreadSummary(*response.thread, false));
}

void SessionController::onThreadUnarchiveFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    const QString threadId = m_pendingUnarchiveRequests.take(id.toKey());
    if (!threadId.isEmpty()) {
        m_mainWindow->setStatusMessage(
            QStringLiteral("Failed to unarchive thread %1: %2").arg(threadId, error.message)
        );
    }
}

void SessionController::onThreadStartedNotificationReceived(const ThreadStartedNotificationParams &params) {
    if (!params.thread) {
        return;
    }
    m_locallyUnloadedThreadIds.remove(params.thread->id);
    m_threadStore->upsertThreadSummary(projectThreadSummary(*params.thread, false));
}

void SessionController::onThreadClosedNotificationReceived(const ThreadClosedNotificationParams &params) {
    markThreadLocallyUnloaded(params.threadId, false);
}

void SessionController::onThreadNameUpdatedNotificationReceived(const ThreadNameUpdatedNotificationParams &params) {
    const auto existing = m_threadStore->threadSummaryById(params.threadId);
    if (!existing.has_value()) {
        return;
    }

    if (params.threadName.isMissing()) {
        return;
    }

    const QString title = params.threadName.hasValue() && !params.threadName.value().trimmed().isEmpty()
        ? params.threadName.value().trimmed()
        : (!existing->preview.trimmed().isEmpty() ? existing->preview.trimmed() : existing->id);
    m_threadStore->updateThreadTitle(params.threadId, title);
}

void SessionController::onThreadStatusChangedNotificationReceived(const ThreadStatusChangedNotificationParams &params) {
    if (!params.status) {
        return;
    }
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onThreadStatusChanged(*params.status);
    }
    reconcileLocalUnloadOverride(params.threadId, params.status.data());
    const QString statusText = shouldPresentThreadAsLocallyNotLoaded(params.threadId, params.status.data())
        ? QStringLiteral("Not Loaded")
        : threadStatusText(*params.status);
    m_threadStore->updateThreadStatusText(params.threadId, statusText);
}

void SessionController::onThreadArchivedNotificationReceived(const ThreadArchivedNotificationParams &params) {
    if (!m_threadStore->setThreadArchived(params.threadId, true)) {
        requestThreadList(true);
    }
}

void SessionController::onThreadUnarchivedNotificationReceived(const ThreadUnarchivedNotificationParams &params) {
    if (!m_threadStore->setThreadArchived(params.threadId, false)) {
        requestThreadList(false);
    }
}

void SessionController::onModelReroutedNotificationReceived(const ModelReroutedNotificationParams &params) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onModelReroutedNotification(params);
    }
}

void SessionController::onTurnStartedNotificationReceived(const TurnStartedNotificationParams &params) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onTurnStartedNotification(params);
    }
}

void SessionController::onTurnCompletedNotificationReceived(const TurnCompletedNotificationParams &params) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onTurnCompletedNotification(params);
    }
}

void SessionController::onItemStartedNotificationReceived(const ItemStartedNotificationParams &params) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onItemStartedNotification(params);
    }
}

void SessionController::onItemCompletedNotificationReceived(const ItemCompletedNotificationParams &params) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onItemCompletedNotification(params);
    }
}

void SessionController::onItemAgentMessageDeltaNotificationReceived(const ItemAgentMessageDeltaNotificationParams &params) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onItemAgentMessageDeltaNotification(params);
    }
}

void SessionController::onItemCommandExecutionOutputDeltaNotificationReceived(
    const ItemCommandExecutionOutputDeltaNotificationParams &params
) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onItemCommandExecutionOutputDeltaNotification(params);
    }
}

void SessionController::onItemCommandExecutionTerminalInteractionNotificationReceived(
    const ItemCommandExecutionTerminalInteractionNotificationParams &params
) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onItemCommandExecutionTerminalInteractionNotification(params);
    }
}

void SessionController::onItemFileChangeOutputDeltaNotificationReceived(
    const ItemFileChangeOutputDeltaNotificationParams &params
) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onItemFileChangeOutputDeltaNotification(params);
    }
}

void SessionController::onItemMcpToolCallProgressNotificationReceived(
    const ItemMcpToolCallProgressNotificationParams &params
) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onItemMcpToolCallProgressNotification(params);
    }
}

void SessionController::onItemPlanDeltaNotificationReceived(const ItemPlanDeltaNotificationParams &params) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onItemPlanDeltaNotification(params);
    }
}

void SessionController::onItemReasoningSummaryPartAddedNotificationReceived(
    const ItemReasoningSummaryPartAddedNotificationParams &params
) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onItemReasoningSummaryPartAddedNotification(params);
    }
}

void SessionController::onItemReasoningSummaryTextDeltaNotificationReceived(
    const ItemReasoningSummaryTextDeltaNotificationParams &params
) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onItemReasoningSummaryTextDeltaNotification(params);
    }
}

void SessionController::onItemReasoningTextDeltaNotificationReceived(const ItemReasoningTextDeltaNotificationParams &params) {
    if (LoadedThread *loadedThread = loadedThreadForId(params.threadId)) {
        loadedThread->onItemReasoningTextDeltaNotification(params);
    }
}

void SessionController::refreshSelectedThreadUi() {
    for (qodex::ui::MainWindow *window : std::as_const(m_windows)) {
        if (window == nullptr || window->threadListPane() == nullptr) {
            continue;
        }
        window->threadListPane()->setCurrentThreadId(m_threadStore->selectedThreadId());
    }
}

void SessionController::onThreadUiProcessExited(const QString &threadId) {
    if (threadId.isEmpty() || loadedThreadForId(threadId) == nullptr) {
        return;
    }

    if (m_pendingThreadUiCloseUnsubscribes.contains(threadId) || isThreadUnsubscribePending(threadId)) {
        return;
    }

    const auto summary = m_threadStore->threadSummaryById(threadId);
    if (summary.has_value() && summary->statusText == QStringLiteral("Not Loaded")) {
        queueUnloadThread(threadId);
        return;
    }

    const JsonRpcId requestId = m_client->sendThreadUnsubscribeRequest(threadId);
    if (!requestId.isValid()) {
        m_locallyUnloadedThreadIds.insert(threadId);
        m_threadStore->updateThreadStatusText(threadId, QStringLiteral("Not Loaded"));
        queueUnloadThread(threadId);
        m_mainWindow->setStatusMessage(QStringLiteral("Thread UI closed, but thread/unsubscribe could not be sent."));
        return;
    }

    m_pendingUnsubscribeRequests.insert(requestId.toKey(), threadId);
    m_pendingThreadUiCloseUnsubscribes.insert(threadId);
    m_locallyUnloadedThreadIds.insert(threadId);
    m_threadStore->updateThreadStatusText(threadId, QStringLiteral("Not Loaded"));
    m_mainWindow->setStatusMessage(QStringLiteral("Thread UI closed. Unloading thread..."));
}

LoadedThread *SessionController::loadedThreadForId(const QString &threadId) const {
    return m_loadedThreads.value(threadId, nullptr);
}

bool SessionController::isThreadUnsubscribePending(const QString &threadId) const {
    return std::any_of(
        m_pendingUnsubscribeRequests.cbegin(),
        m_pendingUnsubscribeRequests.cend(),
        [&threadId](const QString &pendingThreadId) { return pendingThreadId == threadId; }
    );
}

LoadedThread *SessionController::ensureLoadedThread(const QString &threadId, const QString &title) {
    LoadedThread *loadedThread = loadedThreadForId(threadId);
    if (loadedThread != nullptr) {
        return loadedThread;
    }

    ThreadUiProcess *threadUiProcess = m_threadUiProcessManager->launchThreadUiForThread(threadId, title);
    if (threadUiProcess == nullptr) {
        return nullptr;
    }

    loadedThread = new LoadedThread(threadId, title, m_client, threadUiProcess, this);
    QObject::connect(loadedThread, &LoadedThread::statusMessageRequested, this, [this](const QString &message) {
        m_mainWindow->setStatusMessage(message);
    });
    m_loadedThreads.insert(threadId, loadedThread);
    const QList<const LoadedThread *> orderedThreads = loadedThreads();
    const int row = orderedThreads.indexOf(loadedThread);
    emit loadedThreadAdded(loadedThread, row);
    return loadedThread;
}

void SessionController::unloadThread(const QString &threadId) {
    m_pendingThreadUiCloseUnsubscribes.remove(threadId);
    LoadedThread *loadedThread = loadedThreadForId(threadId);
    if (loadedThread != nullptr) {
        const QList<const LoadedThread *> orderedThreads = loadedThreads();
        const int row = orderedThreads.indexOf(loadedThread);
        emit loadedThreadAboutToBeRemoved(loadedThread, row);
    }

    loadedThread = m_loadedThreads.take(threadId);
    if (loadedThread != nullptr) {
        loadedThread->deleteLater();
    }
    m_threadUiProcessManager->destroyThreadUiForThread(threadId);
}

void SessionController::queueUnloadThread(const QString &threadId) {
    if (threadId.isEmpty()) {
        return;
    }

    QMetaObject::invokeMethod(this, [this, threadId] {
        unloadThread(threadId);
    }, Qt::QueuedConnection);
}

qodex::domain::ThreadSummary SessionController::projectThreadSummary(const Thread &thread, const bool archived) const {
    QString gitOrigin;
    QString gitBranch;
    QString gitSha;
    if (thread.gitInfo.hasValue() && thread.gitInfo.value()) {
        const Ref<qodex::codex::GitInfo> &gitInfo = thread.gitInfo.value();
        if (gitInfo->originUrl.hasValue()) {
            gitOrigin = gitInfo->originUrl.value();
        }
        if (gitInfo->branch.hasValue()) {
            gitBranch = gitInfo->branch.value();
        }
        if (gitInfo->sha.hasValue()) {
            gitSha = gitInfo->sha.value();
        }
    }

    return qodex::domain::ThreadSummary{
        .id = thread.id,
        .title = threadDisplayTitle(thread),
        .preview = thread.preview.trimmed(),
        .cwd = thread.cwd,
        .statusText = projectedThreadStatusText(thread),
        .sourceText = thread.source ? threadSourceText(*thread.source) : QStringLiteral("Unknown"),
        .modelProvider = thread.modelProvider,
        .cliVersion = thread.cliVersion,
        .path = thread.path.hasValue() ? thread.path.value() : QString{},
        .agentNickname = thread.agentNickname.hasValue() ? thread.agentNickname.value() : QString{},
        .agentRole = thread.agentRole.hasValue() ? thread.agentRole.value() : QString{},
        .gitOrigin = gitOrigin,
        .gitBranch = gitBranch,
        .gitSha = gitSha,
        .archived = archived,
        .ephemeral = thread.ephemeral,
        .createdAt = thread.createdAt,
        .updatedAt = thread.updatedAt,
    };
}

QString SessionController::projectedThreadStatusText(const Thread &thread) const {
    if (shouldPresentThreadAsLocallyNotLoaded(thread.id, thread.status.data())) {
        return QStringLiteral("Not Loaded");
    }
    return thread.status ? threadStatusText(*thread.status) : QStringLiteral("Unknown");
}

bool SessionController::shouldPresentThreadAsLocallyNotLoaded(
    const QString &threadId,
    const ThreadStatus *status
) const {
    if (threadId.isEmpty() || !m_locallyUnloadedThreadIds.contains(threadId) || loadedThreadForId(threadId) != nullptr) {
        return false;
    }

    return status == nullptr
        || status->kind == ThreadStatus::Kind::Idle
        || status->kind == ThreadStatus::Kind::NotLoaded;
}

void SessionController::reconcileLocalUnloadOverride(const QString &threadId, const ThreadStatus *status) {
    if (threadId.isEmpty() || !m_locallyUnloadedThreadIds.contains(threadId)) {
        return;
    }

    if (loadedThreadForId(threadId) != nullptr || (status != nullptr && status->kind != ThreadStatus::Kind::Idle)) {
        m_locallyUnloadedThreadIds.remove(threadId);
    }
}

void SessionController::markThreadLocallyUnloaded(const QString &threadId, const bool keepServerIdleOverride) {
    if (threadId.isEmpty()) {
        return;
    }

    if (keepServerIdleOverride) {
        m_locallyUnloadedThreadIds.insert(threadId);
    } else {
        m_locallyUnloadedThreadIds.remove(threadId);
    }

    unloadThread(threadId);
    m_threadStore->updateThreadStatusText(threadId, QStringLiteral("Not Loaded"));
}

QString SessionController::threadStatusText(const ThreadStatus &status) const {
    switch (status.kind) {
    case ThreadStatus::Kind::NotLoaded:
        return QStringLiteral("Not Loaded");
    case ThreadStatus::Kind::Idle:
        return QStringLiteral("Idle");
    case ThreadStatus::Kind::SystemError:
        return QStringLiteral("System Error");
    case ThreadStatus::Kind::Active:
        return QStringLiteral("Active");
    }

    return QStringLiteral("Unknown");
}

QString SessionController::threadDisplayTitle(const Thread &thread) const {
    if (thread.name.hasValue() && !thread.name.value().trimmed().isEmpty()) {
        return thread.name.value().trimmed();
    }
    if (!thread.preview.trimmed().isEmpty()) {
        return thread.preview.trimmed();
    }
    return thread.id;
}

QString SessionController::threadSourceText(const SessionSource &source) const {
    switch (source.kind) {
    case SessionSource::Kind::Cli:
        return QStringLiteral("CLI");
    case SessionSource::Kind::Vscode:
        return QStringLiteral("VSCode");
    case SessionSource::Kind::Exec:
        return QStringLiteral("Exec");
    case SessionSource::Kind::AppServer:
        return QStringLiteral("App Server");
    case SessionSource::Kind::Unknown:
        return QStringLiteral("Unknown");
    case SessionSource::Kind::Custom:
        if (const QString *custom = std::get_if<QString>(&source.payload)) {
            return custom->trimmed().isEmpty() ? QStringLiteral("Custom") : *custom;
        }
        return QStringLiteral("Custom");
    case SessionSource::Kind::SubAgent:
        break;
    }

    if (const Ref<SubAgentSource> *subAgent = std::get_if<Ref<SubAgentSource>>(&source.payload)) {
        if (!*subAgent) {
            return QStringLiteral("Sub-agent");
        }

        switch ((*subAgent)->kind) {
        case SubAgentSource::Kind::Review:
            return QStringLiteral("Sub-agent: Review");
        case SubAgentSource::Kind::Compact:
            return QStringLiteral("Sub-agent: Compact");
        case SubAgentSource::Kind::MemoryConsolidation:
            return QStringLiteral("Sub-agent: Memory");
        case SubAgentSource::Kind::ThreadSpawn:
            return QStringLiteral("Sub-agent: Spawn");
        case SubAgentSource::Kind::Other:
            if (const QString *other = std::get_if<QString>(&(*subAgent)->payload)) {
                return other->trimmed().isEmpty() ? QStringLiteral("Sub-agent: Other")
                                                  : QStringLiteral("Sub-agent: %1").arg(*other);
            }
            return QStringLiteral("Sub-agent: Other");
        }
    }

    return QStringLiteral("Sub-agent");
}

void SessionController::finishStartup(const QString &message) {
    if (m_startupFinished) {
        return;
    }

    m_startupFinished = true;
    emit startupProgressChanged(message, 100);
    emit startupFinished();
}

void SessionController::requestModels() {
    if (m_modelListRequestInFlight) {
        return;
    }

    m_pendingModels.clear();
    requestModelListPage(missing<QString>());
}

void SessionController::requestModelListPage(const Nullable<QString> &cursor) {
    m_modelListRequestInFlight = true;

    const JsonRpcId requestId = m_client->sendModelListRequest(
        cursor,
        Nullable<bool>::fromValue(true),
        missing<qint64>()
    );
    if (!requestId.isValid()) {
        m_modelListRequestInFlight = false;
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to send model/list request."));
        return;
    }

    m_modelListRequestKey = requestId.toKey();
}

void SessionController::requestThreadLists() {
    requestThreadList(false);
    requestThreadList(true);
}

void SessionController::requestThreadList(const bool archived) {
    const bool inFlight = archived ? m_archivedThreadListRequestInFlight : m_activeThreadListRequestInFlight;
    if (inFlight) {
        return;
    }

    if (archived) {
        m_archivedThreadListRequestInFlight = true;
    } else {
        m_activeThreadListRequestInFlight = true;
    }
    m_mainWindow->setStatusMessage(QStringLiteral("Loading threads..."));

    const JsonRpcId requestId = m_client->sendThreadListRequest(
        missing<QString>(),
        Nullable<bool>::fromValue(archived),
        missing<QString>(),
        missing<std::variant<QString, QList<QString>>>(),
        missing<qint64>(),
        missing<QList<QString>>(),
        missing<QString>(),
        missing<QString>(),
        missing<QString>(),
        missing<qodex::codex::SortDirection>(),
        missing<ThreadSortKey>(),
        missing<QList<ThreadSourceKind>>(),
        std::nullopt
    );
    if (!requestId.isValid()) {
        if (archived) {
            m_archivedThreadListRequestInFlight = false;
        } else {
            m_activeThreadListRequestInFlight = false;
        }
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/list request."));
        return;
    }

    if (archived) {
        m_archivedThreadListRequestKey = requestId.toKey();
    } else {
        m_activeThreadListRequestKey = requestId.toKey();
    }
}

}  // namespace qodex::app
