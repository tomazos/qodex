#pragma once

#include <QWidget>

class QTableView;

namespace qodex::ui {

class ApiLogModel;

class ApiLogPane final : public QWidget {
    Q_OBJECT

public:
    explicit ApiLogPane(ApiLogModel *model, QWidget *parent = nullptr);

    [[nodiscard]] QByteArray saveViewState() const;
    bool restoreViewState(const QByteArray &state);

signals:
    void inspectApiLogRequested(qint64 apiLogId);

private:
    void ensureVisibleRowsLoaded();
    void applyDefaultColumnState();
    void resizeDefaultColumns();
    void showContextMenu(const QPoint &position);
    void showHeaderContextMenu(const QPoint &position);

    ApiLogModel *m_model = nullptr;
    QTableView *m_tableView = nullptr;
    bool m_restoredViewState = false;
};

}  // namespace qodex::ui
