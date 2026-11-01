CREATE TABLE IF NOT EXISTS class_extension (
    assignment_id INTEGER PRIMARY KEY REFERENCES assignment(id),
    new_deadline TEXT NOT NULL CHECK(new_deadline GLOB '????-??-??T??:??:??Z'),
    reason TEXT NOT NULL CHECK(length(trim(reason)) > 0),
    granted_by INTEGER NOT NULL REFERENCES app_user(id),
    granted_at TEXT NOT NULL
);
