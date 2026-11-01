# Week 7 implementation

v1.1 source with CR1 class-wide extensions, effective-deadline display, migration, regression tests and upgrade evidence.

This is a retrospective weekly work package prepared from the tested release snapshots. It is not a claim of a historical week-specific Git commit. All source dependencies except the compiler/build tools and libsodium development files are vendored. The Windows release runtimes include their runtime DLLs.

Current usability update: all role menus show numbered actions; Create user lists four role choices; yes/no fields validate choices; results use readable labels. server.url and Start-LMS launchers support double-click use. The latest HTTP/console suite passes 90 checks.

Native desktop GUI: C++17/Win32 client, role sidebar, forms, results table and background requests. Errors retain the current form and entries. Desktop form test target passes 28 checks. The console and backend remain available.

Later usability revision: native Windows GUI, retained forms, lecturer My courses and named record dropdowns.
