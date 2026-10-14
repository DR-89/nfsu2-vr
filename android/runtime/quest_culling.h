#pragma once
#include <math.h>
#include <string.h>

/* Matrices are column-major XR clip transforms, depth 0..w. Double precision
 * matters when unprojecting the far corners (near=.5, far=10000). */
static inline int quest_culling_inverse(double out[4][4], const float m[16])
{
    double a[4][8];
    for (int r=0;r<4;++r) for (int c=0;c<8;++c)
        a[r][c] = c<4 ? m[c*4+r] : (c-4==r);
    for (int c=0;c<4;++c) {
        int pivot=c;
        for (int r=c+1;r<4;++r) if (fabs(a[r][c])>fabs(a[pivot][c])) pivot=r;
        if (!isfinite(a[pivot][c]) || fabs(a[pivot][c])<1e-12) return 0;
        for (int k=0;k<8;++k) { double t=a[c][k]; a[c][k]=a[pivot][k]; a[pivot][k]=t; }
        double d=a[c][c];
        for (int k=0;k<8;++k) a[c][k]/=d;
        for (int r=0;r<4;++r) if (r!=c) {
            d=a[r][c];
            for (int k=0;k<8;++k) a[r][k]-=d*a[c][k];
        }
    }
    for (int r=0;r<4;++r) for (int c=0;c<4;++c) {
        out[r][c]=a[r][c+4];
        if (!isfinite(out[r][c])) return 0;
    }
    return 1;
}

/* Enclose both expanded eye frusta with six planes. Starting with the left
 * eye's planes and moving each outward to include every right-eye corner is
 * conservative even with asymmetric/canted views; intersecting the eye planes
 * would incorrectly hide objects visible to just one eye. Output is in the
 * original guest camera space (+Y down, +Z forward). Padding is in game units. */
static inline int quest_culling_stereo(float out[6][4], const float left[16],
                                       const float right[16], float padding)
{
    const double margin=1.35;
    double planes[6][4], inverse[4][4];
    float result[6][4];
    if (!(padding>=0) || !isfinite(padding)) return 0;
    for (int p=0;p<6;++p) {
        for (int c=0;c<4;++c) {
            if (p<4) planes[p][c]=margin*left[c*4+3]+(p&1?-1:1)*left[c*4+p/2];
            else planes[p][c]=p==4 ? left[c*4+2] : (double)left[c*4+3]-left[c*4+2];
        }
        double length=sqrt(planes[p][0]*planes[p][0]+planes[p][1]*planes[p][1]+planes[p][2]*planes[p][2]);
        if (!isfinite(length) || length<1e-12) return 0;
        for (int c=0;c<4;++c) planes[p][c]/=length;
    }
    if (!quest_culling_inverse(inverse,right)) return 0;
    for (int corner=0;corner<8;++corner) {
        double clip[4]={corner&1?margin:-margin,corner&2?margin:-margin,corner&4?1:0,1};
        double point[4]={0};
        for (int r=0;r<4;++r) for (int c=0;c<4;++c) point[r]+=inverse[r][c]*clip[c];
        if (!isfinite(point[3]) || point[3]<=1e-12) return 0;
        for (int c=0;c<3;++c) point[c]/=point[3];
        for (int p=0;p<6;++p) {
            double d=-(planes[p][0]*point[0]+planes[p][1]*point[1]+planes[p][2]*point[2]);
            if (d>planes[p][3]) planes[p][3]=d;
        }
    }
    for (int p=0;p<6;++p) for (int c=0;c<4;++c) {
        result[p][c]=(float)((c==1 || c==2) ? -planes[p][c] : planes[p][c]+(c==3?padding:0));
        if (!isfinite(result[p][c])) return 0;
    }
    memcpy(out,result,sizeof result);
    return 1;
}

/* The retail builder transposes its row-vector view matrix before returning:
 * its final memory layout is row-major world -> guest camera. Planes transform
 * by the transpose of that matrix. Publish only complete finite results. */
static inline int quest_culling_world(float out[6][4], const float camera[6][4],
                                      const float view[16])
{
    float result[6][4];
    for (int p=0;p<6;++p) {
        double plane[4]={0};
        for (int c=0;c<4;++c) for (int r=0;r<4;++r) plane[c]+=(double)camera[p][r]*view[r*4+c];
        double length=sqrt(plane[0]*plane[0]+plane[1]*plane[1]+plane[2]*plane[2]);
        if (!isfinite(length) || length<1e-12) return 0;
        for (int c=0;c<4;++c) {
            result[p][c]=(float)(plane[c]/length);
            if (!isfinite(result[p][c])) return 0;
        }
    }
    memcpy(out,result,sizeof result);
    return 1;
}

/* A sphere can intersect the stereo frustum even when its centre is behind
 * the original, untracked camera. Use the same world planes as the box test
 * when rescuing that case from a subsequent retail hemisphere rejection.
 * Invalid inputs must not authorize the rescue. */
static inline int quest_culling_sphere(const float planes[6][4],
                                       const float point[3], float radius)
{
    if (!isfinite(radius) || radius < 0) return 0;
    for (int c=0;c<3;++c) if (!isfinite(point[c])) return 0;
    for (int p=0;p<6;++p) {
        double distance=planes[p][3], length2=0;
        for (int c=0;c<3;++c) {
            distance+=(double)planes[p][c]*point[c];
            length2+=(double)planes[p][c]*planes[p][c];
        }
        if (!isfinite(distance) || !isfinite(length2) || length2<1e-12) return 0;
        if (distance < -(double)radius*sqrt(length2)) return 0;
    }
    return 1;
}
