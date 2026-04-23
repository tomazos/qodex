#include "threadui_native/ThreadUiEngine.h"

#include <asio.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
#include <utility>

#include "qodex_to_ui.qodex_rpc.h"
#include "threadui/ThreadUiIpcFraming.h"
#include "ui_to_qodex.qodex_rpc.h"

namespace qodex::threadui::native {

namespace {

bool initialized = false;
std::int64_t frameCount = 0;
qodex::threadui::native::LaunchConfig currentLaunchConfig;
FrameCountDisplayCallback frameCountDisplayCallback;
std::mutex fatalErrorMutex;
std::string pendingFatalError;
std::mutex pendingErrorMutex;
std::string pendingError;
std::mutex pendingItemsMutex;
std::vector<qodex::threadui::native::DisplayItem> pendingItems;
std::mutex pendingResolvedLinksMutex;
std::vector<qodex::threadui::native::ResolvedLink> pendingResolvedLinks;
std::mutex pendingThreadStatusMutex;
std::optional<qodex::threadui::native::ThreadStatusUpdate> pendingThreadStatus;
struct IpcClientState;
std::unique_ptr<IpcClientState> ipcClientState;
namespace QodexToUiRpc = qodex::threadui::ipc::qodex_to_ui::rpc::QodexToUi;
namespace UiToQodexRpc = qodex::threadui::ipc::ui_to_qodex::rpc::UiToQodex;

std::string fileChangeKindToString(const qodex::threadui::ipc::common::FileChangeKind kind) {
    switch (kind) {
    case qodex::threadui::ipc::common::FILE_CHANGE_KIND_ADD:
        return "add";
    case qodex::threadui::ipc::common::FILE_CHANGE_KIND_DELETE:
        return "delete";
    case qodex::threadui::ipc::common::FILE_CHANGE_KIND_UPDATE:
        return "update";
    case qodex::threadui::ipc::common::FILE_CHANGE_KIND_UNSPECIFIED:
        break;
    }

    return "unknown";
}

std::string commandActionKindToString(const qodex::threadui::ipc::common::CommandExecutionActionKind kind) {
    switch (kind) {
    case qodex::threadui::ipc::common::COMMAND_EXECUTION_ACTION_KIND_READ:
        return "read";
    case qodex::threadui::ipc::common::COMMAND_EXECUTION_ACTION_KIND_LIST_FILES:
        return "listFiles";
    case qodex::threadui::ipc::common::COMMAND_EXECUTION_ACTION_KIND_SEARCH:
        return "search";
    case qodex::threadui::ipc::common::COMMAND_EXECUTION_ACTION_KIND_UNKNOWN:
        return "unknown";
    case qodex::threadui::ipc::common::COMMAND_EXECUTION_ACTION_KIND_UNSPECIFIED:
        break;
    }

    return "unspecified";
}

std::string linkKindToString(const qodex::threadui::ipc::common::LinkKind kind) {
    switch (kind) {
    case qodex::threadui::ipc::common::LINK_KIND_WEB:
        return "web";
    case qodex::threadui::ipc::common::LINK_KIND_FILE:
        return "file";
    case qodex::threadui::ipc::common::LINK_KIND_MAILTO:
        return "mailto";
    case qodex::threadui::ipc::common::LINK_KIND_UNKNOWN:
    case qodex::threadui::ipc::common::LINK_KIND_UNSPECIFIED:
        break;
    }

    return "unknown";
}

std::string linkActionKindToString(const qodex::threadui::ipc::common::LinkActionKind kind) {
    switch (kind) {
    case qodex::threadui::ipc::common::LINK_ACTION_KIND_OPEN:
        return "open";
    case qodex::threadui::ipc::common::LINK_ACTION_KIND_OPEN_EXTERNALLY:
        return "open_externally";
    case qodex::threadui::ipc::common::LINK_ACTION_KIND_REVEAL_IN_FOLDER:
        return "reveal_in_folder";
    case qodex::threadui::ipc::common::LINK_ACTION_KIND_NONE:
    case qodex::threadui::ipc::common::LINK_ACTION_KIND_UNSPECIFIED:
        break;
    }

    return "none";
}

std::string threadStatusKindToString(const qodex::threadui::ipc::common::ThreadStatusKind kind) {
    switch (kind) {
    case qodex::threadui::ipc::common::THREAD_STATUS_KIND_NOT_LOADED:
        return "not_loaded";
    case qodex::threadui::ipc::common::THREAD_STATUS_KIND_IDLE:
        return "idle";
    case qodex::threadui::ipc::common::THREAD_STATUS_KIND_SYSTEM_ERROR:
        return "system_error";
    case qodex::threadui::ipc::common::THREAD_STATUS_KIND_ACTIVE:
        return "active";
    case qodex::threadui::ipc::common::THREAD_STATUS_KIND_UNSPECIFIED:
        break;
    }

    return "unknown";
}

std::string threadStatusActiveFlagToString(const qodex::threadui::ipc::common::ThreadStatusActiveFlag activeFlag) {
    switch (activeFlag) {
    case qodex::threadui::ipc::common::THREAD_STATUS_ACTIVE_FLAG_WAITING_ON_APPROVAL:
        return "waiting_on_approval";
    case qodex::threadui::ipc::common::THREAD_STATUS_ACTIVE_FLAG_WAITING_ON_USER_INPUT:
        return "waiting_on_user_input";
    case qodex::threadui::ipc::common::THREAD_STATUS_ACTIVE_FLAG_UNSPECIFIED:
        break;
    }

    return "unknown";
}

std::string threadReasoningEffortToString(const qodex::threadui::ipc::common::ThreadReasoningEffort reasoningEffort) {
    switch (reasoningEffort) {
    case qodex::threadui::ipc::common::THREAD_REASONING_EFFORT_NONE:
        return "none";
    case qodex::threadui::ipc::common::THREAD_REASONING_EFFORT_MINIMAL:
        return "minimal";
    case qodex::threadui::ipc::common::THREAD_REASONING_EFFORT_LOW:
        return "low";
    case qodex::threadui::ipc::common::THREAD_REASONING_EFFORT_MEDIUM:
        return "medium";
    case qodex::threadui::ipc::common::THREAD_REASONING_EFFORT_HIGH:
        return "high";
    case qodex::threadui::ipc::common::THREAD_REASONING_EFFORT_XHIGH:
        return "xhigh";
    case qodex::threadui::ipc::common::THREAD_REASONING_EFFORT_UNSPECIFIED:
        break;
    }

    return {};
}

bool hasIpcTarget(const qodex::threadui::native::LaunchConfig &launchConfig) {
    return !launchConfig.host.empty() && launchConfig.port != 0;
}

struct IpcClientState final {
    explicit IpcClientState(const qodex::threadui::native::LaunchConfig &config)
        : launchConfig(config),
          workGuard(asio::make_work_guard(ioContext)),
          resolver(ioContext),
          socket(ioContext),
          reconnectTimer(ioContext) {
    }

