#include "text.h"
#include "util.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace fb {

namespace {

const std::set<std::string>& stopWords() {
    static const std::set<std::string> s = {
        "a", "an", "the", "is", "are", "was", "were", "be", "been", "am", "to", "of", "in", "on",
        "at", "for", "and", "or", "but", "so", "it", "its", "this", "that", "these", "those",
        "my", "our", "your", "their", "we", "i", "me", "us", "they", "he", "she", "you", "with",
        "from", "by", "as", "has", "have", "had", "do", "does", "did", "very", "too", "also",
        "please", "pls", "kindly", "since", "there", "here", "again", "always", "every", "all",
        "can", "could", "would", "should", "will", "just", "still", "some", "any", "no", "not",
        "dont", "doesnt", "isnt", "cant", "wont", "get", "gets", "getting", "got", "when", "while",
        "after", "before", "during", "than", "then", "up", "out", "into", "much", "many", "more",
        "really", "quite", "even", "which", "what", "who", "whom", "about", "being", "like",
        "need", "needs", "needed", "want", "wants", "issue", "problem", "problems", "issues"};
    return s;
}

// Words that mean the same thing for grouping purposes (applied after stemming).
const std::map<std::string, std::string>& synonyms() {
    static const std::map<std::string, std::string> m = {
        {"wi", "wifi"}, {"fi", "wifi"}, {"wifi", "wifi"}, {"internet", "wifi"},
        {"network", "wifi"}, {"connectivity", "wifi"}, {"wlan", "wifi"}, {"connection", "wifi"},
        {"proj", "projector"}, {"beamer", "projector"}, {"projector", "projector"},
        {"aircon", "ac"}, {"ac", "ac"}, {"cooling", "ac"}, {"conditioner", "ac"},
        {"teacher", "teacher"}, {"lecturer", "teacher"}, {"professor", "teacher"},
        {"prof", "teacher"}, {"faculty", "teacher"}, {"sir", "teacher"}, {"madam", "teacher"},
        {"instructor", "teacher"}, {"mam", "teacher"},
        {"class", "lecture"}, {"classroom", "room"}, {"lecture", "lecture"}, {"session", "lecture"},
        {"room", "room"}, {"hall", "room"}, {"lab", "lab"}, {"laboratory", "lab"},
        {"broken", "broken"}, {"damaged", "broken"}, {"faulty", "broken"}, {"defective", "broken"},
        {"malfunction", "broken"}, {"malfunctioning", "broken"}, {"fail", "broken"},
        {"failing", "broken"}, {"crash", "broken"}, {"crashing", "broken"}, {"crashed", "broken"},
        {"down", "broken"}, {"dead", "broken"}, {"stopped", "broken"}, {"working", "work"},
        {"work", "work"}, {"slow", "slow"}, {"sluggish", "slow"}, {"lag", "slow"}, {"laggy", "slow"},
        {"lagging", "slow"}, {"delay", "slow"}, {"delayed", "slow"}, {"late", "slow"},
        {"toilet", "washroom"}, {"restroom", "washroom"}, {"washroom", "washroom"},
        {"bathroom", "washroom"}, {"loo", "washroom"},
        {"dirty", "dirty"}, {"unclean", "dirty"}, {"filthy", "dirty"}, {"unhygienic", "dirty"},
        {"messy", "dirty"}, {"garbage", "dirty"}, {"smelly", "dirty"},
        {"canteen", "canteen"}, {"cafeteria", "canteen"}, {"mess", "canteen"},
        {"fan", "fan"}, {"fans", "fan"}, {"light", "light"}, {"lights", "light"}, {"bulb", "light"},
        {"lamp", "light"}, {"tubelight", "light"},
        {"erp", "portal"}, {"portal", "portal"}, {"website", "portal"}, {"site", "portal"},
        {"login", "login"}, {"signin", "login"}, {"password", "login"},
        {"marks", "marks"}, {"grades", "marks"}, {"grade", "marks"}, {"score", "marks"},
        {"scores", "marks"}, {"result", "marks"}, {"results", "marks"},
        {"syllabus", "syllabus"}, {"curriculum", "syllabus"},
        {"book", "book"}, {"books", "book"}, {"library", "library"},
        {"noisy", "noise"}, {"noise", "noise"}, {"loud", "noise"},
        {"water", "water"}, {"drinking", "water"}, {"cooler", "water"},
        {"bench", "furniture"}, {"benches", "furniture"}, {"chair", "furniture"},
        {"chairs", "furniture"}, {"desk", "furniture"}, {"desks", "furniture"},
        {"seat", "furniture"}, {"seats", "furniture"}, {"table", "furniture"},
        {"tables", "furniture"}};
    return m;
}

std::string stem(std::string w) {
    auto ends = [&](const char* suf) {
        std::string s(suf);
        return w.size() > s.size() + 2 && w.compare(w.size() - s.size(), s.size(), s) == 0;
    };
    if (ends("ing")) w.erase(w.size() - 3);
    else if (ends("ies")) w = w.substr(0, w.size() - 3) + "y";
    else if (ends("es") && !ends("ses")) w.erase(w.size() - 2);
    else if (ends("ed")) w.erase(w.size() - 2);
    else if (ends("ly")) w.erase(w.size() - 2);
    else if (ends("s") && !ends("ss")) w.erase(w.size() - 1);
    return w;
}

// Map look-alike characters and strip repeated letters: "st00pid", "stuuupid" -> "stupid".
std::string normaliseForAbuse(const std::string& in) {
    std::string out;
    for (unsigned char c : in) {
        char ch = static_cast<char>(std::tolower(c));
        switch (ch) {
            case '0': ch = 'o'; break;
            case '1': ch = 'i'; break;
            case '3': ch = 'e'; break;
            case '4': ch = 'a'; break;
            case '5': ch = 's'; break;
            case '7': ch = 't'; break;
            case '@': ch = 'a'; break;
            case '$': ch = 's'; break;
            case '!': ch = 'i'; break;
            default: break;
        }
        out.push_back(ch);
    }
    return out;
}

std::string collapseRepeats(const std::string& w) {
    std::string out;
    for (char c : w)
        if (out.empty() || out.back() != c) out.push_back(c);
    return out;
}

std::set<std::string> collapsedSet(const std::set<std::string>& in) {
    std::set<std::string> out;
    for (const std::string& w : in) out.insert(collapseRepeats(w));
    return out;
}

const std::set<std::string>& abusiveWords() {
    // Stored collapsed ("useless" -> "useles") so that "uselesss" and "useless" both match.
    static const std::set<std::string> s = collapsedSet({
        "stoopid", "idiotic", "moronic",
        "stupid", "idiot", "idiots", "moron", "morons", "dumb", "dumbass", "bastard", "bastards",
        "crap", "shit", "shitty", "bullshit", "fuck", "fucking", "fucker", "asshole", "bitch",
        "bitches", "damn", "jerk", "loser", "losers", "trash", "garbage", "scum", "pathetic",
        "useless", "worthless", "clown", "clowns", "fool", "fools", "retard", "retarded", "slut",
        "whore", "bloody", "hell", "sucks", "suck"});
    return s;
}
// Words that are only abusive when aimed at people; "garbage"/"trash" etc. are legitimate
// words in facility complaints, so they are scored but not enough on their own.
const std::set<std::string>& weakAbusiveWords() {
    static const std::set<std::string> s = collapsedSet({"garbage", "trash", "hell", "bloody", "damn", "suck",
                                            "sucks", "useless", "pathetic", "crap", "clown",
                                            "clowns", "fool", "fools", "loser", "losers"});
    return s;
}
const std::set<std::string>& threatWords() {
    static const std::set<std::string> s = collapsedSet({"kill", "murder", "die", "destroy", "beat", "hurt",
                                            "burn", "bomb", "shoot", "stab"});
    return s;
}
const std::set<std::string>& personWords() {
    static const std::set<std::string> s = collapsedSet({"you", "he", "she", "him", "her", "them", "they",
                                            "teacher", "sir", "madam", "mam", "prof", "professor",
                                            "staff", "faculty", "hod", "principal", "dean", "guys"});
    return s;
}

}  // namespace

