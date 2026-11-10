#include "vr_subtitles.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
namespace {
std::vector<uint8_t> data;unsigned entries;
uint32_t word(size_t at){return uint32_t(data[at])|uint32_t(data[at+1])<<8|uint32_t(data[at+2])<<16|uint32_t(data[at+3])<<24;}
}
extern "C" int vr_subtitles_start(const char *parent,int enabled){
    data.clear();entries=0;if(!enabled||!parent||!*parent)return 0;
    try {
        std::ifstream f(std::filesystem::u8path(parent)/"TEXPACKS/english-subtitles.bin",std::ios::binary|std::ios::ate);
        if(!f)return 0;auto length=f.tellg();if(length<12||length>128*1024)return 0;
        data.resize((size_t)length);f.seekg(0);if(!f.read((char*)data.data(),length))throw 0;
        if(memcmp(data.data(),"NVS1",4)||word(8)!=data.size())throw 0;
        unsigned count=word(4);if(!count||count>256||12+8*count>data.size())throw 0;
        uint32_t previous=0;
        for(unsigned i=0;i<count;i++){
            uint32_t hash=word(12+8*i),offset=word(16+8*i);
            if((i&&hash<=previous)||offset<12+8*count||offset>=data.size())throw 0;
            const auto end=(const uint8_t*)memchr(data.data()+offset,0,data.size()-offset);
            if(!end||end-data.data()-offset>1023)throw 0;
            for(auto p=data.data()+offset;p<end;p++)if((*p<32&&*p!='\n')||*p==127)throw 0;
            previous=hash;
        }
        entries=count;fprintf(stderr,"[VR subtitles] %u validated English lines\n",entries);return 1;
    }catch(...){data.clear();return 0;}
}
extern "C" const uint8_t *vr_subtitles_pack(size_t *size,unsigned *count){
    if(size)*size=entries?data.size():0;if(count)*count=entries;return entries?data.data():nullptr;
}
