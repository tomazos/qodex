#include <QtTest>

#include <QRegularExpression>
#include <QTcpSocket>

#include "threadui/ThreadUiIpcServer.h"

using qodex::threadui::ThreadUiIpcServer;
using qodex::threadui::ThreadUiLaunchConfig;

class ThreadUiIpcServerTest final : public QObject {
    Q_OBJECT

private slots:
    void listensOnLoopbackAndAllocatesDistinctLaunchConfigs();
};

void ThreadUiIpcServerTest::listensOnLoopbackAndAllocatesDistinctLaunchConfigs() {
    ThreadUiIpcServer server;

    QString errorMessage;
    QVERIFY2(server.listen(&errorMessage), qPrintable(errorMessage));
    QVERIFY(server.isListening());
    QCOMPARE(server.host(), QStringLiteral("127.0.0.1"));
    QVERIFY(server.port() != 0);

    const ThreadUiLaunchConfig firstLaunchConfig = server.allocateLaunchConfig();
    const ThreadUiLaunchConfig secondLaunchConfig = server.allocateLaunchConfig();

    QCOMPARE(firstLaunchConfig.host, server.host());
    QCOMPARE(firstLaunchConfig.port, server.port());
    QVERIFY(!firstLaunchConfig.token.isEmpty());
    QVERIFY(!secondLaunchConfig.token.isEmpty());
    QVERIFY(firstLaunchConfig.token != secondLaunchConfig.token);
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{16}$")).match(firstLaunchConfig.token).hasMatch());
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{16}$")).match(secondLaunchConfig.token).hasMatch());
    QCOMPARE(server.unauthenticatedConnectionCount(), 0);

    QTcpSocket socket;
    socket.connectToHost(firstLaunchConfig.host, firstLaunchConfig.port);
    QVERIFY2(socket.waitForConnected(1000), qPrintable(socket.errorString()));
    QTRY_COMPARE(server.unauthenticatedConnectionCount(), 1);

    socket.abort();
    QTRY_COMPARE(server.unauthenticatedConnectionCount(), 0);
}

QTEST_GUILESS_MAIN(ThreadUiIpcServerTest)

#include "ThreadUiIpcServerTest.moc"
