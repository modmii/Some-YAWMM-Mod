#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ogcsys.h>
#include <ogc/pad.h>
#include <wiilight.h>
#include <wiidrc/wiidrc.h>
#include <unistd.h>

#include "sys.h"
#include "fat.h"
#include "nand.h"
#include "restart.h"
#include "title.h"
#include "utils.h"
#include "video.h"
#include "wad.h"
#include "wpad.h"
#include "wkb.h"
#include "globals.h"
#include "iospatch.h"
#include "appboot.h"
#include "fileops.h"
#include "menu.h"

/* NAND device list */
nandDevice ndevList[] = 
{
	{ "Disable",				0,	0x00,	0x00 },
	{ "SD/SDHC Card",			1,	0xF0,	0xF1 },
	{ "USB 2.0 Mass Storage Device",	2,	0xF2,	0xF3 },
};

static nandDevice *ndev = NULL;

// wiiNinja: Define a buffer holding the previous path names as user
// traverses the directory tree. Max of 10 levels is define at this point
static u8 gDirLevel = 0;
static char gDirList [MAX_DIR_LEVELS][MAX_FILE_PATH_LEN];
static s32  gSeleted[MAX_DIR_LEVELS];
static s32  gStart[MAX_DIR_LEVELS];
static u8 gSelected = 0;

static bool gNeedPriiloaderOption = false;

/* Macros */
#define NB_NAND_DEVICES		(sizeof(ndevList) / sizeof(nandDevice))

// Local prototypes: wiiNinja
void WaitPrompt (char *prompt);
int PushCurrentDir(char *dirStr, int Selected, int Start);
char *PopCurrentDir(int *Selected, int *Start);
bool IsListFull (void);
char *PeekCurrentDir (void);
u32 WaitButtons(void);
u32 Pad_GetButtons(void);
void WiiLightControl (int state);

int __Menu_IsGreater(const void *p1, const void *p2)
{
	u32 n1 = *(u32 *)p1;
	u32 n2 = *(u32 *)p2;

	/* Equal */
	if (n1 == n2)
		return 0;

	return (n1 > n2) ? 1 : -1;
}

int __Menu_EntryCmp(const void *p1, const void *p2)
{
	fatFile *f1 = (fatFile *)p1;
	fatFile *f2 = (fatFile *)p2;

	/* Compare entries */ // wiiNinja: Include directory
    if ((f1->isdir) && !(f2->isdir))
        return (-1);
    else if (!(f1->isdir) && (f2->isdir))
        return (1);
    else
        return strcasecmp(f1->filename, f2->filename);
}

char gFileName[MAX_FILE_PATH_LEN];
s32 __Menu_RetrieveList(char *inPath, fatFile **outbuf, u32 *outlen)
{
	fatFile     *buffer = NULL;
	DIR            *dir = NULL;
	struct dirent  *ent = NULL;

	//char dirpath[256], filename[768];
	u32  cnt;

	/* Generate dirpath */
	//sprintf(dirpath, "%s:" WAD_DIRECTORY, fdev->mount);

	/* Open directory */
	dir = opendir(inPath);
	if (!dir)
		return -1;

	/* Count entries */
	for (cnt = 0; ((ent = readdir(dir)) != NULL);) {
		cnt++;
	}

	if (cnt > 0) 
	{
		/* Allocate memory */
		buffer = malloc(sizeof(fatFile) * cnt);
		if (!buffer) 
		{
			closedir(dir);
			return -2;
		}

		memset(buffer, 0, sizeof(fatFile) * cnt);

		/* Reset directory */
		rewinddir(dir);

		/* Get entries */
		for (cnt = 0; ((ent = readdir(dir)) != NULL);)
		{
			bool addFlag = false;
			bool isdir = false;
			bool isdol = false;
			bool iself = false;
			bool iswad = false;
			size_t fsize = 0;

			snprintf(gFileName, MAX_FILE_PATH_LEN, "%s/%s", inPath, ent->d_name);
			if (FSOPFolderExists(gFileName))  // wiiNinja
            {
				isdir = true;
                // Add only the item ".." which is the previous directory
                // AND if we're not at the root directory
                if ((strcmp (ent->d_name, "..") == 0) && (gDirLevel > 1))
                    addFlag = true;
                else if (strcmp (ent->d_name, ".") != 0)
                    addFlag = true;
            }
            else
			{
				if(strlen(ent->d_name)>4)
				{
					if(!strcasecmp(ent->d_name+strlen(ent->d_name)-4, ".wad"))
					{
						fsize = FSOPGetFileSizeBytes(gFileName);
						addFlag = true;
						iswad = true;
					}
					if(!strcasecmp(ent->d_name+strlen(ent->d_name)-4, ".dol"))
					{
						fsize = FSOPGetFileSizeBytes(gFileName);
						addFlag = true;
						isdol = true;
					}
					if(!strcasecmp(ent->d_name+strlen(ent->d_name)-4, ".elf"))
					{
						fsize = FSOPGetFileSizeBytes(gFileName);
						addFlag = true;
						iself = true;
					}
				}
			}

            if (addFlag == true)
            {
				fatFile *file = &buffer[cnt++];

				/* File name */
				strcpy(file->filename, ent->d_name);

				/* File stats */
				file->isdir = isdir;
				file->fsize = fsize;
				file->isdol = isdol;
				file->iself = iself;
				file->iswad = iswad;
			}
		}

		/* Sort list */
		qsort(buffer, cnt, sizeof(fatFile), __Menu_EntryCmp);
	}

	/* Close directory */
	closedir(dir);

	/* Set values */
	*outbuf = buffer;
	*outlen = cnt;

	return 0;
}


