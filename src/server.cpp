// Crow REST API + web front end for the Closed Loop Feedback Management System.
//   ./feedback_server [--port 8080] [--host 127.0.0.1] [--data data/store.json] [--web web] [--demo]
#include "service.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

#include "crow.h"

using namespace fb;

namespace {

crow::response jsonResponse(int code, const json& body) {
    crow::response res(code, body.dump());
    res.set_header("Content-Type", "application/json; charset=utf-8");
    res.set_header("Cache-Control", "no-store");
    res.set_header("X-Content-Type-Options", "nosniff");
    return res;
}

std::string bearerToken(const crow::request& req) {
    std::string h = req.get_header_value("Authorization");
    const std::string prefix = "Bearer ";
    if (h.compare(0, prefix.size(), prefix) == 0) return h.substr(prefix.size());
    return "";
}

using Handler = std::function<json(int uid, const json& body)>;

// Every API route goes through here: auth, body parsing, error mapping.
crow::response run(Service& svc, const crow::request& req, bool needAuth, const Handler& h, int okCode = 200) {
    try {
        if (req.body.size() > 65536) return jsonResponse(413, {{"error", "Request too large."}});
        json body = json::object();
        if (!req.body.empty()) {
            body = json::parse(req.body, nullptr, false);
            if (body.is_discarded() || !body.is_object()) return jsonResponse(400, {{"error", "Body must be a JSON object."}});
        }
        int uid = 0;
        if (needAuth) uid = svc.authenticate(bearerToken(req));
        return jsonResponse(okCode, h(uid, body));
    } catch (const ApiError& e) {
        return jsonResponse(e.code, {{"error", e.what()}});
    } catch (const std::exception& e) {
        std::cerr << "internal error: " << e.what() << "\n";
        return jsonResponse(500, {{"error", "Something went wrong on the server."}});
    }
}

crow::response staticFile(const std::string& webDir, const std::string& name, const std::string& mime) {
    std::ifstream in(webDir + "/" + name, std::ios::binary);
    if (!in) return crow::response(404, "missing " + name);
    std::stringstream ss;
    ss << in.rdbuf();
    crow::response res(200, ss.str());
    res.set_header("Content-Type", mime);
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_header("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; frame-ancestors 'none'");
    res.set_header("Cache-Control", "no-cache");
    return res;
}

int intField(const json& b, const char* key, int fallback = -1) {
    return b.contains(key) && b[key].is_number_integer() ? b[key].get<int>() : fallback;
}

}  // namespace

