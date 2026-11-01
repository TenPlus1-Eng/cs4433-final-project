# Moderated Learning Management System

CS4433 - Lebohang Lebete - 202321241. C++17 Windows desktop GUI, console client and HTTP server,
SQLite persistence, libsodium Argon2id passwords. Continues the supplied week 1-3
requirements and design. Gate 2 approval was confirmed by the student in this chat.

## Build

Prerequisites: CMake 3.20+, C++17 compiler, Ninja or Make, libsodium headers and
library. SQLite, cpp-httplib, JSON and Catch2 sources are included in `vendor`.
On Ubuntu: `sudo apt-get install build-essential cmake ninja-build libsodium-dev`.
On Windows use MSYS2 UCRT64 with GCC, CMake, Ninja and libsodium installed.
From a UCRT64 shell, or PowerShell with `C:\msys64\ucrt64\bin` on PATH:

```text
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cd build
./lms_server --init-admin admin
./lms_server --host 127.0.0.1 --port 8080
```

On Windows, double-click `lms_gui.exe` for the desktop interface; see
`docs/desktop-gui.md`. Forms preserve entries and display errors inline. The Windows
launcher prefers the GUI when it is available. The console remains available:
open a second terminal in `build` and run `./lms_client http://127.0.0.1:8080`.
For double-click use, a `server.url` file beside `lms_client.exe` can hold the
server address. An explicit command-line address overrides that file. The Windows
`Start-LMS.cmd` and `Start-LMS.ps1` launchers in `scripts/` can be copied beside the
runtime executables to start the local server and client together; an optional
`server.port` file selects the local port (default 8080).
For another school PC, bind the server to its LAN address and give the client
that address. Only the server machine holds the database and submission files.
There are no default credentials; bootstrap asks for the first password.
The console shows numbered actions under each role's main menu and redraws them
after each action or error. Type `menu` or `help` at the action prompt to show the
choices again. Creating a user shows four numbered role choices; yes/no fields
also display and validate their choices. Results use readable labels.
See `docs/installation.md`, `docs/user-manual.md`, and `docs/design-updates.md`.

## Deliverables and evidence

The sibling `deliverables` folder groups reports by weeks 4, 5, 6 and 7.
Each week now includes an `implementation/` source directory, its own source ZIP,
build/test instructions and a complete weekly delivery ZIP. The weekly PDF embeds
the source ZIP as an attachment and includes an implementation supplement. Week 4
has a separately built/tested foundation target; weeks 5-6 contain the v1.0 source,
and week 7 contains v1.1 with the CR1 migration and regression tests.
`docs/schedule.md` maps each week to the assignment schedule and acceptance checks.
`tests/tests.cpp` contains Catch2 unit and integration tests. `tests/system_test.py`
launches a real server for the HTTP workflow, fault and recovery checks.
`evidence` holds actual captured results; unavailable external reviews are labelled.
`docs/traceability.csv` maps the SRS to implementation and tests.

```text
cmake -S . -B build-coverage -G Ninja -DLMS_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-coverage --parallel 2
ctest --test-dir build-coverage --output-on-failure
python tests/system_test.py --build build-coverage
python tests/coverage.py --build build-coverage
```

Runtime is offline. Building on a fresh PC requires the listed tools and libsodium;
the supplied Windows runtime packages include the necessary DLLs. Test databases
and credentials are temporary synthetic data, never school records.

## Release and submission status

Local release tags and packages are produced after their respective checks.
The GitHub Actions workflow is supplied, but a successful hosted run and repository
URL require a destination repository. Gate 3 and the three-user usability trial
require human participation. Automated acceptance scenarios do not replace those
reviews. The final report lists these limits explicitly.
