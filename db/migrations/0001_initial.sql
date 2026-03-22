CREATE TABLE settings (
    key TEXT PRIMARY KEY,
    value_json TEXT NOT NULL
);

CREATE TABLE window_state (
    window_key TEXT PRIMARY KEY,
    geometry BLOB NOT NULL,
    layout BLOB,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE view_state (
    view_key TEXT PRIMARY KEY,
    state_blob BLOB NOT NULL,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE thread_metadata (
    thread_id TEXT PRIMARY KEY,
    metadata_json TEXT NOT NULL,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
