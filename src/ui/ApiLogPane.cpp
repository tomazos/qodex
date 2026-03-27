#include "ui/ApiLogPane.h"

#include <QDataStream>
#include <QHeaderView>
#include <QIODevice>
#include <QMenu>
#include <QScrollBar>
#include <QTimer>
#include <QTableView>
#include <QVBoxLayout>
#include <optional>

#include "ui/ApiLogModel.h"

namespace qodex::ui {

namespace {

std::optional<qint64> apiLogIdForIndex(const QModelIndex &index) {
    if (!index.isValid()) {
        return std::nullopt;
    }

    const QVariant apiLogId = index.data(ApiLogModel::ApiLogIdRole);
    if (!apiLogId.isValid()) {
        return std::nullopt;
    }

    return apiLogId.toLongLong();
}

}  // namespace

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
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tableView->setTextElideMode(Qt::ElideRight);
    m_tableView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tableView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    auto *header = m_tableView->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionsClickable(false);
    header->setSortIndicatorShown(true);
    header->setSortIndicator(ApiLogModel::TimeColumn, Qt::AscendingOrder);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    header->setSectionsMovable(true);
    for (int column = ApiLogModel::TimeColumn; column < ApiLogModel::ColumnCount; ++column) {
        header->setSectionResizeMode(column, QHeaderView::Interactive);
    }

    applyDefaultColumnState();
    resizeDefaultColumns();
    m_model->sort(ApiLogModel::TimeColumn, Qt::AscendingOrder);

    layout->addWidget(m_tableView, 1);

    connect(m_tableView, &QWidget::customContextMenuRequested, this, &ApiLogPane::showContextMenu);
    connect(m_tableView->horizontalHeader(), &QWidget::customContextMenuRequested, this, &ApiLogPane::showHeaderContextMenu);
    connect(m_tableView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) { ensureVisibleRowsLoaded(); });
    connect(m_model, &QAbstractItemModel::rowsAboutToBeInserted, this, [this](const QModelIndex &, int, int) {
        if (m_tableView == nullptr || m_tableView->verticalScrollBar() == nullptr) {
            m_keepScrolledToBottomOnInsert = false;
            return;
        }

        QScrollBar *scrollBar = m_tableView->verticalScrollBar();
        m_keepScrolledToBottomOnInsert = scrollBar->value() >= scrollBar->maximum() - 1;
    });
    connect(m_model, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &, int, int) {
        QTimer::singleShot(0, this, [this] {
            ensureVisibleRowsLoaded();
            if (m_keepScrolledToBottomOnInsert && m_tableView != nullptr) {
                m_tableView->scrollToBottom();
            }
            m_keepScrolledToBottomOnInsert = false;
        });
    });
    connect(m_tableView, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        const std::optional<qint64> apiLogId = apiLogIdForIndex(index);
        if (!apiLogId.has_value()) {
            return;
        }

        emit inspectApiLogRequested(*apiLogId);
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
    return state;
}

bool ApiLogPane::restoreViewState(const QByteArray &state) {
    if (m_tableView == nullptr || m_tableView->horizontalHeader() == nullptr || state.isEmpty()) {
        return false;
    }

    QByteArray headerState;
    QDataStream stream(state);
    stream >> headerState;

    if (stream.status() != QDataStream::Ok) {
        return false;
    }

    const bool restored = m_tableView->horizontalHeader()->restoreState(headerState);
    if (!restored) {
        return false;
    }

    m_restoredViewState = true;
    m_tableView->horizontalHeader()->setSortIndicator(ApiLogModel::TimeColumn, Qt::AscendingOrder);
    if (m_model != nullptr) {
        m_model->sort(ApiLogModel::TimeColumn, Qt::AscendingOrder);
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

    QModelIndex index = m_tableView->indexAt(position);
    std::optional<qint64> clickedApiLogId;
    if (index.isValid()) {
        index = index.siblingAtColumn(ApiLogModel::TimeColumn);
        if (m_tableView->selectionModel() != nullptr) {
            m_tableView->selectionModel()->setCurrentIndex(
                index,
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows
            );
        }
        clickedApiLogId = apiLogIdForIndex(index);
    }

    QMenu menu(this);
    QAction *refreshAction = menu.addAction(QStringLiteral("Refresh API Log"));
    QAction *inspectAction = nullptr;
    if (clickedApiLogId.has_value()) {
        menu.addSeparator();
        inspectAction = menu.addAction(QStringLiteral("Inspect"));
    }
    const QAction *selectedAction = menu.exec(m_tableView->viewport()->mapToGlobal(position));
    if (selectedAction == refreshAction) {
        m_model->refresh();
    } else if (selectedAction == inspectAction && clickedApiLogId.has_value()) {
        emit inspectApiLogRequested(*clickedApiLogId);
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
