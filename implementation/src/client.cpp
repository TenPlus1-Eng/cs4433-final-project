#include "httplib.h"
#include "json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <cctype>
#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif
using Json = nlohmann::json;
static std::string roleName(const std::string &role) {
    if (role == "STUDENT") return "Student";
    if (role == "LECTURER") return "Lecturer";
    if (role == "HEAD_OF_DEPARTMENT") return "Head of Department";
    if (role == "ADMINISTRATOR") return "Administrator";
    return role;
}
static std::string fieldLabel(std::string key) {
    static const std::map<std::string, std::string> labels = {
        {"id", "Record ID"}, {"full_name", "Full name"}, {"username", "Username"},
        {"is_active", "Account active"}, {"head_user_id", "Head of Department user ID"},
        {"department_id", "Department ID"}, {"lecturer_id", "Lecturer user ID"},
        {"student_id", "Student user ID"}, {"max_mark", "Maximum mark"},
        {"raw_mark", "Raw mark"}, {"final_mark", "Final mark"},
        {"deadline", "Deadline (UTC: YYYY-MM-DDTHH:MM:SSZ)"},
        {"new_deadline", "New deadline (UTC: YYYY-MM-DDTHH:MM:SSZ)"},
        {"current", "Current password"}, {"replacement", "New password"},
        {"add", "Add this person? Enter yes to add, no to remove"},
        {"version", "Current version from the submissions list"},
        {"submission_id", "Submission ID (0 for all eligible submissions)"}};
    auto found = labels.find(key);
    if (found != labels.end()) return found->second;
    std::replace(key.begin(), key.end(), '_', ' ');
    if (!key.empty()) key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
    return key;
}
static std::string ask(const std::string &label, bool secret = false) {
    std::cout << label << ": ";
    std::string value;
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    bool console = GetConsoleMode(input, &mode);
    if (secret && console)
        SetConsoleMode(input, mode & ~ENABLE_ECHO_INPUT);
#else
    termios mode{};
    bool console = tcgetattr(STDIN_FILENO, &mode) == 0;
    if (secret && console) {
        auto hidden = mode;
        hidden.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &hidden);
    }
#endif
    if (!std::getline(std::cin, value))
        throw std::runtime_error("Input closed");
#ifdef _WIN32
    if (secret && console)
        SetConsoleMode(input, mode);
#else
    if (secret && console)
        tcsetattr(STDIN_FILENO, TCSANOW, &mode);
