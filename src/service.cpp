#include "service.h"
#include "ds.h"
#include "store.h"
#include "text.h"
#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <iostream>

namespace fb {

namespace {
const long long DAY = 86400;
const time_t SESSION_TTL = 12 * 3600;
const int MAX_LOGIN_FAILURES = 5;
const time_t LOGIN_WINDOW = 300;

int rank(Priority p) { return static_cast<int>(p); }

Priority levelForScore(int score) {
    if (score >= 6) return Priority::Critical;
    if (score >= 4) return Priority::High;
    if (score >= 2) return Priority::Medium;
    return Priority::Low;
}

std::string str(const json& body, const char* key) {
    if (!body.is_object() || !body.contains(key) || !body[key].is_string()) return "";
    return trim(body[key].get<std::string>());
}

bool validUsername(const std::string& u) {
    if (u.size() < 3 || u.size() > 32) return false;
    for (unsigned char c : u)
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) return false;
    return true;
}

void checkPassword(const std::string& p) {
    bool letter = false, digit = false;
    for (unsigned char c : p) {
        if (std::isalpha(c)) letter = true;
        if (std::isdigit(c)) digit = true;
    }
    if (p.size() < 8 || p.size() > 128 || !letter || !digit)
        throw ApiError(400, "Password must be 8-128 characters and contain a letter and a digit.");
}

// Orders open issues for display: highest priority first, then higher score, then older issue.
struct IssuePriorityOrder {
    bool operator()(const Issue* l, const Issue* r) const {
        if (l->priority != r->priority) return rank(l->priority) < rank(r->priority);
        if (l->score != r->score) return l->score < r->score;
        return l->id > r->id;
    }
};
}  // namespace

// Persists after a successful mutating operation (not when an ApiError unwinds).
struct CommitGuard {
    Service& s;
    std::lock_guard<std::recursive_mutex> lk;
    int ex;
    explicit CommitGuard(Service& x) : s(x), lk(x.mu_), ex(std::uncaught_exceptions()) {}
    ~CommitGuard() noexcept(false) {
        if (std::uncaught_exceptions() == ex) s.persist();
    }
};

#define READ_LOCK std::lock_guard<std::recursive_mutex> lk_(mu_)

// ------------------------------------------------------------------ construction

Service::Service(std::string dataFile, Config cfg) : cfg_(cfg), dataFile_(std::move(dataFile)) {
    if (!dataFile_.empty()) {
        State loaded;
        if (loadState(dataFile_, loaded)) st_ = std::move(loaded);
    }
}

time_t Service::now() const { return std::time(nullptr) + static_cast<time_t>(st_.clockOffset); }

void Service::persist() {
    if (dataFile_.empty()) return;
    saveState(dataFile_, st_);
}

bool Service::hasUsers() const {
    READ_LOCK;
    return !st_.users.empty();
}

// ------------------------------------------------------------------ lookups

template <typename T>
int Service::nextId(const std::vector<T>& v) {
    int m = 0;
    for (const auto& x : v) m = std::max(m, x.id);
    return m + 1;
}

User* Service::findUser(int id) {
    for (auto& u : st_.users) if (u.id == id) return &u;
    return nullptr;
}
const User* Service::findUser(int id) const {
    for (const auto& u : st_.users) if (u.id == id) return &u;
    return nullptr;
}
Issue* Service::findIssue(int id) {
    for (auto& i : st_.issues) if (i.id == id) return &i;
    return nullptr;
}
Report* Service::findReport(int id) {
    for (auto& r : st_.reports) if (r.id == id) return &r;
    return nullptr;
}
Ticket* Service::findTicket(int id) {
    for (auto& t : st_.tickets) if (t.id == id) return &t;
    return nullptr;
}

// Central permission check: every public operation starts here.
const User& Service::requireRole(int uid, std::initializer_list<Role> roles) {
    const User* u = findUser(uid);
    if (!u) throw ApiError(401, "Please log in.");
    for (Role r : roles)
        if (u->role == r) return *u;
    throw ApiError(403, "Your role (" + toString(u->role) + ") is not allowed to do this.");
}

// Only the team that owns the issue's category (Faculty for Teaching, Staff otherwise) may act on it.
const User& Service::requireTeamFor(int uid, const Issue& issue) {
    return requireRole(uid, {teamFor(issue.category)});
}

bool Service::canSeeCategory(const User& u, Category c) const {
    if (u.role == Role::Moderator) return true;
    return u.role == teamFor(c);
}

// ------------------------------------------------------------------ accounts

namespace {
struct CourseInfo { const char* name; const char* code; std::vector<std::string> sections; };
const std::vector<CourseInfo>& courseCatalog() {
    static const std::vector<CourseInfo> c = {
        {"B.Tech CSE", "CSE", {"A", "B", "C", "D", "E", "F", "G", "H", "I", "ML1", "ML2", "ML3", "ML4", "ML5", "ML6", "CS1", "CS2", "DS1", "DS2"}}};
    return c;
}
const int MAX_SEMESTER = 8;
const int DEFAULT_SECTION_ENROLLMENT = 60;
}  // namespace

json Service::courses() const {
    json arr = json::array();
    for (const auto& c : courseCatalog()) arr.push_back({{"name", c.name}, {"code", c.code}, {"semesters", MAX_SEMESTER}, {"sections", c.sections}});
    return arr;
}

json Service::registerStudent(const json& body) {
    CommitGuard g(*this);
    std::string username = toLower(str(body, "username"));
    std::string password = body.is_object() && body.contains("password") && body["password"].is_string()
                               ? body["password"].get<std::string>() : "";
    std::string name = str(body, "name");
    std::string studentId = str(body, "studentId");
    std::string className = str(body, "className");
    std::string course = str(body, "course");
    std::string section = str(body, "section");
    int semester = 0;

    if (!validUsername(username)) throw ApiError(400, "Username must be 3-32 letters, digits, '.', '_' or '-'.");
    checkPassword(password);
    if (!isNameWithoutDigits(name) || name.size() > 80) throw ApiError(400, "Name must contain letters and no digits.");
    if (!isAllDigits(studentId) || studentId.size() < 5 || studentId.size() > 20)
        throw ApiError(400, "Student ID must be 5-20 digits.");
    bool structured = !course.empty();
    if (structured) {
        const CourseInfo* ci = nullptr;
        for (const auto& c : courseCatalog()) if (course == c.name) ci = &c;
        if (!ci) throw ApiError(400, "Unknown course. Choose a course from the list.");
        if (body.contains("semester")) {
            const json& sv = body["semester"];
            if (sv.is_number_integer()) semester = sv.get<int>();
            else if (sv.is_string() && isAllDigits(sv.get<std::string>()) && sv.get<std::string>().size() <= 2) semester = std::stoi(sv.get<std::string>());
        }
        if (semester < 1 || semester > MAX_SEMESTER) throw ApiError(400, "Semester must be between 1 and 8.");
        bool okSection = false;
        for (const auto& s : ci->sections) if (s == section) okSection = true;
        if (!okSection) throw ApiError(400, "Unknown section. Choose a section from the list.");
        className = std::string(ci->code) + "-S" + std::to_string(semester) + "-" + section;
    } else if (!st_.enrollment.count(className)) {
        throw ApiError(400, "Unknown class. Choose a class from the list.");
    }
    for (const auto& u : st_.users) {
        if (u.username == username) throw ApiError(409, "That username is already taken.");
        if (u.role == Role::Student && u.studentId == studentId) throw ApiError(409, "That student ID is already registered.");
    }
    User u;
    u.id = nextId(st_.users);
    u.username = username;
    u.name = name;
    u.studentId = studentId;
    u.className = className;
    if (structured) {
        u.course = course;
        u.semester = semester;
        u.section = section;
        st_.enrollment.emplace(className, DEFAULT_SECTION_ENROLLMENT);
    }
    u.role = Role::Student;
    u.salt = randomHex(16);
    u.hash = hashPassword(password, u.salt);
    st_.users.push_back(u);
    return {{"id", u.id}, {"username", u.username}, {"name", u.name}, {"role", "Student"}};
}

