#ifndef _WKB_H_
#define _WKB_H_

//#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
//#include <malloc.h>
#include <gctypes.h> // u8, u16, etc...

#include <wiikeyboard/keyboard.h>
#include <wiikeyboard/usbkeyboard.h>

/* Prototypes */
void WKB_Initialize(void);
void WKB_Deinitialize(void);
u16  WKB_GetButtons(void);

#endif
