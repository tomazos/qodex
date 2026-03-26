#include <QtTest>

#include <QScopeGuard>
#include <QStringList>

#include "debug/DebugLog.h"

namespace {

QStringList *g_capturedMessages = nullptr;

void captureMessageHandler(QtMsgType, const QMessageLogContext &, const QString &message) {
    if (g_capturedMessages != nullptr) {
        g_capturedMessages->append(message);
    }
}

}  // namespace

class DebugLogTest final : public QObject {
    Q_OBJECT

private slots:
    void detectsDebugFlagInArguments();
    void qodebugRespectsRuntimeEnableFlag();
};

void DebugLogTest::detectsDebugFlagInArguments() {
    QVERIFY(qodex::debug::argumentsContainDebugFlag({QStringLiteral("--debug")}));
    QVERIFY(qodex::debug::argumentsContainDebugFlag({QStringLiteral("--data"), QStringLiteral("/tmp/qodex.sqlite3"), QStringLiteral("--debug")}));
    QVERIFY(!qodex::debug::argumentsContainDebugFlag({}));
    QVERIFY(!qodex::debug::argumentsContainDebugFlag({QStringLiteral("--data"), QStringLiteral("/tmp/qodex.sqlite3")}));
    QVERIFY(!qodex::debug::argumentsContainDebugFlag({QStringLiteral("--"), QStringLiteral("--debug")}));
}

void DebugLogTest::qodebugRespectsRuntimeEnableFlag() {
    QStringList capturedMessages;
    g_capturedMessages = &capturedMessages;

    const QtMessageHandler previousHandler = qInstallMessageHandler(captureMessageHandler);
    const auto restoreState = qScopeGuard([previousHandler] {
        qInstallMessageHandler(previousHandler);
        qodex::debug::setEnabled(false);
        g_capturedMessages = nullptr;
    });

    qodex::debug::setEnabled(false);
    QODEBUG("hidden message", 1);
    QVERIFY(capturedMessages.isEmpty());

    qodex::debug::setEnabled(true);
    QODEBUG("visible message", 2);

    QCOMPARE(capturedMessages.size(), 1);
    QCOMPARE(capturedMessages.constFirst(), QStringLiteral("visible message 2"));
}

QTEST_GUILESS_MAIN(DebugLogTest)

#include "DebugLogTest.moc"