int main(int argc, char** argv) {
    int port = 8080;
    std::string host = "127.0.0.1", dataFile = "data/store.json", webDir = "web";
    bool demo = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--host" && i + 1 < argc) host = argv[++i];
        else if (a == "--data" && i + 1 < argc) dataFile = argv[++i];
        else if (a == "--web" && i + 1 < argc) webDir = argv[++i];
        else if (a == "--demo") demo = true;
        else { std::cerr << "unknown option " << a << "\n"; return 2; }
    }

    return [&]() {
        std::unique_ptr<Service> s;
        try {
            s.reset(new Service(dataFile));
        } catch (const std::exception& e) {
            std::cerr << "Cannot start: " << e.what() << "\n";
            return 1;
        }
        Service& S = *s;
        if (!S.hasUsers()) {
            const char* envPw = std::getenv("FB_ADMIN_PASSWORD");
            std::string pw = S.bootstrapAdmin(envPw ? envPw : "");
            std::cout << "First run: created moderator account 'admin'.\n";
            if (!envPw) std::cout << "  Generated password (shown once): " << pw << "\n";
        }
        if (demo) {
            S.seedDemoData();
            std::cout << "Demo accounts (password Demo@1234): faculty1, staff1, student1..student6\n";
        }

        crow::SimpleApp app;
        app.loglevel(crow::LogLevel::Warning);

        // ---- web front end ----
        CROW_ROUTE(app, "/")([&]() { return staticFile(webDir, "index.html", "text/html; charset=utf-8"); });
        CROW_ROUTE(app, "/app.js")([&]() { return staticFile(webDir, "app.js", "application/javascript; charset=utf-8"); });
        CROW_ROUTE(app, "/style.css")([&]() { return staticFile(webDir, "style.css", "text/css; charset=utf-8"); });

        // ---- public ----
        CROW_ROUTE(app, "/api/health")([&]() { return jsonResponse(200, {{"ok", true}}); });
        CROW_ROUTE(app, "/api/classes")([&](const crow::request& r) {
            return run(S, r, false, [&](int, const json&) { return S.classes(); });
        });
        CROW_ROUTE(app, "/api/register").methods("POST"_method)([&](const crow::request& r) {
            return run(S, r, false, [&](int, const json& b) { return S.registerStudent(b); }, 201);
        });
        CROW_ROUTE(app, "/api/register-staff").methods("POST"_method)([&](const crow::request& r) {
            const char* code = std::getenv("FB_STAFF_CODE");
            std::string required = code ? code : "";
            return run(S, r, false, [&](int, const json& b) { return S.registerStaff(b, required); }, 201);
        });
        CROW_ROUTE(app, "/api/login").methods("POST"_method)([&](const crow::request& r) {
            return run(S, r, false, [&](int, const json& b) {
                std::string u = b.value("username", std::string());
                std::string p = b.value("password", std::string());
                std::string token = S.login(u, p);
                return json{{"token", token}, {"user", S.me(S.authenticate(token))}};
            });
        });
        CROW_ROUTE(app, "/api/logout").methods("POST"_method)([&](const crow::request& r) {
            return run(S, r, true, [&](int, const json&) { S.logout(bearerToken(r)); return json{{"ok", true}}; });
        });
        CROW_ROUTE(app, "/api/me")([&](const crow::request& r) {
            return run(S, r, true, [&](int uid, const json&) { return S.me(uid); });
        });

        // ---- students ----
        CROW_ROUTE(app, "/api/reports").methods("POST"_method)([&](const crow::request& r) {
            return run(S, r, true, [&](int uid, const json& b) { return S.submitReport(uid, b); }, 201);
        });
        CROW_ROUTE(app, "/api/my/reports")([&](const crow::request& r) {
            return run(S, r, true, [&](int uid, const json&) { return S.myReports(uid); });
        });
        CROW_ROUTE(app, "/api/issues/<int>/confirm").methods("POST"_method)([&](const crow::request& r, int id) {
            return run(S, r, true, [&](int uid, const json& b) { return S.confirm(uid, id, b); });
        });

        // ---- faculty / staff / moderator ----
        CROW_ROUTE(app, "/api/issues")([&](const crow::request& r) {
            return run(S, r, true, [&](int uid, const json&) {
                json f = json::object();
                for (const char* k : {"include", "category", "priority"})
                    if (r.url_params.get(k)) f[k] = std::string(r.url_params.get(k));
                return S.listIssues(uid, f);
            });
        });
        CROW_ROUTE(app, "/api/issues/<int>")([&](const crow::request& r, int id) {
            return run(S, r, true, [&](int uid, const json&) { return S.getIssue(uid, id); });
        });
        CROW_ROUTE(app, "/api/issues/<int>/review").methods("POST"_method)([&](const crow::request& r, int id) {
            return run(S, r, true, [&](int uid, const json&) { return S.reviewIssue(uid, id); });
        });
        CROW_ROUTE(app, "/api/issues/<int>/ticket").methods("POST"_method)([&](const crow::request& r, int id) {
            return run(S, r, true, [&](int uid, const json&) { return S.openTicket(uid, id); }, 201);
        });
        CROW_ROUTE(app, "/api/tickets/<int>/claim").methods("POST"_method)([&](const crow::request& r, int id) {
            return run(S, r, true, [&](int uid, const json& b) { return S.claimTicket(uid, id, b); });
        });
        CROW_ROUTE(app, "/api/tickets/<int>/note").methods("POST"_method)([&](const crow::request& r, int id) {
            return run(S, r, true, [&](int uid, const json& b) { return S.addTicketNote(uid, id, b); });
        });
        CROW_ROUTE(app, "/api/tickets/<int>/done").methods("POST"_method)([&](const crow::request& r, int id) {
            return run(S, r, true, [&](int uid, const json& b) { return S.completeTicket(uid, id, b); });
        });
        CROW_ROUTE(app, "/api/team")([&](const crow::request& r) {
            return run(S, r, true, [&](int uid, const json&) { return S.teamMembers(uid); });
        });
        CROW_ROUTE(app, "/api/analytics")([&](const crow::request& r) {
            return run(S, r, true, [&](int uid, const json&) { return S.analytics(uid); });
        });

        // ---- notifications ----
        CROW_ROUTE(app, "/api/notifications")([&](const crow::request& r) {
            return run(S, r, true, [&](int uid, const json&) { return S.notifications(uid); });
        });
        CROW_ROUTE(app, "/api/notifications/read").methods("POST"_method)([&](const crow::request& r) {
            return run(S, r, true, [&](int uid, const json&) { return S.markNotificationsRead(uid); });
        });

        // ---- moderator / admin ----
        CROW_ROUTE(app, "/api/moderation")([&](const crow::request& r) {
            return run(S, r, true, [&](int uid, const json&) { return S.moderationQueue(uid); });
        });
        CROW_ROUTE(app, "/api/moderation/<int>").methods("POST"_method)([&](const crow::request& r, int id) {
            return run(S, r, true, [&](int uid, const json& b) {
                if (!b.contains("approve") || !b["approve"].is_boolean()) throw ApiError(400, "Send {\"approve\": true|false}.");
                return S.moderate(uid, id, b["approve"].get<bool>());
            });
        });
        CROW_ROUTE(app, "/api/admin/accounts").methods("POST"_method)([&](const crow::request& r) {
            return run(S, r, true, [&](int uid, const json& b) { return S.createAccount(uid, b); }, 201);
        });
        CROW_ROUTE(app, "/api/admin/enrollment").methods("POST"_method)([&](const crow::request& r) {
            return run(S, r, true, [&](int uid, const json& b) { return S.setEnrollment(uid, b); });
        });
        CROW_ROUTE(app, "/api/admin/advance-days").methods("POST"_method)([&](const crow::request& r) {
            return run(S, r, true, [&](int uid, const json& b) { return S.advanceDays(uid, intField(b, "days", 0)); });
        });

        // ---- background rules: ageing, reminders, closure ----
        std::atomic<bool> stop{false};
        std::thread ticker([&]() {
            while (!stop) {
                for (int i = 0; i < 30 && !stop; ++i) std::this_thread::sleep_for(std::chrono::seconds(1));
                if (!stop) S.tick();
            }
        });

        std::cout << "Closed Loop Feedback server: http://" << host << ":" << port << "/  (data: " << dataFile << ")\n";
        app.bindaddr(host).port(static_cast<uint16_t>(port)).multithreaded().run();
        stop = true;
        ticker.join();
        return 0;
    }();
}
