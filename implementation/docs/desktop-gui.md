# Windows desktop GUI update

The preferred Windows client is now **School-LMS.exe** (also built as lms_gui.exe), a native C++17/Win32 application.
The existing C++ HTTP server, SQLite database, permissions and assessment rules are
unchanged. This is a desktop GUI within the assignment scope; no web interface or
browser service replaces the application. The console client remains available.

## Open and sign in

Double-click School-LMS.exe in the runtime folder. It reads server.url beside the EXE.
For an already-initialized local instance it starts the local server if necessary.
Start-LMS.cmd also launches the GUI when present. The initial administrator must
still be bootstrapped once using the installation guide or Start-LMS launcher.
The GUI does not create public self-registration or grant a role automatically.

Enter your username and password, and select Sign in. The administrator, lecturer,
Head of Department and student each receive their own sidebar of available actions.
The server checks every request independently of that sidebar.

## Forms and errors

Choose an action in the left sidebar. Fill in its labelled fields and select Save
changes, Submit file or Download file. Course, assignment, user, department, submission, file, role, active-account and membership fields
use dropdown choices with visible arrows; uploads and downloads use native file-selection dialogs.
Feedback/reasons support multiple lines. Password entries are masked.

Missing/invalid input shows an inline message and focuses the relevant control.
Server validation, duplicate-record conflicts, expired sessions and unavailable
connections show feedback on the same form. None returns the user to the main
menu or clears their entries. Correct the field and retry. A successful save also
keeps the form open. Navigating away and back restores a draft within the same
session; signing out clears drafts so another user cannot see them.

Requests run on a worker thread. Controls are temporarily disabled during the
request, while the window can still redraw and close. Cancelled file dialogs leave
the form alone. A session-expiry message retains the draft; use Sign out and sign
in again before continuing. Drafts are kept in memory only, not across app restarts.

## Records and record IDs

View actions load a readable table with column headings, selection and horizontal
scrolling. Student course/assignment results are flattened into assignment rows.
File history is shown as sequence/name pairs. Select a relevant record before
opening an edit/mark form to prefill compatible IDs and values. IDs are matched
by entity type, so a selected user ID is not used as a department ID. An inactive
user remains inactive when opening their edit form. Existing draft entries take
precedence over new selection hints. Check the current version before saving marks.

## Implementation and verification

`src/gui.cpp` implements the native window, controls, async requests, results table,
form validation and retained drafts. `include/lms/gui_actions.hpp` describes each
role's actions and fields. `src/desktop.rc` and `src/desktop.manifest` enable themed
Windows controls. CMake builds lms_gui on Windows without an additional GUI toolkit.

The lms_gui_forms CTest target exercises the actual form objects using a synthetic
HTTP stub. It checks required-field failures, unchanged control handles, retained
username/password/role values, server conflict, correction/retry success, expired
session, connection failure, draft restoration and safe record prefill. These are
automated UI-component checks, not a claim that the three-person usability trial
has happened. The login layout was inspected; further desktop visual verification was stopped with Escape.
The backend Catch2 suite and 90 HTTP/console checks continue to pass.

Build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`, then
`cmake --build build --parallel 2` and `ctest --test-dir build --output-on-failure`.
GUI form checks use loopback networking on Windows. Linux retains the console build.

## Lecturer courses and record choices

Lecturers open **My courses** to see the course code, title, department and assignment count
for courses assigned to them. Unassigned courses are excluded by the server.
In **Create assignment**, select the course by code and title from the Course dropdown.
If no course is listed, an administrator must use Assign lecturer first.

All record choices use arrow dropdowns rather than handwritten IDs. Account choices
are filtered by required role; assignment and submission choices follow the signed-in
user's course scope. Students can select only their own submission files. Submission
choices automatically fill the current version. Select Refresh choices after records
are created or changed by another user, or before retrying a version conflict. Existing
form drafts retain record IDs when choices refresh. Empty lists remain explicit choices;
records are never silently substituted. The server still validates every selection.

GET /api/lecturer/courses and GET /api/reference-data provide scoped lookup data.
Reference-data submission objects contain IDs, titles, versions and file names, without
marks, feedback or stored file paths; unreleased results stay protected.
