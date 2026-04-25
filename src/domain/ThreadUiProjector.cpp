#include "domain/ThreadUiProjector.h"

#include <QJsonDocument>

#include "domain/threadmodel/AbstractItem.h"
#include "domain/threadmodel/CompletedAgentMessage.h"
#include "domain/threadmodel/CompletedCommandExecution.h"
#include "domain/threadmodel/CompletedFileChange.h"
#include "domain/threadmodel/CompletedImageGeneration.h"
#include "domain/threadmodel/CompletedImageView.h"
#include "domain/threadmodel/CompletedReasoning.h"
#include "domain/threadmodel/CompletedUserMessage.h"

namespace qodex::domain {

namespace {

QString flattenReasoningTextForThreadUi(const qodex::codex::ThreadItemReasoning &reasoning) {
    const auto joinSections = [](const std::optional<QList<QString>> &sections) {
        QStringList nonEmptySections;
        if (sections.has_value()) {
            for (const QString &section : *sections) {
                const QString trimmedSection = section.trimmed();
                if (!trimmedSection.isEmpty()) {
                    nonEmptySections.append(trimmedSection);
                }
            }
        }
        return nonEmptySections.join(QStringLiteral("\n\n"));
    };

    const QString summaryText = joinSections(reasoning.summary);
    if (!summaryText.isEmpty()) {
        return summaryText;
    }

    return joinSections(reasoning.content);
}

std::string patchApplyStatusToString(const qodex::codex::PatchApplyStatus status) {
    switch (status) {
    case qodex::codex::PatchApplyStatus::InProgress:
        return "in_progress";
    case qodex::codex::PatchApplyStatus::Completed:
        return "completed";
    case qodex::codex::PatchApplyStatus::Failed:
        return "failed";
    case qodex::codex::PatchApplyStatus::Declined:
        return "declined";
    }

    return "unknown";
}

qodex::threadui::ipc::common::FileChangeKind toThreadUiFileChangeKind(
    const qodex::codex::Ref<qodex::codex::PatchChangeKind> &kind
) {
    if (!kind) {
        return qodex::threadui::ipc::common::FILE_CHANGE_KIND_UNSPECIFIED;
    }

    switch (kind->kind) {
    case qodex::codex::PatchChangeKind::Kind::Add:
        return qodex::threadui::ipc::common::FILE_CHANGE_KIND_ADD;
    case qodex::codex::PatchChangeKind::Kind::Delete:
        return qodex::threadui::ipc::common::FILE_CHANGE_KIND_DELETE;
    case qodex::codex::PatchChangeKind::Kind::Update:
        return qodex::threadui::ipc::common::FILE_CHANGE_KIND_UPDATE;
    }

    return qodex::threadui::ipc::common::FILE_CHANGE_KIND_UNSPECIFIED;
}

std::string commandExecutionStatusToString(const qodex::codex::CommandExecutionStatus status) {
    switch (status) {
    case qodex::codex::CommandExecutionStatus::InProgress:
        return "in_progress";
    case qodex::codex::CommandExecutionStatus::Completed:
        return "completed";
    case qodex::codex::CommandExecutionStatus::Failed:
        return "failed";
    case qodex::codex::CommandExecutionStatus::Declined:
        return "declined";
    }

    return "unknown";
}

qodex::threadui::ipc::common::CommandExecutionActionKind toThreadUiCommandActionKind(
    qodex::codex::CommandAction::Kind kind
) {
    switch (kind) {
    case qodex::codex::CommandAction::Kind::Read:
        return qodex::threadui::ipc::common::COMMAND_EXECUTION_ACTION_KIND_READ;
    case qodex::codex::CommandAction::Kind::ListFiles:
        return qodex::threadui::ipc::common::COMMAND_EXECUTION_ACTION_KIND_LIST_FILES;
    case qodex::codex::CommandAction::Kind::Search:
        return qodex::threadui::ipc::common::COMMAND_EXECUTION_ACTION_KIND_SEARCH;
    case qodex::codex::CommandAction::Kind::Unknown:
        return qodex::threadui::ipc::common::COMMAND_EXECUTION_ACTION_KIND_UNKNOWN;
    }

    return qodex::threadui::ipc::common::COMMAND_EXECUTION_ACTION_KIND_UNSPECIFIED;
}

void appendCommandAction(
    qodex::threadui::ipc::common::CommandExecution *displayItem,
    const qodex::codex::Ref<qodex::codex::CommandAction> &action
) {
    if (displayItem == nullptr || !action) {
        return;
    }

    qodex::threadui::ipc::common::CommandExecutionAction *displayAction = displayItem->add_actions();
    displayAction->set_kind(toThreadUiCommandActionKind(action->kind));

    switch (action->kind) {
    case qodex::codex::CommandAction::Kind::Read: {
        const qodex::codex::Ref<qodex::codex::CommandActionRead> readAction =
            std::get<qodex::codex::Ref<qodex::codex::CommandActionRead>>(action->payload);
        if (!readAction) {
            return;
        }
        displayAction->set_command(readAction->command.toStdString());
        displayAction->set_name(readAction->name.toStdString());
        displayAction->set_path(readAction->path.toStdString());
        return;
    }
    case qodex::codex::CommandAction::Kind::ListFiles: {
        const qodex::codex::Ref<qodex::codex::CommandActionListFiles> listFilesAction =
            std::get<qodex::codex::Ref<qodex::codex::CommandActionListFiles>>(action->payload);
        if (!listFilesAction) {
            return;
        }
        displayAction->set_command(listFilesAction->command.toStdString());
        if (listFilesAction->path.hasValue()) {
            displayAction->set_path(listFilesAction->path.value().toStdString());
        }
        return;
    }
    case qodex::codex::CommandAction::Kind::Search: {
        const qodex::codex::Ref<qodex::codex::CommandActionSearch> searchAction =
            std::get<qodex::codex::Ref<qodex::codex::CommandActionSearch>>(action->payload);
        if (!searchAction) {
            return;
        }
        displayAction->set_command(searchAction->command.toStdString());
        if (searchAction->query.hasValue()) {
            displayAction->set_query(searchAction->query.value().toStdString());
        }
        if (searchAction->path.hasValue()) {
            displayAction->set_path(searchAction->path.value().toStdString());
        }
        return;
    }
    case qodex::codex::CommandAction::Kind::Unknown:
        if (const auto *unknownAction =
                std::get_if<qodex::codex::Ref<qodex::codex::CommandActionUnknown>>(&action->payload);
            unknownAction != nullptr && *unknownAction) {
            displayAction->set_command((*unknownAction)->command.toStdString());
        }
        return;
    }
}

bool appendCommandExecutionDisplayItem(
    qodex::threadui::ipc::common::CommandExecution *displayItem,
    const qodex::domain::threadmodel::CompletedCommandExecution &commandExecutionItem
) {
    if (displayItem == nullptr) {
        return false;
    }

    const qodex::codex::ThreadItemCommandExecution &payload = commandExecutionItem.data();
    displayItem->set_command(payload.command.toStdString());
    displayItem->set_cwd(payload.cwd.toStdString());
    displayItem->set_status(commandExecutionStatusToString(payload.status));

    if (payload.exitCode.hasValue()) {
        displayItem->set_has_exit_code(true);
        displayItem->set_exit_code(payload.exitCode.value());
    }

    if (payload.durationMs.hasValue()) {
        displayItem->set_has_duration_ms(true);
        displayItem->set_duration_ms(payload.durationMs.value());
    }

    if (payload.processId.hasValue() && !payload.processId.value().isEmpty()) {
        displayItem->set_process_id(payload.processId.value().toStdString());
    }

    if (payload.aggregatedOutput.hasValue()) {
        displayItem->set_aggregated_output(payload.aggregatedOutput.value().toStdString());
    }

    for (const qodex::codex::Ref<qodex::codex::CommandAction> &action : payload.commandActions) {
        appendCommandAction(displayItem, action);
    }

    return !payload.command.isEmpty() || payload.aggregatedOutput.hasValue();
}

bool appendFileChangeDisplayItem(
    qodex::threadui::ipc::common::FileChange *displayItem,
    const qodex::domain::threadmodel::CompletedFileChange &fileChangeItem
) {
    if (displayItem == nullptr) {
        return false;
    }

    const qodex::codex::ThreadItemFileChange &payload = fileChangeItem.data();
    displayItem->set_status(patchApplyStatusToString(payload.status));

    for (const qodex::codex::Ref<qodex::codex::FileUpdateChange> &changeRef : payload.changes) {
        if (!changeRef) {
            continue;
        }

        qodex::threadui::ipc::common::FileChangeChange *displayChange = displayItem->add_changes();
        displayChange->set_path(changeRef->path.toStdString());
        displayChange->set_kind(toThreadUiFileChangeKind(changeRef->kind));
        displayChange->set_diff(changeRef->diff.toStdString());

        if (changeRef->kind && changeRef->kind->kind == qodex::codex::PatchChangeKind::Kind::Update &&
            std::holds_alternative<qodex::codex::Ref<qodex::codex::PatchChangeKindUpdate>>(changeRef->kind->payload)) {
            const qodex::codex::Ref<qodex::codex::PatchChangeKindUpdate> updateKind =
                std::get<qodex::codex::Ref<qodex::codex::PatchChangeKindUpdate>>(changeRef->kind->payload);
            if (updateKind && updateKind->movePath.hasValue() && !updateKind->movePath.value().isEmpty()) {
                displayChange->set_move_path(updateKind->movePath.value().toStdString());
            }
        }
    }

    return displayItem->changes_size() > 0;
}

bool appendImageGenerationDisplayItem(
    qodex::threadui::ipc::common::ImageGeneration *displayItem,
    const qodex::domain::threadmodel::CompletedImageGeneration &imageGenerationItem
) {
    if (displayItem == nullptr) {
        return false;
    }

    const qodex::codex::ThreadItemImageGeneration &payload = imageGenerationItem.data();
    displayItem->set_result(payload.result.toStdString());
    displayItem->set_status(payload.status.toStdString());

    if (payload.revisedPrompt.hasValue()) {
        displayItem->set_revised_prompt(payload.revisedPrompt.value().toStdString());
    }

    if (payload.savedPath.hasValue()) {
        displayItem->set_saved_path(payload.savedPath.value().toStdString());
    }

    return !payload.result.isEmpty() || payload.savedPath.hasValue() || payload.revisedPrompt.hasValue();
}

bool appendImageViewDisplayItem(
    qodex::threadui::ipc::common::ImageView *displayItem,
    const qodex::domain::threadmodel::CompletedImageView &imageViewItem
) {
    if (displayItem == nullptr) {
        return false;
    }

    const qodex::codex::ThreadItemImageView &payload = imageViewItem.data();
    const QString path = payload.path.trimmed();
    if (path.isEmpty()) {
        return false;
    }

    displayItem->set_path(path.toStdString());
    return true;
}

}  // namespace

qodex::threadui::ipc::qodex_to_ui::AddItemsRequest ThreadUiProjector::projectTurns(
    const QList<const qodex::domain::threadmodel::Turn *> &turns
) const {
    qodex::threadui::ipc::qodex_to_ui::AddItemsRequest request;

    for (const qodex::domain::threadmodel::Turn *turn : turns) {
        if (turn == nullptr) {
            continue;
        }

        for (const qodex::domain::threadmodel::AbstractItem *item : turn->orderedItems()) {
            if (item == nullptr) {
                continue;
            }

            const std::optional<qodex::threadui::ipc::common::Item> projected = projectCompletedItem(*item);
            if (!projected.has_value()) {
                continue;
            }

            *request.add_items() = *projected;
        }
    }

    return request;
}

std::optional<qodex::threadui::ipc::common::Item> ThreadUiProjector::projectCompletedItem(
    const qodex::domain::threadmodel::AbstractItem &item
) const {
    qodex::threadui::ipc::common::Item displayItem;
    if (!appendCompletedItem(&displayItem, item)) {
        return std::nullopt;
    }

    displayItem.set_item_id(item.id().toStdString());

    return displayItem;
}

bool ThreadUiProjector::appendCompletedItem(
    qodex::threadui::ipc::common::Item *displayItem,
    const qodex::domain::threadmodel::AbstractItem &item
) const {
    using qodex::codex::ThreadItem;
    using qodex::domain::threadmodel::CompletedAgentMessage;
    using qodex::domain::threadmodel::CompletedCommandExecution;
    using qodex::domain::threadmodel::CompletedFileChange;
    using qodex::domain::threadmodel::CompletedImageGeneration;
    using qodex::domain::threadmodel::CompletedImageView;
    using qodex::domain::threadmodel::CompletedReasoning;
    using qodex::domain::threadmodel::CompletedUserMessage;

    if (displayItem == nullptr || !item.isCompleted()) {
        return false;
    }

    switch (item.kind()) {
    case ThreadItem::Kind::UserMessage: {
        const auto &userMessage = static_cast<const CompletedUserMessage &>(item);
        const QString text = flattenUserMessageContent(userMessage.data().content).trimmed();
        if (text.isEmpty()) {
            return false;
        }
        displayItem->mutable_user_message()->set_text(text.toStdString());
        return true;
    }
    case ThreadItem::Kind::AgentMessage: {
        const auto &agentMessage = static_cast<const CompletedAgentMessage &>(item);
        const QString text = agentMessage.data().text.trimmed();
        if (text.isEmpty()) {
            return false;
        }
        displayItem->mutable_agent_message()->set_text(text.toStdString());
        return true;
    }
    case ThreadItem::Kind::Plan:
        displayItem->mutable_plan()->set_text(summarizeNonMessageItem(item).toStdString());
        return true;
    case ThreadItem::Kind::Reasoning: {
        const auto &reasoning = static_cast<const CompletedReasoning &>(item);
        const QString text = flattenReasoningTextForThreadUi(reasoning.data()).trimmed();
        if (text.isEmpty()) {
            return false;
        }
        displayItem->mutable_reasoning()->set_text(text.toStdString());
        return true;
    }
    case ThreadItem::Kind::CommandExecution:
        return appendCommandExecutionDisplayItem(
            displayItem->mutable_command_execution(),
            static_cast<const CompletedCommandExecution &>(item)
        );
    case ThreadItem::Kind::FileChange:
        return appendFileChangeDisplayItem(
            displayItem->mutable_file_change(),
            static_cast<const CompletedFileChange &>(item)
        );
    case ThreadItem::Kind::McpToolCall:
        displayItem->mutable_mcp_tool_call()->set_text(summarizeNonMessageItem(item).toStdString());
        return true;
    case ThreadItem::Kind::DynamicToolCall:
        displayItem->mutable_dynamic_tool_call()->set_text(summarizeNonMessageItem(item).toStdString());
        return true;
    case ThreadItem::Kind::CollabAgentToolCall:
        displayItem->mutable_collab_agent_tool_call()->set_text(summarizeNonMessageItem(item).toStdString());
        return true;
    case ThreadItem::Kind::WebSearch:
        displayItem->mutable_web_search()->set_text(summarizeNonMessageItem(item).toStdString());
        return true;
    case ThreadItem::Kind::ImageView:
        return appendImageViewDisplayItem(
            displayItem->mutable_image_view(),
            static_cast<const CompletedImageView &>(item)
        );
    case ThreadItem::Kind::ImageGeneration:
        return appendImageGenerationDisplayItem(
            displayItem->mutable_image_generation(),
            static_cast<const CompletedImageGeneration &>(item)
        );
    case ThreadItem::Kind::EnteredReviewMode:
        displayItem->mutable_entered_review_mode()->set_text(summarizeNonMessageItem(item).toStdString());
        return true;
    case ThreadItem::Kind::ExitedReviewMode:
        displayItem->mutable_exited_review_mode()->set_text(summarizeNonMessageItem(item).toStdString());
        return true;
    case ThreadItem::Kind::ContextCompaction:
        displayItem->mutable_context_compaction()->set_text(summarizeNonMessageItem(item).toStdString());
        return true;
    }

    return false;
}

QString ThreadUiProjector::summarizeNonMessageItem(const qodex::domain::threadmodel::AbstractItem &item) const {
    QJsonObject properties = item.properties();
    properties.remove(QStringLiteral("id"));
    const QByteArray compactJson = QJsonDocument(properties).toJson(QJsonDocument::Compact);
    return compactJson.isEmpty() || compactJson == "{}" ? item.id() : QString::fromUtf8(compactJson);
}

QString ThreadUiProjector::flattenUserMessageContent(
    const QList<qodex::codex::Ref<qodex::codex::UserInput>> &content
) const {
    QStringList parts;
    parts.reserve(content.size());

    for (const qodex::codex::Ref<qodex::codex::UserInput> &input : content) {
        if (!input) {
            continue;
        }

        switch (input->kind) {
        case qodex::codex::UserInput::Kind::Text: {
            const qodex::codex::Ref<qodex::codex::UserInputText> textInput =
                std::get<qodex::codex::Ref<qodex::codex::UserInputText>>(input->payload);
            if (textInput && !textInput->text.isEmpty()) {
                parts.append(textInput->text);
            }
            break;
        }
        default:
            break;
        }
    }

    return parts.join(QStringLiteral("\n\n"));
}

}  // namespace qodex::domain
