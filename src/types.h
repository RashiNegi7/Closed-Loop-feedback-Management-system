// Data model for the Closed Loop Feedback Management System.
#ifndef FB_TYPES_H
#define FB_TYPES_H

#include <ctime>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fb {

enum class Role { Student, Faculty, Staff, Moderator };
enum class Category { Teaching, Facilities, Portal, Other };
enum class Priority { Low, Medium, High, Critical };
enum class IssueStatus {
    New,
    UnderReview,
    InProgress,
    WaitingForConfirmation,
    Resolved,
    ClosedNoResponse
};
enum class Moderation { Approved, Flagged, Rejected };
enum class ReportKind { Initial, FollowUp };
enum class Answer { Pending, Yes, No };
enum class TicketStatus { Open, Assigned, Done };

// ---- name helpers (used for JSON, console and web) ----
std::string toString(Role v);
std::string toString(Category v);
std::string toString(Priority v);
std::string toString(IssueStatus v);
std::string toString(Moderation v);
std::string toString(ReportKind v);
std::string toString(Answer v);
std::string toString(TicketStatus v);

bool parseRole(const std::string& s, Role& out);
bool parseCategory(const std::string& s, Category& out);  // case-insensitive
bool parsePriority(const std::string& s, Priority& out);
bool parseIssueStatus(const std::string& s, IssueStatus& out);
bool parseModeration(const std::string& s, Moderation& out);
bool parseReportKind(const std::string& s, ReportKind& out);
bool parseAnswer(const std::string& s, Answer& out);
bool parseTicketStatus(const std::string& s, TicketStatus& out);

inline bool isOpen(IssueStatus s) {
    return s != IssueStatus::Resolved && s != IssueStatus::ClosedNoResponse;
}
// Teaching issues belong to Faculty; everything else belongs to Staff.
inline Role teamFor(Category c) { return c == Category::Teaching ? Role::Faculty : Role::Staff; }

// ---- records ----
struct User {
    int id = 0;
    std::string username;
    std::string name;
    std::string studentId;  // students only
    std::string className;  // students only
    Role role = Role::Student;
    std::string salt;
    std::string hash;
};

// One student's submission. Follow-up comments ("it is NOT solved") use the same
// record so that they pass through the same moderation path.
struct Report {
    int id = 0;  // internal id; shown to its author as a "reference number" only
    ReportKind kind = ReportKind::Initial;
    Category category = Category::Other;
    int issueId = 0;  // 0 while a report is held for moderation
    int studentUserId = 0;
    std::string className;
    std::string message;
    Moderation moderation = Moderation::Approved;
    std::string flagReason;
    time_t createdAt = 0;
    time_t moderatedAt = 0;
    int moderatorId = 0;
};

struct Confirmation {
    Answer answer = Answer::Pending;
    time_t at = 0;
};

struct HistoryEntry {
    time_t at = 0;
    std::string text;
};

struct Issue {
    int id = 0;
    Category category = Category::Other;
    std::string title;
    std::vector<int> reportIds;    // initial reports grouped into this issue
    std::vector<int> followUpIds;  // follow-up comments from students
    IssueStatus status = IssueStatus::New;
    Priority priority = Priority::Low;
    int score = 0;
    time_t createdAt = 0;
    time_t updatedAt = 0;
    time_t closedAt = 0;
    time_t waitingSince = 0;
    bool reminderSent = false;
    int ticketId = 0;
    int reopenCount = 0;
    std::map<int, Confirmation> confirmations;  // student user id -> answer
    std::string closeNote;
    std::vector<HistoryEntry> history;
};

struct TicketNote {
    int userId = 0;
    std::string text;
    time_t at = 0;
};

struct Ticket {
    int id = 0;
    int issueId = 0;
    Role team = Role::Staff;
    int assigneeId = 0;
    TicketStatus status = TicketStatus::Open;
    bool autoCreated = false;
    bool escalated = false;
    std::string actionTaken;  // shown to reporting students when they are asked to confirm
    std::vector<TicketNote> notes;  // internal working notes (team + moderator only)
    time_t createdAt = 0;
    time_t updatedAt = 0;
};

struct Notification {
    int id = 0;
    int userId = 0;
    int issueId = 0;
    std::string text;
    time_t at = 0;
    bool read = false;
};

struct State {
    std::vector<User> users;
    std::vector<Report> reports;
    std::vector<Issue> issues;
    std::vector<Ticket> tickets;
    std::vector<Notification> notifications;
    std::map<std::string, int> enrollment;  // class name -> number of enrolled students
    long long clockOffset = 0;              // demo clock: seconds added to real time
};

// Thrown by the service layer; carries an HTTP-style status code.
struct ApiError : std::runtime_error {
    int code;
    ApiError(int c, const std::string& msg) : std::runtime_error(msg), code(c) {}
};

struct Config {
    int ageDaysPerPoint = 3;        // +1 priority point for every N days an issue stays open
    int ageCapPoints = 6;           // ageing alone can reach Critical (6 points = 18 days)
    int reminderDays = 7;           // reminder after this many days waiting for confirmation
    int closeAfterReminderDays = 7; // then close as "no response" after this many more days
    int unassignedEscalationDays = 3;
    double groupThreshold = 0.40;   // similarity needed to join an existing issue
    int minMessageLength = 10;
    int maxMessageLength = 1000;
};

}  // namespace fb

#endif
