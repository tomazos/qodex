#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

#include "codex/AppServerTransport.h"

using qodex::codex::AppServerTransport;
using qodex::codex::JsonRpcErrorResponseMessage;
using qodex::codex::JsonRpcResponseMessage;

namespace {

QString findCodexExecutable() {
    return QStandardPaths::findExecutable(QStringLiteral("codex"));
}

}  // namespace

class AppServerTransportTest final : public QObject {
    Q_OBJECT

private slots:
    void experimentalMockMethodEchoesPayload();
};

void AppServerTransportTest::experimentalMockMethodEchoesPayload() {
    const QString codex = findCodexExecutable();
    if (codex.isEmpty()) {
        QSKIP("codex executable was not found on PATH");
    }

    AppServerTransport transport;
    QSignalSpy startedSpy(&transport, &AppServerTransport::started);
    QSignalSpy responseSpy(&transport, &AppServerTransport::responseReceived);
    QSignalSpy transportErrorSpy(&transport, &AppServerTransport::transportError);

    transport.start(codex, QStringList{QStringLiteral("app-server")});

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 3000);

    bool initializeSuccessCalled = false;
    bool initializeErrorCalled = false;
    JsonRpcResponseMessage initializeResponse;

    const auto initializeId = transport.sendRequest(
        QStringLiteral("initialize"),
        QJsonObject{
            {QStringLiteral("clientInfo"), QJsonObject{
                {QStringLiteral("name"), QStringLiteral("qodex-test")},
                {QStringLiteral("version"), QStringLiteral("0.1")},
            }},
            {QStringLiteral("capabilities"), QJsonObject{
                {QStringLiteral("experimentalApi"), true},
            }},
        },
        &transport,
        [&](const JsonRpcResponseMessage &response) {
            initializeSuccessCalled = true;
            initializeResponse = response;
        },
        [&](const JsonRpcErrorResponseMessage &) {
            initializeErrorCalled = true;
        }
    );

    QVERIFY(initializeId.isValid());
    QTRY_VERIFY_WITH_TIMEOUT(initializeSuccessCalled, 5000);
    QVERIFY(!initializeErrorCalled);
    QCOMPARE(initializeResponse.id.toKey(), initializeId.toKey());
    QVERIFY(initializeResponse.result.toObject().contains(QStringLiteral("userAgent")));

    QVERIFY(transport.sendNotification(QStringLiteral("initialized")));

    bool successCallbackCalled = false;
    bool errorCallbackCalled = false;
    JsonRpcResponseMessage callbackResponse;
    JsonRpcErrorResponseMessage callbackError;

    const auto requestId = transport.sendRequest(
        QStringLiteral("mock/experimentalMethod"),
        QJsonObject{
            {QStringLiteral("value"), QStringLiteral("hello")},
        },
        &transport,
        [&](const JsonRpcResponseMessage &response) {
            successCallbackCalled = true;
            callbackResponse = response;
        },
        [&](const JsonRpcErrorResponseMessage &error) {
            errorCallbackCalled = true;
            callbackError = error;
        }
    );

    QVERIFY(requestId.isValid());
    QTRY_COMPARE_WITH_TIMEOUT(responseSpy.count(), 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(successCallbackCalled, 5000);

    QVERIFY(!errorCallbackCalled);
    QVERIFY(callbackError.error.message.isEmpty());
    QCOMPARE(callbackResponse.id.toKey(), requestId.toKey());
    QCOMPARE(
        callbackResponse.result.toObject().value(QStringLiteral("echoed")).toString(),
        QStringLiteral("hello")
    );
    QCOMPARE(transportErrorSpy.count(), 0);

    transport.stop();
}

QTEST_GUILESS_MAIN(AppServerTransportTest)

#include "AppServerTransportTest.moc"
