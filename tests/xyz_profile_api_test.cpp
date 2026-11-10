#include "xyz_profile_api.h"
#include "xyz_profiles.h"
#include "xyz_game_api.h"
#include "xyz_races_api.h"
#include "xyz_credentials.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace fs=std::filesystem;
static std::string connected_claim;
static int connections,disconnects;
// No real socket or real name claim is used by this integration test.
extern "C" int xyz_online_connect_claimed(const char *name,const char *room,const char *profile,const char *device,const char *claim){
    if(!name || !room || !profile || !device || !claim)return 0;
    connected_claim=claim;++connections;return 1;
}
extern "C" void xyz_online_disconnect(){++disconnects;}
#ifndef _WIN32
// Standalone ARM executable has no Activity/Keystore. Test API wiring with an
// injected storage fixture here; actual Android protection needs its app test.
namespace xyz { bool credential_crypt(const std::string &id,const std::vector<uint8_t> &in,bool encrypt,std::vector<uint8_t> &out){
    out.clear();if(encrypt){out.assign(id.begin(),id.end());for(auto c:in)out.push_back(c^0xa5);return true;}
    if(in.size()<id.size() || !std::equal(id.begin(),id.end(),in.begin()))return false;
    for(size_t i=id.size();i<in.size();++i)out.push_back(in[i]^0xa5);return true;
} }
#endif
static XYZProfileList list;
static bool idle(){
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    do {xyz_profiles_snapshot(&list);bool busy=false;for(int i=0;i<list.count;++i)busy|=list.auth_busy[i]!=0;
        if(!busy)return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }while(std::chrono::steady_clock::now()<until);return false;
}
static std::string read(const fs::path &p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(){
    const auto root=fs::current_path()/("xyz-profile-api-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(fs::create_directory(root));
    struct Cleanup {fs::path root;std::vector<fs::path> files;~Cleanup(){xyz_profiles_init("");std::error_code ec;for(const auto &p:files)fs::remove(p,ec);fs::remove(root/"xyz-profiles.json",ec);fs::remove(root,ec);}} cleanup{root,{}};
    const char *phrase="acid-acorn-acre-acts-afar-affix-aged-agent";
    const char *token="Ec0iIUolQixwifl_FxyyfA9X-foBclMDwN_l-F2BSCw";
    xyz_profiles_init(root.u8string().c_str());CHECK(idle() && list.count==0);
    CHECK(xyz_profiles_action(XYZ_PROFILE_CREATE,-1,"AUTH_TEST"));CHECK(idle() && list.count==1);
    xyz::Profiles saved(root);CHECK(saved.load());const auto file=root/("xyz-auth-"+saved.all()[0].id+".bin");cleanup.files.push_back(file);
    CHECK(xyz_profiles_action(XYZ_PROFILE_CONNECT,0,"") && connected_claim.empty() && connections==1);
    CHECK(xyz_profiles_action(XYZ_PROFILE_AUTH,0,phrase));CHECK(idle() && list.auth_linked[0]);
    CHECK(xyz_profiles_action(XYZ_PROFILE_CONNECT,0,"") && connected_claim==token && connections==2);
    const auto encrypted=read(file);CHECK(!encrypted.empty() && encrypted.find(token)==std::string::npos && encrypted.find(phrase)==std::string::npos);
    CHECK(read(root/"xyz-profiles.json").find(token)==std::string::npos);
    CHECK(xyz_profiles_action(XYZ_PROFILE_AUTH,0,"wrong phrase"));CHECK(idle() && list.auth_linked[0]);
    CHECK(xyz_profiles_action(XYZ_PROFILE_CONNECT,0,"") && connected_claim==token);
    CHECK(xyz_profiles_action(XYZ_PROFILE_RACES,0,""));XYZRaceRequest request{};CHECK(xyz_races_take(&request));
    CHECK(request.action==XYZ_RACES_OPEN && !std::strcmp(request.claim,token));
    CHECK(xyz_profiles_action(XYZ_PROFILE_DISCONNECT,0,"") && xyz_races_take(&request));
    CHECK(request.action==XYZ_RACES_CLOSE && !request.claim[0]);
    XYZRaceBrowser closed{};closed.epoch=request.epoch;xyz_races_publish(&closed);

    xyz_profiles_init(root.u8string().c_str());CHECK(idle() && list.auth_linked[0]);
    CHECK(xyz_profiles_action(XYZ_PROFILE_CONNECT,0,"") && connected_claim==token);
    CHECK(xyz_profiles_action(XYZ_PROFILE_CREATE,-1,"NO_CLAIM"));CHECK(idle());
    CHECK(xyz_profiles_action(XYZ_PROFILE_CONNECT,1,"") && connected_claim.empty());
    CHECK(xyz_profiles_action(XYZ_PROFILE_AUTH,0,""));CHECK(idle() && !list.auth_linked[0] && !fs::exists(file));
    CHECK(xyz_profiles_action(XYZ_PROFILE_CONNECT,0,"") && connected_claim.empty());
    {std::ofstream corrupt(file,std::ios::binary);corrupt<<"corrupt";}
    xyz_profiles_init(root.u8string().c_str());CHECK(idle());
    CHECK(!xyz_profiles_action(XYZ_PROFILE_CONNECT,0,""));
    CHECK(xyz_profiles_action(XYZ_PROFILE_AUTH,0,phrase));CHECK(idle() && list.auth_linked[0]);
    CHECK(xyz_profiles_action(XYZ_PROFILE_DELETE,0,""));xyz_profiles_init("");CHECK(!fs::exists(file));
    std::puts("Profile API: protected sign-in, restart, both connection paths, per-profile isolation, recovery and deletion passed");
}
