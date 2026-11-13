// Console front end: the Phase 2 menu-driven program, now on top of the full engine
// (login, grouping, auto priority, tickets, all-reporter confirmation, moderation, saved data).
//   ./feedback_console [--data data/console.json] [--demo]
#include "service.h"
#include "util.h"
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>

using namespace fb;

namespace {

std::string ask(const std::string& prompt) {
    std::cout << prompt;
    std::string s;
    if (!std::getline(std::cin, s)) { std::cout << "\n"; std::exit(0); }
    return trim(s);
}
int askInt(const std::string& prompt, int fallback = -1) {
    std::string s = ask(prompt);
    try { size_t used = 0; int v = std::stoi(s, &used); if (used == s.size()) return v; } catch (...) {}
    return fallback;
}
std::string when(long long t) {
    time_t tt = static_cast<time_t>(t);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", std::localtime(&tt));
    return buf;
}

void printIssueRow(const json& i) {
    std::cout << "#" << std::setw(3) << i["id"].get<int>() << " [" << std::setw(8) << i["priority"].get<std::string>() << "] "
              << std::setw(10) << i["category"].get<std::string>() << " | " << i["impact"]["students"].get<int>() << " student(s), "
              << i["impact"]["classes"].size() << " class(es) | " << i["status"].get<std::string>() << " | " << i["title"].get<std::string>() << "\n";
}

void printIssue(const json& d) {
    std::cout << "\nIssue #" << d["id"] << "  " << d["category"].get<std::string>() << "  priority " << d["priority"].get<std::string>()
              << " (score " << d["score"] << ")  status: " << d["status"].get<std::string>() << "\n";
    for (const auto& x : d["descriptions"]) std::cout << "  - \"" << x["text"].get<std::string>() << "\" (" << x["className"].get<std::string>() << ")\n";
    for (const auto& x : d["followUps"]) std::cout << "  follow-up: " << x["text"].get<std::string>() << "\n";
    std::cout << "  Class impact:\n";
    for (const auto& c : d["impact"]["classes"])
        std::cout << "    " << c["className"].get<std::string>() << ": " << c["students"] << "/" << c["enrolled"] << " students (" << c["percent"] << "%)\n";
    const auto& b = d["priorityBreakdown"];
    std::cout << "  Priority points: students " << b["pointsStudents"] << ", classes " << b["pointsClasses"] << ", percent " << b["pointsPercent"]
              << ", keywords " << b["pointsKeywords"] << ", age " << b["pointsAge"] << " (" << b["ageDays"] << " days open)\n";
    if (d.contains("confirmation")) {
        const auto& c = d["confirmation"];
        std::cout << "  Confirmation: " << c["yes"] << " yes, " << c["no"] << " no, " << c["pending"] << " pending\n";
    }
    if (d.contains("ticketDetail")) {
        const auto& t = d["ticketDetail"];
        std::cout << "  Ticket #" << t["id"] << " " << t["status"].get<std::string>() << (t["assignee"].is_null() ? " (unassigned)" : " owner " + t["assignee"].get<std::string>()) << "\n";
        for (const auto& n : t["notes"]) std::cout << "    note: " << n["text"].get<std::string>() << " (" << n["by"].get<std::string>() << ")\n";
    }
    std::cout << "  History:\n";
    for (const auto& h : d["history"]) std::cout << "    " << when(h["at"]) << "  " << h["text"].get<std::string>() << "\n";
}

void showNotifications(Service& s, int uid) {
    json n = s.notifications(uid);
    std::cout << "\nNotifications (" << n["unread"] << " unread)\n";
    for (const auto& x : n["items"]) std::cout << (x["read"].get<bool>() ? "  " : "* ") << when(x["at"]) << "  " << x["text"].get<std::string>() << "\n";
    s.markNotificationsRead(uid);
}

void studentMenu(Service& s, int uid) {
    while (true) {
        std::cout << "\nStudent\n1. Report a problem\n2. My reports / confirm a fix\n3. Notifications\n0. Log out\n";
        int c = askInt("Choose: ");
        try {
            if (c == 0) return;
            if (c == 1) {
                std::string cat = ask("Category (Teaching / Facilities / Portal / Other): ");
                std::string msg = ask("Describe the problem: ");
                json r = s.submitReport(uid, {{"category", cat}, {"message", msg}});
                if (r["held"].get<bool>()) std::cout << r["message"].get<std::string>() << "\n";
                else std::cout << "Saved. Reference " << r["reference"] << ", issue #" << r["issueId"] << ", " << r["studentsAffected"]
                               << " student(s) affected, priority " << r["priority"].get<std::string>() << ".\n";
            } else if (c == 2) {
                json mine = s.myReports(uid);
                if (mine.empty()) { std::cout << "You have not reported anything.\n"; continue; }
                for (const auto& r : mine) {
                    std::cout << "\nReference " << r["reference"] << " [" << r["category"].get<std::string>() << "] "
                              << (r.contains("status") ? r["status"].get<std::string>() : r["moderation"].get<std::string>()) << "\n  " << r["message"].get<std::string>() << "\n";
                    if (r.value("needsMyConfirmation", false)) {
                        if (r.contains("actionTaken")) std::cout << "  Action taken: " << r["actionTaken"].get<std::string>() << "\n";
                        int a = askInt("  Was it solved? (1 yes, 2 no, 0 skip): ");
                        if (a == 1) s.confirm(uid, r["issueId"], {{"solved", true}});
                        else if (a == 2) s.confirm(uid, r["issueId"], {{"solved", false}, {"comment", ask("  What is still wrong? ")}});
                    }
                }
            } else if (c == 3) showNotifications(s, uid);
        } catch (const ApiError& e) { std::cout << "! " << e.what() << "\n"; }
    }
}

void teamMenu(Service& s, int uid, const std::string& roleName) {
    while (true) {
        std::cout << "\n" << roleName << "\n1. List issues (highest priority first)\n2. Open an issue\n3. Analytics\n4. Notifications\n0. Log out\n";
        int c = askInt("Choose: ");
        try {
            if (c == 0) return;
            if (c == 1) {
                json l = s.listIssues(uid, {{"include", "all"}});
                if (l.empty()) std::cout << "No issues.\n";
                for (const auto& i : l) printIssueRow(i);
            } else if (c == 2) {
                int id = askInt("Issue number: ");
                json d = s.getIssue(uid, id);
                printIssue(d);
                const auto& a = d["actions"];
                std::cout << "\nActions:";
                if (a["review"]) std::cout << " [1] review";
                if (a["openTicket"]) std::cout << " [2] open ticket";
                if (a["claim"]) std::cout << " [3] take ticket";
                if (a["note"]) std::cout << " [4] add note";
                if (a["complete"]) std::cout << " [5] work done";
                std::cout << " [0] back\n";
                int act = askInt("Choose: ", 0);
                int tid = d.contains("ticketDetail") ? d["ticketDetail"]["id"].get<int>() : 0;
                if (act == 1 && a["review"]) s.reviewIssue(uid, id);
                else if (act == 2 && a["openTicket"]) s.openTicket(uid, id);
                else if (act == 3 && a["claim"]) s.claimTicket(uid, tid, json::object());
                else if (act == 4 && a["note"]) s.addTicketNote(uid, tid, {{"text", ask("Note: ")}});
                else if (act == 5 && a["complete"]) { s.completeTicket(uid, tid, {{"actionTaken", ask("What was done? ")}}); std::cout << "Students have been asked to confirm.\n"; }
                if (act > 0) std::cout << "Done.\n";
            } else if (c == 3) {
                json a = s.analytics(uid);
                std::cout << "Open " << a["open"] << " | Resolved " << a["resolved"] << " | Closed(no response) " << a["closedNoResponse"]
                          << " | Open tickets " << a["openTickets"] << " | Avg days to resolve " << a["avgResolutionDays"] << "\n";
                for (const auto& cl : a["byClass"]) std::cout << "  " << cl["className"].get<std::string>() << ": " << cl["studentsAffected"] << "/" << cl["enrolled"] << " affected (" << cl["percent"] << "%)\n";
            } else if (c == 4) showNotifications(s, uid);
        } catch (const ApiError& e) { std::cout << "! " << e.what() << "\n"; }
    }
}

void moderatorMenu(Service& s, int uid) {
    while (true) {
        std::cout << "\nModerator\n1. Moderation queue\n2. All issues\n3. Analytics\n4. Create Faculty/Staff account\n5. Set class enrollment\n6. Advance demo clock\n7. Notifications\n0. Log out\n";
        int c = askInt("Choose: ");
        try {
            if (c == 0) return;
            if (c == 1) {
                json q = s.moderationQueue(uid);
                if (q.empty()) std::cout << "Nothing flagged.\n";
                for (const auto& i : q) {
                    std::cout << "\n#" << i["id"] << " " << i["kind"].get<std::string>() << " by " << i["student"].get<std::string>() << "\n  \"" << i["message"].get<std::string>()
                              << "\"\n  flagged: " << i["reason"].get<std::string>() << "\n";
                    int a = askInt("  1 approve, 2 reject, 0 skip: ");
                    if (a == 1 || a == 2) s.moderate(uid, i["id"], a == 1);
                }
            } else if (c == 2) {
                for (const auto& i : s.listIssues(uid, {{"include", "all"}})) printIssueRow(i);
            } else if (c == 3) {
                json a = s.analytics(uid);
                std::cout << a.dump(2) << "\n";
            } else if (c == 4) {
                json r = s.createAccount(uid, {{"username", ask("Username: ")}, {"password", ask("Password: ")}, {"name", ask("Full name: ")}, {"role", ask("Role (Faculty/Staff/Moderator): ")}});
                std::cout << "Created " << r["username"].get<std::string>() << ".\n";
            } else if (c == 5) {
                std::string cls = ask("Class name: ");
                s.setEnrollment(uid, {{"className", cls}, {"count", askInt("Enrolled students: ", 0)}});
                std::cout << "Saved.\n";
            } else if (c == 6) {
                s.advanceDays(uid, askInt("Days: ", 0));
                std::cout << "Clock moved forward.\n";
            } else if (c == 7) showNotifications(s, uid);
        } catch (const ApiError& e) { std::cout << "! " << e.what() << "\n"; }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string dataFile = "data/console.json";
    bool demo = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) dataFile = argv[++i];
        else if (a == "--demo") demo = true;
        else { std::cerr << "unknown option " << a << "\n"; return 2; }
    }
    std::unique_ptr<Service> sp;
    try { sp.reset(new Service(dataFile)); } catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
    Service& s = *sp;
    if (!s.hasUsers()) {
        const char* env = std::getenv("FB_ADMIN_PASSWORD");
        std::string pw = s.bootstrapAdmin(env ? env : "");
        std::cout << "First run: moderator account 'admin' created" << (env ? "." : ", password " + pw + " (shown once).") << "\n";
    }
    if (demo) {
        s.seedDemoData();
        std::cout << "Demo logins (password Demo@1234): Faculty IDs FAC1001 and FAC1002, Staff IDs STF1001 and STF1002, Student IDs 25100001000 to 25100001011\n";
    }
    std::cout << "Closed Loop Feedback Management System (console)\n";
    while (true) {
        std::cout << "\n1. Log in\n2. Register as a student\n0. Exit\n";
        int c = askInt("Choose: ");
        try {
            if (c == 0) break;
            if (c == 2) {
                json cl = s.classes();
                std::cout << "Classes:";
                for (const auto& x : cl) std::cout << " " << x["name"].get<std::string>();
                std::cout << "\n";
                s.registerStudent({{"name", ask("Full name: ")}, {"studentId", ask("Student ID: ")}, {"className", ask("Class: ")},
                                   {"username", ask("Username: ")}, {"password", ask("Password: ")}});
                std::cout << "Registered. Please log in.\n";
            } else if (c == 1) {
                std::string u = ask("Username: "), p = ask("Password: ");
                int uid = s.authenticate(s.login(u, p));
                json me = s.me(uid);
                std::cout << "Welcome, " << me["name"].get<std::string>() << " (" << me["role"].get<std::string>() << "), " << me["unread"] << " unread notification(s).\n";
                std::string role = me["role"];
                if (role == "Student") studentMenu(s, uid);
                else if (role == "Moderator") moderatorMenu(s, uid);
                else teamMenu(s, uid, role);
            }
        } catch (const ApiError& e) { std::cout << "! " << e.what() << "\n"; }
    }
    std::cout << "Goodbye. Your data is saved in " << dataFile << ".\n";
    return 0;
}