json Service::registerStaff(const json& body, const std::string& requiredCode) {
    CommitGuard g(*this);
    if (requiredCode.empty()) throw ApiError(403, "Faculty and staff sign-up is closed. Ask the admin to create your account.");
    time_t t = std::time(nullptr);
    auto& fail = loginFailures_["\x01staff-signup"];
    if (fail.first >= MAX_LOGIN_FAILURES && t - fail.second < LOGIN_WINDOW)
        throw ApiError(429, "Too many wrong access codes. Try again in a few minutes.");
    if (t - fail.second >= LOGIN_WINDOW) fail = {0, t};
    if (!constantTimeEquals(str(body, "accessCode"), requiredCode)) {
        if (fail.first == 0) fail.second = t;
        ++fail.first;
        throw ApiError(403, "Wrong access code. Ask the admin for the code.");
    }
    std::string username = toLower(str(body, "username"));
    std::string password = body.is_object() && body.contains("password") && body["password"].is_string()
                               ? body["password"].get<std::string>() : "";
    std::string name = str(body, "name");
    Role role;
    if (!parseRole(str(body, "role"), role) || (role != Role::Faculty && role != Role::Staff))
        throw ApiError(400, "Choose Faculty or Staff.");
    if (!validUsername(username)) throw ApiError(400, "Username must be 3-32 letters, digits, '.', '_' or '-'.");
    checkPassword(password);
    if (!isNameWithoutDigits(name) || name.size() > 80) throw ApiError(400, "Name must contain letters and no digits.");
    for (const auto& u : st_.users) if (u.username == username) throw ApiError(409, "That username is already taken.");
    loginFailures_.erase("\x01staff-signup");
    User u;
    u.id = nextId(st_.users);
    u.username = username;
    u.name = name;
    u.role = role;
    u.salt = randomHex(16);
    u.hash = hashPassword(password, u.salt);
    st_.users.push_back(u);
    return {{"id", u.id}, {"username", u.username}, {"name", u.name}, {"role", toString(u.role)}};
}

std::string Service::login(const std::string& username, const std::string& password) {
    CommitGuard g(*this);
    std::string uname = toLower(trim(username));
    time_t t = std::time(nullptr);
    auto& fail = loginFailures_[uname];
    if (fail.first >= MAX_LOGIN_FAILURES && t - fail.second < LOGIN_WINDOW)
        throw ApiError(429, "Too many failed attempts. Try again in a few minutes.");
    if (t - fail.second >= LOGIN_WINDOW) fail = {0, t};

    const User* found = nullptr;
    for (const auto& u : st_.users) if (u.username == uname) found = &u;
    // Hash even for unknown users so response time does not reveal which usernames exist.
    std::string salt = found ? found->salt : std::string("0000000000000000");
    std::string h = hashPassword(password, salt);
    if (!found || !constantTimeEquals(h, found->hash)) {
        if (fail.first == 0) fail.second = t;
        ++fail.first;
        throw ApiError(401, "Wrong username or password.");
    }
    loginFailures_.erase(uname);
    std::string token = randomHex(24);
    sessions_[token] = {found->id, t + SESSION_TTL};
    return token;
}

void Service::logout(const std::string& token) {
    READ_LOCK;
    sessions_.erase(token);
}

int Service::authenticate(const std::string& token) {
    {
        READ_LOCK;
        auto it = sessions_.find(token);
        if (it == sessions_.end() || it->second.expires < std::time(nullptr)) {
            if (it != sessions_.end()) sessions_.erase(it);
            throw ApiError(401, "Please log in.");
        }
    }
    tick();
    READ_LOCK;
    auto it = sessions_.find(token);
    if (it == sessions_.end()) throw ApiError(401, "Please log in.");
    return it->second.uid;
}

json Service::me(int uid) {
    READ_LOCK;
    const User& u = requireRole(uid, {Role::Student, Role::Faculty, Role::Staff, Role::Moderator});
    int unread = 0;
    for (const auto& n : st_.notifications) if (n.userId == uid && !n.read) ++unread;
    json j = {{"id", u.id}, {"username", u.username}, {"name", u.name}, {"role", toString(u.role)}, {"unread", unread}};
    if (u.role == Role::Student) {
        j["studentId"] = u.studentId;
        j["className"] = u.className;
        if (!u.course.empty()) {
            j["course"] = u.course;
            j["semester"] = u.semester;
            j["section"] = u.section;
        }
    }
    j["now"] = static_cast<long long>(now());
    return j;
}

json Service::classes() const {
    READ_LOCK;
    json arr = json::array();
    for (const auto& kv : st_.enrollment) arr.push_back({{"name", kv.first}, {"enrolled", kv.second}});
    return arr;
}

json Service::createAccount(int uid, const json& body) {
    CommitGuard g(*this);
    requireRole(uid, {Role::Moderator});
    std::string username = toLower(str(body, "username"));
    std::string password = body.contains("password") && body["password"].is_string() ? body["password"].get<std::string>() : "";
    std::string name = str(body, "name");
    Role role;
    if (!parseRole(str(body, "role"), role) || role == Role::Student)
        throw ApiError(400, "Role must be Faculty, Staff or Moderator. Students register themselves.");
    if (!validUsername(username)) throw ApiError(400, "Username must be 3-32 letters, digits, '.', '_' or '-'.");
    checkPassword(password);
    if (!isNameWithoutDigits(name)) throw ApiError(400, "Name must contain letters and no digits.");
    for (const auto& u : st_.users) if (u.username == username) throw ApiError(409, "That username is already taken.");
    User u;
    u.id = nextId(st_.users);
    u.username = username;
    u.name = name;
    u.role = role;
    u.salt = randomHex(16);
    u.hash = hashPassword(password, u.salt);
    st_.users.push_back(u);
    return {{"id", u.id}, {"username", u.username}, {"name", u.name}, {"role", toString(u.role)}};
}

