# Closed Loop Feedback Management System

Team **Void walkers** - B.Tech CSE. Complete build of the Phase 2 proposal, including every item that was listed as pending.

Students report problems at any point in the term. The system groups similar reports, works out how many students in which classes are affected, assigns a starting priority that rises with age, opens a ticket at **Critical**, routes it to **Faculty** (Teaching) or **Staff** (everything else), and then asks **every** reporting student whether the problem is really solved. An issue is **Resolved only when all of them say yes**. If students stay silent it is closed as **Closed (No Response)**, never as Resolved.

## Quick start

Requires only `g++` (C++17), `make`, and `python3` (for the end-to-end test). Crow, standalone Asio and nlohmann/json are vendored in `third_party/`, so no downloads are needed.

```bash
make                 # builds feedback_server, feedback_console, run_tests
make test            # 136 unit/integration checks
./feedback_server --demo          # web app on http://127.0.0.1:8080
./feedback_console --demo         # console version (same engine)
python3 tests/smoke_http.py http://127.0.0.1:8080 <admin password>   # end-to-end API test on a FRESH data file
```

On first run a moderator account `admin` is created. Its password is taken from `FB_ADMIN_PASSWORD` or generated and printed once. `--demo` adds sample classes plus `faculty1`, `staff1`, `student1`..`student6` (password `Demo@1234` - **demo only**).

Server options: `--port 8080 --host 127.0.0.1 --data data/store.json --web web --demo`. It binds to localhost by default; put it behind an HTTPS reverse proxy if exposing it.

### Demo walk-through (about 3 minutes)
1. Log in as `student1`, `student2`, `student3` (different classes) and report *"The projector in room 204 is not working"* in similar words. They are **grouped into one issue** and priority rises with each distinct student.
2. Log in as `staff1`: the issue shows class impact (students, classes, % of each class) and a transparent score breakdown, with no names or IDs.
3. Log in as `admin` -> **Admin** -> *Advance days* by 18 to show **ageing**: priority climbs to Critical and a **ticket is created automatically**. (A report containing words like "fire" or "harassment" is Critical immediately.)
4. As `staff1`: take the ticket, add a note, press *Work done*. All three students are asked to confirm.
5. Two students say yes - the issue stays **Waiting**. One says no with a comment - it goes back to **In progress**. After the fix, all three say yes -> **Resolved**.
6. Repeat and let students stay silent, then advance 7 days (reminder) and 7 more (**Closed (No Response)**).
7. Submit a rude report: it is **held for the moderator** (Moderation tab) and is not processed until approved.

## Proposal item -> where it lives

| Phase 2 pending item | Implementation |
|---|---|
| Grouping of similar reports (synonyms, tuned threshold) | `src/text.cpp` `canonicalTokens`/`similarity`; `Service::admitReport`. Threshold 0.40 in `Config`. |
| Automatic priority, Critical level, class enrollment | `Service::computePriority`; enrollment table via `setEnrollment` |
| Ageing and escalation | `Service::refreshPriority`, `tickLocked` (+1 point per 3 days, up to +6) |
| Tickets (creation, assignment, progress, notes) | `createTicket`, `claimTicket`, `addTicketNote`, `completeTicket` |
| Confirmation by every reporter, 7-day reminder, no-response closure | `Service::confirm`, `completeTicket`, `tickLocked` |
| Analytics and class-impact view | `Service::analytics`, `impactJson`; charts in the web dashboard |
| Role permissions, login, authentication | `requireRole`, `requireTeamFor`, `login` (salted iterated SHA-256, sessions, lockout) |
| Abusive-comment detection, flagging, moderator approval | `detectAbuse`, `moderationQueue`, `moderate` |
| File-based storage | `src/store.cpp` (atomic JSON writes, corrupt-file protection) |
| Crow REST API, web portal, faculty/admin dashboard | `src/server.cpp`, `web/` |
| In-app notifications | `Service::notify*`; bell and Notifications tab |
| Automated tests | `tests/test_all.cpp` (136 checks), `tests/smoke_http.py` (51 checks) |

