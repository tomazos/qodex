# Database Design

## Goals

Qodex needs durable local persistence across runs for:

- window positions and dock/widget layout
- tree/header/view state
- user preferences
- searchable API logs
- Qodex-owned metadata about Codex objects such as threads

The design should be:

- simple
- cross-platform
- migration-safe
- testable
- suitable for gradual extension

## Storage Choice

Qodex will use a single SQLite database file as its primary persistent store.

Reasons:

- SQLite is a strong fit for structured local application state.
- Qodex is effectively single-instance, so multi-process contention is low.
- We want queryable/searchable logs, not just opaque files.
- One database gives us one migration system, one backup target, and one place to relate settings, UI state, metadata, and logs.

## Database Location

The database should live under Qt-standard per-user app storage.

Preferred path:

- `QStandardPaths::AppDataLocation/qodex.sqlite3`

`AppPaths` should be extended to expose at least:

- `homeDir`
- `appDataDir`
- `appStateDir`
- `databasePath`

## Initialization Order

Migrations should run during Qodex initialization, before any code starts using persistent state.

Expected startup order:

1. Create `QApplication`
2. Parse command-line options
3. Perform single-instance check
4. Open database
5. Run migrations to head
6. Construct services that depend on persistent state
7. Show and start the app

This ensures that all later components see the current schema.

## Migration Model

Qodex should use a forward-only ordered migration chain.

This is intentionally simpler than a Django-style graph. For Qodex, a linear sequence is enough.

Migration files should be named like:

- `0001_initial.sql`
- `0002_window_state.sql`
- `0003_api_log.sql`

Rules:

- migrations are immutable once shipped
- migrations are applied once
- old databases are upgraded by applying missing migrations in order
- a brand new database also runs the full migration chain from the beginning

There should be no rollback support in normal startup.

## Migration Bookkeeping

The database should contain a table that records applied migrations:

```sql
CREATE TABLE schema_migrations (
    version INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    checksum TEXT NOT NULL,
    applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

Why this table exists:

- keeps full migration history
- records human-readable migration names
- allows checksum verification to detect edited historical migrations

Optionally, the current head version may also be mirrored into `PRAGMA user_version`, but `schema_migrations` is the source of truth.

## How Migrations Are Written

Migrations should be written by hand as SQL files.

This is the right tradeoff for Qodex because:

- the schema will stay small and understandable
- SQLite DDL is simple
- we want explicit control over tables, indexes, FTS tables, and constraints
- an ORM-style autogenerator would be unnecessary complexity

Default approach:

- each migration is a manually written `.sql` file
- reviewed and versioned like normal code

If a future migration needs data reshaping that SQL alone does poorly, the system may allow an optional custom C++ migration step, but SQL files are the default and preferred form.

## Migration Execution

At startup, the migration runner should:

1. Open the SQLite file
2. Ensure `schema_migrations` exists
3. Load the compiled-in migration list
4. Compare applied migrations with available migrations
5. Apply any missing migrations in ascending order
6. Record each applied migration in `schema_migrations`

Each migration should run inside its own transaction.

If a migration fails:

- rollback the transaction
- stop startup
- show a clear error

The app should not continue with a partially migrated database.

## Representation in Code

Suggested files:

- `src/storage/DatabaseManager.h`
- `src/storage/DatabaseManager.cpp`
- `src/storage/Migration.h`
- `src/storage/MigrationRunner.h`
- `src/storage/MigrationRunner.cpp`
- `db/migrations/0001_initial.sql`
- `db/migrations/0002_window_state.sql`
- `db/migrations/0003_api_log.sql`

Suggested migration struct shape:

```cpp
struct Migration {
    int version;
    QString name;
    QString resourcePath;
    std::function<bool(sqlite3 *, QString *)> customStep;
};
```

Most migrations should use SQL only. `customStep` is for rare exceptional cases.

## SQL Resources

Migration SQL should be bundled with the application as resources.

That ensures:

- migrations are always present with the binary
- migration execution is independent of the current working directory
- release builds always carry the schema history they need

## Migration Engine API Choice

The migration runner itself should use the raw SQLite C API.

Reason:

- applying multi-statement SQL scripts is straightforward with SQLite directly
- transaction control is explicit
- schema inspection during tests is easy

Normal application data access can still use Qt SQL:

- `QSqlDatabase`
- `QSqlQuery`
- `QSqlError`

So the intended split is:

- migrations: raw SQLite C API
- normal application queries: Qt SQL via `QSQLITE`

## Initial Schema Families

The database should be organized around a few clear concerns.

### Meta

- `schema_migrations`

### Settings

- key/value preferences

Example:

- `settings(key PRIMARY KEY, value_json TEXT NOT NULL)`

### Window State

- window geometry
- dock layout state
- per-window identity

Example:

- `window_state(window_key PRIMARY KEY, geometry BLOB, layout BLOB, updated_at TEXT NOT NULL)`

### View State

- thread tree header state
- column visibility/order/width
- sorting state

Example:

- `view_state(view_key PRIMARY KEY, state_json TEXT NOT NULL, updated_at TEXT NOT NULL)`

### Thread Metadata

This stores Qodex-owned metadata keyed by Codex thread id.

Examples:

- custom labels
- notes
- tags
- pinned/starred state
- last viewed timestamp

Example:

- `thread_metadata(thread_id PRIMARY KEY, metadata_json TEXT NOT NULL, updated_at TEXT NOT NULL)`

### API Log

This should be append-only and queryable.

Suggested fields:

- `id`
- `ts_utc`
- `session_id`
- `direction`
- `message_kind`
- `method`
- `jsonrpc_id`
- `correlation_id`
- `thread_id`
- `success`
- `latency_ms`
- `payload_json`
- `summary_text`

### API Log Search

Use SQLite FTS5 for search over logs.

Example:

- `api_log`
- `api_log_fts`

This allows filtering by method, thread id, date, and direction while also supporting full-text search over summaries and payload-derived text.

## WAL

Write-Ahead Logging is a good option for later because Qodex will likely:

- write logs frequently
- read logs and state from the UI

However, WAL is not required up front. It is easy to enable later with:

```sql
PRAGMA journal_mode = WAL;
```

So WAL should not block the first persistence implementation.

## Testing Strategy

The migration system should be tested at four levels.

### 1. Fresh Database

- create an empty temporary database
- run migrations
- assert the database reaches head
- assert expected tables, columns, indexes, and FTS tables exist

### 2. Upgrade Existing Databases

- keep fixture databases for important historical versions
- migrate them to head
- assert the final schema matches a fresh head database
- assert important data survives and transforms correctly

### 3. No-op Re-run

- migrate a database to head
- run the migrator again
- assert it succeeds and makes no changes

### 4. Failure and Atomicity

- introduce a bad migration in a test-only chain
- assert migration fails clearly
- assert partial changes are rolled back
- assert the database remains at the previous version

Additional checks:

- duplicate migration version numbers fail
- migration order must be strictly increasing
- edited applied migrations cause checksum mismatch failure
- foreign keys are enabled
- expected indexes exist
- FTS search works for log entries

## Operational Rules

- never edit an already-shipped migration
- always add a new migration for schema changes
- keep migrations small and reviewable
- prefer explicit SQL over hidden automatic schema generation

## First Implementation Slice

The implementation should be done in two steps.

### Step 1: Migration Skeleton

- database path wiring in `AppPaths`
- `DatabaseManager`
- `MigrationRunner`
- `schema_migrations`
- one initial migration

### Step 2: First Real Persisted Features

- main window geometry and layout
- thread tree header state
- basic settings table

After that, add:

- thread metadata
- API log storage
- API log search

