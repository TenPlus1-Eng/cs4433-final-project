PRAGMA foreign_keys = ON; 
PRAGMA journal_mode = WAL; 
  
CREATE TABLE app_user ( 
    id             INTEGER PRIMARY KEY AUTOINCREMENT, 
    username       TEXT    NOT NULL UNIQUE COLLATE NOCASE 
                   CHECK (length(trim(username)) > 0), 
    full_name      TEXT    NOT NULL CHECK (length(trim(full_name)) > 0), 
    role           TEXT    NOT NULL 
                   CHECK (role IN 
('STUDENT','LECTURER','HEAD_OF_DEPARTMENT','ADMINISTRATOR')), 
    password_hash  TEXT    NOT NULL, 
    is_active      INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0,1)), 
    failed_logins  INTEGER NOT NULL DEFAULT 0 CHECK (failed_logins >= 0), 
    locked_until   TEXT, 
    last_login_at  TEXT, 
    created_at     TEXT    NOT NULL 
); 
  
CREATE TABLE department ( 
    id             INTEGER PRIMARY KEY AUTOINCREMENT, 
    name           TEXT    NOT NULL UNIQUE CHECK (length(trim(name)) > 0), 
    head_user_id   INTEGER UNIQUE REFERENCES app_user(id) 
); 
  
CREATE TABLE course ( 
    id             INTEGER PRIMARY KEY AUTOINCREMENT, 
    code           TEXT    NOT NULL UNIQUE CHECK (length(trim(code)) > 0), 
    title          TEXT    NOT NULL CHECK (length(trim(title)) > 0), 
    department_id  INTEGER NOT NULL REFERENCES department(id) 
); 
  
CREATE TABLE course_lecturer ( 
    course_id      INTEGER NOT NULL REFERENCES course(id), 
    lecturer_id    INTEGER NOT NULL REFERENCES app_user(id), 
    PRIMARY KEY (course_id, lecturer_id) 
) WITHOUT ROWID; 
  
CREATE TABLE enrolment ( 
    course_id      INTEGER NOT NULL REFERENCES course(id), 
    student_id     INTEGER NOT NULL REFERENCES app_user(id), 
enrolled_at    TEXT    NOT NULL, 
    PRIMARY KEY (course_id, student_id) 
) WITHOUT ROWID; 
  
CREATE TABLE assignment ( 
    id             INTEGER PRIMARY KEY AUTOINCREMENT, 
    course_id      INTEGER NOT NULL REFERENCES course(id), 
    title          TEXT    NOT NULL CHECK (length(trim(title)) > 0), 
    description    TEXT    NOT NULL DEFAULT '', 
    deadline       TEXT    NOT NULL CHECK (deadline GLOB '????-??-??T??:??:??Z'), 
    max_mark       REAL    NOT NULL CHECK (max_mark > 0), 
    created_by     INTEGER NOT NULL REFERENCES app_user(id), 
    created_at     TEXT    NOT NULL 
); 
  
CREATE TABLE extension ( 
    id             INTEGER PRIMARY KEY AUTOINCREMENT, 
    assignment_id  INTEGER NOT NULL REFERENCES assignment(id), 
    student_id     INTEGER NOT NULL REFERENCES app_user(id), 
    new_deadline   TEXT    NOT NULL CHECK (new_deadline GLOB '????-??-??T??:??:??Z'), 
    reason         TEXT    NOT NULL CHECK (length(trim(reason)) > 0), 
    granted_by     INTEGER NOT NULL REFERENCES app_user(id), 
    granted_at     TEXT    NOT NULL, 
    UNIQUE (assignment_id, student_id) 
); 
  
