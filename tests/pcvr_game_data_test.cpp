#include "game_data.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace pcvr_data;
static unsigned checks;
static void check(bool ok,const char *label) { ++checks; if(!ok) throw std::runtime_error(label); }
static Manifest synthetic={
    {"default.xbe",13,"054f68fb24d3a9780774731c5ee65e1f9c63a973d502ae351fa53ecd4d091b82"},
    {"NFSUNDER/ZZDATA0.BIN",14,"5c0b6f26bc4b6472d810c4536bd29644c3be1eaaaf9b88dfe5c5d8d6209f6033"}
};
static void put32(std::vector<unsigned char> &b,size_t p,uint32_t n) { for(unsigned i=0;i<4;++i) b[p+i]=(unsigned char)(n>>(i*8)); }
static size_t record(std::vector<unsigned char> &b,size_t p,const char *name,uint32_t sector,uint32_t size,bool directory=false) {
    size_t n=strlen(name); put32(b,p+4,sector); put32(b,p+8,size); b[p+12]=directory?16:0; b[p+13]=(unsigned char)n;
    memcpy(b.data()+p+14,name,n); return p+((14+n+3)&~size_t(3));
}
static void write(const fs::path &p,const std::string &data) { fs::create_directories(p.parent_path()); std::ofstream f(p,std::ios::binary); f<<data; }
static std::string read(const fs::path &p) { std::ifstream f(p,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
static void image(const fs::path &path,uint64_t base=0,int fault=0) {
    std::vector<unsigned char> b(42*2048);
    memcpy(b.data()+32*2048,"MICROSOFT*XBOX*MEDIA",20);
    put32(b,32*2048+20,35); put32(b,32*2048+24,2048);
    size_t end=record(b,35*2048,"DEFAULT.XBE",38,13);
    record(b,end,"NFSUNDER",fault==5?35:36,2048,true);
    end=record(b,36*2048,fault==4?"../bad":"ZZDATA0.BIN",fault==3?1000:39,14);
    if(fault==2) record(b,end,"zzdata0.bin",39,14);
    memcpy(b.data()+38*2048,"synthetic-xbe",13);
    memcpy(b.data()+39*2048,fault==1?"corrupted-data":"synthetic-data",14);
    if(fault==6) b[32*2048]=0;
    std::ofstream f(path,std::ios::binary); f.seekp(base); f.write((char *)b.data(),b.size());
}
template<class Function> static void rejected(Function fn,const char *label) {
    bool failed=false; try { fn(); } catch(const std::exception &) { failed=true; } check(failed,label);
}
static void run(const fs::path &root) {
    const auto iso=root/L"disc.iso", game=root/L"game";
    auto quiet=[](uint64_t,uint64_t,const std::wstring &){};
    auto never=[] { return false; };
    check(!installed(game,synthetic),"empty game must need setup");
    image(iso); write(game/L"UDATA"/L"save.dat","keep-save"); write(game/L"TDATA"/L"config","keep-config");
    write(game/L"notes.txt","keep-notes");
    {
        Lock lock(game);
        rejected([&]{ Lock second(game); },"second process must not acquire the game lock");
        uint64_t last=0;
        import_iso(iso,game,synthetic,[&](uint64_t done,uint64_t total,const std::wstring &) {
            check(done>=last && done<=total && total==27,"monotonic progress"); last=done;
        },never);
        check(last==27 && installed(game,synthetic),"successful import must be detected");
    }
    { Lock again(game); }
    check(read(game/L"UDATA"/L"save.dat")=="keep-save" && read(game/L"TDATA"/L"config")=="keep-config" && read(game/L"notes.txt")=="keep-notes","saved games and unrelated files must survive");
    for(uint64_t base:{0ull,0xFD90ull,0x30600ull,0xFD90000ull,0x18300000ull}) {
        image(iso,base); import_iso(iso,game,synthetic,quiet,never); check(installed(game,synthetic),"supported partition layout");
    }
    for(int fault=1;fault<=6;++fault) {
        image(iso,0,fault);
        rejected([&]{ import_iso(iso,game,synthetic,quiet,never); },"malformed/incompatible image must be rejected");
        check(installed(game,synthetic) && read(game/L"UDATA"/L"save.dat")=="keep-save","failed import must preserve installed data and saves");
    }
    image(iso); bool cancel=false;
    rejected([&]{ import_iso(iso,game,synthetic,[&](uint64_t,uint64_t,const std::wstring &){cancel=true;},[&]{return cancel;}); },"cancel during extraction");
    check(installed(game,synthetic),"cancellation before promotion must preserve the installation");
    for(const auto &entry:fs::directory_iterator(root)) check(entry.path().filename().wstring().find(L".nfsu2-import-")!=0,"temporary extraction must be cleaned");
    fs::remove(game/L"NFSUNDER"/L"ZZDATA0.BIN"); check(!installed(game,synthetic),"missing asset must need setup");
    import_iso(iso,game,synthetic,quiet,never);
    write(game/L"default.xbe","incorrect-xbe"); check(!installed(game,synthetic),"wrong executable hash must need setup");
    import_iso(iso,game,synthetic,quiet,never);
    // Force a promotion failure by holding the destination executable open.
    HANDLE held=CreateFileW((game/L"default.xbe").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    check(held!=INVALID_HANDLE_VALUE,"test must hold executable");
    rejected([&]{ import_iso(iso,game,synthetic,quiet,never); },"promotion failure must be reported"); CloseHandle(held);
    check(fs::exists(game/L".nfsu2-pcvr-import-pending") && !installed(game,synthetic),"interrupted promotion must block boot");
    import_iso(iso,game,synthetic,quiet,never); check(installed(game,synthetic),"retry must complete interrupted promotion");
    check(read(game/L"UDATA"/L"save.dat")=="keep-save","repair must preserve saves");
    const auto unicode=root/L"Spieldaten-\u00e4-\u6e2c\u8a66";
    import_iso(iso,unicode,synthetic,quiet,never); check(installed(unicode,synthetic),"Unicode destination");
    check(wide(utf8(unicode.wstring()))==unicode.wstring(),"UTF-8 path roundtrip");
    check(retail_manifest().size()==12,"shared retail manifest must contain 12 files");
    check(!installed(game,retail_manifest()),"synthetic inputs must never pass retail validation");
}
int wmain(int argc,wchar_t **argv) {
    try {
        if(argc==3 && !wcscmp(argv[1],L"--ui")) {
            _wputenv_s(L"NFSU2_GAME_DIR",argv[2]);
            int result=pcvr_prepare_game_data(); pcvr_release_game_data(); return result<0?1:0;
        }
        if(argc==4 && !wcscmp(argv[1],L"--retail")) {
            auto game=fs::absolute(argv[3]); Lock lock(game);
            import_iso(argv[2],game,retail_manifest(),[](uint64_t done,uint64_t total,const std::wstring &text) {
                static unsigned prior=999; unsigned percent=unsigned(done*100/total);
                if(percent!=prior) { std::wcout<<percent<<L"% "<<text<<L"\n"; prior=percent; }
            },[]{return false;});
            check(installed(game,retail_manifest()),"retail import startup check"); return 0;
        }
        auto parent=fs::absolute(fs::current_path()).lexically_normal();
        auto root=parent/(L"pcvr-import-tests-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        check(fs::create_directory(root),"create unique test directory");
        run(root);
        check(root.parent_path()==parent && root.filename().wstring().find(L"pcvr-import-tests-")==0,"cleanup stays in the test directory");
        fs::remove_all(root);
        std::cout<<"PASS: "<<checks<<" PCVR import assertions\n"; return 0;
    } catch(const std::exception &error) { std::cerr<<"FAIL: "<<error.what()<<"\n"; return 1; }
}