void Menu_SelectIOS(void)
{
	u8 *iosVersion = NULL;
	u32 iosCnt;
	u8 tmpVersion;

	u32 cnt;
	s32 ret, selected = 0;
	bool found = false;

	/* Get IOS versions */
	ret = Title_GetIOSVersions(&iosVersion, &iosCnt);
	if (ret < 0)
		return;

	/* Sort list */
	qsort(iosVersion, iosCnt, sizeof(u8), __Menu_IsGreater);

	if (gConfig.cIOSVersion < 0)
	{
		tmpVersion = CIOS_VERSION;
	}
	else
	{
		tmpVersion = (u8)gConfig.cIOSVersion;
		// For debugging only
		//printf ("User pre-selected cIOS: %i\n", tmpVersion);
		//WaitButtons();
	}

	/* Set default version */
	for (cnt = 0; cnt < iosCnt; cnt++) 
	{
		u8 version = iosVersion[cnt];

		/* Custom IOS available */
		//if (version == CIOS_VERSION)
		if (version == tmpVersion)
		{
			selected = cnt;
			found = true;
			break;
		}

		/* Current IOS */
		if (version == IOS_GetVersion())
			selected = cnt;
	}

	/* Ask user for IOS version */
	if ((gConfig.cIOSVersion < 0) || (found == false))
	{
		for (;;)
		{
			/* Clear console */
			Con_Clear();

			printf("\t>> Select IOS version to use: < IOS%d >\n\n", iosVersion[selected]);

			printf("\t   Press LEFT/RIGHT to change IOS version.\n\n");

			printf("\t   Press A button to continue.\n");
			printf("\t   Press HOME button to restart.\n\n");

			u32 buttons = WaitButtons();

			/* LEFT/RIGHT buttons */
			if (buttons & WPAD_BUTTON_LEFT) {
				if ((--selected) <= -1)
					selected = (iosCnt - 1);
			}
			if (buttons & WPAD_BUTTON_RIGHT) {
				if ((++selected) >= iosCnt)
					selected = 0;
			}

			/* HOME button */
			if (buttons & WPAD_BUTTON_HOME)
				Restart();

			/* A button */
			if (buttons & WPAD_BUTTON_A)
				break;
		}
	}

	u8 version = iosVersion[selected];

	if (IOS_GetVersion() != version) {
		/* Shutdown subsystems */
		FatUnmount();
		Wpad_Disconnect();


		/* Load IOS */
		if (!loadIOS(version))
		{
			Wpad_Init();
			Menu_SelectIOS();
		}

		/* Initialize subsystems */
		Wpad_Init();
		FatMount();
	}
}

void Menu_FatDevice(void)
{
	FatMount();
	if (gSelected >= FatGetDeviceCount())
		gSelected = 0;

	const u16 konamiCode[] = 
	{
		WPAD_BUTTON_UP, WPAD_BUTTON_UP, WPAD_BUTTON_DOWN, WPAD_BUTTON_DOWN, WPAD_BUTTON_LEFT,
		WPAD_BUTTON_RIGHT, WPAD_BUTTON_LEFT, WPAD_BUTTON_RIGHT, WPAD_BUTTON_B, WPAD_BUTTON_A
	};

	int codePosition = 0;
	extern bool skipRegionSafetyCheck;

	char region = '\0';
	u16 version = 0;
	
	GetSysMenuRegion(&version, &region);
	bool havePriiloader = IsPriiloaderInstalled();
	u32 ios = *((u32*)0x80003140);

	/* Select source device */
	if (gConfig.fatDeviceIndex < 0)
	{
		for (;;) 
		{
			/* Clear console */
			Con_Clear();
			bool deviceOk = (FatGetDeviceCount() > 0);

			if (VersionIsOriginal(version))
				printf("\tIOS %d v%d, System menu: %s%c %s\n\n", ((ios >> 16) & 0xFF), (ios & 0xFFFF), GetSysMenuVersionString(version), region, GetSysMenuRegionString(region));
			else
				printf("\tIOS %d v%d, System menu: Unknown %s\n\n", ((ios >> 16) & 0xFF), (ios & 0xFFFF), GetSysMenuRegionString(region));
			
			
			printf("\tAHB access: %s\n", AHBPROT_DISABLED ? "Yes" : "No");
			printf("\tPriiloader: %s\n\n", havePriiloader ? "Installed" : "Not installed");

			if (!deviceOk)
			{
				printf("\t[+] No source devices found\n\n");
			}
			else
			{
				printf("\t>> Select source device: < %s >\n\n", FatGetDeviceName(gSelected));
				printf("\t   Press LEFT/RIGHT to change the selected device.\n\n");
				printf("\t   Press A button to continue.\n");
			}
			/* Selected device */
			//printf("\tWii menu version: %d, region: %s\n\n", gMenuVersion, GetSysMenuRegionString(&gMenuRegion));
			
			printf("\t   Press 1 button to remount source devices.\n");			
			printf("\t   Press HOME button to restart.\n\n");

			if (skipRegionSafetyCheck)
			{
				printf("[+] WARNING: SM region and version checks disabled!\n\n");
				printf("\t   Press 2 button to reset.\n");
			}
				

			u32 buttons = WaitButtons();

			if (deviceOk && buttons & (WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_RIGHT | WPAD_BUTTON_LEFT | WPAD_BUTTON_A | WPAD_BUTTON_B))
			{
				if (!skipRegionSafetyCheck)
				{
					if (buttons & konamiCode[codePosition])
						++codePosition;
					else
						codePosition = 0;
				}
			}
			if (deviceOk && buttons & WPAD_BUTTON_LEFT)
			{
				if ((s8)(--gSelected) < 0)
					gSelected = FatGetDeviceCount() - 1;
			}
			else if (deviceOk && buttons & WPAD_BUTTON_RIGHT)
			{
				if ((++gSelected) >= FatGetDeviceCount())
					gSelected = 0;
			}
			else if (buttons & WPAD_BUTTON_HOME)
			{
				Restart();
			}
			else if (buttons & WPAD_BUTTON_1)
			{
				printf("\t\t[-] Mounting devices.");
				usleep(500000);
				printf("\r\t\t[\\]");
				usleep(500000);
				printf("\r\t\t[|]");
				usleep(500000);
				printf("\r\t\t[/]");
				usleep(500000);
				printf("\r\t\t[-]");
				FatMount();
				gSelected = 0;
				usleep(500000);
			}
			else if (buttons & WPAD_BUTTON_2 && skipRegionSafetyCheck)
			{
				skipRegionSafetyCheck = false;
			}
			else if (deviceOk && buttons & WPAD_BUTTON_A)
			{
				if (codePosition == sizeof(konamiCode) / sizeof(konamiCode[0])) 
				{
					skipRegionSafetyCheck = true;
					printf("[+] Disabled SM region and version checks\n");
					sleep(3);
				}

				break;
			}
		}
	}
	else
	{
		sleep(5);
		if (gConfig.fatDeviceIndex < FatGetDeviceCount())
			gSelected = gConfig.fatDeviceIndex;
	}

	printf("[+] Selected source device: %s.\n", FatGetDeviceName(gSelected));
	sleep(2);
}

