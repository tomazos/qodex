#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>

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
    void sendsUserInputRequestsAndReportsErrors();
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
    commandExecution->add_action_labels("Read /home/zos/file.txt");
    qodex::threadui::ipc::common::Item *fileChangeItem = addItemsRequest.add_items();
    fileChangeItem->set_item_id("file-1");
    qodex::threadui::ipc::common::FileChange *fileChange = fileChangeItem->mutable_file_change();
    fileChange->set_status("completed");
    qodex::threadui::ipc::common::FileChangeChange *fileChangeUpdate = fileChange->add_changes();
    fileChangeUpdate->set_path("/tmp/styles.css");
    fileChangeUpdate->set_kind(qodex::threadui::ipc::common::FILE_CHANGE_KIND_UPDATE);
    fileChangeUpdate->set_diff("@@ -1 +1 @@\n-color: Highlight;\n+color: LinkText;");

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

    QCOMPARE(items.size(), std::size_t(4));
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
    QCOMPARE(items[2].commandExecution.actionLabels.size(), std::size_t(1));
    QCOMPARE(items[2].commandExecution.actionLabels[0], std::string("Read /home/zos/file.txt"));
    QCOMPARE(items[3].id, std::string("file-1"));
    QCOMPARE(items[3].kind, std::string("file_change"));
    QCOMPARE(items[3].fileChange.status, std::string("completed"));
    QCOMPARE(items[3].fileChange.changes.size(), std::size_t(1));
    QCOMPARE(items[3].fileChange.changes[0].path, std::string("/tmp/styles.css"));
    QCOMPARE(items[3].fileChange.changes[0].kind, std::string("update"));
    QCOMPARE(items[3].fileChange.changes[0].diff, std::string("@@ -1 +1 @@\n-color: Highlight;\n+color: LinkText;"));

    qodex::threadui::native::shutdown();

    QTRY_COMPARE(server.unauthenticatedConnectionCount(), 0);
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
