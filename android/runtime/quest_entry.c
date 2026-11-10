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
#include "vr_music.h"
#include "vr_whine.h"
#include "vr_texpacks.h"
#include "vr_subtitles.h"
#include "vr_vinyls.h"
#include "vr_soundtrack.h"
#include "community/xyz_regional_assets.h"
#include "community/copchase_assets.h"
#include "community/copchase_audio.h"
#include "community/copchase_profile.h"
#include "community/copchase_runtime.h"
extern void (*xbox_file_read_observer)(void*,const char*,void*,size_t,int64_t);
#include "community/xyz_damage_textures.h"
extern const char *(*xbox_path_redirect_hook)(const char *);
#include "apu_xaudio2.h"

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
/* Install optional bundled audio once. Existing user sound overrides stay.
 * SDL's Android reader opens APK assets when the relative file is absent. */
static void install_community_audio(const char *storage)
{
    if(!storage || !*storage)return;
    char folder[4096],path[4096],temp[4112];
    if(snprintf(folder,sizeof folder,"%s/MODPACKS",storage)>=(int)sizeof folder)return;
    mkdir(folder,0700);
    if(snprintf(folder,sizeof folder,"%s/MODPACKS/WHINE",storage)>=(int)sizeof folder)return;
    mkdir(folder,0700);
    const char *names[]={"car_whine.wav","car_whine_2.wav","car_whine_reverse.wav"};
    for(unsigned i=0;i<3;++i){
        if(snprintf(path,sizeof path,"%s/%s",folder,names[i])>=(int)sizeof path)continue;
        if(access(path,F_OK)==0)continue;
        char asset[128];snprintf(asset,sizeof asset,"MODPACKS/WHINE/%s",names[i]);
        SDL_RWops *source=SDL_RWFromFile(asset,"rb");if(!source)continue;
        Sint64 size=SDL_RWsize(source);
        if(size<44 || size>4*1024*1024){SDL_RWclose(source);continue;}
        void *data=malloc((size_t)size);if(!data){SDL_RWclose(source);continue;}
        int complete=SDL_RWread(source,data,1,(size_t)size)==(size_t)size;SDL_RWclose(source);
        snprintf(temp,sizeof temp,"%s.tmp",path);
        if(complete){FILE *f=fopen(temp,"wb");if(f){
            complete=fwrite(data,1,(size_t)size,f)==(size_t)size;
            if(fclose(f))complete=0;
            if(complete)rename(temp,path);else unlink(temp);
        }}
        free(data);
    }
}
static void install_community_textures(const char *storage)
{
    if(!storage || !*storage)return;
    char folder[4096];if(snprintf(folder,sizeof folder,"%s/TEXPACKS",storage)>=(int)sizeof folder)return;mkdir(folder,0700);
    const char *names[]={"uhud.pak","detailed.pak","detailed.tracks","english-subtitles.bin","crash-scratches.tpk"};
    for(unsigned i=0;i<sizeof names/sizeof *names;i++){
        char path[4096],temporary[4112],asset[128];
        if(snprintf(path,sizeof path,"%s/%s",folder,names[i])>=(int)sizeof path)continue;
        if(access(path,F_OK)==0)continue;
        snprintf(asset,sizeof asset,"TEXPACKS/%s",names[i]);SDL_RWops *src=SDL_RWFromFile(asset,"rb");if(!src)continue;
        Sint64 size=SDL_RWsize(src);if(size<=0 || size>160*1024*1024){SDL_RWclose(src);continue;}
        snprintf(temporary,sizeof temporary,"%s.tmp",path);FILE *dst=fopen(temporary,"wb");int ok=dst!=NULL;
        unsigned char bytes[65536];Sint64 left=size;
        while(ok && left){size_t n=(size_t)(left>(Sint64)sizeof bytes?sizeof bytes:left);
            ok=SDL_RWread(src,bytes,1,n)==n && fwrite(bytes,1,n,dst)==n;left-=n;}
        SDL_RWclose(src);if(dst && fclose(dst))ok=0;
        if(ok)rename(temporary,path);else unlink(temporary);
    }
}
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
    vr_music_start(SDL_AndroidGetExternalStoragePath());
    install_community_audio(SDL_AndroidGetExternalStoragePath());
    vr_whine_start(SDL_AndroidGetExternalStoragePath());
    install_community_textures(SDL_AndroidGetExternalStoragePath());
    vr_texpacks_start(SDL_AndroidGetExternalStoragePath(),quest_xr_upscaled_hud(),quest_xr_detailed_map());
    vr_subtitles_start(SDL_AndroidGetExternalStoragePath(),quest_xr_subtitles());
    vr_vinyls_start(SDL_AndroidGetExternalStoragePath(),quest_xr_hq_vinyls());
    vr_soundtrack_start(SDL_AndroidGetExternalStoragePath(),quest_xr_restored_soundtrack());
    copchase_assets_start(SDL_AndroidGetExternalStoragePath(),quest_xr_copchase_at_start());
    quest_xr_copchase_data_status(quest_xr_copchase_at_start() || copchase_assets_ready(),copchase_assets_ready());
    if(copchase_assets_ready())copchase_audio_start(SDL_AndroidGetExternalStoragePath());
    if(copchase_assets_ready() && copchase_runtime_start())xbox_file_read_observer=copchase_profile_observe;
    xyz_damage_assets_start(SDL_AndroidGetExternalStoragePath());
    xyz_regional_start(SDL_AndroidGetExternalStoragePath(),quest_xr_regional_cars());
    xbox_path_redirect_hook=xyz_community_redirect;
    xa2_music_mix=vr_music_mix;
    struct game_start game = {entry, 1};
    pthread_attr_t attr;
    pthread_t thread;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u * 1024 * 1024);
    int err = pthread_create(&thread, &attr, game_thread, &game);
    pthread_attr_destroy(&attr);
    if (err == 0) pthread_join(thread, NULL);
    else fprintf(stderr, "Cannot start game thread: %d\n", err);
    vr_music_stop();
    copchase_audio_stop();
    xbox_file_read_observer=NULL;copchase_runtime_stop();
    quest_xr_shutdown();
    SDL_Quit();
    return game.result;
}
