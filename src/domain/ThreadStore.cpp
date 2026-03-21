#include "domain/ThreadStore.h"

#include <algorithm>

namespace qodex::domain {

ThreadStore::ThreadStore(QObject *parent)
    : QObject(parent) {
}

QList<ThreadSummary> ThreadStore::threadSummaries() const {
    return m_threadSummaries;
}

QString ThreadStore::selectedThreadId() const {
    return m_selectedThreadId;
}

std::optional<ThreadSummary> ThreadStore::threadSummaryById(const QString &threadId) const {
    const qsizetype index = indexOfThread(threadId);
    if (index < 0) {
        return std::nullopt;
    }
    return m_threadSummaries.at(index);
}

void ThreadStore::replaceThreadSummaries(QList<ThreadSummary> summaries) {
    replaceThreadSummaries(std::move(summaries), false);
}

void ThreadStore::replaceThreadSummaries(QList<ThreadSummary> summaries, const bool archived) {
    for (ThreadSummary &summary : summaries) {
        summary.archived = archived;
    }

    QList<ThreadSummary> merged;
    merged.reserve(m_threadSummaries.size() + summaries.size());

    for (const ThreadSummary &existing : m_threadSummaries) {
        if (existing.archived != archived) {
            merged.append(existing);
        }
    }

    for (ThreadSummary &summary : summaries) {
        merged.append(std::move(summary));
    }

    m_threadSummaries = std::move(merged);
    sortThreadSummaries();
    emit threadListChanged();

    if (!m_selectedThreadId.isEmpty() && indexOfThread(m_selectedThreadId) < 0) {
        m_selectedThreadId.clear();
        emit selectedThreadChanged(m_selectedThreadId);
    }
}

void ThreadStore::upsertThreadSummary(ThreadSummary summary) {
    const qsizetype existingIndex = indexOfThread(summary.id);
    if (existingIndex >= 0) {
        m_threadSummaries[existingIndex] = std::move(summary);
    } else {
        m_threadSummaries.append(std::move(summary));
    }

    sortThreadSummaries();
    emit threadListChanged();
}

bool ThreadStore::setThreadArchived(const QString &threadId, const bool archived) {
    const qsizetype index = indexOfThread(threadId);
    if (index < 0) {
        return false;
    }
    if (m_threadSummaries[index].archived == archived) {
        return true;
    }
    m_threadSummaries[index].archived = archived;
    sortThreadSummaries();
    emit threadListChanged();
    return true;
}

bool ThreadStore::updateThreadTitle(const QString &threadId, const QString &title) {
    const qsizetype index = indexOfThread(threadId);
    if (index < 0) {
        return false;
    }
    if (m_threadSummaries[index].title == title) {
        return true;
    }
    m_threadSummaries[index].title = title;
    emit threadListChanged();
    return true;
}

bool ThreadStore::updateThreadStatusText(const QString &threadId, const QString &statusText) {
    const qsizetype index = indexOfThread(threadId);
    if (index < 0) {
        return false;
    }
    if (m_threadSummaries[index].statusText == statusText) {
        return true;
    }
    m_threadSummaries[index].statusText = statusText;
    emit threadListChanged();
    return true;
}

bool ThreadStore::removeThreadSummary(const QString &threadId) {
    const qsizetype index = indexOfThread(threadId);
    if (index < 0) {
        return false;
    }

    m_threadSummaries.removeAt(index);
    emit threadListChanged();

    if (m_selectedThreadId == threadId) {
        m_selectedThreadId.clear();
        emit selectedThreadChanged(m_selectedThreadId);
    }
    return true;
}

void ThreadStore::setSelectedThreadId(const QString &threadId) {
    if (threadId == m_selectedThreadId) {
        return;
    }

    if (!threadId.isEmpty() && indexOfThread(threadId) < 0) {
        return;
    }

    m_selectedThreadId = threadId;
    emit selectedThreadChanged(m_selectedThreadId);
}

qsizetype ThreadStore::indexOfThread(const QString &threadId) const {
    for (qsizetype index = 0; index < m_threadSummaries.size(); ++index) {
        if (m_threadSummaries.at(index).id == threadId) {
            return index;
        }
    }
    return -1;
}

void ThreadStore::sortThreadSummaries() {
    std::sort(
        m_threadSummaries.begin(),
        m_threadSummaries.end(),
        [](const ThreadSummary &left, const ThreadSummary &right) {
            if (left.updatedAt != right.updatedAt) {
                return left.updatedAt > right.updatedAt;
            }
            return left.id < right.id;
        }
    );
}

}  // namespace qodex::domain
