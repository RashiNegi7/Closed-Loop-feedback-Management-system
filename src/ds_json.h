// Lets the team-written containers be stored in / read from nlohmann::json values.
#ifndef FB_DS_JSON_H
#define FB_DS_JSON_H
#include "ds.h"
#include <nlohmann/json.hpp>

template <typename T>
void to_json(nlohmann::json& j, const ScratchVector<T>& v) {
    j = nlohmann::json::array();
    for (const T& x : v) j.push_back(x);
}
template <typename T>
void to_json(nlohmann::json& j, const ScratchSet<T>& s) {
    j = nlohmann::json::array();
    for (const T& x : s) j.push_back(x);
}
template <typename V>
void to_json(nlohmann::json& j, const ScratchMap<std::string, V>& m) {
    j = nlohmann::json::object();
    for (const auto& e : m) j[e.first] = e.second;
}
template <typename T>
void from_json(const nlohmann::json& j, ScratchVector<T>& v) {
    v.clear();
    for (const auto& x : j) v.push_back(x.get<T>());
}
template <typename V>
void from_json(const nlohmann::json& j, ScratchMap<std::string, V>& m) {
    m.clear();
    for (auto it = j.begin(); it != j.end(); ++it) m[it.key()] = it.value().template get<V>();
}
template <typename T>
void from_json(const nlohmann::json& j, ScratchSet<T>& s) {
    s.clear();
    for (const auto& x : j) s.insert(x.get<T>());
}

#endif
