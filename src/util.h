// Small helpers: SHA-256 password hashing, random tokens, string utilities.
#ifndef FB_UTIL_H
#define FB_UTIL_H

#include <string>
#include <vector>

namespace fb {

std::string sha256Hex(const std::string& data);
std::string randomHex(size_t bytes);
// Salted, iterated SHA-256. (Adequate for a coursework prototype; production code
// should use bcrypt/argon2 from a vetted library.)
std::string hashPassword(const std::string& password, const std::string& salt);
bool constantTimeEquals(const std::string& a, const std::string& b);

std::string trim(const std::string& s);
std::string toLower(std::string s);
std::vector<std::string> splitWords(const std::string& s);
std::string truncate(const std::string& s, size_t n);
bool isAllDigits(const std::string& s);
bool isNameWithoutDigits(const std::string& s);  // carried over from the Phase 2 prototype

}  // namespace fb

#endif