void Menu_NandDevice(void)
{
	int ret, selected = 0;

	/* Disable NAND emulator */
	if (ndev) {
		Nand_Unmount(ndev);
		Nand_Disable();
	}

	/* Select source device */
	if (gConfig.nandDeviceIndex < 0)
	{
		for (;;) {
			/* Clear console */
			Con_Clear();

			/* Selected device */
			ndev = &ndevList[selected];

			printf("\t>> Select NAND emulator device: < %s >\n\n", ndev->name);

			printf("\t   Press LEFT/RIGHT to change the selected device.\n\n");

			printf("\t   Press A button to continue.\n");
			printf("\t   Press HOME button to restart.\n\n");

			u32 buttons = WaitButtons();

			/* LEFT/RIGHT buttons */
			if (buttons & WPAD_BUTTON_LEFT) {
				if ((--selected) <= -1)
					selected = (NB_NAND_DEVICES - 1);
			}
			if (buttons & WPAD_BUTTON_RIGHT) {
				if ((++selected) >= NB_NAND_DEVICES)
					selected = 0;
			}

			/* HOME button */
			if (buttons & WPAD_BUTTON_HOME)
				Restart();

			/* A button */
			if (buttons & WPAD_BUTTON_A)
				break;
		}
	}
	else
	{
		ndev = &ndevList[gConfig.nandDeviceIndex];
	}

	/* No NAND device */
	if (!ndev->mode)
		return;

	printf("[+] Enabling NAND emulator...");
	fflush(stdout);

	/* Mount NAND device */
	ret = Nand_Mount(ndev);
	if (ret < 0) {
		printf(" ERROR! (ret = %d)\n", ret);
		goto err;
	}

	/* Enable NAND emulator */
	ret = Nand_Enable(ndev);
	if (ret < 0) {
		printf(" ERROR! (ret = %d)\n", ret);
		goto err;
	} else
		printf(" OK!\n");

	return;

err:
	printf("\n");
	printf("    Press any button to continue...\n");

	WaitButtons();

	/* Prompt menu again */
	Menu_NandDevice();
}

