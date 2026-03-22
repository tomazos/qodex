CREATE TABLE api_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    session_id TEXT,
    direction TEXT NOT NULL,
    message_kind TEXT NOT NULL,
    method TEXT,
    jsonrpc_id TEXT,
    correlation_id TEXT,
    thread_id TEXT,
    success INTEGER,
    latency_ms INTEGER,
    payload_json TEXT NOT NULL,
    summary_text TEXT NOT NULL DEFAULT ''
);

CREATE INDEX api_log_ts_utc_idx ON api_log(ts_utc);
CREATE INDEX api_log_method_idx ON api_log(method);
CREATE INDEX api_log_thread_id_idx ON api_log(thread_id);
CREATE INDEX api_log_jsonrpc_id_idx ON api_log(jsonrpc_id);

CREATE VIRTUAL TABLE api_log_fts USING fts5(
    method,
    summary_text,
    payload_text,
    content = 'api_log',
    content_rowid = 'id'
);

CREATE TRIGGER api_log_ai AFTER INSERT ON api_log BEGIN
    INSERT INTO api_log_fts(rowid, method, summary_text, payload_text)
    VALUES (new.id, coalesce(new.method, ''), coalesce(new.summary_text, ''), coalesce(new.payload_json, ''));
END;

CREATE TRIGGER api_log_ad AFTER DELETE ON api_log BEGIN
    INSERT INTO api_log_fts(api_log_fts, rowid, method, summary_text, payload_text)
    VALUES ('delete', old.id, coalesce(old.method, ''), coalesce(old.summary_text, ''), coalesce(old.payload_json, ''));
END;

CREATE TRIGGER api_log_au AFTER UPDATE ON api_log BEGIN
    INSERT INTO api_log_fts(api_log_fts, rowid, method, summary_text, payload_text)
    VALUES ('delete', old.id, coalesce(old.method, ''), coalesce(old.summary_text, ''), coalesce(old.payload_json, ''));
    INSERT INTO api_log_fts(rowid, method, summary_text, payload_text)
    VALUES (new.id, coalesce(new.method, ''), coalesce(new.summary_text, ''), coalesce(new.payload_json, ''));
END;