    qodex::threadui::native::LaunchConfig launchConfig;
    asio::io_context ioContext;
    asio::executor_work_guard<asio::io_context::executor_type> workGuard;
    asio::ip::tcp::resolver resolver;
    asio::ip::tcp::socket socket;
    asio::steady_timer reconnectTimer;
    std::thread thread;
    std::array<char, 4096> readChunk{};
    std::string inputBuffer;
    std::deque<std::string> writeQueue;
    std::unordered_set<std::uint64_t> pendingSendUserInputRequestIds;
    std::unordered_set<std::uint64_t> pendingResolveLinkRequestIds;
    std::atomic<std::uint64_t> nextRequestId{1};
    std::uint64_t pendingLoginRequestId = 0;
    bool stopRequested = false;
    bool connected = false;
    bool authenticated = false;
    bool writeInProgress = false;
};

std::string endpointDescription(const qodex::threadui::native::LaunchConfig &launchConfig) {
    return launchConfig.host + ":" + std::to_string(launchConfig.port);
}

void scheduleConnect(IpcClientState *state);
void scheduleReconnect(IpcClientState *state, const asio::error_code &errorCode);

void stopIpcClient();

void recordFatalError(const std::string &message) {
    if (message.empty()) {
        return;
    }

    std::lock_guard lock(fatalErrorMutex);
    if (pendingFatalError.empty()) {
        pendingFatalError = message;
    }
}

void recordPendingError(const std::string &message) {
    if (message.empty()) {
        return;
    }

    std::lock_guard lock(pendingErrorMutex);
    if (pendingError.empty()) {
        pendingError = message;
    }
}

void queueResolvedLink(qodex::threadui::native::ResolvedLink resolvedLink) {
    std::lock_guard lock(pendingResolvedLinksMutex);
    pendingResolvedLinks.push_back(std::move(resolvedLink));
}

void queueThreadStatus(qodex::threadui::native::ThreadStatusUpdate statusUpdate) {
    std::lock_guard lock(pendingThreadStatusMutex);
    pendingThreadStatus = std::move(statusUpdate);
}

void beginWriteQueuedFrames(IpcClientState *state);

void queueFrameForWrite(IpcClientState *state, std::string frame) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    state->writeQueue.push_back(std::move(frame));
    if (!state->writeInProgress) {
        beginWriteQueuedFrames(state);
    }
}

void beginWriteQueuedFrames(IpcClientState *state) {
    if (state == nullptr || state->stopRequested || state->writeQueue.empty()) {
        return;
    }

    state->writeInProgress = true;
    asio::async_write(
        state->socket,
        asio::buffer(state->writeQueue.front()),
        [state](const asio::error_code &writeError, const std::size_t) {
            if (state->stopRequested) {
                return;
            }

            if (writeError) {
                scheduleReconnect(state, writeError);
                return;
            }

            state->writeQueue.pop_front();
            if (state->writeQueue.empty()) {
                state->writeInProgress = false;
                return;
            }

            beginWriteQueuedFrames(state);
        }
    );
}

void queueEnvelopeForWrite(IpcClientState *state, const qodex::threadui::ipc::common::RpcEnvelope &envelope) {
    queueFrameForWrite(state, qodex::threadui::ipc::encodeEnvelopeFrame(envelope));
}

void scheduleReconnect(IpcClientState *state, const asio::error_code &errorCode) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    if (errorCode != asio::error::operation_aborted) {
        std::cerr << "Thread UI IPC reconnect scheduled for "
                  << endpointDescription(state->launchConfig)
                  << ": "
                  << errorCode.message()
                  << std::endl;
    }

