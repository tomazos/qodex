#pragma once

#include <QObject>
#include <QList>
#include <QString>

#include <optional>

#include "domain/CodexTypes.h"

namespace qodex::domain {

class ThreadStore final : public QObject {
    Q_OBJECT

public:
    explicit ThreadStore(QObject *parent = nullptr);

    [[nodiscard]] QList<ThreadSummary> threadSummaries() const;
    [[nodiscard]] QString selectedThreadId() const;
    [[nodiscard]] std::optional<ThreadSummary> threadSummaryById(const QString &threadId) const;

    void replaceThreadSummaries(QList<ThreadSummary> summaries);
    void replaceThreadSummaries(QList<ThreadSummary> summaries, bool archived);
    void upsertThreadSummary(ThreadSummary summary);
    bool setThreadArchived(const QString &threadId, bool archived);
    bool updateThreadTitle(const QString &threadId, const QString &title);
    bool updateThreadStatusText(const QString &threadId, const QString &statusText);
    bool removeThreadSummary(const QString &threadId);
    void setSelectedThreadId(const QString &threadId);

signals:
    void threadListChanged();
    void selectedThreadChanged(const QString &threadId);

private:
    [[nodiscard]] qsizetype indexOfThread(const QString &threadId) const;
    void sortThreadSummaries();

    QList<ThreadSummary> m_threadSummaries;
    QString m_selectedThreadId;
};

}  // namespace qodex::domain
