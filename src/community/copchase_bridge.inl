/* Guest-main-thread bridge. Original Xbox lifecycle/call contracts are in
 * build/copchase-re/xbox/lifecycle.asm and docs/copchase-native.md. */
#include "copchase_game.h"
#include "copchase_audio.h"
#include "copchase_profile.h"
#include "copchase_runtime.h"
#include "copchase_bust.h"
#include "copchase_record_flow.h"
static char community_copchase_profile_id[65];
static uint64_t community_copchase_profile_generation=1;
static void community_copchase_profile_clear(void)
{
    community_copchase_profile_id[0]=0;
    ++community_copchase_profile_generation;
}
static CopChaseGame *community_copchase_game;
static uint64_t community_copchase_world_generation=1,community_copchase_player_generation=1;
static uint32_t community_copchase_scratch_base,community_copchase_scratch;
static uint32_t community_copchase_player_car;
static int community_copchase_game_probe=-1;
static CopChaseRecordFlow *community_copchase_records_flow;
static int community_copchase_settlement_pending;
static int community_copchase_records_fault_reported;
static CopChaseBust *community_copchase_bust;
static CopChaseSessionFrame community_copchase_bust_result;
static int community_copchase_bust_waiting;

static int community_copchase_model(uint32_t car,char out[65])
{
    out[0]=0;if(!xyz_xbox_address(car,0x10))return 0;
    uint32_t types=MEM32(0x45af88),type=MEM32(car+0xc);char name[24];
    if(!types || type<types || type-types>=46u*0x890u || (type-types)%0x890u ||
       !cockpit_ui_read(NULL,type,name,sizeof name))return 0;
    unsigned n=0;for(;n<sizeof name && name[n];++n)
        if(!((name[n]>='A' && name[n]<='Z') || (name[n]>='0' && name[n]<='9') || name[n]=='_'))return 0;
    if(!n || n==sizeof name)return 0;memcpy(out,name,n);out[n]=0;return 1;
}
static int community_copchase_records_before(uint32_t car,CopChaseSessionFrame *frame,CopChaseSessionStatus *out)
{
    memset(frame,0,sizeof *frame);memset(out,0,sizeof *out);
    CopChaseSession *session=copchase_runtime_session();if(!session)return 0;
    if(!community_copchase_records_flow)
        community_copchase_records_flow=copchase_record_flow_for_session(session);
    if(!community_copchase_records_flow)return 0;
    char model[65]={0};community_copchase_model(car,model);
    int ready=copchase_record_flow_before(community_copchase_records_flow,
        community_copchase_profile_id,community_copchase_profile_generation,
        community_copchase_world_generation,community_copchase_player_generation,model,frame,out);
    enum CopChaseSessionResult error=copchase_record_flow_error(community_copchase_records_flow);
    if(error!=COPCHASE_SESSION_OK && !community_copchase_records_fault_reported){
        community_copchase_records_fault_reported=1;
        fprintf(stderr,"[CopChase records] result rejected (%d), further pursuits held\n",error);
    }
    return ready;
}
static void community_copchase_records_after(CopChaseSessionFrame *frame,const CopChaseGameStatus *status,int preset)
{
    if(!community_copchase_records_flow)return;
    frame->phase=status->pursuit.phase;
    frame->events=status->pending_events&(COPCHASE_STARTED|COPCHASE_EVADED|COPCHASE_BUSTED|COPCHASE_CANCELLED);
    if(!frame->events)return;
    frame->result.preset=(uint32_t)preset;frame->result.heat=(uint32_t)status->pursuit.heat;
    frame->result.fractional_heat=status->pursuit.retained_heat;
    frame->result.bounty=status->pursuit.bounty;
    frame->result.infractions=status->infractions;frame->result.cops_damaged=status->cops_damaged;
    frame->result.cops_taken_out=status->cops_taken_out;frame->result.cost_to_state=status->cost_to_state;
    frame->result.elapsed_ms=(uint64_t)fmin(4294967295.,fmax(0.,status->pursuit.pursuit_seconds*1000.));
    frame->result.keep_heat=1;
    /* Incident counters are populated only from independently verified events.
     * Always forward cancellation, even with an empty new frame or a queued
     * START: the coordinator retains the original pursuit's identity. */
    copchase_record_flow_after(community_copchase_records_flow,frame);
}

