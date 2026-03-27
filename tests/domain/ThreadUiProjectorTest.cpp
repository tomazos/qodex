#include <QtTest>

#include "domain/ThreadUiProjector.h"
#include "domain/threadmodel/CompletedCommandExecution.h"
#include "domain/threadmodel/CompletedFileChange.h"
#include "domain/threadmodel/CompletedPlan.h"
#include "domain/threadmodel/CompletedReasoning.h"
#include "domain/threadmodel/CompletedUserMessage.h"

using qodex::codex::CommandAction;
using qodex::codex::CommandActionListFiles;
using qodex::codex::CommandActionRead;
using qodex::codex::CommandActionSearch;
using qodex::codex::FileUpdateChange;
using qodex::codex::PatchApplyStatus;
using qodex::codex::PatchChangeKind;
using qodex::codex::PatchChangeKindUpdate;
using qodex::codex::Ref;
using qodex::codex::ThreadItem;
using qodex::codex::ThreadItemAgentMessage;
using qodex::codex::ThreadItemCommandExecution;
using qodex::codex::ThreadItemFileChange;
using qodex::codex::ThreadItemPlan;
using qodex::codex::ThreadItemReasoning;
using qodex::codex::ThreadItemUserMessage;
using qodex::codex::Turn;
using qodex::codex::TurnStatus;
using qodex::codex::UserInput;
using qodex::codex::UserInputText;
using qodex::domain::ThreadUiProjector;
using qodex::domain::threadmodel::CompletedPlan;
using qodex::domain::threadmodel::CompletedReasoning;
using qodex::domain::threadmodel::CompletedUserMessage;

namespace {

Ref<UserInput> makeTextInput(const QString &text) {
    Ref<UserInputText> textInput = Ref<UserInputText>::create();
    textInput->text = text;

    Ref<UserInput> input = Ref<UserInput>::create();
    input->kind = UserInput::Kind::Text;
    input->payload = textInput;
    return input;
}

ThreadItem makeCompletedUserMessageItem(const QString &itemId, const QList<QString> &parts) {
    Ref<ThreadItemUserMessage> payload = Ref<ThreadItemUserMessage>::create();
    payload->id = itemId;
    for (const QString &part : parts) {
        payload->content.append(makeTextInput(part));
    }

    ThreadItem item;
    item.kind = ThreadItem::Kind::UserMessage;
    item.payload = payload;
    return item;
}

ThreadItem makeCompletedAgentMessageItem(const QString &itemId, const QString &text) {
    Ref<ThreadItemAgentMessage> payload = Ref<ThreadItemAgentMessage>::create();
    payload->id = itemId;
    payload->text = text;

    ThreadItem item;
    item.kind = ThreadItem::Kind::AgentMessage;
    item.payload = payload;
    return item;
}

ThreadItem makeStartedAgentMessageItem(const QString &itemId, const QString &text) {
    return makeCompletedAgentMessageItem(itemId, text);
}

}  // namespace

class ThreadUiProjectorTest final : public QObject {
    Q_OBJECT

private slots:
    void projectsTurnsInOrderAndSkipsInprogressItems();
    void projectsReasoningSummaryBeforeRawContent();
    void projectsStructuredCommandExecutionAndFileChangeItems();
    void summarizesGenericCompletedItemsAsCompactJson();
};