char gTmpFilePath[MAX_FILE_PATH_LEN];
/* Install and/or Uninstall multiple WADs - Leathl */
int Menu_BatchProcessWads(fatFile *files, int fileCount, char *inFilePath, int installCnt, int uninstallCnt)
{
	int count;

	for (;;)
	{
		Con_Clear();

		if ((installCnt > 0) & (uninstallCnt == 0)) {
			printf("[+] %d file%s marked for installation.\n", installCnt, (installCnt == 1) ? "" : "s");
			printf("    Do you want to proceed?\n");
		}
		else if ((installCnt == 0) & (uninstallCnt > 0)) {
			printf("[+] %d file%s marked for uninstallation.\n", uninstallCnt, (uninstallCnt == 1) ? "" : "s");
			printf("    Do you want to proceed?\n");
		}
		else {
			printf("[+] %d file%s marked for installation.\n", installCnt, (installCnt == 1) ? "" : "s");
			printf("[+] %d file%s marked for uninstallation.\n", uninstallCnt, (uninstallCnt == 1) ? "" : "s");
			printf("    Do you want to proceed?\n");
		}

		printf("\n\n    Press A to continue.\n");
		printf("    Press B to go back to the menu.\n\n");

		u32 buttons = WaitButtons();

		if (buttons & WPAD_BUTTON_A)
			break;

		if (buttons & WPAD_BUTTON_B)
			return 0;
	}

	WiiLightControl (WII_LIGHT_ON);
	int errors = 0;
	int success = 0;
	s32 ret;

	for (count = 0; count < fileCount; count++)
	{
		fatFile *thisFile = &files[count];

		int mode = thisFile->install;
		if (mode) 
		{
			Con_Clear();
			printf("[+] Processing WAD: %d/%d...\n\n", (errors + success + 1), (installCnt + uninstallCnt));
			printf("[+] Opening \"%s\", please wait...\n\n", thisFile->filename);

			sprintf(gTmpFilePath, "%s/%s", inFilePath, thisFile->filename);

			FILE *fp = fopen(gTmpFilePath, "rb");
			if (!fp) 
			{
				printf(" ERROR!\n");
				errors += 1;
				continue;
			}
			
			if (mode == 2) 
			{
				printf(">> Uninstalling WAD, please wait...\n\n");
				ret = Wad_Uninstall(fp);
			}
			else 
			{
				printf(">> Installing WAD, please wait...\n\n");
				ret = Wad_Install(fp);
			}

			if (ret < 0) 
			{
				if ((errors + success + 1) < (installCnt + uninstallCnt))
				{
					s32 i;
					for (i = 5; i > 0; i--)
					{
						printf("\r   Continue in: %d", i);
						sleep(1);
					}
				}

				errors++;
			}
			else 
			{
				success++;
			}

			thisFile->installstate = ret;

			if (fp)
				fclose(fp);
		}
	}

	WiiLightControl(WII_LIGHT_OFF);

	printf("\n");
	printf("    %d titles succeeded and %d failed...\n", success, errors);

	if (errors > 0)
	{
		printf("\n    Some operations failed");
		printf("\n    Press A to list.\n");
		printf("    Press B skip.\n");

		u32 buttons = WaitButtons();

		if ((buttons & WPAD_BUTTON_A))
		{
			Con_Clear();

			int i = 0;
			for (count = 0; count < fileCount; count++)
			{
				fatFile *thisFile = &files[count];

				if (thisFile->installstate < 0)
				{
					printf("    %.40s: ", thisFile->filename);
					i++;
					
					
					switch (thisFile->installstate)
					{
						case -106:	puts("Not installed?"); break;
						case -996:	puts("Read error"); break;
						case -998:	puts("Skipped"); break;
						case -999:	puts("BRICK BLOCKED"); break;
						case -1010:	puts("Wii System memory full!");
						case -1022:	puts("Content hash mismatch"); break;
						case -1035:	puts("Newer version already installed"); break;
						case -1036:	puts("Needed IOS missing!"); break;
						case -2011:	puts("No trucha bug?"); break;
						/*
						 * from libogc.
						 * This rarely happens unless the WAD had an invalid ticket/tmd size
						 * (certs were not stripped after downloading from NUS maybe?)
						 */
						case ES_EINVAL: puts("Invalid WAD?"); break;

						default: printf("error %d\n", thisFile->installstate); break;
					}
					
					if(i == 17)
					{
						printf("\n    Press any button to continue\n");
						WaitButtons();
						i = 0;
					}
				}
			}
		}
	}

	if (gNeedPriiloaderOption)
	{
		printf("\n    Priiloader has been retained, but all hacks were reset.\n\n");
		printf("    Press A launch Priiloader now.\n");
		printf("    Press any other button to continue...\n");

		u32 buttons = WaitButtons();
		
		if (buttons & WPAD_BUTTON_A)
		{
			gNeedPriiloaderOption = false;
			*(vu32*)0x8132FFFB = 0x4461636f;
			DCFlushRange((void*)0x8132FFFB, 4);
			SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
		}

		gNeedPriiloaderOption = false;
	}
	else
	{
		printf("\n    Press any button to continue...\n");
		WaitButtons();
	}

	return 1;
}

/* File Operations - Leathl */
int Menu_FileOperations(fatFile *file, char *inFilePath)
{
	f32 filesize = (file->fsize / MB_SIZE);

	for (;;)
	{
		Con_Clear();

		if(file->iswad) {
			printf("[+] WAD Filename : %s\n", file->filename);
			printf("    WAD Filesize : %.2f MB\n\n\n", filesize);
			printf("[+] Select action: < %s WAD >\n\n", "Delete"); //There's yet nothing else than delete
		}
		else if(file->isdol) {
			printf("[+] DOL Filename : %s\n", file->filename);
			printf("    DOL Filesize : %.2f MB\n\n\n", filesize);
			printf("[+] Select action: < %s DOL >\n\n", "Delete");
		}
		else if(file->iself) {
			printf("[+] ELF Filename : %s\n", file->filename);
			printf("    ELF Filesize : %.2f MB\n\n\n", filesize);
			printf("[+] Select action: < %s ELF >\n\n", "Delete");
		}
		else
			return 0;

		printf("    Press LEFT/RIGHT to change selected action.\n\n");

		printf("    Press A to continue.\n");
		printf("    Press B to go back to the menu.\n\n");

		u32 buttons = WaitButtons();

		/* A button */
		if (buttons & WPAD_BUTTON_A)
			break;

		/* B button */
		if (buttons & WPAD_BUTTON_B)
			return 0;
	}

	Con_Clear();

	printf("[+] Deleting \"%s\", please wait...\n", file->filename);

	sprintf(gTmpFilePath, "%s/%s", inFilePath, file->filename);
	int error = remove(gTmpFilePath);
	if (error != 0)
		printf("    ERROR!");
	else
		printf("    Successfully deleted!");

	printf("\n");
	printf("    Press any button to continue...\n");

	WaitButtons();

	return !error;
}

