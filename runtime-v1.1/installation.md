# Technician installation and recovery guide

Current Windows interface: after the one-time administrator setup, double-click
`lms_gui.exe`, or use `Start-LMS.cmd` to start the local server and GUI together.
The GUI reads `server.url` beside its executable. See desktop-gui.md for the form
and error-recovery workflow. The console instructions below remain a fallback.

## Windows runtime installation

1. Extract the chosen Windows release ZIP to a local writable folder on the server.
   Keep the EXEs, DLLs and schema.sql together. Use a Windows 64-bit PC.
2. Open PowerShell in that folder. Run `./lms_server.exe --init-admin admin`.
   Set a unique password of 8-128 bytes. This creates lms.db and the first account.
   Bootstrap is refused once any accounts exist. Password input during bootstrap
   is visible; perform it privately. Normal client passwords are hidden.
3. Run `./lms_server.exe --host 127.0.0.1 --port 8080` for a one-PC demonstration.
   In another terminal run `./lms_client.exe http://127.0.0.1:8080`.
4. For the school LAN, use the server's actual LAN IP in `--host`, permit that port
   only on the private school network, and point client PCs to that IP. Copy the
   client EXE and DLLs to the client PCs. Never share the database folder.
5. Sign in as administrator. Create the HoD and lecturer accounts, department,
   course and student accounts, then assign lecturers and enrol students. Role
   codes are STUDENT, LECTURER, HEAD_OF_DEPARTMENT and ADMINISTRATOR.
6. Verify a synthetic submission, marking, moderation and release before loading
   any real school data. Arrange the supervisor Gate 3 review and usability trial.

The service defaults to loopback. Plain HTTP follows DA2 of the previous design;
use only an isolated demonstration/school LAN until TLS is added and reviewed.
The current release does not claim encrypted network transport. Restrict OS access
to the database, submissions and backup folders. Use school-approved disk and
backup encryption. Never expose the port to the internet.

## Build from source

Install GCC, CMake, Ninja and libsodium for the same architecture. On MSYS2 UCRT64,
the corresponding packages are mingw-w64-ucrt-x86_64-gcc,
mingw-w64-ucrt-x86_64-cmake, mingw-w64-ucrt-x86_64-ninja and
mingw-w64-ucrt-x86_64-libsodium. Run the README build commands. The supplied
SQLite 3.45.1, cpp-httplib 0.18.1, JSON 3.11.3 and Catch2 2.13.10 are pinned to
make the coursework baseline reproducible; review security updates before real
deployment. Linux builds use system libsodium and the vendored remaining sources.

## Backup and restore

Stop the running server first, keeping clients disconnected. From its runtime folder:

```text
./lms_server --backup backup-01
./lms_server --db restored.db --files restored-files --restore backup-01
```

The backup folder contains database.sqlite and files/. SQLite's backup API copies
the database; the command also copies every referenced file, including history.
Restore refuses an existing target database. Use new, empty destinations for both
the database and file folder. Test the restored copy before switching production
paths. Starting the server sweeps unreferenced files with managed hash names.
Keep each runtime's database and file folder paired; never reuse a live folder
with an unrelated restored database. Offline backup is a deliberate operational
restriction for a consistent database-plus-files snapshot.

Run `PRAGMA integrity_check` with a SQLite tool, log in to the restored instance,
download a sample binary submission and compare its SHA-256. Retain backups under
the school's approved retention and access policy, and periodically rehearse
recovery. The automated system test exercises both commands and rollback after
an interrupted transaction. It does not prove recovery from every hardware failure.

## v1.1 upgrade

Stop v1.0, back up the whole instance, replace binaries and licences with v1.1, and
start with the same database and files paths. The migration creates the new
class_extension table without changing existing rows. Re-run the synthetic smoke
workflow. Roll back by restoring the complete v1.0 backup into fresh paths and
running the v1.0 package; do not try to remove columns or records by hand.
