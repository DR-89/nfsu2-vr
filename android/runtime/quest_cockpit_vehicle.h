#pragma once
#include <string.h>
#include <math.h>
#include "quest_cockpit_fit.h"

/* Stock-car fit: measured driver position, windshield and body width, calibrated
 * to the accepted Peugeot cabin. No retail mesh or texture is embedded here.
 * Profiles are immutable; personal height/distance remain XR eye offsets. */
typedef struct {
    const char *name;
    float seat[3];
    float near_width,far_width,front_depth,side,rear_limit;
} QuestCockpitVehicle;
static const QuestCockpitVehicle quest_cockpit_vehicles[]={
    {"DEFAULT",{-.10f,.35f,.95f},1,1,1,1,20},
    {"240SX",{0.028710f,0.387445f,0.783809f},0.895846f,1.024196f,0.745138f,1.000000f,20.000000f},
    {"3000GT",{-0.150781f,0.394046f,0.862829f},1.097463f,1.114717f,0.802961f,1.000000f,20.000000f},
    {"350Z",{-0.243177f,0.435649f,0.897662f},0.965195f,1.135109f,0.924898f,1.000000f,0.460000f},
    {"A3",{-0.065582f,-0.431989f,0.975869f},0.932355f,1.115876f,0.863236f,-1.000000f,20.000000f},
    {"CELICA",{-0.116100f,0.447042f,0.869052f},0.862711f,1.114711f,1.040088f,1.000000f,20.000000f},
    {"CIVIC",{0.026244f,0.386362f,0.890257f},0.958497f,1.046880f,0.801929f,1.000000f,20.000000f},
    {"COROLLA",{0.069241f,0.393654f,1.060295f},0.868617f,1.024301f,0.683867f,1.000000f,20.000000f},
    {"ECLIPSE",{-0.066512f,0.447042f,0.858775f},0.829851f,1.101826f,0.949490f,1.000000f,20.000000f},
    {"ESCALADE",{0.311899f,0.459092f,1.123207f},1.163498f,1.253583f,0.908591f,1.000000f,20.000000f},
    {"FOCUS",{-0.123566f,0.426007f,0.971427f},0.867776f,1.080164f,1.074866f,1.000000f,20.000000f},
    {"G35",{0.028441f,0.435649f,0.961768f},0.995057f,1.146818f,0.827482f,1.000000f,20.000000f},
    {"GOLF",{-0.101768f,0.426007f,0.917057f},0.912806f,1.097821f,0.895367f,1.000000f,20.000000f},
    {"GTO",{0.093413f,0.449788f,0.964516f},0.970994f,1.161941f,0.811940f,1.000000f,20.000000f},
    {"HUMMER",{0.194274f,0.475757f,1.143913f},1.158726f,1.280656f,0.760271f,1.000000f,20.000000f},
    {"IMPREZAWRX",{0.071930f,0.428578f,0.926136f},0.912432f,1.102139f,0.762547f,1.000000f,20.000000f},
    {"IS300",{0.211067f,0.442142f,0.899061f},0.876741f,1.111703f,0.717818f,1.000000f,20.000000f},
    {"LANCEREVO8",{0.105100f,0.377847f,0.839308f},1.051033f,1.068376f,0.860278f,1.000000f,20.000000f},
    {"MIATA",{-0.306802f,0.392122f,0.811262f},0.929337f,1.045450f,0.627145f,1.000000f,0.460000f},
    {"MUSTANGGT",{-0.125446f,0.449410f,0.969603f},1.002855f,1.173777f,0.802790f,1.000000f,20.000000f},
    {"NAVIGATOR",{0.313362f,0.461932f,1.404233f},1.168263f,1.260383f,0.820347f,1.000000f,20.000000f},
    {"PEUGOT",{-0.100000f,0.350000f,0.950000f},1.000000f,1.000000f,1.000000f,1.000000f,20.000000f},
    {"RSX",{-0.053459f,0.396328f,0.917057f},0.971780f,1.069398f,0.948470f,1.000000f,20.000000f},
    {"RX7",{-0.275228f,0.377847f,0.803363f},1.032820f,1.061234f,0.805931f,1.000000f,20.000000f},
    {"RX8",{-0.021926f,0.426007f,0.917057f},0.950987f,1.112792f,0.717152f,1.000000f,20.000000f},
    {"SENTRA",{0.108678f,0.472644f,0.931158f},0.768338f,1.122172f,0.745197f,1.000000f,20.000000f},
    {"SKYLINE",{0.034015f,-0.424885f,0.908227f},0.994384f,1.127860f,0.701633f,-1.000000f,20.000000f},
    {"SUPRA",{-0.176109f,0.501648f,0.869052f},0.859497f,1.208291f,0.811797f,1.000000f,20.000000f},
    {"TIBURON",{-0.106063f,0.383905f,0.895097f},1.056506f,1.081044f,0.881364f,1.000000f,20.000000f},
    {"TT",{-0.233893f,0.429573f,0.865928f},0.917311f,1.105781f,0.966617f,1.000000f,20.000000f},
};
enum { QUEST_COCKPIT_VEHICLES=sizeof quest_cockpit_vehicles/sizeof quest_cockpit_vehicles[0] };
static inline int quest_cockpit_vehicle_index(const char *name)
{
    if(name)for(int i=1;i<QUEST_COCKPIT_VEHICLES;++i)if(!strcmp(name,quest_cockpit_vehicles[i].name))return i;
    return 0;
}
static inline const QuestCockpitVehicle *quest_cockpit_vehicle(int index)
{ return &quest_cockpit_vehicles[index>0 && index<QUEST_COCKPIT_VEHICLES?index:0]; }

/* Static cabin and dashboard only: the wheel/hands keep their physical size.
 * The dash is fitted beyond the wheel plane. Mirrors stay on original glass. */
static inline void quest_cockpit_fit_point(float out[3],const float p[3],const QuestCockpitVehicle *fit)
{
    out[0]=p[0]*(p[0]<0?fit->near_width:fit->far_width)*fit->side;
    out[1]=p[1];
    out[2]=p[2]<-.33f?-.33f+(p[2]+.33f)*fit->front_depth:p[2];
}
