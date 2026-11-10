#include "copchase_contact.h"
#include <cmath>
namespace {
/* Preserve the source SSE float32 operation boundaries on ARM64 as well.
 * In particular, a fused dot product can change a strict threshold result. */
float f32(float v){volatile float rounded=v;return rounded;}
bool valid(const CopChasePose &p){
    for(unsigned k=0;k<2;++k)
        if(!std::isfinite(p.position[k]) || std::fabs(p.position[k])>20000 ||
           !std::isfinite(p.velocity[k]) || std::fabs(p.velocity[k])>200)return false;
    return true;
}
}
extern "C" int copchase_contact_update(const CopChasePose *cop,const CopChasePose *player,
                                        uint32_t now,uint32_t previous,CopChaseContactOutput *out){
    if(!out)return 0;
    *out={previous,uint32_t(now-previous),0,0,0};
    if(!cop || !player || !valid(*cop) || !valid(*player))return 0;
    const float dx=f32(player->position[0]-cop->position[0]);
    const float dy=f32(player->position[1]-cop->position[1]);
    const float distance=float(std::sqrt(double(f32(f32(dy*dy)+f32(dx*dx)))));
    out->distance=distance;
    if(!(distance<5.5f))return 1;
    const float vx=f32(cop->velocity[0]-player->velocity[0]);
    const float vy=f32(cop->velocity[1]-player->velocity[1]);
    const float dot=f32(f32(vy*dy)+f32(vx*dx));
    out->closing_speed=f32(dot/f32(distance+.01f));
    if(out->closing_speed>2.5f && out->elapsed_ms>400u){
        out->last_contact_ms=now;out->elapsed_ms=0;out->refreshed=1;
    }
    return 1;
}
