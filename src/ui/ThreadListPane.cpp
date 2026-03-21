#include "ui/ThreadListPane.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QModelIndex>
#include <QPushButton>
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

    auto *headerLayout = new QHBoxLayout();
    auto *label = new QLabel(QStringLiteral("Threads"), this);
    m_refreshButton = new QPushButton(QStringLiteral("Refresh"), this);

    headerLayout->addWidget(label);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_refreshButton);

    m_treeView = new ThreadTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setSortingEnabled(true);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setWordWrap(false);
    m_treeView->setAllColumnsShowFocus(true);
    m_treeView->setRootIsDecorated(true);
    m_treeView->setItemsExpandable(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->header()->setStretchLastSection(false);
    m_treeView->header()->setSectionsClickable(true);
    m_treeView->header()->setSortIndicatorShown(true);
    m_treeView->header()->setSectionResizeMode(ThreadListModel::ThreadColumn, QHeaderView::Stretch);
    m_treeView->header()->setSectionResizeMode(ThreadListModel::CreatedColumn, QHeaderView::ResizeToContents);
    m_treeView->header()->setSectionResizeMode(ThreadListModel::ModifiedColumn, QHeaderView::ResizeToContents);
    m_treeView->sortByColumn(ThreadListModel::ModifiedColumn, Qt::DescendingOrder);

    layout->addLayout(headerLayout);
    layout->addWidget(m_treeView, 1);

    connect(m_refreshButton, &QPushButton::clicked, this, &ThreadListPane::refreshRequested);
    connect(
        m_treeView,
        &QWidget::customContextMenuRequested,
        this,
        &ThreadListPane::showContextMenu
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
        }
    });

    m_treeView->expandAll();
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
    if (index.isValid()) {
        index = index.siblingAtColumn(ThreadListModel::ThreadColumn);
        const QString clickedThreadId = index.data(ThreadListModel::IdRole).toString();
        if (clickedThreadId.isEmpty()) {
            return;
        }

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

        QStringList selectedThreadIds = clickedWasInSnapshot
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

        QStringList archiveThreadIds;
        QStringList unarchiveThreadIds;
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

        if (archiveThreadIds.isEmpty() && unarchiveThreadIds.isEmpty()) {
            return;
        }

        QMenu menu(this);
        QAction *archiveAction = nullptr;
        QAction *unarchiveAction = nullptr;

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

        QAction *selectedAction = menu.exec(m_treeView->viewport()->mapToGlobal(position));
        if (selectedAction == archiveAction) {
            emit archiveThreadsRequested(archiveThreadIds);
            return;
        }
        if (selectedAction == unarchiveAction) {
            emit unarchiveThreadsRequested(unarchiveThreadIds);
        }
    }
}

}  // namespace qodex::ui
