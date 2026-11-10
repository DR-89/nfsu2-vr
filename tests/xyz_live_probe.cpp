// Explicit, opt-in service handshake probe. Never registered with CTest.
#include "xyz_transport.h"
#include "xyz_profiles.h"
#include "xyz_names.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
int main(int argc,char **argv) {
    if(argc==3 && std::string(argv[1])=="--check-name") {
        if(!xyz::valid_online_name(argv[2]))return 2;
        auto socket=xyz::make_socket();if(!socket)return 1;
        const auto reply=socket->name_info(argv[2],{});
        const auto decision=xyz::check_name_reply(reply,argv[2],false);
        std::printf("Read-only name check: %s%s%s\n",decision.allowed?"allowed":"denied",
            decision.notice.empty()?"":" - ",decision.notice.c_str());
        return reply.empty()?1:decision.allowed?0:3;
    }
    if(argc!=2 || std::string(argv[1])!="--connect") {
        std::puts("Use --connect for a TLS handshake, or --check-name NAME for a read-only name check (no cars or name claim).");return 2;
    }
    // load() on a missing path generates a cryptographic device ID in memory;
    // no profile is saved, and neither the ID nor the resume token is logged.
    auto folder=std::filesystem::temp_directory_path()/"nfsu2-xyz-handshake-no-persistence";
    if(std::filesystem::exists(folder))return 2;
    xyz::Profiles profiles(folder);if(!profiles.load())return 2;
    std::string name="VR_Check_"+profiles.identity().substr(0,6);
    xyz::Transport t([]{return xyz::make_socket();});
    if(!t.start({name,profiles.identity(),{}}))return 2;
    const char *labels[]={"offline","connecting","greeting","queued","connected","retry","stopped"};
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);int previous=-1,result=1;
    while(std::chrono::steady_clock::now()<end) {
        auto s=t.snapshot();int state=int(s.state);
        if(state!=previous){std::printf("Service handshake: %s%s%s\n",labels[state],s.notice.empty()?"":" - ",s.notice.c_str());std::fflush(stdout);previous=state;}
        if(s.state==xyz::LinkState::connected){result=0;break;}
        if(s.state==xyz::LinkState::queued || s.state==xyz::LinkState::stopped)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    t.stop();return result;
}
