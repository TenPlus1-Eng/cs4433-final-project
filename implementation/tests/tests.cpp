#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "lms/services.hpp"
#include <fstream>
#include <limits>
#include <sodium.h>
using namespace lms;
struct FixedClock : IClock {
    std::string value = "2026-11-01T10:00:00Z";
    std::string now() const override {
        return value;
    }
};
struct MemoryFiles : IFileStore {
    std::map<std::string, std::string> data;
    StoredFile save(const std::string &bytes) override {
        if (bytes.empty() || bytes.size() > 5242880)
            throw ValidationError("Bad file size");
        auto path = randomToken();
        data[path] = bytes;
        return {path, std::string(64, 'a'), bytes.size()};
    }
    void remove(const std::string &p) noexcept override {
        data.erase(p);
    }
    std::string read(const std::string &p) override {
        return data.at(p);
    }
};
static std::string schema() {
    std::ifstream in("schema.sql");
    return {std::istreambuf_iterator<char>(in), {}};
}
struct Fixture {
    FixedClock clock;
    SqliteDatabase repo{":memory:", schema()};
    PasswordHasher hash;
    SessionManager sessions{clock};
    AuthService auth{repo, clock, hash, sessions};
    AdminService admin{repo, clock, hash, sessions};
    AssignmentService assignments{repo, clock};
    ExtensionService extensions{repo, clock};
    MemoryFiles files;
    SubmissionService submissions{repo, clock, files};
    TieredDailyPenaltyPolicy penalty;
    MarkingService marking{repo, clock, penalty};
    ModerationService moderation{repo, clock, penalty};
    Administrator root{1};
    Student student{2};
    Lecturer lecturer{3};
    HeadOfDepartment hod{4};
    int assignment = 0;
    Fixture() {
        REQUIRE(sodium_init() >= 0);
        for (auto r : {"ADMINISTRATOR", "STUDENT", "LECTURER", "HEAD_OF_DEPARTMENT"})
            repo.execute(Command::CreateUser, {r, r, r, "not-a-real-hash", clock.now()});
        admin.saveDepartment(root, 0, {{"name", "Science"}, {"head_user_id", 4}});
        admin.saveCourse(root, 0, {{"code", "CS4433"}, {"title", "SE"}, {"department_id", 1}});
        admin.assignLecturer(root, 1, 3, true);
        admin.enrol(root, 1, 2, true);
        assignment = assignments.create(lecturer, 1,
                                        {{"title", "Project"},
                                         {"description", "SE"},
                                         {"deadline", clock.now()},
                                         {"max_mark", 50}});
    }
    int submit() {
        return submissions.submit(student, assignment, "work.txt", "answer").at("id");
    }
    Json row(int id) {
        return repo.one(Query::SubmissionId, {id});
    }
    int ver(int id) {
        return row(id).at("version");
    }
    void mark(int id, double raw = 40) {
        marking.markSubmission(lecturer, id, ver(id), raw, "Good work", "Recheck");
    }
};
TEST_CASE("T01 penalty boundaries and equivalence partitions", "[unit]") {
    TieredDailyPenaltyPolicy p;
    for (auto seconds :
         {-1LL, 0LL, 1LL, 86400LL, 86401LL, 172800LL, 172801LL, 259200LL, 259201LL}) {
        int d = TieredDailyPenaltyPolicy::daysLate(utc(epoch("2026-11-01T10:00:00Z") + seconds),
                                                   "2026-11-01T10:00:00Z");
        int expected = seconds <= 0 ? 0 : static_cast<int>((seconds + 86399) / 86400);
        REQUIRE(d == expected);
        REQUIRE(p.calculate(40, 50, d).finalMark == Approx(d > 3 ? 0 : 40 - d * 5));
    }
    REQUIRE(p.calculate(2, 50, 1).finalMark == 0);
    REQUIRE(p.calculate(20, 25, 1).finalMark == Approx(17.5));
    REQUIRE(p.calculate(0, 50, 0).finalMark == 0);
    REQUIRE_THROWS_AS(p.calculate(-1, 50, 0), ValidationError);
    REQUIRE_THROWS_AS(p.calculate(51, 50, 0), ValidationError);
    REQUIRE_THROWS_AS(p.calculate(1, 0, 0), ValidationError);
    REQUIRE_THROWS_AS(p.calculate(1, 50, -1), ValidationError);
    REQUIRE_THROWS_AS(p.calculate(std::numeric_limits<double>::quiet_NaN(), 50, 0),
                      ValidationError);
}
TEST_CASE("T02 UTC validation and lifecycle", "[unit]") {
    for (auto time :
         {"2026-02-30T10:00:00Z", "2026-11-01T25:00:00Z", "bad", "2026-11-01T10:00:00+02:00"})
        REQUIRE_THROWS_AS(epoch(time), ValidationError);
    REQUIRE(utc(epoch("2028-02-29T10:00:00Z")) == "2028-02-29T10:00:00Z");
    for (auto state : {"SUBMITTED", "LATE", "MARKED", "MODERATED", "RELEASED"})
        for (auto action :
             {"mark", "approve", "release", "resubmit", "extend", "return", "unknown"}) {
            bool permitted = SubmissionLifecycle::canTransition(state, action);
            if (permitted)
                REQUIRE_NOTHROW(SubmissionLifecycle::require(state, action));
            else
                REQUIRE_THROWS_AS(SubmissionLifecycle::require(state, action), ConflictError);
        }
    REQUIRE_FALSE(SubmissionLifecycle::canTransition("MARKED", "approve", true));
}
TEST_CASE("T03 polymorphic permissions", "[unit]") {
    int index = 0;
    for (auto role : {"STUDENT", "LECTURER", "HEAD_OF_DEPARTMENT", "ADMINISTRATOR"}) {
        auto u = UserFactory::create({{"id", 10}, {"role", role}});
        REQUIRE(u->getRole() == role);
        REQUIRE(u->isPermitted(Action::Password));
        for (int a = 1; a <= 4; ++a)
            REQUIRE(u->isPermitted(static_cast<Action>(a)) == (a == (index == 0   ? 3
                                                                     : index == 1 ? 2
                                                                     : index == 2 ? 4
                                                                                  : 1)));
        ++index;
    }
    REQUIRE_THROWS_AS(UserFactory::create({{"id", 1}, {"role", "PRINCIPAL"}}), ValidationError);
    REQUIRE_THROWS_AS(requireText("  ", "x"), ValidationError);
}
TEST_CASE_METHOD(Fixture, "T04 submission resubmission and release workflow", "[integration]") {
    int id = submit();
    REQUIRE(row(id)["status"] == "SUBMITTED");
    submissions.submit(student, assignment, "new.txt", "new");
    REQUIRE(repo.list(Query::Files, {id}).size() == 2);
    REQUIRE(submissions.download(lecturer, id, 2) == "new");
    mark(id);
    REQUIRE_FALSE(submissions.resultsForStudent(student)[0].contains("raw_mark"));
    REQUIRE_THROWS_AS(submissions.submit(student, assignment, "x", "y"), ConflictError);
    moderation.approve(hod, id, ver(id));
    REQUIRE_THROWS_AS(marking.markSubmission(lecturer, id, ver(id), 20, "x", "x"), ConflictError);
    moderation.release(hod, assignment);
    REQUIRE(submissions.resultsForStudent(student)[0]["final_mark"] == 40);
    REQUIRE(moderation.auditFor(hod, id).size() == 1);
}
TEST_CASE_METHOD(Fixture, "T05 late accepted but replacement refused", "[integration]") {
    clock.value = "2026-11-02T12:00:00Z";
    int id = submit();
    REQUIRE(row(id)["status"] == "LATE");
    REQUIRE_THROWS_AS(submissions.submit(student, assignment, "new", "new"), ConflictError);
    REQUIRE(files.data.size() == 1);
    mark(id);
    REQUIRE(row(id)["days_late"] == 2);
    REQUIRE(row(id)["final_mark"] == 30);
}
TEST_CASE_METHOD(Fixture, "T06 extensions reclassify and reject after marking", "[integration]") {
    clock.value = "2026-11-02T12:00:00Z";
    int id = submit();
    extensions.grantExtension(lecturer, assignment, 2, "2026-11-03T10:00:00Z", "Illness");
    REQUIRE(row(id)["status"] == "SUBMITTED");
    REQUIRE(assignments.listForStudent(student)[0]["assignments"][0]["effective_deadline"] ==
            "2026-11-03T10:00:00Z");
    submissions.submit(student, assignment, "x", "new");
    mark(id);
    REQUIRE(row(id)["final_mark"] == 40);
    REQUIRE_THROWS_AS(
        extensions.grantExtension(lecturer, assignment, 2, "2026-11-04T10:00:00Z", "More"),
        ConflictError);
}
TEST_CASE_METHOD(Fixture, "T07 moderation return adjustment and stale version", "[integration]") {
    int id = submit();
    mark(id);
    int old = ver(id);
    moderation.returnForRemark(hod, id, old, "Check rubric");
    REQUIRE_THROWS_AS(moderation.approve(hod, id, ver(id)), ConflictError);
    REQUIRE_THROWS_AS(marking.markSubmission(lecturer, id, old, 35, "x", "x"), ConflictError);
    mark(id, 35);
    moderation.adjust(hod, id, ver(id), 30, "Rubric correction");
    REQUIRE(moderation.auditFor(hod, id).size() == 3);
    REQUIRE(row(id)["final_mark"] == 30);
    moderation.release(hod, assignment, id, ver(id));
    REQUIRE(row(id)["status"] == "RELEASED");
}
TEST_CASE_METHOD(Fixture, "T08 validation and scopes", "[integration]") {
    int id = submit();
    Student other(20);
    Lecturer outsider(22);
    HeadOfDepartment stranger(23);
    REQUIRE_THROWS_AS(assignments.create(student, 1, Json::object()), AuthorisationError);
    REQUIRE_THROWS_AS(submissions.listForLecturer(outsider, assignment), AuthorisationError);
    REQUIRE_THROWS_AS(submissions.download(other, id, 1), AuthorisationError);
    REQUIRE_THROWS_AS(moderation.approve(stranger, id, ver(id)), AuthorisationError);
    REQUIRE_THROWS_AS(marking.markSubmission(lecturer, id, ver(id), 60, "x", ""), ValidationError);
    REQUIRE_THROWS_AS(marking.markSubmission(lecturer, id, ver(id), 40, " ", ""), ValidationError);
    REQUIRE_THROWS_AS(extensions.grantExtension(lecturer, assignment, 2, clock.value, "x"),
                      ValidationError);
    REQUIRE_THROWS_AS(
        extensions.grantExtension(lecturer, assignment, 55, "2026-11-03T10:00:00Z", "x"),
        ValidationError);
    REQUIRE_THROWS_AS(submissions.submit(student, assignment, "../x", "x"), ValidationError);
    REQUIRE_THROWS_AS(submissions.submit(student, assignment, "x", ""), ValidationError);
    REQUIRE_THROWS_AS(submissions.submit(student, assignment, "x", std::string(5242881, 'x')),
                      ValidationError);
    REQUIRE(submissions.submit(student, assignment, "x", std::string(5242880, 'x'))["sequence"] ==
            2);
    REQUIRE_THROWS_AS(admin.enrol(root, 1, 3, true), ValidationError);
    REQUIRE_THROWS_AS(admin.assignLecturer(root, 1, 2, true), ValidationError);
    REQUIRE_THROWS_AS(admin.saveDepartment(root, 0, {{"name", "Bad"}, {"head_user_id", 2}}),
                      ValidationError);
}
TEST_CASE_METHOD(Fixture, "T09 rollback and optimistic locking", "[integration]") {
    auto size = repo.list(Query::Courses).size();
    REQUIRE_THROWS_AS(repo.transaction([&] {
        repo.execute(Command::CourseCreate, {"TEMP", "Temp", 1});
        throw ValidationError("Fail");
    }),
                      ValidationError);
    REQUIRE(repo.list(Query::Courses).size() == size);
    int id = submit();
    REQUIRE_THROWS_AS(marking.markSubmission(lecturer, id, 0, 40, "x", ""), ConflictError);
    REQUIRE(row(id)["raw_mark"].is_null());
    REQUIRE(moderation.auditFor(hod, id).empty());
}
TEST_CASE_METHOD(Fixture, "T10 salted passwords login lockout and sessions", "[integration]") {
    admin.resetPassword(root, 2, "Testing123!");
    auto first = repo.one(Query::UserId, {2})["password_hash"].get<std::string>();
    REQUIRE(first.find("$argon2id$") == 0);
    REQUIRE(first != "Testing123!");
    REQUIRE(hash.hash("Testing123!") != first);
    for (int n = 0; n < 5; ++n)
        REQUIRE_THROWS_AS(auth.login("STUDENT", "wrong"), AuthenticationError);
    REQUIRE_THROWS_AS(auth.login("STUDENT", "Testing123!"), AuthenticationError);
    clock.value = "2026-11-01T10:05:00Z";
    auto login = auth.login("student", "Testing123!");
    std::string token = login["token"];
    REQUIRE(auth.authenticate(token)->id == 2);
    clock.value = "2026-11-01T10:20:00Z";
    REQUIRE_THROWS_AS(auth.authenticate(token), AuthenticationError);
    token = auth.login("STUDENT", "Testing123!")["token"];
    auth.changePassword(student, "Testing123!", "Changed123!");
    REQUIRE_THROWS_AS(auth.authenticate(token), AuthenticationError);
    REQUIRE_THROWS_AS(auth.login("STUDENT", "Testing123!"), AuthenticationError);
    REQUIRE_NOTHROW(auth.login("STUDENT", "Changed123!"));
}
TEST_CASE_METHOD(Fixture, "T11 administrator operations", "[integration]") {
    int id = admin.createUser(root, {{"username", "new"},
                                     {"full_name", "New User"},
                                     {"role", "STUDENT"},
                                     {"password", "Test12345!"}});
    admin.updateUser(root, id, {{"full_name", "Updated"}, {"is_active", false}});
    REQUIRE_THROWS_AS(auth.login("new", "Test12345!"), AuthenticationError);
    REQUIRE_THROWS_AS(admin.updateUser(root, 1, {{"full_name", "Root"}, {"is_active", false}}),
                      ConflictError);
    admin.saveCourse(root, 1, {{"code", "CS4433"}, {"title", "Updated"}, {"department_id", 1}});
    admin.saveDepartment(root, 1, {{"name", "New Science"}, {"head_user_id", 4}});
    REQUIRE(admin.list(root, "courses")[0]["title"] == "Updated");
    REQUIRE(admin.list(root, "departments")[0]["name"] == "New Science");
    REQUIRE(admin.list(root, "users").size() == 5);
    admin.enrol(root, 1, 2, false);
    REQUIRE(assignments.listForStudent(student).empty());
    admin.assignLecturer(root, 1, 3, false);
    REQUIRE_THROWS_AS(submissions.listForLecturer(lecturer, assignment), AuthorisationError);
}
TEST_CASE_METHOD(Fixture, "T12 twenty SQL injection strings remain data", "[security]") {
    std::vector<std::string> attacks = {"' OR 1=1--",
                                        "' OR 'a'='a",
                                        "';DROP TABLE app_user;--",
                                        "\" OR 1=1--",
                                        "admin'--",
                                        "' UNION SELECT 1--",
                                        "0;DELETE FROM course",
                                        "') OR ('1'='1",
                                        "'/*",
                                        "' AND SLEEP(5)--",
                                        "';UPDATE app_user SET role='ADMINISTRATOR'--",
                                        "%27 OR 1=1",
                                        "\\' OR 1=1",
                                        "' OR TRUE--",
                                        "' OR NULL IS NULL--",
                                        "' UNION SELECT password_hash FROM app_user--",
                                        "' OR 1=1#",
                                        "';COMMIT;--",
                                        "' COLLATE NOCASE--",
                                        "' OR EXISTS(SELECT 1)--"};
    for (const auto &value : attacks) {
        REQUIRE_THROWS_AS(auth.login(value, "anything"), AuthenticationError);
        REQUIRE(repo.list(Query::UserName, {value}).empty());
    }
    REQUIRE(repo.list(Query::Users).size() == 4);
    REQUIRE(repo.list(Query::Courses).size() == 1);
}
TEST_CASE_METHOD(Fixture, "T13 release skips unmoderated and empty result lists", "[integration]") {
    REQUIRE(moderation.listMarked(hod).empty());
    int id = submit();
    REQUIRE(moderation.release(hod, assignment)["skipped"][0] == id);
    mark(id);
    REQUIRE(moderation.release(hod, assignment)["released"].empty());
    REQUIRE(submissions.listForLecturer(lecturer, assignment).size() == 1);
    REQUIRE_THROWS_AS(moderation.auditFor(HeadOfDepartment(99), id), AuthorisationError);
}
TEST_CASE("T14 disk persistence checksum and sweep", "[integration]") {
    REQUIRE(sodium_init() >= 0);
    auto dir = std::filesystem::temp_directory_path() / randomToken();
    DiskFileStore files(dir.string());
    std::string bytes("\0\xff\x01", 3);
    auto stored = files.save(bytes);
    REQUIRE(stored.sha256.size() == 64);
    REQUIRE(files.read(stored.path) == bytes);
    REQUIRE_THROWS_AS(files.read("../x"), NotFoundError);
    files.sweep(Json::array({{{"stored_path", stored.path}}}));
    REQUIRE(files.read(stored.path) == bytes);
    files.sweep(Json::array());
    REQUIRE_THROWS_AS(files.read(stored.path), NotFoundError);
    std::filesystem::remove(dir);
}

