# Release v1.1 - class extension maintenance

Adds CR1: assigned lecturers can grant a class extension with a reason; student
assignment lists show the class and individual extension details and effective
deadline. The latest applicable deadline wins. A longer individual extension is
preserved. Class extension reclassifies unmarked submissions and is refused
atomically once any submission is marked. Original penalty/moderation rules remain.

Existing databases receive the idempotent class_extension migration at startup.
Keep migration-v1.1.sql beside schema.sql. Stop the server and take a complete
backup first. The upgrade check uses an actual v1.0-created instance and verifies
preserved data, the new table, repeated migration and integrity.

Validation: 16 Catch2 cases / 235 assertions; 81 optimized-release HTTP and console
checks; business line coverage 96.07% (513/534 lines); Cppcheck configured
warning/performance/portability checks with no diagnostics. The original fourteen
Catch2 cases and system workflow are included in the regression run. Full evidence
is in the evidence directory and final report appendices.

Known limits carried forward: v1.0 local login p95 2.5528 s exceeds the 2 s target;
target hardware/memory/offline-interface certification and three-user usability
trial remain pending. Gate 3 requires supervisor review. No TLS; isolated-LAN
demonstration only. FR28 notification remains deferred. GitHub publication needs
write access/sign-in, so no hosted CI pass is claimed. The optimized GCC 16 build
also emits a warning inside the pinned SQLite 3.45.1 source; it is recorded as a
third-party review item, not silently classified as resolved first-party code.
