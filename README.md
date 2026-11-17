# Closed Loop Feedback Management System

Team **Void walkers** - B.Tech CSE.

**Live demo:** https://student-feedback-ccqq.onrender.com
(free hosting: the first load after a quiet period can take about a minute, and data added during a session is lost when the server restarts - the demo accounts and sample complaints are recreated automatically)

Students report problems at any point in the term. The system groups similar reports, works out how many students in which classes are affected, assigns a starting priority that rises with age, opens a ticket at **Critical**, routes it to **Faculty** (Teaching) or **Staff** (everything else), and then asks **every** reporting student whether the problem is really solved. An issue is **Resolved only when all of them say yes**. If students stay silent it is closed as **Closed (No Response)**, never as Resolved.

## Features

- **Sign-up by role.** Login page first, then *Sign up* -> choose **Student**, **Faculty** or **Staff** -> that role's own form.
- **Login with an ID.** Students use their **Student ID**, faculty and staff their **Faculty/Staff ID**, the administrator uses the username `admin`. The role always comes from the stored account on the server, never from the browser.
- **Course, semester and section for students.** Course *B.Tech CSE*, Semester 1-8, Sections **A-I, ML1-ML6, CS1-CS2, DS1-DS2**. Each semester/section combination is its own class, so "% of the class affected" is exact.
- **Grouping** of similar reports, **priority score** with ageing and safety keywords, **tickets**, **confirmation by every reporter**, **reminders and no-response closure**.
- **Anonymity:** faculty and staff never see who reported.
- **Moderation** of abusive text, **in-app notifications**, **analytics** (status, priority, class impact, reports per day, average resolution time).
- **Works on phones:** responsive layout with large tap targets.

## Quick start

Requires `g++` (C++17), `make`, and `python3` (only for the end-to-end test). Crow, standalone Asio and nlohmann/json are included in `third_party/`, so nothing needs downloading.

```bash
make                 # builds feedback_server, feedback_console, run_tests
make test            # 212 unit/integration checks
./feedback_server --demo          # web app on http://127.0.0.1:8080
./feedback_console --demo         # console version (same engine)
python3 tests/smoke_http.py http://127.0.0.1:8080 <admin password>   # 51 end-to-end API checks, run on a FRESH data file (no --demo)
```

