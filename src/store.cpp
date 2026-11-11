#include "store.h"
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using nlohmann::json;

namespace fb {

namespace {

template <typename E>
E enumOr(const json& j, const char* key, bool (*parse)(const std::string&, E&), E fallback) {
    E out = fallback;
    if (j.contains(key) && j[key].is_string()) parse(j[key].get<std::string>(), out);
    return out;
}

json userJ(const User& u) {
    return {{"id", u.id}, {"username", u.username}, {"name", u.name}, {"studentId", u.studentId}, {"facultyId", u.facultyId},
            {"className", u.className}, {"course", u.course}, {"semester", u.semester}, {"section", u.section},
            {"role", toString(u.role)}, {"salt", u.salt}, {"hash", u.hash}};
}
User userFrom(const json& j) {
    User u;
    u.id = j.value("id", 0);
    u.username = j.value("username", "");
    u.name = j.value("name", "");
    u.studentId = j.value("studentId", "");
    u.facultyId = j.value("facultyId", "");
    u.className = j.value("className", "");
    u.course = j.value("course", "");
    u.semester = j.value("semester", 0);
    u.section = j.value("section", "");
    u.role = enumOr<Role>(j, "role", parseRole, Role::Student);
    u.salt = j.value("salt", "");
    u.hash = j.value("hash", "");
    return u;
}

json reportJ(const Report& r) {
    return {{"id", r.id}, {"kind", toString(r.kind)}, {"category", toString(r.category)}, {"issueId", r.issueId},
            {"studentUserId", r.studentUserId}, {"className", r.className}, {"message", r.message},
            {"moderation", toString(r.moderation)}, {"flagReason", r.flagReason},
            {"createdAt", r.createdAt}, {"moderatedAt", r.moderatedAt}, {"moderatorId", r.moderatorId}};
}
Report reportFrom(const json& j) {
    Report r;
    r.id = j.value("id", 0);
    r.kind = enumOr<ReportKind>(j, "kind", parseReportKind, ReportKind::Initial);
    r.category = enumOr<Category>(j, "category", parseCategory, Category::Other);
    r.issueId = j.value("issueId", 0);
    r.studentUserId = j.value("studentUserId", 0);
    r.className = j.value("className", "");
    r.message = j.value("message", "");
    r.moderation = enumOr<Moderation>(j, "moderation", parseModeration, Moderation::Approved);
    r.flagReason = j.value("flagReason", "");
    r.createdAt = j.value("createdAt", static_cast<long long>(0));
    r.moderatedAt = j.value("moderatedAt", static_cast<long long>(0));
    r.moderatorId = j.value("moderatorId", 0);
    return r;
}

json issueJ(const Issue& i) {
    json conf = json::object();
    for (const auto& kv : i.confirmations)
        conf[std::to_string(kv.first)] = {{"answer", toString(kv.second.answer)}, {"at", kv.second.at}};
    json hist = json::array();
    for (const auto& h : i.history) hist.push_back({{"at", h.at}, {"text", h.text}});
    return {{"id", i.id}, {"category", toString(i.category)}, {"title", i.title},
            {"reportIds", i.reportIds}, {"followUpIds", i.followUpIds},
            {"status", toString(i.status)}, {"priority", toString(i.priority)}, {"score", i.score},
            {"createdAt", i.createdAt}, {"updatedAt", i.updatedAt}, {"closedAt", i.closedAt},
            {"waitingSince", i.waitingSince}, {"reminderSent", i.reminderSent},
            {"ticketId", i.ticketId}, {"reopenCount", i.reopenCount}, {"confirmations", conf},
            {"closeNote", i.closeNote}, {"history", hist}};
}
Issue issueFrom(const json& j) {
    Issue i;
    i.id = j.value("id", 0);
    i.category = enumOr<Category>(j, "category", parseCategory, Category::Other);
    i.title = j.value("title", "");
    if (j.contains("reportIds")) i.reportIds = j["reportIds"].get<std::vector<int>>();
    if (j.contains("followUpIds")) i.followUpIds = j["followUpIds"].get<std::vector<int>>();
    i.status = enumOr<IssueStatus>(j, "status", parseIssueStatus, IssueStatus::New);
    i.priority = enumOr<Priority>(j, "priority", parsePriority, Priority::Low);
    i.score = j.value("score", 0);
    i.createdAt = j.value("createdAt", static_cast<long long>(0));
    i.updatedAt = j.value("updatedAt", static_cast<long long>(0));
    i.closedAt = j.value("closedAt", static_cast<long long>(0));
    i.waitingSince = j.value("waitingSince", static_cast<long long>(0));
    i.reminderSent = j.value("reminderSent", false);
    i.ticketId = j.value("ticketId", 0);
    i.reopenCount = j.value("reopenCount", 0);
    i.closeNote = j.value("closeNote", "");
    if (j.contains("confirmations")) {
        for (auto it = j["confirmations"].begin(); it != j["confirmations"].end(); ++it) {
            Confirmation c;
            c.answer = enumOr<Answer>(it.value(), "answer", parseAnswer, Answer::Pending);
            c.at = it.value().value("at", static_cast<long long>(0));
            i.confirmations[std::stoi(it.key())] = c;
        }
    }
    if (j.contains("history"))
        for (const auto& h : j["history"])
            i.history.push_back({h.value("at", static_cast<long long>(0)), h.value("text", "")});
    return i;
}

json ticketJ(const Ticket& t) {
    json notes = json::array();
    for (const auto& n : t.notes) notes.push_back({{"userId", n.userId}, {"text", n.text}, {"at", n.at}});
    return {{"id", t.id}, {"issueId", t.issueId}, {"team", toString(t.team)},
            {"assigneeId", t.assigneeId}, {"status", toString(t.status)},
            {"autoCreated", t.autoCreated}, {"escalated", t.escalated},
            {"actionTaken", t.actionTaken}, {"notes", notes}, {"createdAt", t.createdAt},
            {"updatedAt", t.updatedAt}};
}
Ticket ticketFrom(const json& j) {
    Ticket t;
    t.id = j.value("id", 0);
    t.issueId = j.value("issueId", 0);
    t.team = enumOr<Role>(j, "team", parseRole, Role::Staff);
    t.assigneeId = j.value("assigneeId", 0);
    t.status = enumOr<TicketStatus>(j, "status", parseTicketStatus, TicketStatus::Open);
    t.autoCreated = j.value("autoCreated", false);
    t.escalated = j.value("escalated", false);
    t.actionTaken = j.value("actionTaken", "");
    t.createdAt = j.value("createdAt", static_cast<long long>(0));
    t.updatedAt = j.value("updatedAt", static_cast<long long>(0));
    if (j.contains("notes"))
        for (const auto& n : j["notes"])
            t.notes.push_back({n.value("userId", 0), n.value("text", ""), n.value("at", static_cast<long long>(0))});
    return t;
}

json noteJ(const Notification& n) {
    return {{"id", n.id}, {"userId", n.userId}, {"issueId", n.issueId}, {"text", n.text},
            {"at", n.at}, {"read", n.read}};
}
Notification noteFrom(const json& j) {
    Notification n;
    n.id = j.value("id", 0);
    n.userId = j.value("userId", 0);
    n.issueId = j.value("issueId", 0);
    n.text = j.value("text", "");
    n.at = j.value("at", static_cast<long long>(0));
    n.read = j.value("read", false);
    return n;
}

}  // namespace

json stateToJson(const State& st) {
    json j;
    j["version"] = 1;
    j["clockOffset"] = st.clockOffset;
    j["enrollment"] = st.enrollment;
    j["users"] = json::array();
    for (const auto& u : st.users) j["users"].push_back(userJ(u));
    j["reports"] = json::array();
    for (const auto& r : st.reports) j["reports"].push_back(reportJ(r));
    j["issues"] = json::array();
    for (const auto& i : st.issues) j["issues"].push_back(issueJ(i));
    j["tickets"] = json::array();
    for (const auto& t : st.tickets) j["tickets"].push_back(ticketJ(t));
    j["notifications"] = json::array();
    for (const auto& n : st.notifications) j["notifications"].push_back(noteJ(n));
    return j;
}

State stateFromJson(const json& j) {
    State st;
    st.clockOffset = j.value("clockOffset", static_cast<long long>(0));
    if (j.contains("enrollment")) st.enrollment = j["enrollment"].get<std::map<std::string, int>>();
    if (j.contains("users")) for (const auto& x : j["users"]) st.users.push_back(userFrom(x));
    if (j.contains("reports")) for (const auto& x : j["reports"]) st.reports.push_back(reportFrom(x));
    if (j.contains("issues")) for (const auto& x : j["issues"]) st.issues.push_back(issueFrom(x));
    if (j.contains("tickets")) for (const auto& x : j["tickets"]) st.tickets.push_back(ticketFrom(x));
    if (j.contains("notifications")) for (const auto& x : j["notifications"]) st.notifications.push_back(noteFrom(x));
    return st;
}

bool loadState(const std::string& path, State& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::stringstream buf;
    buf << in.rdbuf();
    json j = json::parse(buf.str(), nullptr, false);
    if (j.is_discarded() || !j.is_object())
        throw std::runtime_error("data file " + path + " is corrupt; move it aside to start fresh");
    out = stateFromJson(j);
    return true;
}

void saveState(const std::string& path, const State& st) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + tmp);
        out << stateToJson(st).dump(1);
        out.flush();
        if (!out) throw std::runtime_error("write failed for " + tmp);
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD error = GetLastError();
        std::remove(tmp.c_str());
        throw std::system_error(static_cast<int>(error), std::system_category(), "cannot replace " + path);
    }
#else
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        int error = errno;
        std::remove(tmp.c_str());
        throw std::system_error(error, std::system_category(), "cannot replace " + path);
    }
#endif
}

}  // namespace fb
