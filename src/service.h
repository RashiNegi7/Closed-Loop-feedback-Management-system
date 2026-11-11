// Business logic for the Closed Loop Feedback Management System.
// Both the REST server and the console program are thin front ends over this class.
#ifndef FB_SERVICE_H
#define FB_SERVICE_H

#include "types.h"
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>

namespace fb {

using json = nlohmann::json;

// Priority score with the reasons behind it (shown to Faculty/Staff so the rule is transparent).
struct PriorityBreakdown {
    int students = 0;
    int classes = 0;
    double maxClassPercent = 0;
    int ageDays = 0;
    int pointsStudents = 0;
    int pointsClasses = 0;
    int pointsPercent = 0;
    int pointsKeywords = 0;
    int pointsAge = 0;
    int score = 0;
    Priority level = Priority::Low;
    std::vector<std::string> criticalWords;
    std::vector<std::string> urgentWords;
};

class Service {
public:
    // dataFile == "" keeps everything in memory (used by the tests).
    explicit Service(std::string dataFile = "", Config cfg = Config());

    // ---------- accounts ----------
    json registerStudent(const json& body);
    json registerStaff(const json& body, const std::string& requiredCode);  // Faculty/Staff self sign-up, needs the admin-shared access code
    std::string login(const std::string& username, const std::string& password);
    std::string loginById(const std::string& id, const std::string& password);  // Student ID, Faculty/Staff ID or admin username
    void logout(const std::string& token);
    int authenticate(const std::string& token);  // -> user id, throws ApiError 401
    json me(int uid);
    json classes() const;
    json courses() const;

    // ---------- students ----------
    json submitReport(int uid, const json& body);   // {category, message}
    json myReports(int uid);
    json confirm(int uid, int issueId, const json& body);  // {solved: bool, comment?: string}

    // ---------- faculty / staff ----------
    json listIssues(int uid, const json& filter);   // {include: open|closed|all, category?, priority?}
    json getIssue(int uid, int issueId);
    json reviewIssue(int uid, int issueId);
    json openTicket(int uid, int issueId);
    json claimTicket(int uid, int ticketId, const json& body);  // {assigneeId?}
    json addTicketNote(int uid, int ticketId, const json& body);  // {text}
    json completeTicket(int uid, int ticketId, const json& body); // {actionTaken}
    json teamMembers(int uid);

    // ---------- moderation & administration ----------
    json moderationQueue(int uid);
    json moderate(int uid, int reportId, bool approve);
    json createAccount(int uid, const json& body);   // {username,password,name,role}
    json setEnrollment(int uid, const json& body);   // {className, count}
    json advanceDays(int uid, int days);             // demo/test clock

    // ---------- shared ----------
    json notifications(int uid);
    json markNotificationsRead(int uid);
    json analytics(int uid);

    // Time-driven rules: ageing, escalation, reminders, no-response closure.
    void tick();

    // ---------- bootstrap / test helpers ----------
    bool hasUsers() const;
    // Creates the first administrator. Returns the password that was used.
    std::string bootstrapAdmin(const std::string& password = "");
    void seedDemoData();
    void seedDemoComplaints();  // sample complaints worked through the full loop (demo only)
    time_t now() const;
    const State& state() const { return st_; }
    const Config& config() const { return cfg_; }

private:
    friend struct CommitGuard;
    State st_;
    Config cfg_;
    std::string dataFile_;
    mutable std::recursive_mutex mu_;

    struct Session { int uid; time_t expires; };
    std::map<std::string, Session> sessions_;
    std::map<std::string, std::pair<int, time_t>> loginFailures_;

    // lookups
    User* findUser(int id);
    const User* findUser(int id) const;
    Issue* findIssue(int id);
    Report* findReport(int id);
    Ticket* findTicket(int id);
    const User& requireRole(int uid, std::initializer_list<Role> roles);
    const User& requireTeamFor(int uid, const Issue& issue);
    template <typename T> static int nextId(const std::vector<T>& v);

    // core rules
    void admitReport(Report& r);
    PriorityBreakdown computePriority(const Issue& issue) const;
    void refreshPriority(Issue& issue, bool announce);
    Ticket& createTicket(Issue& issue, bool automatic);
    void addHistory(Issue& issue, const std::string& text);
    void notify(int userId, int issueId, const std::string& text);
    void notifyTeam(const Issue& issue, const std::string& text);
    void notifyRole(Role role, int issueId, const std::string& text);
    void notifyReporters(const Issue& issue, const std::string& text, bool onlyPending = false);
    std::set<int> reporters(const Issue& issue) const;
    void tickLocked();
    void persist();

    // views
    json impactJson(const Issue& issue) const;
    json issueSummary(const Issue& issue) const;
    json issueDetail(const Issue& issue, const User& viewer) const;
    json ticketJson(const Ticket& t, bool withNotes) const;
    json confirmationJson(const Issue& issue) const;
    bool canSeeCategory(const User& u, Category c) const;
};

}  // namespace fb

#endif
