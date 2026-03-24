#pragma once

#include <QStringList>
#include <QWidget>

class QTreeView;

namespace qodex::ui {

class ThreadListModel;

class ThreadListPane final : public QWidget {
    Q_OBJECT

public:
    explicit ThreadListPane(ThreadListModel *model, QWidget *parent = nullptr);

    [[nodiscard]] QByteArray saveHeaderState() const;
    bool restoreHeaderState(const QByteArray &state);
    void setCurrentThreadId(const QString &threadId);

signals:
    void refreshRequested();
    void threadSelected(const QString &threadId);
    void renameThreadRequested(const QString &threadId);
    void closeThreadsRequested(const QStringList &threadIds);
    void forkThreadRequested(const QString &threadId);
    void archiveThreadsRequested(const QStringList &threadIds);
    void unarchiveThreadsRequested(const QStringList &threadIds);

private:
    void resizeSnugColumns();
    void emitSelectionForIndex(const QModelIndex &index);
    void showContextMenu(const QPoint &position);
    void showHeaderContextMenu(const QPoint &position);

    ThreadListModel *m_model = nullptr;
    QTreeView *m_treeView = nullptr;
};

}  // namespace qodex::ui
