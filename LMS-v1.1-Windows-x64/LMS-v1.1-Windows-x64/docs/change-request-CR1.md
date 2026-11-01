# CR1 - class-wide extension (release v1.1)

Request: a lecturer can extend a deadline for a whole class, and students see
extensions in their assignment list. Classification: adaptive/enhancement
maintenance, primarily perfective, because it expands an already functioning
individual-extension feature to a new school workflow rather than fixing a defect.

## Approved-baseline impact

| Artefact | Impact |
|---|---|
| FR11 / UC08 | Retain individual extension; add FR11a / UC08a class extension |
| FR14 / UC12 | Show effective deadline plus class and individual extension details |
| BR1 / UC20 | effective = max(base, class extension, individual extension) |
| BR10 / DA1 | Re-evaluate unmarked submissions; refuse class extension if any submission is already marked |
| Figure 5.1 components | No new process; existing ExtensionService and DAO gain operations |
| Figure 5.3 domain | Add ClassExtension associated 0..1 with Assignment and grantedBy User |
| Figures 5.4 / 5.5 | grantClassExtension; ClassExtension query/command; shared deadline resolver |
| Figure 5.6 state | Class extension may move Late -> Submitted, like an individual extension |
| Figure 5.7 activity | Extension choice precedes deadline resolution; existing marking path unchanged |
| Figure 5.8 sequence | Resolve three deadline sources before submit/resubmit |
| Figure 5.9 sequence | Penalty recomputation uses shared resolver; no change after marking |
| Figure 5.10 / SQL | New class_extension table, assignment_id PK/FK, deadline, reason, actor, timestamp |
| Source | repository.hpp/.cpp, services.hpp/.cpp, server.cpp, client.cpp, schema.sql, migration-v1.1.sql |
| Tests | T15 precedence and visibility; T16 state, scope and atomic refusal; S21 HTTP visibility; all T01-T14 and S01-S20 rerun |
| Documentation | Student/lecturer manuals, installation migration, release notes, traceability and this supplement |

FR11a: an assigned lecturer may set a class deadline later than the current class
deadline, with a non-blank reason. FR14a: each enrolled student sees base deadline,
optional class extension and individual extension reason/deadline, and effective
deadline. These are Must requirements for CR1 and verified by T15/T16/S21.

The class extension applies to the assignment, not to a frozen list of enrolments;
newly enrolled students therefore receive it. Individual extensions are preserved.
A shorter class extension never takes away a longer individual accommodation.
A later class extension supersedes an earlier class extension. No user sets the
penalty directly. Marked work cannot acquire a new deadline retrospectively; the
whole class operation is refused atomically if any submission is already marked.
This preserves DA1 and avoids silently changing completed assessment snapshots.
The school should review that policy if post-marking class extensions are required.

The new table is 3NF: assignment_id determines deadline, reason and actor/time.
It avoids rewriting assignment.deadline and keeps the original baseline visible.
Startup runs an idempotent CREATE TABLE IF NOT EXISTS migration for existing v1.0
databases. Backup and restore automatically include the new table. The migration
does not alter existing submissions, marks or audit entries. The upgrade test
uses an actual v1.0 database, not only the fresh v1.1 schema.

Estimated change scope is one new persistent entity, two repository operations,
one service method, one endpoint, one console action and two display fields.
The release is isolated after tag v1.0; it does not modify that tagged baseline.