The Phase 2 containers `ScratchVector` and `ScratchPriorityQueue` are kept in `src/ds.h` and used to order issues by priority. The original prototype is preserved in `legacy_phase2/`.

## Rules, exactly as implemented

**Priority score** = students (2-4: +1, 5-9: +2, 10+: +3) + classes (2-3: +1, 4+: +2) + largest % of a class (25%: +1, 50%: +2) + keywords (safety word such as fire/harassment/injury/unsafe: +6; exam/marks/fee/deadline etc.: +1) + age (+1 per 3 open days, capped at +6).
**Levels:** 0-1 Low, 2-3 Medium, 4-5 High, 6+ Critical. Priority never moves down while an issue is open.

**Status flow:** New -> Under Review -> In progress -> Waiting for student confirmation -> Resolved | Closed (No Response). A "no" (comment required) or a new matching report returns an issue to In progress.

**Routing:** Teaching -> Faculty only; Facilities, Portal, Other -> Staff only. Moderators can view everything but cannot work tickets.

**Confirmation timing:** reminder 7 days after "Work done", closure 7 days after the reminder (`Config`).

**Anonymity:** Faculty/Staff views contain category, descriptions, class names, counts, percentages and status - never student name, ID, username or internal report id (asserted in tests). Moderators see identity only in the moderation queue.

## REST API (JSON, `Authorization: Bearer <token>`)

Public: `POST /api/login`, `POST /api/register`, `GET /api/classes`, `GET /api/health`
Student: `POST /api/reports`, `GET /api/my/reports`, `POST /api/issues/<id>/confirm`
Faculty/Staff/Moderator: `GET /api/issues?include=open|closed|all`, `GET /api/issues/<id>`, `POST /api/issues/<id>/review`, `POST /api/issues/<id>/ticket`, `POST /api/tickets/<id>/claim|note|done`, `GET /api/analytics`
Moderator: `GET /api/moderation`, `POST /api/moderation/<id>` `{"approve":bool}`, `POST /api/admin/accounts|enrollment|advance-days`
All users: `GET /api/me`, `GET /api/notifications`, `POST /api/notifications/read`, `POST /api/logout`

## Layout
```
src/    types, util (SHA-256), text (grouping + moderation), store (JSON file), service (all rules),
        server (Crow), console, ds.h (team's containers)
web/    index.html, app.js, style.css  (no external libraries; charts drawn on canvas)
tests/  test_all.cpp, smoke_http.py
third_party/  crow, asio, nlohmann json (vendored)
legacy_phase2/  original prototype
```

## Known limitations (be upfront about these in the demo/report)
- Grouping is rule-based word similarity with a synonym list, not machine learning. Different wording with no shared vocabulary will not group; the same equipment in a different room/lab is deliberately kept separate.
- Abuse detection is a word list with look-alike-character handling. It will miss subtle abuse and may flag harsh-but-legitimate wording (that is why a human moderator decides).
- Anonymity covers system fields. A student who writes their own name inside the message text is not redacted.
- Passwords use salted, iterated SHA-256 (self-implemented, checked against standard test vectors). A production system should use bcrypt/argon2 from a vetted library, and HTTPS.
- Storage is a single JSON file with one process; do not run the server and console on the same file at once. Sessions are in memory and end on server restart.
- The "Advance days" demo clock is for demonstration and testing only.
- The web UI was exercised with an automated DOM check (all three roles render, no script errors) and the API end to end, but not yet in a real browser on a phone; give it a quick visual pass. The `web/` front end was rebuilt after the original files were lost, so check it visually.
- Workflow additions beyond the proposal: Staff/Faculty may open a ticket manually on any issue (otherwise a low-priority issue could not be worked before it ages to Critical); a new report on an issue awaiting confirmation reopens it.
