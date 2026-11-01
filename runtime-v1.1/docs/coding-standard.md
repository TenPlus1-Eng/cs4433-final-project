# Coding and configuration standard

Use C++17, four-space indentation, a 100-column target and the committed
`.clang-format`. Classes use PascalCase, methods camelCase, members a trailing
underscore, SQL names lower snake_case. Prefer RAII and scoped locks; never own
a raw allocation without a destructor. SQLite statement objects finalise on exit.
Only repository.cpp contains application SQL. Bind every data value; fixed schema
DDL and transaction-control SQL are not user input. Server handlers translate
typed exceptions; generic errors must not expose SQL, file paths or passwords.

Services enforce role and record scope. The client may offer role menus, but those
menus are never a security boundary. UTC timestamps are validated by round trip.
Validate finite marks, positive maxima, required feedback and reasons, file size
and original filename before committing. Mark and audit updates share a transaction.
Do not weaken password hashing to improve benchmark results silently.

Use a feature branch for each future change, run the suites, review the diff and
merge by pull request when a remote is available. Main is the reviewed baseline.
Tag release commits, not working directories. Keep build outputs, credentials,
databases, student files and generated coverage counters out of Git. Vendored
sources retain their licences and pinned versions. Current local commits capture
this continuation; they do not reconstruct an earlier seven-week history.

CI builds and tests each push and pull request on Ubuntu. Local Windows results
are evidence of this machine only; the workflow definition alone is not a hosted
CI pass. Record a real run URL after pushing to the chosen repository.
