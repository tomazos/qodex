#pragma once

#include <QDebug>
#include <QStringList>

#include <utility>

namespace qodex::debug {

[[nodiscard]] bool isEnabled();
void setEnabled(bool enabled);
void installStdoutMessageHandler();
[[nodiscard]] bool argumentsContainDebugFlag(const QStringList &arguments);

namespace detail {

inline void emitDebug(const char *file, const int line, const char *function) {
    if (!isEnabled()) {
        return;
    }

    QDebug debug = QMessageLogger(file, line, function).debug();
    debug.noquote().nospace();
}

template <typename First, typename... Rest>
inline void emitDebug(const char *file, const int line, const char *function, First &&first, Rest &&... rest) {
    if (!isEnabled()) {
        return;
    }

    QDebug debug = QMessageLogger(file, line, function).debug();
    debug.noquote().nospace();
    debug << std::forward<First>(first);
    ((debug << ' ' << std::forward<Rest>(rest)), ...);
}

}  // namespace detail

}  // namespace qodex::debug

#define QODEBUG(...) \
    do { \
        ::qodex::debug::detail::emitDebug(__FILE__, __LINE__, Q_FUNC_INFO __VA_OPT__(,) __VA_ARGS__); \
    } while (false)
