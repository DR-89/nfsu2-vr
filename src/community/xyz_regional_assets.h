#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int xyz_regional_start(const char *parent,int enabled);
int xyz_regional_available(const char *model);
const char *xyz_regional_redirect(const char *path);
/* Compose optional disc overlays without redirecting save paths. */
const char *xyz_community_redirect(const char *path);
#ifdef __cplusplus
}
#endif
