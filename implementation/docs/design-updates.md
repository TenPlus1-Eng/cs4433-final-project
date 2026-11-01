# Continuation of section 5: implementation decisions

The prior PDFs remain the design baseline. No framework or web interface replaces
the C++17 console/LAN design. The original SQL schema is extracted verbatim from
week 3, excluding PDF page headers. Four User subclasses, UserFactory, penalty
Strategy, SubmissionLifecycle and named services retain the documented roles.

DA1 is implemented: extensions on MARKED, MODERATED or RELEASED work are refused.
DA2 remains an isolated-LAN HTTP limitation, explicitly recorded in installation.
DA3 uses finite double marks, calculates penalties before display and persists
raw/final values. Values in the console JSON are not rounded in storage.

Implementation refinements relative to Figures 5.4/5.5:

* Aggregate repository interfaces are consolidated as IRepository with fixed
  Query and Command identifiers. SqliteDatabase is the concrete DAO. Services
  supply bound values, never SQL strings. This reduces boilerplate but loses some
  interface segregation and typed domain checking; it is an explicit fidelity
  deviation rather than a claim that the original narrow interfaces were built.
* One recursively locked connection serialises both reads and writes rather than
  implementing the proposed read pool. Every multi-step mutation uses BEGIN
  IMMEDIATE with rollback. This simplifies correctness; performance is measured,
  not inferred from the proposed architecture. Separate file writes are cleaned
  up on rollback and a startup sweep removes interrupted-write orphans.
* User scope and shared deadline resolution sit in Service. Specific services keep
  their named responsibilities. File storage is injected through IFileStore;
  IClock and IPenaltyPolicy are also injectable. Tests use a fixed clock and memory
  files, with real SQLite for repository integration.
* A condition-variable gate permits at most two Argon2id operations, retaining the
  64 MiB interactive memory setting. A striped per-account lock prevents failed
  login count races. Eight HTTP workers limit queued request execution.
* File retrieval is an authenticated extension to the API. Multipart uploads carry
  binary input; a hex JSON field carries downloads without UTF-8 corruption. Hex
  doubles download payload size, an accepted simplicity trade-off for 5 MB files.
* Administrative role changes are not exposed; edit changes name/activation and
  separate password reset revokes sessions. Bootstrap is local and first-run only.
* Single release uses submission_id/version in the assignment release request.
  Batch release skips ineligible rows and returns their IDs, as UC17 specifies.
* Backup and restore include submission files as well as the database. Stop the
  service for a consistent snapshot; live database-only backup would be incomplete.

These decisions must be discussed in the viva, especially the repository/read-pool
deviations. They do not retroactively alter the supervisor's approved design PDF.
The adjacent UML source documents the implemented component and deployment views.
