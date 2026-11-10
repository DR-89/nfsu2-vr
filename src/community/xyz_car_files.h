#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Host boot, before guest threads. Checks only the configured local game data.
 * No network names/paths are accepted; names come from the audited Xbox table. */
void xyz_car_files_init(const char *game_directory_utf8);
int xyz_car_files_available(const char *type_name);
#ifdef __cplusplus
}
#endif
