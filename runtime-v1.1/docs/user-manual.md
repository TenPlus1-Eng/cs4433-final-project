# Role manuals

Current Windows interface: open **lms_gui.exe** and choose an action in the role
sidebar. Forms use dropdowns and labelled fields. Errors remain on the current
form with the entered values preserved; correct them and select Save again.
See desktop-gui.md for the GUI workflow. Numbered-menu instructions below describe
the retained console fallback and the same underlying role responsibilities.

All timestamps are UTC, entered as YYYY-MM-DDTHH:MM:SSZ. Lesotho local time is
UTC+02:00: a local noon deadline is 10:00:00Z. The server supplies submission time.
Do not use a student's PC clock to decide whether work is late. The numbered role
menu is shown after login; select an action and answer its labelled prompts.
Numeric results include record IDs needed by later actions. The current client
prints structured results; practise with a test course before handling real marks.

## Administrator

Choose Create user and enter username and full name, then choose the displayed
role: 1 Student, 2 Lecturer, 3 Head of Department or 4 Administrator. Enter the new
password when prompted. Invalid role choices show the four options again.
Create a HoD before creating its department. List users to retrieve IDs. Create
department with that head_user_id, then create course with its department_id.
Assign/remove lecturer accepts lecturer_id and add=yes to assign, add=no to remove.
Enrol/remove student works the same way. List departments and courses to find IDs.
Edit/deactivate user changes full name and active status; use is_active=no to
deactivate. Accounts with assessment records are retained. You cannot deactivate
your own administrator account. Roles are fixed at creation in this release;
create an appropriately scoped replacement account when a person's duties change.
Reset password revokes that user's sessions. Never send passwords in coursework
screenshots. A department has one HoD and a course may have several lecturers.

## Lecturer

Choose Create assignment, supply the assigned course ID, title, description,
UTC deadline and maximum mark greater than zero. Choose Grant individual extension,
supply assignment and student IDs, a later deadline and a reason. The student must
be enrolled. You cannot extend a submission already marked or moderated.
List submissions shows each submission's ID, status, version, effective deadline,
and file history with sequence numbers. Download the required file using the
submission ID and sequence. Choose a new local output filename.
Mark/re-mark asks for the displayed version, raw mark, written feedback and reason.
The initial mark uses the audit reason Initial mark; a later change requires your
reason. Enter raw marks only: the server calculates penalties. Reload after a
stale-version message. Returned work stays MARKED with is_returned=1 and a reason;
re-marking clears that flag. Moderated and released work cannot be changed.
In v1.1, Grant class extension is an additional menu action. Enter assignment ID,
new class deadline and reason. It is refused if assessment has already started.

## Head of Department

Choose Department submissions to inspect all assessment records in your department.
Read the raw and final marks, feedback and returned flag. Approve mark uses the
submission ID and current version. Adjust and approve additionally asks for a raw
mark and mandatory reason; it records old and new marks in the audit trail.
Return for re-marking asks for a reason. Returned submissions must be re-marked
before approval. Choose Release assignment and supply the assignment ID; use
submission_id=0 and version=0 to release every eligible moderated result, or a
specific submission ID with its displayed version for an individual release.
The reply lists released and skipped IDs. Skipped work remains hidden from students.
View mark audit shows the actor, timestamp, old/new raw and final marks and reason.
No menu edits or deletes audit entries. Other departments' data are inaccessible.

## Student

Choose My courses and assignments to see enrolled courses, assignment IDs and
effective UTC deadlines. In v1.1 the list also identifies class and individual
extension deadlines and reasons. Choose Submit/resubmit file, enter assignment ID
and a readable local file path. The file must contain 1-5,242,880 bytes. The receipt
shows server time, status, sequence and SHA-256. Retain the receipt if a query arises.
You may replace an on-time, unmarked submission before or exactly at the effective
deadline. Earlier files remain in history. A late first submission is accepted;
a late replacement is refused. My results shows only submission status until the
HoD releases the result, then shows raw mark, days late, penalty, final mark and
feedback. Ask the lecturer to correct enrolment or extension issues through school
procedures; do not alter the computer clock or another student's account.

## Errors and passwords

Invalid login uses a generic message. Five failed attempts lock the account for
five minutes. Fifteen minutes without activity ends the session. Change my password
asks for current and replacement passwords and logs you out. A reset or deactivation
also revokes sessions. For an out-of-scope message, check the course/department and
contact the administrator. For a conflict, reload the list and use its latest
version. For storage errors, stop repeated writes and ask the technician to check
disk space and database health. Do not paste password or learner records into logs.
