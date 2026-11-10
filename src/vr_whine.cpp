/* Transmission Whine: ropi's UG2WhineGen sounds and the pitch/gain model of
 * nfsu2-web runtime/whine.c (MIT, community/vendor/roam/LICENSE). Native
 * one-player mixer; settings and telemetry cross threads as atomic values. */
#include "vr_whine.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace {
struct Wave {std::vector<float> pcm;unsigned rate=0;};
struct Voice {double pos=0;float gain=0,pitch=0;};
Wave waves[3];Voice voices[3];
std::atomic<float> gains[3],pitches[3],level{0};
std::atomic<unsigned> stamp{0};std::atomic<bool> enabled{false},ready{false};
std::once_flag startup;
uint32_t previous_car;int previous_gear;float gear_pitch,last_speed,shift_left,mixed_level;
unsigned now_ms(){return unsigned(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
uint16_t rd16(const uint8_t *p){return uint16_t(p[0]|uint16_t(p[1])<<8);}
uint32_t rd32(const uint8_t *p){return rd16(p)|uint32_t(rd16(p+2))<<16;}
bool load(const std::filesystem::path &path,Wave &out)
{
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return false;
    auto size=f.tellg();if(size<44 || size>4*1024*1024)return false;
    std::vector<uint8_t> b(static_cast<size_t>(size));f.seekg(0);
    if(!f.read(reinterpret_cast<char*>(b.data()),size) || std::memcmp(b.data(),"RIFF",4) ||
       std::memcmp(b.data()+8,"WAVE",4) || rd32(b.data()+4)!=b.size()-8)return false;
    unsigned rate=0;bool format=false;
    for(size_t o=12;o+8<=b.size();){
        uint32_t n=rd32(b.data()+o+4);if(n>b.size()-o-8)return false;
        const uint8_t *p=b.data()+o+8;
        if(!std::memcmp(b.data()+o,"fmt ",4)){
            if(n<16 || rd16(p)!=1 || rd16(p+2)!=1 || rd16(p+12)!=2 || rd16(p+14)!=16)return false;
            rate=rd32(p+4);if(rate<8000 || rate>192000 || rd32(p+8)!=rate*2)return false;format=true;
        }else if(!std::memcmp(b.data()+o,"data",4)){
            if(!format || n<8 || (n&1))return false;
            out.rate=rate;out.pcm.resize(n/2);
            for(size_t i=0;i<out.pcm.size();++i)out.pcm[i]=static_cast<int16_t>(rd16(p+2*i))/32768.f;
            return true;
        }
        o+=8u+n+(n&1u);
    }return false;
}
float glide(float x,float target,float k){x+=(target-x)*k;return std::fabs(target-x)<1e-6f?target:x;}
}
int vr_whine_start(const char *parent)
{
    std::call_once(startup,[parent]{
        const char *choice=std::getenv("NFSU2_GEARBOX_WHINE");
        if(choice && (!std::strcmp(choice,"0") || !std::strcmp(choice,"1")))vr_whine_enable(*choice=='1');
        if(!parent || !*parent)return;
        const char *names[]={"car_whine.wav","car_whine_2.wav","car_whine_reverse.wav"};
        Wave loaded[3];
        for(unsigned i=0;i<3;++i)if(!load(std::filesystem::u8path(parent)/"MODPACKS"/"WHINE"/names[i],loaded[i])){
            std::fprintf(stderr,"[whine] optional sound pack unavailable or invalid\n");return;
        }
        for(unsigned i=0;i<3;++i)waves[i]=std::move(loaded[i]);
        ready.store(true,std::memory_order_release);
        std::fprintf(stderr,"[whine] three validated mono sound loops loaded\n");
    });return ready.load(std::memory_order_acquire);
}
void vr_whine_enable(int value){enabled.store(value!=0,std::memory_order_relaxed);}
int vr_whine_enabled(void){return enabled.load(std::memory_order_relaxed);}
int vr_whine_available(void){return ready.load(std::memory_order_acquire);}
void vr_whine_update(uint32_t car,int running,float speed,int gear,float volume,float dt)
{
    bool valid=car && running && std::isfinite(speed) && std::fabs(speed)<=200 && gear>=0 && gear<=8 &&
        std::isfinite(dt) && dt>0 && dt<.25f && std::isfinite(volume) && volume>=0 && volume<=1;
    level.store(valid?volume*1.65f:0,std::memory_order_relaxed);stamp.store(now_ms(),std::memory_order_relaxed);
    if(!valid){previous_car=0;previous_gear=0;gear_pitch=last_speed=shift_left=0;return;}
    if(car!=previous_car){previous_car=car;previous_gear=0;gear_pitch=last_speed=shift_left=0;}
    float kmh=std::fabs(speed)*3.6f,pitch=.3f;
    if(kmh>=1){
        float target=std::max(0.f,(gear-1)*.020f);
        bool up=previous_gear && gear>previous_gear;previous_gear=gear;
        if(up){shift_left=.3f;gear_pitch=target;}
        float step=.001f*dt*60;
        if(last_speed>kmh && gear_pitch>target)gear_pitch=std::max(target,gear_pitch-step);
        if(last_speed<kmh && gear_pitch<target)gear_pitch=std::min(target,gear_pitch+step);
        last_speed=kmh;
        pitch=.3f+kmh/330*.7f+std::min(.6f,.5f*std::sqrt(kmh/70))+gear_pitch;
    }
    float shift=shift_left>0?0.f:1.f;if(shift_left>0)shift_left-=dt;
    gains[0].store(gear?.030f*shift:0,std::memory_order_relaxed);
    gains[1].store(gear && kmh>=1?kmh/140*.015f*shift:0,std::memory_order_relaxed);
    gains[2].store(!gear && kmh>=1?.050f:0,std::memory_order_relaxed);
    pitches[0].store(pitch,std::memory_order_relaxed);pitches[1].store(1,std::memory_order_relaxed);pitches[2].store(pitch,std::memory_order_relaxed);
}
int vr_whine_mix(const int16_t *game,int16_t *out,int samples,unsigned rate)
{
    if(!game || !out || samples<=0 || (samples&1) || rate<8000 || rate>192000 || !ready.load(std::memory_order_acquire))return 0;
    float target=enabled.load(std::memory_order_relaxed) && now_ms()-stamp.load(std::memory_order_relaxed)<=250?level.load(std::memory_order_relaxed):0;
    if(mixed_level==0 && target==0)return 0;
    const float kl=1-std::exp(-1/(.070f*rate)),kg=1-std::exp(-1/(.035f*rate)),kp=1-std::exp(-1/(.020f*rate));
    float gain[3],pitch[3];for(unsigned j=0;j<3;++j){gain[j]=gains[j].load(std::memory_order_relaxed);pitch[j]=pitches[j].load(std::memory_order_relaxed);}
    for(int i=0;i<samples;i+=2){
        mixed_level=glide(mixed_level,target,kl);float mono=0;
        for(unsigned j=0;j<3;++j){
            Voice &v=voices[j];const Wave &w=waves[j];v.gain=glide(v.gain,gain[j],kg);v.pitch=glide(v.pitch,pitch[j],kp);
            auto a=static_cast<size_t>(v.pos),b=(a+1)%w.pcm.size();float fraction=float(v.pos-a);
            mono+=(w.pcm[a]+(w.pcm[b]-w.pcm[a])*fraction)*v.gain;
            v.pos=std::fmod(v.pos+v.pitch*w.rate/rate,double(w.pcm.size()));
        }
        int add=static_cast<int>(mono*mixed_level*32768.f);
        out[i]=int16_t(std::clamp(int(game[i])+add,-32768,32767));out[i+1]=int16_t(std::clamp(int(game[i+1])+add,-32768,32767));
    }return 1;
}
