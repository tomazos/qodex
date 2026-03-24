#include "ui/ThreadListPane.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMouseEvent>
#include <QModelIndex>
#include <QSignalBlocker>
#include <QTreeView>
#include <QVBoxLayout>

#include "ui/ThreadListModel.h"

namespace qodex::ui {

namespace {

constexpr auto kContextSelectionSnapshotProperty = "_qodex_contextSelectionSnapshot";

class ThreadTreeView final : public QTreeView {
public:
    using QTreeView::QTreeView;

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event != nullptr && event->button() == Qt::RightButton && selectionModel() != nullptr) {
            const QModelIndex clickedIndex = indexAt(event->pos()).siblingAtColumn(ThreadListModel::ThreadColumn);
            if (clickedIndex.isValid() && selectionModel()->isRowSelected(clickedIndex.row(), clickedIndex.parent())) {
                QStringList snapshot;
                const QModelIndexList selectedRows = selectionModel()->selectedRows(ThreadListModel::ThreadColumn);
                snapshot.reserve(selectedRows.size());
                for (const QModelIndex &selectedRow : selectedRows) {
                    const QString threadId = selectedRow.data(ThreadListModel::IdRole).toString();
                    if (!threadId.isEmpty()) {
                        snapshot.append(threadId);
                    }
                }
                setProperty(kContextSelectionSnapshotProperty, snapshot);

                selectionModel()->setCurrentIndex(clickedIndex, QItemSelectionModel::NoUpdate);
                event->accept();
                return;
            }
        }

        QTreeView::mousePressEvent(event);
    }
};

}  // namespace

ThreadListPane::ThreadListPane(ThreadListModel *model, QWidget *parent)
    : QWidget(parent),
      m_model(model) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_treeView = new ThreadTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setSortingEnabled(true);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setWordWrap(false);
    m_treeView->setAllColumnsShowFocus(true);
    m_treeView->setIconSize(QSize(16, 16));
    m_treeView->setRootIsDecorated(true);
    m_treeView->setItemsExpandable(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *header = m_treeView->header();
    header->setStretchLastSection(false);
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    header->setSectionsMovable(true);
    header->setFirstSectionMovable(false);
    for (int column = ThreadListModel::ThreadColumn; column < ThreadListModel::ColumnCount; ++column) {
        header->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    for (int column = ThreadListModel::StatusColumn; column < ThreadListModel::ColumnCount; ++column) {
        if (column == ThreadListModel::IdColumn) {
            continue;
        }
        header->setSectionHidden(column, true);
    }
    m_treeView->resizeColumnToContents(ThreadListModel::ThreadColumn);
    if (header->sectionSize(ThreadListModel::ThreadColumn) < 220) {
        header->resizeSection(ThreadListModel::ThreadColumn, 220);
    }
    resizeSnugColumns();
    m_treeView->sortByColumn(ThreadListModel::ModifiedColumn, Qt::DescendingOrder);

    layout->addWidget(m_treeView, 1);

    connect(
        m_treeView,
        &QWidget::customContextMenuRequested,
        this,
        &ThreadListPane::showContextMenu
    );
    connect(
        m_treeView->header(),
        &QWidget::customContextMenuRequested,
        this,
        &ThreadListPane::showHeaderContextMenu
    );
    connect(
        m_treeView->selectionModel(),
        &QItemSelectionModel::currentChanged,
        this,
        [this](const QModelIndex &current) { emitSelectionForIndex(current); }
    );
    connect(
        m_treeView,
        &QTreeView::activated,
        this,
        [this](const QModelIndex &index) { emitSelectionForIndex(index); }
    );
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        if (m_treeView != nullptr) {
            m_treeView->expandAll();
            resizeSnugColumns();
        }
    });

    m_treeView->expandAll();
    resizeSnugColumns();
}

QByteArray ThreadListPane::saveHeaderState() const {
    if (m_treeView == nullptr || m_treeView->header() == nullptr) {
        return {};
    }
    return m_treeView->header()->saveState();
}

bool ThreadListPane::restoreHeaderState(const QByteArray &state) {
    if (m_treeView == nullptr || m_treeView->header() == nullptr || state.isEmpty()) {
        return false;
    }

    const bool restored = m_treeView->header()->restoreState(state);
    if (restored) {
        resizeSnugColumns();
    }
    return restored;
}

void ThreadListPane::resizeSnugColumns() {
    if (m_treeView == nullptr || m_treeView->header() == nullptr) {
        return;
    }

    auto *header = m_treeView->header();
    for (const int column : {
             ThreadListModel::CreatedColumn,
             ThreadListModel::ModifiedColumn,
             ThreadListModel::IdColumn,
         }) {
        if (!header->isSectionHidden(column)) {
            m_treeView->resizeColumnToContents(column);
        }
    }
}

