#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

/* Deliberately small evaluator for screen-space programs. Unsupported math,
 * relative addressing and constant writes fall back to the snapshot path.
 * Sources are read before either half of a paired instruction writes. */
static inline int quest_feedback_position(const uint32_t (*program)[4], uint32_t slots,
    const float (*constants)[4], uint32_t nconstants, const float input[16][4], float out[4])
{
    float reg[13][4]={{0}};
    if(!program || !slots || slots>136 || !constants) return 0;
    for(uint32_t pc=0;pc<slots && pc<32;++pc) {
        const uint32_t *t=program[pc];
        unsigned mac=(t[1]>>21)&15, ilu=(t[1]>>25)&7;
        unsigned rd=(t[3]>>20)&15, om=(t[3]>>12)&15, od=(t[3]>>3)&255;
        if(mac>3 || ilu>2 || (t[3]&2) || (om && !(t[3]&0x800))) return 0;
        float src[3][4], m[4]={0}, l[4]={0};
        unsigned mux[3]={(t[2]>>26)&3,(t[2]>>11)&3,(t[3]>>28)&3};
        unsigned ri[3]={t[2]>>28,(t[2]>>13)&15,((t[2]&3)<<2)|(t[3]>>30)};
        unsigned neg[3]={(t[1]>>8)&1,(t[2]>>25)&1,(t[2]>>10)&1};
        unsigned sw[3]={t[1]&255,(t[2]>>17)&255,(t[2]>>2)&255};
        for(unsigned s=0;s<3;++s) {
            const float zero[4]={0}, *v=zero;
            if(mux[s]==1 && ri[s]<13) v=reg[ri[s]];
            if(mux[s]==2) v=input[(t[1]>>9)&15];
            if(mux[s]==3) {
                unsigned ci=(t[1]>>13)&255; if(ci>191) ci=191;
                if(ci>=nconstants) return 0;
                v=constants[ci];
            }
            for(unsigned c=0;c<4;++c) src[s][c]=v[(sw[s]>>(6-2*c))&3]*(neg[s]?-1.0f:1.0f);
        }
        for(unsigned c=0;c<4;++c) {
            if(mac==1) m[c]=src[0][c];
            if(mac==2) m[c]=src[0][c]*src[1][c];
            if(mac==3) m[c]=src[0][c]+src[2][c];
            if(ilu==1) l[c]=src[2][c];
            if(ilu==2) {
                /* Refuse near-zero reciprocals instead of depending on the
                 * device's denormal/approximate reciprocal implementation. */
                if(!isfinite(src[2][0]) || fabsf(src[2][0])<1e-5f) return 0;
                l[c]=1.0f/src[2][0];
            }
        }
        for(unsigned c=0;c<4;++c) {
            unsigned bit=8u>>c, ld=mac?1:rd;
            if(mac && rd<13 && ((t[3]>>24)&bit)) reg[rd][c]=m[c];
            if(ilu && ld<13 && ((t[3]>>16)&bit)) reg[ld][c]=l[c];
            if(om & bit && od==0) reg[12][c]=(t[3]&4)?l[c]:m[c];
        }
        if(t[3]&1) {
            memcpy(out,reg[12],4*sizeof(float));
            for(unsigned c=0;c<4;++c) if(!isfinite(out[c])) return 0;
            /* Exactly unit W also makes the GPU snap/clip calculation
             * independent of reciprocal rounding. */
            return out[3]==1.0f;
        }
    }
    return 0;
}

/* The caller preserves the one-pixel border by copying it. Prove that all
 * remaining sample centers are strictly inside this triangle. This avoids
 * guessing a rasterizer's edge tie rule and preserves uncovered edge pixels. */
static inline int quest_feedback_covers(const float position[3][4],
    uint32_t width, uint32_t height, uint32_t stored_width, uint32_t stored_height)
{
    if(!width || !height || stored_width<4 || stored_height<4) return 0;
    double p[3][2];
    for(unsigned i=0;i<3;++i) for(unsigned c=0;c<2;++c) {
        float guest=(float)(c?height:width), stored=(float)(c?stored_height:stored_width);
        float bias=0.5f-0.5f*guest/stored;
        float snapped=truncf(position[i][c]*16.0f)/16.0f-bias;
        /* Match the float shader's NDC math before converting to pixels. */
        float ndc=snapped*(2.0f/guest)-1.0f;
        p[i][c]=((double)ndc+1.0)*stored*0.5;
        if(!isfinite(p[i][c])) return 0;
    }
    double area=(p[1][0]-p[0][0])*(p[2][1]-p[0][1])-(p[1][1]-p[0][1])*(p[2][0]-p[0][0]);
    if(!isfinite(area) || fabs(area)<1e-6) return 0;
    double sign=area>0?1:-1;
    for(unsigned e=0;e<3;++e) {
        unsigned n=(e+1)%3;
        double dx=p[n][0]-p[e][0], dy=p[n][1]-p[e][1];
        double margin=(fabs(dx)+fabs(dy))*0.25;
        for(unsigned corner=0;corner<4;++corner) {
            double x=(corner&1)?stored_width-1.5:1.5;
            double y=(corner&2)?stored_height-1.5:1.5;
            if(sign*(dx*(y-p[e][1])-dy*(x-p[e][0]))<=margin) return 0;
        }
    }
    return 1;
}
