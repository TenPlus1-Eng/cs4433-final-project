#include "lms/services.hpp"
#include <cmath>
#include <set>
#include <sodium.h>

namespace lms {
void PasswordHasher::enter() {
    std::unique_lock<std::mutex> lock(mutex_);
    available_.wait(lock, [&] { return active_ < 2; });
    ++active_;
}
void PasswordHasher::leave() {
    std::lock_guard<std::mutex> lock(mutex_);
    --active_;
    available_.notify_one();
}
std::string PasswordHasher::hash(const std::string &password) {
    if (password.size() < 8 || password.size() > 128)
        throw ValidationError("Password must contain 8 to 128 bytes");
    enter();
    char out[crypto_pwhash_STRBYTES];
    int rc = crypto_pwhash_str_alg(
        out, password.data(), password.size(), crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_ARGON2ID13);
    leave();
    if (rc != 0)
        throw DatabaseError();
    return out;
}
bool PasswordHasher::verify(const std::string &hash, const std::string &password) {
    if (password.size() > 128)
        return false;
    enter();
    bool ok = crypto_pwhash_str_verify(hash.c_str(), password.data(), password.size()) == 0;
    leave();
    return ok;
}
std::string SessionManager::create(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto token = randomToken();
    sessions_[token] = {id, clock_.now()};
    return token;
}
int SessionManager::authenticate(const std::string &token) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end())
        throw AuthenticationError();
    auto now = clock_.now();
    if (epoch(now) - epoch(it->second.lastActivity) >= 900) {
        sessions_.erase(it);
        throw AuthenticationError("Session expired; log in again");
    }
    it->second.lastActivity = now;
    return it->second.userId;
}
void SessionManager::logout(const std::string &token) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(token);
}
void SessionManager::revoke(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();)
        if (it->second.userId == id)
            it = sessions_.erase(it);
        else
            ++it;
}
Json AuthService::login(const std::string &username, const std::string &password) {
    requireText(username, "Username", 100);
    if (password.size() > 128)
        throw AuthenticationError();
    auto rows = repo_.list(Query::UserName, {username});
    if (rows.empty())
        throw AuthenticationError();
    int userId = rows[0].at("id");
    std::lock_guard<std::mutex> lock(
        loginMutexes_[static_cast<unsigned>(userId) % loginMutexes_.size()]);
    auto row = repo_.one(Query::UserId, {userId});
    auto now = clock_.now();
    if (!row.at("is_active").get<int>() ||
        (!row["locked_until"].is_null() && epoch(row["locked_until"]) > epoch(now)))
        throw AuthenticationError();
    if (!hasher_.verify(row.at("password_hash"), password)) {
        int count = row.at("failed_logins");
        if (!row["locked_until"].is_null())
            count = 0;
        ++count;
        Json until = count >= 5 ? Json(utc(epoch(now) + 300)) : Json(nullptr);
        repo_.transaction(
            [&] { repo_.execute(Command::LoginFailure, {count, until, row.at("id")}); });
        throw AuthenticationError();
    }
    repo_.transaction([&] { repo_.execute(Command::LoginSuccess, {now, row.at("id")}); });
    return {{"token", sessions_.create(row.at("id"))},
            {"role", row.at("role")},
            {"user_id", row.at("id")}};
}
std::unique_ptr<User> AuthService::authenticate(const std::string &token) {
    auto row = repo_.one(Query::UserId, {sessions_.authenticate(token)});
    if (!row.at("is_active").get<int>())
        throw AuthenticationError();
    return UserFactory::create(row);
}
void AuthService::changePassword(const User &user, const std::string &current,
                                 const std::string &replacement) {
    requirePermission(user, Action::Password);
    auto row = repo_.one(Query::UserId, {user.id});
    if (!hasher_.verify(row.at("password_hash"), current))
        throw AuthenticationError();
    auto hash = hasher_.hash(replacement);
    repo_.transaction([&] { repo_.execute(Command::Password, {hash, user.id}); });
    sessions_.revoke(user.id);
}
void Service::courseScope(const User &user, int id, Action action) {
    requirePermission(user, action);
    auto course = repo_.one(Query::Course, {id});
    if (action == Action::Teach && repo_.list(Query::LecturerCourse, {id, user.id}).empty())
        throw AuthorisationError();
    if (action == Action::Study && repo_.list(Query::Enrolled, {id, user.id}).empty())
        throw AuthorisationError();
    if (action == Action::Moderate) {
        auto dep = repo_.one(Query::Department, {course.at("department_id")});
        if (dep["head_user_id"] != user.id)
            throw AuthorisationError();
    }
}
Json Service::assignmentScope(const User &user, int id, Action action) {
    auto a = repo_.one(Query::Assignment, {id});
    courseScope(user, a.at("course_id"), action);
    return a;
}
Json Service::submissionScope(const User &user, int id, Action action) {
    auto s = repo_.one(Query::SubmissionId, {id});
    assignmentScope(user, s.at("assignment_id"), action);
    if (action == Action::Study && s.at("student_id") != user.id)
        throw AuthorisationError();
    return s;
}
std::string Service::deadline(const Json &a, int student) {
    std::string effective = a.at("deadline");
    auto group = repo_.list(Query::ClassExtension, {a.at("id")});
    if (!group.empty())
        effective = std::max(effective, group[0].at("new_deadline").get<std::string>());
    auto individual = repo_.list(Query::Extension, {a.at("id"), student});
    if (!individual.empty())
        effective = std::max(effective, individual[0].at("new_deadline").get<std::string>());
    return effective;
}
void Service::audit(const User &user, const Json &old, double raw, double final,
                    const std::string &type, const std::string &reason) {
    repo_.execute(Command::AuditCreate,
                  {old.at("id"), user.id, clock_.now(), type, old.at("raw_mark"), raw,
                   old.at("final_mark"), final, reason});
}
Json AdminService::list(const User &u, const std::string &kind) {
    requirePermission(u, Action::Admin);
    if (kind == "users")
        return repo_.list(Query::Users);
    if (kind == "departments")
        return repo_.list(Query::Departments);
    if (kind == "courses")
        return repo_.list(Query::Courses);
    throw ValidationError("Unknown list");
}
int AdminService::createUser(const User &u, const Json &data) {
    requirePermission(u, Action::Admin);
    auto username = data.at("username").get<std::string>();
    auto name = data.at("full_name").get<std::string>();
    requireText(username, "Username", 100);
    requireText(name, "Full name", 200);
    UserFactory::create({{"id", 0}, {"role", data.at("role")}});
    auto hash = hasher_.hash(data.at("password"));
    long long id = 0;
    repo_.transaction([&] {
        id = repo_.execute(Command::CreateUser,
                           {username, name, data.at("role"), hash, clock_.now()});
    });
    return static_cast<int>(id);
}
void AdminService::updateUser(const User &u, int id, const Json &data) {
    requirePermission(u, Action::Admin);
    repo_.one(Query::UserId, {id});
    auto name = data.at("full_name").get<std::string>();
    requireText(name, "Full name", 200);
    bool active = data.at("is_active");
    if (id == u.id && !active)
        throw ConflictError("Cannot deactivate your own administrator account");
    repo_.transaction([&] { repo_.execute(Command::UpdateUser, {name, active, id}); });
    if (!active)
        sessions_.revoke(id);
}
void AdminService::resetPassword(const User &u, int id, const std::string &password) {
    requirePermission(u, Action::Admin);
    repo_.one(Query::UserId, {id});
    auto hash = hasher_.hash(password);
    repo_.transaction([&] { repo_.execute(Command::Password, {hash, id}); });
    sessions_.revoke(id);
}
int AdminService::saveDepartment(const User &u, int id, const Json &d) {
    requirePermission(u, Action::Admin);
    auto name = d.at("name").get<std::string>();
    requireText(name, "Name", 200);
    auto head = d.value("head_user_id", Json(nullptr));
    if (!head.is_null() && repo_.one(Query::UserId, {head}).at("role") != "HEAD_OF_DEPARTMENT")
        throw ValidationError("Department head must have the HoD role");
    repo_.transaction([&] {
        if (id) {
            repo_.one(Query::Department, {id});
            repo_.execute(Command::DepartmentUpdate, {name, head, id});
        } else
            id = static_cast<int>(repo_.execute(Command::DepartmentCreate, {name, head}));
    });
    return id;
}
int AdminService::saveCourse(const User &u, int id, const Json &d) {
    requirePermission(u, Action::Admin);
    std::string code = d.at("code"), title = d.at("title");
    requireText(code, "Code", 50);
    requireText(title, "Title", 200);
    repo_.one(Query::Department, {d.at("department_id")});
    repo_.transaction([&] {
        if (id) {
            repo_.one(Query::Course, {id});
            repo_.execute(Command::CourseUpdate, {code, title, d.at("department_id"), id});
        } else
            id = static_cast<int>(
                repo_.execute(Command::CourseCreate, {code, title, d.at("department_id")}));
    });
    return id;
}
void AdminService::assignLecturer(const User &u, int course, int lecturer, bool add) {
    requirePermission(u, Action::Admin);
    repo_.one(Query::Course, {course});
    if (repo_.one(Query::UserId, {lecturer}).at("role") != "LECTURER")
        throw ValidationError("Select a lecturer");
    repo_.transaction([&] {
        repo_.execute(add ? Command::LecturerAdd : Command::LecturerRemove, {course, lecturer});
    });
}
void AdminService::enrol(const User &u, int course, int student, bool add) {
    requirePermission(u, Action::Admin);
    repo_.one(Query::Course, {course});
    if (repo_.one(Query::UserId, {student}).at("role") != "STUDENT")
        throw ValidationError("Select a student");
    repo_.transaction([&] {
        if (add)
            repo_.execute(Command::Enrol, {course, student, clock_.now()});
        else
            repo_.execute(Command::Unenrol, {course, student});
    });
}
int AssignmentService::create(const User &u, int course, const Json &d) {
    courseScope(u, course, Action::Teach);
    std::string title = d.at("title"), description = d.at("description"), until = d.at("deadline");
    requireText(title, "Title", 200);
    if (description.size() > 10000)
        throw ValidationError("Description too long");
    epoch(until);
    double maximum = d.at("max_mark");
    if (!std::isfinite(maximum) || maximum <= 0)
        throw ValidationError("Maximum must be positive");
    long long id = 0;
    repo_.transaction([&] {
        id = repo_.execute(Command::AssignmentCreate,
                           {course, title, description, until, maximum, u.id, clock_.now()});
    });
    return static_cast<int>(id);
}
Json AssignmentService::listForLecturer(const User &u) {
    requirePermission(u, Action::Teach);
    auto courses = repo_.list(Query::LecturerCourses, {u.id});
    for (auto &c : courses)
        c["assignment_count"] = repo_.list(Query::Assignments, {c.at("id")}).size();
    return courses;
}
Json AssignmentService::referenceData(const User &u) {
    Json out;
    for (auto key : {"courses", "users", "departments", "assignments", "submissions"})
        out[key] = Json::array();
    auto role = u.getRole();
    if (role == "ADMINISTRATOR") {
        out["courses"] = repo_.list(Query::Courses);
        out["users"] = repo_.list(Query::Users);
        out["departments"] = repo_.list(Query::Departments);
        return out;
    }
    auto query = role == "LECTURER"  ? Query::LecturerCourses
                 : role == "STUDENT" ? Query::StudentCourses
                                     : Query::DepartmentCourses;
    out["courses"] = repo_.list(query, {u.id});
    std::set<int> users;
    for (auto c : out["courses"]) {
        if (role != "STUDENT")
            for (auto student : repo_.list(Query::CourseStudents, {c.at("id")}))
                if (users.insert(student.at("id").get<int>()).second)
                    out["users"].push_back(student);
        for (auto a : repo_.list(Query::Assignments, {c.at("id")})) {
            a["course_code"] = c.at("code");
            out["assignments"].push_back(a);
            for (auto sub : repo_.list(Query::LecturerSubmissions, {a.at("id")})) {
                if (role == "STUDENT" && sub.at("student_id") != u.id)
                    continue;
                Json safe = {{"id", sub.at("id")},
                             {"assignment_id", a.at("id")},
                             {"student_id", sub.at("student_id")},
                             {"version", sub.at("version")},
                             {"title", a.at("title")},
                             {"course_code", c.at("code")}};
                safe["files"] = Json::array();
                for (auto f : repo_.list(Query::Files, {sub.at("id")}))
                    safe["files"].push_back(
                        {{"id", f.at("seq_no")}, {"title", f.at("original_name")}});
                out["submissions"].push_back(safe);
            }
        }
    }
    return out;
}
Json AssignmentService::listForStudent(const User &u) {
    requirePermission(u, Action::Study);
    auto courses = repo_.list(Query::StudentCourses, {u.id});
    for (auto &c : courses) {
        c["assignments"] = repo_.list(Query::Assignments, {c.at("id")});
        for (auto &a : c["assignments"]) {
            a["effective_deadline"] = deadline(a, u.id);
            auto group = repo_.list(Query::ClassExtension, {a.at("id")});
            auto individual = repo_.list(Query::Extension, {a.at("id"), u.id});
            a["class_extension"] = group.empty() ? Json(nullptr)
                                                 : Json({{"deadline", group[0].at("new_deadline")},
                                                         {"reason", group[0].at("reason")}});
            a["individual_extension"] = individual.empty()
                                            ? Json(nullptr)
                                            : Json({{"deadline", individual[0].at("new_deadline")},
                                                    {"reason", individual[0].at("reason")}});
        }
    }
    return courses;
}
void ExtensionService::grantClassExtension(const User &u, int id, const std::string &until,
                                           const std::string &reason) {
    requireText(reason, "Class extension reason");
    epoch(until);
    repo_.transaction([&] {
        auto a = assignmentScope(u, id, Action::Teach);
        std::string previous = a.at("deadline");
        auto existing = repo_.list(Query::ClassExtension, {id});
        if (!existing.empty())
            previous = existing[0].at("new_deadline");
        if (epoch(until) <= epoch(previous))
            throw ValidationError("Class deadline must move forward");
        auto submissions = repo_.list(Query::LecturerSubmissions, {id});
        for (const auto &s : submissions)
            SubmissionLifecycle::require(s.at("status"), "extend");
        repo_.execute(Command::ClassExtensionSave, {id, until, reason, u.id, clock_.now()});
        for (auto s : submissions) {
            auto effective = deadline(a, s.at("student_id"));
            repo_.execute(Command::ExtensionReclassify,
                          {epoch(s.at("submitted_at")) <= epoch(effective) ? "SUBMITTED" : "LATE",
                           s.at("id"), s.at("version")});
        }
    });
}
void ExtensionService::grantExtension(const User &u, int id, int student, const std::string &until,
                                      const std::string &reason) {
    requireText(reason, "Reason");
    epoch(until);
    repo_.transaction([&] {
        auto a = assignmentScope(u, id, Action::Teach);
        if (repo_.list(Query::Enrolled, {a.at("course_id"), student}).empty())
            throw ValidationError("Student is not enrolled");
        if (epoch(until) <= epoch(deadline(a, student)))
            throw ValidationError("Extension must be later than the current effective deadline");
        auto rows = repo_.list(Query::SubmissionPair, {id, student});
        if (!rows.empty())
            SubmissionLifecycle::require(rows[0].at("status"), "extend");
        repo_.execute(Command::ExtensionSave, {id, student, until, reason, u.id, clock_.now()});
        if (!rows.empty()) {
            auto s = rows[0];
            repo_.execute(Command::ExtensionReclassify,
                          {epoch(s.at("submitted_at")) <= epoch(until) ? "SUBMITTED" : "LATE",
                           s.at("id"), s.at("version")});
        }
    });
}
Json SubmissionService::submit(const User &u, int id, const std::string &name,
                               const std::string &bytes) {
    assignmentScope(u, id, Action::Study);
    requireText(name, "File name", 255);
    if (name.find_first_of("/\\\r\n") != std::string::npos)
        throw ValidationError("Use a file name without directories");
    auto stored = files_.save(bytes);
    Json receipt;
    try {
        repo_.transaction([&] {
            auto a = assignmentScope(u, id, Action::Study);
            auto until = deadline(a, u.id);
            auto now = clock_.now();
            auto rows = repo_.list(Query::SubmissionPair, {id, u.id});
            int submission = 0, sequence = 1;
            std::string state = epoch(now) <= epoch(until) ? "SUBMITTED" : "LATE";
            if (rows.empty())
                submission = static_cast<int>(
                    repo_.execute(Command::SubmissionCreate, {id, u.id, now, state}));
            else {
                auto s = rows[0];
                SubmissionLifecycle::require(s.at("status"), "resubmit");
                if (epoch(now) > epoch(until))
                    throw ConflictError("The effective deadline has passed");
                submission = s.at("id");
                sequence = static_cast<int>(repo_.list(Query::Files, {submission}).size()) + 1;
                repo_.execute(Command::SubmissionReplace,
                              {now, state, submission, s.at("version")});
            }
            repo_.execute(Command::FileCreate, {submission, sequence, name, stored.path,
                                                stored.size, stored.sha256, now});
            receipt = {{"id", submission},
                       {"sequence", sequence},
                       {"status", state},
                       {"submitted_at", now},
                       {"sha256", stored.sha256}};
        });
    } catch (...) {
        files_.remove(stored.path);
        throw;
    }
    return receipt;
}
Json SubmissionService::listForLecturer(const User &u, int id) {
    auto a = assignmentScope(u, id, Action::Teach);
    auto rows = repo_.list(Query::LecturerSubmissions, {id});
    for (auto &s : rows) {
        s["effective_deadline"] = deadline(a, s.at("student_id"));
        s["files"] = repo_.list(Query::Files, {s.at("id")});
        for (auto &f : s["files"])
            f.erase("stored_path");
    }
    return rows;
}
Json SubmissionService::resultsForStudent(const User &u) {
    requirePermission(u, Action::Study);
    Json result = Json::array();
    for (auto s : repo_.list(Query::StudentResults, {u.id})) {
        Json item = {{"id", s.at("id")},
                     {"assignment_id", s.at("assignment_id")},
                     {"status", s.at("status")}};
        if (s.at("status") == "RELEASED") {
            item["raw_mark"] = s.at("raw_mark");
            item["final_mark"] = s.at("final_mark");
            item["feedback"] = s.at("feedback");
            item["days_late"] = s.at("days_late");
            item["penalty"] = s.at("raw_mark").get<double>() - s.at("final_mark").get<double>();
        } else if (s.at("status") != "SUBMITTED" && s.at("status") != "LATE")
            item["status"] = "Awaiting results";
        result.push_back(item);
    }
    return result;
}
std::string SubmissionService::download(const User &u, int id, int seq) {
    if (u.isPermitted(Action::Teach))
        submissionScope(u, id, Action::Teach);
    else if (u.isPermitted(Action::Moderate))
        submissionScope(u, id, Action::Moderate);
    else
        submissionScope(u, id, Action::Study);
    for (auto f : repo_.list(Query::Files, {id}))
        if (f.at("seq_no") == seq)
            return files_.read(f.at("stored_path"));
    throw NotFoundError();
}
void MarkingService::markSubmission(const User &u, int id, int version, double raw,
                                    const std::string &feedback, const std::string &reason) {
    requireText(feedback, "Written feedback");
    repo_.transaction([&] {
        auto s = submissionScope(u, id, Action::Teach);
        SubmissionLifecycle::require(s.at("status"), "mark");
        if (s.at("version") != version)
            throw ConflictError();
        bool initial = s.at("raw_mark").is_null();
        if (!initial)
            requireText(reason, "Reason");
        auto a = repo_.one(Query::Assignment, {s.at("assignment_id")});
        int days = TieredDailyPenaltyPolicy::daysLate(s.at("submitted_at"),
                                                      deadline(a, s.at("student_id")));
        auto result = penalty_.calculate(raw, a.at("max_mark"), days);
        repo_.execute(Command::Mark,
                      {raw, result.finalMark, days, feedback, u.id, clock_.now(), id, version});
        audit(u, s, raw, result.finalMark, initial ? "INITIAL" : "REMARK",
              initial ? "Initial mark" : reason);
    });
}
Json ModerationService::listMarked(const User &u) {
    requirePermission(u, Action::Moderate);
    return repo_.list(Query::DepartmentSubmissions, {u.id});
}
void ModerationService::approveInternal(const User &u, int id, int version, const Json &replacement,
                                        const std::string &reason) {
    repo_.transaction([&] {
        auto s = submissionScope(u, id, Action::Moderate);
        SubmissionLifecycle::require(s.at("status"), "approve",
                                     s.at("is_returned").get<int>() != 0);
        if (s.at("version") != version)
            throw ConflictError();
        double raw =
            replacement.is_null() ? s.at("raw_mark").get<double>() : replacement.get<double>();
        if (!replacement.is_null())
            requireText(reason, "Adjustment reason");
        auto a = repo_.one(Query::Assignment, {s.at("assignment_id")});
        int days = TieredDailyPenaltyPolicy::daysLate(s.at("submitted_at"),
                                                      deadline(a, s.at("student_id")));
        auto result = penalty_.calculate(raw, a.at("max_mark"), days);
        repo_.execute(Command::Approve,
                      {raw, result.finalMark, days, u.id, clock_.now(), id, version});
        if (raw != s.at("raw_mark").get<double>() ||
            result.finalMark != s.at("final_mark").get<double>())
            audit(u, s, raw, result.finalMark, "HOD_ADJUST", reason);
    });
}
void ModerationService::returnForRemark(const User &u, int id, int version,
                                        const std::string &reason) {
    requireText(reason, "Return reason");
    repo_.transaction([&] {
        auto s = submissionScope(u, id, Action::Moderate);
        SubmissionLifecycle::require(s.at("status"), "return", s.at("is_returned").get<int>() != 0);
        repo_.execute(Command::Return, {reason, u.id, id, version});
    });
}
Json ModerationService::release(const User &u, int assignment, int submission, int version) {
    assignmentScope(u, assignment, Action::Moderate);
    Json released = Json::array(), skipped = Json::array();
    repo_.transaction([&] {
        for (auto s : repo_.list(Query::LecturerSubmissions, {assignment})) {
            int id = s.at("id");
            if (submission && submission != id)
                continue;
            if (s.at("status") != "MODERATED" || s.at("feedback").is_null() ||
                s.at("feedback").get<std::string>().find_first_not_of(" \r\n\t") ==
                    std::string::npos) {
                skipped.push_back(id);
                continue;
            }
            repo_.execute(Command::Release,
                          {u.id, clock_.now(), id, submission ? Json(version) : s.at("version")});
            released.push_back(id);
        }
    });
    return {{"released", released}, {"skipped", skipped}};
}
Json ModerationService::auditFor(const User &u, int id) {
    submissionScope(u, id, Action::Moderate);
    return repo_.list(Query::Audit, {id});
}
} // namespace lms