void Menu_WadManage(fatFile *file, char *inFilePath)
{
	FILE *fp  = NULL;
	
	//char filepath[256];
	f32  filesize;

	u32  mode = 0;

	/* File size in megabytes */
	filesize = (file->fsize / MB_SIZE);

	for (;;) {
		/* Clear console */
		Con_Clear();
		if(file->iswad) {
			printf("[+] WAD Filename : %s\n", file->filename);
			printf("    WAD Filesize : %.2f MB\n\n\n", filesize);


			printf("[+] Select action: < %s WAD >\n\n", (!mode) ? "Install" : "Uninstall");

			printf("    Press LEFT/RIGHT to change selected action.\n\n");
			printf("    Press A to continue.\n");
		}
		else {
			if(file->isdol) {
				printf("[+] DOL Filename : %s\n", file->filename);
				printf("    DOL Filesize : %.2f MB\n\n\n", filesize);
				printf("    Press A to launch DOL.\n");
			}
			if(file->iself) {
				printf("[+] ELF Filename : %s\n", file->filename);
				printf("    ELF Filesize : %.2f MB\n\n\n", filesize);
				printf("    Press A to launch ELF.\n");
			}
		}
		printf("    Press B to go back to the menu.\n\n");

		u32 buttons = WaitButtons();

		/* LEFT/RIGHT buttons */
		if (buttons & (WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT))
			mode ^= 1;

		/* A button */
		if (buttons & WPAD_BUTTON_A)
			break;

		/* B button */
		if (buttons & WPAD_BUTTON_B)
			return;
	}

	/* Clear console */
	Con_Clear();

	printf("[+] Opening \"%s\", please wait...", file->filename);
	fflush(stdout);

	/* Generate filepath */
	// sprintf(filepath, "%s:" WAD_DIRECTORY "/%s", fdev->mount, file->filename);
	sprintf(gTmpFilePath, "%s/%s", inFilePath, file->filename); // wiiNinja
	if(file->iswad) 
	{
		/* Open WAD */
		fp = fopen(gTmpFilePath, "rb");
		if (!fp) 
		{
			printf(" ERROR!\n");
			goto out;
		} 
		else
		{
			printf(" OK!\n\n");
		}

		printf("[+] %s WAD, please wait...\n", (!mode) ? "Installing" : "Uninstalling");

		/* Do install/uninstall */
		WiiLightControl (WII_LIGHT_ON);
		
		if (!mode)
		{
			Wad_Install(fp);
			WiiLightControl(WII_LIGHT_OFF);

			if (gNeedPriiloaderOption)
			{
				printf("\n    Priiloader has been retained, but all hacks were reset.\n\n");
				printf("    Press A launch Priiloader now.\n");
				printf("    Press any other button to continue...\n");

				u32 buttons = WaitButtons();
				
				if (fp)
					fclose(fp);
				
				if (buttons & WPAD_BUTTON_A)
				{
					gNeedPriiloaderOption = false;
					*(vu32*)0x8132FFFB = 0x4461636f;
					DCFlushRange((void*)0x8132FFFB, 4);
					SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
				}

				gNeedPriiloaderOption = false;

				return;
			}
		}
		else
		{

			Wad_Uninstall(fp);
			WiiLightControl(WII_LIGHT_OFF);
		}
	}
	else 
	{
		printf("launch dol/elf here \n");
		
		if(LoadApp(inFilePath, file->filename)) 
		{
			FatUnmount();
			Wpad_Disconnect();
			LaunchApp();
		}

		return;
	}

out:
	/* Close file */
	if (fp)
		fclose(fp);

	printf("\n");
	printf("    Press any button to continue...\n");

	/* Wait for button */
	WaitButtons();
}

