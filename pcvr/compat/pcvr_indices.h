#pragma once
#include <stdint.h>
#include <emmintrin.h>

/* SSE2 is baseline on Windows x64. Unaligned loads/stores and scalar tails
 * preserve arbitrary input alignment and every uint32_t value, including
 * unsigned range extrema and wraparound during rebasing. */
static inline void pcvr_index_range(const uint32_t *indices,uint32_t count,uint32_t *low,uint32_t *high)
{
    const __m128i sign=_mm_set1_epi32((int)0x80000000u);
    __m128i minimum=_mm_set1_epi32(-1),maximum=_mm_setzero_si128();
    uint32_t i=0;
    for(;count-i>=4;i+=4) {
        __m128i v=_mm_loadu_si128((const __m128i *)(indices+i));
        __m128i ordered=_mm_xor_si128(v,sign);
        __m128i smaller=_mm_cmpgt_epi32(_mm_xor_si128(minimum,sign),ordered);
        __m128i larger=_mm_cmpgt_epi32(ordered,_mm_xor_si128(maximum,sign));
        minimum=_mm_or_si128(_mm_and_si128(smaller,v),_mm_andnot_si128(smaller,minimum));
        maximum=_mm_or_si128(_mm_and_si128(larger,v),_mm_andnot_si128(larger,maximum));
    }
    uint32_t lows[4],highs[4];
    _mm_storeu_si128((__m128i *)lows,minimum); _mm_storeu_si128((__m128i *)highs,maximum);
    uint32_t lo=UINT32_MAX,hi=0;
    for(unsigned lane=0;lane<4;++lane) { if(lows[lane]<lo) lo=lows[lane]; if(highs[lane]>hi) hi=highs[lane]; }
    for(;i<count;++i) { if(indices[i]<lo) lo=indices[i]; if(indices[i]>hi) hi=indices[i]; }
    *low=lo; *high=hi;
}
static inline void pcvr_index_rebase(uint32_t *output,const uint32_t *indices,uint32_t count,uint32_t low)
{
    __m128i base=_mm_set1_epi32((int)low);
    uint32_t i=0;
    for(;count-i>=4;i+=4)
        _mm_storeu_si128((__m128i *)(output+i),_mm_sub_epi32(_mm_loadu_si128((const __m128i *)(indices+i)),base));
    for(;i<count;++i) output[i]=indices[i]-low;
}
