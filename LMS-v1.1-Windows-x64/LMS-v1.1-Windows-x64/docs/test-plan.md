# V-Model test plan

| Level | Design artefact verified | Cases | Entry | Exit |
|---|---|---|---|---|
| Unit | Penalty Strategy, role hierarchy, UTC rules and lifecycle | T01-T03 | Approved design; deterministic clock | All boundary/permission checks pass |
| Integration | Schema, repository, service collaboration, audit transactions | T04-T14 | Build succeeds with SQLite and libsodium | No failed cases; business line coverage >=70% |
| System | Client/server interfaces and UC13, UC10, UC17 including exceptions | S01-S20 with named subchecks | Integrated executable and temporary synthetic instance | HTTP workflow, 30 invalid/fault inputs, binary round trip and recovery pass |
| Acceptance | FR01-FR27 stakeholder workflow; usability NFR10 | Automated role scenarios plus human trial form | System tests pass, school representative available | Supervisor Gate 3 and three non-technical users sign off |

Use equivalence partitions: on-time versus late, enrolled versus outsider, each role,
valid versus invalid mark/file/reason, each lifecycle state, current versus stale
version. Boundary values are deadline-1s, deadline, +1s, +24h, +24h+1s, +48h,
+48h+1s, +72h and +72h+1s. Mark boundaries include negative, zero, maximum and
above maximum, fractional maxima and non-finite values. Files test zero, exact 5 MB
and 5 MB+1 byte. Extension boundaries include equal, earlier and later deadline;
session and lockout tests advance the injected clock to the exact expiry instant.

Record test output before changing the release. Coverage is executable source
lines in domain.cpp and services.cpp only, excluding vendor, API, repository and
tests. This scope matches business-logic NFR12; do not present it as whole-project
coverage. CI runs the same suites, then static analysis and gcov aggregation.
Load tests are a separate workload; loopback figures do not certify the stipulated
two-core/4 GB school machine or the external network disabled condition.

Defect evidence is in defects.csv. Initial red tests are retained as described
failures rather than represented as historical weekly runs. Fixed defects are
closed only after the relevant rerun. Human acceptance and external CI remain
pending where the required people/repository are unavailable.

Human trial: give three first-time non-technical lecturers only the role manual,
ask each to create an assignment, time the action, record errors and accessibility
feedback, and obtain consent before recording names. Pass is <=3 minutes each and
all role actions reachable within three menu selections. Record actual timings;
no participants or signatures are fabricated in this delivery.
