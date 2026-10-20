#ifndef QUEST_REAR_CAMERA_H
#define QUEST_REAR_CAMERA_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* US Xbox camera lifetime. The registry is filled by 0x1e18d0 and cleared
 * by 0x1e1f50. Never dereference a cached owner after a race transition. */
typedef int (*QuestRearRead)(void *,uint32_t,void *,size_t);
typedef void (*QuestRearControl)(void *,uint32_t,int);
typedef struct {
    uint32_t manager,car,main_mover,rear_mover;
} QuestRearCamera;
typedef struct {
    QuestRearCamera camera;
    int main_active,rear_valid,rear_linked,rear_empty;
} QuestRearSnapshot;

static int quest_rear_pointer(uint32_t address,uint32_t bytes)
{
    uint32_t low=address&0x7fffffffu;
    return low>=0x10000 && low<=0x04000000u-bytes;
}
static int quest_rear_read32(QuestRearRead read,void *ctx,uint32_t address,uint32_t *out)
{
    return quest_rear_pointer(address,4) && read(ctx,address,out,4);
}
static int quest_rear_snapshot(QuestRearRead read,void *ctx,QuestRearSnapshot *s)
{
    uint32_t phase,count,manager,registered,player,view,rear_view,main_camera,head,vtable,id,camera,car;
    memset(s,0,sizeof *s);
#define REAR_READ(a,v) do { if(!quest_rear_read32(read,ctx,(a),&(v)))return 0; } while(0)
    REAR_READ(0x00428e34u,phase);
    if(phase!=6)return 0;
    REAR_READ(0x0044941cu,count);
    if(count<1 || count>2)return 0;
    REAR_READ(0x0044940cu,manager); /* Local player 0, not an AI or old owner. */
    if(!quest_rear_pointer(manager,0x248))return 0;
    int found=0;
    for(unsigned i=0;i<count;++i) {
        REAR_READ(0x00449414u+4*i,registered);
        if(registered==manager)found=1;
    }
    if(!found)return 0;
    REAR_READ(manager+0x10,player);REAR_READ(manager+0x18,view);REAR_READ(manager+0x1c,rear_view);
    if(player!=0 || view!=1 || rear_view!=3)return 0;
    s->camera.manager=manager;
    REAR_READ(manager+4,s->camera.car);REAR_READ(manager+0x238,s->camera.main_mover);
    REAR_READ(manager+0x244,s->camera.rear_mover);
    if(!quest_rear_pointer(s->camera.car,0x558) ||
       !quest_rear_pointer(s->camera.main_mover,0x50))return 0;
    REAR_READ(s->camera.main_mover,vtable);REAR_READ(s->camera.main_mover+0x10,id);
    REAR_READ(s->camera.main_mover+0x1c,camera);REAR_READ(s->camera.main_mover+0x2c,car);
    REAR_READ(0x003f5b70u,main_camera);
    if(vtable!=0x00344880u || id!=1 || camera!=main_camera || car!=s->camera.car)return 0;
    REAR_READ(0x003f5b74u,head);
    s->main_active=head==s->camera.main_mover+4;
    REAR_READ(0x003f5c54u,head);
    s->rear_empty=head==0 || head==0x003f5c54u;
    s->rear_linked=s->camera.rear_mover && head==s->camera.rear_mover+4;
    if(s->camera.rear_mover && quest_rear_pointer(s->camera.rear_mover,0x30)) {
        REAR_READ(s->camera.rear_mover,vtable);REAR_READ(s->camera.rear_mover+0x10,id);
        REAR_READ(s->camera.rear_mover+0x1c,camera);REAR_READ(s->camera.rear_mover+0x2c,car);
        s->rear_valid=vtable==0x003446f0u && id==3 && camera==0x003efb20u && car==s->camera.car;
    }
#undef REAR_READ
    return 1;
}
static int quest_rear_same_owner(const QuestRearCamera *a,const QuestRearCamera *b)
{
    return a->manager==b->manager && a->car==b->car && a->main_mover==b->main_mover;
}

/* Reconcile once per guest view update, not only on a camera-button event.
 * Restart/next-race can destroy the mover while cockpit remains selected.
 * Return +1 for a newly attached camera, -1 for our camera detached, 0 otherwise.
 * Existing retail/profile cameras remain owned and managed by the game. */
static int quest_rear_update(QuestRearCamera *owned,QuestRearRead read,void *ctx,
    QuestRearControl control,int selected)
{
    QuestRearSnapshot s;
    if(!quest_rear_snapshot(read,ctx,&s)) {memset(owned,0,sizeof *owned);return 0;}
    if(!quest_rear_same_owner(owned,&s.camera) || owned->rear_mover!=s.camera.rear_mover)
        memset(owned,0,sizeof *owned);
    if(selected && s.main_active && !s.camera.rear_mover && s.rear_empty) {
        control(ctx,s.camera.manager,1);
        QuestRearSnapshot next;
        if(quest_rear_snapshot(read,ctx,&next) && quest_rear_same_owner(&s.camera,&next.camera) &&
           next.rear_valid && next.rear_linked) {
            *owned=next.camera;return 1;
        }
    } else if(!selected && owned->rear_mover && s.rear_valid && s.rear_linked) {
        control(ctx,s.camera.manager,0);
        memset(owned,0,sizeof *owned);return -1;
    }
    return 0;
}
#endif
