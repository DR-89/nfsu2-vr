#include "xyz_xbox_lobby.h"
#include "xyz_xbox_race.h"
#include "xyz_xbox_race_settings.h"
#include "xyz_xbox_career.h"
/* Called on the guest main thread only, with the existing re-entry guard. */
static XyzXboxLobby community_engine_lobby;
static uint32_t community_engine_thread;
static unsigned community_engine_load_pumps;
static int community_engine_call(void *context,enum XyzLobbyOp op,uint32_t self,
                                 unsigned count,const uint32_t *args,uint32_t *result)
{
    (void)context;
    static const struct {uint32_t address;unsigned count;} entries[XYZ_L_OP_COUNT]={
        {0x1953c0,0},{0x19c0b0,0},{0x19aea0,1},{0x1924f0,0},{0x19a7d0,1},
        {0x2901b0,6},{0x28f750,5},{0x28f4a0,4},{0x28af50,2},{0x28bcd0,4},
        {0x1985d0,4},{0x196120,1},{0x198740,2},{0x198230,5},{0x2932b0,1},{0x195b20,3},{0x19b560,4},{0x191280,0}
    };
    if((unsigned)op>=XYZ_L_OP_COUNT || count!=entries[op].count || !result || (count && !args))return 0;
    recomp_func_t fn=recomp_lookup(entries[op].address);
    if(!fn)return 0;
    if(op==XYZ_L_INIT){community_engine_thread=g_fs_base;community_engine_load_pumps=0;}
    *result=guest_call_full(fn,self,0,0x0018ab7fu,count,args);return 1;
}
static XyzXboxGuest community_engine_memory={NULL,cockpit_ui_read,community_guest_write,community_guest_call};
static XyzLobbyGuest community_engine_guest={&community_engine_memory,community_engine_call};
static XyzXboxRace community_engine_race;
static int community_engine_race_call(void *context,enum XyzRaceOp op,uint32_t self,
                                      unsigned count,const uint32_t *args,uint32_t *result)
{
    (void)context;
    static const struct {uint32_t address;unsigned count;} entries[XYZ_R_OP_COUNT]={
        {0x1e08e0,1},{0x1e0270,0},{0x1dffc0,1},{0x113e80,1},{0x1f1b80,0},
        {0x1f9890,0},{0x1ee130,1},{0x18b1f0,0},{0x191280,0},{0x118be0,1}
    };
    if((unsigned)op>=XYZ_R_OP_COUNT || count!=entries[op].count || !result || (count && !args))return 0;
    recomp_func_t fn=recomp_lookup(entries[op].address);if(!fn)return 0;
    *result=guest_call_full(fn,self,0,0x0018ab7fu,count,args);return 1;
}
static XyzRaceGuest community_race_guest={&community_engine_guest,community_engine_race_call};
static int community_career_call(void *context,enum XyzCareerOp op)
{
    (void)context;uint32_t args[2]={0};unsigned count=0;uint32_t address=0;
    if(op==XYZ_C_QUIT){
        /* Pause's Quit is a selected command (D9CC8 -> C1B20), not an
         * independent FE notification. Sending its selection hash to
         * 166470 silently does nothing. Resolve the live controller and
         * invoke the same original Quit method, preserving the selection. */
        uint32_t name=0x34d94c;
        uint32_t controller=guest_call_full(recomp_lookup(0x128bb0),0,0,0x0018ab7fu,1,&name);
        static uint32_t last_controller=UINT32_MAX;
        if(controller!=last_controller){fprintf(stderr,"[XYZ career] Quit controller=%08x package=%08x coordinator=%08x\n",controller,
            xyz_xbox_address(controller,8)?MEM32(controller+4):0,MEM32(0x449438));last_controller=controller;}
        if(!xyz_xbox_address(controller,8) || !quest_ui_package(cockpit_ui_read,NULL,MEM32(controller+4),"UI_Pause.fng") ||
           !xyz_xbox_address(MEM32(0x449438),0x60))return 0;
        guest_call_full(recomp_lookup(0xc1b20),controller,0,0x0018ab7fu,0,NULL);return 1;
    }
    else if(op==XYZ_C_MAIN){
        args[0]=0x34c7a0;
        guest_call_full(recomp_lookup(0x118be0),0,0,0x0018ab7fu,1,args);
        /* Pause::Quit has recorded the career and notified its coordinator.
         * The native overlay does not drive Pause's exit animation. Complete
         * its free-roam branch (1F37E2 -> 1F37CE) at this boundary using the
         * original asynchronous world-unload request, as race return does. */
        if(MEM32(0x428e14)==0)guest_call_full(recomp_lookup(0x18b1f0),0x428e10,0,0x0018ab7fu,0,NULL);
        return 1;
    }
    else if(op==XYZ_C_EXPLORE)address=0x14c5b0;
    recomp_func_t fn=recomp_lookup(address);if(!fn)return 0;
    guest_call_full(fn,0,0,0x0018ab7fu,count,args);return 1;
}
static XyzCareerGuest community_career_guest={&community_engine_memory,community_career_call};
static int community_engine_callback_owner(void)
{
    return xyz_xbox_address(esp,16) && xyz_xbox_lobby_owns_callback(&community_engine_lobby,MEM32(esp+4),MEM32(esp+12));
}
static void community_engine_callback(void)
{
    xyz_xbox_lobby_event(&community_engine_guest,&community_engine_lobby,MEM32(esp+8));
    eax=0;esp+=4;
}
static int community_engine_join_owner(void)
{
    return community_engine_lobby.initialized && xyz_xbox_address(esp,16) &&
        MEM32(esp+12)==community_engine_lobby.games && MEM32(esp+4)==community_engine_lobby.protocol;
}
static void community_engine_join_callback(void)
{
    xyz_xbox_lobby_event(&community_engine_guest,&community_engine_lobby,MEM32(esp+8));
    recomp_func_t fn=recomp_lookup(0x196410u);if(fn)fn();else {eax=0;esp+=4;}
}
static void community_engine_search_callback(void)
{
    xyz_xbox_lobby_event(&community_engine_guest,&community_engine_lobby,MEM32(esp+8));
    recomp_func_t fn=recomp_lookup(0x196680u);if(fn)fn();else {eax=0;esp+=4;}
}
static int community_engine_request_owner(void)
{
    return community_engine_lobby.initialized && xyz_xbox_address(esp,16) &&
        MEM32(esp+12)==community_engine_lobby.manager && MEM32(esp+4)==community_engine_lobby.protocol;
}
static void community_engine_request_callback(void)
{
    xyz_xbox_lobby_event(&community_engine_guest,&community_engine_lobby,MEM32(esp+8));
    recomp_func_t fn=recomp_lookup(0x18f530u);if(fn)fn();else {eax=0;esp+=4;}
}
static void community_engine_game_callback(void)
{
    uint32_t argument=esp+8,message=MEM32(argument),prepared=0;
    if(xyz_xbox_address(message,20) && MEM32(message+8)==0x706c6179u){
        if((community_engine_lobby.stage!=XYZ_LOBBY_IN_GAME && community_engine_lobby.stage!=XYZ_LOBBY_STARTING) ||
           !xyz_xbox_lobby_prepare_play(&community_engine_guest,&community_engine_lobby,message,&prepared)){
            community_engine_lobby.event_error=1;eax=0;esp+=4;return;
        }
        MEM32(argument)=prepared;
    }
    recomp_func_t fn=recomp_lookup(0x198a40u);if(fn)fn();else {eax=0;esp+=4;}
    if(prepared){MEM32(argument)=message;community_engine_lobby.play_received=1;}
}
