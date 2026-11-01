#define NOMINMAX
#include "httplib.h"
#include "json.hpp"
#include "lms/gui_actions.hpp"
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <shellapi.h>
#include <thread>
#include <windows.h>
#include <winsock2.h>
#ifdef LMS_GUI_TEST
#include <atomic>
#endif

using Json = nlohmann::json;
using namespace desktop;
static constexpr UINT RequestDone = WM_APP + 10;
static constexpr COLORREF Navy = RGB(20, 35, 57), Background = RGB(244, 247, 251),
                          Ink = RGB(29, 43, 65), Muted = RGB(91, 107, 128),
                          Accent = RGB(21, 110, 115);
static std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n, L' ');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
static std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                nullptr, nullptr);
    std::string out(n, ' ');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}
static std::string text(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return utf8(s);
}
static std::string roleLabel(const std::string &r) {
    if (r == "ADMINISTRATOR")
        return "Administrator";
    if (r == "HEAD_OF_DEPARTMENT")
        return "Head of Department";
    if (r == "LECTURER")
        return "Lecturer";
    return "Student";
}
static std::string label(std::string s) {
    static const std::map<std::string, std::string> names = {
        {"id", "ID"},
        {"full_name", "Full name"},
        {"student_id", "Student ID"},
        {"course_id", "Course ID"},
        {"assignment_id", "Assignment ID"},
        {"raw_mark", "Raw mark"},
        {"final_mark", "Final mark"},
        {"effective_deadline", "Effective deadline (UTC)"},
        {"is_active", "Active"},
        {"is_returned", "Returned"},
        {"version", "Version"},
        {"sha256", "SHA-256"}};
    auto it = names.find(s);
    if (it != names.end())
        return it->second;
    std::replace(s.begin(), s.end(), '_', ' ');
    if (!s.empty())
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}
static std::string scalar(const Json &j) {
    if (j.is_null())
        return "-";
    if (j.is_string())
        return j.get<std::string>();
    if (j.is_boolean())
        return j.get<bool>() ? "Yes" : "No";
    if (j.is_number())
        return j.dump();
    if (j.is_array()) {
        std::string out;
        for (auto v : j) {
            if (!out.empty())
                out += "; ";
            if (v.is_object() && v.contains("seq_no"))
                out += scalar(v["seq_no"]) + ": " + scalar(v["original_name"]);
            else
                out += scalar(v);
        }
        return out;
    }
    std::string out;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!out.empty())
            out += "; ";
        out += label(it.key()) + ": " + scalar(it.value());
    }
    return out;
}
struct Reply {
    int status = 0;
    Json data;
    std::string error;
    bool login = false;
    std::string path;
};
struct Input {
    Field field;
    Json choices = Json::array();
    HWND label{}, control{}, browse{};
};
class App {
  public:
    HWND window{}, sidebar{}, submit{}, notice{}, title{}, subtitle{}, table{}, logout{},
        loginUser{}, loginPassword{}, loginServer{};
    HFONT font{}, heading{}, small{}, brand{};
    HBRUSH background{}, white{}, navy{};
    std::vector<HWND> page;
    std::vector<Input> inputs;
    std::vector<Action> actions;
    std::map<int, std::map<std::string, std::string>> drafts;
    Json references = Json::object();
    Json selected = Json::object();
    std::vector<Json> rows;
    std::vector<std::string> columns;
    int actionIndex = -1;
    bool signedIn = false, busy = false, failed = false;
    std::string token, role, username, url = "http://127.0.0.1:8080";
    std::thread worker;
    std::mutex mutex;
    std::optional<Reply> pending;
    std::shared_ptr<httplib::Client> connection;
    App()
        : font(CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Segoe UI")),
          heading(CreateFontW(-27, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Segoe UI")),
          small(CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Segoe UI")),
          brand(CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Segoe UI")),
          background(CreateSolidBrush(Background)), white(CreateSolidBrush(RGB(255, 255, 255))),
          navy(CreateSolidBrush(Navy)) {
        wchar_t executable[MAX_PATH];
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        std::ifstream config(std::filesystem::path(executable).parent_path() / "server.url");
        std::string line;
        if (std::getline(config, line) && !line.empty())
            url = line;
        auto directory = std::filesystem::path(executable).parent_path();
        // Double-click starts an existing local school instance if it is stopped.
        // Remote LAN servers and first-time administrator setup stay explicit.
        const std::string local = "http://127.0.0.1:";
        if (url.rfind(local, 0) == 0 && std::filesystem::exists(directory / L"lms.db") &&
            std::filesystem::exists(directory / L"lms_server.exe")) {
            try {
                std::size_t used = 0;
                auto port = std::stoul(url.substr(local.size()), &used);
                if (used == url.size() - local.size() && port > 0 && port <= 65535) {
                    httplib::Client probe(url);
                    probe.set_connection_timeout(0, 250000);
                    probe.set_read_timeout(0, 250000);
                    if (!probe.Get("/api/student/courses")) {
                        auto server = directory / L"lms_server.exe";
                        std::wstring command = L"\"" + server.wstring() +
                                               L"\" --host 127.0.0.1 --port " +
                                               std::to_wstring(port);
                        STARTUPINFOW startup{};
                        startup.cb = sizeof(startup);
                        PROCESS_INFORMATION process{};
                        if (CreateProcessW(server.c_str(), command.data(), nullptr, nullptr, FALSE,
                                           CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup,
                                           &process)) {
                            CloseHandle(process.hThread);
                            CloseHandle(process.hProcess);
                        }
                    }
                }
            } catch (...) { /* Show connection feedback on the login form. */
            }
        }
    }
    ~App() {
        if (connection)
            connection->stop();
        if (worker.joinable())
            worker.join();
        DeleteObject(font);
        DeleteObject(heading);
        DeleteObject(small);
        DeleteObject(brand);
        DeleteObject(background);
        DeleteObject(white);
        DeleteObject(navy);
    }
    HWND control(const wchar_t *cls, const std::string &value, DWORD style, int id,
                 HFONT f = nullptr) {
        HWND h =
            CreateWindowExW(std::wstring(cls) == L"EDIT" ? WS_EX_CLIENTEDGE : 0, cls,
                            wide(value).c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f ? f : font), TRUE);
        page.push_back(h);
        return h;
    }
    void clearPage() {
        for (auto h : page)
            DestroyWindow(h);
        page.clear();
        inputs.clear();
        sidebar = submit = notice = title = subtitle = table = logout = nullptr;
        loginUser = loginPassword = loginServer = nullptr;
        rows.clear();
        columns.clear();
        failed = false;
    }
    void status(const std::string &message, bool error = false) {
        failed = error;
        SetWindowTextW(notice, wide(message).c_str());
        InvalidateRect(notice, nullptr, TRUE);
    }
    static bool recordChoice(const Field &f) {
        return f.key != "version" && (f.type == "id" || f.key == "submission_id");
    }
    static std::string chosen(const Input &in) {
        auto n = SendMessageW(in.control, CB_GETCURSEL, 0, 0);
        return n > 0 && n <= static_cast<int>(in.choices.size()) ? scalar(in.choices[n - 1]["id"])
                                                                 : "";
    }
    static void selectChoice(Input &in, const std::string &value) {
        SendMessageW(in.control, CB_SETCURSEL, 0, 0);
        for (size_t n = 0; n < in.choices.size(); ++n)
            if (scalar(in.choices[n]["id"]) == value)
                SendMessageW(in.control, CB_SETCURSEL, n + 1, 0);
    }
    void fillChoices(Input &in) {
        std::string key = in.field.key, source = key == "course_id"       ? "courses"
                                                 : key == "department_id" ? "departments"
                                                 : key == "assignment_id" ? "assignments"
                                                 : key == "submission_id" ? "submissions"
                                                                          : "users";
        in.choices = Json::array();
        if (key == "submission_id" && actions[actionIndex].title == "Release marks")
            in.choices.push_back({{"id", 0}, {"title", "All approved submissions"}});
        if (key == "sequence") {
            for (const auto &other : inputs)
                if (other.field.key == "submission_id")
                    for (auto sub : references.value("submissions", Json::array()))
                        if (scalar(sub["id"]) == chosen(other))
                            in.choices = sub["files"];
        } else
            for (auto row : references.value(source, Json::array())) {
                std::string expected = key == "lecturer_id"    ? "LECTURER"
                                       : key == "head_user_id" ? "HEAD_OF_DEPARTMENT"
                                       : key == "student_id"   ? "STUDENT"
                                                               : "";
                if (!expected.empty() && row.value("role", std::string()) != expected)
                    continue;
                in.choices.push_back(row);
            }
        SendMessageW(in.control, CB_RESETCONTENT, 0, 0);
        SendMessageW(in.control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Choose an option..."));
        for (auto row : in.choices) {
            std::string name = row.value(
                "full_name", row.value("name", row.value("title", std::string("Record"))));
            if (row.contains("code"))
                name = row["code"].get<std::string>() + " - " + name;
            if (row.contains("course_code"))
                name = row["course_code"].get<std::string>() + " - " + name;
            if (row.contains("student_id"))
                name += " / student " + scalar(row["student_id"]);
            name += " (#" + scalar(row["id"]) + ")";
            SendMessageW(in.control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(name).c_str()));
        }
        SendMessageW(in.control, CB_SETCURSEL, 0, 0);
        SendMessageW(in.control, CB_SETDROPPEDWIDTH, 520, 0);
    }
    void changedChoice() {
        for (auto &in : inputs) {
            if (in.field.key == "sequence") {
                auto value = chosen(in);
                fillChoices(in);
                selectChoice(in, value);
            }
            if (in.field.key == "version")
                for (const auto &other : inputs)
                    if (other.field.key == "submission_id") {
                        auto value = chosen(other);
                        for (auto sub : references.value("submissions", Json::array()))
                            if (scalar(sub["id"]) == value)
                                SetWindowTextW(in.control, wide(scalar(sub["version"])).c_str());
                        if (value == "0")
                            SetWindowTextW(in.control, L"0");
                    }
        }
    }
    void saveDraft() {
        if (actionIndex < 0)
            return;
        for (auto &input : inputs) {
            if (recordChoice(input.field))
                drafts[actionIndex][input.field.key] = chosen(input);
            else if (input.field.type == "role" || input.field.type == "bool" ||
                     input.field.type == "membership")
                drafts[actionIndex][input.field.key] =
                    std::to_string(SendMessageW(input.control, CB_GETCURSEL, 0, 0));
            else
                drafts[actionIndex][input.field.key] = text(input.control);
        }
    }
    void loginPage() {
        clearPage();
        signedIn = false;
        actionIndex = -1;
        title = control(L"STATIC", "Welcome to School LMS", SS_LEFT, 10, heading);
        subtitle = control(L"STATIC", "Sign in to manage coursework and assessment.", SS_LEFT, 11);
        control(L"STATIC", "Username", SS_LEFT, 12);
        loginUser = control(L"EDIT", username, WS_TABSTOP | ES_AUTOHSCROLL, 100);
        control(L"STATIC", "Password", SS_LEFT, 13);
        loginPassword = control(L"EDIT", "", WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, 101);
        control(L"STATIC", "School server", SS_LEFT, 14);
        loginServer = control(L"EDIT", url, WS_TABSTOP | ES_AUTOHSCROLL, 102);
        submit = control(L"BUTTON", "Sign in", WS_TABSTOP | BS_OWNERDRAW, 1);
        notice = control(L"STATIC", "Use the account provided by your school administrator.",
                         SS_LEFT, 15, small);
        layout();
        SetFocus(loginUser);
    }
    void shell(int index = 0) {
        clearPage();
        signedIn = true;
        sidebar = control(
            L"LISTBOX", "",
            WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT | WS_VSCROLL, 200);
        for (auto &a : actions)
            SendMessageW(sidebar, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(a.title).c_str()));
        SendMessageW(sidebar, LB_SETITEMHEIGHT, 0, 32);
        SendMessageW(sidebar, LB_SETCURSEL, index, 0);
        logout = control(L"BUTTON", "Sign out", WS_TABSTOP | BS_PUSHBUTTON, 2);
        control(L"BUTTON", "Refresh choices", WS_TABSTOP | BS_PUSHBUTTON, 3);
        title = control(L"STATIC", actions[index].title, SS_LEFT, 10, heading);
        subtitle = control(L"STATIC", actions[index].description, SS_LEFT, 11);
        table = control(WC_LISTVIEWW, "",
                        WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 300);
        ListView_SetExtendedListViewStyle(table, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                     LVS_EX_GRIDLINES);
        actionIndex = index;
        int n = 0;
        for (auto field : actions[index].fields) {
            Input input;
            input.field = field;
            input.label = control(L"STATIC", field.label, SS_LEFT, 400 + n, small);
            bool combo = field.type == "role" || field.type == "bool" || field.type == "membership";
            if (recordChoice(field)) {
                input.control =
                    control(L"COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 500 + n);
                fillChoices(input);
            } else if (combo) {
                input.control =
                    control(L"COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 500 + n);
                std::vector<std::string> options =
                    field.type == "role"
                        ? std::vector<std::string>{"Student", "Lecturer", "Head of Department",
                                                   "Administrator"}
                    : field.type == "membership"
                        ? std::vector<std::string>{"Add to course", "Remove from course"}
                        : std::vector<std::string>{"Yes - active", "No - inactive"};
                for (auto &option : options)
                    SendMessageW(input.control, CB_ADDSTRING, 0,
                                 reinterpret_cast<LPARAM>(wide(option).c_str()));
                SendMessageW(input.control, CB_SETCURSEL, 0, 0);
            } else
                input.control =
                    control(L"EDIT", field.value,
                            WS_TABSTOP | (field.type == "password" ? ES_PASSWORD : 0) |
                                (field.type == "multiline"
                                     ? ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL
                                     : ES_AUTOHSCROLL),
                            500 + n);
            if (field.type == "file" || field.type == "save")
                input.browse = control(L"BUTTON", "Browse...", WS_TABSTOP | BS_PUSHBUTTON, 600 + n);
            auto draft = drafts[index].find(field.key);
            if (draft != drafts[index].end()) {
                if (recordChoice(field))
                    selectChoice(input, draft->second);
                else if (combo)
                    SendMessageW(input.control, CB_SETCURSEL, std::stoi(draft->second), 0);
                else
                    SetWindowTextW(input.control, wide(draft->second).c_str());
            } else if (!selected.empty()) {
                std::string key = field.key;
                bool user = selected.contains("username"), course = selected.contains("code"),
                     department = selected.contains("head_user_id"),
                     submission = selected.contains("submitted_at");
                bool sameEdit = (actions[index].title == "Edit user" && user) ||
                                (actions[index].title == "Edit course" && course) ||
                                (actions[index].title == "Edit department" && department);
                bool linkedId = field.type == "id" || field.key == "version";
                if (!selected.contains(key) && selected.contains("id")) {
                    bool match = (key == "user_id" && user) || (key == "course_id" && course) ||
                                 (key == "department_id" && department) ||
                                 (key == "submission_id" && submission) ||
                                 (key == "assignment_id" && selected.contains("max_mark") &&
                                  selected.contains("deadline")) ||
                                 (key == "student_id" && user &&
                                  selected.value("role", std::string()) == "STUDENT") ||
                                 (key == "lecturer_id" && user &&
                                  selected.value("role", std::string()) == "LECTURER") ||
                                 (key == "head_user_id" && user &&
                                  selected.value("role", std::string()) == "HEAD_OF_DEPARTMENT");
                    if (match)
                        key = "id";
                }
                if (field.key == "is_active" && sameEdit && selected.contains("is_active"))
                    SendMessageW(input.control, CB_SETCURSEL, selected["is_active"] == 0 ? 1 : 0,
                                 0);
                if ((linkedId || sameEdit ||
                     (submission && actions[index].path.find("/api/submissions/") == 0)) &&
                    selected.contains(key) && !selected[key].is_structured() && !combo &&
                    field.type != "password") {
                    if (recordChoice(field))
                        selectChoice(input, scalar(selected[key]));
                    else
                        SetWindowTextW(input.control, wide(scalar(selected[key])).c_str());
                }
            }
            SendMessageW(input.control, EM_SETLIMITTEXT, field.type == "multiline" ? 10000 : 1024,
                         0);
            inputs.push_back(input);
            ++n;
        }
        changedChoice();
        submit = control(L"BUTTON",
                         actions[index].method == "GET"        ? "Refresh results"
                         : actions[index].method == "UPLOAD"   ? "Submit file"
                         : actions[index].method == "DOWNLOAD" ? "Download file"
                                                               : "Save changes",
                         WS_TABSTOP | BS_OWNERDRAW, 1);
        notice = control(L"STATIC",
                         actions[index].method == "GET"
                             ? "Choose Refresh results to load records."
                             : "Complete the fields, then save. Errors will stay on this form.",
                         SS_LEFT, 15, small);
        layout();
        InvalidateRect(window, nullptr, TRUE);
        if (inputs.empty())
            sendAction();
        else
            SetFocus(inputs[0].control);
    }
    void layout() {
        RECT r;
        GetClientRect(window, &r);
        int w = r.right, h = r.bottom;
        if (!signedIn) {
            int x = std::max(260, (w - 430) / 2), y = 110;
            MoveWindow(title, x, y, 470, 42, TRUE);
            MoveWindow(subtitle, x, y + 48, 480, 44, TRUE);
            MoveWindow(GetDlgItem(window, 12), x, y + 108, 430, 22, TRUE);
            MoveWindow(loginUser, x, y + 134, 430, 36, TRUE);
            MoveWindow(GetDlgItem(window, 13), x, y + 190, 430, 22, TRUE);
            MoveWindow(loginPassword, x, y + 216, 430, 36, TRUE);
            MoveWindow(GetDlgItem(window, 14), x, y + 272, 430, 22, TRUE);
            MoveWindow(loginServer, x, y + 298, 430, 36, TRUE);
            MoveWindow(submit, x, y + 360, 430, 42, TRUE);
            MoveWindow(notice, x, y + 420, 470, 78, TRUE);
            return;
        }
        MoveWindow(sidebar, 12, 146, 206, std::max(100, h - 232), TRUE);
        MoveWindow(logout, 24, h - 67, 180, 37, TRUE);
        MoveWindow(GetDlgItem(window, 3), 24, h - 112, 180, 34, TRUE);
        int left = 258, right = w - 30, available = right - left;
        MoveWindow(title, left, 35, available, 40, TRUE);
        MoveWindow(subtitle, left, 82, available, 45, TRUE);
        int formWidth = 350, formY = 158;
        for (auto &in : inputs) {
            MoveWindow(in.label, left, formY, formWidth, 20, TRUE);
            int height = in.field.type == "multiline" ? 66 : 34;
            MoveWindow(in.control, left, formY + 24, in.browse ? formWidth - 94 : formWidth,
                       in.field.type == "role" || in.field.type == "bool" ||
                               in.field.type == "membership" || recordChoice(in.field)
                           ? 220
                           : height,
                       TRUE);
            if (in.browse)
                MoveWindow(in.browse, left + formWidth - 88, formY + 24, 88, 34, TRUE);
            formY += height + 43;
        }
        if (inputs.empty()) {
            MoveWindow(submit, left, 142, 165, 37, TRUE);
            MoveWindow(table, left, 198, available, std::max(120, h - 292), TRUE);
        } else {
            MoveWindow(submit, left, std::min(formY + 4, h - 137), formWidth, 39, TRUE);
            MoveWindow(table, left + formWidth + 30, 158, std::max(100, available - formWidth - 30),
                       std::max(120, h - 252), TRUE);
        }
        MoveWindow(notice, left, h - 75, available, 58, TRUE);
    }
    void enable(bool value) {
        busy = !value;
        for (auto h : page)
            if (h != notice && h != title && h != subtitle && h != table)
                EnableWindow(h, value);
        SetCursor(LoadCursorW(nullptr, value ? IDC_ARROW : IDC_WAIT));
    }
    void browse(int index) {
        wchar_t path[32768] = {};
        auto current = wide(text(inputs[index].control));
        wcsncpy(path, current.c_str(), 32767);
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFile = path;
        dialog.nMaxFile = 32768;
        dialog.lpstrFilter = L"All files\0*.*\0";
        dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
        bool save = inputs[index].field.type == "save";
        dialog.Flags |= save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;
        if (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))
            SetWindowTextW(inputs[index].control, path);
    }
    void start(const std::string &method, const std::string &path, const Json &data,
               const std::string &file = "", const std::string &save = "", bool login = false) {
        if (worker.joinable())
            worker.join();
        enable(false);
        status(login             ? "Signing in..."
               : method == "GET" ? "Loading records..."
                                 : "Saving your changes...");
        connection = std::make_shared<httplib::Client>(url);
        connection->set_connection_timeout(4);
        connection->set_read_timeout(30);
        auto client = connection;
        auto session = token;
        worker = std::thread([this, client, method, path, data, file, save, login, session] {
            Reply reply;
            reply.login = login;
            reply.path = path;
            try {
                httplib::Headers headers;
                if (!login)
                    headers.emplace("Authorization", "Bearer " + session);
                httplib::Result response;
                if (method == "GET" || method == "DOWNLOAD")
                    response = client->Get(path, headers);
                else if (method == "UPLOAD") {
                    std::ifstream stream(std::filesystem::u8path(file), std::ios::binary);
                    if (!stream)
                        throw std::runtime_error(
                            "Cannot open the selected file. Choose another file and try again.");
                    stream.seekg(0, std::ios::end);
                    auto length = stream.tellg();
                    if (length <= 0 || length > 5242880)
                        throw std::runtime_error("Choose a file containing 1 byte to 5 MB.");
                    stream.seekg(0);
                    std::string bytes(std::istreambuf_iterator<char>(stream), {});
                    response = client->Post(
                        path, headers,
                        httplib::MultipartFormDataItems{
                            {"file", bytes, std::filesystem::u8path(file).filename().u8string(),
                             "application/octet-stream"}});
                } else if (method == "POST")
                    response = client->Post(path, headers, data.dump(), "application/json");
                else
                    response = client->Put(path, headers, data.dump(), "application/json");
                if (!response)
                    throw std::runtime_error("Cannot reach the school server. Check the connection "
                                             "and try again. Your entries have been kept.");
                reply.status = response->status;
                reply.data = Json::parse(response->body);
                if (reply.status != 200)
                    reply.error = reply.data.value(
                        "error", std::string("The request could not be completed."));
                if (method == "DOWNLOAD" && reply.status == 200) {
                    std::string hex = reply.data.at("hex");
                    if (hex.size() % 2)
                        throw std::runtime_error("The server returned an invalid file.");
                    std::ofstream out(std::filesystem::u8path(save), std::ios::binary);
                    if (!out)
                        throw std::runtime_error(
                            "Cannot save to this location. Choose another folder.");
                    for (std::size_t i = 0; i < hex.size(); i += 2)
                        out.put(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
                    out.close();
                    if (!out)
                        throw std::runtime_error("The file could not be saved completely.");
                    reply.data = {{"saved_file", save}};
                }
            } catch (const std::exception &e) {
                reply.error = e.what();
            }
            {
                std::lock_guard<std::mutex> guard(mutex);
                pending = std::move(reply);
            }
            PostMessageW(window, RequestDone, 0, 0);
        });
    }
    void sendAction() {
        if (busy)
            return;
        if (!signedIn) {
            username = text(loginUser);
            url = text(loginServer);
            if (username.empty()) {
                status("Enter your username.", true);
                SetFocus(loginUser);
                return;
            }
            if (text(loginPassword).empty()) {
                status("Enter your password.", true);
                SetFocus(loginPassword);
                return;
            }
            if (url.rfind("http://", 0) != 0) {
                status("Enter a server address beginning with http://.", true);
                SetFocus(loginServer);
                return;
            }
            start("POST", "/api/login", {{"username", username}, {"password", text(loginPassword)}},
                  "", "", true);
            return;
        }
        saveDraft();
        const auto &action = actions[actionIndex];
        Json data = Json::object();
        std::string path = action.path, file, save;
        for (auto &in : inputs) {
            auto value = text(in.control);
            try {
                Json parsed;
                if (recordChoice(in.field)) {
                    value = chosen(in);
                    if (value.empty())
                        throw std::runtime_error(
                            "Choose an option from the dropdown. Use Refresh choices if needed.");
                    parsed = std::stoi(value);
                } else if (in.field.type == "role") {
                    static const std::vector<std::string> roles = {
                        "STUDENT", "LECTURER", "HEAD_OF_DEPARTMENT", "ADMINISTRATOR"};
                    auto index = SendMessageW(in.control, CB_GETCURSEL, 0, 0);
                    if (index < 0 || index > 3)
                        throw std::runtime_error("Choose a role.");
                    parsed = roles[index];
                } else if (in.field.type == "bool" || in.field.type == "membership")
                    parsed = SendMessageW(in.control, CB_GETCURSEL, 0, 0) == 0;
                else {
                    if (!in.field.optional &&
                        value.find_first_not_of(" \r\n\t") == std::string::npos)
                        throw std::runtime_error(in.field.label + " is required.");
                    if (in.field.type == "password" && in.field.key != "current" &&
                        (value.size() < 8 || value.size() > 128))
                        throw std::runtime_error("Passwords must contain 8 to 128 bytes.");
                    if (in.field.type == "id" || in.field.type == "integer") {
                        std::size_t n;
                        auto number = std::stoll(value, &n);
                        if (n != value.size() || number < 0 ||
                            (in.field.type == "id" && number == 0) || number > 2147483647)
                            throw std::runtime_error("Enter a valid " + in.field.label + ".");
                        parsed = number;
                    } else if (in.field.type == "number") {
                        std::size_t n;
                        auto number = std::stod(value, &n);
                        if (n != value.size() || !std::isfinite(number))
                            throw std::runtime_error("Enter a valid number for " + in.field.label +
                                                     ".");
                        parsed = number;
                    } else
                        parsed = value;
                }
                auto marker = "{" + in.field.key + "}";
                auto position = path.find(marker);
                if (position != std::string::npos)
                    path.replace(position, marker.size(), scalar(parsed));
                else if (in.field.type == "file")
                    file = value;
                else if (in.field.type == "save")
                    save = value;
                else
                    data[in.field.key] = parsed;
            } catch (const std::exception &e) {
                status(std::string("Please correct ") + in.field.label + ": " + e.what(), true);
                SetFocus(in.control);
                return;
            }
        }
        start(action.method, path, data, file, save);
    }
    void resultTable(Json data) {
        ListView_DeleteAllItems(table);
        while (ListView_DeleteColumn(table, 0)) {
        }
        rows.clear();
        columns.clear();
        Json records = data.is_array() ? data : Json::array({data});
        for (auto item : records) {
            if (!item.is_object())
                item = {{"value", item}};
            if (item.contains("assignments")) {
                for (auto assignment : item["assignments"]) {
                    assignment["course"] = item.value("code", std::string(""));
                    assignment["course_id"] = item.at("id");
                    rows.push_back(assignment);
                }
                if (item["assignments"].empty()) {
                    item.erase("assignments");
                    rows.push_back(item);
                }
            } else
                rows.push_back(item);
        }
        std::set<std::string> keys;
        for (auto &row : rows)
            for (auto it = row.begin(); it != row.end(); ++it)
                if (it.key() != "password_hash" && it.key() != "stored_path" && it.key() != "hex")
                    keys.insert(it.key());
        for (auto key :
             {"id", "username", "full_name", "course", "title", "assignment_id", "student_id",
              "status", "effective_deadline", "raw_mark", "final_mark", "feedback", "version"})
            if (keys.erase(key))
                columns.push_back(key);
        columns.insert(columns.end(), keys.begin(), keys.end());
        for (std::size_t c = 0; c < columns.size(); ++c) {
            auto name = wide(label(columns[c]));
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = name.data();
            column.cx = columns[c] == "id"                                 ? 55
                        : columns[c].find("deadline") != std::string::npos ? 190
                                                                           : 145;
            SendMessageW(table, LVM_INSERTCOLUMNW, c, reinterpret_cast<LPARAM>(&column));
        }
        for (std::size_t r = 0; r < rows.size(); ++r)
            for (std::size_t c = 0; c < columns.size(); ++c) {
                auto value = rows[r].contains(columns[c]) ? scalar(rows[r][columns[c]]) : "-";
                if (columns[c] == "role")
                    value = roleLabel(value);
                auto cell = wide(value);
                LVITEMW item{};
                item.iItem = static_cast<int>(r);
                item.iSubItem = static_cast<int>(c);
                item.pszText = cell.data();
                item.mask = LVIF_TEXT;
                SendMessageW(table, c == 0 ? LVM_INSERTITEMW : LVM_SETITEMW, 0,
                             reinterpret_cast<LPARAM>(&item));
            }
    }
    void complete() {
        if (worker.joinable())
            worker.join();
        std::optional<Reply> reply;
        {
            std::lock_guard<std::mutex> guard(mutex);
            reply = std::move(pending);
            pending.reset();
        }
        enable(true);
        connection.reset();
        if (!reply)
            return;
        if (!reply->error.empty()) {
            status(reply->error + "  Correct the fields and try again.", true);
            return;
        }
        if (reply->login) {
            token = reply->data.at("token");
            role = reply->data.at("role");
            actions = actionsFor(role);
            drafts.clear();
            selected = Json::object();
            start("GET", "/api/reference-data", Json::object());
            return;
        }
        if (reply->path == "/api/reference-data") {
            references = reply->data;
            shell(actionIndex < 0 ? 0 : actionIndex);
            return;
        }
        if (reply->path == "/api/logout") {
            token.clear();
            drafts.clear();
            selected = Json::object();
            loginPage();
            status("You have signed out.");
            InvalidateRect(window, nullptr, TRUE);
            return;
        }
        resultTable(reply->data);
        if (reply->path == "/api/password") {
            token.clear();
            drafts.clear();
            loginPage();
            status("Password updated. Sign in with your new password.");
            return;
        }
        if (actions[actionIndex].method == "GET")
            status(
                rows.empty()
                    ? "No records found."
                    : std::to_string(rows.size()) +
                          " record(s) loaded. Select a record to use its details in another form.");
        else
            status(reply->data.contains("id")
                       ? "Saved successfully. Record ID: " + scalar(reply->data["id"])
                       : "Completed successfully. Your form remains open.");
    }
    void signOut() {
        if (busy)
            return;
        start("POST", "/api/logout", Json::object());
    }
    void paint() {
        PAINTSTRUCT p;
        HDC dc = BeginPaint(window, &p);
        RECT r;
        GetClientRect(window, &r);
        FillRect(dc, &r, background);
        RECT rail = {0, 0, 230, r.bottom};
        FillRect(dc, &rail, navy);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        SelectObject(dc, brand);
        TextOutW(dc, 24, 30, L"SCHOOL LMS", 10);
        SelectObject(dc, small);
        SetTextColor(dc, RGB(178, 197, 218));
        TextOutW(dc, 24, 65, L"Coursework & assessment", 23);
        if (signedIn) {
            auto s = wide(roleLabel(role));
            TextOutW(dc, 24, 108, s.data(), static_cast<int>(s.size()));
            auto name = wide(username);
            TextOutW(dc, 24, r.bottom - 100, name.data(), static_cast<int>(name.size()));
        } else {
            TextOutW(dc, 24, 122, L"A school day, simplified.", 25);
        }
        EndPaint(window, &p);
    }
};
static App *app = nullptr;
static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE:
        app->window = hwnd;
        app->loginPage();
        return 0;
    case WM_SIZE:
        app->layout();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize = {1100, 840};
        return 0;
    case WM_PAINT:
        app->paint();
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == 1 && HIWORD(w) == BN_CLICKED)
            app->sendAction();
        else if (LOWORD(w) == 3 && HIWORD(w) == BN_CLICKED && !app->busy) {
            app->saveDraft();
            app->start("GET", "/api/reference-data", Json::object());
        } else if (LOWORD(w) >= 500 && LOWORD(w) < 550 && HIWORD(w) == CBN_SELCHANGE)
            app->changedChoice();
        else if (LOWORD(w) == 2 && HIWORD(w) == BN_CLICKED)
            app->signOut();
        else if (LOWORD(w) == 200 && HIWORD(w) == LBN_SELCHANGE && !app->busy) {
            int index = static_cast<int>(SendMessageW(app->sidebar, LB_GETCURSEL, 0, 0));
            if (index >= 0 && index != app->actionIndex) {
                app->saveDraft();
                app->shell(index);
            }
        } else if (LOWORD(w) >= 600 && LOWORD(w) < 650 && HIWORD(w) == BN_CLICKED)
            app->browse(LOWORD(w) - 600);
        return 0;
    case WM_NOTIFY: {
        const auto *header = reinterpret_cast<NMHDR *>(l);
        if (header->idFrom == 300 && header->code == LVN_ITEMCHANGED) {
            int index = ListView_GetNextItem(app->table, -1, LVNI_SELECTED);
            if (index >= 0 && index < static_cast<int>(app->rows.size()))
                app->selected = app->rows[index];
        }
        return 0;
    }
    case WM_DRAWITEM: {
        auto *item = reinterpret_cast<DRAWITEMSTRUCT *>(l);
        if (item->CtlID == 1) {
            HBRUSH brush = CreateSolidBrush((item->itemState & ODS_DISABLED)   ? RGB(149, 169, 176)
                                            : (item->itemState & ODS_SELECTED) ? RGB(13, 84, 90)
                                                                               : Accent);
            HPEN pen = CreatePen(PS_SOLID, 1, Accent);
            auto oldBrush = SelectObject(item->hDC, brush);
            auto oldPen = SelectObject(item->hDC, pen);
            RoundRect(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right,
                      item->rcItem.bottom, 9, 9);
            SelectObject(item->hDC, oldBrush);
            SelectObject(item->hDC, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, RGB(255, 255, 255));
            SelectObject(item->hDC, app->font);
            auto caption = wide(text(item->hwndItem));
            RECT r = item->rcItem;
            DrawTextW(item->hDC, caption.c_str(), -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            if (item->itemState & ODS_FOCUS) {
                InflateRect(&r, -4, -4);
                DrawFocusRect(item->hDC, &r);
            }
            return TRUE;
        }
        if (item->CtlID == 200 && item->itemID != static_cast<UINT>(-1)) {
            HBRUSH brush =
                CreateSolidBrush((item->itemState & ODS_SELECTED) ? RGB(34, 81, 104) : Navy);
            FillRect(item->hDC, &item->rcItem, brush);
            DeleteObject(brush);
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, RGB(240, 246, 253));
            SelectObject(item->hDC, app->font);
            auto title = wide(app->actions[item->itemID].title);
            RECT r = item->rcItem;
            r.left += 13;
            DrawTextW(item->hDC, title.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(w);
        SetBkColor(dc, Background);
        SetTextColor(dc, reinterpret_cast<HWND>(l) == app->notice
                             ? (app->failed ? RGB(174, 38, 48) : Accent)
                             : Ink);
        return reinterpret_cast<LRESULT>(app->background);
    }
    case WM_CTLCOLOREDIT:
        SetBkColor(reinterpret_cast<HDC>(w), RGB(255, 255, 255));
        SetTextColor(reinterpret_cast<HDC>(w), Ink);
        return reinterpret_cast<LRESULT>(app->white);
    case RequestDone:
        app->complete();
        return 0;
    case WM_CLOSE:
        if (app->connection)
            app->connection->stop();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
#ifdef LMS_GUI_TEST
static int verifyForms(App &state) {
    Json evidence = Json::array();
    auto check = [&](const std::string &name, bool ok) {
        evidence.push_back({{"test", name}, {"passed", ok}});
        if (!ok)
            throw std::runtime_error(name);
    };
    httplib::Server stub;
    std::atomic<int> responseCode{409};
    stub.Post("/api/admin/users", [&](const httplib::Request &, httplib::Response &reply) {
        reply.status = responseCode.load();
        reply.set_content(reply.status == 200   ? "{\"id\":910}"
                          : reply.status == 401 ? "{\"error\":\"Session expired\"}"
                                                : "{\"error\":\"Username already exists\"}",
                          "application/json");
    });
    int port = stub.bind_to_any_port("127.0.0.1");
    if (port < 0)
        return 1;
    std::thread server([&] { stub.listen_after_bind(); });
    auto finish = [&] {
        stub.stop();
        if (server.joinable())
            server.join();
        std::ofstream out("gui-test-results.json");
        out << Json({{"checks", evidence}, {"total", evidence.size()}}).dump(2);
    };
    auto pump = [&] {
        auto end = GetTickCount64() + 8000;
        while (state.busy && GetTickCount64() < end) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(5);
        }
        check("Background request completes without blocking the UI", !state.busy);
    };
    try {
        for (auto role : {"STUDENT", "LECTURER", "HEAD_OF_DEPARTMENT", "ADMINISTRATOR"}) {
            auto actions = actionsFor(role);
            check(std::string(role) + " has a usable action list", actions.size() >= 4);
            check(std::string(role) + " includes password maintenance",
                  actions.back().title == "Change password");
        }
        state.url = "http://127.0.0.1:" + std::to_string(port);
        state.token = "synthetic-unit-test-session";
        state.role = "ADMINISTRATOR";
        state.username = "UI test";
        state.actions = actionsFor(state.role);
        state.shell(1);
        check("Create-user role dropdown shows four choices",
              SendMessageW(state.inputs[2].control, CB_GETCOUNT, 0, 0) == 4);
        SetWindowTextW(state.inputs[0].control, L"Kept username");
        SetWindowTextW(state.inputs[3].control, L"StrongTest123!");
        auto form = state.inputs[0].control;
        state.sendAction();
        check("Missing required field stays on create-user form",
              state.actionIndex == 1 && !state.busy && state.failed);
        check("Validation retains entered username and password",
              text(state.inputs[0].control) == "Kept username" &&
                  text(state.inputs[3].control) == "StrongTest123!");
        check("Validation does not rebuild the form", state.inputs[0].control == form);
        SetWindowTextW(state.inputs[1].control, L"Kept full name");
        SendMessageW(state.inputs[2].control, CB_SETCURSEL, 1, 0);
        state.sendAction();
        pump();
        check("Server conflict stays on the same form",
              state.actionIndex == 1 && state.failed && state.inputs[0].control == form);
        check("Server conflict shows an inline explanation",
              text(state.notice).find("Username already exists") != std::string::npos);
        check("Server conflict retains all entered fields",
              text(state.inputs[0].control) == "Kept username" &&
                  text(state.inputs[1].control) == "Kept full name" &&
                  SendMessageW(state.inputs[2].control, CB_GETCURSEL, 0, 0) == 1);
        responseCode = 200;
        SetWindowTextW(state.inputs[0].control, L"Corrected username");
        state.sendAction();
        pump();
        check("Correcting the field and retrying succeeds",
              !state.failed && text(state.notice).find("910") != std::string::npos);
        check("Successful save keeps the user's current form",
              state.actionIndex == 1 && state.inputs[0].control == form);
        responseCode = 401;
        state.sendAction();
        pump();
        check("Session error keeps the form and entered values",
              state.signedIn && state.actionIndex == 1 &&
                  text(state.inputs[0].control) == "Corrected username" && state.failed);
        state.saveDraft();
        state.shell(2);
        state.saveDraft();
        state.shell(1);
        check("Navigating away and back restores the draft",
              text(state.inputs[0].control) == "Corrected username" &&
                  text(state.inputs[1].control) == "Kept full name");
        stub.stop();
        server.join();
        state.sendAction();
        pump();
        check("Connection error keeps the form and its values",
              state.actionIndex == 1 && state.failed &&
                  text(state.inputs[0].control) == "Corrected username");
        state.drafts.clear();
        state.selected = {{"id", 23},
                          {"username", "selected.student"},
                          {"full_name", "Selected student"},
                          {"role", "STUDENT"},
                          {"is_active", 0}};
        state.references["users"] = Json::array({state.selected});
        state.shell(2);
        check("Editing a selected user fills the correct user ID",
              state.chosen(state.inputs[0]) == "23");
        check("Editing an inactive user preserves inactive status",
              SendMessageW(state.inputs[2].control, CB_GETCURSEL, 0, 0) == 1);
        state.shell(8);
        check("A selected user ID is never mistaken for a department ID",
              state.chosen(state.inputs[2]).empty());
        state.shell(11);
        check("Selected student fills enrolment student ID", state.chosen(state.inputs[1]) == "23");
        state.drafts.clear();
        state.selected = Json::object();
        state.role = "LECTURER";
        state.actions = actionsFor(state.role);
        state.references["courses"] =
            Json::array({{{"id", 7}, {"code", "CS4433"}, {"title", "Software Engineering"}}});
        state.shell(1);
        check("Lecturer has a My courses view", state.actions[0].path == "/api/lecturer/courses");
        check("Course selector has a visible dropdown arrow",
              (GetWindowLongPtrW(state.inputs[0].control, GWL_STYLE) & CBS_DROPDOWNLIST) ==
                  CBS_DROPDOWNLIST);
        state.selectChoice(state.inputs[0], "7");
        check("Course dropdown displays code and title",
              text(state.inputs[0].control).find("CS4433 - Software Engineering") !=
                  std::string::npos);
        state.saveDraft();
        state.shell(2);
        state.shell(1);
        check("Course draft retains record ID", state.chosen(state.inputs[0]) == "7");
        finish();
        return 0;
    } catch (const std::exception &e) {
        evidence.push_back({{"failure", e.what()}});
        finish();
        return 1;
    }
}
#endif
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    App state;
    app = &state;
    WNDCLASSW cls{};
    cls.lpfnWndProc = windowProc;
    cls.hInstance = instance;
    cls.lpszClassName = L"SchoolLMSDesktop";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassW(&cls);
    HWND window =
        CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName,
                        L"School LMS - Coursework & Assessment", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                        CW_USEDEFAULT, 1240, 900, nullptr, nullptr, instance, nullptr);
    if (!window)
        return 1;
#ifdef LMS_GUI_TEST
    if (std::wstring(GetCommandLineW()).find(L"--preview-form") == std::wstring::npos) {
        int testResult = verifyForms(state);
        DestroyWindow(window);
        app = nullptr;
        return testResult;
    }
    // Synthetic visual fixture, available only in the test executable.
    SetWindowTextW(window, L"School LMS - UI preview (synthetic data)");
    state.role = "ADMINISTRATOR";
    state.username = "UI preview";
    state.token = "non-authenticated-visual-fixture";
    state.actions = actionsFor(state.role);
    state.shell(1);
    SetWindowTextW(state.inputs[0].control, L"demo.student");
    SetWindowTextW(state.inputs[1].control, L"Demo Student");
    state.sendAction();
#endif
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    app = nullptr;
    return static_cast<int>(message.wParam);
}
