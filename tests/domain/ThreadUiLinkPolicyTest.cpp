#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "domain/ThreadUiLinkPolicy.h"

using qodex::domain::ThreadUiLinkPolicy;

class ThreadUiLinkPolicyTest final : public QObject {
    Q_OBJECT

private slots:
    void resolvesHttpsLinks();
    void resolvesRelativeFileLinksAgainstThreadCwd();
    void reportsUnresolvedRelativeLinksWithoutThreadCwd();
};

void ThreadUiLinkPolicyTest::resolvesHttpsLinks() {
    ThreadUiLinkPolicy linkPolicy;

    const auto resolvedLink = linkPolicy.resolveLink(QStringLiteral("https://github.com/tomazos/qodex/issues/6"), {});

    QCOMPARE(resolvedLink.kind(), qodex::threadui::ipc::common::LINK_KIND_WEB);
    QCOMPARE(
        QString::fromStdString(resolvedLink.normalized_href()),
        QStringLiteral("https://github.com/tomazos/qodex/issues/6")
    );
    QCOMPARE(resolvedLink.default_action(), qodex::threadui::ipc::common::LINK_ACTION_KIND_OPEN_EXTERNALLY);
    QCOMPARE(resolvedLink.can_open_externally(), true);
    QVERIFY(QString::fromStdString(resolvedLink.tooltip()).contains(QStringLiteral("Default: Open externally")));
}

void ThreadUiLinkPolicyTest::resolvesRelativeFileLinksAgainstThreadCwd() {
    ThreadUiLinkPolicy linkPolicy;
    QTemporaryDir temporaryDir;
    QVERIFY2(temporaryDir.isValid(), "Expected temporary directory to be created.");

    const QString documentsDirPath = QDir(temporaryDir.path()).filePath(QStringLiteral("docs"));
    QVERIFY(QDir().mkpath(documentsDirPath));

    const QString filePath = QDir(documentsDirPath).filePath(QStringLiteral("read me.txt"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(file.write("hello\n") > 0);
    file.close();

    const auto resolvedLink = linkPolicy.resolveLink(
        QStringLiteral("docs/read%20me.txt#L12C3"),
        temporaryDir.path()
    );

    QCOMPARE(resolvedLink.kind(), qodex::threadui::ipc::common::LINK_KIND_FILE);
    QCOMPARE(QString::fromStdString(resolvedLink.resolved_path()), filePath);
    QCOMPARE(resolvedLink.exists(), true);
    QCOMPARE(resolvedLink.is_directory(), false);
    QCOMPARE(resolvedLink.has_line(), true);
    QCOMPARE(resolvedLink.line(), qint64{12});
    QCOMPARE(resolvedLink.has_column(), true);
    QCOMPARE(resolvedLink.column(), qint64{3});
    QCOMPARE(resolvedLink.default_action(), qodex::threadui::ipc::common::LINK_ACTION_KIND_OPEN);
    QCOMPARE(resolvedLink.can_open(), true);
    QCOMPARE(resolvedLink.can_reveal_in_folder(), true);
    QVERIFY(QString::fromStdString(resolvedLink.normalized_href()).startsWith(QStringLiteral("file://")));
}

void ThreadUiLinkPolicyTest::reportsUnresolvedRelativeLinksWithoutThreadCwd() {
    ThreadUiLinkPolicy linkPolicy;

    const auto resolvedLink = linkPolicy.resolveLink(QStringLiteral("src/app/LoadedThread.cpp"), {});

    QCOMPARE(resolvedLink.kind(), qodex::threadui::ipc::common::LINK_KIND_UNKNOWN);
    QCOMPARE(resolvedLink.default_action(), qodex::threadui::ipc::common::LINK_ACTION_KIND_NONE);
    QCOMPARE(resolvedLink.can_open(), false);
    QVERIFY(
        QString::fromStdString(resolvedLink.tooltip())
            .contains(QStringLiteral("Relative link cannot be resolved without a thread cwd"))
    );
}

QTEST_GUILESS_MAIN(ThreadUiLinkPolicyTest)

#include "ThreadUiLinkPolicyTest.moc"
