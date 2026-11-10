/* sha256.c -- SHA-256 (FIPS 180-4) for the wasm build, where CommonCrypto
 * does not exist.  module.c pins the lifted server.dll to the file it was
 * lifted from with this hash (the Mac uses CC_SHA256 for the same check).
 * Plain portable C; checked against the FIPS 180-4 vectors by
 * web/test/lan_unit.c (node --test web/test/lan-wasm.test.mjs) and against
 * gen/server/modinfo.c's hash of the game's own server.dll at every load
 * ("sha256 ok" in the log). */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "sha256.h"

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};
#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))

static void block(uint32_t h[8], const uint8_t *p){
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++){
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++){
        uint32_t t1 = k + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

void nfsu2_sha256(const void *data, size_t n, uint8_t out[32]){
    uint32_t h[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    const uint8_t *p = data;
    size_t left = n;
    for (; left >= 64; left -= 64, p += 64) block(h, p);
    uint8_t tail[128]; memset(tail, 0, sizeof tail);
    memcpy(tail, p, left);
    tail[left] = 0x80;
    size_t tl = left < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)n * 8u;
    for (int i = 0; i < 8; i++) tail[tl - 1 - i] = (uint8_t)(bits >> (8 * i));
    block(h, tail);
    if (tl == 128) block(h, tail + 64);
    for (int i = 0; i < 8; i++){
        out[4*i] = (uint8_t)(h[i] >> 24); out[4*i+1] = (uint8_t)(h[i] >> 16);
        out[4*i+2] = (uint8_t)(h[i] >> 8); out[4*i+3] = (uint8_t)h[i];
    }
}

/* Native file verification: streaming form of the same compression function.
 * Keep the original one-shot entry unchanged for the authentication adapter. */
void nfsu2_sha256_init(Nfsu2Sha256 *s){
    static const uint32_t initial[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    memset(s,0,sizeof *s);memcpy(s->h,initial,sizeof initial);
}
void nfsu2_sha256_update(Nfsu2Sha256 *s,const void *data,size_t n){
    const uint8_t *p=data;if(!n)return;s->bytes+=n;
    if(s->used){size_t take=64-s->used;if(take>n)take=n;memcpy(s->tail+s->used,p,take);s->used+=take;p+=take;n-=take;
        if(s->used<64)return;block(s->h,s->tail);s->used=0;}
    while(n>=64){block(s->h,p);p+=64;n-=64;}
    if(n){memcpy(s->tail,p,n);s->used=n;}
}
void nfsu2_sha256_final(Nfsu2Sha256 *s,uint8_t out[32]){
    uint8_t tail[128]={0};memcpy(tail,s->tail,s->used);tail[s->used]=0x80;
    size_t n=s->used<56?64:128;uint64_t bits=s->bytes*8;
    for(int i=0;i<8;i++)tail[n-1-i]=(uint8_t)(bits>>(8*i));
    block(s->h,tail);if(n==128)block(s->h,tail+64);
    for(int i=0;i<8;i++)for(int j=0;j<4;j++)out[i*4+j]=(uint8_t)(s->h[i]>>(24-8*j));
    memset(s,0,sizeof *s);
}
