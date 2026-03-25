#include "ui/LoadedThreadsPane.h"

#include <QHeaderView>
#include <QTreeView>
#include <QVBoxLayout>

#include "ui/LoadedThreadsModel.h"

namespace qodex::ui {

LoadedThreadsPane::LoadedThreadsPane(LoadedThreadsModel *model, QWidget *parent)
    : QWidget(parent),
      m_model(model) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setWordWrap(false);
    m_treeView->setAllColumnsShowFocus(true);
    m_treeView->setRootIsDecorated(true);
    m_treeView->setItemsExpandable(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto *header = m_treeView->header();
    header->setStretchLastSection(false);
    header->setSectionsClickable(true);
    header->setSectionsMovable(true);
    header->setFirstSectionMovable(false);
    header->setSectionResizeMode(0, QHeaderView::Interactive);
    header->setSectionResizeMode(1, QHeaderView::Interactive);

    layout->addWidget(m_treeView, 1);

    if (m_model != nullptr) {
        connect(m_model, &LoadedThreadsModel::treeRebuilt, this, [this] {
            if (m_treeView != nullptr) {
                m_treeView->expandAll();
                resizeSnugColumns();
            }
        });
    }

    m_treeView->expandAll();
    resizeSnugColumns();
}

QByteArray LoadedThreadsPane::saveHeaderState() const {
    if (m_treeView == nullptr || m_treeView->header() == nullptr) {
        return {};
    }
    return m_treeView->header()->saveState();
}

bool LoadedThreadsPane::restoreHeaderState(const QByteArray &state) {
    if (m_treeView == nullptr || m_treeView->header() == nullptr || state.isEmpty()) {
        return false;
    }

    const bool restored = m_treeView->header()->restoreState(state);
    if (restored) {
        resizeSnugColumns();
    }
    return restored;
}

void LoadedThreadsPane::resizeSnugColumns() {
    if (m_treeView == nullptr || m_treeView->header() == nullptr) {
        return;
    }

    m_treeView->resizeColumnToContents(0);
    m_treeView->resizeColumnToContents(1);
    if (m_treeView->header()->sectionSize(0) < 260) {
        m_treeView->header()->resizeSection(0, 260);
    }
    if (m_treeView->header()->sectionSize(1) < 320) {
        m_treeView->header()->resizeSection(1, 320);
    }
}

}  // namespace qodex::ui
