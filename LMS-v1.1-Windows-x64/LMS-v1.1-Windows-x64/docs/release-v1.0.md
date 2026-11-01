# Release v1.0 - baseline coursework implementation

C++17 server and console client, SQLite schema from week 3, all four roles,
Argon2id authentication, account lockout and session expiry, course administration,
individual extensions, on-time and late submissions, file history, automatic
penalties, marking, moderation, adjustment, return, release and append-only audit.
Includes CMake, vendored headers, Catch2 tests, HTTP tests, CI definition, deployment
UML source, technician instructions and role manuals.

Validation on this Windows x64 workstation: 14 Catch2 cases / 219 assertions;
77 HTTP/system checks; business line coverage 95.77%; Cppcheck warning/performance/
portability analysis with no remaining diagnostics. Synthetic load used 800 students,
50 staff and 50 concurrent users: listing p95 0.0091 s, login p95 2.5528 s, 800
submissions in 44.342 s with 800 verified checksums and SQLite integrity ok.

Known open requirement: NFR01's 2 s login p95 target failed this local measurement.
The target two-core / 4 GB hardware, memory limit, disconnected external interface
and human usability trial are not certified. Gate 3 is pending supervisor review.
Plain HTTP is an isolated-LAN limitation (DA2). Consolidated repository and serialised
reads differ from Figure 5.5; see design-updates.md. FR28 (Could priority login
notification) is not implemented. These are not silently marked as acceptance passes.

This is a reviewable coursework baseline and demonstration package. Arrange Gate 3
and the listed measurements before operational school use. There is no claim of
earlier daily Git history, independent review or a hosted CI run before publication.
