#include <QtTest>

#include <QBuffer>
#include <QTextStream>

#include "cli/CliDispatcher.h"

class CliDispatcherTest final : public QObject {
    Q_OBJECT

private slots:
    void recognizesCliInvocationsFromRawArguments();
    void rendersHelpCommandOutput();
};

void CliDispatcherTest::recognizesCliInvocationsFromRawArguments() {
    const qodex::cli::CliDispatcher dispatcher;

    QVERIFY(dispatcher.shouldRunCli({QStringLiteral("help")}));
    QVERIFY(dispatcher.shouldRunCli({QStringLiteral("db"), QStringLiteral("query")}));
    QVERIFY(dispatcher.shouldRunCli({QStringLiteral("--data"), QStringLiteral("/tmp/qodex.sqlite3"), QStringLiteral("db"), QStringLiteral("query")}));
    QVERIFY(dispatcher.shouldRunCli({QStringLiteral("db")}));
    QVERIFY(!dispatcher.shouldRunCli({}));
    QVERIFY(!dispatcher.shouldRunCli({QStringLiteral("--data"), QStringLiteral("/tmp/qodex.sqlite3")}));
    QVERIFY(!dispatcher.shouldRunCli({QStringLiteral("--help")}));
}

void CliDispatcherTest::rendersHelpCommandOutput() {
    const qodex::cli::CliDispatcher dispatcher;
    const qodex::cli::CliCommand *helpCommand = dispatcher.findCommand({QStringLiteral("help")});
    QVERIFY(helpCommand != nullptr);

    QByteArray stdoutBytes;
    QBuffer stdoutBuffer(&stdoutBytes);
    QVERIFY(stdoutBuffer.open(QIODevice::WriteOnly));
    QTextStream stdoutStream(&stdoutBuffer);

    QByteArray stderrBytes;
    QBuffer stderrBuffer(&stderrBytes);
    QVERIFY(stderrBuffer.open(QIODevice::WriteOnly));
    QTextStream stderrStream(&stderrBuffer);

    const int exitCode = helpCommand->run(
        {},
        qodex::cli::CliContext{
            .executableName = QStringLiteral("qodex"),
            .stdoutStream = &stdoutStream,
            .stderrStream = &stderrStream,
            .dispatcher = &dispatcher,
        }
    );

    stdoutStream.flush();
    stderrStream.flush();

    QCOMPARE(exitCode, 0);
    QVERIFY(QString::fromUtf8(stderrBytes).isEmpty());

    const QString stdoutText = QString::fromUtf8(stdoutBytes);
    QVERIFY(stdoutText.contains(QStringLiteral("Qodex CLI")));
    QVERIFY(stdoutText.contains(QStringLiteral("help")));
    QVERIFY(stdoutText.contains(QStringLiteral("db query")));
}

QTEST_GUILESS_MAIN(CliDispatcherTest)

#include "CliDispatcherTest.moc"