CREATE TABLE submission ( 
    id             INTEGER PRIMARY KEY AUTOINCREMENT, 
    assignment_id  INTEGER NOT NULL REFERENCES assignment(id), 
    student_id     INTEGER NOT NULL REFERENCES app_user(id), 
    submitted_at   TEXT    NOT NULL, 
    status         TEXT    NOT NULL 
                   CHECK (status IN ('SUBMITTED','LATE','MARKED','MODERATED','RELEASED')), 
    is_returned    INTEGER NOT NULL DEFAULT 0 CHECK (is_returned IN (0,1)), 
    raw_mark       REAL    CHECK (raw_mark >= 0), 
    final_mark     REAL    CHECK (final_mark >= 0), 
    days_late      INTEGER NOT NULL DEFAULT 0 CHECK (days_late >= 0), 
    feedback       TEXT, 
    return_reason  TEXT, 
    returned_by    INTEGER REFERENCES app_user(id), 
    marked_by      INTEGER REFERENCES app_user(id), 
    marked_at      TEXT, 
    moderated_by   INTEGER REFERENCES app_user(id), 
    moderated_at   TEXT, 
    released_by    INTEGER REFERENCES app_user(id), 
    released_at    TEXT, 
    version        INTEGER NOT NULL DEFAULT 1, 
    UNIQUE (assignment_id, student_id), 
    CHECK (is_returned = 0 OR status = 'MARKED'), 
    CHECK (status IN ('SUBMITTED','LATE') 
           OR (raw_mark IS NOT NULL AND final_mark IS NOT NULL 
               AND marked_by IS NOT NULL 
               AND feedback IS NOT NULL AND length(trim(feedback)) > 0)), 
    CHECK (status NOT IN ('MODERATED','RELEASED') OR moderated_by IS NOT NULL), 
    CHECK (status <> 'RELEASED' OR released_by IS NOT NULL) 
); 
  
CREATE TABLE submission_file ( 
    id             INTEGER PRIMARY KEY AUTOINCREMENT, 
    submission_id  INTEGER NOT NULL REFERENCES submission(id), 
    seq_no         INTEGER NOT NULL CHECK (seq_no >= 1), 
    original_name  TEXT    NOT NULL, 
    stored_path    TEXT    NOT NULL UNIQUE, 
    size_bytes     INTEGER NOT NULL CHECK (size_bytes BETWEEN 1 AND 5242880), 
    sha256         TEXT    NOT NULL CHECK (length(sha256) = 64), 
    uploaded_at    TEXT    NOT NULL, 
    UNIQUE (submission_id, seq_no) 
); 
  
CREATE TABLE mark_audit ( 
    id             INTEGER PRIMARY KEY AUTOINCREMENT, 
    submission_id  INTEGER NOT NULL REFERENCES submission(id), 
    changed_by     INTEGER NOT NULL REFERENCES app_user(id), 
    changed_at     TEXT    NOT NULL, 
change_type    TEXT    NOT NULL CHECK (change_type IN ('INITIAL','REMARK','HOD_ADJUST')), 
    old_raw        REAL, 
    new_raw        REAL    NOT NULL, 
    old_final      REAL, 
    new_final      REAL    NOT NULL, 
    reason         TEXT    NOT NULL CHECK (length(trim(reason)) > 0) 
); 
  
CREATE INDEX ix_course_department    ON course(department_id); 
CREATE INDEX ix_assignment_course    ON assignment(course_id); 
CREATE INDEX ix_enrolment_student    ON enrolment(student_id); 
CREATE INDEX ix_submission_student   ON submission(student_id); 
CREATE INDEX ix_submission_status    ON submission(assignment_id, status); 
CREATE INDEX ix_audit_submission     ON mark_audit(submission_id); 
  
CREATE TRIGGER trg_audit_no_update BEFORE UPDATE ON mark_audit 
BEGIN SELECT RAISE(ABORT, 'mark_audit is append-only'); END; 
  
CREATE TRIGGER trg_audit_no_delete BEFORE DELETE ON mark_audit 
BEGIN SELECT RAISE(ABORT, 'mark_audit is append-only'); END; 
  
CREATE TRIGGER trg_user_no_delete BEFORE DELETE ON app_user 
BEGIN SELECT RAISE(ABORT, 'accounts are deactivated, never deleted'); END; 
 

CREATE TABLE IF NOT EXISTS class_extension (
    assignment_id INTEGER PRIMARY KEY REFERENCES assignment(id),
    new_deadline TEXT NOT NULL CHECK(new_deadline GLOB '????-??-??T??:??:??Z'),
    reason TEXT NOT NULL CHECK(length(trim(reason)) > 0),
    granted_by INTEGER NOT NULL REFERENCES app_user(id),
    granted_at TEXT NOT NULL
);
