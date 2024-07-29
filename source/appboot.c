#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ogc/machine/processor.h>
#include <ogcsys.h>
#include <ogc/lwp_threads.h>

#include "appboot.h"
#include "fat.h"
#include "sys.h"
#include "appmetadata.h"
#include "iospatch.h"
#include "video.h"

struct __argv arguments;
char* m_argv[256];

u8* metaBuffer = NULL;
u32 metaSize = 0;

u8* appBuffer = NULL;
u32 appSize = 0;
u32 appEntry = 0;

u32 appIos = 0;

#include "appboot_bin.h"

#define MEM2PROT 0x0D8B420A
#define ESMODULESTART (u16*)0x939F0000
#define MB_SIZE	1048576.0

static const u16 ticket[] = {
	0x685B,               // ldr r3,[r3,#4] ; get TMD pointer
	0x22EC, 0x0052,       // movls r2, 0x1D8
	0x189B,               // adds r3, r3, r2; add offset of access rights field in TMD
	0x681B,               // ldr r3, [r3]   ; load access rights (haxxme!)
	0x4698,               // mov r8, r3  ; store it for the DVD video bitcheck later
	0x07DB                // lsls r3, r3, #31; check AHBPROT bit
};

static bool patchahbprot(void)
{
	u16* patch;

	if ((read32(0x0D800064) == 0xFFFFFFFF) ? 1 : 0) 
	{
		write16(MEM2PROT, 2);
		for (patch = ESMODULESTART; patch < ESMODULESTART + 0x4000; ++patch) {
			if (!memcmp(patch, ticket, sizeof(ticket)))
			{
				patch[4] = 0x23FF;
				DCFlushRange(patch + 4, 2);
				return 0;
			}
		}
		return -1;
	}
	else {
		return -2;
	}
}

bool LoadApp(const char* path, const char* filename)
{
	Con_Clear();

	appBuffer = (u8*)0x92000000;
	
	char currentPath[256];
	snprintf(currentPath, sizeof(currentPath), "%s/meta.xml", path);
	u16 argumentsSize = 0;
	char* Arguments = LoadArguments(currentPath, &argumentsSize);

	if (Arguments)
	{
		*(vu32*)0x91000000 = argumentsSize;
		memcpy((void*)0x91000020, Arguments, argumentsSize);
		DCFlushRange((void*)0x91000020, argumentsSize);
		ICInvalidateRange((void*)0x91000020, argumentsSize);
		free(Arguments);
	}
	else
	{
		*(vu32*)0x91000000 = 0;
	}

/*
	// This causes crashes when XML nodes are empty
	struct MetaData* appData = LoadMetaData(currentPath);

	if (appData)
	{
		printf("-> App title: %s version %s\n", appData->name, appData->version);
		printf("-> Coder(s): %s\n", appData->coder);
		if (appData->releaseDate != NULL)
			printf("-> Release date: %s\n", appData->releaseDate);
		FreeMetaData(appData);

		printf("\n");
	}
*/

	snprintf(currentPath, sizeof(currentPath), "%s/%s", path, filename);
	
	FILE* f = fopen(currentPath, "rb");

	if (f == NULL)
		return false;

	printf("-> Load: %s\n", currentPath);

	fseek(f, 0, SEEK_END);
	appSize = ftell(f);
	rewind(f);

	if (appSize > 0x1000000)
	{
		fclose(f);
		return false;
	}
		
	u32 ret = fread(appBuffer, 1, appSize, f);
	if (ret != appSize)
	{
		printf("Failed to read file: %s (0x%X -> 0x%X)\n", currentPath, ret, appSize );
		fclose(f);
		return false;
	}
	else
	{
		f32 filesize = (appSize / MB_SIZE);
		printf("-> App size: %.2f MB\n\n", filesize);
	}

	DCFlushRange(appBuffer, appSize);
	ICInvalidateRange(appBuffer, appSize);

	fclose(f);
	return (ret == appSize);
}

u8* GetApp(u32* size)
{
	*size = appSize;
	return appBuffer;
}

void LaunchApp(void)
{	
	entrypoint entry = NULL;
	
	LoadBooter(&entry);
	
	if (!appIos)
		appIos = IOS_GetPreferredVersion();
	
	if (AHBPROT_DISABLED)
	{
		if (appIos > 0 && appIos < 200)
		{
			printf("-> Patch IOS for AHB access\n");
			patchahbprot();
		}	
	}

	if (appIos > 0)
		__IOS_LaunchNewIOS(appIos);

	if (AHBPROT_DISABLED)
	{
		printf("-> Reenable DVD access\n");
		mask32(0x0D800180, 1 << 21, 0);
	}

	printf("-> And we're outta here!\n");

	entry();

	printf("--> Well.. this shouldn't happen\n");
	exit(-1);
}

void SetIos(int ios)
{
	appIos = ios;
}

bool LoadBooter(entrypoint* entry)
{
	dolhdr* dol = (dolhdr*)appboot_bin;

	u32 i;
	for (i = 0; i < 7; i++)
	{
		if (dol->sizeText[i] == 0 || dol->addressText[i] < 0x100)
			continue;

		memmove((void*)dol->addressText[i], appboot_bin + dol->offsetText[i], dol->sizeText[i]);
		DCFlushRange((void*)dol->addressText[i], dol->sizeText[i]);
	}

	for (i = 0; i < 11; i++)
	{
		if (dol->sizeData[i] == 0)
			continue;

		memmove((void*)dol->addressData[i], appboot_bin + dol->offsetData[i], dol->sizeData[i]);
		DCFlushRange((void*)dol->addressData[i], dol->sizeData[i]);
	}

	*entry = (entrypoint)dol->entrypoint;

	return true;
}