json Service::setEnrollment(int uid, const json& body) {
    CommitGuard g(*this);
    requireRole(uid, {Role::Moderator});
    std::string cls = str(body, "className");
    if (cls.empty() || cls.size() > 40) throw ApiError(400, "Class name is required (max 40 characters).");
    if (!body.contains("count") || !body["count"].is_number_integer()) throw ApiError(400, "count must be a whole number.");
    int count = body["count"].get<int>();
    if (count < 1 || count > 5000) throw ApiError(400, "count must be between 1 and 5000.");
    st_.enrollment[cls] = count;
    return {{"className", cls}, {"enrolled", count}};
}

json Service::advanceDays(int uid, int days) {
    CommitGuard g(*this);
    requireRole(uid, {Role::Moderator});
    if (days < 1 || days > 365) throw ApiError(400, "days must be between 1 and 365.");
    st_.clockOffset += static_cast<long long>(days) * DAY;
    tickLocked();
    return {{"advancedDays", days}, {"now", static_cast<long long>(now())}};
}

// ------------------------------------------------------------------ notifications

void Service::notify(int userId, int issueId, const std::string& text) {
    Notification n;
    n.id = nextId(st_.notifications);
    n.userId = userId;
    n.issueId = issueId;
    n.text = text;
    n.at = now();
    st_.notifications.push_back(n);
}
void Service::notifyRole(Role role, int issueId, const std::string& text) {
    for (const auto& u : st_.users) if (u.role == role) notify(u.id, issueId, text);
}
void Service::notifyTeam(const Issue& issue, const std::string& text) {
    notifyRole(teamFor(issue.category), issue.id, text);
}
std::set<int> Service::reporters(const Issue& issue) const {
    std::set<int> out;
    for (int rid : issue.reportIds)
        for (const auto& r : st_.reports)
            if (r.id == rid && r.moderation == Moderation::Approved) out.insert(r.studentUserId);
    return out;
}
void Service::notifyReporters(const Issue& issue, const std::string& text, bool onlyPending) {
    for (int sid : reporters(issue)) {
        if (onlyPending) {
            auto it = issue.confirmations.find(sid);
            if (it == issue.confirmations.end() || it->second.answer != Answer::Pending) continue;
        }
        notify(sid, issue.id, text);
    }
}
void Service::addHistory(Issue& issue, const std::string& text) {
    issue.history.push_back({now(), text});
    issue.updatedAt = now();
}

json Service::notifications(int uid) {
    READ_LOCK;
    requireRole(uid, {Role::Student, Role::Faculty, Role::Staff, Role::Moderator});
    json arr = json::array();
    int unread = 0;
    for (auto it = st_.notifications.rbegin(); it != st_.notifications.rend(); ++it) {
        if (it->userId != uid) continue;
        if (!it->read) ++unread;
        if (arr.size() < 100)
            arr.push_back({{"id", it->id}, {"issueId", it->issueId}, {"text", it->text},
                           {"at", static_cast<long long>(it->at)}, {"read", it->read}});
    }
    return {{"unread", unread}, {"items", arr}};
}

json Service::markNotificationsRead(int uid) {
    CommitGuard g(*this);
    requireRole(uid, {Role::Student, Role::Faculty, Role::Staff, Role::Moderator});
    for (auto& n : st_.notifications) if (n.userId == uid) n.read = true;
    return {{"ok", true}};
}

// ------------------------------------------------------------------ priority

PriorityBreakdown Service::computePriority(const Issue& issue) const {
    PriorityBreakdown b;
    std::map<std::string, std::set<int>> perClass;
    std::set<int> students;
    std::string allText;
    for (int rid : issue.reportIds) {
        for (const auto& r : st_.reports) {
            if (r.id != rid || r.moderation != Moderation::Approved) continue;
            students.insert(r.studentUserId);
            perClass[r.className].insert(r.studentUserId);
            allText += r.message + " ";
        }
    }
    b.students = static_cast<int>(students.size());
    b.classes = static_cast<int>(perClass.size());
    for (const auto& kv : perClass) {
        auto e = st_.enrollment.find(kv.first);
        if (e != st_.enrollment.end() && e->second > 0)
            b.maxClassPercent = std::max(b.maxClassPercent, 100.0 * kv.second.size() / e->second);
    }
    b.pointsStudents = b.students >= 10 ? 3 : b.students >= 5 ? 2 : b.students >= 2 ? 1 : 0;
    b.pointsClasses = b.classes >= 4 ? 2 : b.classes >= 2 ? 1 : 0;
    b.pointsPercent = b.maxClassPercent >= 50 ? 2 : b.maxClassPercent >= 25 ? 1 : 0;
    b.criticalWords = criticalKeywords(allText);
    b.urgentWords = urgentKeywords(allText);
    b.pointsKeywords = !b.criticalWords.empty() ? 6 : (!b.urgentWords.empty() ? 1 : 0);

    time_t end = isOpen(issue.status) ? now() : (issue.closedAt ? issue.closedAt : now());
    b.ageDays = static_cast<int>(std::max<long long>(0, (end - issue.createdAt) / DAY));
    b.pointsAge = std::min(cfg_.ageCapPoints, b.ageDays / std::max(1, cfg_.ageDaysPerPoint));

    b.score = b.pointsStudents + b.pointsClasses + b.pointsPercent + b.pointsKeywords + b.pointsAge;
    b.level = levelForScore(b.score);
    return b;
}

Ticket& Service::createTicket(Issue& issue, bool automatic) {
    Ticket t;
    t.id = nextId(st_.tickets);
    t.issueId = issue.id;
    t.team = teamFor(issue.category);
    t.status = TicketStatus::Open;
    t.autoCreated = automatic;
    t.createdAt = t.updatedAt = now();
    st_.tickets.push_back(t);
    issue.ticketId = t.id;
    if (issue.status == IssueStatus::New) issue.status = IssueStatus::UnderReview;
    addHistory(issue, automatic ? "Ticket created automatically because the issue reached Critical priority; routed to " + toString(t.team)
                                : "Ticket opened by " + toString(t.team));
    notifyTeam(issue, "New ticket for issue #" + std::to_string(issue.id) + ": " + issue.title);
    notifyReporters(issue, "Issue #" + std::to_string(issue.id) + " has been escalated to a ticket and sent to " +
                               toString(t.team) + ".");
    return st_.tickets.back();
}

