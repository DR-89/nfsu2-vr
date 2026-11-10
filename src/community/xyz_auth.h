#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Normalize the service's eight-word keyphrase, with its optional prefix.
 * Outputs are cleared on failure. No network, storage, logging or ownership
 * claim: successful derivation alone does not prove the name belongs to us. */
int xyz_auth_phrase(const char *text,char *out,size_t capacity);
/* Worker thread only: PBKDF2-SHA256, 100000 rounds. The token is the second
 * 32-byte block of the upstream 64-byte derivation, base64url without padding.
 * The encryption key for cloud saves is deliberately not derived here. */
int xyz_auth_token(const char *text,char *out,size_t capacity);
/* Optional wire claim: empty or exactly 43 base64url characters. */
int xyz_auth_claim_valid(const char *claim);
void xyz_auth_clear(void *data,size_t size);
#ifdef __cplusplus
}
#endif
