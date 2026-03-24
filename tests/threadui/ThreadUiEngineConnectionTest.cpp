#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>

#include "qodex_to_ui.pb.h"
#include "threadui/ThreadUiIpcServer.h"
#include "threadui_native/ThreadUiEngine.h"

using qodex::threadui::ThreadUiIpcServer;
using qodex::threadui::native::LaunchConfig;

class ThreadUiEngineConnectionTest final : public QObject {
    Q_OBJECT

private slots:
    void establishesTcpConnectionToQodexListener();
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

    QCOMPARE(items.size(), std::size_t(2));
    QCOMPARE(items[0].kind, qodex::threadui::native::DisplayItemKind::UserMessage);
    QCOMPARE(items[0].text, std::string("Hello from user"));
    QCOMPARE(items[1].kind, qodex::threadui::native::DisplayItemKind::AgentMessage);
    QCOMPARE(items[1].text, std::string("Hello from agent"));

    qodex::threadui::native::shutdown();

    QTRY_COMPARE(server.unauthenticatedConnectionCount(), 0);
    QTRY_COMPARE(server.authenticatedConnectionCount(), 0);
}

QTEST_GUILESS_MAIN(ThreadUiEngineConnectionTest)

#include "ThreadUiEngineConnectionTest.moc"