static int community_copchase_read(void *context,uint32_t address,void *data,uint32_t size)
{return cockpit_ui_read(context,address,data,size);}
static int community_copchase_write(void *context,uint32_t address,const void *data,uint32_t size)
{return community_guest_write(context,address,data,size);}
static int community_copchase_call(void *context,uint32_t address,uint32_t self,
                                  unsigned count,const uint32_t *args,uint32_t *result)
{
    (void)context;
    static const struct {uint32_t address;unsigned count;} allowed[]={
        {0x37550,1},{0x39b00,1},{0x1e47d0,1},{0x1e4a70,1},
        {0x1d13f0,0},{0x1d14a0,0},{0x1d6e70,2},{0x1d8440,5},
        {0x1f10c0,1},{0x207e00,1},{0x1ffda0,1}};
    int found=0;for(unsigned i=0;i<sizeof allowed/sizeof *allowed;++i)
        if(address==allowed[i].address && count==allowed[i].count)found=1;
    if(!found || !result || (count && !args) ||
       (address!=0x1e47d0 && !xyz_xbox_address(self,4)))return 0;
    recomp_func_t fn=recomp_lookup_manual(address);if(!fn)fn=recomp_lookup(address);
    if(!fn)return 0;
    *result=guest_call_full(fn,self,0,0x0018ab7fu,count,args);return 1;
}
static uint32_t community_copchase_alloc(void *context,uint32_t size)
{
    (void)context;if(!size || size>4096 || community_copchase_scratch_base)return 0;
    uint32_t allocation=0,total=size+15;
    if(!community_guest_call(NULL,XYZ_G_ALLOC,0,0,1,&total,&allocation) ||
       !xyz_xbox_address(allocation,total))return 0;
    community_copchase_scratch_base=allocation;
    return community_copchase_scratch=(allocation+15u)&~15u;
}
static void community_copchase_free(void *context,uint32_t address)
{
    (void)context;if(!address || address!=community_copchase_scratch)return;
    uint32_t result=0,allocation=community_copchase_scratch_base;
    community_copchase_scratch=community_copchase_scratch_base=0;
    community_guest_call(NULL,XYZ_G_FREE,0,0,1,&allocation,&result);
}
static int community_copchase_guards(void *context,CopChaseGameGuards *out)
{
    (void)context;XYZOnlineStatus online;XyzXboxStreaming streaming;
    memset(out,0,sizeof *out);xyz_online_snapshot(&online);
    out->world_generation=community_copchase_world_generation;
    out->player_generation=community_copchase_player_generation;
    out->assets_ready=copchase_assets_ready();out->offline=online.state==XYZ_ONLINE_OFFLINE;
    out->settlement_pending=community_copchase_settlement_pending ||
        copchase_record_flow_holding(community_copchase_records_flow);
    out->races_inactive=!xyz_races_active();out->fleet_retired=xyz_online_fleet_retired();
    out->paused=quest_cockpit_ui_paused(cockpit_ui_read,NULL) ||
        quest_cockpit_ui_menu(cockpit_ui_read,NULL)!=QUEST_UI_HUD;
    out->streaming_idle=xyz_xbox_streaming(cockpit_ui_read,NULL,&streaming) &&
        !streaming.phase && !streaming.car_busy &&
        !MEM32(0x38dbb0) && !MEM32(0x38dbc8) && !MEM32(0x38dbe0) &&
        !MEM8(0x38dba4) && !MEM8(0x38dbbc) && !MEM8(0x38dbd4) && !MEM8(0x3e3ab0);
    return 1;
}
static int community_copchase_read32(void *context,uint32_t address,uint32_t *value)
{return community_copchase_read(context,address,value,4);}
static int community_copchase_write_batch(void *context,const CopChaseRoadWrite *writes,unsigned count)
{
    (void)context;if(!writes || !count || (count>6 && count!=13))return 0;CopChaseGameGuards guards;
    community_copchase_guards(NULL,&guards);
    if(!guards.assets_ready || !guards.offline || !guards.races_inactive || !guards.fleet_retired ||
       guards.paused || guards.settlement_pending || !guards.streaming_idle)return 0;
    uint32_t size=count==13?12:4;
    for(unsigned i=0;i<count;++i)if(writes[i].size!=size || !xyz_xbox_address(writes[i].address,size))return 0;
    /* No guest callbacks or yield between preflight and commit. The caller
     * has just revalidated each owning car/body on this same guest thread. */
    for(unsigned i=0;i<count;++i)memcpy((void*)XBOX_PTR(writes[i].address),writes[i].bytes,size);
    return 1;
}
static int community_copchase_fine_identity(void *context,CopChaseFineIdentity *out)
{
    (void)context;memset(out,0,sizeof *out);CopChaseGameGuards guards;
    community_copchase_guards(NULL,&guards);
    out->profile=community_copchase_profile_generation;out->world=guards.world_generation;out->player=guards.player_generation;
    out->valid=community_copchase_profile_id[0] && MEM32(0x406cc0)==1;
    out->offline=guards.offline && guards.races_inactive && guards.fleet_retired && !MEM32(0x456290);
    out->career=MEM32(0x428e34)==6 && MEM32(0x41c338)==1 && !MEM32(0x3cc470) && !MEM32(0x406960);
    return 1;
}
static int community_copchase_bust_context(void *context,CopChaseBustContext *out)
{
    (void)context;memset(out,0,sizeof *out);community_copchase_fine_identity(NULL,&out->identity);
    uint32_t player=MEM32(0x44940c);out->world=MEM32(0x4493e0);out->director=MEM32(0x449438);
    if(!xyz_xbox_address(player,0x14) || !xyz_xbox_address(out->world,0x118) ||
       !xyz_xbox_address(out->director,4))return 0;
    out->car=MEM32(player+4);if(!xyz_xbox_address(out->car,0x10) || out->car!=community_copchase_player_car)return 0;
    out->controller=MEM32(out->car+8);out->flow=MEM32(0x428e34);out->director_state=MEM32(out->director);
    out->hud=quest_cockpit_ui_menu(cockpit_ui_read,NULL)==QUEST_UI_HUD;
    out->root_pause=quest_cockpit_ui_paused(cockpit_ui_read,NULL);
    CopChaseGameGuards guards;community_copchase_guards(NULL,&guards);
    out->can_pause=guards.assets_ready && guards.streaming_idle && !guards.paused && out->identity.offline;
    out->modal_visible=quest_xr_copchase_bust_visible();return 1;
}
static int community_copchase_bust_call(void *context,uint32_t address,uint32_t self,
                                      unsigned count,const uint32_t *args,uint32_t *result)
{
    (void)context;if(!result || (count && !args))return 0;
    int allowed=(address==0x111c50 && count==0 && self==0x405364) ||
        (address==0x111c40 && count==1 && self==0x405364) ||
        (address==0x1f2b50 && count==3 && self==community_copchase_player_car) ||
        (address==0x1e45f0 && count==1 && self==MEM32(0x449438) &&
            xyz_xbox_address(self,0x28) && args[0]==1);
    if(!allowed)return 0;recomp_func_t fn=recomp_lookup_manual(address);if(!fn)fn=recomp_lookup(address);
    if(!fn)return 0;*result=guest_call_full(fn,self,0,0x0018ab7fu,count,args);return 1;
}
static void community_copchase_bust_frame(const CopChaseGameStatus *game,const CopChaseSessionFrame *frame)
{
    if(!community_copchase_bust){const CopChaseBustHost host={{NULL,community_copchase_fine_identity,
        community_copchase_read32,community_copchase_bust_call},community_copchase_bust_context};
        community_copchase_bust=copchase_bust_create(&host);}
    if(!community_copchase_bust)return;
    if((game->pending_events&COPCHASE_BUSTED) && frame->model_id[0]){
        community_copchase_bust_result=*frame;community_copchase_bust_waiting=1;
    }
    struct timespec clock;clock_gettime(CLOCK_MONOTONIC,&clock);
    uint64_t now=(uint64_t)clock.tv_sec*1000u+(uint64_t)clock.tv_nsec/1000000u;
    if(community_copchase_bust_waiting){
        const CopChaseSessionFrame *pending=&community_copchase_bust_result;CopChaseSessionStatus records;
        copchase_session_snapshot(copchase_runtime_session(),&records);
        if(pending->profile_generation!=community_copchase_profile_generation ||
           pending->world_generation!=community_copchase_world_generation || pending->player_generation!=community_copchase_player_generation)
            community_copchase_bust_waiting=0;
        else if(!records.blocked && !records.queued && records.session_high && records.session_low &&
            records.profile_generation==pending->profile_generation && !strcmp(records.model_id,pending->model_id) &&
            records.records.last.event==COPCHASE_BUSTED && records.records.last.session_high==records.session_high &&
            records.records.last.session_low==records.session_low){
            enum CopChaseBustError result=copchase_bust_arm(community_copchase_bust,records.session_high,records.session_low,
                pending->result.infractions,now);
            community_copchase_bust_waiting=0;
            if(result!=COPCHASE_BUST_OK)fprintf(stderr,"[CopChase bust] result could not open (%d)\n",result);
        }
    }
    copchase_bust_step(community_copchase_bust,now);
    CopChaseBustStatus status;copchase_bust_snapshot(community_copchase_bust,&status);
    uint64_t high=0,low=0;if(quest_xr_copchase_bust_confirm(&high,&low)){
        enum CopChaseBustError result=status.failure_visible?
            copchase_bust_dismiss(community_copchase_bust,high,low,now):
            copchase_bust_confirm(community_copchase_bust,high,low,now);
        fprintf(stderr,"[CopChase bust] explicit %s result=%d\n",status.failure_visible?"dismissal":"payment",result);
    }
    copchase_bust_snapshot(community_copchase_bust,&status);
    QuestCopChaseBust ui={0};ui.token_high=status.token_high;ui.token_low=status.token_low;
    ui.active=(status.owned_pause && status.ready) || status.failure_visible;ui.ready=status.ready;ui.paid=status.paid;
    ui.failed=status.failure_visible;ui.uncertain=status.error==COPCHASE_BUST_PAYMENT_UNCERTAIN;
    ui.error=status.error==COPCHASE_BUST_PAYMENT_IO?1:status.error?2:0;
    ui.fine=status.fine;ui.charged=status.charged;ui.bounty=community_copchase_bust_result.result.bounty;
    ui.seconds=(uint32_t)(community_copchase_bust_result.result.elapsed_ms/1000);
    ui.infractions=community_copchase_bust_result.result.infractions;
    quest_xr_copchase_bust_publish(&ui);
}
static void community_copchase_world_ending(void)
{
    community_copchase_music=0;
    ++community_copchase_world_generation;++community_copchase_player_generation;
    community_copchase_player_car=0;
    QuestCopChaseScene scene={0};quest_xr_copchase_scene_publish(&scene);
    copchase_game_world_ending(community_copchase_game);
}
static void community_copchase_frame(void)
{
    if(community_copchase_game_probe<0){
        const char *probe=getenv("NFSU2_COPCHASE_GAME_PROBE"),*script=getenv("RECOMP_PAD_SCRIPT");
        community_copchase_game_probe=probe && !strcmp(probe,"1") && script && *script;
    }
    int values[5];quest_xr_copchase_options(values);
    int enabled=values[0] && quest_xr_copchase_at_start();
    if(community_copchase_game_probe){enabled=1;values[1]=1;values[2]=1;values[3]=100;values[4]=10;}
    if(community_copchase_probe_enabled())enabled=0; /* Exclusive QA owner. */
    if(!community_copchase_game){
        if(!enabled || !copchase_assets_ready())return;
        const CopChaseGameHost host={NULL,community_copchase_read,community_copchase_write,
            community_copchase_call,community_copchase_alloc,community_copchase_free,community_copchase_guards,community_copchase_write_batch};
        community_copchase_game=copchase_game_create(&host);if(!community_copchase_game)return;
    }
    uint32_t player=MEM32(0x44940c),car=xyz_xbox_address(player,0x14)?MEM32(player+4):0;
    if(car!=community_copchase_player_car){++community_copchase_player_generation;community_copchase_player_car=car;}
    CopChaseSessionFrame records_frame;CopChaseSessionStatus records;
    int records_ready=community_copchase_records_before(car,&records_frame,&records);
    community_copchase_settlement_pending=copchase_record_flow_holding(community_copchase_records_flow);
    CopChaseGameConfig config=copchase_game_defaults();config.pursuit.enabled=enabled;
    config.pursuit.style=values[1];config.pursuit.start_heat=values[2];
    config.aggression_percent=values[3];config.maximum_vehicles=values[4];
    config.retained_heat=records_ready?records.retained_heat:0;
    config.spikes_visible=quest_xr_copchase_scene_ready();
    if(!records_ready && !community_copchase_settlement_pending)config.pursuit.enabled=0;
    copchase_game_configure(community_copchase_game,&config);
    copchase_game_frame(community_copchase_game,MEMF(0x428a0c));
    CopChaseGameStatus status;copchase_game_snapshot(community_copchase_game,&status,1);
    community_copchase_records_after(&records_frame,&status,values[1]);
    community_copchase_bust_frame(&status,&records_frame);
    QuestCopChaseScene scene={0};scene.enabled=enabled && status.blocked!=COPCHASE_GAME_CONTEXT && status.blocked!=COPCHASE_GAME_IDENTITY;
    scene.active=scene.enabled && status.spike_strip.active;
    memcpy(scene.position,status.spike_strip.position,sizeof scene.position);memcpy(scene.forward,status.spike_strip.forward,sizeof scene.forward);
    memcpy(scene.left,status.spike_strip.left,sizeof scene.left);scene.half_width=status.spike_strip.half_width;
    quest_xr_copchase_scene_publish(&scene);
    QuestCopChaseHUD hud={0};
    if(enabled && status.blocked!=COPCHASE_GAME_CONTEXT && status.blocked!=COPCHASE_GAME_IDENTITY){
        hud.phase=status.pursuit.phase;hud.heat=status.pursuit.heat;hud.cops=(int)status.active_cars;
        hud.busted=status.pursuit.busted;hud.cooldown=status.pursuit.cooldown;hud.bounty=status.pursuit.bounty;
        hud.seconds=(uint32_t)fmin(4294967295.,fmax(0.,status.pursuit.pursuit_seconds));
        if(status.pending_events&COPCHASE_EVADED)hud.notice=1;
        if(status.pending_events&COPCHASE_BUSTED)hud.notice=2;
    }
    quest_xr_copchase_publish(&hud);
    CopChaseAudioStatus audio={0};audio.generation=community_copchase_world_generation;
    audio.enabled=enabled;audio.running=MEM32(0x428e34)==6 && status.blocked!=COPCHASE_GAME_CONTEXT &&
        status.blocked!=COPCHASE_GAME_IDENTITY && status.blocked!=COPCHASE_GAME_FAULT;
    audio.paused=status.blocked==COPCHASE_GAME_PAUSED || status.blocked==COPCHASE_GAME_STREAMING || status.blocked==COPCHASE_GAME_SETTLEMENT;
    audio.phase=status.pursuit.phase;audio.heat=status.pursuit.heat;audio.events=status.pending_events;
    static unsigned previous_flat;static uint64_t previous_world,previous_player,last_spike_radio;
    if(previous_world!=community_copchase_world_generation || previous_player!=community_copchase_player_generation){
        previous_world=community_copchase_world_generation;previous_player=community_copchase_player_generation;previous_flat=0;last_spike_radio=0;
    }
    for(unsigned wheel=0;wheel<4;++wheel){
        audio.flat_tyres+=(status.flat_tyre_mask>>wheel)&1u;
        audio.new_flat_tyres+=((status.flat_tyre_mask&~previous_flat)>>wheel)&1u;
    }
    previous_flat=status.flat_tyre_mask;audio.speed_mps=status.filtered_speed;audio.rim_sound=1;audio.rim_volume_percent=60;
    audio.master_volume=MEMF(0x406ce8);
    float music_volume=MEMF(0x406cf4);
    audio.music_volume=isfinite(music_volume) && music_volume>=0 && music_volume<=1?.25f*music_volume:0;
    audio.music=copchase_audio_music_available();
    community_copchase_music=audio.enabled && audio.running && audio.music && audio.phase!=COPCHASE_IDLE;
    copchase_audio_publish(&audio);
    if(status.pending_road_events&COPCHASE_ROADBLOCK_DEPLOYED)copchase_audio_event(status.spike_strip.active?"spikes":"roadblock",2,NULL);
    if(status.pending_road_events&COPCHASE_SPIKES_APPROACHING)copchase_audio_event("spikenear",2,NULL);
    if(status.pending_road_events&COPCHASE_TIRES_SPIKED){
        struct timespec clock;clock_gettime(CLOCK_MONOTONIC,&clock);
        uint64_t now=(uint64_t)clock.tv_sec*1000u+(uint64_t)clock.tv_nsec/1000000u;
        if(!last_spike_radio || now-last_spike_radio>5000){copchase_audio_event("spikehit",2,NULL);last_spike_radio=now;}
    }
    if(community_copchase_game_probe){
        static unsigned frame;
        if((frame++%120)==0 || status.pending_events || status.pending_road_events || status.pending_incident_events){
            fprintf(stderr,"[CopChase game] block=%d phase=%d heat=%d cops=%u active=%u visible=%u unknown=%u speed=%.2f acts=%llu releases=%llu failures=%llu los=%llu driver=%llu events=%x road=%x incident=%x infractions=%u damaged=%u takenout=%u cost=%u collisions=%llu records=%d queued=%u\n",
                status.blocked,status.pursuit.phase,status.pursuit.heat,status.registered_cars,status.active_cars,
                status.visible_cars,status.unknown_cars,status.filtered_speed,
                (unsigned long long)status.activations,(unsigned long long)status.releases,
                (unsigned long long)status.failures,(unsigned long long)status.los_queries,
                (unsigned long long)status.driver_overrides,status.pending_events,status.pending_road_events,
                status.pending_incident_events,status.infractions,status.cops_damaged,status.cops_taken_out,status.cost_to_state,
                (unsigned long long)status.collision_events,records_ready,records.queued);
        }
    }
}
