#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariant>

#include <optional>

class QSqlDatabase;

namespace qodex::storage {

struct WindowStateRecord {
    QString windowKey;
    QByteArray geometry;
    QByteArray layout;
};

struct ViewStateRecord {
    QString viewKey;
    QByteArray state;
};

class DatabaseManager final {
public:
    explicit DatabaseManager(const QString &databasePath);
    ~DatabaseManager();

    [[nodiscard]] bool open(QString *errorMessage);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString databasePath() const;

    [[nodiscard]] QList<WindowStateRecord> loadWindowStates(QString *errorMessage) const;
    [[nodiscard]] std::optional<QByteArray> loadViewState(const QString &viewKey, QString *errorMessage) const;
    [[nodiscard]] std::optional<QString> loadSetting(const QString &key, QString *errorMessage) const;

    [[nodiscard]] bool replaceWindowStates(const QList<WindowStateRecord> &windowStates, QString *errorMessage);
    [[nodiscard]] bool replaceViewStates(const QList<ViewStateRecord> &viewStates, QString *errorMessage);
    [[nodiscard]] bool saveSetting(const QString &key, const QString &valueJson, QString *errorMessage);

private:
    [[nodiscard]] bool initializeConnection(QString *errorMessage);
    [[nodiscard]] bool executeStatement(const QString &sql, const QList<QVariant> &bindings, QString *errorMessage) const;

    QString m_databasePath;
    QString m_connectionName;
    QSqlDatabase *m_database = nullptr;
};

}  // namespace qodex::storage
