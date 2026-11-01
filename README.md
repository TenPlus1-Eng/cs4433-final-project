# Week 7 - implementation included

v1.1 source with CR1 class-wide extensions, effective-deadline display, migration, regression tests and upgrade evidence.

## Contents

- `implementation/`: actual C++17 source, SQLite schema, CMake, dependencies and tests.
- `implementation-week-7.zip`: the same source as a portable archive.
- `202321241-week-7.pdf`: weekly report, implementation supplement and embedded source ZIP attachment.
- `202321241-week-7.md`: editable report and implementation supplement.
- Windows release ZIP and runtime folder: run the included server/client launchers after reading installation.md.

## Build and test

Install CMake 3.20+, a C++17 compiler, Ninja and libsodium headers/library.
On Windows put `C:\msys64\ucrt64\bin` on PATH; on Ubuntu install
`build-essential cmake ninja-build libsodium-dev`.

```text
cd implementation
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

In the build folder run `lms_server --init-admin admin`, set a password, then
start `lms_server`. In another terminal run `lms_client http://127.0.0.1:8080`.
Use the role manuals to create users, courses and enrolments before assessment.

The later release evidence is labelled by version. Human Gate 3/usability and
target-machine checks remain outstanding; the measured v1.0 login p95 of 2.5528 s
exceeded its 2 s target. Publishing to GitHub still requires authenticated write access.

## Current usability update

Double-click runtime-v1.1/Start-LMS.cmd to start the server and client. Create user displays 1 Student, 2 Lecturer, 3 Head of Department and 4 Administrator. Every main menu lists its actions, and menu/help redraws it. The latest implementation and runtime include these changes; 90 HTTP/console checks pass.

## Native desktop GUI

Double-click runtime-v1.1/School-LMS.exe, or Start-LMS.cmd. The server must be initialized once; the GUI auto-starts an existing local instance. Errors stay on the same form and retain entries. See implementation/docs/desktop-gui.md. Native form tests: 28 checks passed. Computer-use visual verification was stopped with Escape; no further desktop interaction was performed.