#endif
    if (secret)
        std::cout << '\n';
    return value;
}
struct Operation {
    std::string label, verb, path, fields;
};
static std::string chooseRole() {
    const std::vector<std::string> roles = {"STUDENT", "LECTURER", "HEAD_OF_DEPARTMENT", "ADMINISTRATOR"};
    while (true) {
        std::cout << "\nChoose the new user's role:\n";
        for (std::size_t i = 0; i < roles.size(); ++i)
            std::cout << "  " << i + 1 << ". " << roleName(roles[i]) << '\n';
        auto value = ask("Role number (1-4)");
        for (auto &ch : value) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        for (std::size_t i = 0; i < roles.size(); ++i)
            if (value == std::to_string(i + 1) || value == roles[i]) return roles[i];
        std::cout << "Please choose one of the four roles shown above.\n";
    }
}
static void showResult(const Json &value, int depth = 0) {
    std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    if (value.is_object()) {
        if (value.size() == 1 && value.contains("ok") && value.at("ok") == true) {
            std::cout << indent << "Action completed successfully.\n";
            return;
        }
        for (auto item = value.begin(); item != value.end(); ++item) {
            std::cout << indent << fieldLabel(item.key()) << ": ";
            if (item.value().is_structured()) {
                std::cout << '\n'; showResult(item.value(), depth + 1);
            } else if (item.key() == "role" && item.value().is_string()) {
                std::cout << roleName(item.value().get<std::string>()) << '\n';
            } else {
                showResult(item.value(), 0);
            }
        }
    } else if (value.is_array()) {
        if (value.empty()) std::cout << indent << "No records to show.\n";
        for (std::size_t i = 0; i < value.size(); ++i) {
            std::cout << indent << "--- Record " << i + 1 << " ---\n";
            showResult(value[i], depth + 1);
        }
    } else if (value.is_null()) std::cout << "Not set\n";
    else if (value.is_boolean()) std::cout << (value.get<bool>() ? "Yes" : "No") << '\n';
    else if (value.is_string()) std::cout << value.get<std::string>() << '\n';
    else std::cout << value.dump() << '\n';
}
static std::string replace(std::string path) {
    for (std::size_t left; (left = path.find('{')) != std::string::npos;) {
        auto right = path.find('}', left);
        auto parameter = path.substr(left + 1, right - left - 1);
        auto value = ask(parameter == "sequence" ? "File sequence number" : fieldLabel(parameter) + " ID");
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("Enter a numeric ID");
        path.replace(left, right - left + 1, value);
    }
    return path;
}
static Json form(const std::string &fields) {
    Json data = Json::object();
    std::istringstream in(fields);
    std::string field;
    while (in >> field) {
        auto colon = field.find(':');
        auto key = field.substr(0, colon);
        auto type = colon == std::string::npos ? "text" : field.substr(colon + 1);
        if (key == "role") {
            data[key] = chooseRole();
            continue;
        }
        if (type == "bool") {
            while (true) {
                auto value = ask(fieldLabel(key) + " (yes/no)");
                for (auto &ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (value == "yes" || value == "true" || value == "1") { data[key] = true; break; }
                if (value == "no" || value == "false" || value == "0") { data[key] = false; break; }
                std::cout << "Please enter yes or no.\n";
            }
            continue;
        }
        auto value = ask(fieldLabel(key), type == "password");
        if (type == "number")
            data[key] = std::stod(value);
        else if (type == "integer")
            data[key] = std::stoi(value);
        else
            data[key] = value;
    }
    return data;
}
int main(int argc, char **argv) {
    try {
        std::string host = "http://127.0.0.1:8080";
        if (argc > 1) {
            host = argv[1];
        } else {
            auto config = std::filesystem::absolute(argv[0]).parent_path() / "server.url";
            std::ifstream address(config);
            std::string configured;
            if (std::getline(address, configured) && !configured.empty())
                host = configured;
        }
        std::cout << "Server: " << host << '\n';
        httplib::Client client(host);
        client.set_connection_timeout(5);
        client.set_read_timeout(120);
        std::string token, role;
        while (true) {
            if (token.empty()) {
                std::cout << "\nModerated Learning Management System\n";
                auto username = ask("Username (or quit)");
                if (username == "quit")
                    break;
                auto password = ask("Password", true);
                auto response = client.Post(
                    "/api/login", Json({{"username", username}, {"password", password}}).dump(),
                    "application/json");
                if (!response) {
                    std::cout << "Server unavailable\n";
                    continue;
                }
                auto d = Json::parse(response->body);
                if (response->status != 200) {
                    showResult(d);
                    continue;
                }
                token = d.at("token");
                role = d.at("role");
            }
            std::vector<Operation> menu;
            if (role == "ADMINISTRATOR")
                menu = {{"List users", "GET", "/api/admin/users", ""},
                        {"Create user", "POST", "/api/admin/users",
                         "username full_name role password:password"},
                        {"Edit/deactivate user", "PUT", "/api/admin/users/{user}",
                         "full_name is_active:bool"},
                        {"Reset password", "PUT", "/api/admin/users/{user}/password",
                         "password:password"},
                        {"List departments", "GET", "/api/admin/departments", ""},
                        {"Create department", "POST", "/api/admin/departments",
                         "name head_user_id:integer"},
                        {"Edit department", "PUT", "/api/admin/departments/{department}",
                         "name head_user_id:integer"},
                        {"List courses", "GET", "/api/admin/courses", ""},
                        {"Create course", "POST", "/api/admin/courses",
                         "code title department_id:integer"},
                        {"Edit course", "PUT", "/api/admin/courses/{course}",
                         "code title department_id:integer"},
                        {"Assign/remove lecturer", "PUT", "/api/admin/courses/{course}/lecturers",
                         "lecturer_id:integer add:bool"},
                        {"Enrol/remove student", "PUT", "/api/admin/courses/{course}/enrolments",
                         "student_id:integer add:bool"}};
            if (role == "LECTURER")
                menu = {{"Create assignment", "POST", "/api/courses/{course}/assignments",
                         "title description deadline max_mark:number"},
                        {"Grant individual extension", "PUT",
                         "/api/assignments/{assignment}/extensions/{student}",
                         "new_deadline reason"},
                        {"List submissions and file history", "GET",
                         "/api/assignments/{assignment}/submissions", ""},
                        {"Mark/re-mark", "PUT", "/api/submissions/{submission}/mark",
                         "version:integer raw_mark:number feedback reason"}};
            if (role == "LECTURER")
                menu.push_back({"Grant class extension", "PUT",
                                "/api/assignments/{assignment}/class-extension",
                                "new_deadline reason"});
            if (role == "STUDENT")
                menu = {{"My courses and assignments", "GET", "/api/student/courses", ""},
                        {"Submit/resubmit file", "UPLOAD",
                         "/api/assignments/{assignment}/submission", ""},
                        {"My results", "GET", "/api/student/results", ""}};
            if (role == "HEAD_OF_DEPARTMENT")
                menu = {{"Department submissions", "GET", "/api/hod/submissions", ""},
                        {"Approve mark", "POST", "/api/submissions/{submission}/approve",
                         "version:integer"},
                        {"Adjust and approve", "PUT", "/api/submissions/{submission}/adjust",
                         "version:integer raw_mark:number reason"},
                        {"Return for re-marking", "POST", "/api/submissions/{submission}/return",
                         "version:integer reason"},
                        {"Release assignment (0 for all)", "POST",
                         "/api/assignments/{assignment}/release",
                         "submission_id:integer version:integer"},
                        {"View mark audit", "GET", "/api/hod/audit?submission={submission}", ""}};
            if (role != "ADMINISTRATOR")
                menu.push_back({"Download submission file", "DOWNLOAD",
                                "/api/submissions/{submission}/files/{sequence}", ""});
            menu.push_back({"Change my password", "PUT", "/api/password",
                            "current:password replacement:password"});
            menu.push_back({"Log out", "POST", "/api/logout", ""});
            std::cout << "\n==================================================\n"
                      << roleName(role) << " - Main menu\n"
                      << "==================================================\n";
            for (std::size_t i = 0; i < menu.size(); ++i)
                std::cout << i + 1 << ". " << menu[i].label << '\n';
            try {
                auto selection = ask("Enter an option number (or type menu to show options)");
                if (selection == "menu" || selection == "help") continue;
                std::size_t consumed = 0;
                int choice = std::stoi(selection, &consumed);
                if (consumed != selection.size()) throw std::runtime_error("Enter an option number from the menu");
                if (choice < 1 || choice > static_cast<int>(menu.size()))
                    throw std::runtime_error("Choose a listed action");
                auto op = menu[choice - 1];
                std::cout << "\n--- " << op.label << " ---\n";
                auto path = replace(op.path);
                auto data = form(op.fields);
                httplib::Headers headers = {{"Authorization", "Bearer " + token}};
                httplib::Result response;
                if (op.verb == "GET" || op.verb == "DOWNLOAD")
                    response = client.Get(path, headers);
                else if (op.verb == "UPLOAD") {
                    auto filename = ask("File path");
                    std::ifstream in(filename, std::ios::binary);
                    if (!in)
                        throw std::runtime_error("Cannot read file");
                    in.seekg(0, std::ios::end);
                    if (in.tellg() > 5242880)
                        throw std::runtime_error("File exceeds 5 MB");
                    in.seekg(0);
                    std::string bytes(std::istreambuf_iterator<char>(in), {});
                    auto name = filename.substr(filename.find_last_of("/\\") == std::string::npos
                                                    ? 0
                                                    : filename.find_last_of("/\\") + 1);
                    response = client.Post(path, headers,
                                           httplib::MultipartFormDataItems{
                                               {"file", bytes, name, "application/octet-stream"}});
                } else if (op.verb == "POST")
                    response = client.Post(path, headers, data.dump(), "application/json");
                else
                    response = client.Put(path, headers, data.dump(), "application/json");
                if (!response)
                    throw std::runtime_error(
                        "Server unavailable; retry after checking the connection");
                auto result = Json::parse(response->body);
                if (op.verb == "DOWNLOAD" && response->status == 200) {
                    auto out = ask("Save file as");
                    if (std::ifstream(out).good())
                        throw std::runtime_error("Choose a new file name to avoid overwriting");
                    std::ofstream file(out, std::ios::binary);
                    auto hex = result.at("hex").get<std::string>();
                    for (std::size_t i = 0; i < hex.size(); i += 2)
                        file.put(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
                    if (!file)
                        throw std::runtime_error("Cannot save file");
                    std::cout << "File saved\n";
                } else
                    showResult(result);
                if (response->status == 401 || path == "/api/logout" ||
                    (path == "/api/password" && response->status == 200))
                    token.clear();
            } catch (const std::exception &e) {
                if (!std::cin)
                    return 0;
                std::cout << "Please check your input: " << e.what() << '\n';
            }
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
