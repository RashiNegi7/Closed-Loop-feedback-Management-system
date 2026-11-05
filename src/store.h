// File-based persistence: the whole State is saved as one JSON document.
#ifndef FB_STORE_H
#define FB_STORE_H

#include "types.h"
#include <nlohmann/json.hpp>

namespace fb {

nlohmann::json stateToJson(const State& st);
State stateFromJson(const nlohmann::json& j);

// Returns false when the file does not exist. Throws std::runtime_error when it exists but is corrupt.
bool loadState(const std::string& path, State& out);
// Writes to "<path>.tmp" then renames, so a crash never leaves a half-written file.
void saveState(const std::string& path, const State& st);

}  // namespace fb

#endif