    state->connected = false;
    state->authenticated = false;
    state->pendingLoginRequestId = 0;
    state->inputBuffer.clear();
    state->writeQueue.clear();
    state->writeInProgress = false;
    state->reconnectTimer.expires_after(std::chrono::milliseconds(100));
    state->reconnectTimer.async_wait([state](const asio::error_code &timerError) {
        if (timerError == asio::error::operation_aborted || state->stopRequested) {
            return;
        }

        scheduleConnect(state);
    });
}

void stopClientOnProtocolFailure(IpcClientState *state, const std::string &message) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    std::cerr << message << std::endl;
    recordFatalError(message);
    state->stopRequested = true;
    asio::error_code ignoredError;
    state->reconnectTimer.cancel();
    state->resolver.cancel();
    state->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignoredError);
    state->socket.close(ignoredError);
    state->workGuard.reset();
}

void beginReadFromSocket(IpcClientState *state);

void queueDisplayItems(const qodex::threadui::ipc::qodex_to_ui::AddItemsRequest &request) {
    std::lock_guard lock(pendingItemsMutex);
    pendingItems.reserve(pendingItems.size() + static_cast<std::size_t>(request.items_size()));

    for (const auto &item : request.items()) {
        const std::string itemId = item.item_id();
        switch (item.kind_case()) {
        case qodex::threadui::ipc::common::Item::kUserMessage:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .id = itemId,
                .kind = "user",
                .text = item.user_message().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::kAgentMessage:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .id = itemId,
                .kind = "agent",
                .text = item.agent_message().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::kPlan:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .id = itemId,
                .kind = "plan",
                .text = item.plan().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::kReasoning:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .id = itemId,
                .kind = "reasoning",
                .text = item.reasoning().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::kCommandExecution:
        {
            qodex::threadui::native::DisplayItem displayItem;
            displayItem.id = itemId;
            displayItem.kind = "command_execution";
            displayItem.commandExecution.command = item.command_execution().command();
            displayItem.commandExecution.cwd = item.command_execution().cwd();
            displayItem.commandExecution.status = item.command_execution().status();
            displayItem.commandExecution.hasExitCode = item.command_execution().has_exit_code();
            displayItem.commandExecution.exitCode = item.command_execution().exit_code();
            displayItem.commandExecution.hasDurationMs = item.command_execution().has_duration_ms();
            displayItem.commandExecution.durationMs = item.command_execution().duration_ms();
            displayItem.commandExecution.processId = item.command_execution().process_id();
            displayItem.commandExecution.aggregatedOutput = item.command_execution().aggregated_output();
            displayItem.commandExecution.actions.reserve(
                static_cast<std::size_t>(item.command_execution().actions_size())
            );
            for (const auto &action : item.command_execution().actions()) {
                displayItem.commandExecution.actions.push_back(qodex::threadui::native::DisplayCommandExecution::Action{
                    .kind = commandActionKindToString(action.kind()),
                    .path = action.path(),
                    .query = action.query(),
                    .name = action.name(),
                    .command = action.command(),
                });
            }
            pendingItems.push_back(std::move(displayItem));
            break;
        }
        case qodex::threadui::ipc::common::Item::kFileChange:
        {
            qodex::threadui::native::DisplayItem displayItem;
            displayItem.id = itemId;
            displayItem.kind = "file_change";
            displayItem.fileChange.status = item.file_change().status();
            displayItem.fileChange.changes.reserve(static_cast<std::size_t>(item.file_change().changes_size()));
            for (const auto &change : item.file_change().changes()) {
                displayItem.fileChange.changes.push_back(qodex::threadui::native::DisplayFileChangeChange{
                    .path = change.path(),
                    .kind = fileChangeKindToString(change.kind()),
                    .movePath = change.move_path(),
                    .diff = change.diff(),
                });
            }
            pendingItems.push_back(std::move(displayItem));
            break;
        }
        case qodex::threadui::ipc::common::Item::kMcpToolCall:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .id = itemId,
                .kind = "mcp_tool_call",
                .text = item.mcp_tool_call().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::kDynamicToolCall:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .id = itemId,
                .kind = "dynamic_tool_call",
                .text = item.dynamic_tool_call().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::kCollabAgentToolCall:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .id = itemId,
                .kind = "collab_agent_tool_call",
                .text = item.collab_agent_tool_call().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::kWebSearch:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .id = itemId,
                .kind = "web_search",
                .text = item.web_search().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::kImageView:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .id = itemId,
                .kind = "image_view",
                .text = item.image_view().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::kImageGeneration:
        {
            qodex::threadui::native::DisplayItem displayItem;
            displayItem.id = itemId;
            displayItem.kind = "image_generation";
            displayItem.imageGeneration.result = item.image_generation().result();
            displayItem.imageGeneration.revisedPrompt = item.image_generation().revised_prompt();
            displayItem.imageGeneration.savedPath = item.image_generation().saved_path();
            displayItem.imageGeneration.status = item.image_generation().status();
            pendingItems.push_back(std::move(displayItem));
            break;
        }
        case qodex::threadui::ipc::common::Item::kEnteredReviewMode:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .id = itemId,
                .kind = "entered_review_mode",
                .text = item.entered_review_mode().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::kExitedReviewMode:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .id = itemId,
                .kind = "exited_review_mode",
                .text = item.exited_review_mode().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::kContextCompaction:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .id = itemId,
                .kind = "context_compaction",
                .text = item.context_compaction().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::KIND_NOT_SET:
            break;
        }
    }
}

