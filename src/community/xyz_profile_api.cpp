#include "xyz_profile_api.h"
#include "xyz_profiles.h"
#include "xyz_game_api.h"
#include "xyz_races_api.h"
#include "xyz_auth_store.h"
#include "xyz_auth.h"
#include <cstdio>
#include <memory>
#include <cstring>
namespace {
std::unique_ptr<xyz::Profiles> profiles;
std::unique_ptr<xyz::AuthStore> authentication;
const char *error = "Online profiles are unavailable";
}
extern "C" void xyz_profiles_init(const char *path) {
    try {
        authentication.reset();profiles.reset(); error = "Online profile folder is unavailable";
        if (!path || !*path) return;
        profiles = std::make_unique<xyz::Profiles>(std::filesystem::u8path(path));
        if(!profiles->load()){error=nullptr;return;}
        std::vector<std::string> ids;for(const auto &p:profiles->all())ids.push_back(p.id);
        authentication=std::make_unique<xyz::AuthStore>(std::filesystem::u8path(path),ids);error = nullptr;
    } catch (...) { error = "Cannot load online profiles"; }
}
extern "C" void xyz_profiles_snapshot(XYZProfileList *out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out)); out->selected = -1;
    try {
        if (profiles) {
            const auto selected = profiles->selected();
            for (const auto &p : profiles->all()) {
                int i = out->count++;
                std::snprintf(out->name[i], sizeof(out->name[i]), "%s", p.name.c_str());
                std::snprintf(out->room[i], sizeof(out->room[i]), "%s", p.room.c_str());
                if(authentication){
                    auto auth=authentication->status(p.id);out->auth_busy[i]=auth.busy;out->auth_linked[i]=auth.linked;
                    if(&p==selected)std::snprintf(out->auth_notice,sizeof out->auth_notice,"%s",auth.notice.c_str());
                }
                if (&p == selected) out->selected = i;
            }
        }
        std::snprintf(out->error, sizeof(out->error), "%s", error ? error : profiles ? profiles->error().c_str() : "Online profiles are unavailable");
    } catch (...) { std::snprintf(out->error, sizeof(out->error), "%s", "Cannot read online profiles"); }
}
extern "C" int xyz_profiles_action(int action, int index, const char *text) {
    try {
        if (!profiles) return 0;
        error = nullptr;
        if(action==XYZ_PROFILE_DISCONNECT){if(xyz_races_active())return xyz_races_close();xyz_online_disconnect();return 1;}
        if (action == XYZ_PROFILE_CREATE) return profiles->create(text ? text : "");
        if (index < 0 || size_t(index) >= profiles->all().size()) { error = "Select an online profile first"; return 0; }
        // Copy values before a successful mutation replaces the backing vector.
        auto p = profiles->all()[size_t(index)];
        if(action==XYZ_PROFILE_AUTH){
            if(authentication && authentication->link(p.id,text?text:""))return 1;
            error="Sign-in is unavailable or still processing. Try again shortly.";return 0;
        }
        std::string claim;
        struct Clear {std::string &s;~Clear(){xyz_auth_clear(s.data(),s.size());}} clear{claim};
        if(action==XYZ_PROFILE_CONNECT || action==XYZ_PROFILE_RACES){
            if(!authentication || !authentication->token(p.id,claim)){
                error="Sign-in is not ready. Check LINK KEYPHRASE before connecting.";return 0;
            }
        }
        if(action==XYZ_PROFILE_RACES){
            if(xyz_races_active() || xyz_races_open_claimed(p.name.c_str(),p.room.c_str(),p.id.c_str(),profiles->identity().c_str(),claim.c_str()))return 1;
            error="Cannot open this race browser";return 0;
        }
        if(action==XYZ_PROFILE_CONNECT) {
            if(xyz_races_active()){error="Disconnect the race browser before connecting free roam";return 0;}
            if(xyz_online_connect_claimed(p.name.c_str(),p.room.c_str(),p.id.c_str(),profiles->identity().c_str(),claim.c_str()))return 1;
            error="Cannot connect this online profile";return 0;
        }
        if (action == XYZ_PROFILE_SELECT) return profiles->select(p.id);
        if (action == XYZ_PROFILE_DELETE){
            if(!profiles->remove(p.id))return 0;
            if(authentication)authentication->forget(p.id);return 1;
        }
        if (action == XYZ_PROFILE_RENAME) return profiles->edit(p.id, text ? text : "", p.room);
        if (action == XYZ_PROFILE_ROOM) return profiles->edit(p.id, p.name, text ? text : "");
        return 0;
    } catch (...) { error = "Cannot save online profiles"; return 0; }
}