void ThreadListPane::setCurrentThreadId(const QString &threadId) {
    if (m_model == nullptr || m_treeView == nullptr) {
        return;
    }

    if (threadId.isEmpty()) {
        m_treeView->clearSelection();
        return;
    }

    const auto findThreadIndex = [&](const auto &self, const QModelIndex &parent) -> QModelIndex {
        for (int row = 0; row < m_model->rowCount(parent); ++row) {
            const QModelIndex index = m_model->index(row, ThreadListModel::ThreadColumn, parent);
            if (index.data(ThreadListModel::IdRole).toString() == threadId) {
                return index;
            }

            const QModelIndex nested = self(self, index);
            if (nested.isValid()) {
                return nested;
            }
        }
        return {};
    };

    const QModelIndex selectedIndex = findThreadIndex(findThreadIndex, QModelIndex{});
    if (selectedIndex.isValid()) {
        for (QModelIndex parent = selectedIndex.parent(); parent.isValid(); parent = parent.parent()) {
            m_treeView->expand(parent);
        }
        if (m_treeView->selectionModel() != nullptr) {
            QSignalBlocker blocker(m_treeView->selectionModel());
            m_treeView->selectionModel()->setCurrentIndex(selectedIndex, QItemSelectionModel::NoUpdate);
        } else {
            m_treeView->setCurrentIndex(selectedIndex);
        }
        m_treeView->scrollTo(selectedIndex, QAbstractItemView::PositionAtCenter);
        return;
    }

    m_treeView->clearSelection();
}

void ThreadListPane::emitSelectionForIndex(const QModelIndex &index) {
    if (!index.isValid()) {
        return;
    }
    const QString threadId = index.data(ThreadListModel::IdRole).toString();
    if (threadId.isEmpty()) {
        return;
    }
    emit threadSelected(threadId);
}

