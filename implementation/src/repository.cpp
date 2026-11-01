#include "lms/repository.hpp"
#include <fstream>
#include <map>
#include <set>
#include <sodium.h>

namespace lms {
namespace {
const std::map<Query, const char *> queries = {
    {Query::LecturerCourses, "SELECT c.* FROM course c JOIN course_lecturer l ON c.id=l.course_id "
                             "WHERE l.lecturer_id=? ORDER BY c.code"},
    {Query::DepartmentCourses, "SELECT c.* FROM course c JOIN department d ON c.department_id=d.id "
                               "WHERE d.head_user_id=? ORDER BY c.code"},
    {Query::CourseStudents,
     "SELECT u.id,u.username,u.full_name,u.role,u.is_active FROM app_user u JOIN enrolment e ON "
     "u.id=e.student_id WHERE e.course_id=? ORDER BY u.full_name"},
    {Query::UserId, "SELECT * FROM app_user WHERE id=?"},
    {Query::UserName, "SELECT * FROM app_user WHERE username=?"},
    {Query::Users, "SELECT id,username,full_name,role,is_active FROM app_user ORDER BY id"},
    {Query::Departments, "SELECT * FROM department ORDER BY id"},
    {Query::Courses, "SELECT * FROM course ORDER BY id"},
    {Query::Department, "SELECT * FROM department WHERE id=?"},
    {Query::Course, "SELECT * FROM course WHERE id=?"},
    {Query::LecturerCourse, "SELECT * FROM course_lecturer WHERE course_id=? AND lecturer_id=?"},
    {Query::Enrolled, "SELECT * FROM enrolment WHERE course_id=? AND student_id=?"},
    {Query::StudentCourses,
     "SELECT c.* FROM course c JOIN enrolment e ON c.id=e.course_id WHERE e.student_id=?"},
    {Query::Assignments, "SELECT * FROM assignment WHERE course_id=?"},
    {Query::Assignment, "SELECT * FROM assignment WHERE id=?"},
    {Query::Extension, "SELECT * FROM extension WHERE assignment_id=? AND student_id=?"},
    {Query::SubmissionId, "SELECT * FROM submission WHERE id=?"},
    {Query::SubmissionPair, "SELECT * FROM submission WHERE assignment_id=? AND student_id=?"},
    {Query::LecturerSubmissions, "SELECT s.* FROM submission s WHERE s.assignment_id=?"},
    {Query::StudentResults, "SELECT * FROM submission WHERE student_id=?"},
    {Query::DepartmentSubmissions,
     "SELECT s.* FROM submission s JOIN assignment a ON s.assignment_id=a.id JOIN course c ON "
     "a.course_id=c.id JOIN department d ON c.department_id=d.id WHERE d.head_user_id=?"},
    {Query::Audit, "SELECT * FROM mark_audit WHERE submission_id=? ORDER BY id"},
    {Query::Files, "SELECT * FROM submission_file WHERE submission_id=? ORDER BY seq_no"},
    {Query::FilePaths, "SELECT stored_path FROM submission_file"},
    {Query::ClassExtension, "SELECT * FROM class_extension WHERE assignment_id=?"}};
const std::map<Command, const char *> commands = {
    {Command::ClassExtensionSave,
     "INSERT INTO class_extension(assignment_id,new_deadline,reason,granted_by,granted_at) "
     "VALUES(?,?,?,?,?) ON CONFLICT(assignment_id) DO UPDATE SET "
     "new_deadline=excluded.new_deadline,reason=excluded.reason,granted_by=excluded.granted_by,"
     "granted_at=excluded.granted_at"},
    {Command::CreateUser,
     "INSERT INTO app_user(username,full_name,role,password_hash,created_at) VALUES(?,?,?,?,?)"},
    {Command::UpdateUser, "UPDATE app_user SET full_name=?,is_active=? WHERE id=?"},
    {Command::Password,
     "UPDATE app_user SET password_hash=?,failed_logins=0,locked_until=NULL WHERE id=?"},
    {Command::LoginFailure, "UPDATE app_user SET failed_logins=?,locked_until=? WHERE id=?"},
    {Command::LoginSuccess,
     "UPDATE app_user SET failed_logins=0,locked_until=NULL,last_login_at=? WHERE id=?"},
    {Command::DepartmentCreate, "INSERT INTO department(name,head_user_id) VALUES(?,?)"},
    {Command::DepartmentUpdate, "UPDATE department SET name=?,head_user_id=? WHERE id=?"},
    {Command::CourseCreate, "INSERT INTO course(code,title,department_id) VALUES(?,?,?)"},
    {Command::CourseUpdate, "UPDATE course SET code=?,title=?,department_id=? WHERE id=?"},
    {Command::LecturerAdd,
     "INSERT OR IGNORE INTO course_lecturer(course_id,lecturer_id) VALUES(?,?)"},
    {Command::LecturerRemove, "DELETE FROM course_lecturer WHERE course_id=? AND lecturer_id=?"},
    {Command::Enrol,
     "INSERT OR IGNORE INTO enrolment(course_id,student_id,enrolled_at) VALUES(?,?,?)"},
    {Command::Unenrol, "DELETE FROM enrolment WHERE course_id=? AND student_id=?"},
    {Command::AssignmentCreate,
     "INSERT INTO assignment(course_id,title,description,deadline,max_mark,created_by,created_at) "
     "VALUES(?,?,?,?,?,?,?)"},
    {Command::ExtensionSave,
     "INSERT INTO extension(assignment_id,student_id,new_deadline,reason,granted_by,granted_at) "
     "VALUES(?,?,?,?,?,?) ON CONFLICT(assignment_id,student_id) DO UPDATE SET "
     "new_deadline=excluded.new_deadline,reason=excluded.reason,granted_by=excluded.granted_by,"
     "granted_at=excluded.granted_at"},
    {Command::SubmissionCreate,
     "INSERT INTO submission(assignment_id,student_id,submitted_at,status) VALUES(?,?,?,?)"},
    {Command::SubmissionReplace,
     "UPDATE submission SET submitted_at=?,status=?,version=version+1 WHERE id=? AND version=?"},
    {Command::FileCreate, "INSERT INTO "
                          "submission_file(submission_id,seq_no,original_name,stored_path,size_"
                          "bytes,sha256,uploaded_at) VALUES(?,?,?,?,?,?,?)"},
    {Command::Mark,
     "UPDATE submission SET "
     "raw_mark=?,final_mark=?,days_late=?,feedback=?,marked_by=?,marked_at=?,status='MARKED',is_"
     "returned=0,return_reason=NULL,returned_by=NULL,version=version+1 WHERE id=? AND version=?"},
    {Command::Approve, "UPDATE submission SET "
                       "raw_mark=?,final_mark=?,days_late=?,status='MODERATED',moderated_by=?,"
                       "moderated_at=?,version=version+1 WHERE id=? AND version=?"},
    {Command::Return,
     "UPDATE submission SET is_returned=1,return_reason=?,returned_by=?,version=version+1 WHERE "
     "id=? AND version=?"},
    {Command::Release,
     "UPDATE submission SET status='RELEASED',released_by=?,released_at=?,version=version+1 WHERE "
     "id=? AND version=?"},
    {Command::AuditCreate, "INSERT INTO "
                           "mark_audit(submission_id,changed_by,changed_at,change_type,old_raw,new_"
                           "raw,old_final,new_final,reason) VALUES(?,?,?,?,?,?,?,?,?)"},
    {Command::ExtensionReclassify,
     "UPDATE submission SET status=?,version=version+1 WHERE id=? AND version=?"}};
class Statement {
  public:
    sqlite3_stmt *p{};
    Statement(sqlite3 *db, const char *sql, Json values) {
        if (!values.is_array())
            values = Json::array({values});
        if (sqlite3_prepare_v2(db, sql, -1, &p, nullptr) != SQLITE_OK)
            throw DatabaseError();
        if (sqlite3_bind_parameter_count(p) != static_cast<int>(values.size())) {
            sqlite3_finalize(p);
            p = nullptr;
            throw DatabaseError();
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            int n = static_cast<int>(i) + 1;
            const auto &v = values[i];
            int rc = SQLITE_ERROR;
            if (v.is_null())
                rc = sqlite3_bind_null(p, n);
            else if (v.is_number_integer())
                rc = sqlite3_bind_int64(p, n, v.get<long long>());
            else if (v.is_number())
                rc = sqlite3_bind_double(p, n, v.get<double>());
            else if (v.is_boolean())
                rc = sqlite3_bind_int(p, n, v.get<bool>() ? 1 : 0);
            else if (v.is_string()) {
                const auto &s = v.get_ref<const std::string &>();
                rc =
                    sqlite3_bind_text(p, n, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
            }
            if (rc != SQLITE_OK) {
                sqlite3_finalize(p);
                p = nullptr;
                throw DatabaseError();
            }
        }
    }
    ~Statement() {
        sqlite3_finalize(p);
    }
};
} // namespace
Json IRepository::one(Query q, const Json &values) {
    auto rows = list(q, values);
    if (rows.empty())
        throw NotFoundError();
    return rows[0];
}
SqliteDatabase::SqliteDatabase(const std::string &path, const std::string &schema) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw DatabaseError();
    }
    sqlite3_busy_timeout(db_, 5000);
    try {
        control("PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;");
        if (!schema.empty())
            control(schema.c_str());
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}
SqliteDatabase::~SqliteDatabase() {
    sqlite3_close(db_);
}
void SqliteDatabase::control(const char *sql) {
    if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw DatabaseError();
}
Json SqliteDatabase::list(Query q, const Json &values) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    Statement s(db_, queries.at(q), values);
    Json result = Json::array();
    int rc;
    while ((rc = sqlite3_step(s.p)) == SQLITE_ROW) {
        Json row = Json::object();
        for (int c = 0; c < sqlite3_column_count(s.p); ++c) {
            auto name = sqlite3_column_name(s.p, c);
            switch (sqlite3_column_type(s.p, c)) {
            case SQLITE_NULL:
                row[name] = nullptr;
                break;
            case SQLITE_INTEGER:
                row[name] = sqlite3_column_int64(s.p, c);
                break;
            case SQLITE_FLOAT:
                row[name] = sqlite3_column_double(s.p, c);
                break;
            default:
                row[name] = reinterpret_cast<const char *>(sqlite3_column_text(s.p, c));
            }
        }
        result.push_back(row);
    }
    if (rc != SQLITE_DONE)
        throw DatabaseError();
    return result;
}
long long SqliteDatabase::execute(Command c, const Json &values) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    Statement s(db_, commands.at(c), values);
    int result = sqlite3_step(s.p);
    if (result == SQLITE_CONSTRAINT)
        throw ConflictError("Data conflicts with an existing record or constraint");
    if (result != SQLITE_DONE)
        throw DatabaseError();
    if ((c == Command::Mark || c == Command::Approve || c == Command::Return ||
         c == Command::Release || c == Command::SubmissionReplace ||
         c == Command::ExtensionReclassify) &&
        sqlite3_changes(db_) != 1)
        throw ConflictError();
    return sqlite3_last_insert_rowid(db_);
}
void SqliteDatabase::transaction(const std::function<void()> &work) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    control("BEGIN IMMEDIATE");
    try {
        work();
        control("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void SqliteDatabase::backup(const std::string &destination) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    sqlite3 *target{};
    if (sqlite3_open(destination.c_str(), &target) != SQLITE_OK) {
        sqlite3_close(target);
        throw DatabaseError();
    }
    auto handle = sqlite3_backup_init(target, "main", db_, "main");
    int rc = SQLITE_ERROR;
    if (handle) {
        rc = sqlite3_backup_step(handle, -1);
        sqlite3_backup_finish(handle);
    }
    sqlite3_close(target);
    if (rc != SQLITE_DONE)
        throw DatabaseError();
}
std::string randomToken() {
    unsigned char bytes[32];
    randombytes_buf(bytes, sizeof bytes);
    char hex[65];
    sodium_bin2hex(hex, sizeof hex, bytes, sizeof bytes);
    return hex;
}
DiskFileStore::DiskFileStore(const std::string &root) : root_(std::filesystem::absolute(root)) {
    std::filesystem::create_directories(root_);
}
StoredFile DiskFileStore::save(const std::string &bytes) {
    if (bytes.empty() || bytes.size() > 5242880)
        throw ValidationError("File must contain 1 to 5242880 bytes");
    auto name = randomToken();
    auto path = root_ / name;
    try {
        std::ofstream out(path, std::ios::binary);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
    } catch (...) {
        remove(name);
        throw DatabaseError();
    }
    unsigned char digest[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(digest, reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
    char hex[65];
    sodium_bin2hex(hex, sizeof hex, digest, sizeof digest);
    return {name, hex, bytes.size()};
}
void DiskFileStore::remove(const std::string &name) noexcept {
    if (name.size() != 64 || name.find_first_not_of("0123456789abcdef") != std::string::npos)
        return;
    std::error_code error;
    std::filesystem::remove(root_ / name, error);
}
std::string DiskFileStore::read(const std::string &name) {
    if (name.size() != 64 || name.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw NotFoundError();
    std::ifstream in(root_ / name, std::ios::binary);
    if (!in)
        throw NotFoundError();
    return std::string(std::istreambuf_iterator<char>(in), {});
}
void DiskFileStore::sweep(const Json &referenced) {
    std::set<std::string> names;
    for (const auto &r : referenced)
        names.insert(r.at("stored_path"));
    for (const auto &entry : std::filesystem::directory_iterator(root_))
        if (entry.is_regular_file() && !names.count(entry.path().filename().string()))
            remove(entry.path().filename().string());
}
} // namespace lms
