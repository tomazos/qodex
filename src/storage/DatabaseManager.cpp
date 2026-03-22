#include "storage/DatabaseManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "storage/MigrationRunner.h"

namespace qodex::storage {

DatabaseManager::DatabaseManager(const QString &databasePath)
    : m_databasePath(databasePath),
      m_connectionName(
          QStringLiteral("qodex.Database.%1")
              .arg(QString::fromLatin1(QCryptographicHash::hash(databasePath.toUtf8(), QCryptographicHash::Sha256).toHex().first(12)))
      ),
      m_database(new QSqlDatabase()) {
    Q_ASSERT(m_database != nullptr);
}

DatabaseManager::~DatabaseManager() {
    close();
    delete m_database;
    m_database = nullptr;
}

bool DatabaseManager::open(QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (isOpen()) {
        return true;
    }

    const QFileInfo databaseInfo(m_databasePath);
    if (!databaseInfo.dir().exists() && !QDir().mkpath(databaseInfo.dir().absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to create database directory %1").arg(databaseInfo.dir().absolutePath());
        }
        return false;
    }

    if (!MigrationRunner::migrate(m_databasePath, errorMessage)) {
        return false;
    }

    return initializeConnection(errorMessage);
}

void DatabaseManager::close() {
    if (m_database == nullptr || !m_database->isValid()) {
        return;
    }

    const QString connectionName = m_connectionName;
    if (m_database->isOpen()) {
        m_database->close();
    }
    *m_database = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

bool DatabaseManager::isOpen() const {
    return m_database != nullptr && m_database->isValid() && m_database->isOpen();
}

QString DatabaseManager::databasePath() const {
    return m_databasePath;
}

QList<WindowStateRecord> DatabaseManager::loadWindowStates(QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    QList<WindowStateRecord> records;
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return records;
    }

    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral(
            "SELECT window_key, geometry, layout FROM window_state ORDER BY window_key ASC;"
        ))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return {};
    }

    while (query.next()) {
        records.append({
            query.value(0).toString(),
            query.value(1).toByteArray(),
            query.value(2).toByteArray(),
        });
    }
    return records;
}

std::optional<QByteArray> DatabaseManager::loadViewState(const QString &viewKey, QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return std::nullopt;
    }

    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("SELECT state_blob FROM view_state WHERE view_key = ?;"));
    query.addBindValue(viewKey);
    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return query.value(0).toByteArray();
}

std::optional<QString> DatabaseManager::loadSetting(const QString &key, QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return std::nullopt;
    }

    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("SELECT value_json FROM settings WHERE key = ?;"));
    query.addBindValue(key);
    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return query.value(0).toString();
}

bool DatabaseManager::replaceWindowStates(const QList<WindowStateRecord> &windowStates, QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return false;
    }

    if (!m_database->transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_database->lastError().text();
        }
        return false;
    }

    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("DELETE FROM window_state;"))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        m_database->rollback();
        return false;
    }

    query.prepare(QStringLiteral(
        "INSERT INTO window_state(window_key, geometry, layout, updated_at) "
        "VALUES (?, ?, ?, CURRENT_TIMESTAMP);"
    ));
    for (const WindowStateRecord &record : windowStates) {
        query.bindValue(0, record.windowKey);
        query.bindValue(1, record.geometry);
        query.bindValue(2, record.layout);
        if (!query.exec()) {
            if (errorMessage != nullptr) {
                *errorMessage = query.lastError().text();
            }
            m_database->rollback();
            return false;
        }
        query.finish();
    }

    if (!m_database->commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_database->lastError().text();
        }
        return false;
    }
    return true;
}

bool DatabaseManager::replaceViewStates(const QList<ViewStateRecord> &viewStates, QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return false;
    }

    if (!m_database->transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_database->lastError().text();
        }
        return false;
    }

    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("DELETE FROM view_state;"))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        m_database->rollback();
        return false;
    }

    query.prepare(QStringLiteral(
        "INSERT INTO view_state(view_key, state_blob, updated_at) "
        "VALUES (?, ?, CURRENT_TIMESTAMP);"
    ));
    for (const ViewStateRecord &record : viewStates) {
        query.bindValue(0, record.viewKey);
        query.bindValue(1, record.state);
        if (!query.exec()) {
            if (errorMessage != nullptr) {
                *errorMessage = query.lastError().text();
            }
            m_database->rollback();
            return false;
        }
        query.finish();
    }

    if (!m_database->commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_database->lastError().text();
        }
        return false;
    }
    return true;
}

bool DatabaseManager::saveSetting(const QString &key, const QString &valueJson, QString *errorMessage) {
    return executeStatement(
        QStringLiteral(
            "INSERT INTO settings(key, value_json) VALUES (?, ?) "
            "ON CONFLICT(key) DO UPDATE SET value_json = excluded.value_json;"
        ),
        {key, valueJson},
        errorMessage
    );
}

bool DatabaseManager::initializeConnection(QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    *m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database->setDatabaseName(m_databasePath);
    if (!m_database->open()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_database->lastError().text();
        }
        *m_database = QSqlDatabase();
        return false;
    }

    if (!executeStatement(QStringLiteral("PRAGMA foreign_keys = ON;"), {}, errorMessage)) {
        close();
        return false;
    }

    if (!executeStatement(QStringLiteral("PRAGMA busy_timeout = 1000;"), {}, errorMessage)) {
        close();
        return false;
    }

    return true;
}

bool DatabaseManager::executeStatement(const QString &sql, const QList<QVariant> &bindings, QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Database is not open.");
        }
        return false;
    }

    QSqlQuery query(*m_database);
    query.prepare(sql);
    for (const QVariant &binding : bindings) {
        query.addBindValue(binding);
    }
    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

}  // namespace qodex::storage
