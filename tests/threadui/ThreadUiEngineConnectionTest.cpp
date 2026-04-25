#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>

#include <optional>

#include "common.pb.h"
#include "qodex_to_ui.pb.h"
#include "threadui/ThreadUiIpcServer.h"
#include "threadui_native/ThreadUiEngine.h"

using qodex::threadui::ThreadUiIpcServer;
using qodex::threadui::native::LaunchConfig;

class ThreadUiEngineConnectionTest final : public QObject {
    Q_OBJECT

private slots:
    void establishesTcpConnectionToQodexListener();
    void receivesThreadStatusUpdatesOverIpc();
    void sendsUserInputRequestsAndReportsErrors();
    void resolvesLinksOverIpc();
    void rejectsUserInputWhenNoAuthenticatedConnectionExists();
};

void ThreadUiEngineConnectionTest::establishesTcpConnectionToQodexListener() {
    ThreadUiIpcServer server;

    QString errorMessage;
    QVERIFY2(server.listen(&errorMessage), qPrintable(errorMessage));
    QCOMPARE(server.unauthenticatedConnectionCount(), 0);
    QCOMPARE(server.authenticatedConnectionCount(), 0);

    const qodex::threadui::ThreadUiLaunchConfig launchConfig = server.allocateLaunchConfig();

    qodex::threadui::native::initialize(LaunchConfig{
        .host = launchConfig.host.toStdString(),
        .port = launchConfig.port,
        .token = launchConfig.token.toStdString(),
    });

    QTRY_COMPARE(server.unauthenticatedConnectionCount(), 0);
    QTRY_COMPARE(server.authenticatedConnectionCount(), 1);

    qodex::threadui::ipc::qodex_to_ui::AddItemsRequest addItemsRequest;
    qodex::threadui::ipc::common::Item *userItem = addItemsRequest.add_items();
    userItem->set_item_id("user-1");
    userItem->mutable_user_message()->set_text("Hello from user");
    qodex::threadui::ipc::common::Item *agentItem = addItemsRequest.add_items();
    agentItem->set_item_id("agent-1");
    agentItem->mutable_agent_message()->set_text("Hello from agent");
    qodex::threadui::ipc::common::Item *commandItem = addItemsRequest.add_items();
    commandItem->set_item_id("command-1");
    qodex::threadui::ipc::common::CommandExecution *commandExecution =
        commandItem->mutable_command_execution();
    commandExecution->set_command("/bin/bash -lc \"date\"");
    commandExecution->set_cwd("/home/zos");
    commandExecution->set_status("completed");
    commandExecution->set_has_exit_code(true);
    commandExecution->set_exit_code(0);
    commandExecution->set_has_duration_ms(true);
    commandExecution->set_duration_ms(215);
    commandExecution->set_process_id("12345");
    commandExecution->set_aggregated_output("Wed Mar 25 19:35:16 AEST 2026\n");
    qodex::threadui::ipc::common::CommandExecutionAction *commandAction = commandExecution->add_actions();
    commandAction->set_kind(qodex::threadui::ipc::common::COMMAND_EXECUTION_ACTION_KIND_READ);
    commandAction->set_path("/home/zos/file.txt");
    commandAction->set_name("file.txt");
    commandAction->set_command("cat /home/zos/file.txt");
    qodex::threadui::ipc::common::Item *fileChangeItem = addItemsRequest.add_items();
    fileChangeItem->set_item_id("file-1");
    qodex::threadui::ipc::common::FileChange *fileChange = fileChangeItem->mutable_file_change();
    fileChange->set_status("completed");
    qodex::threadui::ipc::common::FileChangeChange *fileChangeUpdate = fileChange->add_changes();
    fileChangeUpdate->set_path("/tmp/styles.css");
    fileChangeUpdate->set_kind(qodex::threadui::ipc::common::FILE_CHANGE_KIND_UPDATE);
    fileChangeUpdate->set_diff("@@ -1 +1 @@\n-color: Highlight;\n+color: LinkText;");
    qodex::threadui::ipc::common::Item *imageViewItem = addItemsRequest.add_items();
    imageViewItem->set_item_id("image-view-1");
    qodex::threadui::ipc::common::ImageView *imageView = imageViewItem->mutable_image_view();
    imageView->set_path("/tmp/iota-camera-direct-w.png");
    qodex::threadui::ipc::common::Item *imageGenerationItem = addItemsRequest.add_items();
    imageGenerationItem->set_item_id("image-1");
    qodex::threadui::ipc::common::ImageGeneration *imageGeneration =
        imageGenerationItem->mutable_image_generation();
    imageGeneration->set_result("ZmFrZS1iYXNlNjQ=");
    imageGeneration->set_revised_prompt("Make it cinematic");
    imageGeneration->set_saved_path("/home/zos/.codex/generated_images/thread/image-1.png");
    imageGeneration->set_status("completed");

    QVERIFY2(server.sendAddItems(launchConfig.token, addItemsRequest, &errorMessage), qPrintable(errorMessage));

    std::vector<qodex::threadui::native::DisplayItem> items;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000 && items.empty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        items = qodex::threadui::native::takePendingItems();
        if (items.empty()) {
            QTest::qWait(20);
        }
    }

    QCOMPARE(items.size(), std::size_t(6));
    QCOMPARE(items[0].id, std::string("user-1"));
    QCOMPARE(items[0].kind, std::string("user"));
    QCOMPARE(items[0].text, std::string("Hello from user"));
    QCOMPARE(items[1].id, std::string("agent-1"));
    QCOMPARE(items[1].kind, std::string("agent"));
    QCOMPARE(items[1].text, std::string("Hello from agent"));
    QCOMPARE(items[2].id, std::string("command-1"));
    QCOMPARE(items[2].kind, std::string("command_execution"));
    QCOMPARE(items[2].commandExecution.command, std::string("/bin/bash -lc \"date\""));
    QCOMPARE(items[2].commandExecution.cwd, std::string("/home/zos"));
    QCOMPARE(items[2].commandExecution.status, std::string("completed"));
    QCOMPARE(items[2].commandExecution.hasExitCode, true);
    QCOMPARE(items[2].commandExecution.exitCode, std::int64_t(0));
    QCOMPARE(items[2].commandExecution.hasDurationMs, true);
    QCOMPARE(items[2].commandExecution.durationMs, std::int64_t(215));
    QCOMPARE(items[2].commandExecution.processId, std::string("12345"));
    QCOMPARE(items[2].commandExecution.aggregatedOutput, std::string("Wed Mar 25 19:35:16 AEST 2026\n"));
    QCOMPARE(items[2].commandExecution.actions.size(), std::size_t(1));
    QCOMPARE(items[2].commandExecution.actions[0].kind, std::string("read"));
    QCOMPARE(items[2].commandExecution.actions[0].path, std::string("/home/zos/file.txt"));
    QCOMPARE(items[2].commandExecution.actions[0].name, std::string("file.txt"));
    QCOMPARE(items[2].commandExecution.actions[0].command, std::string("cat /home/zos/file.txt"));
    QCOMPARE(items[3].id, std::string("file-1"));
    QCOMPARE(items[3].kind, std::string("file_change"));
    QCOMPARE(items[3].fileChange.status, std::string("completed"));
    QCOMPARE(items[3].fileChange.changes.size(), std::size_t(1));
    QCOMPARE(items[3].fileChange.changes[0].path, std::string("/tmp/styles.css"));
    QCOMPARE(items[3].fileChange.changes[0].kind, std::string("update"));
    QCOMPARE(items[3].fileChange.changes[0].diff, std::string("@@ -1 +1 @@\n-color: Highlight;\n+color: LinkText;"));
    QCOMPARE(items[4].id, std::string("image-view-1"));
    QCOMPARE(items[4].kind, std::string("image_view"));
    QCOMPARE(items[4].imageView.path, std::string("/tmp/iota-camera-direct-w.png"));
    QCOMPARE(items[5].id, std::string("image-1"));
    QCOMPARE(items[5].kind, std::string("image_generation"));
    QCOMPARE(items[5].imageGeneration.result, std::string("ZmFrZS1iYXNlNjQ="));
    QCOMPARE(items[5].imageGeneration.revisedPrompt, std::string("Make it cinematic"));
    QCOMPARE(
        items[5].imageGeneration.savedPath,
        std::string("/home/zos/.codex/generated_images/thread/image-1.png")
    );
    QCOMPARE(items[5].imageGeneration.status, std::string("completed"));

    qodex::threadui::native::shutdown();

    QTRY_COMPARE(server.unauthenticatedConnectionCount(), 0);
    QTRY_COMPARE(server.authenticatedConnectionCount(), 0);
}

