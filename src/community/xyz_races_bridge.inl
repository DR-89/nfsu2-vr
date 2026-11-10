/* Production controller at the guest main-loop boundary. Render requests
 * contain copies; only this thread touches the original game. */
#include "xyz_races_api.h"
#include "xyz_auth.h"
static int community_races_controller(void)
{
    const XyzXboxGuest *m=community_engine_guest.memory;
    if(!m || !m->read)return 0;
    for(unsigned port=0;port<4;++port){uint8_t device;
        if(m->read(m->context,0x427ff8+port,&device,1) && device!=UINT8_MAX &&
           xyz_xbox_lobby_select_controller(&community_engine_guest,&community_engine_lobby,port))return 1;
    }
    return 0;
}
static int community_races_info(const XyzLobbyGame *game,XyzRaceInfo *info)
{
    const XyzXboxGuest *m=community_engine_guest.memory;memset(info,0,sizeof *info);
    return m && xyz_xbox_race_info(m->read,m->context,game,info);
}
static int community_races_tracks(XYZRaceBrowser *view)
{
    const XyzXboxGuest *m=community_engine_guest.memory;
    const unsigned modes[]={0,1,2,3,4,10};view->track_count=0;
    if(!m || !m->read)return 0;
    for(unsigned mode=0;mode<6;++mode){
        XyzRaceTrack tracks[XYZ_TRACK_LIMIT];unsigned count=0;
        if(!xyz_xbox_tracks(m->read,m->context,modes[mode],tracks,XYZ_TRACK_LIMIT,&count))goto bad;
        for(unsigned i=0;i<count;++i){XyzRaceRules rules;
            if(!xyz_xbox_track_rules(modes[mode],&tracks[i],&rules))goto bad;
            if(view->track_count>=XYZ_RACES_TRACK_CHOICES)goto bad;
            XYZRaceChoice *choice=&view->tracks[view->track_count++];memset(choice,0,sizeof *choice);
            choice->mode=modes[mode];choice->id=tracks[i].id;choice->reverse=tracks[i].reverse;
            choice->min_laps=rules.min_laps;choice->max_laps=rules.max_laps;choice->max_players=rules.max_players;
            if(!xyz_xbox_track_name(m->read,m->context,choice->id,0,choice->name,sizeof choice->name))
                snprintf(choice->name,sizeof choice->name,"TRACK %u",choice->id);
            if(!choice->reverse || !xyz_xbox_track_name(m->read,m->context,choice->id,1,choice->reverse_name,sizeof choice->reverse_name))
                memcpy(choice->reverse_name,choice->name,sizeof choice->name);
        }
    }return 1;
bad:
    memset(view->tracks,0,sizeof view->tracks);view->track_count=0;return 0;
}
static const char *community_races_rejection(uint32_t code)
{
    switch(code){
    case 0x66756c6c: return "The lobby is full or the server is busy. Refresh and try again.";
    case 0x70617373: case 0x62706173: return "The lobby password was rejected.";
    case 0x7567616d: return "This lobby is no longer available. Refresh the list.";
    default: return "The server rejected the request. Refresh the list and try again.";
    }
}
static void community_races_frame(const XyzXboxFrame *frame,int main_menu,int paused,uint64_t now)
{
    static XYZRaceBrowser view;
    static XyzRaceSettingsLease settings;
    static int owned,searched,closing,saw_world,returning;
    static uint64_t deadline;
    static int catalog_loaded;
    static char close_notice[128];
    static XyzXboxCareer career;
    static XYZRaceRequest deferred;
    static uint64_t transfer_log;
    XYZRaceRequest request;int command=xyz_races_take(&request);
    const char *failure=NULL;
    if(command && request.action==XYZ_RACES_OPEN){
        memset(&view,0,sizeof view);view.epoch=request.epoch;close_notice[0]=0;
        memcpy(view.name,request.name,sizeof view.name);
        if(owned || community_engine_lobby.stage!=XYZ_LOBBY_EMPTY){failure="Another online game is still active.";goto close;}
        if(!main_menu){
            if(!xyz_xbox_career_begin(&community_career_guest,&career,frame,paused)){
                failure="Pause career free roam or use the main menu to browse races.";goto close;
            }
            deferred=request;xyz_auth_clear(&request,sizeof request);command=0;xyz_online_disconnect();
            closing=0;view.active=1;view.from_career=1;view.state=XYZ_RACES_CONNECTING;
            snprintf(view.notice,sizeof view.notice,"Returning to the menu to browse races. Your career position is kept.");
            fprintf(stderr,"[XYZ races] career browser transfer requested\n");goto publish;
        }
        if(frame->status!=XYZ_XBOX_FRONTEND || frame->flow!=3){failure="Wait until the main menu is ready.";goto close;}
connect:;
        int connected=xyz_online_connect_races_claimed(request.name,request.room,request.profile,request.device,request.claim);
        xyz_auth_clear(request.claim,sizeof request.claim);
        if(!connected){failure="Cannot connect this online profile.";goto close;}
        memset(&community_engine_race,0,sizeof community_engine_race);
        owned=1;searched=closing=saw_world=returning=catalog_loaded=0;deadline=now+45000000ull;
        view.active=1;view.state=XYZ_RACES_CONNECTING;
        fprintf(stderr,"[XYZ races] browser session requested\n");
    }
    if(command && request.action==XYZ_RACES_CLOSE){
        if(!owned)view.epoch=request.epoch;
        closing=1;goto close;
    }
    if(deferred.action){
        XYZOnlineStatus pending_status;xyz_online_snapshot(&pending_status);
        int retired=pending_status.state==XYZ_ONLINE_OFFLINE && xyz_online_fleet_retired();
        if(now>=transfer_log){
            const uint32_t fields[]={0x41c338,0x3fbbf4,0x3fbbfc,0x428e14,0x441b48,0x45c4b4};uint32_t values[6]={0};
            const XyzXboxGuest *memory=community_career_guest.memory;
            for(unsigned i=0;memory && memory->read && i<6;i++)memory->read(memory->context,fields[i],values+i,4);
            fprintf(stderr,"[XYZ races] career transfer stage=%d paused=%d retired=%d flow=%u status=%d mode=%u gates=%u/%u/%u stream=%u cars=%u\n",
                career.stage,paused,retired,frame->flow,frame->status,values[0],values[1],values[2],values[3],values[4],values[5]);
            transfer_log=now+5000000ull;
        }
        if(xyz_xbox_career_step(&community_career_guest,&career,frame,paused,main_menu,retired)){
            request=deferred;xyz_auth_clear(&deferred,sizeof deferred);goto connect;
        }
        goto publish;
    }
    if(closing && career.stage!=XYZ_CAREER_IDLE)goto close;
    if(!owned)goto finish;
    if(closing)goto close;
    if(!community_engine_race.captured && !main_menu){closing=1;goto close;}
    XYZOnlineStatus status;xyz_online_snapshot(&status);
    if(status.state==XYZ_ONLINE_STOPPED || status.state==XYZ_ONLINE_OFFLINE || status.state==XYZ_ONLINE_RETRY){
        failure=status.notice[0]?status.notice:"The race connection ended. Reopen the browser to reconnect.";goto close;
    }
    if(status.state!=XYZ_ONLINE_CONNECTED || !status.lan_ready){
        if(status.state==XYZ_ONLINE_QUEUED){deadline=now+45000000ull;
            snprintf(view.notice,sizeof view.notice,"Queue %u of %u",status.queue_position,status.queue_total);}
        if(now>=deadline){failure="The race server did not become ready.";goto close;}
        goto publish;
    }
    if(community_engine_race.captured || (returning && community_engine_race.stage==XYZ_RACE_CLOSED)){
        if(command && request.epoch==view.epoch && request.action==XYZ_RACES_LEAVE)returning=1;
        if(frame->flow==6 && frame->world && frame->car && frame->status==XYZ_XBOX_OTHER_RACE)saw_world=1;
        if(returning || (saw_world && frame->flow==3 && frame->status==XYZ_XBOX_FRONTEND)){
            view.state=XYZ_RACES_RETURNING;
            xyz_xbox_race_return(&community_race_guest,&community_engine_race,frame);
            if(xyz_xbox_race_close(&community_race_guest,&community_engine_race,frame)){
                if(!xyz_xbox_race_settings_release(&community_engine_guest,&settings,frame))goto publish;
                xyz_xbox_lobby_close(&community_engine_guest,&community_engine_lobby);
                memset(&community_engine_race,0,sizeof community_engine_race);
                searched=saw_world=returning=0;view.count=0;view.state=XYZ_RACES_CONNECTING;
                memset(&view.lobby,0,sizeof view.lobby);view.host=view.local_ready=view.startable=0;
                deadline=now+45000000ull;
                fprintf(stderr,"[XYZ races] race returned; original coordinator restored\n");
            }
            goto publish;
        }
        if(!saw_world && now>=deadline){failure="The race did not start. Returning to the browser.";goto close;}
        xyz_xbox_lobby_tick(&community_engine_guest,&community_engine_lobby,status.generation,now);
        if(community_engine_lobby.stage==XYZ_LOBBY_FAILED){failure="The race session ended.";goto close;}
        view.state=XYZ_RACES_RACING;goto publish;
    }
    if(!main_menu){closing=1;goto close;}
    if(community_engine_lobby.stage==XYZ_LOBBY_EMPTY &&
       !xyz_xbox_lobby_begin(&community_engine_guest,&community_engine_lobby,frame,view.name,status.generation,0x196390,now)){
        failure="The game is not ready to open its race browser.";goto close;
    }
    xyz_xbox_lobby_tick(&community_engine_guest,&community_engine_lobby,status.generation,now);
    if(community_engine_lobby.stage==XYZ_LOBBY_FAILED){failure="The race lobby connection failed. Reopen the browser to retry.";goto close;}
    if(community_engine_lobby.stage==XYZ_LOBBY_READY){
        if(!xyz_xbox_race_settings_release(&community_engine_guest,&settings,frame)){
            failure="Restoring the previous race settings.";goto close;
        }
        if(!catalog_loaded){community_races_tracks(&view);catalog_loaded=1;}
    }
    if(command && request.epoch==view.epoch){
        if(request.action==XYZ_RACES_CREATE && community_engine_lobby.stage==XYZ_LOBBY_READY){
            if(!xyz_xbox_race_settings_apply(&community_engine_guest,&community_engine_lobby,&settings,frame,&request.options) ||
               !xyz_xbox_lobby_create(&community_engine_guest,&community_engine_lobby,request.options.performance,now)){
                if(!xyz_xbox_race_settings_release(&community_engine_guest,&settings,frame)){
                    failure="Cannot restore the previous race settings.";goto close;
                }
                snprintf(view.notice,sizeof view.notice,"These race options are unavailable. Choose another track or refresh.");
                view.state=XYZ_RACES_READY;
            }else{view.state=XYZ_RACES_JOINING;view.count=0;view.notice[0]=0;}
        }
        if(request.action==XYZ_RACES_JOIN && community_engine_lobby.stage==XYZ_LOBBY_READY){
            XyzLobbyGame current;XyzRaceInfo info;XyzRaceSettings options={0};
            int valid=xyz_xbox_lobby_game(&community_engine_guest,&community_engine_lobby,request.index,&current) &&
                community_races_info(&current,&info) && info.compatible;
            if(valid){
                options.mode=info.mode;options.track=info.track;options.laps=info.laps;options.reverse=info.reverse;
                options.min_players=current.min_players;options.max_players=current.max_players;
                options.collision=current.custom_flags&1;options.catchup=(current.custom_flags>>1)&1;
                options.ranked=(current.system_flags>>18)&1;
            }
            if(!valid ||
               current.custom_flags!=request.selected.custom_flags || strcmp(current.params,request.selected.params) ||
               !xyz_xbox_race_settings_join(&community_engine_guest,&community_engine_lobby,&settings,frame,&options) ||
               !xyz_xbox_lobby_join_game(&community_engine_guest,&community_engine_lobby,request.index,&request.selected,request.password,now)){
                if(!xyz_xbox_race_settings_release(&community_engine_guest,&settings,frame)){
                    failure="Cannot restore the previous race settings.";goto close;
                }
                snprintf(view.notice,sizeof view.notice,"The selected race changed or is unavailable. Refresh and select it again.");
                view.state=XYZ_RACES_READY;
            }else{view.race=info;view.state=XYZ_RACES_JOINING;view.count=0;view.notice[0]=0;}
        }
        if(request.action==XYZ_RACES_SET_READY && community_engine_lobby.stage==XYZ_LOBBY_IN_GAME){
            if((request.ready && !community_races_controller()) ||
               !xyz_xbox_lobby_ready(&community_engine_guest,&community_engine_lobby,request.ready))
                snprintf(view.notice,sizeof view.notice,"Connect a controller before marking yourself ready.");
            else view.notice[0]=0;
        }
        if(request.action==XYZ_RACES_START && community_engine_lobby.stage==XYZ_LOBBY_IN_GAME){
            if(!community_races_controller() || !xyz_xbox_lobby_start(&community_engine_guest,&community_engine_lobby,now))
                snprintf(view.notice,sizeof view.notice,"Wait until the required players are ready.");
            else{view.state=XYZ_RACES_STARTING;view.notice[0]=0;}
        }
        if(request.action==XYZ_RACES_LEAVE && community_engine_lobby.stage==XYZ_LOBBY_IN_GAME){
            if(!xyz_xbox_lobby_leave(&community_engine_guest,&community_engine_lobby,now)){failure="Cannot leave the race lobby.";goto close;}
            view.state=XYZ_RACES_RETURNING;view.notice[0]=0;
        }
    }
    if(community_engine_lobby.stage==XYZ_LOBBY_PLAY_READY){
        XyzLobbyGame current;XyzRaceInfo info;
        if(!xyz_xbox_lobby_current_game(&community_engine_guest,&community_engine_lobby,&current) ||
           !community_races_info(&current,&info) || !info.compatible ||
           !xyz_xbox_race_begin(&community_race_guest,&community_engine_race,&community_engine_lobby,frame)){
            failure="Cannot start this race with the installed track and controller.";goto close;
        }
        view.race=info;view.state=XYZ_RACES_RACING;view.notice[0]=0;saw_world=returning=0;deadline=now+90000000ull;
        fprintf(stderr,"[XYZ races] original race handoff accepted mode=%u track=%u players=%u\n",info.mode,info.track,view.lobby.players);
        goto publish;
    }
    if(community_engine_lobby.stage==XYZ_LOBBY_IN_GAME || community_engine_lobby.stage==XYZ_LOBBY_STARTING){
        XyzLobbyGame current;
        if(!xyz_xbox_lobby_snapshot(&community_engine_guest,&community_engine_lobby,&view.lobby) || view.lobby.players>6 ||
           !xyz_xbox_lobby_current_game(&community_engine_guest,&community_engine_lobby,&current) ||
           !community_races_info(&current,&view.race) || !view.race.compatible){failure="This lobby changed to an unsupported race.";goto close;}
        view.host=view.lobby.players && !strcmp(view.lobby.player[0],view.name);
        view.local_ready=0;view.startable=view.host && view.lobby.players>=current.min_players;
        for(unsigned i=0;i<view.lobby.players;++i){
            int ready=(view.lobby.player_flags[i]&0x08000000u)!=0;
            if(!strcmp(view.lobby.player[i],view.name))view.local_ready=ready;
            else if(!ready)view.startable=0;
        }
        view.state=community_engine_lobby.stage==XYZ_LOBBY_STARTING?XYZ_RACES_STARTING:XYZ_RACES_IN_LOBBY;
        if(community_engine_lobby.error!=XYZ_LOBBY_OK)snprintf(view.notice,sizeof view.notice,"%s",community_races_rejection(community_engine_lobby.reply_code));
        goto publish;
    }
    if(community_engine_lobby.stage==XYZ_LOBBY_READY &&
       (view.state==XYZ_RACES_JOINING || view.state==XYZ_RACES_RETURNING || view.state==XYZ_RACES_IN_LOBBY || view.state==XYZ_RACES_STARTING)){
        searched=0;view.host=view.local_ready=view.startable=0;memset(&view.lobby,0,sizeof view.lobby);
        snprintf(view.notice,sizeof view.notice,"%s",community_engine_lobby.error==XYZ_LOBBY_OK?"Returned to the race list.":community_races_rejection(community_engine_lobby.reply_code));
    }
    if(community_engine_lobby.stage==XYZ_LOBBY_READY && (!searched || (command && request.action==XYZ_RACES_SEARCH && request.epoch==view.epoch))){
        unsigned start=searched?request.start:0;
        if(!xyz_xbox_lobby_search(&community_engine_guest,&community_engine_lobby,start,XYZ_RACES_PAGE_SIZE,now)){
            failure="Cannot refresh the race list.";goto close;
        }
        searched=1;view.start=start;view.count=0;memset(view.rows,0,sizeof view.rows);memset(view.info,0,sizeof view.info);
        view.state=XYZ_RACES_SEARCHING;if(command && request.action==XYZ_RACES_SEARCH)view.notice[0]=0;
    }
    if(community_engine_lobby.stage==XYZ_LOBBY_READY && searched){
        if(community_engine_lobby.error!=XYZ_LOBBY_OK){failure="The server rejected the race search.";goto close;}
        XyzLobbySnapshot lobby;
        if(!xyz_xbox_lobby_snapshot(&community_engine_guest,&community_engine_lobby,&lobby)){failure="Cannot read the race list.";goto close;}
        unsigned count=lobby.listed_games;if(count>XYZ_RACES_PAGE_SIZE)count=XYZ_RACES_PAGE_SIZE;
        for(unsigned i=0;i<count;++i){
            XyzLobbyGame row;
            if(!xyz_xbox_lobby_game(&community_engine_guest,&community_engine_lobby,i,&row)){
                failure="The race list changed. Reopen the browser to refresh it.";goto close;
            }
            if(memcmp(&row,&view.rows[i],sizeof row) || view.state!=XYZ_RACES_READY)community_races_info(&row,&view.info[i]);
            view.rows[i]=row;
        }
        memset(view.rows+count,0,(XYZ_RACES_PAGE_SIZE-count)*sizeof view.rows[0]);
        memset(view.info+count,0,(XYZ_RACES_PAGE_SIZE-count)*sizeof view.info[0]);
        if(view.state!=XYZ_RACES_READY)fprintf(stderr,"[XYZ races] browser ready start=%u rows=%u\n",view.start,count);
        view.count=count;view.state=XYZ_RACES_READY;memcpy(view.name,lobby.persona,sizeof view.name);
    }
publish:
    xyz_races_publish(&view);goto finish;
close:
    if(failure)snprintf(close_notice,sizeof close_notice,"%s",failure);
    closing=1;
    xyz_auth_clear(&deferred,sizeof deferred);
    if(owned && community_engine_race.captured){
        xyz_xbox_race_return(&community_race_guest,&community_engine_race,frame);
        if(!xyz_xbox_race_close(&community_race_guest,&community_engine_race,frame)){
            view.state=XYZ_RACES_CLOSING;
            snprintf(view.notice,sizeof view.notice,"%s",close_notice[0]?close_notice:"Returning from the race before disconnecting.");
            goto publish;
        }
    }
    if(owned){
        if(!xyz_xbox_race_settings_release(&community_engine_guest,&settings,frame)){
            view.state=XYZ_RACES_CLOSING;snprintf(view.notice,sizeof view.notice,"Restoring the previous race settings.");goto publish;
        }
        xyz_xbox_lobby_close(&community_engine_guest,&community_engine_lobby);xyz_online_disconnect();owned=0;
        fprintf(stderr,"[XYZ races] browser closed; names restored, lobby disposed\n");
    }
    if(!xyz_xbox_career_resume(&community_career_guest,&career,frame,main_menu)){
        view.active=1;view.state=XYZ_RACES_CLOSING;
        snprintf(view.notice,sizeof view.notice,"Returning to your career free roam.");goto publish;
    }
    view.active=0;view.state=close_notice[0]?XYZ_RACES_ERROR:XYZ_RACES_OFFLINE;view.count=0;
    memset(view.rows,0,sizeof view.rows);memset(view.info,0,sizeof view.info);memset(&view.lobby,0,sizeof view.lobby);
    view.host=view.local_ready=view.startable=0;
    snprintf(view.notice,sizeof view.notice,"%s",close_notice[0]?close_notice:"Disconnected.");
    xyz_races_publish(&view);close_notice[0]=0;
finish:
    xyz_auth_clear(&request,sizeof request);
}
