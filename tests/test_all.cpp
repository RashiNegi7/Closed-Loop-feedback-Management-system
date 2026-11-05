// Assert-based tests for the Closed Loop Feedback Management System (no external framework).
#include "service.h"
#include "text.h"
#include "util.h"
#include <cstdio>
#include <filesystem>
#include <iostream>

using namespace fb;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        ++g_checks;                                                                        \
        if (!(cond)) { ++g_failed; std::cout << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; } \
    } while (0)
#define CHECK_THROWS(want, expr)                                                           \
    do {                                                                                   \
        ++g_checks;                                                                        \
        try { expr; ++g_failed; std::cout << "  FAIL (no error) line " << __LINE__ << "  " #expr "\n"; } \
        catch (const ApiError& e) { if (e.code != (want)) { ++g_failed; std::cout << "  FAIL line " << __LINE__ << " got " << e.code << " (" << e.what() << ") wanted " << (want) << "\n"; } } \
    } while (0)

struct World {
    Service svc;
    int admin = 0, faculty = 0, staff = 0;
    std::vector<int> students;  // 6 students: 0-2 CSE-A, 3-4 CSE-B, 5 ECE-A
    World() {
        svc.bootstrapAdmin("Admin1234");
        admin = svc.state().users[0].id;
        svc.setEnrollment(admin, {{"className", "CSE-A"}, {"count", 30}});
        svc.setEnrollment(admin, {{"className", "CSE-B"}, {"count", 30}});
        svc.setEnrollment(admin, {{"className", "ECE-A"}, {"count", 30}});
        faculty = svc.createAccount(admin, {{"username", "fac"}, {"password", "Passw0rd1"}, {"name", "Dr Faculty"}, {"role", "Faculty"}})["id"];
        staff = svc.createAccount(admin, {{"username", "stf"}, {"password", "Passw0rd1"}, {"name", "Sam Staff"}, {"role", "Staff"}})["id"];
        const char* cls[] = {"CSE-A", "CSE-A", "CSE-A", "CSE-B", "CSE-B", "ECE-A"};
        const char* nm[] = {"Alice Anand", "Bob Bhatt", "Chitra Chopra", "Dinesh Das", "Esha Eapen", "Farah Fernandes"};
        for (int i = 0; i < 6; ++i) {
            json r = svc.registerStudent({{"username", "stu" + std::to_string(i)}, {"password", "Passw0rd1"}, {"name", nm[i]},
                                          {"studentId", "2510000" + std::to_string(i)}, {"className", cls[i]}});
            students.push_back(r["id"]);
        }
    }
    json report(int s, const std::string& cat, const std::string& msg) {
        return svc.submitReport(students[s], {{"category", cat}, {"message", msg}});
    }
    const Issue& issue(int id) {
        for (const auto& i : svc.state().issues) if (i.id == id) return i;
        throw std::runtime_error("no issue");
    }
};

static void testAccounts() {
    std::cout << "accounts & permissions\n";
    World w;
    CHECK_THROWS(400, w.svc.registerStudent({{"username", "x"}, {"password", "Passw0rd1"}, {"name", "A B"}, {"studentId", "12345"}, {"className", "CSE-A"}}));
    CHECK_THROWS(400, w.svc.registerStudent({{"username", "okuser"}, {"password", "short"}, {"name", "A B"}, {"studentId", "12345"}, {"className", "CSE-A"}}));
    CHECK_THROWS(400, w.svc.registerStudent({{"username", "okuser"}, {"password", "Passw0rd1"}, {"name", "A1 B"}, {"studentId", "12345"}, {"className", "CSE-A"}}));
    CHECK_THROWS(400, w.svc.registerStudent({{"username", "okuser"}, {"password", "Passw0rd1"}, {"name", "A B"}, {"studentId", "12a45"}, {"className", "CSE-A"}}));
    CHECK_THROWS(400, w.svc.registerStudent({{"username", "okuser"}, {"password", "Passw0rd1"}, {"name", "A B"}, {"studentId", "12345"}, {"className", "NOPE"}}));
    CHECK_THROWS(409, w.svc.registerStudent({{"username", "stu0"}, {"password", "Passw0rd1"}, {"name", "A B"}, {"studentId", "99999"}, {"className", "CSE-A"}}));
    CHECK_THROWS(409, w.svc.registerStudent({{"username", "other"}, {"password", "Passw0rd1"}, {"name", "A B"}, {"studentId", "25100000"}, {"className", "CSE-A"}}));

    std::string tok = w.svc.login("STU0", "Passw0rd1");  // username is case-insensitive
    CHECK(w.svc.authenticate(tok) == w.students[0]);
    w.svc.logout(tok);
    CHECK_THROWS(401, w.svc.authenticate(tok));
    CHECK_THROWS(401, w.svc.authenticate("garbage"));
    CHECK_THROWS(401, w.svc.login("stu0", "wrong"));
    CHECK_THROWS(401, w.svc.login("nobody", "wrong"));
    for (int i = 0; i < 5; ++i) { try { w.svc.login("stu1", "bad"); } catch (const ApiError&) {} }
    CHECK_THROWS(429, w.svc.login("stu1", "Passw0rd1"));  // locked out after repeated failures

    // role checks
    CHECK_THROWS(403, w.svc.listIssues(w.students[0], json::object()));
    CHECK_THROWS(403, w.svc.submitReport(w.staff, {{"category", "Other"}, {"message", "a long enough message"}}));
    CHECK_THROWS(403, w.svc.createAccount(w.staff, {{"username", "zzz"}, {"password", "Passw0rd1"}, {"name", "Z Z"}, {"role", "Staff"}}));
    CHECK_THROWS(403, w.svc.advanceDays(w.faculty, 1));
    CHECK_THROWS(403, w.svc.moderationQueue(w.staff));
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(w.svc.state().users[0].hash != "Admin1234");  // never stored in clear
}

static void testSubmissionAndGrouping() {
    std::cout << "submission, routing, grouping\n";
    World w;
    CHECK_THROWS(400, w.report(0, "Facilities", "too short"));
    CHECK_THROWS(400, w.report(0, "Nonsense", "The projector is not working in room 204"));
    json a = w.report(0, "Facilities", "The projector in room 204 is not working");
    json b = w.report(3, "Facilities", "Projector in classroom 204 is broken");
    json c = w.report(5, "Facilities", "projector 204 does not work, please fix it");
    CHECK(a["issueId"] == b["issueId"] && b["issueId"] == c["issueId"]);
    CHECK(c["studentsAffected"] == 3);
    json d = w.report(1, "Facilities", "The fans in room 204 do not work at all");
    CHECK(d["issueId"] != a["issueId"]);                  // same room, different equipment
    json e = w.report(1, "Facilities", "Projector in room 305 is broken");
    CHECK(e["issueId"] != a["issueId"]);                  // same equipment, different room
    json f = w.report(2, "Portal", "The projector in room 204 is not working");
    CHECK(f["issueId"] != a["issueId"]);                  // category differs -> never grouped
    // same student reporting again counts once
    json g = w.report(0, "Facilities", "Projector in room 204 still broken today");
    CHECK(g["issueId"] == a["issueId"]);
    CHECK(g["studentsAffected"] == 3);

    // routing: Teaching -> Faculty only, everything else -> Staff only
    w.report(0, "Teaching", "The professor skips the DSA syllabus portion");
    json fac = w.svc.listIssues(w.faculty, json::object());
    json stf = w.svc.listIssues(w.staff, json::object());
    for (auto& i : fac) CHECK(i["category"] == "Teaching");
    for (auto& i : stf) CHECK(i["category"] != "Teaching");
    CHECK(fac.size() == 1);
    CHECK(w.svc.listIssues(w.admin, json::object()).size() == fac.size() + stf.size());
    CHECK_THROWS(403, w.svc.getIssue(w.staff, fac[0]["id"]));
    CHECK_THROWS(403, w.svc.reviewIssue(w.staff, fac[0]["id"]));
    CHECK_THROWS(404, w.svc.getIssue(w.staff, 9999));
}

static void testPriorityAndAgeing() {
    std::cout << "priority, ageing, tickets\n";
    World w;
    // single, ordinary report starts Low and ages upward
    json a = w.report(0, "Facilities", "The corridor bench near the library is wobbly");
    int id = a["issueId"];
    CHECK(w.issue(id).priority == Priority::Low);
    w.svc.advanceDays(w.admin, 3);  CHECK(w.issue(id).score == 1 && w.issue(id).priority == Priority::Low);
    w.svc.advanceDays(w.admin, 3);  CHECK(w.issue(id).priority == Priority::Medium);
    w.svc.advanceDays(w.admin, 6);  CHECK(w.issue(id).priority == Priority::High);
    CHECK(w.issue(id).ticketId == 0);
    w.svc.advanceDays(w.admin, 6);  // 18 days open
    CHECK(w.issue(id).priority == Priority::Critical);
    CHECK(w.issue(id).ticketId != 0);  // ticket appears at Critical
    CHECK(w.svc.state().tickets[0].team == Role::Staff);

    // safety keyword is Critical immediately and creates a ticket routed to Faculty for Teaching
    json b = w.report(1, "Teaching", "The lecturer harassed a student in class");
    const Issue& sb = w.issue(b["issueId"]);
    CHECK(sb.priority == Priority::Critical && sb.ticketId != 0);
    CHECK(w.svc.state().tickets.back().team == Role::Faculty);

    // impact: many students across classes raises priority
    json c = w.report(0, "Portal", "ERP marks page is not loading for semester 3");
    for (int s = 1; s < 6; ++s) w.report(s, "Portal", "Marks page in the ERP portal is not loading");
    const Issue& sc = w.issue(c["issueId"]);
    CHECK(sc.priority >= Priority::High);
    json detail = w.svc.getIssue(w.staff, sc.id);
    CHECK(detail["impact"]["students"] == 6);
    CHECK(detail["impact"]["classes"].size() == 3);
    CHECK(detail["priorityBreakdown"]["pointsStudents"] == 2);

    // list is ordered highest priority first
    json list = w.svc.listIssues(w.staff, json::object());

    Priority prev = Priority::Critical;
    for (auto& i : list) { Priority p; parsePriority(i["priority"], p); CHECK(p <= prev); prev = p; }

    // manual ticket on a low issue
    json d = w.report(2, "Other", "The notice board outside the office is torn badly");
    CHECK_THROWS(403, w.svc.openTicket(w.faculty, d["issueId"]));
    json t = w.svc.openTicket(w.staff, d["issueId"]);
    CHECK(t["status"] == "Open");
    CHECK_THROWS(409, w.svc.openTicket(w.staff, d["issueId"]));
}

static json startFix(World& w, int issueId, const std::string& action = "Replaced the faulty cable") {
    Role team = teamFor(w.issue(issueId).category);
    int actor = team == Role::Faculty ? w.faculty : w.staff;
    if (w.issue(issueId).ticketId == 0) w.svc.openTicket(actor, issueId);
    int tid = w.issue(issueId).ticketId;
    w.svc.claimTicket(actor, tid, json::object());
    return w.svc.completeTicket(actor, tid, {{"actionTaken", action}});
}

static void testClosedLoop() {
    std::cout << "closed loop confirmation\n";
    World w;
    json a = w.report(0, "Facilities", "The projector in room 204 is not working");
    w.report(1, "Facilities", "Projector in classroom 204 is broken");
    w.report(3, "Facilities", "projector 204 does not work");
    int id = a["issueId"];
    int tid;
    CHECK_THROWS(409, w.svc.confirm(w.students[0], id, {{"solved", true}}));  // nothing to confirm yet
    w.svc.reviewIssue(w.staff, id);
    CHECK(w.issue(id).status == IssueStatus::UnderReview);
    w.svc.openTicket(w.staff, id);
    tid = w.issue(id).ticketId;
    CHECK_THROWS(409, w.svc.completeTicket(w.staff, tid, {{"actionTaken", "fixed it"}}));  // must claim first
    w.svc.claimTicket(w.staff, tid, json::object());
    CHECK(w.issue(id).status == IssueStatus::InProgress);
    w.svc.addTicketNote(w.staff, tid, {{"text", "ordered a new lamp"}});
    CHECK_THROWS(400, w.svc.completeTicket(w.staff, tid, {{"actionTaken", "x"}}));
    w.svc.completeTicket(w.staff, tid, {{"actionTaken", "Replaced the projector lamp"}});
    CHECK(w.issue(id).status == IssueStatus::WaitingForConfirmation);  // staff cannot resolve
    CHECK(w.issue(id).confirmations.size() == 3);

    // students not in the issue / wrong role cannot answer
    CHECK_THROWS(404, w.svc.confirm(w.students[5], id, {{"solved", true}}));
    CHECK_THROWS(403, w.svc.confirm(w.staff, id, {{"solved", true}}));
    CHECK_THROWS(400, w.svc.confirm(w.students[0], id, {{"nonsense", 1}}));

    w.svc.confirm(w.students[0], id, {{"solved", true}});
    w.svc.confirm(w.students[1], id, {{"solved", true}});
    CHECK(w.issue(id).status == IssueStatus::WaitingForConfirmation);  // 2 of 3 is NOT enough
    CHECK_THROWS(409, w.svc.confirm(w.students[0], id, {{"solved", true}}));
    json mine = w.svc.myReports(w.students[3]);
    CHECK(mine[0]["needsMyConfirmation"] == true);
    CHECK(mine[0]["actionTaken"] == "Replaced the projector lamp");
    json last = w.svc.confirm(w.students[3], id, {{"solved", true}});
    CHECK(last["resolved"] == true && w.issue(id).status == IssueStatus::Resolved);

    // a "no" sends it back, needs a comment, and the next round needs everybody again
    json b = w.report(0, "Portal", "The ERP login page keeps crashing");
    w.report(1, "Portal", "ERP login crashes every time I open it");
    int id2 = b["issueId"];
    startFix(w, id2);
    w.svc.confirm(w.students[0], id2, {{"solved", true}});
    CHECK_THROWS(400, w.svc.confirm(w.students[1], id2, {{"solved", false}}));  // comment required
    json no = w.svc.confirm(w.students[1], id2, {{"solved", false}, {"comment", "Still crashing on my phone"}});
    CHECK(no["reopened"] == true);
    CHECK(w.issue(id2).status == IssueStatus::InProgress);
    CHECK(w.issue(id2).reopenCount == 1);
    json det = w.svc.getIssue(w.staff, id2);
    CHECK(det["followUps"].size() == 1);
    int tid2 = w.issue(id2).ticketId;
    CHECK(w.svc.state().tickets[tid2 - 1].status == TicketStatus::Open || w.svc.state().tickets[tid2 - 1].status == TicketStatus::Assigned);
    w.svc.completeTicket(w.staff, tid2, {{"actionTaken", "Patched the login service"}});
    w.svc.confirm(w.students[0], id2, {{"solved", true}});
    CHECK(w.issue(id2).status == IssueStatus::WaitingForConfirmation);  // student 0's earlier yes was reset
    w.svc.confirm(w.students[1], id2, {{"solved", true}});
    CHECK(w.issue(id2).status == IssueStatus::Resolved);

    // a new report while waiting reopens the issue
    json c = w.report(0, "Facilities", "Water cooler on the second floor is not working");
    startFix(w, c["issueId"]);
    json again = w.report(1, "Facilities", "Water cooler on second floor does not work");
    CHECK(again["issueId"] == c["issueId"]);
    CHECK(w.issue(c["issueId"]).status == IssueStatus::InProgress);
}

static void testNoResponse() {
    std::cout << "reminder and no-response closure\n";
    World w;
    json a = w.report(0, "Facilities", "The AC in lab 3 is broken");
    w.report(1, "Facilities", "Air conditioner not working in lab 3");
    int id = a["issueId"];
    startFix(w, id);
    w.svc.confirm(w.students[0], id, {{"solved", true}});
    auto unreadFor = [&](int s) { return w.svc.notifications(w.students[s])["unread"].get<int>(); };
    int before = unreadFor(1);
    w.svc.advanceDays(w.admin, 6);
    CHECK(unreadFor(1) == before && !w.issue(id).reminderSent);
    w.svc.advanceDays(w.admin, 1);  // day 7
    CHECK(w.issue(id).reminderSent);
    CHECK(unreadFor(1) == before + 1);               // silent student reminded
    w.svc.advanceDays(w.admin, 6);
    CHECK(w.issue(id).status == IssueStatus::WaitingForConfirmation);
    w.svc.advanceDays(w.admin, 1);  // day 14
    CHECK(w.issue(id).status == IssueStatus::ClosedNoResponse);   // never "Resolved"
    CHECK(w.issue(id).status != IssueStatus::Resolved);
    CHECK(toString(w.issue(id).status) == "Closed (No Response)");
    json an = w.svc.analytics(w.admin);
    CHECK(an["closedNoResponse"] == 1 && an["resolved"] == 0);

    // one big jump closes in a single step
    json b = w.report(2, "Other", "The campus gate lights are always off at night");
    startFix(w, b["issueId"]);
    w.svc.advanceDays(w.admin, 40);
    CHECK(w.issue(b["issueId"]).status == IssueStatus::ClosedNoResponse);
}

static void testModeration() {
    std::cout << "abusive-comment moderation\n";
    World w;
    json held = w.report(0, "Teaching", "The teacher is a stupid idiot and useless");
    CHECK(held["held"] == true);
    CHECK(w.svc.listIssues(w.faculty, json::object()).size() == 0);  // not visible to faculty
    json q = w.svc.moderationQueue(w.admin);
    CHECK(q.size() == 1 && !q[0]["reason"].get<std::string>().empty());
    CHECK_THROWS(403, w.svc.moderate(w.faculty, q[0]["id"], true));
    w.svc.moderate(w.admin, q[0]["id"], false);
    CHECK(w.svc.moderationQueue(w.admin).size() == 0);
    CHECK(w.svc.listIssues(w.faculty, json::object()).size() == 0);
    CHECK(w.svc.myReports(w.students[0])[0]["moderation"] == "Rejected");

    json held2 = w.report(1, "Teaching", "You are an idiot but also the class timing is wrong");
    CHECK(held2["held"] == true);
    json q2 = w.svc.moderationQueue(w.admin);
    json ok = w.svc.moderate(w.admin, q2[0]["id"], true);
    CHECK(ok["issueId"].get<int>() > 0);
    CHECK(w.svc.listIssues(w.faculty, json::object()).size() == 1);

    // abusive follow-up comment is held; staff see a placeholder, not the text
    json a = w.report(2, "Facilities", "The projector in room 204 is not working");
    startFix(w, a["issueId"]);
    json r = w.svc.confirm(w.students[2], a["issueId"], {{"solved", false}, {"comment", "you are all useless idiots"}});
    CHECK(r["held"] == true);
    json det = w.svc.getIssue(w.staff, a["issueId"]);
    CHECK(det["followUps"][0]["text"] == "[comment awaiting moderation]");
    CHECK(detectAbuse("The portal is slow and the garbage near the canteen smells").flagged == false);
    CHECK(detectAbuse("St00pid system").flagged == true);
}

static void testAnonymity() {
    std::cout << "anonymity\n";
    World w;
    json a = w.report(0, "Teaching", "The professor skips the DSA syllabus portion");
    w.report(1, "Teaching", "Teacher is not covering DSA syllabus properly");
    int id = a["issueId"];
    startFix(w, id);
    std::string dump = w.svc.listIssues(w.faculty, json::object()).dump() + w.svc.getIssue(w.faculty, id).dump() +
                       w.svc.analytics(w.faculty).dump();
    for (const char* secret : {"Alice", "Anand", "Bob", "Bhatt", "stu0", "stu1", "25100000", "25100001", "reference", "studentUserId"})
        CHECK(dump.find(secret) == std::string::npos);
    CHECK(dump.find("className") != std::string::npos);  // class impact IS shown
    // a student only ever sees their own reports
    json mine = w.svc.myReports(w.students[1]);
    CHECK(mine.size() == 1 && mine[0]["message"] == "Teacher is not covering DSA syllabus properly");
}

static void testPersistence() {
    std::cout << "file persistence\n";
    std::string path = (std::filesystem::temp_directory_path() / "fb_test_store.json").string();
    std::remove(path.c_str());
    int issueId, tid, uid;
    {
        Service s(path);
        s.bootstrapAdmin("Admin1234");
        int admin = s.state().users[0].id;
        s.setEnrollment(admin, {{"className", "CSE-A"}, {"count", 30}});
        s.createAccount(admin, {{"username", "stf"}, {"password", "Passw0rd1"}, {"name", "Sam Staff"}, {"role", "Staff"}});
        uid = s.registerStudent({{"username", "stu"}, {"password", "Passw0rd1"}, {"name", "Al Ice"}, {"studentId", "12345"}, {"className", "CSE-A"}})["id"];
        json r = s.submitReport(uid, {{"category", "Facilities"}, {"message", "The projector in room 204 is not working"}});
        issueId = r["issueId"];
        s.advanceDays(admin, 20);
        tid = 0;
        for (const auto& i : s.state().issues) if (i.id == issueId) tid = i.ticketId;
        CHECK(tid != 0);
    }
    Service s2(path);  // restart
    CHECK(s2.state().issues.size() == 1 && s2.state().tickets.size() == 1);
    CHECK(s2.state().issues[0].priority == Priority::Critical);
    CHECK(s2.state().clockOffset == 20LL * 86400);
    std::string tok = s2.login("stu", "Passw0rd1");  // password survives restart
    CHECK(s2.authenticate(tok) == uid);
    CHECK(s2.myReports(uid).size() == 1);
    CHECK(s2.notifications(uid)["items"].size() >= 1);
    std::remove(path.c_str());
    // corrupt file is refused rather than silently wiped
    { FILE* f = fopen(path.c_str(), "w"); fputs("{not json", f); fclose(f); }
    bool threw = false;
    try { Service s3(path); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    std::remove(path.c_str());

    std::filesystem::path missingPath = std::filesystem::temp_directory_path() /
        ("fb_missing_" + randomHex(8)) / "store.json";
    bool saveErrorReported = false;
    try {
        Service cannotSave(missingPath.string());
        cannotSave.bootstrapAdmin("Admin1234");
    } catch (const std::exception&) {
        saveErrorReported = true;
    }
    CHECK(saveErrorReported);
}

static void testAnalytics() {
    std::cout << "analytics\n";
    World w;
    w.report(0, "Facilities", "The projector in room 204 is not working");
    w.report(3, "Facilities", "Projector in classroom 204 is broken");
    w.report(5, "Portal", "ERP marks page is not loading for semester 3");
    json an = w.svc.analytics(w.admin);
    CHECK(an["totalIssues"] == 2 && an["open"] == 2);
    CHECK(an["byCategory"]["Facilities"] == 1);
    bool foundClass = false;
    for (auto& c : an["byClass"]) if (c["className"] == "CSE-A") { foundClass = true; CHECK(c["studentsAffected"] == 1); CHECK(c["percent"].get<double>() >= 3.3 && c["percent"].get<double>() < 3.4); }
    CHECK(foundClass);
    CHECK(an["reportsPerDay"].size() == 14);
    CHECK(an["reportsPerDay"][13]["reports"] == 3);
    CHECK(w.svc.analytics(w.faculty)["totalIssues"] == 0);  // scoped to the team's categories
    CHECK_THROWS(403, w.svc.analytics(w.students[0]));
}

int main() {
    testAccounts();
    testSubmissionAndGrouping();
    testPriorityAndAgeing();
    testClosedLoop();
    testNoResponse();
    testModeration();
    testAnonymity();
    testPersistence();
    testAnalytics();
    std::cout << "\n" << g_checks << " checks, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