**Windows:** install [MSYS2](https://www.msys2.org), open the **MSYS2 UCRT64** terminal, run `pacman -S --needed mingw-w64-ucrt-x86_64-gcc make git`, then the commands above.

Server options: `--port 8080 --host 127.0.0.1 --data data/store.json --web web --demo`. It listens on localhost by default; use `--host 0.0.0.0` to reach it from other devices (for example a phone on the same Wi-Fi) and put it behind HTTPS if it is exposed to the internet.

Environment variables:

| Variable | Meaning |
|---|---|
| `FB_ADMIN_PASSWORD` | Password for the `admin` account. If unset, a random one is generated and printed once on first run. |
| `FB_STAFF_CODE` | Access code that faculty and staff must enter to sign up themselves. If unset, faculty/staff sign-up is closed and only the admin can create those accounts. |

## Logins

| Role | Log in with | Notes |
|---|---|---|
| Student | Student ID (5-20 digits) | created through *Sign up -> Student* |
| Faculty / Staff | Faculty/Staff ID (3-20 letters, digits or `-`, starting with a letter; not case-sensitive) | *Sign up -> Faculty/Staff* needs the access code, or the admin creates the account |
| Admin | username `admin` | password from `FB_ADMIN_PASSWORD` |

Five wrong passwords in five minutes lock that ID out for a while. Passwords are stored only as salted, iterated SHA-256 hashes.

### Demo accounts (`--demo`, password `Demo@1234` - demo only)

| ID | Name | Role / class |
|---|---|---|
| `FAC1001`, `FAC1002` | Dr Meera Sharma, Prof. Anil Kumar | Faculty |
| `STF1001`, `STF1002` | Ravi Rao, Sunita Devi | Staff |
| `25100001000` - `25100001005` | Asha Verma, Kabir Singh, Neha Joshi, Rohan Mehta, Isha Kapoor, Dev Patel | Students, B.Tech CSE, Semester 3 (sections A, B, ML1, A, B, ML1) |
| `25100001006` - `25100001009` | Priya Nair, Arjun Rao, Sneha Gupta, Vikram Shah | Students, Semester 5 (CS1, CS1, DS1, DS2) |
| `25100001010`, `25100001011` | Ananya Das, Karan Malhotra | Students, Semester 1, section C |

`--demo` also loads **15 sample issues filed over the last 12 days**, worked through the real workflow: 4 resolved, 5 in progress (one reopened after a student answered "not solved"), 4 new, 1 under review, 1 waiting for a student to confirm. One is **Critical** (a safety report), one **High** (five students from four classes), and the rest Medium or Low. Ages, reports per day and resolution times in *Analytics* come from these dates.

### Demo walk-through (about 5 minutes)

1. Log in as faculty `FAC1001` -> **Issues**. Open the resolved *data structures* issue: the history shows each step and its date; there are no student names.
2. Log in as student `25100001005` (Dev) -> **My reports**. Answer **Yes, solved** on the lab timetable issue to close the loop.
3. Log in as `STF1001` -> **Issues**. Take a *New* issue: review it, open a ticket, take it, add a note, press *Work done*.
4. As a student, submit a new report in similar words to an open one: it is **grouped** and the priority score changes. A report with words like "fire" or "harassment" is Critical immediately.
5. Log in as `admin` -> **Analytics**, **Moderation** and **Admin**. *Advance days* moves the demo clock forward to show ageing, the 7-day reminder and no-response closure.
6. Submit a rude report: it is **held for the moderator** and is not processed until approved.

## Rules, exactly as implemented

**Priority score** = students (2-4: +1, 5-9: +2, 10+: +3) + classes (2-3: +1, 4+: +2) + largest % of a class (25%: +1, 50%: +2) + keywords (safety word such as fire/harassment/injury/unsafe/sparking/exposed wire: +6; exam/marks/fee/attendance/deadline etc.: +1) + age (+1 per 3 open days, capped at +6).
**Levels:** 0-1 Low, 2-3 Medium, 4-5 High, 6+ Critical. Priority never moves down while an issue is open.

**Status flow:** New -> Under Review -> In progress -> Waiting for student confirmation -> Resolved | Closed (No Response). A "no" (comment required) or a new matching report returns an issue to In progress.

**Routing:** Teaching -> Faculty only; Facilities, Portal, Other -> Staff only. A team sees its whole queue so that anyone on it can take an unassigned ticket. Moderators can view everything but cannot work tickets.

**Confirmation timing:** reminder 7 days after "Work done", closure 7 days after the reminder (`Config`).

**Anonymity:** Faculty/Staff views contain category, descriptions, class names, counts, percentages and status - never student name, ID, username or internal report id (asserted in tests). Moderators see identity only in the moderation queue.

**Permissions:** every API call checks the role on the server. Students cannot read other students' reports or any staff view; faculty and staff cannot use moderator or admin functions.

## No STL containers or algorithms

As required by the course, **our own code does not use STL containers or algorithms** (`vector`, `map`, `set`, `pair`, `sort`, `transform`, `min`, `max`, `swap`, ...). `src/ds.h` holds the team's own data structures:

| Our structure | What it is | Used for |
|---|---|---|
| `ScratchVector<T>` | growable array (copy, move, insert, erase) | users, reports, issues, tickets, history, notes |
| `ScratchSet<T>` | ordered set (sorted array, binary search) | distinct students and classes behind an issue |
| `ScratchMap<K,V>` | ordered map (sorted array, binary search) | class enrollment, per-class counts, confirmations |
| `ScratchPriorityQueue<T,C>` | binary max-heap | ordering issues by priority |
| `scratchSort`, `scratchMax`, `scratchMin`, `scratchSwap`, `ScratchPair` | stable insertion sort and small helpers | sorting, comparisons |

`src/ds_json.h` lets these containers be saved to and loaded from the JSON data file. `tests/test_all.cpp` has direct tests for every container. To check the rule yourself, this prints nothing:

```bash
grep -rnE "std::(vector|map|set|pair|sort|transform|max|min|swap)\b|#include <(vector|map|set|algorithm|queue)>" src tests
```

Still used from the standard library, as allowed: `std::string` and its helpers, streams (`iostream`, `fstream`, `sstream`), `std::function`, `std::unique_ptr`, mutexes and locks, `std::chrono`/`time`, `std::filesystem`, `std::initializer_list`, `std::move`/`std::forward`, and the C functions from `<cctype>`, `<cmath>`, `<cstdlib>`, `<cstdio>`. The web libraries (Crow, Asio, nlohmann/json) are allowed and use the STL internally.

## Proposal item -> where it lives

| Item | Implementation |
|---|---|
| Grouping of similar reports (synonyms, tuned threshold) | `src/text.cpp` `canonicalTokens`/`similarity`; `Service::admitReport`. Threshold 0.40 in `Config`. |
| Automatic priority, Critical level, class enrollment | `Service::computePriority`; enrollment table via `setEnrollment` |
| Ageing and escalation | `Service::refreshPriority`, `tickLocked` (+1 point per 3 days, up to +6) |
| Tickets (creation, assignment, progress, notes) | `createTicket`, `claimTicket`, `addTicketNote`, `completeTicket` |
| Confirmation by every reporter, 7-day reminder, no-response closure | `Service::confirm`, `completeTicket`, `tickLocked` |
| Analytics and class-impact view | `Service::analytics`, `impactJson`; shown in the web *Analytics* tab |
| Role permissions, login by ID, sign-up | `requireRole`, `requireTeamFor`, `loginById`, `registerStudent`, `registerStaff`, `courses` (salted iterated SHA-256, sessions, lockout) |
| Abusive-comment detection, flagging, moderator approval | `detectAbuse`, `moderationQueue`, `moderate` |
| File-based storage | `src/store.cpp` (atomic JSON writes, corrupt-file protection) |
| Crow REST API and web portal | `src/server.cpp`, `web/` |
| In-app notifications | `Service::notify*`; *Notifications* tab |
| Demo data | `Service::seedDemoData`, `seedDemoComplaints` |
| Automated tests | `tests/test_all.cpp` (212 checks), `tests/smoke_http.py` (51 checks) |

The earlier containers `ScratchVector` and `ScratchPriorityQueue` from Phase 2 were extended in `src/ds.h`. The original prototype is preserved in `legacy_phase2/`.

## REST API (JSON, `Authorization: Bearer <token>`)

Public: `POST /api/login` (`{"id": ..., "password": ...}`), `POST /api/register` (student), `POST /api/register-staff` (faculty/staff, needs the access code), `GET /api/courses`, `GET /api/classes`, `GET /api/health`
Student: `POST /api/reports`, `GET /api/my/reports`, `POST /api/issues/<id>/confirm`
Faculty/Staff/Moderator: `GET /api/issues?include=open|closed|all`, `GET /api/issues/<id>`, `POST /api/issues/<id>/review`, `POST /api/issues/<id>/ticket`, `POST /api/tickets/<id>/claim|note|done`, `GET /api/analytics`
Moderator: `GET /api/moderation`, `POST /api/moderation/<id>` `{"approve":bool}`, `POST /api/admin/accounts|enrollment|advance-days`
All users: `GET /api/me`, `GET /api/notifications`, `POST /api/notifications/read`, `POST /api/logout`

## Hosting

The repository contains a `Dockerfile`, so it can run on any host that builds Docker images (we use Render's free plan). Set `FB_ADMIN_PASSWORD` and `FB_STAFF_CODE` as environment variables. Every push to GitHub rebuilds and restarts the service. Free hosting has no permanent disk: it also sleeps after about 15 minutes without traffic, and data created since the last start is lost then; `--demo` recreates the demo accounts and sample issues on every start.

## Layout
```
src/    types, util (SHA-256), text (grouping + moderation), store (JSON file), service (all rules),
        server (Crow), console, ds.h / ds_json.h (team's containers)
web/    index.html, app.js, style.css  (plain HTML/CSS/JS, no external libraries, phone friendly)
tests/  test_all.cpp, smoke_http.py
third_party/  crow, asio, nlohmann json (included)
legacy_phase2/  original prototype
Dockerfile, .dockerignore  (hosting)
```
