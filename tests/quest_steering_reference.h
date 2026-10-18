#pragma once
#include <math.h>
#include <stdint.h>
/* Reference for the supported Xbox executable's left axis conversion:
 * 0x001C2350..0x001C23C1; float constants at 0x35CA70/6C/68.
 * The x87 path rounds normalized input to float before removing the deadzone.
 * Keep this independent of the production inverse to test the game-facing
 * response, rather than merely comparing two encoders. */
static float retail_steering(int16_t axis)
{
    float normalized=(float)(((double)axis+.5)*3.051804378628731e-5);
    double response=(fabs(normalized)-0.23999999463558197)*1.3157894611358643;
    if(response<0)response=0;
    return (float)(normalized<0?-response:response);
}
