#pragma once
#include "lms/repository.hpp"
#include <array>
#include <condition_variable>
#include <map>

namespace lms {
class PasswordHasher {
    std::mutex mutex_;
    std::condition_variable available_;
    int active_{};
    void enter();
    void leave();

  public:
    std::string hash(const std::string &password);
    bool verify(const std::string &hash, const std::string &password);
};
struct Session {
    int userId{};
    std::string lastActivity;
};
class SessionManager {
    std::mutex mutex_;
    std::map<std::string, Session> sessions_;
    IClock &clock_;

  public:
    explicit SessionManager(IClock &clock) : clock_(clock) {}
    std::string create(int userId);
    int authenticate(const std::string &token);
    void logout(const std::string &token);
    void revoke(int userId);
};
class AuthService {
    IRepository &repo_;
    IClock &clock_;
    PasswordHasher &hasher_;
    SessionManager &sessions_;
    std::array<std::mutex, 128> loginMutexes_;

  public:
    AuthService(IRepository &r, IClock &c, PasswordHasher &h, SessionManager &s)
        : repo_(r), clock_(c), hasher_(h), sessions_(s) {}
    Json login(const std::string &username, const std::string &password);
    std::unique_ptr<User> authenticate(const std::string &token);
    void logout(const std::string &token) {
        sessions_.logout(token);
    }
    void changePassword(const User &, const std::string &current, const std::string &replacement);
};
// Shared service dependencies. All scope decisions stay in the business layer.
class Service {
  protected:
    IRepository &repo_;
    IClock &clock_;

  public:
    Service(IRepository &r, IClock &c) : repo_(r), clock_(c) {}

  protected:
    void courseScope(const User &, int courseId, Action);
    Json assignmentScope(const User &, int assignmentId, Action);
    Json submissionScope(const User &, int submissionId, Action);
    std::string deadline(const Json &assignment, int studentId);
    void audit(const User &, const Json &previous, double raw, double final,
               const std::string &type, const std::string &reason);

  public:
    virtual ~Service() = default;
};
class AdminService final : public Service {
    PasswordHasher &hasher_;
    SessionManager &sessions_;

  public:
    AdminService(IRepository &r, IClock &c, PasswordHasher &h, SessionManager &s)
        : Service(r, c), hasher_(h), sessions_(s) {}
    Json list(const User &, const std::string &kind);
    int createUser(const User &, const Json &);
    void updateUser(const User &, int id, const Json &);
    void resetPassword(const User &, int id, const std::string &password);
    int saveDepartment(const User &, int id, const Json &);
    int saveCourse(const User &, int id, const Json &);
    void assignLecturer(const User &, int course, int lecturer, bool add);
    void enrol(const User &, int course, int student, bool add);
};
class AssignmentService final : public Service {
  public:
    using Service::Service;
    int create(const User &, int course, const Json &);
    Json listForStudent(const User &);
    Json listForLecturer(const User &);
    Json referenceData(const User &);
};
class ExtensionService final : public Service {
  public:
    void grantClassExtension(const User &, int assignment, const std::string &until,
                             const std::string &reason);

  public:
    using Service::Service;
    void grantExtension(const User &, int assignment, int student, const std::string &until,
                        const std::string &reason);
};
class SubmissionService final : public Service {
    IFileStore &files_;

  public:
    SubmissionService(IRepository &r, IClock &c, IFileStore &f) : Service(r, c), files_(f) {}
    Json submit(const User &, int assignment, const std::string &name, const std::string &bytes);
    Json listForLecturer(const User &, int assignment);
    Json resultsForStudent(const User &);
    std::string download(const User &, int submission, int sequence);
};
class MarkingService final : public Service {
    IPenaltyPolicy &penalty_;

  public:
    MarkingService(IRepository &r, IClock &c, IPenaltyPolicy &p) : Service(r, c), penalty_(p) {}
    void markSubmission(const User &, int submission, int version, double raw,
                        const std::string &feedback, const std::string &reason);
};
class ModerationService final : public Service {
    IPenaltyPolicy &penalty_;
    void approveInternal(const User &, int submission, int version, const Json &raw,
                         const std::string &reason);

  public:
    ModerationService(IRepository &r, IClock &c, IPenaltyPolicy &p) : Service(r, c), penalty_(p) {}
    Json listMarked(const User &);
    void approve(const User &u, int id, int v) {
        approveInternal(u, id, v, nullptr, "");
    }
    void adjust(const User &u, int id, int v, double raw, const std::string &reason) {
        approveInternal(u, id, v, raw, reason);
    }
    void returnForRemark(const User &, int id, int version, const std::string &reason);
    Json release(const User &, int assignment, int submission = 0, int version = 0);
    Json auditFor(const User &, int submission);
};
} // namespace lms
