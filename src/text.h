// Text analysis: report similarity (grouping), abusive-comment detection, priority keywords.
#ifndef FB_TEXT_H
#define FB_TEXT_H

#include "ds.h"
#include <string>

namespace fb {

// Lower-case, strip punctuation, drop stop words, stem, map synonyms to one canonical word.
ScratchSet<std::string> canonicalTokens(const std::string& text);

// 0..1. Average of Jaccard and cosine (binary) similarity of the canonical token sets.
double similarity(const ScratchSet<std::string>& a, const ScratchSet<std::string>& b);

struct AbuseResult {
    bool flagged = false;
    std::string reason;
};
AbuseResult detectAbuse(const std::string& text);

// Keywords that raise starting priority. Returned lists are the matched words.
ScratchVector<std::string> criticalKeywords(const std::string& text);
ScratchVector<std::string> urgentKeywords(const std::string& text);

}  // namespace fb

#endif
