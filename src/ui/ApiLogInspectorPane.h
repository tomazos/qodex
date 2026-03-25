#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QSplitter;
class QTreeWidget;

namespace qodex::storage {
class DatabaseManager;
}

namespace qodex::ui {

class ApiLogInspectorPane final : public QWidget {
    Q_OBJECT

public:
    explicit ApiLogInspectorPane(qodex::storage::DatabaseManager *databaseManager, QWidget *parent = nullptr);

    void inspectApiLog(qint64 apiLogId);
    [[nodiscard]] QByteArray saveViewState() const;
    bool restoreViewState(const QByteArray &state);

private:
    void clearInspector(const QString &message);

    qodex::storage::DatabaseManager *m_databaseManager = nullptr;
    qint64 m_currentApiLogId = 0;
    QLabel *m_titleLabel = nullptr;
    QTreeWidget *m_fieldsTree = nullptr;
    QPlainTextEdit *m_payloadEdit = nullptr;
    QSplitter *m_splitter = nullptr;
};

}  // namespace qodex::ui