// Recomputes priority (impact + keywords + ageing). Priority never moves backwards for an open issue.
void Service::refreshPriority(Issue& issue, bool announce) {
    PriorityBreakdown b = computePriority(issue);
    Priority newLevel = b.level;
    if (isOpen(issue.status) && rank(newLevel) < rank(issue.priority)) newLevel = issue.priority;
    bool rose = rank(newLevel) > rank(issue.priority);
    issue.score = b.score;
    if (rose) {
        std::string from = toString(issue.priority);
        issue.priority = newLevel;
        addHistory(issue, "Priority rose from " + from + " to " + toString(newLevel) + " (score " + std::to_string(b.score) + ")");
        if (announce)
            notifyTeam(issue, "Issue #" + std::to_string(issue.id) + " priority rose to " + toString(newLevel) + ": " + issue.title);
    } else {
        issue.priority = newLevel;
    }
    if (isOpen(issue.status) && issue.priority == Priority::Critical && issue.ticketId == 0) createTicket(issue, true);
}

// ------------------------------------------------------------------ student reports

// Puts an approved report into the most similar open issue of the same category, or opens a new one.
void Service::admitReport(Report& r) {
    std::set<std::string> tokens = canonicalTokens(r.message);
    Issue* best = nullptr;
    double bestScore = 0;
    for (auto& issue : st_.issues) {
        if (!isOpen(issue.status) || issue.category != r.category) continue;
        for (int rid : issue.reportIds) {
            for (const auto& other : st_.reports) {
                if (other.id != rid || other.moderation != Moderation::Approved) continue;
                double s = similarity(tokens, canonicalTokens(other.message));
                if (s > bestScore) { bestScore = s; best = &issue; }
            }
        }
    }
    std::string ref = "Reference #" + std::to_string(r.id);
    if (best && bestScore >= cfg_.groupThreshold) {
        Issue& issue = *best;
        std::set<int> before = reporters(issue);
        issue.reportIds.push_back(r.id);
        r.issueId = issue.id;
        bool newStudent = !before.count(r.studentUserId);
        if (issue.status == IssueStatus::WaitingForConfirmation) {
            // A fresh report about something marked as fixed means it probably is not fixed.
            issue.status = IssueStatus::InProgress;
            issue.waitingSince = 0;
            issue.reminderSent = false;
            ++issue.reopenCount;
            for (auto& kv : issue.confirmations) kv.second = Confirmation();
            if (Ticket* t = findTicket(issue.ticketId)) t->status = t->assigneeId ? TicketStatus::Assigned : TicketStatus::Open;
            addHistory(issue, "Reopened: a new report arrived while waiting for student confirmation");
            notifyTeam(issue, "Issue #" + std::to_string(issue.id) + " was reopened by a new report.");
        } else {
            addHistory(issue, newStudent ? "Another student reported the same problem" : "A reporting student added more detail");
        }
        refreshPriority(issue, true);
        int others = static_cast<int>(reporters(issue).size()) - 1;
        notify(r.studentUserId, issue.id,
               ref + " was grouped into issue #" + std::to_string(issue.id) + " with " + std::to_string(std::max(0, others)) +
                   " other student(s). Status: " + toString(issue.status) + ".");
    } else {
        Issue issue;
        issue.id = nextId(st_.issues);
        issue.category = r.category;
        issue.title = truncate(r.message, 70);
        issue.reportIds.push_back(r.id);
        issue.status = IssueStatus::New;
        issue.createdAt = issue.updatedAt = now();
        r.issueId = issue.id;
        st_.issues.push_back(issue);
        Issue& ref2 = st_.issues.back();
        addHistory(ref2, "Issue created from a student report");
        refreshPriority(ref2, false);
        notifyTeam(ref2, "New " + toString(ref2.priority) + " priority issue #" + std::to_string(ref2.id) + ": " + ref2.title);
        notify(r.studentUserId, ref2.id, ref + " created issue #" + std::to_string(ref2.id) + ". Starting priority: " + toString(ref2.priority) + ".");
    }
}

json Service::submitReport(int uid, const json& body) {
    CommitGuard g(*this);
    const User& u = requireRole(uid, {Role::Student});
    Category cat;
    if (!parseCategory(str(body, "category"), cat)) throw ApiError(400, "Category must be Teaching, Facilities, Portal or Other.");
    std::string msg = str(body, "message");
    if (static_cast<int>(msg.size()) < cfg_.minMessageLength)
        throw ApiError(400, "Please describe the problem in at least " + std::to_string(cfg_.minMessageLength) + " characters.");
    if (static_cast<int>(msg.size()) > cfg_.maxMessageLength)
        throw ApiError(400, "Message is too long (max " + std::to_string(cfg_.maxMessageLength) + " characters).");

    Report r;
    r.id = nextId(st_.reports);
    r.kind = ReportKind::Initial;
    r.category = cat;
    r.studentUserId = u.id;
    r.className = u.className;
    r.message = msg;
    r.createdAt = now();
    AbuseResult abuse = detectAbuse(msg);
    if (abuse.flagged) {
        r.moderation = Moderation::Flagged;
        r.flagReason = abuse.reason;
        st_.reports.push_back(r);
        notifyRole(Role::Moderator, 0, "A report was flagged for moderation: " + abuse.reason);
        notify(u.id, 0, "Reference #" + std::to_string(r.id) + " is being checked by a moderator before it is processed.");
        return {{"reference", r.id}, {"held", true},
                {"message", "Your report was held for moderator review because it may contain abusive language. You can rewrite it politely and submit again."}};
    }
    st_.reports.push_back(r);
    Report& stored = st_.reports.back();
    admitReport(stored);
    const Issue* issue = nullptr;
    for (const auto& i : st_.issues) if (i.id == stored.issueId) issue = &i;
    return {{"reference", stored.id}, {"held", false}, {"issueId", stored.issueId},
            {"priority", toString(issue->priority)}, {"status", toString(issue->status)},
            {"studentsAffected", static_cast<int>(reporters(*issue).size())}};
}

json Service::myReports(int uid) {
    READ_LOCK;
    requireRole(uid, {Role::Student});
    json arr = json::array();
    for (auto it = st_.reports.rbegin(); it != st_.reports.rend(); ++it) {
        const Report& r = *it;
        if (r.studentUserId != uid || r.kind != ReportKind::Initial) continue;
        json j = {{"reference", r.id}, {"category", toString(r.category)}, {"message", r.message},
                  {"moderation", toString(r.moderation)}, {"at", static_cast<long long>(r.createdAt)}};
        if (r.moderation == Moderation::Flagged) j["note"] = "Held for moderator review";
        if (r.moderation == Moderation::Rejected) j["note"] = "Not accepted by the moderator";
        if (r.issueId) {
            for (const auto& i : st_.issues) {
                if (i.id != r.issueId) continue;
                j["issueId"] = i.id;
                j["status"] = toString(i.status);
                j["priority"] = toString(i.priority);
                j["studentsAffected"] = static_cast<int>(reporters(i).size());
                j["history"] = json::array();
                for (const auto& h : i.history) j["history"].push_back({{"at", static_cast<long long>(h.at)}, {"text", h.text}});
                auto c = i.confirmations.find(uid);
                bool mine = i.status == IssueStatus::WaitingForConfirmation && c != i.confirmations.end() && c->second.answer == Answer::Pending;
                j["needsMyConfirmation"] = mine;
                if (c != i.confirmations.end()) j["myAnswer"] = toString(c->second.answer);
                if (i.status == IssueStatus::ClosedNoResponse) j["closeNote"] = i.closeNote;
                if (const Ticket* t = [&]() -> const Ticket* { for (const auto& tt : st_.tickets) if (tt.id == i.ticketId) return &tt; return nullptr; }())
                    if (!t->actionTaken.empty()) j["actionTaken"] = t->actionTaken;
            }
        }
        arr.push_back(j);
    }
    return arr;
}

