#include <QtTest>

#include "CodexProtocol.h"
#include "ui/ThreadTranscriptFormatter.h"

using qodex::codex::Ref;
using qodex::codex::Thread;
using qodex::codex::ThreadItem;
using qodex::codex::ThreadItemAgentMessage;
using qodex::codex::ThreadItemUserMessage;
using qodex::codex::Turn;
using qodex::codex::UserInput;
using qodex::codex::UserInputMention;
using qodex::codex::UserInputText;

class ThreadTranscriptFormatterTest final : public QObject {
    Q_OBJECT

private slots:
    void formatsUserAndAgentMessages();
    void fallsBackWhenNoTranscriptItemsExist();
};

void ThreadTranscriptFormatterTest::formatsUserAndAgentMessages() {
    Ref<UserInputText> userText = Ref<UserInputText>::create();
    userText->text = QStringLiteral("Hello Codex");

    Ref<UserInput> userTextInput = Ref<UserInput>::create();
    userTextInput->kind = UserInput::Kind::Text;
    userTextInput->payload = userText;

    Ref<UserInputMention> mention = Ref<UserInputMention>::create();
    mention->name = QStringLiteral("repo");
    mention->path = QStringLiteral("/tmp/repo");

    Ref<UserInput> mentionInput = Ref<UserInput>::create();
    mentionInput->kind = UserInput::Kind::Mention;
    mentionInput->payload = mention;

    Ref<ThreadItemUserMessage> userMessage = Ref<ThreadItemUserMessage>::create();
    userMessage->id = QStringLiteral("u1");
    userMessage->content = {userTextInput, mentionInput};

    Ref<ThreadItem> userItem = Ref<ThreadItem>::create();
    userItem->kind = ThreadItem::Kind::UserMessage;
    userItem->payload = userMessage;

    Ref<ThreadItemAgentMessage> agentMessage = Ref<ThreadItemAgentMessage>::create();
    agentMessage->id = QStringLiteral("a1");
    agentMessage->text = QStringLiteral("I can help with that.");

    Ref<ThreadItem> agentItem = Ref<ThreadItem>::create();
    agentItem->kind = ThreadItem::Kind::AgentMessage;
    agentItem->payload = agentMessage;

    Ref<Turn> turn = Ref<Turn>::create();
    turn->id = QStringLiteral("t1");
    turn->items = {userItem, agentItem};

    Thread thread;
    thread.id = QStringLiteral("thread-1");
    thread.turns = {turn};

    const QString transcript = qodex::ui::formatThreadTranscript(thread);
    QCOMPARE(
        transcript,
        QStringLiteral("User:\nHello Codex\n@repo\n\nAgent:\nI can help with that.")
    );
}

void ThreadTranscriptFormatterTest::fallsBackWhenNoTranscriptItemsExist() {
    Thread thread;
    thread.id = QStringLiteral("thread-2");

    QCOMPARE(
        qodex::ui::formatThreadTranscript(thread),
        QStringLiteral("No user or agent messages are available for this thread.")
    );
}

QTEST_GUILESS_MAIN(ThreadTranscriptFormatterTest)

#include "ThreadTranscriptFormatterTest.moc"
