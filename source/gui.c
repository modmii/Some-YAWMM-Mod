#include <stdio.h>
#include <stdlib.h>
#include <ogcsys.h>

#include "video.h"
#include "fat.h"
#include "menu.h"
#include "nand.h"
#include "globals.h"
#include "fileops.h"

/* Constants */
#define CONSOLE_XCOORD		70
#define CONSOLE_YCOORD		114
#define CONSOLE_WIDTH		502
#define CONSOLE_HEIGHT		300

s32 __Gui_DrawPng(void *img, u32 x, u32 y)
{
	IMGCTX   ctx = NULL;
	PNGUPROP imgProp;
	char path[1024];
	s32 ret = -1;
	s32 i;

	for (i = 0; i < FatGetDeviceCount(); i++)
	{
		snprintf(path, sizeof(path), "%s:%s", FatGetDevicePrefix(i), WM_BACKGROUND_PATH);
		if (FSOPFileExists(path))
		{
			ctx = PNGU_SelectImageFromDevice(path);
			break;
		}
			
	}

	if(!ctx)
	{
		/* Select PNG data */
		ctx = PNGU_SelectImageFromBuffer(img);
		if (!ctx) {
			ret = -1;
			goto out;
		}
	}
	
	/* Get image properties */
	ret = PNGU_GetImageProperties(ctx, &imgProp);
	if (ret != PNGU_OK) {
		ret = -1;
		goto out;
	}

	/* Draw image */
	Video_DrawPng(ctx, imgProp, x, y);

	/* Success */
	ret = 0;

out:
	/* Free memory */
	if (ctx)
		PNGU_ReleaseImageContext(ctx);

	return ret;
}


void Gui_InitConsole(void)
{
	/* Initialize console */
	Con_Init(CONSOLE_XCOORD, CONSOLE_YCOORD, CONSOLE_WIDTH, CONSOLE_HEIGHT);
}

void Gui_DrawBackground(void)
{
	extern char bgData[];

	/* Draw background */
	__Gui_DrawPng(bgData, 0, 0);
} 
