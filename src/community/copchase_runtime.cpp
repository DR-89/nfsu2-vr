#include "copchase_runtime.h"
#include "copchase_profile.h"
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>
namespace {std::string records_path;CopChaseSession *session;}
extern "C" int copchase_runtime_start(){
    copchase_runtime_stop();
    try{
        namespace fs=std::filesystem;const char *setting=std::getenv("NFSU2_GAME_DIR");
        const auto game=fs::canonical(fs::absolute(fs::u8path(setting && *setting?setting:"game")));
        const auto title=fs::canonical(game/"UDATA"/"4541005a");
        if(!fs::is_directory(title))return 0;
        records_path=(game/"CopChaseRecords").u8string();
        if(!copchase_profile_start(title.u8string().c_str())){records_path.clear();return 0;}
        session=copchase_session_start(records_path.c_str());
        if(!session){copchase_runtime_stop();return 0;}
        return 1;
    }catch(...){copchase_runtime_stop();return 0;}
}
extern "C" void copchase_runtime_stop(){
    if(session){const auto result=copchase_session_stop(session);session=nullptr;
        if(result!=COPCHASE_SESSION_OK)std::fprintf(stderr,"[CopChase records] shutdown could not commit all accepted results (%d)\n",int(result));}
    copchase_profile_stop();records_path.clear();
}
extern "C" const char *copchase_runtime_records_path(){return records_path.c_str();}
extern "C" CopChaseSession *copchase_runtime_session(){return session;}
