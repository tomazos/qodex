#pragma once

#include <QWidget>

class QListView;
class QPushButton;

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

private:
    void emitSelectionForIndex(const QModelIndex &index);

    ThreadListModel *m_model = nullptr;
    QListView *m_listView = nullptr;
    QPushButton *m_refreshButton = nullptr;
};

}  // namespace qodex::ui
