#!/usr/bin/env python3
"""End-to-end test of the REST API against a running server (Python standard library only).
Usage: python3 tests/smoke_http.py [base_url] [admin_password]"""
import json, sys, urllib.request, urllib.error

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8080"
ADMIN_PW = sys.argv[2] if len(sys.argv) > 2 else "Admin1234"
passed = failed = 0

def call(method, path, body=None, token=None, raw=False):
    req = urllib.request.Request(BASE + path, method=method, data=json.dumps(body).encode() if body is not None else None)
    req.add_header("Content-Type", "application/json")
    if token: req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req) as r:
            data = r.read()
            return r.status, (data if raw else json.loads(data)), r.headers
    except urllib.error.HTTPError as e:
        data = e.read()
        return e.code, (data if raw else json.loads(data)), e.headers

def check(name, cond, extra=""):
    global passed, failed
    if cond: passed += 1
    else:
        failed += 1; print("  FAIL", name, extra)

# --- static front end and security headers
st, body, hdr = call("GET", "/", raw=True)
check("index served", st == 200 and b"Closed Loop Feedback" in body)
check("CSP header", "default-src 'self'" in (hdr.get("Content-Security-Policy") or ""))
check("app.js served", call("GET", "/app.js", raw=True)[0] == 200)
check("health", call("GET", "/api/health")[1]["ok"] is True)

# --- auth
check("unauthenticated is 401", call("GET", "/api/issues")[0] == 401)
st, r, _ = call("POST", "/api/login", {"username": "admin", "password": "wrong"})
check("bad login 401", st == 401)
st, r, _ = call("POST", "/api/login", {"username": "admin", "password": ADMIN_PW})
check("admin login", st == 200 and r["user"]["role"] == "Moderator")
admin = r["token"]
check("malformed JSON is 400", call("POST", "/api/login", None)[0] in (200, 400, 401))

# --- admin setup
for cls, n in [("CSE-A", 30), ("CSE-B", 30)]:
    check("enrollment " + cls, call("POST", "/api/admin/enrollment", {"className": cls, "count": n}, admin)[0] == 200)
check("create faculty", call("POST", "/api/admin/accounts", {"username": "fac", "password": "Passw0rd1", "name": "Dr Fac", "role": "Faculty"}, admin)[0] == 201)
check("create staff", call("POST", "/api/admin/accounts", {"username": "stf", "password": "Passw0rd1", "name": "Sam Staff", "role": "Staff"}, admin)[0] == 201)
fac = call("POST", "/api/login", {"username": "fac", "password": "Passw0rd1"})[1]["token"]
stf = call("POST", "/api/login", {"username": "stf", "password": "Passw0rd1"})[1]["token"]

# --- students register + login
toks = []
for i, (nm, cls) in enumerate([("Alice Anand", "CSE-A"), ("Bob Bhatt", "CSE-A"), ("Chitra Chopra", "CSE-B")]):
    st, r, _ = call("POST", "/api/register", {"username": f"stu{i}", "password": "Passw0rd1", "name": nm, "studentId": f"2510{i}0099", "className": cls})
    check("register " + nm, st == 201, r)
    toks.append(call("POST", "/api/login", {"username": f"stu{i}", "password": "Passw0rd1"})[1]["token"])
check("classes list public", len(call("GET", "/api/classes")[1]) == 2)

# --- submit, group, route
st, r, _ = call("POST", "/api/reports", {"category": "Facilities", "message": "The projector in room 204 is not working"}, toks[0])
check("submit 201", st == 201 and r["held"] is False, r)
issue = r["issueId"]
r2 = call("POST", "/api/reports", {"category": "Facilities", "message": "Projector in classroom 204 is broken"}, toks[2])[1]
check("grouped", r2["issueId"] == issue and r2["studentsAffected"] == 2, r2)
check("short message 400", call("POST", "/api/reports", {"category": "Facilities", "message": "short"}, toks[1])[0] == 400)
check("staff cannot submit", call("POST", "/api/reports", {"category": "Other", "message": "this should be refused"}, stf)[0] == 403)

