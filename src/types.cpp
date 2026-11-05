#include "types.h"
#include <algorithm>
#include <cctype>

namespace fb {

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
}  // namespace

std::string toString(Role v) {
    switch (v) {
        case Role::Student: return "Student";
        case Role::Faculty: return "Faculty";
        case Role::Staff: return "Staff";
        case Role::Moderator: return "Moderator";
    }
    return "Student";
}
std::string toString(Category v) {
    switch (v) {
        case Category::Teaching: return "Teaching";
        case Category::Facilities: return "Facilities";
        case Category::Portal: return "Portal";
        case Category::Other: return "Other";
    }
    return "Other";
}
std::string toString(Priority v) {
    switch (v) {
        case Priority::Low: return "Low";
        case Priority::Medium: return "Medium";
        case Priority::High: return "High";
        case Priority::Critical: return "Critical";
    }
    return "Low";
}
std::string toString(IssueStatus v) {
    switch (v) {
        case IssueStatus::New: return "New";
        case IssueStatus::UnderReview: return "Under Review";
        case IssueStatus::InProgress: return "In progress";
        case IssueStatus::WaitingForConfirmation: return "Waiting for student confirmation";
        case IssueStatus::Resolved: return "Resolved";
        case IssueStatus::ClosedNoResponse: return "Closed (No Response)";
    }
    return "New";
}
std::string toString(Moderation v) {
    switch (v) {
        case Moderation::Approved: return "Approved";
        case Moderation::Flagged: return "Flagged";
        case Moderation::Rejected: return "Rejected";
    }
    return "Approved";
}
std::string toString(ReportKind v) { return v == ReportKind::Initial ? "Report" : "FollowUp"; }
std::string toString(Answer v) {
    switch (v) {
        case Answer::Pending: return "Pending";
        case Answer::Yes: return "Yes";
        case Answer::No: return "No";
    }
    return "Pending";
}
std::string toString(TicketStatus v) {
    switch (v) {
        case TicketStatus::Open: return "Open";
        case TicketStatus::Assigned: return "Assigned";
        case TicketStatus::Done: return "Done";
    }
    return "Open";
}

// Generic parser: matches against toString() of every enumerator, case-insensitively.
template <typename E>
static bool parseEnum(const std::string& s, E& out, int count) {
    std::string want = lower(s);
    for (int i = 0; i < count; ++i) {
        if (lower(toString(static_cast<E>(i))) == want) {
            out = static_cast<E>(i);
            return true;
        }
    }
    return false;
}

bool parseRole(const std::string& s, Role& o) { return parseEnum(s, o, 4); }
bool parseCategory(const std::string& s, Category& o) { return parseEnum(s, o, 4); }
bool parsePriority(const std::string& s, Priority& o) { return parseEnum(s, o, 4); }
bool parseIssueStatus(const std::string& s, IssueStatus& o) { return parseEnum(s, o, 6); }
bool parseModeration(const std::string& s, Moderation& o) { return parseEnum(s, o, 3); }
bool parseReportKind(const std::string& s, ReportKind& o) { return parseEnum(s, o, 2); }
bool parseAnswer(const std::string& s, Answer& o) { return parseEnum(s, o, 3); }
bool parseTicketStatus(const std::string& s, TicketStatus& o) { return parseEnum(s, o, 3); }

}  // namespace fb
