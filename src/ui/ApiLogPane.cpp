#include "ui/ApiLogPane.h"

#include <QDataStream>
#include <QHeaderView>
#include <QIODevice>
#include <QMenu>
#include <QScrollBar>
#include <QTimer>
#include <QTableView>
#include <QVBoxLayout>

#include "ui/ApiLogModel.h"

namespace qodex::ui {

ApiLogPane::ApiLogPane(ApiLogModel *model, QWidget *parent)
    : QWidget(parent),
      m_model(model) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tableView = new QTableView(this);
    m_tableView->setModel(m_model);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setWordWrap(false);
    m_tableView->setSortingEnabled(true);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tableView->setTextElideMode(Qt::ElideRight);
    m_tableView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tableView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    auto *header = m_tableView->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    header->setSectionsMovable(true);
    for (int column = ApiLogModel::TimeColumn; column < ApiLogModel::ColumnCount; ++column) {
        header->setSectionResizeMode(column, QHeaderView::Interactive);
    }

    applyDefaultColumnState();
    resizeDefaultColumns();
    m_tableView->sortByColumn(ApiLogModel::TimeColumn, Qt::DescendingOrder);

    layout->addWidget(m_tableView, 1);

    connect(m_tableView, &QWidget::customContextMenuRequested, this, &ApiLogPane::showContextMenu);
    connect(m_tableView->horizontalHeader(), &QWidget::customContextMenuRequested, this, &ApiLogPane::showHeaderContextMenu);
    connect(m_tableView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) { ensureVisibleRowsLoaded(); });
    connect(m_tableView, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!index.isValid()) {
            return;
        }

        const QVariant apiLogId = index.data(ApiLogModel::ApiLogIdRole);
        if (!apiLogId.isValid()) {
            return;
        }

        emit inspectApiLogRequested(apiLogId.toLongLong());
    });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        if (!m_restoredViewState) {
            applyDefaultColumnState();
            resizeDefaultColumns();
        }
        QTimer::singleShot(0, this, &ApiLogPane::ensureVisibleRowsLoaded);
    });
    QTimer::singleShot(0, this, &ApiLogPane::ensureVisibleRowsLoaded);
}

QByteArray ApiLogPane::saveViewState() const {
    if (m_tableView == nullptr || m_tableView->horizontalHeader() == nullptr) {
        return {};
    }

    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream << m_tableView->horizontalHeader()->saveState();
    stream << qint32(m_tableView->horizontalHeader()->sortIndicatorSection());
    stream << qint32(m_tableView->horizontalHeader()->sortIndicatorOrder());
    return state;
}

bool ApiLogPane::restoreViewState(const QByteArray &state) {
    if (m_tableView == nullptr || m_tableView->horizontalHeader() == nullptr || state.isEmpty()) {
        return false;
    }

    QByteArray headerState;
    qint32 sortSection = ApiLogModel::TimeColumn;
    qint32 sortOrder = Qt::DescendingOrder;

    QDataStream stream(state);
    stream >> headerState;
    stream >> sortSection;
    stream >> sortOrder;

    if (stream.status() != QDataStream::Ok) {
        return false;
    }

    const bool restored = m_tableView->horizontalHeader()->restoreState(headerState);
    if (!restored) {
        return false;
    }

    m_restoredViewState = true;
    if (sortSection >= 0 && sortSection < ApiLogModel::ColumnCount) {
        m_tableView->sortByColumn(sortSection, static_cast<Qt::SortOrder>(sortOrder));
    }
    return true;
}

void ApiLogPane::ensureVisibleRowsLoaded() {
    if (m_model == nullptr || m_tableView == nullptr || m_model->rowCount() <= 0) {
        return;
    }

    int firstVisibleRow = m_tableView->rowAt(0);
    if (firstVisibleRow < 0) {
        firstVisibleRow = 0;
    }

    int lastVisibleRow = m_tableView->rowAt(m_tableView->viewport()->height() - 1);
    if (lastVisibleRow < 0) {
        lastVisibleRow = std::min(firstVisibleRow + 30, m_model->rowCount() - 1);
    }

    m_model->ensureRowsCached(firstVisibleRow, lastVisibleRow);
}

void ApiLogPane::applyDefaultColumnState() {
    if (m_tableView == nullptr || m_tableView->horizontalHeader() == nullptr) {
        return;
    }

    auto *header = m_tableView->horizontalHeader();
    for (const int column : {
             ApiLogModel::ThreadIdColumn,
             ApiLogModel::JsonRpcIdColumn,
             ApiLogModel::CorrelationIdColumn,
             ApiLogModel::SessionIdColumn,
             ApiLogModel::PayloadColumn,
         }) {
        header->setSectionHidden(column, true);
    }
}

void ApiLogPane::resizeDefaultColumns() {
    if (m_tableView == nullptr || m_tableView->horizontalHeader() == nullptr) {
        return;
    }

    m_tableView->resizeColumnToContents(ApiLogModel::TimeColumn);
    m_tableView->resizeColumnToContents(ApiLogModel::DirectionColumn);
    m_tableView->resizeColumnToContents(ApiLogModel::KindColumn);
    m_tableView->resizeColumnToContents(ApiLogModel::MethodColumn);
    m_tableView->resizeColumnToContents(ApiLogModel::SuccessColumn);
    m_tableView->resizeColumnToContents(ApiLogModel::LatencyColumn);
    m_tableView->horizontalHeader()->resizeSection(ApiLogModel::SummaryColumn, 440);
    m_tableView->horizontalHeader()->resizeSection(ApiLogModel::ThreadIdColumn, 220);
    m_tableView->horizontalHeader()->resizeSection(ApiLogModel::JsonRpcIdColumn, 180);
    m_tableView->horizontalHeader()->resizeSection(ApiLogModel::CorrelationIdColumn, 180);
    m_tableView->horizontalHeader()->resizeSection(ApiLogModel::SessionIdColumn, 240);
    m_tableView->horizontalHeader()->resizeSection(ApiLogModel::PayloadColumn, 340);
}

void ApiLogPane::showContextMenu(const QPoint &position) {
    if (m_model == nullptr || m_tableView == nullptr) {
        return;
    }

    QMenu menu(this);
    QAction *refreshAction = menu.addAction(QStringLiteral("Refresh API Log"));
    const QAction *selectedAction = menu.exec(m_tableView->viewport()->mapToGlobal(position));
    if (selectedAction == refreshAction) {
        m_model->refresh();
    }
}

void ApiLogPane::showHeaderContextMenu(const QPoint &position) {
    if (m_tableView == nullptr || m_model == nullptr || m_tableView->horizontalHeader() == nullptr) {
        return;
    }

    auto *header = m_tableView->horizontalHeader();
    QMenu menu(this);
    QAction *refreshAction = menu.addAction(QStringLiteral("Refresh API Log"));
    menu.addSeparator();

    for (int column = 0; column < m_model->columnCount(); ++column) {
        const QString title = m_model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
        if (title.isEmpty()) {
            continue;
        }

        QAction *action = menu.addAction(title);
        action->setCheckable(true);
        action->setChecked(!header->isSectionHidden(column));

        connect(action, &QAction::toggled, this, [this, header, column](const bool visible) {
            header->setSectionHidden(column, !visible);
            if (visible) {
                resizeDefaultColumns();
            }
        });
    }

    const QAction *selectedAction = menu.exec(header->viewport()->mapToGlobal(position));
    if (selectedAction == refreshAction) {
        m_model->refresh();
    }
}

}  // namespace qodex::ui
