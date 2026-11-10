#ifndef XYZ_DAMAGE_ARENA_H
#define XYZ_DAMAGE_ARENA_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One guest-thread owner. Backing chunks stay reserved until process exit;
 * freed clone ranges are reused only after their render retirement delay. */
void xyz_damage_arena_init(uint32_t (*reserve)(uint32_t,uint32_t));
uint32_t xyz_damage_arena_alloc(uint32_t bytes);
void xyz_damage_arena_free(uint32_t address);
uint32_t xyz_damage_arena_available(void);
#ifdef __cplusplus
}
#endif
#endif
