#ifndef XYZ_DAMAGE_H
#define XYZ_DAMAGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Guest thread only. Guest pointers always remain 32 bit, including on ARM64.
 * Allocation returns owned, 128-byte-aligned GPU-visible storage. */
typedef struct XyzDamageHost {
    void *(*map)(uint32_t address);
    uint32_t (*address)(const void *pointer);
    int (*valid)(uint32_t address,uint32_t size);
    uint32_t (*alloc)(uint32_t size);
    void (*free)(uint32_t address);
    uint32_t (*available)(void);
    uint32_t (*call)(uint32_t function,uint32_t self,int count,const uint32_t *args,float *result);
    uint32_t (*scratches)(uint32_t *data,uint32_t *size);
} XyzDamageHost;
int xyz_damage_start(const XyzDamageHost *host,int compact);
void xyz_damage_frame(float world_seconds);
int xyz_damage_enabled(void);
void dmg_render_entry(uint32_t cri,uint32_t args);
void dmg_impulse(uint32_t body,uint32_t impulse,uint32_t point);
void dmg_zero(uint32_t car_damage,uint32_t caller);
int xyz_damage_open(uint32_t cri,int part,int original_open);
int xyz_damage_wants_open(uint32_t cri,int part);
void dmg_body_draw(uint32_t model,uint32_t matrix);
void dmg_marker_pre(int slot,uint32_t model,uint32_t matrix);
void dmg_marker_post(void);
uint32_t dmg_net_rev(void);
int dmg_net_mine(uint8_t *epoch,uint16_t *first,uint8_t *dents,int max);
void dmg_net_car(int slot,uint32_t car,uint32_t peer,uint8_t epoch,uint16_t first,int count,const uint8_t *dents);
typedef struct XyzDamageStats {uint32_t clones,retired,cars,failures,frame,world_ticks,drawn,overlays,loose,fallen,debris;
    uint32_t debris_started,ground_queries,ground_found,wall_queries,wall_found;} XyzDamageStats;
void xyz_damage_stats(XyzDamageStats *out);
#ifdef __cplusplus
}
#endif
#endif
