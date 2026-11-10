#include "../src/vr_subtitles.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#define CHECK(v) do {if(!(v)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);return 1;}}while(0)
static void word(std::vector<uint8_t>&b,unsigned at,uint32_t n){for(unsigned i=0;i<4;i++)b[at+i]=(uint8_t)(n>>(i*8));}
int main(int argc,char **argv){
    namespace fs=std::filesystem;auto folder=fs::temp_directory_path()/"nfsu2-subtitle-test";
    fs::create_directories(folder/"TEXPACKS");auto path=folder/"TEXPACKS/english-subtitles.bin";
    std::vector<uint8_t>b(40);memcpy(b.data(),"NVS1",4);word(b,4,2);word(b,8,40);
    word(b,12,1);word(b,16,28);word(b,20,2);word(b,24,34);memcpy(b.data()+28,"First\0Next\0",11);
    auto save=[&]{std::ofstream f(path,std::ios::binary|std::ios::trunc);f.write((char*)b.data(),b.size());};
    size_t size=0;unsigned count=0;save();
    CHECK(!vr_subtitles_start(folder.u8string().c_str(),0));CHECK(!vr_subtitles_pack(&size,&count)&&!size&&!count);
    CHECK(vr_subtitles_start(folder.u8string().c_str(),1));
    auto data=vr_subtitles_pack(&size,&count);CHECK(size==40&&count==2&&!memcmp(data,b.data(),40));
    word(b,20,1);save();CHECK(!vr_subtitles_start(folder.u8string().c_str(),1));
    word(b,20,2);word(b,24,40);save();CHECK(!vr_subtitles_start(folder.u8string().c_str(),1));
    word(b,24,34);memset(b.data()+34,'a',6);save();CHECK(!vr_subtitles_start(folder.u8string().c_str(),1));
    b[39]=0;b[35]=1;save();CHECK(!vr_subtitles_start(folder.u8string().c_str(),1));
    word(b,4,0xffffffff);save();CHECK(!vr_subtitles_start(folder.u8string().c_str(),1));
    CHECK(!vr_subtitles_pack(&size,&count)&&!size&&!count);
    fs::remove(path);fs::remove(folder/"TEXPACKS");fs::remove(folder);
    if(argc==2){CHECK(vr_subtitles_start(argv[1],1));CHECK(vr_subtitles_pack(&size,&count)&&count==107&&size==10867);}
    puts("Subtitle pack: opt-in, text lifetime, bounds, ordering, termination and control characters passed.");
}
