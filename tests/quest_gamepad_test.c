#define SDL_MAIN_HANDLED
#include "../android/runtime/quest_gamepad.h"
#include <stdio.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s (%s)\n",__LINE__,#x,SDL_GetError());return 1;}}while(0)
int main(void)
{
    SDL_SetMainReady();SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    CHECK(!SDL_Init(SDL_INIT_GAMECONTROLLER));
    int index=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,6,15,0);CHECK(index>=0);
    SDL_GameControllerUpdate();CHECK(SDL_IsGameController(index));
    SDL_GameController *pad=SDL_GameControllerOpen(index);CHECK(pad);
    SDL_Joystick *joy=SDL_GameControllerGetJoystick(pad);CHECK(joy);
    uint16_t buttons;uint8_t analog[8];int16_t sticks[4];int clicks[2];
    CHECK(!SDL_JoystickSetVirtualAxis(joy,SDL_CONTROLLER_AXIS_TRIGGERLEFT,-32768));
    CHECK(!SDL_JoystickSetVirtualAxis(joy,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,-32768));
    SDL_GameControllerUpdate();CHECK(quest_gamepad_sample(pad,&buttons,analog,sticks,clicks)==1);
    for(int button=0;button<4;++button) {
        SDL_JoystickSetVirtualButton(joy,button,1);SDL_GameControllerUpdate();
        CHECK(quest_gamepad_sample(pad,&buttons,analog,sticks,clicks)==2 && analog[button]==255);
        SDL_JoystickSetVirtualButton(joy,button,0);
    }
    SDL_JoystickSetVirtualAxis(joy,SDL_CONTROLLER_AXIS_LEFTX,-32768);
    SDL_JoystickSetVirtualAxis(joy,SDL_CONTROLLER_AXIS_RIGHTY,-32768);
    SDL_JoystickSetVirtualAxis(joy,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,32767);
    SDL_JoystickSetVirtualButton(joy,SDL_CONTROLLER_BUTTON_LEFTSTICK,1);
    SDL_JoystickSetVirtualButton(joy,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);
    SDL_GameControllerUpdate();quest_gamepad_sample(pad,&buttons,analog,sticks,clicks);
    CHECK(sticks[0]==-32768 && sticks[3]==32767 && analog[7]==255 && analog[6]==0);
    CHECK(clicks[0] && !clicks[1] && buttons==8);
    SDL_JoystickSetVirtualAxis(joy,SDL_CONTROLLER_AXIS_LEFTX,4000);
    SDL_GameControllerUpdate();quest_gamepad_sample(pad,&buttons,analog,sticks,clicks);CHECK(sticks[0]==0);
    CHECK(!SDL_JoystickDetachVirtual(index));SDL_GameControllerUpdate();CHECK(!SDL_GameControllerGetAttached(pad));
    SDL_GameControllerClose(pad);SDL_Quit();
    puts("SDL virtual gamepad: hotplug, face buttons, clicks, D-pad, triggers, signed axes and deadzone passed.");return 0;
}
