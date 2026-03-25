#pragma once

#include <QWidget>

class QTreeView;

namespace qodex::ui {

class ModelsModel;

class ModelsPane final : public QWidget {
    Q_OBJECT

public:
    explicit ModelsPane(ModelsModel *model, QWidget *parent = nullptr);

    [[nodiscard]] QByteArray saveHeaderState() const;
    bool restoreHeaderState(const QByteArray &state);

private:
    void resizeSnugColumns();

    ModelsModel *m_model = nullptr;
    QTreeView *m_treeView = nullptr;
};

}  // namespace qodex::ui
