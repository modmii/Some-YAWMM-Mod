/*
 * Most of this code was adapted from Priiloader.
 * https://github.com/DacoTaco/priiloader/blob/master/tools/DacosLove/source/Input.cpp
 */

#include <stdlib.h>
#include <unistd.h>
#include <ogc/lwp.h>
#include <wiiuse/wpad.h>

#include "wkb.h"

enum
{
    /* Configuration i guess */
    WKB_THREAD_PRIORITY = 0x7F,
    WKB_THREAD_STACK    = 0x4000,
    WKB_THREAD_UDELAY   = 400,
    WKB_ANIMATION_UDELAY= 250000,

    /* The keys themselves */
    WKB_ARROW_UP        = 0x52,
    WKB_ARROW_DOWN      = 0x51,
    WKB_ARROW_LEFT      = 0x50,
    WKB_ARROW_RIGHT     = 0x4F,

    WKB_SPACEBAR        = 0x2C,
    WKB_ENTER           = 0x28,
    WKB_NUMPAD_ENTER    = 0x58,
    WKB_BACKSPACE       = 0x2A,
    WKB_DELETE          = 0x4C,
    WKB_ESCAPE          = 0x29,
    WKB_HOME            = 0x4A,

    WKB_KEY_PLUS        = 0x2E,
    WKB_KEY_MINUS       = 0x2D,

    WKB_KEY_X           = 0x1B,
    WKB_KEY_Y           = 0x1C,
    WKB_KEY_1           = 0x1E,
    WKB_KEY_2           = 0x1F,
};

static lwp_t WKBThreadHandle = LWP_THREAD_NULL;
static volatile bool WKBThreadActive;
static u16 WKBButtonsPressed;

static void WKBEventHandler(USBKeyboard_event evt)
{
    // OSReport("Keyboard event: {%i, 0x%02hhx}", evt.type, evt.keyCode);

    if (!(evt.type == USBKEYBOARD_PRESSED || evt.type == USBKEYBOARD_RELEASED))
        return;

    u16 button;
    switch (evt.keyCode)
    {
        /*
         * Maybe I should create an array with a map of keycodes to the corresponding WPAD buttons.
         * Like there's just this u16 WKBKeyMap[0x100] somewhere here and I can just say button = WKBKeyMap[evt.keyCode];
         */
        case WKB_ENTER:
        case WKB_NUMPAD_ENTER:  button = WPAD_BUTTON_A; break;
        case WKB_BACKSPACE:     button = WPAD_BUTTON_B; break;

        case WKB_ARROW_UP:      button = WPAD_BUTTON_UP; break;
        case WKB_ARROW_DOWN:    button = WPAD_BUTTON_DOWN; break;
        case WKB_ARROW_LEFT:    button = WPAD_BUTTON_LEFT; break;
        case WKB_ARROW_RIGHT:   button = WPAD_BUTTON_RIGHT; break;

        case WKB_ESCAPE:
        case WKB_HOME:          button = WPAD_BUTTON_HOME; break;
        case WKB_KEY_PLUS:      button = WPAD_BUTTON_PLUS; break;
        case WKB_KEY_MINUS:     button = WPAD_BUTTON_MINUS; break;
        case WKB_KEY_1:         button = WPAD_BUTTON_1; break;

        default: return;
    }

    if (evt.type == USBKEYBOARD_PRESSED)
        WKBButtonsPressed |= button;
    else
        WKBButtonsPressed &= ~button;
}

static void* WKBThread(__attribute__((unused)) void* arg)
{
    while (WKBThreadActive)
    {
        /*
         * Despite having a return type of s32, USBKeyboard_Open() returns 1 if it was successful, rather than 0.
         * So this statement right here will check if a USB keyboard was detected, and if not, try open it again.
         */
        if (!USBKeyboard_IsConnected() && USBKeyboard_Open(WKBEventHandler) == true)
        {
            /*
             * And once it does open it successfully:
             *
             * "wake up the keyboard by sending it a command.
             * im looking at you, bastard LINQ keyboard."
             * - https://github.com/DacoTaco/priiloader/blob/master/tools/DacosLove/source/Input.cpp#L93-L94
             *
             * Just thought i would make this a fun lil animation :)
             */
            for (int led = 0; led < 3; led++) { USBKeyboard_SetLed(led, true); usleep(WKB_ANIMATION_UDELAY); }
        }

        USBKeyboard_Scan();
        usleep(WKB_THREAD_UDELAY);
    }

//  for (int led = 3; led; led--) { USBKeyboard_SetLed(led, false); usleep(WKB_ANIMATION_UDELAY); }
    return NULL;
}

void WKB_Initialize(void)
{
    USB_Initialize();
    USBKeyboard_Initialize();

    WKBThreadActive = true;
    LWP_CreateThread(&WKBThreadHandle, WKBThread, NULL, NULL, WKB_THREAD_STACK, WKB_THREAD_PRIORITY);
    atexit(WKB_Deinitialize);
}

void WKB_Deinitialize(void)
{
    WKBThreadActive = false;
    usleep(WKB_THREAD_UDELAY);

    USBKeyboard_Close();
    USBKeyboard_Deinitialize();

    if (WKBThreadHandle != LWP_THREAD_NULL)
        LWP_JoinThread(WKBThreadHandle, NULL);

    WKBThreadHandle = LWP_THREAD_NULL;
}

u16 WKB_GetButtons(void)
{
    u16 buttons = WKBButtonsPressed;
    WKBButtonsPressed = 0;

    return buttons;
}



