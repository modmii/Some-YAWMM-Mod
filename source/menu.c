#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ogcsys.h>
#include <ogc/pad.h>
#include <wiilight.h>
#include <wiidrc/wiidrc.h>
#include <unistd.h>
#include <errno.h>

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

static int gSelected;

static bool gNeedPriiloaderOption = false;

/* Macros */
#define NB_NAND_DEVICES		(sizeof(ndevList) / sizeof(nandDevice))

// Local prototypes: wiiNinja
void WaitPrompt (char *prompt);
u32 WaitButtons(void);
u32 Pad_GetButtons(void);
void WiiLightControl (int state);

void PriiloaderRetainedPrompt(void);

static int __Menu_IsGreater(const void *p1, const void *p2)
{
	u32 n1 = *(u32 *)p1;
	u32 n2 = *(u32 *)p2;

	/* Equal */
	if (n1 == n2)
		return 0;

	return (n1 > n2) ? 1 : -1;
}

static int __Menu_EntryCmp(const void *p1, const void *p2)
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

static s32 __Menu_RetrieveList(char *inPath, fatFile **outbuf, u32 *outlen)
{
	fatFile     *buffer = NULL;
	DIR            *dir = NULL;
	struct dirent  *ent = NULL;
	u32             cnt = 0;

	char tmpPath[MAX_FILE_PATH_LEN];

	/* Clear output values */
	*outbuf = NULL;
	*outlen = 0;

	/* Open directory */
	dir = opendir(inPath);
	if (!dir)
		return -1;

	/* Get entries */
	while ((ent = readdir(dir)) != NULL)
	{
		bool addFlag = false;
		bool isdir = false;
		bool isdol = false;
		bool iself = false;
		bool iswad = false;
		size_t fsize = 0;

		/* Hide entries that start with "._". I hate macOS */
		if (!strncmp(ent->d_name, "._", 2))
			continue;

		snprintf(tmpPath, MAX_FILE_PATH_LEN, "%s/%s", inPath, ent->d_name);
		if (FSOPFolderExists(tmpPath))  // wiiNinja
		{
			isdir = true;
			// Add only the item ".." which is the previous directory
			// AND if we're not at the root directory
			if ((strcmp (ent->d_name, "..") == 0) && (strchr(inPath, '/') == strrchr(inPath, '/')))
				addFlag = true;
			else if (strcmp (ent->d_name, ".") != 0)
				addFlag = true;
		}
		else
		{
			if (strrchr(ent->d_name, '.'))
			{
				if (!strcasecmp(strrchr(ent->d_name, '.'), ".wad"))
				{
					fsize = FSOPGetFileSizeBytes(tmpPath);
					addFlag = true;
					iswad = true;
				}
				if (!strcasecmp(strrchr(ent->d_name, '.'), ".dol"))
				{
					fsize = FSOPGetFileSizeBytes(tmpPath);
					addFlag = true;
					isdol = true;
				}
				if (!strcasecmp(strrchr(ent->d_name, '.'), ".elf"))
				{
					fsize = FSOPGetFileSizeBytes(tmpPath);
					addFlag = true;
					iself = true;
				}
			}
		}

		if (addFlag)
		{
			buffer = reallocarray(*outbuf, cnt + 1, sizeof(fatFile));
			if (!buffer) // Reallocation failed. Why?
			{
				free(*outbuf);
				*outbuf = NULL;
				return -997;
			}
			*outbuf = buffer;

			fatFile *file = &buffer[cnt++];

			// Clear fatFile structure
			memset(file, 0, sizeof(fatFile));

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
			printf("\t   Press HOME button to exit.\n\n");

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

	static const u16 konamiCode[] =
	{
		WPAD_BUTTON_UP, WPAD_BUTTON_UP, WPAD_BUTTON_DOWN, WPAD_BUTTON_DOWN, WPAD_BUTTON_LEFT,
		WPAD_BUTTON_RIGHT, WPAD_BUTTON_LEFT, WPAD_BUTTON_RIGHT, WPAD_BUTTON_B, WPAD_BUTTON_A
	};

	int codePosition = 0;
	extern bool skipRegionSafetyCheck;

	char region = '\0';
	u16 version = 0;
	
	u32 iosVersion  = IOS_GetVersion();
	u16 iosRevision = IOS_GetRevision();
//	u8  iosRevMajor = (iosRevision >> 8) & 0xFF;
//	u8  iosRevMinor = iosRevision & 0xFF;

	GetSysMenuRegion(&version, &region);
	bool havePriiloader = IsPriiloaderInstalled();

	/* Select source device */
	if (gConfig.fatDeviceIndex < 0)
	{
		for (;;) 
		{
			/* Clear console */
			Con_Clear();
			bool deviceOk = (FatGetDeviceCount() > 0);

			/*
			 * 0xc9 - top left corner
			 * 0xc8 - bottom left corner
			 * 0xbb - top right corner
			 * 0xbc - bottom right corner
			 * 0xcd - horizontal
			 * 0xb9 - vertical conjuction to left
			 * 0xcc - vertical conjuction to right
			 */

			char horizontal[60];
			memset(horizontal, 0xcd, sizeof(horizontal));

			printf(" \xc9%.59s\xbb", horizontal);
			printf(" \xcc%.14sWelcome to YAWM ModMii Edition!%.14s\xb9", horizontal, horizontal);
			printf(" \xc8%.59s\xbc", horizontal);

			printf("	Running on IOS%u v%u (AHB access %s)\n\n", iosVersion, iosRevision, AHBPROT_DISABLED ? "enabled" : "disabled");

			if (VersionIsOriginal(version))
				printf("	System menu: %s%c %s\n", GetSysMenuVersionString(version), region ?: '?', GetSysMenuRegionString(region)); // The ? should not appear any more, but, in any case.
			else
				printf("	System menu: Unknown (v%u) %s\n", version, GetSysMenuRegionString(region));

			printf("	 Priiloader: %s\n\n", havePriiloader ? "Installed" : "Not installed");

			if (!deviceOk)
			{
				printf("	[+] No source devices found\n\n");
			}
			else
			{
				printf("	>> Select source device: < %s >\n\n", FatGetDeviceName(gSelected));
				printf("	   Press LEFT/RIGHT to change the selected device.\n");
				printf("	   Press A button to continue.\n\n");
			}

			printf("	   Press 1 button to remount source devices.\n");
			printf("	   Press HOME button to exit.\n\n");

			if (skipRegionSafetyCheck)
			{
			//	printf("[+] WARNING: SM region and version checks disabled!\n"); // not for SM exclusively anymore
			//	printf("	Press 2 button to reset.\n");
				puts("	[+] WARNING: Safety checks disabled!!! Press 2 to reset.");
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
					puts("[+] Disabled safety checks. Be careful out there!");
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
			printf("\t   Press HOME button to exit.\n\n");

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
int Menu_BatchProcessWads(fatFile *files, int fileCount, char *inFilePath)
{
	int installCnt = 0;
	int uninstallCnt = 0;
	int count;

	for (fatFile* f = files; f < files + fileCount; f++) {
		if (f->install == 1) installCnt++; else
		if (f->install == 2) uninstallCnt++;
	}

	if (!(installCnt || uninstallCnt))
		return 0;

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
					printf("    %.40s: %s\n", thisFile->filename, wad_strerror(thisFile->installstate));
					i++;

					
					if (i == 17)
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
		PriiloaderRetainedPrompt();

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

/* Folder operations - thepikachugamer */
int Menu_FolderOperations(fatFile* file, char* path)
{
	int ret;

	char workpath[MAX_FILE_PATH_LEN];

	fatFile     *flist = NULL;
	unsigned int fcnt = 0;
	unsigned int wadcnt = 0;

	char* ptr_fname = workpath + sprintf(workpath, "%s%s/", path, file->filename);

	ret = __Menu_RetrieveList(workpath, &flist, &fcnt);
	if (ret != 0)
	{
		WaitPrompt("__Menu_RetrieveList failed");
		return 0;
	}

	fatFile* wads[fcnt];
	for (fatFile* f = flist; f < flist + fcnt; f++)
	{
		if (f->iswad) wads[wadcnt++] = f;
	}

	const char* folderOperations[] = { "Install all WADs", "Install and delete all WADs" };
	int mode = 0;
	while (true)
	{
		Con_Clear();

		printf("[+] Folder name: %s\n", file->filename);
		printf("    WAD count  : %i\n\n", wadcnt);

		printf("[+] Select action: < %s >\n\n", folderOperations[mode]);

		puts("    Press LEFT/RIGHT to change selected action.");
		puts("    Press A to continue.");
		puts("    Press B to return to the menu.");

		u32 buttons = WaitButtons();

		if (buttons & (WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT))
			mode ^= 1;

		if (buttons & WPAD_BUTTON_A)
			break;

		if (buttons & WPAD_BUTTON_B)
			goto finish;
	}

	if (!wadcnt)
	{
		WaitPrompt(">    WAD count: 0\n");
		goto finish;
	}

	int start = 0; // No cursor here so we just need start
	while (true)
	{
		Con_Clear();

		printf("[+] List of WADs to %s:\n\n", mode ? "install and delete" : "install");
		for (int i = 0; i < ENTRIES_PER_PAGE; i++)
		{
			int index = start + i;
			if (index >= wadcnt) // Data store interrupt safety!!
				putchar('\n');
			else
				printf("    %.50s\n", wads[index]->filename);
		}

		putchar('\n');
		printf("[+] Press UP/DOWN to move list.\n");
		printf("    Press A to continue.\n");
		printf("    Press B to cancel.");

		u32 buttons = WaitButtons();

		if (buttons & WPAD_BUTTON_UP)
		{
			if (start) start--;
		}
		else if (buttons & WPAD_BUTTON_DOWN)
		{
			if (wadcnt - start > ENTRIES_PER_PAGE) start++;
		}
		else if (buttons & WPAD_BUTTON_A)
		{
			break;
		}
		else if (buttons & WPAD_BUTTON_B)
		{
			goto finish;
		}
	}

	for (int i = 0; i < wadcnt; i++)
	{
		fatFile *f  = wads[i];
		FILE    *fp = NULL;

		Con_Clear();

		printf("[+] Processing WAD %i/%i...\n\n", i + 1, wadcnt);

		printf("[+] Opening \"%s\", please wait...\n", f->filename);
		strcpy(ptr_fname, f->filename);
		fp = fopen(workpath, "rb");
		if (!fp)
		{
			printf("    ERROR! (errno=%i)\n", errno);
		}
		else
		{
			// puts(">> Installing WAD...");
			f->installstate = ret = Wad_Install(fp);
			fclose(fp);

			if (!ret)
			{
				if (mode == 1)
				{
					printf(">> Deleting WAD... ");
					// ret = FSOPDeleteFile(workpath);
					ret = remove(workpath);
					if (!ret)
						puts("OK!");
					else
						printf("ERROR! (errno=%i)\n", errno);
				}
			}
			else if (ret == -1010)
			{
				do { wads[i++]->installstate = -1010; } while (i < wadcnt);
				WaitPrompt("Wii System Memory is full to the brim. Installation terminated...\n");
				break;
			}
		}

		usleep((!ret) ? 500000 : 4000000);
		continue;
	}

	start = 0;
	while (true)
	{
		Con_Clear();

		printf("[+] End results:\n\n");

		for (int i = 0; i < ENTRIES_PER_PAGE; i++)
		{
			int index = start + i;
			if (index >= wadcnt) // Data store interrupt safety!!
				putchar('\n');
			else
				printf("    %-.32s: %s\n", wads[index]->filename, wad_strerror(wads[index]->installstate));
		}

		putchar('\n');
		printf("[+] Press UP/DOWN to move list.\n");
		printf("    Press A to continue.");

		u32 buttons = WaitButtons();

		if (buttons & WPAD_BUTTON_UP)
		{
			if (start) start--;
		}
		else if (buttons & WPAD_BUTTON_DOWN)
		{
			if (wadcnt - start > ENTRIES_PER_PAGE) start++;
		}
		else if (buttons & WPAD_BUTTON_A)
		{
			break;
		}
	}

finish:
	free(flist);
	return 0;
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
			printf("    WAD Filesize : %.2f MB\n\n", filesize);


			printf("[+] Select action: < %s WAD >\n\n", (!mode) ? "Install" : "Uninstall");

			printf("    Press LEFT/RIGHT to change selected action.\n");
			printf("    Press A to continue.\n");
		}
		else {
			if(file->isdol) {
				printf("[+] DOL Filename : %s\n", file->filename);
				printf("    DOL Filesize : %.2f MB\n\n", filesize);

				printf("    Press A to launch DOL.\n");
			}
			if(file->iself) {
				printf("[+] ELF Filename : %s\n", file->filename);
				printf("    ELF Filesize : %.2f MB\n\n", filesize);

				printf("    Press A to launch ELF.\n");
			}
		}
		puts("    Press B to go back to the menu.");

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
	sprintf(gTmpFilePath, "%s%s", inFilePath, file->filename); // wiiNinja
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
				PriiloaderRetainedPrompt();
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
	fatFile *fileList = NULL;
	u32      fileCnt;
	int ret, selected = 0, start = 0;
	bool batchMode = false;
	char tmpPath[MAX_FILE_PATH_LEN];

	Con_Clear();
	printf("[+] Retrieving file list...");
	fflush(stdout);

	// if user provides startup directory, try it out first
	if (strcmp(gConfig.startupPath, WAD_DIRECTORY) != 0)
	{
		// replace root dir with provided startup directory
		sprintf(tmpPath, "%s:%s", FatGetDevicePrefix(gSelected), gConfig.startupPath);

		// Be absolutely sure that the path ends with a /, we need this
		if (strchr(tmpPath, 0)[-1] != '/')
			strcat(tmpPath, "/");

		if (FSOPFolderExists(tmpPath))
			goto getList;
	}

	sprintf(tmpPath, "%s:%s", FatGetDevicePrefix(gSelected), WAD_DIRECTORY);

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
/*
	int counter;
	for (counter = 0; counter < fileCnt; counter++) 
	{
		fatFile *file = &fileList[counter];
		file->install = 0;
		file->installstate = 0;
	}
*/
	for (;;)
	{
		u32 cnt;
		s32 index;

		/* Clear console */
		Con_Clear();

		/** Print entries **/
		char* pathStart = tmpPath;
		char* pathEnd = strchr(tmpPath, 0);
		if ((pathEnd - pathStart) > 30)
			pathStart = pathEnd - 30;
		
		printf("[+] Files on [%s]:\n\n", pathStart);
		
		/* Print entries */
		for (cnt = start; cnt < fileCnt; cnt++)
		{
			fatFile *file     = &fileList[cnt];
			f32      filesize = file->fsize / MB_SIZE;

			/* Entries per page limit */
			if ((cnt - start) >= ENTRIES_PER_PAGE)
				break;


			/* Print filename */
			//printf("\t%2s %s (%.2f MB)\n", (cnt == selected) ? ">>" : "  ", file->filename, filesize);
            if (file->isdir) // wiiNinja
			{
				printf("\t%2s [%.40s]\n", (cnt == selected) ? ">>" : "  ", file->filename);
			}
            else 
			{
                if(file->iswad)
					printf("\t%2s%c%.40s (%.2f MB)\n", (cnt == selected) ? ">>" : "  ", " +-"[file->install], file->filename, filesize);
				else
					printf("\t%2s %.40s (%.2f MB)\n", (cnt == selected) ? ">>" : "  ", file->filename, filesize);
			}
		}

		putchar('\n');
		fatFile *file = &fileList[selected];
		// There is only one occurence of /, likely because we are at the root!
		bool atRoot = (strchr(tmpPath, '/') == strrchr(tmpPath, '/'));

		const char* operationB = (atRoot) ? "Change source device" : "Go to parent directory";
		const char* operationR = (file->isdir) ? "Folder operations" : "File operations";

		if (batchMode)
		{
			printf("[+] A:   Batch operations       B:   %s\n", operationB);
			printf("    1/R: %-23s"                "2/L: Disable batch mode\n", operationR);
			printf("    +/X: Mark for install       -/Y: Mark for uninstall");
		}
		else
		{
			const char* operationA = "";

			if (file->iswad)
				operationA = "Install/Uninstall WAD";
			else if (file->isdol || file->iself)
				operationA = "Launch application";
			else if (file->isdir)
				operationA = "Enter directory";

			//     "[+]   A: Install/Uninstall WAD"
			printf("[+] A:   %-23s"                "B:   %s\n", operationA, operationB);
			printf("    1/R: %-23s"                "2/L: Enable batch mode", operationR);
		}


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
			if (fileCnt - start > ENTRIES_PER_PAGE) {
				start += ENTRIES_PER_PAGE;
				selected += ENTRIES_PER_PAGE;
				if (selected >= fileCnt)
					selected = fileCnt - 1;
			}
			else {
				selected = fileCnt - 1;
			}
		}
		else if (buttons & WPAD_BUTTON_DOWN) 
		{
			if (++selected >= fileCnt)
				selected = 0;
		}
		else if (buttons & WPAD_BUTTON_LEFT)
		{
			if (start >= ENTRIES_PER_PAGE) {
				start -= ENTRIES_PER_PAGE;
				selected -= ENTRIES_PER_PAGE;
				if (selected < 0)
					selected = 0;
			}
			else {
				selected = start = 0;
			}
		}
		else if (buttons & WPAD_BUTTON_HOME)
		{
			Restart();
		}

		else if (buttons & (WPAD_BUTTON_PLUS | WPAD_BUTTON_MINUS) && batchMode && file->iswad)
		{
			int install = (buttons & WPAD_BUTTON_PLUS) ? 1 : 2;

			if (Wpad_TimeButton())
			{
				// installCnt = uninstallCnt = 0;
				for (fatFile* f = fileList; f < fileList + fileCnt; f++)
				{
					if (!f->iswad)
					{
						continue;
					}
					f->install = (f->install != install) ? install : 0;
				}
			}
			else
			{
				file->install = (file->install != install) ? install : 0;

				selected++;
				if (selected >= fileCnt)
					selected = 0;
			}
		}

		/* 1 Button - Leathl */
		if (buttons & WPAD_BUTTON_1 && !batchMode)
		{
			int res = (file->isdir ? Menu_FolderOperations : Menu_FileOperations)(file, tmpPath);
			if (res != 0)
				goto getList;
		}

		/* 2 button - thepikachugamer */
		if (buttons & WPAD_BUTTON_2)
		{
			batchMode ^= 1;
			if (!batchMode) // Turned off
			{
				for (fatFile* f = fileList; f < fileList + fileCnt; f++)
				{
					if (!f->iswad)
						continue;
					f->install = 0;
				}
			}
		}

		/* A button */
		else if (buttons & WPAD_BUTTON_A)
		{
			if (batchMode)
			{
				int res = Menu_BatchProcessWads(fileList, fileCnt, tmpPath);

				if (res == 1)
				{
					for (fatFile* f = fileList; f <  fileList + fileCnt; f++)
					{
						f->install = f->installstate = 0;
					}
					batchMode = false;
				}
			}
			// else use standard wadmanage menu - Leathl
			else
			{
				if (file->isdir) // wiiNinja
				{
					selected = start = 0;

					if (!strcmp(file->filename, "..")) // We only add this entry when we are not at the root
					{
						// Previous dir                         v
						strrchr(tmpPath, '/')[0] = 0; // sd:/wad/
						strrchr(tmpPath, '/')[1] = 0; //     ^
					}
					else
					{
						strcat(tmpPath, file->filename);
						strcat(tmpPath, "/");
					}

					goto getList;
				}
				else
				{
					Menu_WadManage(file, tmpPath);
				}
			}

		}

		/* B button */
		else if (buttons & WPAD_BUTTON_B)
		{
			if (atRoot)
				return;

			selected = start = 0;
			
			// Previous dir                         v
			strrchr(tmpPath, '/')[0] = 0; // sd:/wad/
			strrchr(tmpPath, '/')[1] = 0; //     ^
			
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

#if 0
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
#endif

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

	if (buttons & WPAD_CLASSIC_BUTTON_A)
		buttons |= WPAD_BUTTON_A;
	else if (buttons & WPAD_CLASSIC_BUTTON_B)
		buttons |= WPAD_BUTTON_B;
	else if (buttons & WPAD_CLASSIC_BUTTON_X)
		buttons |= WPAD_BUTTON_1;
	else if (buttons & WPAD_CLASSIC_BUTTON_Y)
		buttons |= WPAD_BUTTON_2;
	else if (buttons & WPAD_CLASSIC_BUTTON_LEFT)
		buttons |= WPAD_BUTTON_LEFT;
	else if (buttons & WPAD_CLASSIC_BUTTON_RIGHT)
		buttons |= WPAD_BUTTON_RIGHT;
	else if (buttons & WPAD_CLASSIC_BUTTON_DOWN)
		buttons |= WPAD_BUTTON_DOWN;
	else if (buttons & WPAD_CLASSIC_BUTTON_UP)
		buttons |= WPAD_BUTTON_UP;
	else if (buttons & WPAD_CLASSIC_BUTTON_HOME)
		buttons |= WPAD_BUTTON_HOME;
	else if (buttons & WPAD_CLASSIC_BUTTON_PLUS)
		buttons |= WPAD_BUTTON_PLUS;
	else if (buttons & WPAD_CLASSIC_BUTTON_MINUS)
		buttons |= WPAD_BUTTON_MINUS;

	if (buttonsGC)
	{
		if (buttonsGC & PAD_BUTTON_A)
			buttons |= WPAD_BUTTON_A;
		else if (buttonsGC & PAD_BUTTON_B)
			buttons |= WPAD_BUTTON_B;
		else if (buttonsGC & PAD_BUTTON_LEFT)
			buttons |= WPAD_BUTTON_LEFT;
		else if (buttonsGC & PAD_BUTTON_RIGHT)
			buttons |= WPAD_BUTTON_RIGHT;
		else if (buttonsGC & PAD_BUTTON_DOWN)
			buttons |= WPAD_BUTTON_DOWN;
		else if (buttonsGC & PAD_BUTTON_UP)
			buttons |= WPAD_BUTTON_UP;
		else if (buttonsGC & PAD_BUTTON_START)
			buttons |= WPAD_BUTTON_HOME;
		else if (buttonsGC & PAD_BUTTON_X)
			buttons |= WPAD_BUTTON_PLUS;
		else if (buttonsGC & PAD_BUTTON_Y)
			buttons |= WPAD_BUTTON_MINUS;
		else if (buttonsGC & (PAD_TRIGGER_R | PAD_TRIGGER_Z))
			buttons |= WPAD_BUTTON_1;
		else if (buttonsGC & PAD_TRIGGER_L)
			buttons |= WPAD_BUTTON_2;
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
        else if(buttonsDRC & (WIIDRC_BUTTON_PLUS | WIIDRC_BUTTON_X))
            buttons |= WPAD_BUTTON_PLUS;
        else if(buttonsDRC & (WIIDRC_BUTTON_MINUS | WIIDRC_BUTTON_Y))
            buttons |= WPAD_BUTTON_MINUS;
        else if(buttonsDRC & (WIIDRC_BUTTON_R | WIIDRC_BUTTON_ZR))
            buttons |= WPAD_BUTTON_1;
        else if(buttonsDRC & (WIIDRC_BUTTON_L | WIIDRC_BUTTON_ZL))
            buttons |= WPAD_BUTTON_2;
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

void PriiloaderRetainedPrompt(void)
{
	puts("    Priiloader has been retained, but all hacks were reset.\n");

	puts("    Press A launch Priiloader now.");
	puts("    Press any other button to continue...");

	u32 buttons = WaitButtons();

	if (buttons & WPAD_BUTTON_A)
	{
		gNeedPriiloaderOption = false;
		*(vu32*)0x8132FFFB = 0x4461636f;
		DCFlushRange((void*)0x8132FFFB, 4);
		SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
		__builtin_unreachable();
	}
}
