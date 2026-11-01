#pragma once
#include "json.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

namespace lms {
using Json = nlohmann::json;
struct Error : std::runtime_error {
    int status;
    Error(int code, const std::string &message) : std::runtime_error(message), status(code) {}
};
struct AuthenticationError : Error {
    explicit AuthenticationError(const std::string &s = "Authentication failed") : Error(401, s) {}
};
struct AuthorisationError : Error {
    AuthorisationError() : Error(403, "Action is outside your role or scope") {}
};
struct ValidationError : Error {
    explicit ValidationError(const std::string &s) : Error(400, s) {}
};
struct NotFoundError : Error {
    NotFoundError() : Error(404, "Record not found") {}
};
struct ConflictError : Error {
    explicit ConflictError(const std::string &s = "Record changed; reload and try again")
        : Error(409, s) {}
};
struct DatabaseError : Error {
    DatabaseError() : Error(500, "Storage operation failed") {}
};

enum class Action { Password, Admin, Teach, Study, Moderate };
class User {
  public:
    const int id;
    explicit User(int value) : id(value) {}
    virtual ~User() = default;
    virtual std::string getRole() const = 0;
    virtual bool isPermitted(Action action) const noexcept = 0;
};
class Student final : public User {
  public:
    using User::User;
    std::string getRole() const override {
        return "STUDENT";
    }
    bool isPermitted(Action a) const noexcept override {
        return a == Action::Study || a == Action::Password;
    }
};
class Lecturer final : public User {
  public:
    using User::User;
    std::string getRole() const override {
        return "LECTURER";
    }
    bool isPermitted(Action a) const noexcept override {
        return a == Action::Teach || a == Action::Password;
    }
};
class HeadOfDepartment final : public User {
  public:
    using User::User;
    std::string getRole() const override {
        return "HEAD_OF_DEPARTMENT";
    }
    bool isPermitted(Action a) const noexcept override {
        return a == Action::Moderate || a == Action::Password;
    }
};
class Administrator final : public User {
  public:
    using User::User;
    std::string getRole() const override {
        return "ADMINISTRATOR";
    }
    bool isPermitted(Action a) const noexcept override {
        return a == Action::Admin || a == Action::Password;
    }
};
struct UserFactory {
    static std::unique_ptr<User> create(const Json &row);
};
struct IClock {
    virtual ~IClock() = default;
    virtual std::string now() const = 0;
};
struct SystemClock final : IClock {
    std::string now() const override;
};
long long epoch(const std::string &utc);
std::string utc(long long seconds);
void requireText(const std::string &value, const std::string &label, std::size_t limit = 10000);
void requirePermission(const User &user, Action action);
struct PenaltyResult {
    double finalMark;
    double penalty;
};
struct IPenaltyPolicy {
    virtual ~IPenaltyPolicy() = default;
    virtual PenaltyResult calculate(double raw, double maximum, int days) const = 0;
};
struct TieredDailyPenaltyPolicy final : IPenaltyPolicy {
    PenaltyResult calculate(double raw, double maximum, int days) const override;
    static int daysLate(const std::string &submitted, const std::string &deadline);
};
struct SubmissionLifecycle {
    static bool canTransition(const std::string &from, const std::string &event,
                              bool returned = false);
    static void require(const std::string &from, const std::string &event, bool returned = false);
};
} // namespace lms
