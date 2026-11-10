#include "xyz_profiles.h"
#include "xyz_json.h"
#include "names.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <fstream>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace xyz {
namespace fs = std::filesystem;
static std::string random_id() {
    std::array<unsigned char,16> b{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, b.data(), ULONG(b.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return {};
#else
    size_t have = 0;
    while (have < b.size()) {
        auto n = getrandom(b.data() + have, b.size() - have, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return {};
        have += size_t(n);
    }
#endif
    const char *hex = "0123456789abcdef"; std::string s;
    for (auto c : b) { s += hex[c >> 4]; s += hex[c & 15]; } return s;
}
static bool valid_id(const std::string &s) { return s.size() == 32 && s.find_first_not_of("0123456789abcdef") == std::string::npos; }
static bool utf8_room(const std::string &s) {
    if (s.size() > 63) return false;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i++]; unsigned cp = c, need = 0, min = 0;
        if (c < 32 || c == 127) return false;
        if (c < 128) continue;
        if ((c & 0xe0) == 0xc0) { cp = c & 31; need = 1; min = 0x80; }
        else if ((c & 0xf0) == 0xe0) { cp = c & 15; need = 2; min = 0x800; }
        else if ((c & 0xf8) == 0xf0) { cp = c & 7; need = 3; min = 0x10000; }
        else return false;
        while (need--) {
            if (i == s.size() || (static_cast<unsigned char>(s[i]) & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 63);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}
std::string online_name(const std::string &typed) {
    if (typed.size() > 256 || typed.find('\0') != std::string::npos) return {};
    std::string out; bool gap = false;
    for (unsigned char c : typed) {
        if (out.size() >= 15) break;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) { gap = true; continue; }
        if (gap && !out.empty() && out.size() < 14) out += '_';
        gap = false; out += char(c);
    }
    if (out.empty()) return {};
    while (out.size() < 3) out += '_';
    if (name_blocked(typed.c_str()) || name_blocked(out.c_str())) return {};
    return out;
}
const Profile *Profiles::selected() const {
    for (const auto &p : profiles_) if (p.id == selected_) return &p;
    return nullptr;
}
bool Profiles::load() {
    loaded_ = false; profiles_.clear(); identity_.clear(); selected_.clear(); error_.clear();
    std::error_code ec; const auto path = directory_ / "xyz-profiles.json";
    const bool exists = fs::exists(path, ec);
    if (ec) { error_ = "Cannot inspect online profiles"; return false; }
    if (!exists) {
        identity_ = random_id();
        loaded_ = !identity_.empty();
        if (!loaded_) error_ = "Cannot generate profile identity";
        return loaded_;
    }
    auto size = fs::file_size(path, ec);
    if (ec || !size || size > 16384) { error_ = "Invalid online profile file"; return false; }
    std::ifstream in(path, std::ios::binary); std::string text(size, '\0');
    if (!in.read(text.data(), std::streamsize(size))) { error_ = "Cannot read online profiles"; return false; }
    auto j = parse_json(text, 16384); uint32_t version = 0;
    if (!j || !json_u32(j.get(), "version", version) || version != 1) { error_ = "Unsupported online profile file"; return false; }
    auto id = json_string(j.get(), "identity", 32), selection = json_string(j.get(), "selected", 32);
    auto entries = cJSON_GetObjectItemCaseSensitive(j.get(), "profiles");
    if (!valid_id(id) || !cJSON_IsArray(entries) || cJSON_GetArraySize(entries) > 8) { error_ = "Invalid online profiles"; return false; }
    std::vector<Profile> parsed; std::unordered_set<std::string> ids;
    for (auto e = entries->child; e; e = e->next) {
        Profile p{json_string(e, "id", 32), json_string(e, "name", 15), json_string(e, "room", 63)};
        auto room = cJSON_GetObjectItemCaseSensitive(e, "room");
        if (!valid_id(p.id) || !ids.insert(p.id).second || p.name.empty() || online_name(p.name) != p.name ||
            !cJSON_IsString(room) || std::strlen(room->valuestring) > 63 || !utf8_room(p.room)) {
            error_ = "Invalid online profile entry"; return false;
        }
        parsed.push_back(std::move(p));
    }
    if ((parsed.empty() && !selection.empty()) || (!parsed.empty() && !ids.count(selection))) { error_ = "Invalid selected online profile"; return false; }
    profiles_ = std::move(parsed); identity_ = id; selected_ = selection; loaded_ = true; return true;
}
// Exclusive temp file + flushed atomic replacement. On failure the previous file
// and in-memory selection survive. Names never become filesystem paths. No save
// files or credentials are included in this separate local connection catalogue.
bool Profiles::persist(const std::vector<Profile> &profiles, const std::string &selection) {
    if (!loaded_) { error_ = "Load online profiles before changing them"; return false; }
    Json j(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddNumberToObject(j.get(), "version", 1);
    cJSON_AddStringToObject(j.get(), "identity", identity_.c_str());
    cJSON_AddStringToObject(j.get(), "selected", selection.c_str());
    auto a = cJSON_AddArrayToObject(j.get(), "profiles");
    for (auto &p : profiles) {
        auto e = cJSON_CreateObject(); cJSON_AddItemToArray(a, e);
        cJSON_AddStringToObject(e, "id", p.id.c_str()); cJSON_AddStringToObject(e, "name", p.name.c_str()); cJSON_AddStringToObject(e, "room", p.room.c_str());
    }
    auto text = json_text(j.get()); if (text.empty()) { error_ = "Cannot encode online profiles"; return false; }
    std::error_code ec; fs::create_directories(directory_, ec);
    if (ec) { error_ = "Cannot create online profile folder"; return false; }
    auto suffix = random_id(); if (suffix.empty()) { error_ = "Cannot create profile temporary file"; return false; }
    auto tmp = directory_ / ("xyz-profiles." + suffix + ".tmp"), dest = directory_ / "xyz-profiles.json";
    bool ok = false;
#ifdef _WIN32
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        ok = WriteFile(f, text.data(), DWORD(text.size()), &written, nullptr) && written == text.size() && FlushFileBuffers(f);
        CloseHandle(f);
        if (ok) ok = MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }
#else
    int f = open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (f >= 0) {
        size_t done = 0;
        while (done < text.size()) {
            auto n = write(f, text.data() + done, text.size() - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            done += size_t(n);
        }
        ok = done == text.size() && fsync(f) == 0;
        if (close(f)) ok = false;
        if (ok) ok = rename(tmp.c_str(), dest.c_str()) == 0;
        if (ok) { int dir = open(directory_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); if (dir >= 0) { fsync(dir); close(dir); } }
    }
#endif
    if (!ok) { fs::remove(tmp, ec); error_ = "Cannot save online profiles"; return false; }
    profiles_ = profiles; selected_ = selection; error_.clear(); return true;
}
bool Profiles::create(const std::string &typed) {
    auto name = online_name(typed);
    if (name.empty() || profiles_.size() >= 8) { error_ = name.empty() ? "Choose a valid online name" : "At most eight online profiles"; return false; }
    auto id = random_id(); if (id.empty()) { error_ = "Cannot generate profile ID"; return false; }
    auto changed = profiles_; changed.push_back({id, name, {}}); return persist(changed, id);
}
bool Profiles::edit(const std::string &id, const std::string &typed, const std::string &room) {
    auto name = online_name(typed);
    if (name.empty() || !utf8_room(room)) { error_ = "Invalid online name or room"; return false; }
    auto changed = profiles_;
    for (auto &p : changed) if (p.id == id) { p.name = name; p.room = room; return persist(changed, selected_); }
    error_ = "Online profile not found"; return false;
}
bool Profiles::select(const std::string &id) {
    for (auto &p : profiles_) if (p.id == id) return persist(profiles_, id);
    error_ = "Online profile not found"; return false;
}
bool Profiles::remove(const std::string &id) {
    auto changed = profiles_;
    auto found = std::find_if(changed.begin(), changed.end(), [&](const Profile &p) { return p.id == id; });
    if (found == changed.end()) { error_ = "Online profile not found"; return false; }
    changed.erase(found);
    return persist(changed, selected_ == id ? (changed.empty() ? "" : changed.front().id) : selected_);
}
}
