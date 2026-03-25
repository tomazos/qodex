#pragma once

#include <QWidget>

class QTreeView;

namespace qodex::ui {

class LoadedThreadsModel;

class LoadedThreadsPane final : public QWidget {
    Q_OBJECT

public:
    explicit LoadedThreadsPane(LoadedThreadsModel *model, QWidget *parent = nullptr);

    [[nodiscard]] QByteArray saveHeaderState() const;
    bool restoreHeaderState(const QByteArray &state);

private:
    void resizeSnugColumns();

    LoadedThreadsModel *m_model = nullptr;
    QTreeView *m_treeView = nullptr;
};

}  // namespace qodex::ui
