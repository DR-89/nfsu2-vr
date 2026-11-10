#include "xyz_xbox_tracks.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    uint32_t base=0x10000,total=6;unsigned headers=0;int changed=0,bad_read=0;
    std::vector<uint8_t> bytes=std::vector<uint8_t>(6*0x128);
    void set(unsigned n,uint16_t id,uint32_t modes,int reverse=1,int point=0){
        auto p=bytes.data()+n*0x128;std::memset(p,0,0x128);std::strcpy((char*)p,"fixture");
        std::memcpy(p+0x8a,&id,2);std::memcpy(p+0x94,&modes,4);p[0x86]=(uint8_t)reverse;std::memcpy(p+0x80,&point,4);
    }
    Fixture(){set(0,4014,1);set(1,4101,4);set(2,4402,2);set(3,4174,8,0,1);set(4,4000,1);set(5,4801,1);}
    static int read(void *c,uint32_t at,void *out,size_t n){
        auto &f=*(Fixture*)c;if(f.bad_read && at==f.base+0x128)return 0;
        if(at==0x43d110 && n==8){uint32_t h[2]={f.base,f.total};if(f.changed && f.headers++)h[0]+=0x128;std::memcpy(out,h,8);return 1;}
        if(at<f.base || size_t(at-f.base)+n>f.bytes.size())return 0;
        std::memcpy(out,f.bytes.data()+at-f.base,n);return 1;
    }
    int list(unsigned mode,XyzRaceTrack *out,unsigned cap,unsigned *count){return xyz_xbox_tracks(read,this,mode,out,cap,count);}
};
int main(int argc,char **argv){
    XyzRaceTrack tracks[64];unsigned count=0;XyzRaceRules rules;
    {Fixture f;CHECK(f.list(0,tracks,64,&count) && count==1 && tracks[0].id==4014 && tracks[0].reverse);
        CHECK(xyz_xbox_track_rules(0,tracks,&rules) && rules.min_laps==2 && rules.max_laps==10 && rules.max_players==6);
        CHECK(!xyz_xbox_track_rules(2,tracks,&rules) && !rules.max_players);
        CHECK(f.list(2,tracks,64,&count) && count==1 && xyz_xbox_track_rules(2,tracks,&rules) && rules.max_players==3 && rules.max_laps==1);
        CHECK(f.list(3,tracks,64,&count) && count==1 && !tracks[0].reverse && xyz_xbox_track_rules(3,tracks,&rules) && rules.min_laps==1);
        CHECK(f.list(4,tracks,64,&count) && count==0);
        CHECK(!f.list(8,tracks,64,&count) && count==0 && !tracks[0].id);
    }
    {Fixture f;f.set(1,4015,1);CHECK(!f.list(0,tracks,1,&count) && !count && !tracks[0].id);}
    {Fixture f;f.set(1,4014,4);CHECK(!f.list(0,tracks,64,&count) && !count && !tracks[0].id);}
    {Fixture f;f.set(4,4400,0);f.set(5,4400,0);CHECK(f.list(0,tracks,64,&count) && count==1 && tracks[0].id==4014);}
    {Fixture f;f.set(1,4201,8);CHECK(f.list(3,tracks,64,&count) && count==2);
        CHECK(xyz_xbox_track_rules(3,tracks,&rules) && rules.min_laps==2 && rules.max_laps==10);
        f.set(1,4501,0x800);CHECK(f.list(10,tracks,64,&count) && count==1);
        CHECK(xyz_xbox_track_rules(10,tracks,&rules) && rules.min_laps==4 && rules.max_players==6);
        f.set(1,4401,2);CHECK(f.list(2,tracks,64,&count) && count==2);
        CHECK(xyz_xbox_track_rules(2,tracks,&rules) && rules.max_players==4 && rules.min_laps==1 && rules.max_laps==1);
    }
    {Fixture f;f.bad_read=1;CHECK(!f.list(0,tracks,64,&count) && !count && !tracks[0].id);}
    {Fixture f;f.changed=1;CHECK(!f.list(0,tracks,64,&count) && !count && !tracks[0].id);}
    for(unsigned n:{0u,513u,UINT32_MAX}){Fixture f;f.total=n;CHECK(!f.list(0,tracks,64,&count) && !count);}
    {Fixture f;f.base=0xffffff00;CHECK(!f.list(0,tracks,64,&count));}
    {Fixture f;f.bytes[0x88]=1;CHECK(f.list(0,tracks,64,&count) && !count);}
    {Fixture f;f.bytes[0x86]=2;CHECK(!f.list(0,tracks,64,&count));}
    {Fixture f;std::memset(f.bytes.data(),'A',32);CHECK(!f.list(0,tracks,64,&count));}
    // Optional owned stock fixture; never included in or required by CTest.
    if(argc>1){
        FILE *file=std::fopen(argv[1],"rb");CHECK(file);CHECK(!std::fseek(file,0,SEEK_END));long size=std::ftell(file);CHECK(size==131*0x128);
        Fixture f;f.bytes.resize((size_t)size);std::rewind(file);CHECK(std::fread(f.bytes.data(),1,(size_t)size,file)==(size_t)size);std::fclose(file);f.total=131;
        const unsigned modes[]={0,1,2,3,4,10},counts[]={31,23,10,17,11,8};
        for(unsigned m=0;m<6;++m){CHECK(f.list(modes[m],tracks,64,&count) && count==counts[m]);
            for(unsigned i=0;i<count;++i){CHECK(xyz_xbox_track_rules(modes[m],&tracks[i],&rules));
                CHECK(rules.max_players>=3 && rules.max_players<=6 && rules.min_laps>=1 && rules.max_laps<=10);}
            std::printf("Owned stock track catalog: mode=%u tracks=%u\n",modes[m],count);
        }
    }
    std::puts("Xbox network tracks: bounded catalog, exact mode filters, stale catalog, placeholders and rule bounds passed");return 0;
}
