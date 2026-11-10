#include "../src/vr_soundtrack.h"
#include "../src/community/vendor/auth/sha256.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <filesystem>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static std::string hex(const uint8_t *p){char s[65];for(int i=0;i<32;i++)std::snprintf(s+i*2,3,"%02x",p[i]);return s;}
int main(int argc,char **argv){
    Nfsu2Sha256 sha;uint8_t digest[32],reference[32];
    nfsu2_sha256_init(&sha);nfsu2_sha256_update(&sha,nullptr,0);nfsu2_sha256_final(&sha,digest);
    CHECK(hex(digest)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    nfsu2_sha256_init(&sha);for(const char *p="abc";*p;p++)nfsu2_sha256_update(&sha,p,1);nfsu2_sha256_final(&sha,digest);
    CHECK(hex(digest)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::array<char,1000> a;a.fill('a');nfsu2_sha256_init(&sha);
    for(int i=0;i<1000;i++)nfsu2_sha256_update(&sha,a.data(),a.size());nfsu2_sha256_final(&sha,digest);
    CHECK(hex(digest)=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    for(size_t count:{1,55,56,63,64,65,127,128,129,4097}){
        std::vector<uint8_t> data(count);for(size_t i=0;i<count;i++)data[i]=(uint8_t)(i*73);
        nfsu2_sha256(data.data(),data.size(),reference);
        for(size_t stride:{1,3,17,63,64,65,1000}){
            nfsu2_sha256_init(&sha);for(size_t i=0;i<count;i+=stride)nfsu2_sha256_update(&sha,data.data()+i,std::min(stride,count-i));
            nfsu2_sha256_final(&sha,digest);CHECK(!memcmp(digest,reference,32));
        }
    }
    CHECK(!vr_soundtrack_start(nullptr,1));CHECK(!vr_soundtrack_redirect("D:\\NFSUNDER\\ZDIR.BIN"));
    if(argc==2){
        CHECK(vr_soundtrack_start(argv[1],1));
        for(const char *name:{"ZDIR.BIN","ZZDATA10.BIN","ZZDATA11.BIN","ZZDATA12.BIN"}){
            auto path=std::string("\\Device\\CdRom0\\NFSUNDER\\")+name;
            const char *mapped=vr_soundtrack_redirect(path.c_str());CHECK(mapped && std::filesystem::is_regular_file(std::filesystem::u8path(mapped)));
        }
        CHECK(vr_soundtrack_redirect("d:/nfsunder/zdir.bin"));
        CHECK(!vr_soundtrack_redirect("D:\\NFSUNDER\\ZZDATA8.BIN"));
        CHECK(!vr_soundtrack_redirect("D:\\NFSUNDER\\ZDIR.BIN.extra"));
        CHECK(!vr_soundtrack_redirect("D:\\NFSUNDER\\..\\ZDIR.BIN"));
        CHECK(!vr_soundtrack_redirect("\\Device\\Harddisk0\\partition1\\NFSUNDER\\ZDIR.BIN"));
        CHECK(!vr_soundtrack_redirect(nullptr));
        CHECK(!vr_soundtrack_start(argv[1],0));CHECK(!vr_soundtrack_redirect("D:\\NFSUNDER\\ZDIR.BIN"));
    }
    std::puts("Restored soundtrack: streaming SHA-256, optional pack verification and exact disc routing passed.");return 0;
}
