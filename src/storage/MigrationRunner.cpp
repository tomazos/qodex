#include "storage/MigrationRunner.h"

#include <sqlite3.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QScopeGuard>

#include "storage/Migration.h"

namespace qodex::storage {

namespace {

QList<Migration> availableMigrations() {
    return {
        {
            1,
            QStringLiteral("0001_initial"),
            QStringLiteral(":/db/migrations/0001_initial.sql"),
        },
        {
            2,
            QStringLiteral("0002_api_log"),
            QStringLiteral(":/db/migrations/0002_api_log.sql"),
        },
    };
}

QString computeChecksum(const QByteArray &content) {
    return QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
}

bool execSql(sqlite3 *database, const QByteArray &sql, QString *errorMessage) {
    char *sqliteError = nullptr;
    const int rc = sqlite3_exec(database, sql.constData(), nullptr, nullptr, &sqliteError);
    if (rc == SQLITE_OK) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = sqliteError != nullptr ? QString::fromUtf8(sqliteError) : QString::fromUtf8(sqlite3_errmsg(database));
    }
    sqlite3_free(sqliteError);
    return false;
}

bool execSql(sqlite3 *database, const char *sql, QString *errorMessage) {
    return execSql(database, QByteArray(sql), errorMessage);
}

bool beginTransaction(sqlite3 *database, QString *errorMessage) {
    return execSql(database, "BEGIN IMMEDIATE TRANSACTION;", errorMessage);
}

void rollbackTransaction(sqlite3 *database) {
    QString ignored;
    execSql(database, "ROLLBACK;", &ignored);
}

bool commitTransaction(sqlite3 *database, QString *errorMessage) {
    return execSql(database, "COMMIT;", errorMessage);
}

bool ensureMigrationsTable(sqlite3 *database, QString *errorMessage) {
    return execSql(
        database,
        R"sql(
CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    checksum TEXT NOT NULL,
    applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
)sql",
        errorMessage
    );
}

bool loadMigrationSql(const Migration &migration, QByteArray *sqlContent, QString *errorMessage) {
    QFile sqlFile(migration.resourcePath);
    if (!sqlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to open migration resource %1").arg(migration.resourcePath);
        }
        return false;
    }

    *sqlContent = sqlFile.readAll();
    return true;
}

}  // namespace

bool MigrationRunner::migrate(const QString &databasePath, QString *errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    const QFileInfo databaseInfo(databasePath);
    const QDir parentDir = databaseInfo.dir();
    if (!parentDir.exists() && !QDir().mkpath(parentDir.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to create database directory %1").arg(parentDir.absolutePath());
        }
        return false;
    }

    sqlite3 *database = nullptr;
    const int openRc = sqlite3_open_v2(
        databasePath.toUtf8().constData(),
        &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
        nullptr
    );
    if (openRc != SQLITE_OK || database == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = database != nullptr ? QString::fromUtf8(sqlite3_errmsg(database))
                                                : QStringLiteral("Failed to open SQLite database.");
        }
        if (database != nullptr) {
            sqlite3_close(database);
        }
        return false;
    }

    const auto closeDatabase = qScopeGuard([&database] {
        sqlite3_close(database);
        database = nullptr;
    });

    sqlite3_busy_timeout(database, 1000);

    if (!execSql(database, "PRAGMA foreign_keys = ON;", errorMessage)) {
        return false;
    }

    if (!ensureMigrationsTable(database, errorMessage)) {
        return false;
    }

    const QList<Migration> migrations = availableMigrations();
    int previousVersion = 0;
    QHash<int, Migration> migrationsByVersion;
    for (const Migration &migration : migrations) {
        if (migration.version <= previousVersion) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Migrations are not strictly increasing.");
            }
            return false;
        }
        previousVersion = migration.version;
        migrationsByVersion.insert(migration.version, migration);
    }

    sqlite3_stmt *selectStatement = nullptr;
    const int prepareRc = sqlite3_prepare_v2(
        database,
        "SELECT version, name, checksum FROM schema_migrations ORDER BY version ASC;",
        -1,
        &selectStatement,
        nullptr
    );
    if (prepareRc != SQLITE_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = QString::fromUtf8(sqlite3_errmsg(database));
        }
        return false;
    }

    const auto finalizeStatement = qScopeGuard([&selectStatement] {
        sqlite3_finalize(selectStatement);
        selectStatement = nullptr;
    });

    QHash<int, QString> appliedChecksums;
    while (sqlite3_step(selectStatement) == SQLITE_ROW) {
        const int version = sqlite3_column_int(selectStatement, 0);
        const QString name = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(selectStatement, 1)));
        const QString checksum =
            QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(selectStatement, 2)));

        if (!migrationsByVersion.contains(version)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Database contains unknown migration version %1.").arg(version);
            }
            return false;
        }

        const Migration migration = migrationsByVersion.value(version);
        QByteArray sqlContent;
        if (!loadMigrationSql(migration, &sqlContent, errorMessage)) {
            return false;
        }

        const QString expectedChecksum = computeChecksum(sqlContent);
        if (name != migration.name || checksum != expectedChecksum) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Applied migration %1 does not match the bundled migration.").arg(version);
            }
            return false;
        }

        appliedChecksums.insert(version, checksum);
    }

    for (const Migration &migration : migrations) {
        if (appliedChecksums.contains(migration.version)) {
            continue;
        }

        QByteArray sqlContent;
        if (!loadMigrationSql(migration, &sqlContent, errorMessage)) {
            return false;
        }

        const QString checksum = computeChecksum(sqlContent);
        if (!beginTransaction(database, errorMessage)) {
            return false;
        }

        if (!execSql(database, sqlContent, errorMessage)) {
            rollbackTransaction(database);
            return false;
        }

        sqlite3_stmt *insertStatement = nullptr;
        const int insertPrepareRc = sqlite3_prepare_v2(
            database,
            "INSERT INTO schema_migrations(version, name, checksum) VALUES (?, ?, ?);",
            -1,
            &insertStatement,
            nullptr
        );
        if (insertPrepareRc != SQLITE_OK) {
            if (errorMessage != nullptr) {
                *errorMessage = QString::fromUtf8(sqlite3_errmsg(database));
            }
            rollbackTransaction(database);
            return false;
        }

        sqlite3_bind_int(insertStatement, 1, migration.version);
        sqlite3_bind_text(insertStatement, 2, migration.name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insertStatement, 3, checksum.toUtf8().constData(), -1, SQLITE_TRANSIENT);

        const int stepRc = sqlite3_step(insertStatement);
        sqlite3_finalize(insertStatement);
        if (stepRc != SQLITE_DONE) {
            if (errorMessage != nullptr) {
                *errorMessage = QString::fromUtf8(sqlite3_errmsg(database));
            }
            rollbackTransaction(database);
            return false;
        }

        if (!execSql(database, QByteArray("PRAGMA user_version = ") + QByteArray::number(migration.version) + ';', errorMessage)) {
            rollbackTransaction(database);
            return false;
        }

        if (!commitTransaction(database, errorMessage)) {
            rollbackTransaction(database);
            return false;
        }
    }

    return true;
}

int MigrationRunner::headVersion() {
    const QList<Migration> migrations = availableMigrations();
    return migrations.isEmpty() ? 0 : migrations.constLast().version;
}

}  // namespace qodex::storage