void ThreadUiEngineConnectionTest::receivesThreadStatusUpdatesOverIpc() {
    ThreadUiIpcServer server;

    QString errorMessage;
    QVERIFY2(server.listen(&errorMessage), qPrintable(errorMessage));

    const qodex::threadui::ThreadUiLaunchConfig launchConfig = server.allocateLaunchConfig();

    qodex::threadui::native::initialize(LaunchConfig{
        .host = launchConfig.host.toStdString(),
        .port = launchConfig.port,
        .token = launchConfig.token.toStdString(),
    });

    QTRY_COMPARE(server.authenticatedConnectionCount(), 1);

    qodex::threadui::ipc::qodex_to_ui::SetThreadStatusRequest request;
    request.set_kind(qodex::threadui::ipc::common::THREAD_STATUS_KIND_ACTIVE);
    request.set_text("Active - Waiting on approval");
    request.add_active_flags(qodex::threadui::ipc::common::THREAD_STATUS_ACTIVE_FLAG_WAITING_ON_APPROVAL);
    request.set_active_turn_id("turn-123");
    request.set_model("gpt-5.5");
    request.set_reasoning_effort(qodex::threadui::ipc::common::THREAD_REASONING_EFFORT_XHIGH);

    QVERIFY2(server.sendSetThreadStatus(launchConfig.token, request, &errorMessage), qPrintable(errorMessage));

    std::optional<qodex::threadui::native::ThreadStatusUpdate> statusUpdate;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000 && !statusUpdate.has_value()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        statusUpdate = qodex::threadui::native::takePendingThreadStatus();
        if (!statusUpdate.has_value()) {
            QTest::qWait(20);
        }
    }

    QVERIFY(statusUpdate.has_value());
    QCOMPARE(statusUpdate->kind, std::string("active"));
    QCOMPARE(statusUpdate->text, std::string("Active - Waiting on approval"));
    QCOMPARE(statusUpdate->model, std::string("gpt-5.5"));
    QCOMPARE(statusUpdate->reasoningEffort, std::string("xhigh"));
    QCOMPARE(statusUpdate->activeTurnId, std::string("turn-123"));
    QCOMPARE(statusUpdate->activeFlags.size(), std::size_t(1));
    QCOMPARE(statusUpdate->activeFlags[0], std::string("waiting_on_approval"));

    qodex::threadui::native::shutdown();

    QTRY_COMPARE(server.authenticatedConnectionCount(), 0);
}