void sendAddItemsResponse(
    IpcClientState *state,
    const std::uint64_t requestId,
    const qodex::threadui::ipc::common::ResultStatus status,
    const std::string &message
) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    qodex::threadui::ipc::qodex_to_ui::AddItemsResponse response;
    response.set_status(status);
    response.set_message(message);
    queueEnvelopeForWrite(
        state,
        qodex::threadui::ipc::makeResponseEnvelope<QodexToUiRpc::AddItems>(requestId, response)
    );
}

void sendSetThreadStatusResponse(
    IpcClientState *state,
    const std::uint64_t requestId,
    const qodex::threadui::ipc::common::ResultStatus status,
    const std::string &message
) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    qodex::threadui::ipc::qodex_to_ui::SetThreadStatusResponse response;
    response.set_status(status);
    response.set_message(message);
    queueEnvelopeForWrite(
        state,
        qodex::threadui::ipc::makeResponseEnvelope<QodexToUiRpc::SetThreadStatus>(requestId, response)
    );
}

void handleEnvelope(IpcClientState *state, const qodex::threadui::ipc::common::RpcEnvelope &envelope) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    if (envelope.is_response()) {
        struct UiToQodexResponseHandler final {
            IpcClientState *state;

            bool onLoginResponse(
                const std::uint64_t requestId,
                const qodex::threadui::ipc::ui_to_qodex::LoginResponse &response,
                std::string *errorMessage
            ) {
                if (requestId != state->pendingLoginRequestId) {
                    if (errorMessage != nullptr) {
                        *errorMessage = "Received a Login response for an unknown request id.";
                    }
                    return false;
                }

                state->pendingLoginRequestId = 0;
                if (response.status() != qodex::threadui::ipc::common::RESULT_STATUS_OK) {
                    if (errorMessage != nullptr) {
                        *errorMessage = "Thread UI Login failed: " + response.message();
                    }
                    return false;
                }

                state->authenticated = true;
                std::cout << "Thread UI Login succeeded: " << response.message() << std::endl;
                return true;
            }

            bool onSendUserInputResponse(
                const std::uint64_t requestId,
                const qodex::threadui::ipc::ui_to_qodex::SendUserInputResponse &response,
                std::string *errorMessage
            ) {
                if (!state->pendingSendUserInputRequestIds.contains(requestId)) {
                    if (errorMessage != nullptr) {
                        *errorMessage = "Received a SendUserInput response for an unknown request id.";
                    }
                    return false;
                }

                state->pendingSendUserInputRequestIds.erase(requestId);
                if (response.status() != qodex::threadui::ipc::common::RESULT_STATUS_OK) {
                    recordPendingError("SendUserInput failed: " + response.message());
                }
                return true;
            }

            bool onResolveLinkResponse(
                const std::uint64_t requestId,
                const qodex::threadui::ipc::ui_to_qodex::ResolveLinkResponse &response,
                std::string *errorMessage
            ) {
                if (!state->pendingResolveLinkRequestIds.contains(requestId)) {
                    if (errorMessage != nullptr) {
                        *errorMessage = "Received a ResolveLink response for an unknown request id.";
                    }
                    return false;
                }

                state->pendingResolveLinkRequestIds.erase(requestId);
                qodex::threadui::native::ResolvedLink resolvedLink;
                resolvedLink.requestId = requestId;
                resolvedLink.ok = response.status() == qodex::threadui::ipc::common::RESULT_STATUS_OK;
                resolvedLink.message = response.message();

                if (response.has_resolved_link()) {
                    const auto &payload = response.resolved_link();
                    resolvedLink.rawHref = payload.raw_href();
                    resolvedLink.normalizedHref = payload.normalized_href();
                    resolvedLink.tooltip = payload.tooltip();
                    resolvedLink.kind = linkKindToString(payload.kind());
                    resolvedLink.resolvedPath = payload.resolved_path();
                    resolvedLink.exists = payload.exists();
                    resolvedLink.isDirectory = payload.is_directory();
                    resolvedLink.hasLine = payload.has_line();
                    resolvedLink.line = payload.line();
                    resolvedLink.hasColumn = payload.has_column();
                    resolvedLink.column = payload.column();
                    resolvedLink.defaultAction = linkActionKindToString(payload.default_action());
                    resolvedLink.canOpen = payload.can_open();
                    resolvedLink.canOpenExternally = payload.can_open_externally();
                    resolvedLink.canRevealInFolder = payload.can_reveal_in_folder();
                    resolvedLink.canCopyResolvedPath = payload.can_copy_resolved_path();
                }

                queueResolvedLink(std::move(resolvedLink));
                return true;
            }
        } handler{state};

        std::string dispatchErrorMessage;
        if (!UiToQodexRpc::dispatchResponseEnvelope(envelope, handler, &dispatchErrorMessage)) {
            stopClientOnProtocolFailure(state, dispatchErrorMessage);
        }
        return;
    }

    struct QodexToUiRequestHandler final {
        IpcClientState *state;

        bool onAddItemsRequest(
            const std::uint64_t requestId,
            const qodex::threadui::ipc::qodex_to_ui::AddItemsRequest &request,
            std::string *
        ) {
            queueDisplayItems(request);
            sendAddItemsResponse(
                state,
                requestId,
                qodex::threadui::ipc::common::RESULT_STATUS_OK,
                "Items added."
            );
            return true;
        }

        bool onSetThreadStatusRequest(
            const std::uint64_t requestId,
            const qodex::threadui::ipc::qodex_to_ui::SetThreadStatusRequest &request,
            std::string *
        ) {
            qodex::threadui::native::ThreadStatusUpdate statusUpdate;
            statusUpdate.kind = threadStatusKindToString(request.kind());
            statusUpdate.text = request.text();
            statusUpdate.model = request.model();
            statusUpdate.reasoningEffort = threadReasoningEffortToString(request.reasoning_effort());
            statusUpdate.activeTurnId = request.active_turn_id();
            statusUpdate.activeFlags.reserve(static_cast<std::size_t>(request.active_flags_size()));
            for (const int activeFlagValue : request.active_flags()) {
                statusUpdate.activeFlags.push_back(
                    threadStatusActiveFlagToString(
                        static_cast<qodex::threadui::ipc::common::ThreadStatusActiveFlag>(activeFlagValue)
                    )
                );
            }
            queueThreadStatus(std::move(statusUpdate));
            sendSetThreadStatusResponse(
                state,
                requestId,
                qodex::threadui::ipc::common::RESULT_STATUS_OK,
                "Thread status updated."
            );
            return true;
        }
    } handler{state};

    std::string dispatchErrorMessage;
    if (!QodexToUiRpc::dispatchRequestEnvelope(envelope, handler, &dispatchErrorMessage)) {
        if (envelope.method() == QodexToUiRpc::AddItems::kMethodName) {
            sendAddItemsResponse(
                state,
                envelope.request_id(),
                qodex::threadui::ipc::common::RESULT_STATUS_ERROR,
                dispatchErrorMessage
            );
        } else if (envelope.method() == QodexToUiRpc::SetThreadStatus::kMethodName) {
            sendSetThreadStatusResponse(
                state,
                envelope.request_id(),
                qodex::threadui::ipc::common::RESULT_STATUS_ERROR,
                dispatchErrorMessage
            );
        }
        stopClientOnProtocolFailure(state, dispatchErrorMessage);
    }
}