// ------------------------------------------------------------------ the closed loop

json Service::confirm(int uid, int issueId, const json& body) {
    CommitGuard g(*this);
    requireRole(uid, {Role::Student});
    Issue* issue = findIssue(issueId);
    if (!issue || !reporters(*issue).count(uid)) throw ApiError(404, "You have not reported that issue.");
    if (issue->status != IssueStatus::WaitingForConfirmation) throw ApiError(409, "This issue is not waiting for your confirmation.");
    auto conf = issue->confirmations.find(uid);
    if (conf == issue->confirmations.end() || conf->second.answer != Answer::Pending) throw ApiError(409, "You have already answered.");
    if (!body.is_object() || !body.contains("solved") || !body["solved"].is_boolean())
        throw ApiError(400, "Send {\"solved\": true} or {\"solved\": false, \"comment\": \"...\"}.");
    bool solved = body["solved"].get<bool>();
    std::string idText = std::to_string(issue->id);

    if (solved) {
        conf->second = {Answer::Yes, now()};
        int yes = 0, total = static_cast<int>(issue->confirmations.size());
        for (const auto& kv : issue->confirmations) if (kv.second.answer == Answer::Yes) ++yes;
        addHistory(*issue, "A reporting student confirmed the fix (" + std::to_string(yes) + " of " + std::to_string(total) + ")");
        if (yes == total) {
            issue->status = IssueStatus::Resolved;
            issue->closedAt = now();
            addHistory(*issue, "Resolved: every reporting student confirmed the problem is solved");
            notifyReporters(*issue, "Issue #" + idText + " is resolved. Thank you for confirming.");
            notifyTeam(*issue, "Issue #" + idText + " is resolved: all reporting students confirmed.");
            return {{"issueId", issue->id}, {"status", toString(issue->status)}, {"resolved", true}};
        }
        return {{"issueId", issue->id}, {"status", toString(issue->status)}, {"resolved", false},
                {"message", "Thanks. The issue will be resolved once every reporting student confirms."}};
    }

    std::string comment = str(body, "comment");
    if (comment.size() < 5) throw ApiError(400, "Please explain what is still wrong (at least 5 characters).");
    if (comment.size() > static_cast<size_t>(cfg_.maxMessageLength)) throw ApiError(400, "Comment is too long.");
    Report f;
    f.id = nextId(st_.reports);
    f.kind = ReportKind::FollowUp;
    f.category = issue->category;
    f.issueId = issue->id;
    f.studentUserId = uid;
    f.className = findUser(uid)->className;
    f.message = comment;
    f.createdAt = now();
    AbuseResult abuse = detectAbuse(comment);
    if (abuse.flagged) {
        f.moderation = Moderation::Flagged;
        f.flagReason = abuse.reason;
        notifyRole(Role::Moderator, issue->id, "A follow-up comment was flagged for moderation: " + abuse.reason);
    }
    st_.reports.push_back(f);
    issue->followUpIds.push_back(f.id);
    issue->status = IssueStatus::InProgress;
    issue->waitingSince = 0;
    issue->reminderSent = false;
    ++issue->reopenCount;
    for (auto& kv : issue->confirmations) kv.second = Confirmation();
    if (Ticket* t = findTicket(issue->ticketId)) t->status = t->assigneeId ? TicketStatus::Assigned : TicketStatus::Open;
    addHistory(*issue, "A reporting student said the problem is NOT solved; sent back to In progress");
    notifyTeam(*issue, "Issue #" + idText + " was sent back: a student says it is not solved.");
    return {{"issueId", issue->id}, {"status", toString(issue->status)}, {"resolved", false}, {"reopened", true},
            {"held", abuse.flagged}};
}

// ------------------------------------------------------------------ faculty / staff

json Service::listIssues(int uid, const json& filter) {
    READ_LOCK;
    const User& u = requireRole(uid, {Role::Faculty, Role::Staff, Role::Moderator});
    std::string include = filter.value("include", std::string("open"));
    Category catFilter;
    bool hasCat = filter.contains("category") && filter["category"].is_string() && parseCategory(filter["category"].get<std::string>(), catFilter);
    Priority priFilter;
    bool hasPri = filter.contains("priority") && filter["priority"].is_string() && parsePriority(filter["priority"].get<std::string>(), priFilter);

    ScratchPriorityQueue<const Issue*, IssuePriorityOrder> queue;  // team-written heap from Phase 2
    std::vector<const Issue*> closed;
    for (const auto& i : st_.issues) {
        if (!canSeeCategory(u, i.category)) continue;
        if (hasCat && i.category != catFilter) continue;
        if (hasPri && i.priority != priFilter) continue;
        if (isOpen(i.status)) queue.push(&i);
        else closed.push_back(&i);
    }
    json arr = json::array();
    if (include != "closed")
        while (!queue.empty()) { arr.push_back(issueSummary(*queue.top())); queue.pop(); }
    if (include != "open") {
        std::sort(closed.begin(), closed.end(), [](const Issue* a, const Issue* b) { return a->closedAt > b->closedAt; });
        for (const Issue* i : closed) arr.push_back(issueSummary(*i));
    }
    return arr;
}

json Service::getIssue(int uid, int issueId) {
    READ_LOCK;
    const User& u = requireRole(uid, {Role::Faculty, Role::Staff, Role::Moderator});
    for (const auto& i : st_.issues)
        if (i.id == issueId) {
            if (!canSeeCategory(u, i.category)) throw ApiError(403, "That issue belongs to another team.");
            return issueDetail(i, u);
        }
    throw ApiError(404, "Issue not found.");
}

json Service::reviewIssue(int uid, int issueId) {
    CommitGuard g(*this);
    Issue* issue = findIssue(issueId);
    if (!issue) throw ApiError(404, "Issue not found.");
    requireTeamFor(uid, *issue);
    if (issue->status != IssueStatus::New) throw ApiError(409, "Only new issues can be moved to Under Review.");
    issue->status = IssueStatus::UnderReview;
    addHistory(*issue, "Under review by " + toString(teamFor(issue->category)));
    notifyReporters(*issue, "Issue #" + std::to_string(issue->id) + " is now under review.");
    return issueSummary(*issue);
}

