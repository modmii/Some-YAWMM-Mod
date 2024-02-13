#include <stdio.h>
#include <stdlib.h>
#include <ogcsys.h>

#include "sys.h"
#include "malloc.h"
#include "mload.h"
#include "ehcmodule_elf.h"

/* Constants */
#define CERTS_LEN 0x280

/* Variables */
static const char certs_fs[] ATTRIBUTE_ALIGN(32) = "/sys/cert.sys";
u32 boot2version;
static bool gDisablePRButtons = false;

void __Sys_ResetCallback(__attribute__((unused)) u32 irq, __attribute__((unused)) void *ctx)
{
	/* Reboot console */
	if (!gDisablePRButtons)
		Sys_Reboot();
}

void __Sys_PowerCallback(void)
{
	/* Poweroff console */
	if (!gDisablePRButtons)
		Sys_Shutdown();
}

bool isIOSstub(u8 ios_number)
{
	u32 tmd_size = 0;
	tmd_view *ios_tmd;

	if ((boot2version >= 5) && (ios_number == 202 || ios_number == 222 || ios_number == 223 || ios_number == 224))
		return true;

	ES_GetTMDViewSize(0x0000000100000000ULL | ios_number, &tmd_size);
	if (!tmd_size)
	{
		// getting size failed. invalid or fake tmd for sure!
		// gprintf("failed to get tmd for ios %d\n",ios_number);
		return true;
	}
	ios_tmd = memalign32(tmd_size);
	if (!ios_tmd)
	{
		// gprintf("failed to mem align the TMD struct!\n");
		return true;
	}
	memset(ios_tmd, 0, tmd_size);
	ES_GetTMDView(0x0000000100000000ULL | ios_number, (u8 *)ios_tmd, tmd_size);
	// gprintf("IOS %d is rev %d(0x%x) with tmd size of %u and %u contents\n",ios_number,ios_tmd->title_version,ios_tmd->title_version,tmd_size,ios_tmd->num_contents);
	/*Stubs have a few things in common:
	- title version : it is mostly 65280 , or even better : in hex the last 2 digits are 0.
			example : IOS 60 rev 6400 = 0x1900 = 00 = stub
	- exception for IOS21 which is active, the tmd size is 592 bytes (or 140 with the views)
	- the stub ios' have 1 app of their own (type 0x1) and 2 shared apps (type 0x8001).
	eventho the 00 check seems to work fine , we'll only use other knowledge as well cause some
	people/applications install an ios with a stub rev >_> ...*/
	u8 Version = ios_tmd->title_version;

	if ((boot2version >= 5) && (ios_number == 249 || ios_number == 250) && (Version < 18))
		return true;
	if ((ios_number == 202 || ios_number == 222 || ios_number == 223 || ios_number == 224) && (Version < 4))
		return true;
	// version now contains the last 2 bytes. as said above, if this is 00, its a stub
	if (Version == 0)
	{
		if ((ios_tmd->num_contents == 3) && (ios_tmd->contents[0].type == 1 && ios_tmd->contents[1].type == 0x8001 && ios_tmd->contents[2].type == 0x8001))
		{
			// gprintf("IOS %d is a stub\n",ios_number);
			free(ios_tmd);
			return true;
		}
		else
		{
			// gprintf("IOS %d is active\n",ios_number);
			free(ios_tmd);
			return false;
		}
	}
	// gprintf("IOS %d is active\n",ios_number);
	free(ios_tmd);
	return false;
}

bool loadIOS(int ios)
{
	if (isIOSstub(ios))
		return false;
	mload_close();
	if (IOS_ReloadIOS(ios) >= 0)
	{
		if (IOS_GetVersion() != 249 && IOS_GetVersion() != 250)
		{
			if (mload_init() >= 0)
			{
				data_elf my_data_elf;
				mload_elf((void *)ehcmodule_elf, &my_data_elf);
				mload_run_thread(my_data_elf.start, my_data_elf.stack, my_data_elf.size_stack, 0x47);
			}
		}
		return true;
	}
	return false;
}

void Sys_Init(void)
{
	/* Initialize video subsytem */
	VIDEO_Init();

	/* Set RESET/POWER button callback */
	SYS_SetResetCallback(__Sys_ResetCallback);
	SYS_SetPowerCallback(__Sys_PowerCallback);
}

void Sys_Reboot(void)
{
	/* Restart console */
	STM_RebootSystem();
}

void Sys_Shutdown(void)
{
	/* Poweroff console */
	if (CONF_GetShutdownMode() == CONF_SHUTDOWN_IDLE)
	{
		s32 ret;

		/* Set LED mode */
		ret = CONF_GetIdleLedMode();
		if (ret >= 0 && ret <= 2)
			STM_SetLedMode(ret);

		/* Shutdown to idle */
		STM_ShutdownToIdle();
	}
	else
	{
		/* Shutdown to standby */
		STM_ShutdownToStandby();
	}
}

void Sys_LoadMenu(void)
{
	int HBC = 0;
	char *sig = (char *)0x80001804;
	if (sig[0] == 'S' &&
		sig[1] == 'T' &&
		sig[2] == 'U' &&
		sig[3] == 'B' &&
		sig[4] == 'H' &&
		sig[5] == 'A' &&
		sig[6] == 'X' &&
		sig[7] == 'X')
	{
		HBC = 1; // Exit to HBC
	}

	/* Homebrew Channel stub */
	if (HBC == 1)
	{
		exit(0);
	}
	/* Return to the Wii system menu */
	SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

s32 Sys_GetCerts(signed_blob **certs, u32 *len)
{
	static signed_blob certificates[CERTS_LEN] ATTRIBUTE_ALIGN(32);

	s32 fd, ret;

	/* Open certificates file */
	fd = IOS_Open(certs_fs, 1);
	if (fd < 0)
		return fd;

	/* Read certificates */
	ret = IOS_Read(fd, certificates, sizeof(certificates));

	/* Close file */
	IOS_Close(fd);

	/* Set values */
	if (ret > 0)
	{
		*certs = certificates;
		*len = sizeof(certificates);
	}

	return ret;
}

void SetPRButtons(bool enabled)
{
	gDisablePRButtons = !enabled;
}
