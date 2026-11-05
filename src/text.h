// Text analysis: report similarity (grouping), abusive-comment detection, priority keywords.
#ifndef FB_TEXT_H
#define FB_TEXT_H

#include <set>
#include <string>
#include <vector>

namespace fb {

// Lower-case, strip punctuation, drop stop words, stem, map synonyms to one canonical word.
std::set<std::string> canonicalTokens(const std::string& text);

// 0..1. Average of Jaccard and cosine (binary) similarity of the canonical token sets.
double similarity(const std::set<std::string>& a, const std::set<std::string>& b);

struct AbuseResult {
    bool flagged = false;
    std::string reason;
};
AbuseResult detectAbuse(const std::string& text);

// Keywords that raise starting priority. Returned lists are the matched words.
std::vector<std::string> criticalKeywords(const std::string& text);
std::vector<std::string> urgentKeywords(const std::string& text);

}  // namespace fb

#endif
