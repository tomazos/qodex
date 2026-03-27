CREATE TABLE instruction_document (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    content TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TRIGGER instruction_document_touch_updated_at
AFTER UPDATE OF name, content ON instruction_document
FOR EACH ROW
BEGIN
    UPDATE instruction_document
    SET updated_at = CURRENT_TIMESTAMP
    WHERE id = NEW.id;
END;
