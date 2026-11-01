#pragma once
#include "lms/domain.hpp"
#include <filesystem>
#include <functional>
#include <mutex>
#include <sqlite3.h>

namespace lms {
// Queries are fixed in repository.cpp; callers supply values, never SQL.
enum class Query {
    UserId,
    UserName,
    Users,
    Departments,
    Courses,
    Department,
    Course,
    LecturerCourse,
    Enrolled,
    StudentCourses,
    Assignments,
    Assignment,
    Extension,
    SubmissionId,
    SubmissionPair,
    LecturerSubmissions,
    StudentResults,
    DepartmentSubmissions,
    Audit,
    Files,
    FilePaths,
    ClassExtension,
    LecturerCourses,
    DepartmentCourses,
    CourseStudents
};
enum class Command {
    CreateUser,
    UpdateUser,
    Password,
    LoginFailure,
    LoginSuccess,
    DepartmentCreate,
    DepartmentUpdate,
    CourseCreate,
    CourseUpdate,
    LecturerAdd,
    LecturerRemove,
    Enrol,
    Unenrol,
    AssignmentCreate,
    ExtensionSave,
    SubmissionCreate,
    SubmissionReplace,
    FileCreate,
    Mark,
    Approve,
    Return,
    Release,
    AuditCreate,
    ExtensionReclassify,
    ClassExtensionSave
};
struct IRepository {
    virtual ~IRepository() = default;
    virtual Json list(Query query, const Json &values = Json::array()) = 0;
    virtual long long execute(Command command, const Json &values) = 0;
    virtual void transaction(const std::function<void()> &work) = 0;
    Json one(Query query, const Json &values = Json::array());
};
class SqliteDatabase final : public IRepository {
    sqlite3 *db_{};
    std::recursive_mutex mutex_;
    void control(const char *sql);

  public:
    explicit SqliteDatabase(const std::string &path, const std::string &schema = "");
    ~SqliteDatabase() override;
    SqliteDatabase(const SqliteDatabase &) = delete;
    SqliteDatabase &operator=(const SqliteDatabase &) = delete;
    Json list(Query, const Json &values = Json::array()) override;
    long long execute(Command, const Json &) override;
    void transaction(const std::function<void()> &) override;
    void backup(const std::string &destination);
};
struct StoredFile {
    std::string path;
    std::string sha256;
    std::size_t size{};
};
struct IFileStore {
    virtual ~IFileStore() = default;
    virtual StoredFile save(const std::string &bytes) = 0;
    virtual void remove(const std::string &path) noexcept = 0;
    virtual std::string read(const std::string &path) = 0;
};
class DiskFileStore final : public IFileStore {
    std::filesystem::path root_;

  public:
    explicit DiskFileStore(const std::string &root);
    StoredFile save(const std::string &) override;
    void remove(const std::string &) noexcept override;
    std::string read(const std::string &) override;
    void sweep(const Json &referenced);
};
std::string randomToken();
} // namespace lms
