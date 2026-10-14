#include <SDL.h>
#include <SDL_system.h>
#include <android/log.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include "quest_xr.h"

static void load_options(void)
{
    FILE *f = fopen("quest-env.txt", "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *equals = strchr(line, '=');
        if (!equals || line[0] == '#') continue;
        *equals++ = 0;
        if (!strncmp(line, "NFSU2_", 6) || !strncmp(line, "RECOMP_", 7))
            setenv(line, equals, 1);
    }
    fclose(f);
}

struct game_start { int (*entry)(void); int result; };
static void *game_thread(void *arg)
{
    struct game_start *game = arg;
    pthread_setname_np(pthread_self(), "NFSU2 guest");
    quest_xr_game_thread();
    game->result = game->entry();
    return NULL;
}

int quest_run_game(int (*entry)(void))
{
    const char *storage = SDL_AndroidGetInternalStoragePath();
    if (!storage || chdir(storage) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NFSU2 Quest",
            "Cannot open the app's game storage.", NULL);
        return 1;
    }
    freopen("nfsu2.log", "w", stdout);
    dup2(fileno(stdout), STDERR_FILENO);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    __android_log_print(ANDROID_LOG_INFO, "NFSU2", "Starting from %s", storage);
    fprintf(stderr, "[Quest] native arm64 game boot; build=" __DATE__ " " __TIME__ "; storage=%s\n", storage);
    mkdir("save", 0700);
    mkdir("frames", 0700);
    load_options();
    setenv("RECOMP_QUIET", "1", 0);
    setenv("RECOMP_FPS_LOG", "1", 0);
    setenv("RECOMP_VK_INSTANCE", "0", 0);
    setenv("NFSU2_VR_RESOLUTION", "1", 0);
    setenv("NFSU2_VR_60FPS", "1", 0);
    setenv("NFSU2_GAME_DIR", "game", 1);
    if (access("game/default.xbe", R_OK) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NFSU2 Quest",
            "Game data missing. Install the extracted Xbox NTSC-U disc in the app's files/game directory.", NULL);
        return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!quest_xr_initialize()) {
        fprintf(stderr, "[Quest] OpenXR initialization failed\n");
        return 1;
    }
    /* The NV2A executor's old screen-space rejection cannot see tracked eye
     * transforms. Rejecting there would drop geometry before the VR shader. */
    if (quest_xr_enabled()) setenv("RECOMP_CULL", "0", 1);
    struct game_start game = {entry, 1};
    pthread_attr_t attr;
    pthread_t thread;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u * 1024 * 1024);
    int err = pthread_create(&thread, &attr, game_thread, &game);
    pthread_attr_destroy(&attr);
    if (err == 0) pthread_join(thread, NULL);
    else fprintf(stderr, "Cannot start game thread: %d\n", err);
    quest_xr_shutdown();
    SDL_Quit();
    return game.result;
}
