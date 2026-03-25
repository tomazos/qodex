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
    addItemsRequest.add_items()->mutable_user_message()->set_text("Hello from user");
    addItemsRequest.add_items()->mutable_agent_message()->set_text("Hello from agent");
    addItemsRequest.add_items()->mutable_command_execution()->set_text("{\"command\":\"date\"}");
    qodex::threadui::ipc::common::FileChange *fileChange = addItemsRequest.add_items()->mutable_file_change();
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
    QCOMPARE(items[0].kind, std::string("user"));
    QCOMPARE(items[0].text, std::string("Hello from user"));
    QCOMPARE(items[1].kind, std::string("agent"));
    QCOMPARE(items[1].text, std::string("Hello from agent"));
    QCOMPARE(items[2].kind, std::string("command_execution"));
    QCOMPARE(items[2].text, std::string("{\"command\":\"date\"}"));
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

QTEST_GUILESS_MAIN(ThreadUiEngineConnectionTest)

#include "ThreadUiEngineConnectionTest.moc"
