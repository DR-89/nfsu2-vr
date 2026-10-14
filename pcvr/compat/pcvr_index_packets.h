#pragma once
#include <stdint.h>
#include <emmintrin.h>

/* ARRAY_ELEMENT16 supplies low-half then high-half indices. Preserve every
 * word, including arbitrary alignment and the final partial SIMD group. The
 * caller checks available index capacity before changing any state. */
static inline void pcvr_unpack_indices16(uint32_t *out,const uint32_t *in,uint32_t words)
{
    const __m128i zero=_mm_setzero_si128();
    uint32_t i=0;
    for(;words-i>=4;i+=4) {
        __m128i packed=_mm_loadu_si128((const __m128i *)(in+i));
        _mm_storeu_si128((__m128i *)(out+2*i),_mm_unpacklo_epi16(packed,zero));
        _mm_storeu_si128((__m128i *)(out+2*i+4),_mm_unpackhi_epi16(packed,zero));
    }
    for(;i<words;++i) { out[2*i]=in[i]&0xffffu; out[2*i+1]=in[i]>>16; }
}