void ThreadUiEngineConnectionTest::sendsUserInputRequestsAndReportsErrors() {
    ThreadUiIpcServer server;

    QString errorMessage;
    QVERIFY2(server.listen(&errorMessage), qPrintable(errorMessage));

    const qodex::threadui::ThreadUiLaunchConfig launchConfig = server.allocateLaunchConfig();

    qodex::threadui::native::initialize(LaunchConfig{
        .host = launchConfig.host.toStdString(),
        .port = launchConfig.port,
        .token = launchConfig.token.toStdString(),
    });

    QTRY_COMPARE(server.authenticatedConnectionCount(), 1);

    int requestCount = 0;
    QString lastToken;
    std::uint64_t lastRequestId = 0;
    QString lastText;
    QObject::connect(
        &server,
        &ThreadUiIpcServer::sendUserInputRequested,
        this,
        [&requestCount, &lastToken, &lastRequestId, &lastText](
            const QString &token,
            const std::uint64_t requestId,
            const QString &text
        ) {
            ++requestCount;
            lastToken = token;
            lastRequestId = requestId;
            lastText = text;
        }
    );

    qodex::threadui::native::sendUserInput("Hello from renderer");

    QTRY_COMPARE(requestCount, 1);
    QCOMPARE(lastToken, launchConfig.token);
    QCOMPARE(lastText, QStringLiteral("Hello from renderer"));
    QVERIFY(lastRequestId != 0);

    QVERIFY2(
        server.sendUserInputResponse(
            launchConfig.token,
            lastRequestId,
            qodex::threadui::ipc::common::RESULT_STATUS_OK,
            QStringLiteral("Accepted."),
            &errorMessage
        ),
        qPrintable(errorMessage)
    );

    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCOMPARE(qodex::threadui::native::takePendingError(), std::string{});

    const std::uint64_t firstRequestId = lastRequestId;
    qodex::threadui::native::sendUserInput("This should fail");

    QTRY_COMPARE(requestCount, 2);
    QCOMPARE(lastToken, launchConfig.token);
    QCOMPARE(lastText, QStringLiteral("This should fail"));
    QVERIFY(lastRequestId > firstRequestId);

    QVERIFY2(
        server.sendUserInputResponse(
            launchConfig.token,
            lastRequestId,
            qodex::threadui::ipc::common::RESULT_STATUS_ERROR,
            QStringLiteral("Rejected."),
            &errorMessage
        ),
        qPrintable(errorMessage)
    );

    std::string pendingError;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000 && pendingError.empty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        pendingError = qodex::threadui::native::takePendingError();
        if (pendingError.empty()) {
            QTest::qWait(20);
        }
    }

    QCOMPARE(pendingError, std::string("SendUserInput failed: Rejected."));

    qodex::threadui::native::shutdown();

    QTRY_COMPARE(server.authenticatedConnectionCount(), 0);
}