void Menu_WadList(void)
{
	char str [100];
	fatFile *fileList = NULL;
	u32      fileCnt;
	int ret, selected = 0, start = 0;
    char *tmpPath = malloc (MAX_FILE_PATH_LEN);
	int installCnt = 0;
	int uninstallCnt = 0;

	//fatFile *installFiles = malloc(sizeof(fatFile) * 50);
	//int installCount = 0;

    // wiiNinja: check for malloc error
    if (tmpPath == NULL)
    {
        ret = -997; // What am I gonna use here?
		printf(" ERROR! Out of memory (ret = %d)\n", ret);
        return;
    }

	Con_Clear();
	printf("[+] Retrieving file list...");
	fflush(stdout);

	gDirLevel = 0;

	// push root dir as base folderGetDevice()
	//sprintf(tmpPath, "%s:%s", fdev->mount, WAD_DIRECTORY);
	sprintf(tmpPath, "%s:%s", FatGetDevicePrefix(gSelected), WAD_DIRECTORY);
	PushCurrentDir(tmpPath,0,0);
	// if user provides startup directory, try it out first
	if (strcmp (WAD_DIRECTORY, gConfig.startupPath) != 0)
	{
		// replace root dir with provided startup directory
		sprintf(tmpPath, "%s:%s", FatGetDevicePrefix(gSelected), gConfig.startupPath);

		if (FSOPFolderExists(tmpPath))
			PushCurrentDir(tmpPath, 0, 0);
		else
			sprintf(tmpPath, "%s:%s", FatGetDevicePrefix(gSelected), WAD_DIRECTORY);
		
		// If the directory can be successfully opened, it must exists
  //      DIR *tmpDirPtr = opendir(tmpPath);
  //      if (tmpDirPtr)
  //      {
		//	closedir (tmpDirPtr);
		//	PushCurrentDir(tmpPath,0,0);
  //      }
		//else // unable to open provided dir, stick with root dir
		//{
		//	sprintf(tmpPath, "%s:%s", FatGetDevicePrefix(gSelected), WAD_DIRECTORY);
		//}

	}

	/* Retrieve filelist */
getList:
    free(fileList);
    fileList = NULL;

	ret = __Menu_RetrieveList(tmpPath, &fileList, &fileCnt);
	if (ret < 0) 
	{
		printf(" ERROR! (ret = %d)\n", ret);
		goto err;
	}

	/* No files */
	if (!fileCnt) 
	{
		printf(" No files found!\n");
		goto err;
	}

	/* Set install-values to 0 - Leathl */
	int counter;
	for (counter = 0; counter < fileCnt; counter++) 
	{
		fatFile *file = &fileList[counter];
		file->install = 0;
		file->installstate = 0;
	}

	for (;;)
	{
		u32 cnt;
		s32 index;

		/* Clear console */
		Con_Clear();

		/** Print entries **/
		cnt = strlen(tmpPath);
		if(cnt > 30)
			index = cnt - 30;
		else
			index = 0;
		
		printf("[+] Files on [%s]:\n\n", tmpPath+index);
		
		/* Print entries */
		for (cnt = start; cnt < fileCnt; cnt++)
		{
			fatFile *file     = &fileList[cnt];
			f32      filesize = file->fsize / MB_SIZE;

			/* Entries per page limit */
			if ((cnt - start) >= ENTRIES_PER_PAGE)
				break;

			strncpy(str, file->filename, 40); //Only 40 chars to fit the screen
			str[40] = 0;

			/* Print filename */
			//printf("\t%2s %s (%.2f MB)\n", (cnt == selected) ? ">>" : "  ", file->filename, filesize);
            if (file->isdir) // wiiNinja
			{
				printf("\t%2s [%s]\n", (cnt == selected) ? ">>" : "  ", str);
			}
            else 
			{
                if(file->iswad)
					printf("\t%2s%s%s (%.2f MB)\n", (cnt == selected) ? ">>" : "  ", (file->install == 1) ? "+" : ((file->install == 2) ? "-" : " "), str, filesize);
				else
					printf("\t%2s %s (%.2f MB)\n", (cnt == selected) ? ">>" : "  ", str, filesize);
			}
		}

		printf("\n");
		fatFile *file = &fileList[selected];
		
		if (file->iswad)
			printf("[+] Press A to (un)install.");
		else if (file->isdol || file->iself)
			printf("[+] Press A to launch dol/elf.");
		else if (file->isdir)
			printf("[+] Press A to Enter directory.");
		
		if (gDirLevel > 1)
			printf(" Press B to go up-level DIR.\n");
		else
			printf(" Press B to select a device.\n");

		if (file->iswad)
			printf("    Use +/X and -/Y to (un)mark. Press 1/Z/ZR for delete menu.");

			/** Controls **/
		u32 buttons = WaitButtons();
			
		/* DPAD buttons */
		if (buttons & WPAD_BUTTON_UP) 
		{
			if (--selected < 0)
				selected = (fileCnt - 1);
		}
		else if (buttons & WPAD_BUTTON_RIGHT)
		{
			selected +=  ENTRIES_PER_PAGE;

			if (selected >= fileCnt)
				selected = 0;
		}
		else if (buttons & WPAD_BUTTON_DOWN) 
		{
			if (++selected >= fileCnt)
				selected = 0;
		}
		else if (buttons & WPAD_BUTTON_LEFT)
		{
				selected -=  ENTRIES_PER_PAGE;

			if (selected < 0)
				selected = (fileCnt - 1);
		}
		else if (buttons & WPAD_BUTTON_HOME)
		{
			Restart();
		}

		if (file->iswad) 
		{
			/* Plus Button - Leathl */
			if (buttons & WPAD_BUTTON_PLUS)
			{
				if(Wpad_TimeButton())
				{
					installCnt = 0;
					int i = 0;
					while(i < fileCnt)
					{
						fatFile *file = &fileList[i];
						if (((file->isdir) == false) && (file->install == 0)) 
						{
							file->install = 1;
							installCnt++;
						}
						else if (((file->isdir) == false) && (file->install == 1)) 
						{
							file->install = 0;
							installCnt--;
						}
						else if (((file->isdir) == false) && (file->install == 2)) 
						{
							file->install = 1;

							installCnt++;
							uninstallCnt--;
						}

						i++;
					}
				}
				else
				{
					fatFile *file = &fileList[selected];
					if (((file->isdir) == false) && (file->install == 0)) 
					{
						file->install = 1;
						installCnt++;
					}
					else if (((file->isdir) == false) & (file->install == 1)) 
					{
						file->install = 0;
						installCnt--;
					}
					else if (((file->isdir) == false) & (file->install == 2)) 
					{
						file->install = 1;

						installCnt++;
						uninstallCnt--;
					}
					
					selected++;
					if (selected >= fileCnt)
						selected = 0;
				}
			}

			/* Minus Button - Leathl */
			else if (buttons & WPAD_BUTTON_MINUS)
			{
				if(Wpad_TimeButton())
				{
					installCnt = 0;
					int i = 0;
			  
					while(i < fileCnt)
					{
						fatFile *file = &fileList[i];
						if (((file->isdir) == false) && (file->install == 0)) 
						{
							file->install = 2;
							uninstallCnt++;
						}
						else if (((file->isdir) == false) && (file->install == 1)) 
						{
							file->install = 2;
							uninstallCnt++;
							installCnt--;
						}
						else if (((file->isdir) == false) & (file->install == 2)) 
						{
							file->install = 0;
							uninstallCnt--;
						}

						i++;
					}
				}
				else
				{
					fatFile *file = &fileList[selected];
					if (((file->isdir) == false) && (file->install == 0)) 
					{
						file->install = 2;
						uninstallCnt++;
					}
					else if (((file->isdir) == false) && (file->install == 1)) 
					{
						file->install = 2;
						uninstallCnt++;
						installCnt--;
					}
					else if (((file->isdir) == false) && (file->install == 2)) 
					{
						file->install = 0;
						uninstallCnt--;
					}
			 
					selected++;
					if (selected >= fileCnt)
						selected = 0;
				}
			}

		}
		
		/* 1 Button - Leathl */
		if (buttons & WPAD_BUTTON_1)
		{
			fatFile *tmpFile = &fileList[selected];
			char *tmpCurPath = PeekCurrentDir();
            if (tmpCurPath != NULL) 
			{
				int res = Menu_FileOperations(tmpFile, tmpCurPath);
                if (res != 0)
					goto getList;
			}
		}

		/* A button */
		else if (buttons & WPAD_BUTTON_A)
		{
			fatFile *tmpFile = &fileList[selected];
			char *tmpCurPath;
			if (tmpFile->isdir) // wiiNinja
			{
				if (strcmp (tmpFile->filename, "..") == 0)
				{
					selected = 0;
					start = 0;

					// Previous dir
					tmpCurPath = PopCurrentDir(&selected, &start);
					if (tmpCurPath != NULL)
						sprintf(tmpPath, "%s", tmpCurPath);

					installCnt = 0;
					uninstallCnt = 0;

					goto getList;
				}
				else if (IsListFull() == true)
				{
					WaitPrompt ("Maximum number of directory levels is reached.\n");
				}
				else
				{
					tmpCurPath = PeekCurrentDir ();
					if (tmpCurPath != NULL)
					{
						if(gDirLevel > 1)
							sprintf(tmpPath, "%s/%s", tmpCurPath, tmpFile->filename);
						else
							sprintf(tmpPath, "%s%s", tmpCurPath, tmpFile->filename);
					}
					
					// wiiNinja: Need to PopCurrentDir
					PushCurrentDir (tmpPath, selected, start);
					selected = 0;
					start = 0;

					installCnt = 0;
					uninstallCnt = 0;

					goto getList;
				}
			}
			else
			{
				//If at least one WAD is marked, goto batch screen - Leathl
				if ((installCnt > 0) || (uninstallCnt > 0)) 
				{
					char *thisCurPath = PeekCurrentDir ();
					if (thisCurPath != NULL) 
					{
						int res = Menu_BatchProcessWads(fileList, fileCnt, thisCurPath, installCnt, uninstallCnt);

						if (res == 1) 
						{
							int counter;
							for (counter = 0; counter < fileCnt; counter++) 
							{
								fatFile *temp = &fileList[counter];
								temp->install = 0;
								temp->installstate = 0;
							}

							installCnt = 0;
							uninstallCnt = 0;
						}
					}
				}
				//else use standard wadmanage menu - Leathl
				else 
				{
					tmpCurPath = PeekCurrentDir();
					if (tmpCurPath != NULL)
						Menu_WadManage(tmpFile, tmpCurPath);
				}
			}
		}

		/* B button */
		else if (buttons & WPAD_BUTTON_B)
		{
			if (gDirLevel <= 1)
				return;

			char *tmpCurPath;
			selected = 0;
			start = 0;
			
			// Previous dir
			tmpCurPath = PopCurrentDir(&selected, &start);
			if (tmpCurPath != NULL)
				sprintf(tmpPath, "%s", tmpCurPath);
			
			goto getList;
		}

		/** Scrolling **/
		/* List scrolling */
		index = (selected - start);

		if (index >= ENTRIES_PER_PAGE)
			start += index - (ENTRIES_PER_PAGE - 1);
		else if (index < 0)
			start += index;
	}

err:
	printf("\n");
	printf("    Press any button to continue...\n");

	free(tmpPath);

	/* Wait for button */
	WaitButtons();
}
void Menu_Loop(void)
{
	u8 iosVersion;
	if (AHBPROT_DISABLED)
	{
		IOSPATCH_Apply();
	}
	else
	{
		/* Select IOS menu */
		Menu_SelectIOS();
	}

	/* Retrieve IOS version */
	iosVersion = IOS_GetVersion();

	ndev = &ndevList[0];

	/* NAND device menu */
	if ((iosVersion == CIOS_VERSION || iosVersion == 250) && IOS_GetRevision() > 13)
	{
		Menu_NandDevice();
	}
	
	for (;;) 
	{
		/* FAT device menu */
		Menu_FatDevice();

		/* WAD list menu */
		Menu_WadList();
	}
}