void beginReadFromSocket(IpcClientState *state) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    state->socket.async_read_some(
        asio::buffer(state->readChunk),
        [state](const asio::error_code &readError, const std::size_t bytesRead) {
            if (state->stopRequested) {
                return;
            }

            if (readError) {
                scheduleReconnect(state, readError);
                return;
            }

            state->inputBuffer.append(state->readChunk.data(), bytesRead);

            while (true) {
                qodex::threadui::ipc::common::RpcEnvelope envelope;
                std::string parseErrorMessage;
                const qodex::threadui::ipc::FrameDecodeResult decodeResult =
                    qodex::threadui::ipc::tryDecodeNextEnvelope(&state->inputBuffer, &envelope, &parseErrorMessage);
                if (decodeResult == qodex::threadui::ipc::FrameDecodeResult::Incomplete) {
                    break;
                }

                if (decodeResult == qodex::threadui::ipc::FrameDecodeResult::InvalidFrame) {
                    stopClientOnProtocolFailure(state, parseErrorMessage);
                    return;
                }

                handleEnvelope(state, envelope);
                if (state->stopRequested) {
                    return;
                }
            }

            beginReadFromSocket(state);
        }
    );
}

void sendLoginRequest(IpcClientState *state) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    qodex::threadui::ipc::ui_to_qodex::LoginRequest request;
    request.set_token(state->launchConfig.token);

    const std::uint64_t requestId = state->nextRequestId++;
    state->pendingLoginRequestId = requestId;
    queueEnvelopeForWrite(
        state,
        qodex::threadui::ipc::makeRequestEnvelope<UiToQodexRpc::Login>(requestId, request)
    );
    std::cout << "Sent Thread UI Login request." << std::endl;
}