json Service::openTicket(int uid, int issueId) {
    CommitGuard g(*this);
    Issue* issue = findIssue(issueId);
    if (!issue) throw ApiError(404, "Issue not found.");
    requireTeamFor(uid, *issue);
    if (!isOpen(issue->status)) throw ApiError(409, "This issue is closed.");
    if (issue->ticketId) throw ApiError(409, "This issue already has a ticket.");
    Ticket& t = createTicket(*issue, false);
    return ticketJson(t, true);
}

json Service::claimTicket(int uid, int ticketId, const json& body) {
    CommitGuard g(*this);
    Ticket* t = findTicket(ticketId);
    if (!t) throw ApiError(404, "Ticket not found.");
    Issue* issue = findIssue(t->issueId);
    requireTeamFor(uid, *issue);
    if (t->status != TicketStatus::Open) throw ApiError(409, "Ticket is not open for assignment.");
    int assignee = uid;
    if (body.is_object() && body.contains("assigneeId") && body["assigneeId"].is_number_integer()) {
        assignee = body["assigneeId"].get<int>();
        const User* a = findUser(assignee);
        if (!a || a->role != t->team) throw ApiError(400, "Assignee must belong to the " + toString(t->team) + " team.");
    }
    t->assigneeId = assignee;
    t->status = TicketStatus::Assigned;
    t->updatedAt = now();
    issue->status = IssueStatus::InProgress;
    addHistory(*issue, "Work started by " + toString(t->team));
    notifyReporters(*issue, "Work has started on issue #" + std::to_string(issue->id) + ".");
    if (assignee != uid) notify(assignee, issue->id, "Ticket #" + std::to_string(t->id) + " was assigned to you.");
    return ticketJson(*t, true);
}

json Service::addTicketNote(int uid, int ticketId, const json& body) {
    CommitGuard g(*this);
    Ticket* t = findTicket(ticketId);
    if (!t) throw ApiError(404, "Ticket not found.");
    requireTeamFor(uid, *findIssue(t->issueId));
    std::string text = str(body, "text");
    if (text.empty() || text.size() > 1000) throw ApiError(400, "Note must be 1-1000 characters.");
    t->notes.push_back({uid, text, now()});
    t->updatedAt = now();
    return ticketJson(*t, true);
}

// Staff/Faculty say the work is done. This does NOT resolve the issue; students decide that.
json Service::completeTicket(int uid, int ticketId, const json& body) {
    CommitGuard g(*this);
    Ticket* t = findTicket(ticketId);
    if (!t) throw ApiError(404, "Ticket not found.");
    Issue* issue = findIssue(t->issueId);
    requireTeamFor(uid, *issue);
    if (t->status != TicketStatus::Assigned) throw ApiError(409, "Claim the ticket before marking the work done.");
    std::string action = str(body, "actionTaken");
    if (action.size() < 5 || action.size() > 1000) throw ApiError(400, "Describe what was done (5-1000 characters).");
    t->status = TicketStatus::Done;
    t->actionTaken = action;
    t->updatedAt = now();
    issue->status = IssueStatus::WaitingForConfirmation;
    issue->waitingSince = now();
    issue->reminderSent = false;
    issue->confirmations.clear();
    for (int sid : reporters(*issue)) issue->confirmations[sid] = Confirmation();
    addHistory(*issue, "Work marked done; waiting for all " + std::to_string(issue->confirmations.size()) + " reporting student(s) to confirm");
    notifyReporters(*issue, "Issue #" + std::to_string(issue->id) + ": " + toString(t->team) + " says it is fixed (" + action + "). Was it really solved? Please confirm.");
    return issueSummary(*issue);
}

json Service::teamMembers(int uid) {
    READ_LOCK;
    const User& u = requireRole(uid, {Role::Faculty, Role::Staff});
    json arr = json::array();
    for (const auto& m : st_.users) if (m.role == u.role) arr.push_back({{"id", m.id}, {"name", m.name}});
    return arr;
}

// ------------------------------------------------------------------ moderation

json Service::moderationQueue(int uid) {
    READ_LOCK;
    requireRole(uid, {Role::Moderator});
    json arr = json::array();
    for (const auto& r : st_.reports) {
        if (r.moderation != Moderation::Flagged) continue;
        const User* s = findUser(r.studentUserId);
        arr.push_back({{"id", r.id}, {"kind", toString(r.kind)}, {"category", toString(r.category)},
                       {"message", r.message}, {"reason", r.flagReason}, {"issueId", r.issueId},
                       {"student", s ? s->name + " (" + s->studentId + ")" : ""}, {"className", r.className},
                       {"at", static_cast<long long>(r.createdAt)}});
    }
    return arr;
}

json Service::moderate(int uid, int reportId, bool approve) {
    CommitGuard g(*this);
    const User& mod = requireRole(uid, {Role::Moderator});
    Report* r = findReport(reportId);
    if (!r || r->moderation != Moderation::Flagged) throw ApiError(404, "No flagged item with that id.");
    r->moderatedAt = now();
    r->moderatorId = mod.id;
    std::string ref = "Reference #" + std::to_string(r->id);
    if (!approve) {
        r->moderation = Moderation::Rejected;
        notify(r->studentUserId, r->issueId, ref + " was not accepted because it broke the respectful-language rule. Please rewrite it politely.");
        return {{"id", r->id}, {"moderation", "Rejected"}};
    }
    r->moderation = Moderation::Approved;
    if (r->kind == ReportKind::Initial) {
        admitReport(*r);
    } else if (Issue* i = findIssue(r->issueId)) {
        addHistory(*i, "A student follow-up comment was approved by a moderator");
        notifyTeam(*i, "A follow-up comment on issue #" + std::to_string(i->id) + " is now visible.");
    }
    return {{"id", r->id}, {"moderation", "Approved"}, {"issueId", r->issueId}};
}

// ------------------------------------------------------------------ time-driven rules

void Service::tick() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto fingerprint = [&]() {
        size_t f = st_.notifications.size() * 7 + st_.tickets.size() * 13;
        for (const auto& i : st_.issues)
            f += i.history.size() * 31 + static_cast<size_t>(i.status) * 5 + static_cast<size_t>(i.priority) * 3 + (i.reminderSent ? 1 : 0);
        return f;
    };
    size_t before = fingerprint();
    tickLocked();
    if (fingerprint() != before) {
        try {
            persist();
        } catch (const std::exception& e) {
            std::cerr << "ERROR: could not save time-driven updates: " << e.what() << "\n";
        }
    }
}

