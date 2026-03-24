#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QTcpSocket>

#include "qodex_to_ui.qodex_rpc.h"
#include "threadui/ThreadUiIpcFraming.h"
#include "threadui/ThreadUiIpcServer.h"
#include "ui_to_qodex.qodex_rpc.h"

using qodex::threadui::ThreadUiIpcServer;
using qodex::threadui::ThreadUiLaunchConfig;
namespace UiToQodexRpc = qodex::threadui::ipc::ui_to_qodex::rpc::UiToQodex;

namespace {

bool readNextEnvelope(
    QTcpSocket *socket,
    std::string *inputBuffer,
    qodex::threadui::ipc::common::RpcEnvelope *envelope,
    std::string *errorMessage
) {
    if (socket == nullptr || inputBuffer == nullptr || envelope == nullptr || errorMessage == nullptr) {
        return false;
    }

    qodex::threadui::ipc::FrameDecodeResult decodeResult = qodex::threadui::ipc::FrameDecodeResult::Incomplete;
    errorMessage->clear();

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000 && decodeResult == qodex::threadui::ipc::FrameDecodeResult::Incomplete) {
        decodeResult = qodex::threadui::ipc::tryDecodeNextEnvelope(inputBuffer, envelope, errorMessage);
        if (decodeResult != qodex::threadui::ipc::FrameDecodeResult::Incomplete) {
            break;
        }

        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const QByteArray chunk = socket->readAll();
        if (!chunk.isEmpty()) {
            inputBuffer->append(chunk.constData(), static_cast<std::size_t>(chunk.size()));
            decodeResult = qodex::threadui::ipc::tryDecodeNextEnvelope(inputBuffer, envelope, errorMessage);
        }

        if (decodeResult == qodex::threadui::ipc::FrameDecodeResult::Incomplete) {
            QTest::qWait(20);
        }
    }

    if (decodeResult != qodex::threadui::ipc::FrameDecodeResult::Success) {
        if (errorMessage->empty()) {
            *errorMessage = "Timed out waiting for the next envelope.";
        }
        return false;
    }

    return true;
}

}  // namespace

class ThreadUiIpcServerTest final : public QObject {
    Q_OBJECT

private slots:
    void listensOnLoopbackAndAllocatesDistinctLaunchConfigs();
    void acceptsValidLoginAndRepliesOk();
    void acceptsTestPingAndRepliesOk();
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

    const qodex::threadui::ipc::common::RpcEnvelope envelope =
        qodex::threadui::ipc::makeRequestEnvelope<UiToQodexRpc::Login>(1, request);

    const std::string frame = qodex::threadui::ipc::encodeEnvelopeFrame(envelope);
    QCOMPARE(socket.write(frame.data(), static_cast<qint64>(frame.size())), static_cast<qint64>(frame.size()));
    QVERIFY(socket.waitForBytesWritten(1000));

    std::string inputBuffer;
    qodex::threadui::ipc::common::RpcEnvelope responseEnvelope;
    std::string readErrorMessage;
    QVERIFY2(readNextEnvelope(&socket, &inputBuffer, &responseEnvelope, &readErrorMessage), readErrorMessage.c_str());
    QVERIFY(responseEnvelope.is_response());
    QCOMPARE(responseEnvelope.request_id(), 1U);
    QCOMPARE(responseEnvelope.method(), std::string(UiToQodexRpc::Login::kMethodName));

    qodex::threadui::ipc::ui_to_qodex::LoginResponse response;
    QVERIFY(response.ParseFromString(responseEnvelope.payload()));
    QCOMPARE(response.status(), qodex::threadui::ipc::common::RESULT_STATUS_OK);
    QCOMPARE(response.message(), std::string("Thread UI connection authenticated."));

    QTRY_COMPARE(server.unauthenticatedConnectionCount(), 0);
    QTRY_COMPARE(server.authenticatedConnectionCount(), 1);

    socket.abort();
    QTRY_COMPARE(server.authenticatedConnectionCount(), 0);
}

void ThreadUiIpcServerTest::acceptsTestPingAndRepliesOk() {
    ThreadUiIpcServer server;

    QString errorMessage;
    QVERIFY2(server.listen(&errorMessage), qPrintable(errorMessage));
    const ThreadUiLaunchConfig launchConfig = server.allocateLaunchConfig();

    QTcpSocket socket;
    socket.connectToHost(launchConfig.host, launchConfig.port);
    QVERIFY2(socket.waitForConnected(1000), qPrintable(socket.errorString()));

    qodex::threadui::ipc::ui_to_qodex::LoginRequest loginRequest;
    loginRequest.set_token(launchConfig.token.toStdString());

    const qodex::threadui::ipc::common::RpcEnvelope loginEnvelope =
        qodex::threadui::ipc::makeRequestEnvelope<UiToQodexRpc::Login>(1, loginRequest);

    const std::string loginFrame = qodex::threadui::ipc::encodeEnvelopeFrame(loginEnvelope);
    QCOMPARE(socket.write(loginFrame.data(), static_cast<qint64>(loginFrame.size())), static_cast<qint64>(loginFrame.size()));
    QVERIFY(socket.waitForBytesWritten(1000));

    std::string inputBuffer;
    qodex::threadui::ipc::common::RpcEnvelope loginResponseEnvelope;
    std::string readErrorMessage;
    QVERIFY2(
        readNextEnvelope(&socket, &inputBuffer, &loginResponseEnvelope, &readErrorMessage),
        readErrorMessage.c_str()
    );
    QVERIFY(loginResponseEnvelope.is_response());
    QCOMPARE(loginResponseEnvelope.method(), std::string(UiToQodexRpc::Login::kMethodName));

    qodex::threadui::ipc::ui_to_qodex::TestPingRequest testPingRequest;
    testPingRequest.set_value(7);

    const qodex::threadui::ipc::common::RpcEnvelope testPingEnvelope =
        qodex::threadui::ipc::makeRequestEnvelope<UiToQodexRpc::TestPing>(2, testPingRequest);

    const std::string testPingFrame = qodex::threadui::ipc::encodeEnvelopeFrame(testPingEnvelope);
    QCOMPARE(
        socket.write(testPingFrame.data(), static_cast<qint64>(testPingFrame.size())),
        static_cast<qint64>(testPingFrame.size())
    );
    QVERIFY(socket.waitForBytesWritten(1000));

    qodex::threadui::ipc::common::RpcEnvelope testPingResponseEnvelope;
    QVERIFY2(
        readNextEnvelope(&socket, &inputBuffer, &testPingResponseEnvelope, &readErrorMessage),
        readErrorMessage.c_str()
    );
    QVERIFY(testPingResponseEnvelope.is_response());
    QCOMPARE(testPingResponseEnvelope.request_id(), 2U);
    QCOMPARE(testPingResponseEnvelope.method(), std::string(UiToQodexRpc::TestPing::kMethodName));

    qodex::threadui::ipc::ui_to_qodex::TestPingResponse testPingResponse;
    QVERIFY(testPingResponse.ParseFromString(testPingResponseEnvelope.payload()));
    QCOMPARE(testPingResponse.status(), qodex::threadui::ipc::common::RESULT_STATUS_OK);
    QCOMPARE(testPingResponse.message(), std::string("Test ping received."));

    QCOMPARE(server.highestReceivedTestPing(), 7);

    socket.abort();
    QTRY_COMPARE(server.authenticatedConnectionCount(), 0);
}

QTEST_GUILESS_MAIN(ThreadUiIpcServerTest)

#include "ThreadUiIpcServerTest.moc"