# --- permissions and anonymity
check("student cannot list issues", call("GET", "/api/issues", None, toks[0])[0] == 403)
check("faculty cannot see staff issue", call("GET", f"/api/issues/{issue}", None, fac)[0] == 403)
st, listing, _ = call("GET", "/api/issues", None, stf)
check("staff sees issue", st == 200 and len(listing) == 1)
dump = json.dumps(listing) + json.dumps(call("GET", f"/api/issues/{issue}", None, stf)[1])
check("no student identity in staff view", not any(x in dump for x in ["Alice", "Bob", "Chitra", "stu0", "25100099", "reference"]))
check("faculty list empty", call("GET", "/api/issues", None, fac)[1] == [])

# --- ticket workflow and closed loop
det = call("GET", f"/api/issues/{issue}", None, stf)[1]
check("review action offered", det["actions"]["review"] is True)
check("review", call("POST", f"/api/issues/{issue}/review", None, stf)[0] == 200)
st, t, _ = call("POST", f"/api/issues/{issue}/ticket", None, stf)
check("open ticket", st == 201, t)
tid = t["id"]
check("cannot finish unclaimed", call("POST", f"/api/tickets/{tid}/done", {"actionTaken": "replaced lamp"}, stf)[0] == 409)
check("claim", call("POST", f"/api/tickets/{tid}/claim", {}, stf)[0] == 200)
check("note", call("POST", f"/api/tickets/{tid}/note", {"text": "ordered lamp"}, stf)[0] == 200)
check("done", call("POST", f"/api/tickets/{tid}/done", {"actionTaken": "Replaced the projector lamp"}, stf)[0] == 200)
mine = call("GET", "/api/my/reports", None, toks[0])[1]
check("student asked to confirm", mine[0]["needsMyConfirmation"] is True and mine[0]["actionTaken"].startswith("Replaced"))
check("first yes does not resolve", call("POST", f"/api/issues/{issue}/confirm", {"solved": True}, toks[0])[1]["resolved"] is False)
check("no needs comment", call("POST", f"/api/issues/{issue}/confirm", {"solved": False}, toks[2])[0] == 400)
check("no with comment reopens", call("POST", f"/api/issues/{issue}/confirm", {"solved": False, "comment": "still flickering"}, toks[2])[1]["reopened"] is True)
check("back in progress", call("GET", f"/api/issues/{issue}", None, stf)[1]["status"] == "In progress")
call("POST", f"/api/tickets/{tid}/done", {"actionTaken": "Replaced the projector cable too"}, stf)
call("POST", f"/api/issues/{issue}/confirm", {"solved": True}, toks[0])
check("one of two is not enough", call("GET", f"/api/issues/{issue}", None, stf)[1]["status"] == "Waiting for student confirmation")
check("last yes resolves", call("POST", f"/api/issues/{issue}/confirm", {"solved": True}, toks[2])[1]["resolved"] is True)

# --- moderation
st, r, _ = call("POST", "/api/reports", {"category": "Teaching", "message": "The teacher is a stupid idiot"}, toks[1])
check("abusive report held", st == 201 and r["held"] is True, r)
q = call("GET", "/api/moderation", None, admin)[1]
check("in queue", len(q) == 1)
check("faculty not allowed in queue", call("GET", "/api/moderation", None, fac)[0] == 403)
check("reject", call("POST", f"/api/moderation/{q[0]['id']}", {"approve": False}, admin)[0] == 200)

# --- demo clock: ageing, escalation, no-response
call("POST", "/api/reports", {"category": "Other", "message": "The notice board is torn near the office"}, toks[1])
check("clock forbidden for staff", call("POST", "/api/admin/advance-days", {"days": 5}, stf)[0] == 403)
check("advance 20 days", call("POST", "/api/admin/advance-days", {"days": 20}, admin)[0] == 200)
top = call("GET", "/api/issues", None, stf)[1]
check("aged to Critical with ticket", top[0]["priority"] == "Critical" and "ticket" in top[0], top)

# --- analytics, notifications
an = call("GET", "/api/analytics", None, admin)[1]
check("analytics", an["resolved"] == 1 and an["open"] >= 1 and len(an["reportsPerDay"]) == 14, an)
n = call("GET", "/api/notifications", None, toks[0])[1]
check("notifications", n["unread"] > 0 and len(n["items"]) > 0)
check("mark read", call("POST", "/api/notifications/read", None, toks[0])[0] == 200)
check("logout", call("POST", "/api/logout", None, toks[0])[0] == 200)
check("token dead after logout", call("GET", "/api/me", None, toks[0])[0] == 401)

print(f"{passed + failed} checks, {failed} failed")
sys.exit(1 if failed else 0)
