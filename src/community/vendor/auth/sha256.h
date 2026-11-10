#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {uint32_t h[8];uint64_t bytes;uint8_t tail[64];size_t used;} Nfsu2Sha256;
void nfsu2_sha256_init(Nfsu2Sha256 *);
void nfsu2_sha256_update(Nfsu2Sha256 *,const void *,size_t);
void nfsu2_sha256_final(Nfsu2Sha256 *,uint8_t out[32]);
void nfsu2_sha256(const void *,size_t,uint8_t out[32]);
#ifdef __cplusplus
}
#endif