std::set<std::string> canonicalTokens(const std::string& textIn) {
    std::string text = toLower(textIn);
    // join the most common split spellings before tokenising
    auto replaceAll = [&](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replaceAll("wi-fi", "wifi");
    replaceAll("wi fi", "wifi");
    replaceAll("not working", "broken");
    replaceAll("doesn't work", "broken");
    replaceAll("doesnt work", "broken");
    replaceAll("does not work", "broken");
    replaceAll("don't work", "broken");
    replaceAll("isn't working", "broken");
    replaceAll("isnt working", "broken");
    replaceAll("out of order", "broken");
    replaceAll("not available", "broken");
    replaceAll("not opening", "broken");
    replaceAll("not loading", "broken");
    replaceAll("not responding", "broken");

    std::set<std::string> out;
    for (const std::string& raw : splitWords(text)) {
        if (raw.size() < 2 || stopWords().count(raw)) continue;
        auto direct = synonyms().find(raw);
        std::string w = direct != synonyms().end() ? direct->second : stem(raw);
        auto syn = synonyms().find(w);
        if (syn != synonyms().end()) w = syn->second;
        if (w.size() < 2 || stopWords().count(w)) continue;
        out.insert(w);
    }
    return out;
}

namespace {
// Things a complaint is about. Two reports about different things are different issues even if
// they happen in the same room; two reports about the same thing get a small boost.
const std::set<std::string>& objectTokens() {
    static const std::set<std::string> s = {
        "projector", "ac", "fan", "light", "wifi", "furniture", "water", "board", "mic", "speaker",
        "computer", "printer", "door", "window", "lift", "marks", "syllabus", "book", "login",
        "noise", "plug", "socket", "switch", "attendance", "timetable", "washroom", "canteen"};
    return s;
}
// Where it happens. Same equipment in a different place is treated as a separate issue.
const std::set<std::string>& placeTokens() {
    static const std::set<std::string> s = {"lab", "library", "canteen", "washroom", "hostel",
                                            "ground", "auditorium", "office", "gate", "parking",
                                            "block", "building", "floor"};
    return s;
}
bool isNumber(const std::string& t) { return !t.empty() && std::isdigit(static_cast<unsigned char>(t[0])); }
bool disjointNonEmpty(const std::set<std::string>& x, const std::set<std::string>& y) {
    if (x.empty() || y.empty()) return false;
    for (const auto& t : x)
        if (y.count(t)) return false;
    return true;
}
}  // namespace

double similarity(const std::set<std::string>& a, const std::set<std::string>& b) {
    if (a.empty() || b.empty()) return 0.0;
    size_t inter = 0;
    for (const auto& t : a) inter += b.count(t);
    double uni = static_cast<double>(a.size() + b.size() - inter);
    double jaccard = uni > 0 ? inter / uni : 0.0;
    double cosine = inter / std::sqrt(static_cast<double>(a.size()) * static_cast<double>(b.size()));
    double score = 0.5 * jaccard + 0.5 * cosine;

    std::set<std::string> objA, objB, plA, plB, numA, numB;
    for (const auto& t : a) {
        if (objectTokens().count(t)) objA.insert(t);
        if (placeTokens().count(t)) plA.insert(t);
        if (isNumber(t)) numA.insert(t);
    }
    for (const auto& t : b) {
        if (objectTokens().count(t)) objB.insert(t);
        if (placeTokens().count(t)) plB.insert(t);
        if (isNumber(t)) numB.insert(t);
    }
    bool sharedObject = false;
    for (const auto& t : objA)
        if (objB.count(t)) sharedObject = true;
    if (sharedObject) score += 0.15;
    if (disjointNonEmpty(objA, objB)) score *= 0.5;   // projector vs fan
    if (disjointNonEmpty(plA, plB)) score *= 0.5;     // lab vs library
    if (disjointNonEmpty(numA, numB)) score *= 0.4;   // room 204 vs room 305
    return std::min(1.0, score);
}

AbuseResult detectAbuse(const std::string& text) {
    AbuseResult res;
    std::string norm = normaliseForAbuse(text);
    std::vector<std::string> words = splitWords(norm);

    int strong = 0, weak = 0;
    std::string matched;
    bool personNearby = false;
    for (const std::string& w : words) {
        std::string c = collapseRepeats(w);
        if (personWords().count(c)) personNearby = true;
    }
    for (const std::string& w : words) {
        std::string c = collapseRepeats(w);
        if (abusiveWords().count(c)) {
            if (weakAbusiveWords().count(c)) ++weak;
            else ++strong;
            if (matched.size() < 80) matched += (matched.empty() ? "" : ", ") + w;
        }
    }
    // Spaced-out letters ("s t u p i d"): join runs of single-letter words and test the result.
    {
        std::string run;
        auto checkRun = [&]() {
            if (run.size() >= 4) {
                std::string c = collapseRepeats(run);
                for (const char* bad : {"stupid", "idiot", "bastard", "asshole", "fuck", "shit", "moron"}) {
                    if (c.find(bad) != std::string::npos) {
                        ++strong;
                        matched += (matched.empty() ? "" : ", ") + std::string(bad);
                    }
                }
            }
            run.clear();
        };
        for (const std::string& w : words) {
            if (w.size() == 1 && std::isalpha(static_cast<unsigned char>(w[0]))) run += w;
            else checkRun();
        }
        checkRun();
    }
    bool threat = false;
    if (personNearby) {
        for (const std::string& w : words)
            if (threatWords().count(collapseRepeats(w))) threat = true;
    }

    // shouting: mostly capital letters in a longer message
    size_t letters = 0, caps = 0;
    for (unsigned char c : text) {
        if (std::isalpha(c)) {
            ++letters;
            if (std::isupper(c)) ++caps;
        }
    }
    bool shouting = letters >= 20 && caps * 10 >= letters * 8;

    if (threat) {
        res.flagged = true;
        res.reason = "possible threat or violent language";
    } else if (strong > 0) {
        res.flagged = true;
        res.reason = "abusive language (" + matched + ")";
    } else if (weak >= 2 || (weak >= 1 && personNearby && words.size() <= 12)) {
        res.flagged = true;
        res.reason = "possibly abusive language (" + matched + ")";
    } else if (shouting) {
        res.flagged = true;
        res.reason = "message is written mostly in capital letters";
    }
    return res;
}

namespace {
std::vector<std::string> matchPrefixes(const std::string& text, const std::vector<std::string>& list) {
    std::vector<std::string> found;
    std::vector<std::string> words = splitWords(text);
    std::string joined = " " + toLower(text) + " ";
    for (const std::string& key : list) {
        bool hit = false;
        if (key.find(' ') != std::string::npos) {
            hit = joined.find(key) != std::string::npos;
        } else {
            for (const std::string& w : words)
                if (w.compare(0, key.size(), key) == 0) { hit = true; break; }
        }
        if (hit) found.push_back(key);
    }
    return found;
}
}  // namespace

std::vector<std::string> criticalKeywords(const std::string& text) {
    static const std::vector<std::string> k = {
        "fire", "smoke", "shock", "electrocut", "harass", "ragging", "assault", "threat",
        "unsafe", "danger", "injur", "collaps", "flood", "gas leak", "bully", "bullying",
        "discriminat", "abus", "molest", "stalk", "suicid", "self harm", "short circuit",
        "sparking", "exposed wire", "food poison", "fainted", "ambulance"};
    return matchPrefixes(text, k);
}

std::vector<std::string> urgentKeywords(const std::string& text) {
    static const std::vector<std::string> k = {
        "exam", "marks", "grade", "result", "fee", "payment", "attendance", "deadline",
        "submission", "internal", "assignment", "hall ticket", "admit card", "scholarship",
        "placement", "cannot access", "can't access", "unable to", "locked out"};
    return matchPrefixes(text, k);
}

}  // namespace fb