void ThreadUiEngineConnectionTest::resolvesLinksOverIpc() {
    ThreadUiIpcServer server;

    QString errorMessage;
    QVERIFY2(server.listen(&errorMessage), qPrintable(errorMessage));

    const qodex::threadui::ThreadUiLaunchConfig launchConfig = server.allocateLaunchConfig();

    qodex::threadui::native::initialize(LaunchConfig{
        .host = launchConfig.host.toStdString(),
        .port = launchConfig.port,
        .token = launchConfig.token.toStdString(),
    });

    QTRY_COMPARE(server.authenticatedConnectionCount(), 1);

    int requestCount = 0;
    QString lastToken;
    std::uint64_t lastRequestId = 0;
    QString lastHref;
    QObject::connect(
        &server,
        &ThreadUiIpcServer::resolveLinkRequested,
        this,
        [&requestCount, &lastToken, &lastRequestId, &lastHref](
            const QString &token,
            const std::uint64_t requestId,
            const QString &href
        ) {
            ++requestCount;
            lastToken = token;
            lastRequestId = requestId;
            lastHref = href;
        }
    );

    const std::uint64_t requestId = qodex::threadui::native::resolveLink("https://github.com/tomazos/qodex/issues/6");
    QVERIFY(requestId != 0);

    QTRY_COMPARE(requestCount, 1);
    QCOMPARE(lastToken, launchConfig.token);
    QCOMPARE(lastRequestId, requestId);
    QCOMPARE(lastHref, QStringLiteral("https://github.com/tomazos/qodex/issues/6"));

    qodex::threadui::ipc::common::ResolvedLink resolvedLink;
    resolvedLink.set_raw_href("https://github.com/tomazos/qodex/issues/6");
    resolvedLink.set_normalized_href("https://github.com/tomazos/qodex/issues/6");
    resolvedLink.set_tooltip("https://github.com/tomazos/qodex/issues/6\nDefault: Open externally");
    resolvedLink.set_kind(qodex::threadui::ipc::common::LINK_KIND_WEB);
    resolvedLink.set_default_action(qodex::threadui::ipc::common::LINK_ACTION_KIND_OPEN_EXTERNALLY);
    resolvedLink.set_can_open_externally(true);

    QVERIFY2(
        server.sendResolveLinkResponse(
            launchConfig.token,
            requestId,
            qodex::threadui::ipc::common::RESULT_STATUS_OK,
            QStringLiteral("Resolved."),
            resolvedLink,
            &errorMessage
        ),
        qPrintable(errorMessage)
    );

    std::vector<qodex::threadui::native::ResolvedLink> pendingResolvedLinks;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000 && pendingResolvedLinks.empty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        pendingResolvedLinks = qodex::threadui::native::takePendingResolvedLinks();
        if (pendingResolvedLinks.empty()) {
            QTest::qWait(20);
        }
    }

    QCOMPARE(pendingResolvedLinks.size(), std::size_t(1));
    QCOMPARE(pendingResolvedLinks[0].requestId, requestId);
    QCOMPARE(pendingResolvedLinks[0].ok, true);
    QCOMPARE(pendingResolvedLinks[0].message, std::string("Resolved."));
    QCOMPARE(pendingResolvedLinks[0].rawHref, std::string("https://github.com/tomazos/qodex/issues/6"));
    QCOMPARE(pendingResolvedLinks[0].normalizedHref, std::string("https://github.com/tomazos/qodex/issues/6"));
    QCOMPARE(pendingResolvedLinks[0].kind, std::string("web"));
    QCOMPARE(pendingResolvedLinks[0].defaultAction, std::string("open_externally"));
    QCOMPARE(pendingResolvedLinks[0].canOpenExternally, true);

    qodex::threadui::native::shutdown();

    QTRY_COMPARE(server.authenticatedConnectionCount(), 0);
}

void ThreadUiEngineConnectionTest::rejectsUserInputWhenNoAuthenticatedConnectionExists() {
    qodex::threadui::native::initialize(LaunchConfig{
        .host = "127.0.0.1",
        .port = 1,
        .token = "disconnected-test-token",
    });

    qodex::threadui::native::sendUserInput("This should not queue silently");

    std::string pendingError;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000 && pendingError.empty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        pendingError = qodex::threadui::native::takePendingError();
        if (pendingError.empty()) {
            QTest::qWait(20);
        }
    }

    QVERIFY2(!pendingError.empty(), "Expected a disconnected sendUserInput error.");
    QCOMPARE(pendingError, std::string("Thread UI is not connected to qodex."));

    qodex::threadui::native::shutdown();
}

QTEST_GUILESS_MAIN(ThreadUiEngineConnectionTest)

#include "ThreadUiEngineConnectionTest.moc"