void scheduleConnect(IpcClientState *state) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    asio::error_code closeError;
    state->socket.close(closeError);
    state->resolver.async_resolve(
        state->launchConfig.host,
        std::to_string(state->launchConfig.port),
        [state](const asio::error_code &resolveError, asio::ip::tcp::resolver::results_type endpoints) {
            if (state->stopRequested) {
                return;
            }

            if (resolveError) {
                scheduleReconnect(state, resolveError);
                return;
            }

            asio::async_connect(
                state->socket,
                endpoints,
                [state](const asio::error_code &connectError, const asio::ip::tcp::endpoint &) {
                    if (state->stopRequested) {
                        return;
                    }

                    if (connectError) {
                        scheduleReconnect(state, connectError);
                        return;
                    }

                    asio::error_code noDelayError;
                    state->socket.set_option(asio::ip::tcp::no_delay(true), noDelayError);
                    if (noDelayError) {
                        scheduleReconnect(state, noDelayError);
                        return;
                    }

                    state->connected = true;
                    state->authenticated = false;
                    state->pendingLoginRequestId = 0;
                    state->pendingSendUserInputRequestIds.clear();
                    state->pendingResolveLinkRequestIds.clear();
                    state->inputBuffer.clear();
                    state->writeQueue.clear();
                    state->writeInProgress = false;
                    std::cout << "Connected Thread UI IPC socket to "
                              << endpointDescription(state->launchConfig)
                              << '.'
                              << std::endl;
                    beginReadFromSocket(state);
                    sendLoginRequest(state);
                }
            );
        }
    );
}