void Service::tickLocked() {
    for (auto& issue : st_.issues) {
        if (!isOpen(issue.status)) continue;
        refreshPriority(issue, true);  // ageing + possible Critical escalation

        if (issue.status == IssueStatus::WaitingForConfirmation && issue.waitingSince) {
            time_t reminderDue = issue.waitingSince + static_cast<time_t>(cfg_.reminderDays) * DAY;
            time_t closeDue = reminderDue + static_cast<time_t>(cfg_.closeAfterReminderDays) * DAY;
            if (now() >= closeDue) {
                int pending = 0;
                for (const auto& kv : issue.confirmations) if (kv.second.answer == Answer::Pending) ++pending;
                issue.status = IssueStatus::ClosedNoResponse;
                issue.closedAt = now();
                issue.closeNote = std::to_string(pending) + " reporting student(s) never answered; closed without confirmation.";
                addHistory(issue, "Closed (No Response): " + issue.closeNote);
                notifyReporters(issue, "Issue #" + std::to_string(issue.id) + " was closed without a response. Report it again if the problem remains.", true);
                notifyTeam(issue, "Issue #" + std::to_string(issue.id) + " closed with no response (not counted as resolved).");
                continue;
            }
            if (now() >= reminderDue && !issue.reminderSent) {
                issue.reminderSent = true;
                addHistory(issue, "Reminder sent to students who have not answered");
                notifyReporters(issue, "Reminder: was issue #" + std::to_string(issue.id) + " really solved? Please answer in the portal.", true);
            }
        }
    }
    for (auto& t : st_.tickets) {
        if (t.status != TicketStatus::Open || t.escalated) continue;
        if (now() - t.createdAt >= static_cast<time_t>(cfg_.unassignedEscalationDays) * DAY) {
            t.escalated = true;
            addHistory(*findIssue(t.issueId), "Ticket unassigned for " + std::to_string(cfg_.unassignedEscalationDays) + " days; escalated to moderators");
            notifyRole(Role::Moderator, t.issueId, "Ticket #" + std::to_string(t.id) + " for issue #" + std::to_string(t.issueId) + " has had no owner for " + std::to_string(cfg_.unassignedEscalationDays) + " days.");
        }
    }
}

// ------------------------------------------------------------------ views

// Class impact: how many DISTINCT students in which classes. Never lists who they are.
json Service::impactJson(const Issue& issue) const {
    std::map<std::string, std::set<int>> perClass;
    std::set<int> all;
    for (int rid : issue.reportIds)
        for (const auto& r : st_.reports)
            if (r.id == rid && r.moderation == Moderation::Approved) {
                perClass[r.className].insert(r.studentUserId);
                all.insert(r.studentUserId);
            }
    json classes = json::array();
    double maxPct = 0;
    for (const auto& kv : perClass) {
        auto e = st_.enrollment.find(kv.first);
        int enrolled = e == st_.enrollment.end() ? 0 : e->second;
        double pct = enrolled > 0 ? std::round(1000.0 * kv.second.size() / enrolled) / 10.0 : 0.0;
        maxPct = std::max(maxPct, pct);
        classes.push_back({{"className", kv.first}, {"students", static_cast<int>(kv.second.size())},
                           {"enrolled", enrolled}, {"percent", pct}});
    }
    return {{"students", static_cast<int>(all.size())}, {"classes", classes}, {"maxPercent", maxPct}};
}

json Service::confirmationJson(const Issue& issue) const {
    int yes = 0, no = 0, pending = 0;
    for (const auto& kv : issue.confirmations) {
        if (kv.second.answer == Answer::Yes) ++yes;
        else if (kv.second.answer == Answer::No) ++no;
        else ++pending;
    }
    json j = {{"total", static_cast<int>(issue.confirmations.size())}, {"yes", yes}, {"no", no}, {"pending", pending},
              {"reminderSent", issue.reminderSent}};
    if (issue.waitingSince) {
        time_t rd = issue.waitingSince + static_cast<time_t>(cfg_.reminderDays) * DAY;
        j["reminderDue"] = static_cast<long long>(rd);
        j["closeDue"] = static_cast<long long>(rd + static_cast<time_t>(cfg_.closeAfterReminderDays) * DAY);
    }
    return j;
}

json Service::ticketJson(const Ticket& t, bool withNotes) const {
    const User* a = findUser(t.assigneeId);
    json j = {{"id", t.id}, {"issueId", t.issueId}, {"team", toString(t.team)}, {"status", toString(t.status)},
              {"assignee", a ? json(a->name) : json(nullptr)}, {"assigneeId", t.assigneeId},
              {"autoCreated", t.autoCreated}, {"escalated", t.escalated}, {"actionTaken", t.actionTaken},
              {"createdAt", static_cast<long long>(t.createdAt)}};
    if (withNotes) {
        j["notes"] = json::array();
        for (const auto& n : t.notes) {
            const User* u = findUser(n.userId);
            j["notes"].push_back({{"by", u ? u->name : "?"}, {"text", n.text}, {"at", static_cast<long long>(n.at)}});
        }
    }
    return j;
}

// Summary shown in lists. Contains no student identity and no internal report ids.
json Service::issueSummary(const Issue& issue) const {
    PriorityBreakdown b = computePriority(issue);
    json j = {{"id", issue.id}, {"category", toString(issue.category)}, {"title", issue.title},
              {"status", toString(issue.status)}, {"priority", toString(issue.priority)}, {"score", issue.score},
              {"createdAt", static_cast<long long>(issue.createdAt)}, {"updatedAt", static_cast<long long>(issue.updatedAt)},
              {"ageDays", b.ageDays}, {"impact", impactJson(issue)}, {"reopenCount", issue.reopenCount},
              {"team", toString(teamFor(issue.category))}};
    if (issue.ticketId)
        for (const auto& t : st_.tickets)
            if (t.id == issue.ticketId) j["ticket"] = {{"id", t.id}, {"status", toString(t.status)}};
    if (!issue.confirmations.empty()) j["confirmation"] = confirmationJson(issue);
    if (!issue.closeNote.empty()) j["closeNote"] = issue.closeNote;
    return j;
}