void ThreadUiProjectorTest::projectsTurnsInOrderAndSkipsInprogressItems() {
    ThreadUiProjector projector;

    qodex::domain::threadmodel::Turn firstTurn(QStringLiteral("turn-1"));
    Turn firstTurnSnapshot;
    firstTurnSnapshot.id = QStringLiteral("turn-1");
    firstTurnSnapshot.status = TurnStatus::Completed;
    firstTurn.applyMetadata(firstTurnSnapshot);
    QVERIFY(firstTurn.applyCompletedItem(
        makeCompletedUserMessageItem(QStringLiteral("user-1"), {QStringLiteral("Hello"), QStringLiteral("World")})
    ));
    QVERIFY(firstTurn.applyStartedItem(
        makeStartedAgentMessageItem(QStringLiteral("agent-inprogress"), QStringLiteral("Not done yet"))
    ));

    qodex::domain::threadmodel::Turn secondTurn(QStringLiteral("turn-2"));
    Turn secondTurnSnapshot;
    secondTurnSnapshot.id = QStringLiteral("turn-2");
    secondTurnSnapshot.status = TurnStatus::Completed;
    secondTurn.applyMetadata(secondTurnSnapshot);
    QVERIFY(secondTurn.applyCompletedItem(
        makeCompletedAgentMessageItem(QStringLiteral("agent-1"), QStringLiteral("Goodbye"))
    ));

    const auto request = projector.projectTurns({&firstTurn, &secondTurn});

    QCOMPARE(request.items_size(), 2);
    QCOMPARE(QString::fromStdString(request.items(0).item_id()), QStringLiteral("user-1"));
    QCOMPARE(QString::fromStdString(request.items(0).user_message().text()), QStringLiteral("Hello\n\nWorld"));
    QCOMPARE(QString::fromStdString(request.items(1).item_id()), QStringLiteral("agent-1"));
    QCOMPARE(QString::fromStdString(request.items(1).agent_message().text()), QStringLiteral("Goodbye"));
}

void ThreadUiProjectorTest::projectsReasoningSummaryBeforeRawContent() {
    ThreadUiProjector projector;

    ThreadItemReasoning reasoningPayload;
    reasoningPayload.id = QStringLiteral("reasoning-1");
    reasoningPayload.summary = QList<QString>{QStringLiteral("First summary paragraph"), QStringLiteral(""), QStringLiteral("Second")};
    reasoningPayload.content = QList<QString>{QStringLiteral("Raw reasoning body")};

    CompletedReasoning reasoning(reasoningPayload);

    const auto projectedItem = projector.projectCompletedItem(reasoning);
    QVERIFY(projectedItem.has_value());
    QCOMPARE(QString::fromStdString(projectedItem->item_id()), QStringLiteral("reasoning-1"));
    QCOMPARE(
        QString::fromStdString(projectedItem->reasoning().text()),
        QStringLiteral("First summary paragraph\n\nSecond")
    );
}