void startIpcClient(const qodex::threadui::native::LaunchConfig &launchConfig) {
    auto state = std::make_unique<IpcClientState>(launchConfig);
    IpcClientState *statePtr = state.get();

    asio::post(state->ioContext, [statePtr] {
        scheduleConnect(statePtr);
    });

    state->thread = std::thread([statePtr] {
        statePtr->ioContext.run();
    });

    ipcClientState = std::move(state);
}

void stopIpcClient() {
    if (!ipcClientState) {
        return;
    }

    ipcClientState->stopRequested = true;
    IpcClientState *state = ipcClientState.get();
    asio::post(state->ioContext, [state] {
        asio::error_code ignoredError;
        state->resolver.cancel();
        state->reconnectTimer.cancel();
        state->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignoredError);
        state->socket.close(ignoredError);
        state->workGuard.reset();
    });

    if (ipcClientState->thread.joinable()) {
        ipcClientState->thread.join();
    }

    ipcClientState.reset();
}

bool isPowerOfTwo(const std::int64_t value) {
    return value > 0 && (value & (value - 1)) == 0;
}

}  // namespace

void initialize(const LaunchConfig &launchConfig) {
    if (initialized) {
        return;
    }

    initialized = true;
    frameCount = 0;
    currentLaunchConfig = launchConfig;
    {
        std::lock_guard lock(fatalErrorMutex);
        pendingFatalError.clear();
    }
    {
        std::lock_guard lock(pendingErrorMutex);
        pendingError.clear();
    }
    {
        std::lock_guard lock(pendingItemsMutex);
        pendingItems.clear();
    }
    {
        std::lock_guard lock(pendingResolvedLinksMutex);
        pendingResolvedLinks.clear();
    }
    {
        std::lock_guard lock(pendingThreadStatusMutex);
        pendingThreadStatus.reset();
    }

    std::cout << "Qodex thread UI engine initialized.";
    if (hasIpcTarget(currentLaunchConfig)) {
        std::cout << " Qodex IPC target: " << currentLaunchConfig.host << ':' << currentLaunchConfig.port << '.';
        startIpcClient(currentLaunchConfig);
    } else {
        std::cout << " Qodex IPC target is not configured.";
    }
    std::cout << std::endl;
}

void setFrameCountDisplayCallback(FrameCountDisplayCallback callback) {
    frameCountDisplayCallback = std::move(callback);
}

std::vector<DisplayItem> takePendingItems() {
    std::lock_guard lock(pendingItemsMutex);
    std::vector<DisplayItem> items = std::move(pendingItems);
    pendingItems.clear();
    return items;
}

std::vector<ResolvedLink> takePendingResolvedLinks() {
    std::lock_guard lock(pendingResolvedLinksMutex);
    std::vector<ResolvedLink> links = std::move(pendingResolvedLinks);
    pendingResolvedLinks.clear();
    return links;
}

std::optional<ThreadStatusUpdate> takePendingThreadStatus() {
    std::lock_guard lock(pendingThreadStatusMutex);
    std::optional<ThreadStatusUpdate> statusUpdate = std::move(pendingThreadStatus);
    pendingThreadStatus.reset();
    return statusUpdate;
}