json Service::issueDetail(const Issue& issue, const User& viewer) const {
    json j = issueSummary(issue);
    PriorityBreakdown b = computePriority(issue);
    j["priorityBreakdown"] = {
        {"students", b.students}, {"classes", b.classes}, {"maxClassPercent", std::round(b.maxClassPercent * 10) / 10},
        {"ageDays", b.ageDays}, {"pointsStudents", b.pointsStudents}, {"pointsClasses", b.pointsClasses},
        {"pointsPercent", b.pointsPercent}, {"pointsKeywords", b.pointsKeywords}, {"pointsAge", b.pointsAge},
        {"score", b.score}, {"criticalWords", b.criticalWords}, {"urgentWords", b.urgentWords}};
    j["descriptions"] = json::array();
    for (int rid : issue.reportIds)
        for (const auto& r : st_.reports)
            if (r.id == rid && r.moderation == Moderation::Approved)
                j["descriptions"].push_back({{"text", r.message}, {"className", r.className}, {"at", static_cast<long long>(r.createdAt)}});
    j["followUps"] = json::array();
    for (int fid : issue.followUpIds)
        for (const auto& r : st_.reports)
            if (r.id == fid) {
                bool ok = r.moderation == Moderation::Approved;
                j["followUps"].push_back({{"text", ok ? r.message : (r.moderation == Moderation::Flagged ? "[comment awaiting moderation]" : "[comment removed by moderator]")},
                                          {"at", static_cast<long long>(r.createdAt)}});
            }
    j["history"] = json::array();
    for (const auto& h : issue.history) j["history"].push_back({{"at", static_cast<long long>(h.at)}, {"text", h.text}});
    if (issue.ticketId)
        for (const auto& t : st_.tickets)
            if (t.id == issue.ticketId) j["ticketDetail"] = ticketJson(t, true);

    bool team = viewer.role == teamFor(issue.category);
    bool open = isOpen(issue.status);
    const Ticket* tk = nullptr;
    for (const auto& t : st_.tickets) if (t.id == issue.ticketId) tk = &t;
    j["actions"] = {{"review", team && issue.status == IssueStatus::New},
                    {"openTicket", team && open && !tk},
                    {"claim", team && tk && tk->status == TicketStatus::Open},
                    {"note", team && tk != nullptr},
                    {"complete", team && tk && tk->status == TicketStatus::Assigned}};
    return j;
}

// ------------------------------------------------------------------ analytics

json Service::analytics(int uid) {
    READ_LOCK;
    const User& u = requireRole(uid, {Role::Faculty, Role::Staff, Role::Moderator});
    std::map<std::string, int> byStatus, byCategory, byPriority;
    std::map<std::string, std::set<int>> classStudents;  // class -> distinct students with an open issue
    int open = 0, resolved = 0, closedNoResp = 0, total = 0;
    double resDays = 0;
    std::vector<const Issue*> visible;
    for (const auto& i : st_.issues) {
        if (!canSeeCategory(u, i.category)) continue;
        visible.push_back(&i);
        ++total;
        ++byStatus[toString(i.status)];
        ++byCategory[toString(i.category)];
        if (isOpen(i.status)) {
            ++open;
            ++byPriority[toString(i.priority)];
            for (int rid : i.reportIds)
                for (const auto& r : st_.reports)
                    if (r.id == rid && r.moderation == Moderation::Approved) classStudents[r.className].insert(r.studentUserId);
        } else if (i.status == IssueStatus::Resolved) {
            ++resolved;
            resDays += static_cast<double>(i.closedAt - i.createdAt) / DAY;
        } else {
            ++closedNoResp;
        }
    }
    json byClass = json::array();
    for (const auto& kv : st_.enrollment) {
        int affected = classStudents.count(kv.first) ? static_cast<int>(classStudents[kv.first].size()) : 0;
        byClass.push_back({{"className", kv.first}, {"enrolled", kv.second}, {"studentsAffected", affected},
                           {"percent", kv.second > 0 ? std::round(1000.0 * affected / kv.second) / 10.0 : 0.0}});
    }
    // reports per day, last 14 days
    json perDay = json::array();
    time_t today = now() / DAY;
    for (int d = 13; d >= 0; --d) {
        int n = 0;
        for (const auto& r : st_.reports) {
            if (r.kind != ReportKind::Initial || r.moderation != Moderation::Approved || !canSeeCategory(u, r.category)) continue;
            if (r.createdAt / DAY == today - d) ++n;
        }
        perDay.push_back({{"daysAgo", d}, {"reports", n}});
    }
    ScratchPriorityQueue<const Issue*, IssuePriorityOrder> q;
    for (const Issue* i : visible) if (isOpen(i->status)) q.push(i);
    json top = json::array();
    for (int k = 0; k < 5 && !q.empty(); ++k) { top.push_back(issueSummary(*q.top())); q.pop(); }

    int openTickets = 0, unassigned = 0, flagged = 0;
    for (const auto& t : st_.tickets) {
        const Issue* i = nullptr;
        for (const auto& x : st_.issues) if (x.id == t.issueId) i = &x;
        if (!i || !canSeeCategory(u, i->category)) continue;
        if (t.status != TicketStatus::Done) ++openTickets;
        if (t.status == TicketStatus::Open) ++unassigned;
    }
    if (u.role == Role::Moderator)
        for (const auto& r : st_.reports) if (r.moderation == Moderation::Flagged) ++flagged;
    int closed = resolved + closedNoResp;
    return {{"totalIssues", total}, {"open", open}, {"resolved", resolved}, {"closedNoResponse", closedNoResp},
            {"avgResolutionDays", resolved ? std::round(10 * resDays / resolved) / 10 : 0.0},
            {"confirmationRate", closed ? std::round(1000.0 * resolved / closed) / 10.0 : 0.0},
            {"openTickets", openTickets}, {"unassignedTickets", unassigned}, {"flaggedPending", flagged},
            {"byStatus", byStatus}, {"byCategory", byCategory}, {"byPriority", byPriority},
            {"byClass", byClass}, {"reportsPerDay", perDay}, {"topIssues", top}};
}

// ------------------------------------------------------------------ bootstrap & demo data

std::string Service::bootstrapAdmin(const std::string& password) {
    CommitGuard g(*this);
    std::string pw = password.empty() ? randomHex(5) + "A1" : password;
    User u;
    u.id = nextId(st_.users);
    u.username = "admin";
    u.name = "System Moderator";
    u.role = Role::Moderator;
    u.salt = randomHex(16);
    u.hash = hashPassword(pw, u.salt);
    st_.users.push_back(u);
    return pw;
}

void Service::seedDemoData() {
    CommitGuard g(*this);
    st_.enrollment = {{"CSE-A", 60}, {"CSE-B", 55}, {"ECE-A", 50}};
    auto add = [&](const std::string& un, const std::string& name, Role role, const std::string& sid, const std::string& cls) {
        for (const auto& x : st_.users) if (x.username == un) return;
        User u;
        u.id = nextId(st_.users);
        u.username = un;
        u.name = name;
        u.role = role;
        u.studentId = sid;
        u.className = cls;
        u.salt = randomHex(16);
        u.hash = hashPassword("Demo@1234", u.salt);
        st_.users.push_back(u);
    };
    add("faculty1", "Dr Meera Sharma", Role::Faculty, "", "");
    add("staff1", "Ravi Rao", Role::Staff, "", "");
    const char* names[] = {"Asha Verma", "Kabir Singh", "Neha Joshi", "Rohan Mehta", "Isha Kapoor", "Dev Patel"};
    for (int i = 0; i < 6; ++i)
        add("student" + std::to_string(i + 1), names[i], Role::Student, "2510000" + std::to_string(1000 + i), i % 3 == 0 ? "CSE-A" : i % 3 == 1 ? "CSE-B" : "ECE-A");
}

}  // namespace fb
