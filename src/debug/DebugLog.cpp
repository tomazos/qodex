#include "debug/DebugLog.h"

#include <QMutex>
#include <QMutexLocker>

#include <atomic>
#include <cstdio>

namespace qodex::debug {

namespace {

std::atomic<bool> s_enabled{false};
std::atomic<bool> s_stdoutHandlerInstalled{false};

QMutex &messageHandlerMutex() {
    static QMutex mutex;
    return mutex;
}

void stdoutMessageHandler(const QtMsgType type, const QMessageLogContext &context, const QString &message) {
    const QString formatted = qFormatLogMessage(type, context, message);
    const QByteArray utf8 = formatted.toUtf8();

    const QMutexLocker locker(&messageHandlerMutex());
    std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

}  // namespace

bool isEnabled() {
    return s_enabled.load(std::memory_order_relaxed);
}

void setEnabled(const bool enabled) {
    s_enabled.store(enabled, std::memory_order_relaxed);
}

void installStdoutMessageHandler() {
    if (s_stdoutHandlerInstalled.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    qSetMessagePattern(
        QStringLiteral("[%{time yyyy-MM-dd hh:mm:ss.zzz} %{type}] %{if-category}%{category}: %{endif}%{message}")
    );
    qInstallMessageHandler(stdoutMessageHandler);
}

bool argumentsContainDebugFlag(const QStringList &arguments) {
    for (const QString &argument : arguments) {
        if (argument == QStringLiteral("--")) {
            return false;
        }
        if (argument == QStringLiteral("--debug")) {
            return true;
        }
    }

    return false;
}

}  // namespace qodex::debug
