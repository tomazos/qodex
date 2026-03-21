#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

#include "CodexClient.h"
#include "codex/AppServerTransport.h"

using qodex::codex::AppServerTransport;
using qodex::codex::CodexClient;
using qodex::codex::ClientInfo;
using qodex::codex::InitializeCapabilities;
using qodex::codex::InitializeResponse;
using qodex::codex::JsonRpcErrorObject;
using qodex::codex::JsonRpcId;
using qodex::codex::MockExperimentalMethodResponse;
using qodex::codex::Nullable;
using qodex::codex::Ref;

namespace {

QString findCodexExecutable() {
    return QStandardPaths::findExecutable(QStringLiteral("codex"));
}

}  // namespace

class CodexClientGeneratedTest final : public QObject {
    Q_OBJECT

private slots:
    void experimentalMockMethodEchoesPayload();
};

void CodexClientGeneratedTest::experimentalMockMethodEchoesPayload() {
    const QString codex = findCodexExecutable();
    if (codex.isEmpty()) {
        QSKIP("codex executable was not found on PATH");
    }

    AppServerTransport transport;
    CodexClient client(&transport);

    QSignalSpy startedSpy(&transport, &AppServerTransport::started);
    QSignalSpy transportErrorSpy(&transport, &AppServerTransport::transportError);

    transport.start(codex, QStringList{QStringLiteral("app-server")});

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 3000);

    bool initializeSucceeded = false;
    bool initializeFailed = false;
    JsonRpcId initializeResponseId;
    InitializeResponse initializeResult;

    QObject::connect(
        &client,
        &CodexClient::initializeSucceeded,
        &client,
        [&](const JsonRpcId &id, const InitializeResponse &result) {
            initializeSucceeded = true;
            initializeResponseId = id;
            initializeResult = result;
        }
    );
    QObject::connect(
        &client,
        &CodexClient::initializeFailed,
        &client,
        [&](const JsonRpcId &, const JsonRpcErrorObject &) {
            initializeFailed = true;
        }
    );

    Ref<InitializeCapabilities> capabilities = Ref<InitializeCapabilities>::create();
    capabilities->experimentalApi = true;
    const Nullable<Ref<InitializeCapabilities>> initializeCapabilities =
        Nullable<Ref<InitializeCapabilities>>::fromValue(capabilities);

    Ref<ClientInfo> clientInfo = Ref<ClientInfo>::create();
    clientInfo->name = QStringLiteral("qodex-test");
    clientInfo->version = QStringLiteral("0.1");

    const JsonRpcId initializeId = client.sendInitializeRequest(
        initializeCapabilities,
        clientInfo
    );

    QVERIFY(initializeId.isValid());
    QTRY_VERIFY_WITH_TIMEOUT(initializeSucceeded, 5000);
    QVERIFY(!initializeFailed);
    QCOMPARE(initializeResponseId.toKey(), initializeId.toKey());
    QVERIFY(!initializeResult.userAgent.isEmpty());

    QVERIFY(client.sendInitializedNotification());

    bool mockSucceeded = false;
    bool mockFailed = false;
    JsonRpcId mockResponseId;
    MockExperimentalMethodResponse mockResult;

    QObject::connect(
        &client,
        &CodexClient::mockExperimentalMethodSucceeded,
        &client,
        [&](const JsonRpcId &id, const MockExperimentalMethodResponse &result) {
            mockSucceeded = true;
            mockResponseId = id;
            mockResult = result;
        }
    );
    QObject::connect(
        &client,
        &CodexClient::mockExperimentalMethodFailed,
        &client,
        [&](const JsonRpcId &, const JsonRpcErrorObject &) {
            mockFailed = true;
        }
    );

    const Nullable<QString> mockValue = Nullable<QString>::fromValue(QStringLiteral("hello"));

    const JsonRpcId requestId = client.sendMockExperimentalMethodRequest(mockValue);

    QVERIFY(requestId.isValid());
    QTRY_VERIFY_WITH_TIMEOUT(mockSucceeded, 5000);
    QVERIFY(!mockFailed);
    QCOMPARE(mockResponseId.toKey(), requestId.toKey());
    QVERIFY(mockResult.echoed.hasValue());
    QCOMPARE(mockResult.echoed.value(), QStringLiteral("hello"));
    QCOMPARE(transportErrorSpy.count(), 0);

    transport.stop();
}

QTEST_GUILESS_MAIN(CodexClientGeneratedTest)

#include "CodexClientGeneratedTest.moc"
