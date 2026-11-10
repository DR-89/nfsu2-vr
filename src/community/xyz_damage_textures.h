#ifndef XYZ_DAMAGE_TEXTURES_H
#define XYZ_DAMAGE_TEXTURES_H
#include "xyz_damage.h"
#ifdef __cplusplus
extern "C" {
#endif
void xyz_damage_assets_start(const char *parent);
int xyz_damage_textures_load(const XyzDamageHost *host);
uint32_t xyz_damage_texture(uint32_t hash);
#ifdef __cplusplus
}
#endif
#endif
