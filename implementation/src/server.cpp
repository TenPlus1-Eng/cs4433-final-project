#include "httplib.h"
#include "lms/services.hpp"
#include <fstream>
#include <iostream>
#include <sodium.h>

using namespace lms;
static std::string readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw ValidationError("Cannot read file: " + path);
    return {std::istreambuf_iterator<char>(in), {}};
}
int main(int argc, char **argv) {
    try {
        if (sodium_init() < 0)
            throw DatabaseError();
        std::string database = "lms.db", storage = "submissions", host = "127.0.0.1",
                    schema =
                        (std::filesystem::absolute(argv[0]).parent_path() / "schema.sql").string(),
                    operation, target;
        int port = 8080;
        for (int i = 1; i < argc; ++i) {
            std::string key = argv[i];
            if (i + 1 >= argc)
                throw ValidationError("Every option needs a value");
            std::string value = argv[++i];
            if (key == "--db")
                database = value;
            else if (key == "--files")
                storage = value;
            else if (key == "--host")
                host = value;
            else if (key == "--port")
                port = std::stoi(value);
            else if (key == "--schema")
                schema = value;
            else if (key == "--init-admin" || key == "--backup" || key == "--restore") {
                operation = key;
                target = value;
            } else
                throw ValidationError("Unknown option");
        }
        if (port < 1 || port > 65535)
            throw ValidationError("Invalid port");
        if (operation == "--restore") {
            if (std::filesystem::exists(database))
                throw ConflictError("Restore requires a new database path; stop the server first");
            SqliteDatabase source((std::filesystem::path(target) / "database.sqlite").string());
            source.backup(database);
            std::filesystem::create_directories(storage);
            std::filesystem::copy(std::filesystem::path(target) / "files", storage,
                                  std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::skip_existing);
            std::cout << "Restored backup\n";
            return 0;
        }
        bool fresh = !std::filesystem::exists(database);
        SqliteDatabase repo(database, fresh ? readFile(schema)
                                            : readFile(std::filesystem::path(schema)
                                                           .parent_path()
                                                           .append("migration-v1.1.sql")
                                                           .string()));
        DiskFileStore files(storage);
        files.sweep(repo.list(Query::FilePaths));
        if (operation == "--backup") {
            std::filesystem::create_directories(target);
            repo.backup((std::filesystem::path(target) / "database.sqlite").string());
            std::filesystem::create_directories(std::filesystem::path(target) / "files");
            for (auto f : repo.list(Query::FilePaths))
                std::filesystem::copy_file(std::filesystem::path(storage) /
                                               f.at("stored_path").get<std::string>(),
                                           std::filesystem::path(target) / "files" /
                                               f.at("stored_path").get<std::string>(),
                                           std::filesystem::copy_options::overwrite_existing);
            std::cout
                << "Backup complete (run with server stopped for a consistent file snapshot)\n";
            return 0;
        }
        SystemClock clock;
        PasswordHasher hasher;
        SessionManager sessions(clock);
        AuthService auth(repo, clock, hasher, sessions);
        AdminService admin(repo, clock, hasher, sessions);
        AssignmentService assignments(repo, clock);
        ExtensionService extensions(repo, clock);
        SubmissionService submissions(repo, clock, files);
        TieredDailyPenaltyPolicy policy;
        MarkingService marking(repo, clock, policy);
        ModerationService moderation(repo, clock, policy);
        if (operation == "--init-admin") {
            if (!repo.list(Query::Users).empty())
                throw ConflictError("Bootstrap is allowed only on an empty database");
            std::string password;
            std::cout << "Initial administrator password (8-128 bytes): ";
            std::getline(std::cin, password);
            Administrator bootstrap(0);
            admin.createUser(bootstrap, {{"username", target},
                                         {"full_name", "System administrator"},
                                         {"role", "ADMINISTRATOR"},
                                         {"password", password}});
            std::cout << "Administrator created\n";
            return 0;
        }
        httplib::Server server;
        server.set_payload_max_length(6 * 1024 * 1024);
        server.new_task_queue = [] { return new httplib::ThreadPool(8); };
        auto handler = [&](auto work, bool login = false) {
            return [&, work, login](const httplib::Request &req, httplib::Response &res) {
                try {
                    std::unique_ptr<User> user;
                    std::string token;
                    if (!login) {
                        auto header = req.get_header_value("Authorization");
                        if (header.rfind("Bearer ", 0) != 0)
                            throw AuthenticationError();
                        token = header.substr(7);
                        user = auth.authenticate(token);
                    }
                    Json input = Json::object();
                    if (!req.body.empty() && !req.is_multipart_form_data())
                        input = Json::parse(req.body);
                    Json result = work(req, user.get(), input, token);
                    res.set_content(result.dump(), "application/json");
                } catch (const Error &e) {
                    res.status = e.status;
                    res.set_content(Json({{"error", e.what()}}).dump(), "application/json");
                } catch (const nlohmann::json::exception &) {
                    res.status = 400;
                    res.set_content("{\"error\":\"Invalid or missing JSON field\"}",
                                    "application/json");
                } catch (const std::invalid_argument &) {
                    res.status = 400;
                    res.set_content("{\"error\":\"Invalid number\"}", "application/json");
                } catch (const std::out_of_range &) {
                    res.status = 400;
                    res.set_content("{\"error\":\"Number outside range\"}", "application/json");
                } catch (...) {
                    res.status = 500;
                    res.set_content("{\"error\":\"Operation failed\"}", "application/json");
                }
            };
        };
        auto id = [](const httplib::Request &r, int index = 1) {
            return std::stoi(r.matches[index].str());
        };
        server.Post("/api/login",
                    handler([&](const auto &, User *, const Json &d, const auto &)
                                -> Json { return auth.login(d.at("username"), d.at("password")); },
                            true));
        server.Post("/api/logout",
                    handler([&](const auto &, User *, const Json &, const auto &t) -> Json {
                        auth.logout(t);
                        return {{"ok", true}};
                    }));
        server.Put("/api/password",
                   handler([&](const auto &, User *u, const Json &d, const auto &) -> Json {
                       auth.changePassword(*u, d.at("current"), d.at("replacement"));
                       return {{"ok", true}};
                   }));
        server.Get(R"(/api/admin/(users|departments|courses))",
                   handler([&](const auto &r, User *u, const Json &, const auto &) -> Json {
                       return admin.list(*u, r.matches[1]);
                   }));
        server.Post("/api/admin/users",
                    handler([&](const auto &, User *u, const Json &d, const auto &) -> Json {
                        return {{"id", admin.createUser(*u, d)}};
                    }));
        server.Put(R"(/api/admin/users/(\d+))",
                   handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                       admin.updateUser(*u, id(r), d);
                       return {{"ok", true}};
                   }));
        server.Put(R"(/api/admin/users/(\d+)/password)",
                   handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                       admin.resetPassword(*u, id(r), d.at("password"));
                       return {{"ok", true}};
                   }));
        server.Post("/api/admin/departments",
                    handler([&](const auto &, User *u, const Json &d, const auto &) -> Json {
                        return {{"id", admin.saveDepartment(*u, 0, d)}};
                    }));
        server.Put(R"(/api/admin/departments/(\d+))",
                   handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                       return {{"id", admin.saveDepartment(*u, id(r), d)}};
                   }));
        server.Post("/api/admin/courses",
                    handler([&](const auto &, User *u, const Json &d, const auto &) -> Json {
                        return {{"id", admin.saveCourse(*u, 0, d)}};
                    }));
        server.Put(R"(/api/admin/courses/(\d+))",
                   handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                       return {{"id", admin.saveCourse(*u, id(r), d)}};
                   }));
        server.Put(R"(/api/admin/courses/(\d+)/lecturers)",
                   handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                       admin.assignLecturer(*u, id(r), d.at("lecturer_id"), d.value("add", true));
                       return {{"ok", true}};
                   }));
        server.Put(R"(/api/admin/courses/(\d+)/enrolments)",
                   handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                       admin.enrol(*u, id(r), d.at("student_id"), d.value("add", true));
                       return {{"ok", true}};
                   }));
        server.Post(R"(/api/courses/(\d+)/assignments)",
                    handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                        return {{"id", assignments.create(*u, id(r), d)}};
                    }));
        server.Put(R"(/api/assignments/(\d+)/class-extension)",
                   handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                       extensions.grantClassExtension(*u, id(r), d.at("new_deadline"),
                                                      d.at("reason"));
                       return {{"ok", true}};
                   }));
        server.Get("/api/lecturer/courses",
                   handler([&](const auto &, User *u, const Json &, const auto &) -> Json {
                       return assignments.listForLecturer(*u);
                   }));
        server.Get("/api/reference-data",
                   handler([&](const auto &, User *u, const Json &, const auto &) -> Json {
                       return assignments.referenceData(*u);
                   }));
        server.Get("/api/student/courses",
                   handler([&](const auto &, User *u, const Json &, const auto &) -> Json {
                       return assignments.listForStudent(*u);
                   }));
        server.Get("/api/student/results",
                   handler([&](const auto &, User *u, const Json &, const auto &) -> Json {
                       return submissions.resultsForStudent(*u);
                   }));
        server.Put(R"(/api/assignments/(\d+)/extensions/(\d+))",
                   handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                       extensions.grantExtension(*u, id(r), id(r, 2), d.at("new_deadline"),
                                                 d.at("reason"));
                       return {{"ok", true}};
                   }));
        server.Get(R"(/api/assignments/(\d+)/submissions)",
                   handler([&](const auto &r, User *u, const Json &, const auto &) -> Json {
                       return submissions.listForLecturer(*u, id(r));
                   }));
        server.Post(R"(/api/assignments/(\d+)/submission)",
                    handler([&](const auto &r, User *u, const Json &, const auto &) -> Json {
                        if (!r.has_file("file"))
                            throw ValidationError("Choose a file");
                        auto f = r.get_file_value("file");
                        return submissions.submit(*u, id(r), f.filename, f.content);
                    }));
        server.Get(R"(/api/submissions/(\d+)/files/(\d+))",
                   handler([&](const auto &r, User *u, const Json &, const auto &) -> Json {
                       auto bytes = submissions.download(*u, id(r), id(r, 2));
                       std::string hex(bytes.size() * 2 + 1, '\0');
                       sodium_bin2hex(hex.data(), hex.size(),
                                      reinterpret_cast<const unsigned char *>(bytes.data()),
                                      bytes.size());
                       hex.pop_back();
                       return {{"hex", hex}};
                   }));
        server.Put(R"(/api/submissions/(\d+)/mark)",
                   handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                       marking.markSubmission(*u, id(r), d.at("version"), d.at("raw_mark"),
                                              d.at("feedback"), d.value("reason", ""));
                       return {{"ok", true}};
                   }));
        server.Get("/api/hod/submissions",
                   handler([&](const auto &r, User *u, const Json &, const auto &) -> Json {
                       auto rows = moderation.listMarked(*u);
                       if (r.has_param("assignment")) {
                           Json filtered = Json::array();
                           for (auto s : rows)
                               if (s.at("assignment_id") ==
                                   std::stoi(r.get_param_value("assignment")))
                                   filtered.push_back(s);
                           return filtered;
                       }
                       return rows;
                   }));
        server.Get("/api/hod/audit",
                   handler([&](const auto &r, User *u, const Json &, const auto &) -> Json {
                       if (!r.has_param("submission"))
                           throw ValidationError("Submission ID required");
                       return moderation.auditFor(*u, std::stoi(r.get_param_value("submission")));
                   }));
        server.Post(R"(/api/submissions/(\d+)/approve)",
                    handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                        moderation.approve(*u, id(r), d.at("version"));
                        return {{"ok", true}};
                    }));
        server.Put(R"(/api/submissions/(\d+)/adjust)",
                   handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                       moderation.adjust(*u, id(r), d.at("version"), d.at("raw_mark"),
                                         d.at("reason"));
                       return {{"ok", true}};
                   }));
        server.Post(R"(/api/submissions/(\d+)/return)",
                    handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                        moderation.returnForRemark(*u, id(r), d.at("version"), d.at("reason"));
                        return {{"ok", true}};
                    }));
        server.Post(R"(/api/assignments/(\d+)/release)",
                    handler([&](const auto &r, User *u, const Json &d, const auto &) -> Json {
                        return moderation.release(*u, id(r), d.value("submission_id", 0),
                                                  d.value("version", 0));
                    }));
        std::cout << "LMS server listening on " << host << ":" << port << std::endl;
        if (!server.listen(host, port))
            throw ValidationError("Cannot listen on this host and port");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
