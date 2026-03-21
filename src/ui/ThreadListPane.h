#pragma once

#include <QStringList>
#include <QWidget>

class QPushButton;
class QTreeView;

namespace qodex::ui {

class ThreadListModel;

class ThreadListPane final : public QWidget {
    Q_OBJECT

public:
    explicit ThreadListPane(ThreadListModel *model, QWidget *parent = nullptr);

    void setCurrentThreadId(const QString &threadId);

signals:
    void refreshRequested();
    void threadSelected(const QString &threadId);
    void archiveThreadsRequested(const QStringList &threadIds);
    void unarchiveThreadsRequested(const QStringList &threadIds);

private:
    void emitSelectionForIndex(const QModelIndex &index);
    void showContextMenu(const QPoint &position);

    ThreadListModel *m_model = nullptr;
    QTreeView *m_treeView = nullptr;
    QPushButton *m_refreshButton = nullptr;
};

}  // namespace qodex::ui