TEST_CASE_METHOD(Fixture, "T15 CR1 class deadline and individual precedence", "[regression]") {
    extensions.grantExtension(lecturer, assignment, 2, "2026-11-05T10:00:00Z",
                              "Individual illness");
    extensions.grantClassExtension(lecturer, assignment, "2026-11-03T10:00:00Z", "School closure");
    auto view = assignments.listForStudent(student)[0]["assignments"][0];
    REQUIRE(view["effective_deadline"] == "2026-11-05T10:00:00Z");
    REQUIRE(view["class_extension"]["reason"] == "School closure");
    REQUIRE(view["individual_extension"]["reason"] == "Individual illness");
    extensions.grantClassExtension(lecturer, assignment, "2026-11-06T10:00:00Z", "More closure");
    REQUIRE(assignments.listForStudent(student)[0]["assignments"][0]["effective_deadline"] ==
            "2026-11-06T10:00:00Z");
    REQUIRE_THROWS_AS(
        extensions.grantExtension(lecturer, assignment, 2, "2026-11-06T10:00:00Z", "Equal"),
        ValidationError);
}
TEST_CASE_METHOD(Fixture, "T16 CR1 all-student reclassification and atomic refusal",
                 "[regression]") {
    clock.value = "2026-11-02T10:00:00Z";
    int id = submit();
    extensions.grantClassExtension(lecturer, assignment, clock.value, "School event");
    REQUIRE(row(id)["status"] == "SUBMITTED");
    REQUIRE(submissions.submit(student, assignment, "new.txt", "new")["sequence"] == 2);
    REQUIRE_THROWS_AS(extensions.grantClassExtension(lecturer, assignment, clock.value, "Equal"),
                      ValidationError);
    REQUIRE_THROWS_AS(
        extensions.grantClassExtension(student, assignment, "2026-11-03T10:00:00Z", "Wrong role"),
        AuthorisationError);
    REQUIRE_THROWS_AS(extensions.grantClassExtension(Lecturer(50), assignment,
                                                     "2026-11-03T10:00:00Z", "Wrong course"),
                      AuthorisationError);
    REQUIRE_THROWS_AS(
        extensions.grantClassExtension(lecturer, assignment, "2026-11-03T10:00:00Z", " "),
        ValidationError);
    mark(id);
    REQUIRE_THROWS_AS(
        extensions.grantClassExtension(lecturer, assignment, "2026-11-03T10:00:00Z", "Too late"),
        ConflictError);
    REQUIRE(repo.one(Query::ClassExtension, {assignment})["new_deadline"] == clock.value);
    REQUIRE(row(id)["final_mark"] == 40);
}

TEST_CASE("T17 lecturer course choices are scoped and safe", "[integration]") {
    Fixture f;
    f.admin.saveCourse(f.root, 0,
                       {{"code", "OTHER"}, {"title", "Other course"}, {"department_id", 1}});
    auto courses = f.assignments.listForLecturer(f.lecturer);
    REQUIRE(courses.size() == 1);
    REQUIRE(courses[0]["code"] == "CS4433");
    REQUIRE_THROWS_AS(f.assignments.listForLecturer(f.student), AuthorisationError);
    auto id = f.submit();
    f.mark(id);
    auto refs = f.assignments.referenceData(f.student);
    REQUIRE(refs["courses"].size() == 1);
    REQUIRE(refs["submissions"].size() == 1);
    REQUIRE_FALSE(refs["submissions"][0].contains("raw_mark"));
    REQUIRE_FALSE(refs["submissions"][0].contains("feedback"));
    REQUIRE_FALSE(refs.dump().find("stored_path") != std::string::npos);
    f.admin.assignLecturer(f.root, 1, 3, false);
    REQUIRE(f.assignments.listForLecturer(f.lecturer).empty());
    REQUIRE(f.assignments.referenceData(f.lecturer)["assignments"].empty());
}