void SetPriiloaderOption(bool enabled)
{
	gNeedPriiloaderOption = enabled;
}

// Start of wiiNinja's added routines
int PushCurrentDir (char *dirStr, int Selected, int Start)
{
    int retval = 0;

    // Store dirStr into the list and increment the gDirLevel
    // WARNING: Make sure dirStr is no larger than MAX_FILE_PATH_LEN
    if (gDirLevel < MAX_DIR_LEVELS)
    {
        strcpy (gDirList [gDirLevel], dirStr);
		gSeleted[gDirLevel]=Selected;
		gStart[gDirLevel]=Start;
        gDirLevel++;
        //if (gDirLevel >= MAX_DIR_LEVELS)
        //    gDirLevel = 0;
    }
    else
        retval = -1;

    return (retval);
}

char *PopCurrentDir(int *Selected, int *Start)
{
	if (gDirLevel > 1)
        gDirLevel--;
    else {
        gDirLevel = 0;
	}
	*Selected = gSeleted[gDirLevel];
	*Start = gStart[gDirLevel];
	return PeekCurrentDir();
}

bool IsListFull (void)
{
    if (gDirLevel < MAX_DIR_LEVELS)
        return (false);
    else
        return (true);
}

char *PeekCurrentDir (void)
{
    // Return the current path
    if (gDirLevel > 0)
        return (gDirList [gDirLevel-1]);
    else
        return (NULL);
}

void WaitPrompt (char *prompt)
{
	printf("\n%s", prompt);
	printf("    Press any button to continue...\n");

	/* Wait for button */
	WaitButtons();
}

u32 Pad_GetButtons(void)
{
	u32 buttons = 0, cnt;

	/* Scan pads */
	PAD_ScanPads();

	/* Get pressed buttons */
	//for (cnt = 0; cnt < MAX_WIIMOTES; cnt++)
	for (cnt = 0; cnt < 4; cnt++)
		buttons |= PAD_ButtonsDown(cnt);

	return buttons;
}

u32 WiiDRC_GetButtons(void)
{
	if(!WiiDRC_Inited() || !WiiDRC_Connected())
		return 0;

	/* Scan pads */
	WiiDRC_ScanPads();

	/* Get pressed buttons */
	return WiiDRC_ButtonsDown();
}

