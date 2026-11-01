# Self-review record

Performed during the continuation, not an independent peer review.

* Role menu restrictions also exist in services; lecturer course, HoD department
  and student ownership checks are exercised by T08 and S04/S06.
* All user values are bound in a statement RAII wrapper. Schema and transaction
  control contain no user values. Twenty injection strings cannot authenticate
  or change data (T12). A scalar binding defect was found and fixed (D01).
* Mark mutations and audit writes share one transaction. Audit triggers prevent
  update/delete. No service exposes deletion. Stale versions are rejected.
* Raw/final marks are withheld from students until release. Returned work cannot
  be approved until re-marked. Penalties are server-derived (T04-T08).
* Argon2id uses libsodium, random salts and verification API. Sessions are random,
  transient, idle-expiring and revoked by password reset/deactivation. Hashing
  concurrency was corrected before performance measurement (D05).
* Files are size-limited, use server-generated paths and preserve history.
  Rollback removes newly created files; startup removes crash orphans. Binary
  transport was corrected after review (D02).
* Backup was reviewed as a database-plus-files operation; technician instructions
  require a stopped server and fresh restore destinations.
* Cppcheck found uninitialised members; both were corrected (D03). Dense initial
  formatting was replaced by a committed clang-format policy (D04).
* Remaining limitations: broad JSON repository abstraction, serialised reads,
  plain LAN HTTP, structured JSON output, no realised FR28 notification, no
  independent peer review, no human usability sign-off and no hosted CI evidence.

Future review priorities are TLS, typed narrow repositories, backup locking,
usability with school staff, sustainable retention and target-hardware load tests.