void ThreadListPane::showContextMenu(const QPoint &position) {
    if (m_treeView == nullptr || m_model == nullptr || m_treeView->selectionModel() == nullptr) {
        return;
    }

    QModelIndex index = m_treeView->indexAt(position);
    QString clickedThreadId;
    QStringList selectedThreadIds;
    QStringList archiveThreadIds;
    QStringList unarchiveThreadIds;
    if (index.isValid()) {
        index = index.siblingAtColumn(ThreadListModel::ThreadColumn);
        clickedThreadId = index.data(ThreadListModel::IdRole).toString();
        if (!clickedThreadId.isEmpty()) {
        const QStringList snapshot =
            m_treeView->property(kContextSelectionSnapshotProperty).toStringList();
        m_treeView->setProperty(kContextSelectionSnapshotProperty, {});

        const bool clickedWasInSnapshot = snapshot.contains(clickedThreadId);
        if (clickedWasInSnapshot) {
            QSignalBlocker blocker(m_treeView->selectionModel());
            m_treeView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        } else if (!m_treeView->selectionModel()->isSelected(index)) {
            m_treeView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
            m_treeView->selectionModel()->select(
                index,
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows
            );
        } else {
            m_treeView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        }

        selectedThreadIds = clickedWasInSnapshot
            ? snapshot
            : QStringList{};
        if (!clickedWasInSnapshot) {
            const QModelIndexList selectedRows = m_treeView->selectionModel()->selectedRows(ThreadListModel::ThreadColumn);
            selectedThreadIds.reserve(selectedRows.size());
            for (const QModelIndex &selectedIndex : selectedRows) {
                const QString threadId = selectedIndex.data(ThreadListModel::IdRole).toString();
                if (!threadId.isEmpty()) {
                    selectedThreadIds.append(threadId);
                }
            }
        }

        archiveThreadIds.reserve(selectedThreadIds.size());
        unarchiveThreadIds.reserve(selectedThreadIds.size());

        for (const QString &threadId : selectedThreadIds) {
            const QModelIndex threadIndex = m_model->match(
                m_model->index(0, ThreadListModel::ThreadColumn),
                ThreadListModel::IdRole,
                threadId,
                1,
                Qt::MatchExactly | Qt::MatchRecursive
            ).value(0);
            if (!threadIndex.isValid()) {
                continue;
            }

            const bool archived = threadIndex.data(ThreadListModel::ArchivedRole).toBool();
            if (archived) {
                unarchiveThreadIds.append(threadId);
            } else {
                archiveThreadIds.append(threadId);
            }
        }
        }
    }

    QMenu menu(this);
    QAction *refreshAction = menu.addAction(QStringLiteral("Refresh Thread List"));
    QAction *resumeAction = nullptr;
    QAction *renameAction = nullptr;
    QAction *closeAction = nullptr;
    QAction *forkAction = nullptr;
    QAction *archiveAction = nullptr;
    QAction *unarchiveAction = nullptr;
    QStringList closeThreadIds;

    if (!clickedThreadId.isEmpty()) {
        menu.addSeparator();

        resumeAction = menu.addAction(QStringLiteral("Resume Thread"));
        resumeAction->setEnabled(selectedThreadIds.size() == 1);
        renameAction = menu.addAction(QStringLiteral("Rename..."));
        renameAction->setEnabled(selectedThreadIds.size() == 1);
        for (const QString &threadId : selectedThreadIds) {
            const QModelIndex threadIndex = m_model->match(
                m_model->index(0, ThreadListModel::ThreadColumn),
                ThreadListModel::IdRole,
                threadId,
                1,
                Qt::MatchExactly | Qt::MatchRecursive
            ).value(0);
            if (!threadIndex.isValid()) {
                continue;
            }
            if (threadIndex.data(ThreadListModel::StatusRole).toString() != QStringLiteral("Not Loaded")) {
                closeThreadIds.append(threadId);
            }
        }
        if (!closeThreadIds.isEmpty()) {
            closeAction = menu.addAction(
                closeThreadIds.size() == 1 ? QStringLiteral("Close Thread")
                                           : QStringLiteral("Close %1 Threads").arg(closeThreadIds.size())
            );
        }
        forkAction = menu.addAction(QStringLiteral("Fork Thread"));
        forkAction->setEnabled(selectedThreadIds.size() == 1);
        if (!archiveThreadIds.isEmpty() || !unarchiveThreadIds.isEmpty()) {
            menu.addSeparator();
        }

        if (!archiveThreadIds.isEmpty()) {
            archiveAction = menu.addAction(
                archiveThreadIds.size() == 1 ? QStringLiteral("Archive Thread")
                                             : QStringLiteral("Archive %1 Threads").arg(archiveThreadIds.size())
            );
        }
        if (!unarchiveThreadIds.isEmpty()) {
            unarchiveAction = menu.addAction(
                unarchiveThreadIds.size() == 1 ? QStringLiteral("Unarchive Thread")
                                               : QStringLiteral("Unarchive %1 Threads").arg(unarchiveThreadIds.size())
            );
        }
    }

    QAction *selectedAction = menu.exec(m_treeView->viewport()->mapToGlobal(position));
    if (selectedAction == nullptr) {
        return;
    }
    if (selectedAction == refreshAction) {
        emit refreshRequested();
        return;
    }
    if (selectedAction == resumeAction && selectedThreadIds.size() == 1) {
        emit resumeThreadRequested(selectedThreadIds.constFirst());
        return;
    }
    if (selectedAction == renameAction && selectedThreadIds.size() == 1) {
        emit renameThreadRequested(selectedThreadIds.constFirst());
        return;
    }
    if (selectedAction == closeAction && !closeThreadIds.isEmpty()) {
        emit closeThreadsRequested(closeThreadIds);
        return;
    }
    if (selectedAction == forkAction && selectedThreadIds.size() == 1) {
        emit forkThreadRequested(selectedThreadIds.constFirst());
        return;
    }
    if (selectedAction == archiveAction) {
        emit archiveThreadsRequested(archiveThreadIds);
        return;
    }
    if (selectedAction == unarchiveAction) {
        emit unarchiveThreadsRequested(unarchiveThreadIds);
    }
}

void ThreadListPane::showHeaderContextMenu(const QPoint &position) {
    if (m_treeView == nullptr || m_model == nullptr || m_treeView->header() == nullptr) {
        return;
    }

    auto *header = m_treeView->header();
    QMenu menu(this);
    QAction *refreshAction = menu.addAction(QStringLiteral("Refresh Thread List"));
    menu.addSeparator();

    for (int column = 0; column < m_model->columnCount(); ++column) {
        const QString title = m_model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
        if (title.isEmpty()) {
            continue;
        }

        QAction *action = menu.addAction(title);
        action->setCheckable(true);
        action->setChecked(!header->isSectionHidden(column));

        if (column == ThreadListModel::ThreadColumn) {
            action->setEnabled(false);
            continue;
        }

        connect(action, &QAction::toggled, this, [this, header, column](const bool visible) {
            header->setSectionHidden(column, !visible);
            if (visible && m_treeView != nullptr) {
                m_treeView->resizeColumnToContents(column);
            }
        });
    }

    QAction *selectedAction = menu.exec(header->viewport()->mapToGlobal(position));
    if (selectedAction == refreshAction) {
        emit refreshRequested();
    }
}

}  // namespace qodex::ui
