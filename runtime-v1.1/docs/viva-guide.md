# 15-20 minute viva demonstration

Use synthetic accounts only. Start a clean instance and keep the test evidence open.

1. Explain the stakeholder conflict: lecturer immediate release versus principal
   moderation. Demonstrate the hidden student result before HoD release.
2. Show the four polymorphic User subclasses and UserFactory; explain why a client
   menu is not enough and demonstrate a server-side scope rejection.
3. Submit, resubmit and download a binary file. Show history, checksum and UTC receipt.
4. Mark a late submission; calculate a 50 maximum / 40 raw / 26-hour delay by hand:
   two days, 10 marks penalty, 30 final. Explain the 72-hour-plus-one-second edge.
5. Return for re-marking, adjust a raw mark, approve, release and show audit old/new
   values. Explain the transaction and optimistic version counter.
6. Show only Argon2id encoded hashes, never passwords. Explain salt, the two-hash
   memory gate, lockout and transient session tokens.
7. Compare v1.0 and v1.1. Demonstrate a class extension and a longer individual
   extension, then explain why class extensions after marking are rejected.
8. Run Catch2 and show coverage, the HTTP results, defect log and Cppcheck output.
   Explain why 95% line coverage does not prove every business condition is correct.
9. State the measured login shortfall, unmeasured target hardware/memory/usability,
   plain HTTP limitation and repository/read-pool design deviations candidly.
10. Show the weekly schedule, release tags and installation/recovery instructions.

Be ready to explain the single-value JSON binding defect, byte-safe download fix,
rollback, why backup includes files, why no network share opens SQLite, and how
the CR1 schema migrates an existing instance without changing released marks.
The author must review and understand the supplied continuation before submission.
