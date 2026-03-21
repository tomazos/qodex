#include "ui/ThreadListPane.h"

#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QModelIndex>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/ThreadListModel.h"

namespace qodex::ui {

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

    m_listView = new QListView(this);
    m_listView->setModel(m_model);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setUniformItemSizes(true);

    layout->addLayout(headerLayout);
    layout->addWidget(m_listView, 1);

    connect(m_refreshButton, &QPushButton::clicked, this, &ThreadListPane::refreshRequested);
    connect(
        m_listView->selectionModel(),
        &QItemSelectionModel::currentChanged,
        this,
        [this](const QModelIndex &current) { emitSelectionForIndex(current); }
    );
    connect(
        m_listView,
        &QListView::activated,
        this,
        [this](const QModelIndex &index) { emitSelectionForIndex(index); }
    );
}

void ThreadListPane::setCurrentThreadId(const QString &threadId) {
    if (m_model == nullptr || m_listView == nullptr) {
        return;
    }

    if (threadId.isEmpty()) {
        m_listView->clearSelection();
        return;
    }

    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex index = m_model->index(row, 0);
        if (index.data(ThreadListModel::IdRole).toString() == threadId) {
            m_listView->setCurrentIndex(index);
            return;
        }
    }

    m_listView->clearSelection();
}

void ThreadListPane::emitSelectionForIndex(const QModelIndex &index) {
    if (!index.isValid()) {
        return;
    }
    emit threadSelected(index.data(ThreadListModel::IdRole).toString());
}

}  // namespace qodex::ui
