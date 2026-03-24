#include <QtTest>

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

    qodex::threadui::native::shutdown();

    QTRY_COMPARE(server.unauthenticatedConnectionCount(), 0);
    QTRY_COMPARE(server.authenticatedConnectionCount(), 0);
}

QTEST_GUILESS_MAIN(ThreadUiEngineConnectionTest)

#include "ThreadUiEngineConnectionTest.moc"
