#pragma once
#include "cJSON.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>

namespace xyz {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
inline bool unique_keys(const cJSON *j) {
    std::unordered_set<std::string> keys;
    for (const cJSON *p = j->child; p; p = p->next) {
        if (cJSON_IsObject(j) && (!p->string || !keys.insert(p->string).second)) return false;
        if ((cJSON_IsObject(p) || cJSON_IsArray(p)) && !unique_keys(p)) return false;
    }
    return true;
}
inline Json parse_json(const std::string &s, size_t limit = 65536) {
    // No embedded NULs, including escaped NULs which cJSON would truncate.
    if (s.empty() || s.size() > limit || s.find('\0') != std::string::npos ||
        s.find("\\u0000") != std::string::npos) return Json(nullptr, cJSON_Delete);
    Json j(cJSON_ParseWithLengthOpts(s.c_str(), s.size() + 1, nullptr, 1), cJSON_Delete);
    if (j && (!cJSON_IsObject(j.get()) || !unique_keys(j.get()))) j.reset();
    return j;
}
inline std::string json_string(const cJSON *j, const char *key, size_t limit) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsString(v) || !v->valuestring || std::strlen(v->valuestring) > limit) return {};
    return v->valuestring;
}
inline bool json_u32(const cJSON *j, const char *key, uint32_t &v) {
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsNumber(p) || !std::isfinite(p->valuedouble) || p->valuedouble < 0 ||
        p->valuedouble > UINT32_MAX || std::floor(p->valuedouble) != p->valuedouble) return false;
    v = static_cast<uint32_t>(p->valuedouble); return true;
}
inline std::string json_text(const cJSON *j) {
    char *p = cJSON_PrintUnformatted(j);
    if (!p) return {};
    std::string s(p); cJSON_free(p); return s;
}
}
