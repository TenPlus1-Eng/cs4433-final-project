#include "lms/domain.hpp"
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>

namespace lms {
std::unique_ptr<User> UserFactory::create(const Json &row) {
    int id = row.at("id");
    const auto role = row.at("role").get<std::string>();
    if (role == "STUDENT")
        return std::make_unique<Student>(id);
    if (role == "LECTURER")
        return std::make_unique<Lecturer>(id);
    if (role == "HEAD_OF_DEPARTMENT")
        return std::make_unique<HeadOfDepartment>(id);
    if (role == "ADMINISTRATOR")
        return std::make_unique<Administrator>(id);
    throw ValidationError("Unknown role");
}
std::string utc(long long value) {
    auto seconds = static_cast<std::time_t>(value);
    std::tm t{};
#ifdef _WIN32
    gmtime_s(&t, &seconds);
#else
    gmtime_r(&seconds, &t);
#endif
    std::ostringstream out;
    out << std::put_time(&t, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
long long epoch(const std::string &value) {
    if (value.size() != 20)
        throw ValidationError("Use UTC time YYYY-MM-DDTHH:MM:SSZ");
    std::tm t{};
    std::istringstream in(value);
    in >> std::get_time(&t, "%Y-%m-%dT%H:%M:%SZ");
    if (in.fail())
        throw ValidationError("Invalid UTC timestamp");
#ifdef _WIN32
    auto result = _mkgmtime(&t);
#else
    auto result = timegm(&t);
#endif
    if (result < 0 || utc(result) != value)
        throw ValidationError("Invalid UTC date or time");
    return result;
}
std::string SystemClock::now() const {
    return utc(std::time(nullptr));
}
void requireText(const std::string &s, const std::string &label, std::size_t limit) {
    if (s.empty() || s.size() > limit || s.find_first_not_of(" \r\n\t") == std::string::npos)
        throw ValidationError(label + " is required or exceeds its size limit");
}
void requirePermission(const User &user, Action action) {
    if (!user.isPermitted(action))
        throw AuthorisationError();
}
PenaltyResult TieredDailyPenaltyPolicy::calculate(double raw, double maximum, int days) const {
    if (!std::isfinite(raw) || !std::isfinite(maximum) || maximum <= 0 || raw < 0 ||
        raw > maximum || days < 0)
        throw ValidationError("Mark must be within 0 and the positive maximum");
    double final = days > 3 ? 0 : std::max(0.0, raw - days * 0.1 * maximum);
    return {final, raw - final};
}
int TieredDailyPenaltyPolicy::daysLate(const std::string &submitted, const std::string &deadline) {
    auto difference = epoch(submitted) - epoch(deadline);
    return difference <= 0 ? 0 : static_cast<int>((difference + 86399) / 86400);
}
bool SubmissionLifecycle::canTransition(const std::string &from, const std::string &event,
                                        bool returned) {
    if (event == "resubmit")
        return from == "SUBMITTED";
    if (event == "extend")
        return from == "SUBMITTED" || from == "LATE";
    if (event == "mark")
        return from == "SUBMITTED" || from == "LATE" || from == "MARKED";
    if (event == "approve" || event == "return")
        return from == "MARKED" && !returned;
    if (event == "release")
        return from == "MODERATED";
    return false;
}
void SubmissionLifecycle::require(const std::string &from, const std::string &event,
                                  bool returned) {
    if (!canTransition(from, event, returned))
        throw ConflictError("This action is not allowed in the current submission state");
}
} // namespace lms
