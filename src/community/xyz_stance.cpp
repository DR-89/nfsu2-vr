#include "xyz_stance.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace {
std::mutex mutex;
std::filesystem::path folder;
std::map<uint32_t,XyzStance> cars;
XyzStanceView view{};
XyzStance original{0,0,0,0,100};
std::atomic<int> enabled{0};
bool initialized=false;
bool save(const std::map<uint32_t,XyzStance> &next,int on){
    if(!initialized || folder.empty())return false;
    std::string text="NVS1 "+std::to_string(on)+"\n";
    for(const auto &e:next){char row[128];auto v=e.second;
        std::snprintf(row,sizeof row,"%08x %d %d %d %d %d\n",e.first,v.fc,v.rc,v.fo,v.ro,v.tw);text+=row;}
    std::error_code ec;std::filesystem::create_directories(folder,ec);if(ec)return false;
    auto tmp=folder/"xyz-stance.tmp",dest=folder/"xyz-stance.txt";bool ok=false;
#ifdef _WIN32
    HANDLE file=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){DWORD written=0;
        ok=WriteFile(file,text.data(),DWORD(text.size()),&written,nullptr) && written==text.size() && FlushFileBuffers(file);
        CloseHandle(file);if(ok)ok=MoveFileExW(tmp.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;}
#else
    int file=open(tmp.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
    if(file>=0){size_t done=0;while(done<text.size()){auto n=write(file,text.data()+done,text.size()-done);if(n<0 && errno==EINTR)continue;if(n<=0)break;done+=size_t(n);}
        ok=done==text.size() && fsync(file)==0;if(close(file))ok=false;if(ok)ok=rename(tmp.c_str(),dest.c_str())==0;
        if(ok){int dir=open(folder.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dir>=0){fsync(dir);close(dir);}}}
#endif
    if(!ok)std::filesystem::remove(tmp,ec);return ok;
}
void update(){
    auto found=cars.find(view.type);view.custom=view.available && found!=cars.end();
    view.value=view.custom?found->second:original;view.enabled=enabled.load(std::memory_order_relaxed);
}
}
extern "C" int xyz_stance_valid(const XyzStance *v){
    return v && v->fc>=0 && v->fc<=150 && v->rc>=0 && v->rc<=150 && v->fo>=-40 && v->fo<=80 && v->ro>=-40 && v->ro<=80 && v->tw>=80 && v->tw<=130;
}
extern "C" void xyz_stance_clamp(XyzStance *v){
    if(!v)return;v->fc=std::clamp<int>(v->fc,0,150);v->rc=std::clamp<int>(v->rc,0,150);
    v->fo=std::clamp<int>(v->fo,-40,80);v->ro=std::clamp<int>(v->ro,-40,80);v->tw=std::clamp<int>(v->tw,80,130);
}
extern "C" void xyz_stance_init(const char *directory){
    std::lock_guard<std::mutex> lock(mutex);cars.clear();view={};original={0,0,0,0,100};enabled=0;
    initialized=directory && *directory;folder=initialized?std::filesystem::u8path(directory):std::filesystem::path{};
    if(!initialized)return;
    try{
        auto path=folder/"xyz-stance.txt";std::error_code ec;
        if(!std::filesystem::exists(path,ec))return;
        auto bytes=std::filesystem::file_size(path,ec);if(ec || bytes>8192)return;
        std::ifstream file(path);std::string line;int on=-1,used=0;
        if(!std::getline(file,line) || std::sscanf(line.c_str(),"NVS1 %d %n",&on,&used)!=1 || used!=int(line.size()) || (on!=0 && on!=1))return;
        std::map<uint32_t,XyzStance> loaded;
        while(std::getline(file,line)){
            unsigned hash;int a,b,c,d,e;used=0;
            if(loaded.size()>=46 || std::sscanf(line.c_str(),"%8x %d %d %d %d %d %n",&hash,&a,&b,&c,&d,&e,&used)!=6 || used!=int(line.size()) || !hash || loaded.count(hash) ||
               a<0 || a>150 || b<0 || b>150 || c<-40 || c>80 || d<-40 || d>80 || e<80 || e>130)return;
            loaded[hash]={(int16_t)a,(int16_t)b,(int16_t)c,(int16_t)d,(int16_t)e};
        }
        if(file.bad())return;cars=std::move(loaded);enabled=on;view.enabled=on;
    }catch(...) {cars.clear();enabled=0;}
}
extern "C" int xyz_stance_enabled(void){return enabled.load(std::memory_order_relaxed);}
extern "C" void xyz_stance_snapshot(XyzStanceView *out){if(out){std::lock_guard<std::mutex> lock(mutex);*out=view;}}
extern "C" int xyz_stance_publish(uint32_t type,const char *name,int stock,XyzStance *out){
    std::lock_guard<std::mutex> lock(mutex);XyzStanceView before=view;
    view.type=type;view.available=type!=0;std::snprintf(view.name,sizeof view.name,"%s",type && name?name:"");
    original={(int16_t)std::clamp(stock,0,70),0,0,0,100};update();
    if(std::memcmp(&before,&view,sizeof view))++view.revision;
    if(out)*out=view.value;return view.enabled && view.custom;
}
extern "C" int xyz_stance_edit(uint32_t expected,int row,int step){
    std::lock_guard<std::mutex> lock(mutex);
    if(row<0 || row>7 || !step || (row && (!view.available || view.type!=expected)))return 0;
    try{
        auto next=cars;int on=enabled.load();XyzStance value=view.value;
        if(!row)on=!on;
        else if(row==7 || (row==1 && view.custom))next.erase(expected);
        else {
            int16_t *fields[]={&value.fc,&value.rc,&value.fo,&value.ro,&value.tw};
            if(row>=2){int delta=(row<=3?5:1)*(step>0?1:-1);*fields[row-2]+=delta;xyz_stance_clamp(&value);}
            if(next.size()>=46 && !next.count(expected))return 0;
            next[expected]=value;
        }
        if(!save(next,on)){view.save_failed=1;++view.revision;return 0;}
        cars=std::move(next);enabled=on;view.save_failed=0;update();++view.revision;return 1;
    }catch(...){view.save_failed=1;++view.revision;return 0;}
}
