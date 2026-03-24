#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QTcpSocket>

#include "threadui/ThreadUiIpcServer.h"
#include "threadui/ThreadUiIpcFraming.h"
#include "common.pb.h"
#include "ui_to_qodex.pb.h"

using qodex::threadui::ThreadUiIpcServer;
using qodex::threadui::ThreadUiLaunchConfig;

class ThreadUiIpcServerTest final : public QObject {
    Q_OBJECT

private slots:
    void listensOnLoopbackAndAllocatesDistinctLaunchConfigs();
    void acceptsValidLoginAndRepliesOk();
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

void ThreadUiIpcServerTest::acceptsValidLoginAndRepliesOk() {
    ThreadUiIpcServer server;

    QString errorMessage;
    QVERIFY2(server.listen(&errorMessage), qPrintable(errorMessage));
    const ThreadUiLaunchConfig launchConfig = server.allocateLaunchConfig();

    QTcpSocket socket;
    socket.connectToHost(launchConfig.host, launchConfig.port);
    QVERIFY2(socket.waitForConnected(1000), qPrintable(socket.errorString()));

    qodex::threadui::ipc::ui_to_qodex::LoginRequest request;
    request.set_token(launchConfig.token.toStdString());

    qodex::threadui::ipc::common::RpcEnvelope envelope;
    envelope.set_request_id(1);
    envelope.set_is_response(false);
    envelope.set_method(qodex::threadui::ipc::kLoginMethodName);
    envelope.set_payload(request.SerializeAsString());

    const std::string frame = qodex::threadui::ipc::encodeEnvelopeFrame(envelope);
    QCOMPARE(socket.write(frame.data(), static_cast<qint64>(frame.size())), static_cast<qint64>(frame.size()));
    QVERIFY(socket.waitForBytesWritten(1000));

    std::string inputBuffer;
    qodex::threadui::ipc::common::RpcEnvelope responseEnvelope;
    qodex::threadui::ipc::FrameDecodeResult decodeResult = qodex::threadui::ipc::FrameDecodeResult::Incomplete;
    std::string parseErrorMessage;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000 && decodeResult == qodex::threadui::ipc::FrameDecodeResult::Incomplete) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const QByteArray chunk = socket.readAll();
        if (!chunk.isEmpty()) {
            inputBuffer.append(chunk.constData(), static_cast<std::size_t>(chunk.size()));
            decodeResult = qodex::threadui::ipc::tryDecodeNextEnvelope(
                &inputBuffer,
                &responseEnvelope,
                &parseErrorMessage
            );
        }

        if (decodeResult == qodex::threadui::ipc::FrameDecodeResult::Incomplete) {
            QTest::qWait(20);
        }
    }

    QCOMPARE(decodeResult, qodex::threadui::ipc::FrameDecodeResult::Success);
    QVERIFY2(parseErrorMessage.empty(), parseErrorMessage.c_str());
    QVERIFY(responseEnvelope.is_response());
    QCOMPARE(responseEnvelope.request_id(), 1U);
    QCOMPARE(responseEnvelope.method(), std::string(qodex::threadui::ipc::kLoginMethodName));

    qodex::threadui::ipc::ui_to_qodex::LoginResponse response;
    QVERIFY(response.ParseFromString(responseEnvelope.payload()));
    QCOMPARE(response.status(), qodex::threadui::ipc::common::RESULT_STATUS_OK);
    QCOMPARE(response.message(), std::string("Thread UI connection authenticated."));

    QTRY_COMPARE(server.unauthenticatedConnectionCount(), 0);
    QTRY_COMPARE(server.authenticatedConnectionCount(), 1);

    socket.abort();
    QTRY_COMPARE(server.authenticatedConnectionCount(), 0);
}

QTEST_GUILESS_MAIN(ThreadUiIpcServerTest)

#include "ThreadUiIpcServerTest.moc"
