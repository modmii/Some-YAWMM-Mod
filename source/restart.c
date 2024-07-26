#include <stdio.h>
#include <ogcsys.h>

#include "restart.h"
#include "nand.h"
#include "sys.h"
#include "wpad.h"
#include "video.h"

void Restart_Wait(void)
{
	puts("\n	Press any button to exit...");
	fflush(stdout);

	/* Wait for button */
	Wpad_WaitButtons();

	Restart();
}

void Restart(void)
{
	Con_Clear();
	puts("\n	Exiting...");
	fflush(stdout);

	/* Disable NAND emulator */
	Nand_Disable();

	/* Load system menu */
	Sys_LoadMenu();
}
