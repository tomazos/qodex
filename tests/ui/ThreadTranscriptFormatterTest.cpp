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
using qodex::codex::UserInputImage;
using qodex::codex::UserInputLocalImage;
using qodex::codex::UserInputMention;
using qodex::codex::UserInputSkill;
using qodex::codex::UserInputText;

class ThreadTranscriptFormatterTest final : public QObject {
    Q_OBJECT

private slots:
    void formatsUserAndAgentMessagesAsHtml();
    void includesImageAndLocalImageMarkup();
    void fallsBackWhenNoTranscriptItemsExist();
};

void ThreadTranscriptFormatterTest::formatsUserAndAgentMessagesAsHtml() {
    Ref<UserInputText> userText = Ref<UserInputText>::create();
    userText->text = QStringLiteral("Hello **Codex**\n\nInline math: $x^2$");

    Ref<UserInput> userTextInput = Ref<UserInput>::create();
    userTextInput->kind = UserInput::Kind::Text;
    userTextInput->payload = userText;

    Ref<UserInputMention> mention = Ref<UserInputMention>::create();
    mention->name = QStringLiteral("repo");
    mention->path = QStringLiteral("/tmp/repo");

    Ref<UserInput> mentionInput = Ref<UserInput>::create();
    mentionInput->kind = UserInput::Kind::Mention;
    mentionInput->payload = mention;

    Ref<UserInputSkill> skill = Ref<UserInputSkill>::create();
    skill->name = QStringLiteral("openai-docs");
    skill->path = QStringLiteral("/tmp/skill");

    Ref<UserInput> skillInput = Ref<UserInput>::create();
    skillInput->kind = UserInput::Kind::Skill;
    skillInput->payload = skill;

    Ref<ThreadItemUserMessage> userMessage = Ref<ThreadItemUserMessage>::create();
    userMessage->id = QStringLiteral("u1");
    userMessage->content = {userTextInput, mentionInput, skillInput};

    Ref<ThreadItem> userItem = Ref<ThreadItem>::create();
    userItem->kind = ThreadItem::Kind::UserMessage;
    userItem->payload = userMessage;

    Ref<ThreadItemAgentMessage> agentMessage = Ref<ThreadItemAgentMessage>::create();
    agentMessage->id = QStringLiteral("a1");
    agentMessage->text = QStringLiteral("I can help with that.\n\n- one\n- two");

    Ref<ThreadItem> agentItem = Ref<ThreadItem>::create();
    agentItem->kind = ThreadItem::Kind::AgentMessage;
    agentItem->payload = agentMessage;

    Ref<Turn> turn = Ref<Turn>::create();
    turn->id = QStringLiteral("t1");
    turn->items = {userItem, agentItem};

    Thread thread;
    thread.id = QStringLiteral("thread-1");
    thread.turns = {turn};

    const QString transcript = qodex::ui::formatThreadTranscriptHtml(
        thread,
        QUrl::fromLocalFile(QStringLiteral("/tmp/"))
    );
    QVERIFY(transcript.contains(QStringLiteral("<article class=\"message user\">")));
    QVERIFY(transcript.contains(QStringLiteral("<article class=\"message agent\">")));
    QVERIFY(transcript.contains(QStringLiteral("Hello")));
    QVERIFY(transcript.contains(QStringLiteral("Codex")));
    QVERIFY(transcript.contains(QStringLiteral("Inline math: $x^2$")));
    QVERIFY(transcript.contains(QStringLiteral("href=\"file:///tmp/repo\"")));
    QVERIFY(transcript.contains(QStringLiteral("openai-docs")));
    QVERIFY(transcript.contains(QStringLiteral("one")));
    QVERIFY(transcript.contains(QStringLiteral("two")));
}

void ThreadTranscriptFormatterTest::includesImageAndLocalImageMarkup() {
    Ref<UserInputImage> image = Ref<UserInputImage>::create();
    image->url = QStringLiteral("https://example.com/image.png");

    Ref<UserInput> imageInput = Ref<UserInput>::create();
    imageInput->kind = UserInput::Kind::Image;
    imageInput->payload = image;

    Ref<UserInputLocalImage> localImage = Ref<UserInputLocalImage>::create();
    localImage->path = QStringLiteral("/tmp/local-image.png");

    Ref<UserInput> localImageInput = Ref<UserInput>::create();
    localImageInput->kind = UserInput::Kind::LocalImage;
    localImageInput->payload = localImage;

    Ref<ThreadItemUserMessage> userMessage = Ref<ThreadItemUserMessage>::create();
    userMessage->id = QStringLiteral("u1");
    userMessage->content = {imageInput, localImageInput};

    Ref<ThreadItem> userItem = Ref<ThreadItem>::create();
    userItem->kind = ThreadItem::Kind::UserMessage;
    userItem->payload = userMessage;

    Ref<Turn> turn = Ref<Turn>::create();
    turn->id = QStringLiteral("t1");
    turn->items = {userItem};

    Thread thread;
    thread.id = QStringLiteral("thread-images");
    thread.turns = {turn};

    const QString transcript = qodex::ui::formatThreadTranscriptHtml(
        thread,
        QUrl::fromLocalFile(QStringLiteral("/tmp/"))
    );
    QVERIFY(transcript.contains(QStringLiteral("https://example.com/image.png")));
    QVERIFY(transcript.contains(QStringLiteral("file:///tmp/local-image.png")));
    QVERIFY(transcript.contains(QStringLiteral("<figure class=\"input-image\">")));
}

void ThreadTranscriptFormatterTest::fallsBackWhenNoTranscriptItemsExist() {
    Thread thread;
    thread.id = QStringLiteral("thread-2");

    const QString transcript = qodex::ui::formatThreadTranscriptHtml(
        thread,
        QUrl::fromLocalFile(QStringLiteral("/tmp/"))
    );
    QVERIFY(transcript.contains(QStringLiteral("No user or agent messages are available for this thread.")));
    QVERIFY(transcript.contains(QStringLiteral("empty-state")));
}

QTEST_GUILESS_MAIN(ThreadTranscriptFormatterTest)

#include "ThreadTranscriptFormatterTest.moc"
