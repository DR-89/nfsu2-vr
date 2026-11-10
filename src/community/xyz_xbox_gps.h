#ifndef XYZ_XBOX_GPS_H
#define XYZ_XBOX_GPS_H
#include "xyz_xbox_vehicle.h"
#include <math.h>
/* Retail GPS::GetState 111DE0, destination projection 111DA0, and
 * Speedometer Units controller B5140/B51A0. Read only on the guest thread.
 * The caller supplies the validated local car's world position. */
static inline int xyz_xbox_gps(XyzXboxRead read,void *ctx,const float position[3],float *metres,int *metric)
{
    uint32_t flow,race,search,path;uint8_t enabled,units;float target[2];
    *metres=-1;*metric=0;
    if(!read || !position || !read(ctx,0x428e34,&flow,4) || flow!=6 ||
       !read(ctx,0x3cc470,&race,4) || race ||
       !read(ctx,0x3fbdd9,&enabled,1) || enabled!=1 ||
       !read(ctx,0x3d8b84,&search,4) || (search!=1 && search!=2) ||
       !read(ctx,0x3d8b88,&path,4) || path==3 || path==4 ||
       !read(ctx,0x3fbdd0,target,sizeof target) ||
       !read(ctx,0x406cda,&units,1) || units>1)return 0;
    for(unsigned i=0;i<2;++i)
        if(!isfinite(position[i]) || !isfinite(target[i]) || fabsf(position[i])>20000 || fabsf(target[i])>20000)return 0;
    float distance=hypotf(target[0]-position[0],target[1]-position[1]);
    if(!isfinite(distance) || distance>=1000000)return 0;
    *metres=distance;*metric=units;return 1;
}
#endif