std::string takeFatalError() {
    std::lock_guard lock(fatalErrorMutex);
    std::string message = std::move(pendingFatalError);
    pendingFatalError.clear();
    return message;
}

std::string takePendingError() {
    std::lock_guard lock(pendingErrorMutex);
    std::string message = std::move(pendingError);
    pendingError.clear();
    return message;
}

void sendUserInput(const std::string &text) {
    if (!initialized || text.empty() || !ipcClientState) {
        if (initialized && text.empty()) {
            recordPendingError("Input must not be empty.");
        } else if (!initialized || !ipcClientState) {
            recordPendingError("Thread UI is not connected.");
        }
        return;
    }

    IpcClientState *state = ipcClientState.get();
    asio::post(state->ioContext, [state, text] {
        if (state->stopRequested) {
            return;
        }

        if (!state->connected) {
            recordPendingError("Thread UI is not connected to qodex.");
            return;
        }

        if (!state->authenticated) {
            recordPendingError("Thread UI is still connecting to qodex.");
            return;
        }

        qodex::threadui::ipc::ui_to_qodex::SendUserInputRequest request;
        request.set_text(text);

        const std::uint64_t requestId = state->nextRequestId++;
        state->pendingSendUserInputRequestIds.insert(requestId);
        queueEnvelopeForWrite(
            state,
            qodex::threadui::ipc::makeRequestEnvelope<UiToQodexRpc::SendUserInput>(requestId, request)
        );
    });
}

std::uint64_t resolveLink(const std::string &href) {
    if (!initialized || href.empty() || !ipcClientState) {
        return 0;
    }

    IpcClientState *state = ipcClientState.get();
    const std::uint64_t requestId = state->nextRequestId++;
    asio::post(state->ioContext, [state, href, requestId] {
        if (state->stopRequested) {
            qodex::threadui::native::ResolvedLink resolvedLink;
            resolvedLink.requestId = requestId;
            resolvedLink.ok = false;
            resolvedLink.message = "Thread UI is shutting down.";
            resolvedLink.rawHref = href;
            queueResolvedLink(std::move(resolvedLink));
            return;
        }

        if (!state->connected) {
            qodex::threadui::native::ResolvedLink resolvedLink;
            resolvedLink.requestId = requestId;
            resolvedLink.ok = false;
            resolvedLink.message = "Thread UI is not connected to qodex.";
            resolvedLink.rawHref = href;
            queueResolvedLink(std::move(resolvedLink));
            return;
        }

        if (!state->authenticated) {
            qodex::threadui::native::ResolvedLink resolvedLink;
            resolvedLink.requestId = requestId;
            resolvedLink.ok = false;
            resolvedLink.message = "Thread UI is still connecting to qodex.";
            resolvedLink.rawHref = href;
            queueResolvedLink(std::move(resolvedLink));
            return;
        }

        qodex::threadui::ipc::ui_to_qodex::ResolveLinkRequest request;
        request.set_href(href);

        state->pendingResolveLinkRequestIds.insert(requestId);
        queueEnvelopeForWrite(
            state,
            qodex::threadui::ipc::makeRequestEnvelope<UiToQodexRpc::ResolveLink>(requestId, request)
        );
    });

    return requestId;
}

void tick() {
    if (!initialized) {
        return;
    }

    if (isPowerOfTwo(frameCount)) {
        std::cout << "Frame " << frameCount << std::endl;
    }

    if (frameCountDisplayCallback) {
        frameCountDisplayCallback(frameCount);
    }

    ++frameCount;
}

void shutdown() {
    if (!initialized) {
        return;
    }

    initialized = false;
    stopIpcClient();
    currentLaunchConfig = {};
    frameCountDisplayCallback = nullptr;
    {
        std::lock_guard lock(fatalErrorMutex);
        pendingFatalError.clear();
    }
    {
        std::lock_guard lock(pendingErrorMutex);
        pendingError.clear();
    }
    {
        std::lock_guard lock(pendingItemsMutex);
        pendingItems.clear();
    }
    {
        std::lock_guard lock(pendingResolvedLinksMutex);
        pendingResolvedLinks.clear();
    }
    {
        std::lock_guard lock(pendingThreadStatusMutex);
        pendingThreadStatus.reset();
    }
    std::cout << "Qodex thread UI engine shutdown." << std::endl;
}

}  // namespace qodex::threadui::native