void ThreadUiProjectorTest::projectsStructuredCommandExecutionAndFileChangeItems() {
    ThreadUiProjector projector;

    ThreadItemCommandExecution commandPayload;
    commandPayload.id = QStringLiteral("command-1");
    commandPayload.command = QStringLiteral("git status");
    commandPayload.cwd = QStringLiteral("/home/zos/qodex");
    commandPayload.status = qodex::codex::CommandExecutionStatus::Completed;
    commandPayload.exitCode = qodex::codex::Nullable<qint64>::fromValue(qint64{0});
    commandPayload.durationMs = qodex::codex::Nullable<qint64>::fromValue(qint64{42});
    commandPayload.processId = qodex::codex::Nullable<QString>::fromValue(QStringLiteral("123"));
    commandPayload.aggregatedOutput = qodex::codex::Nullable<QString>::fromValue(QStringLiteral("On branch main\n"));

    Ref<CommandActionRead> readAction = Ref<CommandActionRead>::create();
    readAction->path = QStringLiteral("/home/zos/qodex/README.md");
    Ref<CommandAction> action = Ref<CommandAction>::create();
    action->kind = CommandAction::Kind::Read;
    action->payload = readAction;
    commandPayload.commandActions.append(action);

    qodex::domain::threadmodel::CompletedCommandExecution commandExecution(commandPayload);

    const auto projectedCommand = projector.projectCompletedItem(commandExecution);
    QVERIFY(projectedCommand.has_value());
    QCOMPARE(QString::fromStdString(projectedCommand->item_id()), QStringLiteral("command-1"));
    QCOMPARE(QString::fromStdString(projectedCommand->command_execution().command()), QStringLiteral("git status"));
    QCOMPARE(QString::fromStdString(projectedCommand->command_execution().cwd()), QStringLiteral("/home/zos/qodex"));
    QCOMPARE(QString::fromStdString(projectedCommand->command_execution().status()), QStringLiteral("completed"));
    QCOMPARE(projectedCommand->command_execution().has_exit_code(), true);
    QCOMPARE(projectedCommand->command_execution().exit_code(), std::int64_t{0});
    QCOMPARE(projectedCommand->command_execution().has_duration_ms(), true);
    QCOMPARE(projectedCommand->command_execution().duration_ms(), std::int64_t{42});
    QCOMPARE(QString::fromStdString(projectedCommand->command_execution().process_id()), QStringLiteral("123"));
    QCOMPARE(
        QString::fromStdString(projectedCommand->command_execution().aggregated_output()),
        QStringLiteral("On branch main\n")
    );
    QCOMPARE(projectedCommand->command_execution().actions_size(), 1);
    QCOMPARE(
        projectedCommand->command_execution().actions(0).kind(),
        qodex::threadui::ipc::common::COMMAND_EXECUTION_ACTION_KIND_READ
    );
    QCOMPARE(
        QString::fromStdString(projectedCommand->command_execution().actions(0).path()),
        QStringLiteral("/home/zos/qodex/README.md")
    );
    QCOMPARE(
        QString::fromStdString(projectedCommand->command_execution().actions(0).name()),
        QString()
    );
    QCOMPARE(
        QString::fromStdString(projectedCommand->command_execution().actions(0).command()),
        QString()
    );

    ThreadItemFileChange fileChangePayload;
    fileChangePayload.id = QStringLiteral("file-1");
    fileChangePayload.status = PatchApplyStatus::Completed;

    Ref<PatchChangeKindUpdate> updateKindPayload = Ref<PatchChangeKindUpdate>::create();
    updateKindPayload->movePath = qodex::codex::Nullable<QString>::fromValue(QStringLiteral("/tmp/new-name.txt"));
    Ref<PatchChangeKind> updateKind = Ref<PatchChangeKind>::create();
    updateKind->kind = PatchChangeKind::Kind::Update;
    updateKind->payload = updateKindPayload;

    Ref<FileUpdateChange> change = Ref<FileUpdateChange>::create();
    change->path = QStringLiteral("/tmp/old-name.txt");
    change->kind = updateKind;
    change->diff = QStringLiteral("@@ -1 +1 @@\n-old\n+new");
    fileChangePayload.changes.append(change);

    qodex::domain::threadmodel::CompletedFileChange fileChange(fileChangePayload);

    const auto projectedFileChange = projector.projectCompletedItem(fileChange);
    QVERIFY(projectedFileChange.has_value());
    QCOMPARE(QString::fromStdString(projectedFileChange->item_id()), QStringLiteral("file-1"));
    QCOMPARE(QString::fromStdString(projectedFileChange->file_change().status()), QStringLiteral("completed"));
    QCOMPARE(projectedFileChange->file_change().changes_size(), 1);
    QCOMPARE(
        QString::fromStdString(projectedFileChange->file_change().changes(0).path()),
        QStringLiteral("/tmp/old-name.txt")
    );
    QCOMPARE(
        projectedFileChange->file_change().changes(0).kind(),
        qodex::threadui::ipc::common::FILE_CHANGE_KIND_UPDATE
    );
    QCOMPARE(
        QString::fromStdString(projectedFileChange->file_change().changes(0).move_path()),
        QStringLiteral("/tmp/new-name.txt")
    );
    QCOMPARE(
        QString::fromStdString(projectedFileChange->file_change().changes(0).diff()),
        QStringLiteral("@@ -1 +1 @@\n-old\n+new")
    );
}

void ThreadUiProjectorTest::summarizesGenericCompletedItemsAsCompactJson() {
    ThreadUiProjector projector;

    ThreadItemPlan planPayload;
    planPayload.id = QStringLiteral("plan-1");
    planPayload.text = QStringLiteral("Step 1");

    CompletedPlan plan(planPayload);

    const auto projectedItem = projector.projectCompletedItem(plan);
    QVERIFY(projectedItem.has_value());
    QCOMPARE(QString::fromStdString(projectedItem->item_id()), QStringLiteral("plan-1"));
    QCOMPARE(QString::fromStdString(projectedItem->plan().text()), QStringLiteral("{\"text\":\"Step 1\"}"));

    ThreadItemUserMessage emptyUserPayload;
    emptyUserPayload.id = QStringLiteral("empty-user");
    CompletedUserMessage emptyUserMessage(emptyUserPayload);
    QVERIFY(!projector.projectCompletedItem(emptyUserMessage).has_value());
}

QTEST_GUILESS_MAIN(ThreadUiProjectorTest)

#include "ThreadUiProjectorTest.moc"
