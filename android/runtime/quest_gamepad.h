#pragma once
#include <SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* Read a mapped SDL pad into the Xbox packet shared by both VR platforms.
 * Call SDL_GameControllerUpdate before sampling. Y is positive upward. */
static inline int quest_gamepad_sample(SDL_GameController *controller,
    uint16_t *buttons,uint8_t analog[8],int16_t sticks[4],int clicks[2])
{
    *buttons=0;memset(analog,0,8);memset(sticks,0,8);
    const SDL_GameControllerButton digital[]={SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,SDL_CONTROLLER_BUTTON_START,SDL_CONTROLLER_BUTTON_BACK};
    for(int i=0;i<6;++i)if(SDL_GameControllerGetButton(controller,digital[i]))*buttons|=1u<<i;
    const SDL_GameControllerButton face[]={SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_B,SDL_CONTROLLER_BUTTON_X,SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
    for(int i=0;i<6;++i)analog[i]=SDL_GameControllerGetButton(controller,face[i])?255:0;
    for(int i=0;i<2;++i) {
        int t=SDL_GameControllerGetAxis(controller,(SDL_GameControllerAxis)(SDL_CONTROLLER_AXIS_TRIGGERLEFT+i));
        analog[6+i]=(uint8_t)((t>0?t:0)*255/32767);
        clicks[i]=SDL_GameControllerGetButton(controller,i?SDL_CONTROLLER_BUTTON_RIGHTSTICK:SDL_CONTROLLER_BUTTON_LEFTSTICK);
    }
    for(int i=0;i<4;++i) {
        int v=SDL_GameControllerGetAxis(controller,(SDL_GameControllerAxis)i);
        if(i&1)v=-v;if(v>32767)v=32767;
        sticks[i]=abs(v)<5000?0:(int16_t)v;
    }
    int used=*buttons||clicks[0]||clicks[1];
    for(int i=0;i<8;++i)used|=analog[i]>20;
    for(int i=0;i<4;++i)used|=sticks[i]!=0;
    return used?2:1;
}