// Routine to wait for a button from either the Wiimote or a gamecube
// controller. The return value will mimic the WPAD buttons to minimize
// the amount of changes to the original code, that is expecting only
// Wiimote button presses. Note that the "HOME" button on the Wiimote
// is mapped to the "SELECT" button on the Gamecube Ctrl. (wiiNinja 5/15/2009)
u32 WaitButtons(void)
{
	u32 buttons = 0;
    u32 buttonsGC = 0;
	u32 buttonsDRC = 0;
	u16 buttonsWKB = 0;

	/* Wait for button pressing */
	while (!(buttons | buttonsGC | buttonsDRC | buttonsWKB))
    {
        // Wii buttons
		buttons = Wpad_GetButtons();

        // GC buttons
        buttonsGC = Pad_GetButtons();

        // DRC buttons
        buttonsDRC = WiiDRC_GetButtons();

		// USB Keyboard buttons
		buttonsWKB = WKB_GetButtons();

		VIDEO_WaitVSync();
	}

	if(buttons & WPAD_CLASSIC_BUTTON_A)
		buttons |= WPAD_BUTTON_A;
	else if(buttons & WPAD_CLASSIC_BUTTON_B)
		buttons |= WPAD_BUTTON_B;
	else if(buttons & WPAD_CLASSIC_BUTTON_LEFT)
		buttons |= WPAD_BUTTON_LEFT;
	else if(buttons & WPAD_CLASSIC_BUTTON_RIGHT)
		buttons |= WPAD_BUTTON_RIGHT;
	else if(buttons & WPAD_CLASSIC_BUTTON_DOWN)
		buttons |= WPAD_BUTTON_DOWN;
	else if(buttons & WPAD_CLASSIC_BUTTON_UP)
		buttons |= WPAD_BUTTON_UP;
	else if(buttons & WPAD_CLASSIC_BUTTON_HOME)
		buttons |= WPAD_BUTTON_HOME;
	else if(buttons & (WPAD_CLASSIC_BUTTON_X | WPAD_CLASSIC_BUTTON_PLUS))
		buttons |= WPAD_BUTTON_PLUS;
	else if(buttons & (WPAD_CLASSIC_BUTTON_Y | WPAD_CLASSIC_BUTTON_MINUS))
		buttons |= WPAD_BUTTON_MINUS;
	else if(buttons & WPAD_CLASSIC_BUTTON_ZR)
		buttons |= WPAD_BUTTON_1;

    if (buttonsGC)
    {
        if(buttonsGC & PAD_BUTTON_A)
            buttons |= WPAD_BUTTON_A;
        else if(buttonsGC & PAD_BUTTON_B)
            buttons |= WPAD_BUTTON_B;
        else if(buttonsGC & PAD_BUTTON_LEFT)
            buttons |= WPAD_BUTTON_LEFT;
        else if(buttonsGC & PAD_BUTTON_RIGHT)
            buttons |= WPAD_BUTTON_RIGHT;
        else if(buttonsGC & PAD_BUTTON_DOWN)
            buttons |= WPAD_BUTTON_DOWN;
        else if(buttonsGC & PAD_BUTTON_UP)
            buttons |= WPAD_BUTTON_UP;
        else if(buttonsGC & PAD_BUTTON_START)
            buttons |= WPAD_BUTTON_HOME;
        else if(buttonsGC & PAD_BUTTON_X)
            buttons |= WPAD_BUTTON_PLUS;
        else if(buttonsGC & PAD_BUTTON_Y)
            buttons |= WPAD_BUTTON_MINUS;
        else if(buttonsGC & PAD_TRIGGER_Z)
            buttons |= WPAD_BUTTON_1;
    }

    if (buttonsDRC)
    {
        if(buttonsDRC & WIIDRC_BUTTON_A)
            buttons |= WPAD_BUTTON_A;
        else if(buttonsDRC & WIIDRC_BUTTON_B)
            buttons |= WPAD_BUTTON_B;
        else if(buttonsDRC & WIIDRC_BUTTON_LEFT)
            buttons |= WPAD_BUTTON_LEFT;
        else if(buttonsDRC & WIIDRC_BUTTON_RIGHT)
            buttons |= WPAD_BUTTON_RIGHT;
        else if(buttonsDRC & WIIDRC_BUTTON_DOWN)
            buttons |= WPAD_BUTTON_DOWN;
        else if(buttonsDRC & WIIDRC_BUTTON_UP)
            buttons |= WPAD_BUTTON_UP;
        else if(buttonsDRC & WIIDRC_BUTTON_HOME)
            buttons |= WPAD_BUTTON_HOME;
        else if(buttonsDRC & (WIIDRC_BUTTON_X | WIIDRC_BUTTON_PLUS))
            buttons |= WPAD_BUTTON_PLUS;
        else if(buttonsDRC & (WIIDRC_BUTTON_Y | WIIDRC_BUTTON_MINUS))
            buttons |= WPAD_BUTTON_MINUS;
        else if(buttonsDRC & WIIDRC_BUTTON_ZR)
            buttons |= WPAD_BUTTON_1;
    }

    if (buttonsWKB)
		buttons |= buttonsWKB;

	return buttons;
} // WaitButtons


void WiiLightControl (int state)
{
	switch (state)
	{
		case WII_LIGHT_ON:
			/* Turn on Wii Light */
			WIILIGHT_SetLevel(255);
			WIILIGHT_TurnOn();
			break;

		case WII_LIGHT_OFF:
		default:
			/* Turn off Wii Light */
			WIILIGHT_SetLevel(0);
			WIILIGHT_TurnOn();
			WIILIGHT_Toggle();
			break;
	}
} // WiiLightControl

