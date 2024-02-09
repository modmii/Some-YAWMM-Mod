#include <stdio.h>
#include <string.h>
#include <ogcsys.h>
#include <ogc/pad.h>
#include <unistd.h>

#include "sys.h"
#include "title.h"
#include "utils.h"
#include "video.h"
#include "wad.h"
#include "wpad.h"
#include "nand.h"
#include "fileops.h"
#include "sha1.h"
#include "menu.h"
#include "iospatch.h"
#include "malloc.h"

// Turn upper and lower into a full title ID
#define TITLE_ID(x,y)		(((u64)(x) << 32) | (y))
// Get upper or lower half of a title ID
#define TITLE_UPPER(x)		((u32)((x) >> 32))
// Turn upper and lower into a full title ID
#define TITLE_LOWER(x)		((u32)(x))

const char RegionLookupTable[16] =
{
	'J', 'U', 'E', 0, 0, 0, 'K', 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

const u16 VersionList[] = 
{
//	J		U		E		K

	64,		33,		66,					// 1.0
	128,	97,		130,				// 2.0
					162,				// 2.1
	192,	193,	194,				// 2.2
	224,	225,	226,				// 3.0
	256,	257,	258,				// 3.1
	288,	289,	290,				// 3.2
	352,	353,	354,	326,		// 3.3
	384,	385,	386, 				// 3.4
							390, 		// 3.5
	416,	417,	418,				// 4.0
	448,	449,	450,	454, 		// 4.1
	480,	481,	482,	486, 		// 4.2
	512,	513, 	514,	518, 		// 4.3/vWii 1.0.0
	544,	545,	546,				// vWii 4.0.0
	608,	609,	610					// vWii 5.2.0
};

u32 VersionListSize = sizeof(VersionList) / sizeof(VersionList[0]);

const char* VersionLookupTable[7][20] =
{
//		0		1		2		3		4		5		6		7		8		9		10		11		12		13		14		15		16		17		18		19
	{	"",		"",		"1.0",	"",		"2.0",	"",		"2.2",	"3.0",	"3.1",	"3.2",	"",		"3.3",	"3.4",	"4.0",	"4.1",	"4.2",	"4.3",	"4.3",	"",		"4.3"	},
	{	"",		"1.0",	"",		"2.0",	"",		"",		"2.2",	"3.0",	"3.1",	"3.2",	"",		"3.3",	"3.4",	"4.0",	"4.1",	"4.2",	"4.3",	"4.3"	"",		"4.3"	},
	{	"",		"",		"1.0",	"",		"2.0",	"2.1",	"2.2",	"3.0",	"3.1",	"3.2",	"",		"3.3",	"3.4",	"4.0",	"4.1",	"4.2",	"4.3",	"4.3",	"",		"4.3"	},
	{	"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		""		},
	{	"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		""		},
	{	"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		""		},
	{	"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"3.3",	"",		"3.5",	"",		"4.1",	"4.2",	"4.3",	"",		"",		""		},
};

u32 WaitButtons(void);
static u32 gPriiloaderSize = 0;
static bool gForcedInstall = false;

u32 be32(const u8 *p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

u64 be64(const u8 *p)
{
	return ((u64)be32(p) << 32) | be32(p + 4);
}

static inline void DecEncTxtBuffer(char* buffer)
{
	u32 key = 0x73B5DBFA;
	s32 i;

	for (i = 0; i < 0x100; i++)
	{
		buffer[i] ^= key & 0xFF;
		key = (key << 1) | (key >> 31);
	}
}

u64 get_title_ios(u64 title) {
	s32 ret, fd;
	static char filepath[256] ATTRIBUTE_ALIGN(32);	
	
	// Check to see if title exists
	if (ES_GetDataDir(title, filepath) >= 0 ) {
		u32 tmd_size = 0;
		static u8 tmd_buf[MAX_SIGNED_TMD_SIZE] ATTRIBUTE_ALIGN(32);
	
		ret = ES_GetStoredTMDSize(title, &tmd_size);
		if (ret < 0){
			// If we fail to use the ES function, try reading manually
			// This is a workaround added since some IOS (like 21) don't like our
			// call to ES_GetStoredTMDSize
			
			//printf("Error! ES_GetStoredTMDSize: %d\n", ret);
					
			sprintf(filepath, "/title/%08x/%08x/content/title.tmd", TITLE_UPPER(title), TITLE_LOWER(title));
			
			ret = ISFS_Open(filepath, ISFS_OPEN_READ);
			if (ret <= 0)
			{
				//printf("Error! ISFS_Open (ret = %d)\n", ret);
				return 0;
			}
			
			fd = ret;
			
			ret = ISFS_Seek(fd, 0x184, 0);
			if (ret < 0)
			{
				//printf("Error! ISFS_Seek (ret = %d)\n", ret);
				return 0;
			}
			
			ret = ISFS_Read(fd,tmd_buf,8);
			if (ret < 0)
			{
				//printf("Error! ISFS_Read (ret = %d)\n", ret);
				return 0;
			}
			
			ret = ISFS_Close(fd);
			if (ret < 0)
			{
				//printf("Error! ISFS_Close (ret = %d)\n", ret);
				return 0;
			}
			
			return be64(tmd_buf);
			
		} else {
			// Normal versions of IOS won't have a problem, so we do things the "right" way.
			
			// Some of this code adapted from bushing's title_lister.c
			signed_blob *s_tmd = (signed_blob *)tmd_buf;
			ret = ES_GetStoredTMD(title, s_tmd, tmd_size);
			if (ret < 0){
				//printf("Error! ES_GetStoredTMD: %d\n", ret);
				return -1;
			}
			tmd *t = SIGNATURE_PAYLOAD(s_tmd);
			return t->sys_version;
		}
		
		
	} 
	return 0;
}

static bool GetRegionFromTXT(char* region)
{
	u32 size = 0;
	*region = 0;
	char* buffer = (char*)NANDLoadFile("/title/00000001/00000002/data/setting.txt", &size);

	if (!buffer)
		return false;

	DecEncTxtBuffer(buffer);
	
	char* current = strstr(buffer, "AREA");

	if(current)
	{
		char* start = strchr(current, '=');
		char* end = strchr(current, '\n');

		if (start && end)
		{
			start++;
			
			if (!strncmp(start, "JPN", 3))
				*region = 'J';
			else if (!strncmp(start, "USA", 3))
				*region = 'U';
			else if (!strncmp(start, "EUR", 3))
				*region = 'E';
			else if (!strncmp(start, "KOR", 3))
				*region = 'K';
			
			if (*region != 0)
			{
				free(buffer);
				return true;
			}	
		}
	}
	else
	{
		printf("Error! GetRegionFromTXT: Item AREA not found!\n");
	}

	free(buffer);
	return false;
}

s32 GetSysMenuRegion(u16* version, char* region)
{
	u16 v = 0;
	s32 ret = Title_GetVersion(0x100000002LL, &v);

	if (ret < 0)
		return ret;

	if (version)
		*version = v;

	if(!GetRegionFromTXT(region))
	{
		printf("\nCouldn't find the region of this system\n");
		sleep(5);
		return -1;
	}

	return 0;
}

bool VersionIsOriginal(u16 version)
{
	s32 i;
	
	for (i = 0; i < VersionListSize; i++)
	{
		if (VersionList[i] == version)
			return true;
	}

	return false;
}

const char* GetSysMenuRegionString(const char region)
{
	switch (region)
	{
		case 'J': return "Japan (NTSC-J)";
		case 'U': return "USA (NTSC-U/C)";
		case 'E': return "Europe (PAL)";
		case 'K': return "Korea (NTSC-K)";
	}

	return "Unknown";
}

const char* GetSysMenuVersionString(u16 version)
{
	/*
	 * ==== ==== === version
	 * 0000 0010 0110 0000
	 *              = ==== region
	 */
	return VersionLookupTable[version % 32][version / 32];
};

static u32 GetSysMenuBootContent(void)
{
	static u32 cid = 0;
	
	if (!cid)
	{
		s32 ret;
		u32 size = 0;
		signed_blob *s_tmd = NULL;

		ret = ES_GetStoredTMDSize(0x100000002LL, &size);
		if (!size)
		{
			printf("Error! ES_GetStoredTMDSize failed (ret=%i)\n", ret);
			return 0;
		}

		s_tmd = memalign32(size);
		if (!s_tmd)
		{
			printf("Error! Memory allocation failed!\n");
			return 0;
		}

		ret = ES_GetStoredTMD(0x100000002LL, (u8*)s_tmd, size);
		if (ret < 0)
		{
			printf("Error! ES_GetStoredTMD failed (ret=%i)\n", ret);
			free(s_tmd);
			return 0;
		}

		tmd *p_tmd = SIGNATURE_PAYLOAD(s_tmd);

		for (int i = 0; i < p_tmd->num_contents; i++)
		{
			tmd_content* content = &p_tmd->contents[i];
			if (content->index == p_tmd->boot_index)
			{
				cid = content->cid;
				break;
			}
		}

		free(s_tmd);
		if (!cid)
		{
			printf("Error! Cannot find system menu boot content!\n");
			return 0;
		}
	}

	return cid;
}

bool GetSysMenuExecPath(char path[ISFS_MAXPATH], bool mainDOL)
{
	u32 cid = GetSysMenuBootContent();
	if (!cid) return false;

	if (mainDOL) cid |= 0x10000000;
	sprintf(path, "/title/00000001/00000002/content/%08x.app", cid);

	return true;
}

bool IsPriiloaderInstalled()
{
	char path[ISFS_MAXPATH] ATTRIBUTE_ALIGN(0x20);
	
	if (!GetSysMenuExecPath(path, true))
		return false;

	u32 size = 0;
	NANDGetFileSize(path, &size);

	return (size > 0);
}

static bool BackUpPriiloader()
{
	char path[ISFS_MAXPATH] ATTRIBUTE_ALIGN(0x20);
	
	if (!GetSysMenuExecPath(path, false))
		return false;

	u32 size = 0;
	s32 ret = NANDBackUpFile(path, "/tmp/priiload.app", &size);
	if (ret < 0)
	{
		printf("Error! NANDBackUpFile: Failed! (Error: %d)\n", ret);
		return false;
	}
						
	ret = NANDGetFileSize("/tmp/priiload.app", &gPriiloaderSize);

	return (gPriiloaderSize == size);
}

static bool MoveMenu(bool restore)
{
	ATTRIBUTE_ALIGN(0x20)
	char srcPath[ISFS_MAXPATH], dstPath[ISFS_MAXPATH];

	if (!GetSysMenuBootContent())
		return false;

	GetSysMenuExecPath(srcPath, restore);
	GetSysMenuExecPath(dstPath, !restore);

	u32 size = 0;
	s32 ret = NANDBackUpFile(srcPath, dstPath, &size);
	if (ret < 0)
	{
		printf("Error! NANDBackUpFile: Failed! (Error: %d)\n", ret);
		return false;
	}

	u32 checkSize = 0;
	ret = NANDGetFileSize(dstPath, &checkSize);

	return (checkSize == size);
}

static bool RestorePriiloader()
{
	char dstPath[ISFS_MAXPATH] ATTRIBUTE_ALIGN(0x20);

	if (!GetSysMenuExecPath(dstPath, false));

	u32 size = 0;
	s32 ret = NANDBackUpFile("/tmp/priiload.app", dstPath, &size);
	if (ret < 0)
	{
		printf("Error! NANDBackUpFile: Failed! (Error: %d)\n", ret);
		return false;
	}

	u32 checkSize = 0;
	ret = NANDGetFileSize(dstPath, &checkSize);

	return (checkSize == size && checkSize == gPriiloaderSize);
}

static void PrintCleanupResult(s32 result)
{
	
	if (result < 0)
	{
		switch (result)
		{
			case -102:
			{
				printf(" Acces denied.\n");
			} break;
			case -106:
			{
				printf(" Not found.\n");
			} break;
			default:
			{
				printf(" Error: %d\n", result);
			} break;
		}
	}
	else
	{
		printf(" OK!\n");
	}

	sleep(1);
}

static void CleanupPriiloaderLeftOvers(bool retain)
{
	if (!retain)
	{
		printf("\n\t\tCleanup Priiloader leftover files...\n");
		printf("\r\t\t>> Password file...");
		PrintCleanupResult(NANDDeleteFile("/title/00000001/00000002/data/password.txt"));
		printf("\r\t\t>> Settings file...");
		PrintCleanupResult(NANDDeleteFile("/title/00000001/00000002/data/loader.ini"));
		printf("\r\t\t>> Ticket...");
		PrintCleanupResult(NANDDeleteFile("/title/00000001/00000002/data/ticket"));
		printf("\r\t\t>> File: main.nfo...");
		PrintCleanupResult(NANDDeleteFile("/title/00000001/00000002/data/main.nfo"));
		printf("\r\t\t>> File: main.bin...");
		PrintCleanupResult(NANDDeleteFile("/title/00000001/00000002/data/main.bin"));
	}
	
	printf("\n\t\tRemoving Priiloader hacks...\n");
	
	printf("\r\t\t>> File: hacks_s.ini...");
	PrintCleanupResult(NANDDeleteFile("/title/00000001/00000002/data/hacks_s.ini"));
	printf("\r\t\t>> File: hacks.ini...");
	PrintCleanupResult(NANDDeleteFile("/title/00000001/00000002/data/hacks.ini"));
	printf("\r\t\t>> File: hacksh_s.ini...");
	PrintCleanupResult(NANDDeleteFile("/title/00000001/00000002/data/hacksh_s.ini"));
	printf("\r\t\t>> File: hackshas.ini...");
	PrintCleanupResult(NANDDeleteFile("/title/00000001/00000002/data/hackshas.ini"));

	if (retain)
	{
		printf("\n\t\tPriiloader hacks will be reset!\n");
		printf("\t\tRemember to set them again.\n");
	}	
}

static bool CompareHashes(bool priiloader)
{
	ATTRIBUTE_ALIGN(0x20)
	char srcPath[ISFS_MAXPATH], dstPath[ISFS_MAXPATH] = "/tmp/priiload.app";

	if (!GetSysMenuExecPath(srcPath, false))
		return false;

	if (!priiloader)
		GetSysMenuExecPath(dstPath, true);

	u32 sizeA = 0;
	u32 sizeB = 0;
	u8* dataA = NANDLoadFile(srcPath, &sizeA);
	if (!dataA)
		return false;

	u8* dataB = NANDLoadFile(dstPath, &sizeB);
	if (!dataB)
	{
		free(dataA);
		return false;
	}
	
	bool ret = (sizeA == sizeB) && !CompareHash(dataA, sizeA, dataB, sizeB);

	free(dataA);
	free(dataB);

	return ret;
}

/* 'WAD Header' structure */
typedef struct {
	/* Header length */
	u32 header_len;

	/* WAD type */
	u16 type;

	u16 padding;

	/* Data length */
	u32 certs_len;
	u32 crl_len;
	u32 tik_len;
	u32 tmd_len;
	u32 data_len;
	u32 footer_len;
} ATTRIBUTE_PACKED wadHeader;

/* Variables */
static u8 wadBuffer[BLOCK_SIZE] ATTRIBUTE_ALIGN(32);

s32 __Wad_GetTitleID(FILE *fp, wadHeader *header, u64 *tid)
{
	signed_blob *p_tik    = NULL;
	tik         *tik_data = NULL;

	u32 offset = 0;
	s32 ret;

	/* Ticket offset */
	offset += round_up(header->header_len, 64);
	offset += round_up(header->certs_len,  64);
	offset += round_up(header->crl_len,    64);

	/* Read ticket */
	ret = FSOPReadOpenFileA(fp, (void*)&p_tik, offset, header->tik_len);
	if (ret != 1)
		goto out;

	/* Ticket data */
	tik_data = (tik *)SIGNATURE_PAYLOAD(p_tik);

	/* Copy title ID */
	*tid = tik_data->titleid;

out:
	/* Free memory */
	free(p_tik);

	return ret;
}

void __Wad_FixTicket(signed_blob *p_tik)
{
	u8 *data = (u8 *)p_tik;
	u8 *ckey = data + 0x1F1;

	if (*ckey > 1) {
		/* Set common key */
		*ckey = 0;

		/* Fakesign ticket */
		Title_FakesignTik(p_tik);
	}
}

// Some of the safety checks can block region changing
// Entering the Konami code turns this true, so it will
// skip the problematic checks for region changing.
bool skipRegionSafetyCheck = false;

s32 Wad_Install(FILE *fp)
{
	SetPRButtons(false);
	wadHeader   *header  = NULL;
	signed_blob *p_certs = NULL, *p_crl = NULL, *p_tik = NULL, *p_tmd = NULL;

	tmd *tmd_data  = NULL;

	u32 cnt, offset = 0;
	int ret;
	u64 tid;
	bool retainPriiloader = false;
	bool cleanupPriiloader = false;

	printf("\t\t>> Reading WAD data...");
	fflush(stdout);
	
	ret = FSOPReadOpenFileA(fp, (void*)&header, offset, sizeof(wadHeader));
	if (ret != 1)
		goto err;
	else
		offset += round_up(header->header_len, 64);
	
	//Don't try to install boot2
	__Wad_GetTitleID(fp, header, &tid);
	
	if (tid == TITLE_ID(1, 1))
	{
		printf("\n    I can't let you do that Dave\n");
		ret = -999;
		goto out;
	}
	
	/* WAD certificates */
	ret = FSOPReadOpenFileA(fp, (void*)&p_certs, offset, header->certs_len);
	if (ret != 1)
		goto err;
	else
		offset += round_up(header->certs_len, 64);
		
	/* WAD crl */
	if (header->crl_len) {
		ret = FSOPReadOpenFileA(fp, (void*)&p_crl, offset, header->crl_len);
		if (ret != 1)
			goto err;
		else
			offset += round_up(header->crl_len, 64);
	}

	/* WAD ticket */
	ret = FSOPReadOpenFileA(fp, (void*)&p_tik, offset, header->tik_len);
	if (ret != 1)
		goto err;
	else
		offset += round_up(header->tik_len, 64);

	/* WAD TMD */
	ret = FSOPReadOpenFileA(fp, (void*)&p_tmd, offset, header->tmd_len);
	if (ret != 1)
		goto err;
	else
		offset += round_up(header->tmd_len, 64);

	Con_ClearLine();
	
	/* Get TMD info */
	
	tmd_data = (tmd *)SIGNATURE_PAYLOAD(p_tmd);
	
	if(TITLE_LOWER(tmd_data->sys_version) != 0 && isIOSstub(TITLE_LOWER(tmd_data->sys_version)))
	{
		printf("\n    This Title wants IOS%i but the installed version\n    is a stub.\n", TITLE_LOWER(tmd_data->sys_version));
		ret = -999;
		goto err;
	}
	
	if(get_title_ios(TITLE_ID(1, 2)) == tid)
	{
		if (( tmd_data->num_contents == 3) && (tmd_data->contents[0].type == 1 && tmd_data->contents[1].type == 0x8001 && tmd_data->contents[2].type == 0x8001))
		{
			printf("\n    I won't install a stub System Menu IOS\n");
			ret = -999;
			goto err;
		}
	}
	
	if(tid  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | 'E')) || tid  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | 'P')) || tid  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | 'J')) || tid  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | 'K')))
	{
		if ((tmd_data->num_contents == 3) && (tmd_data->contents[0].type == 1 && tmd_data->contents[1].type == 0x8001 && tmd_data->contents[2].type == 0x8001))
		{
			printf("\n    I won't install a stub EULA IOS\n");
			ret = -999;
			goto err;
		}
	}
	
	if(tid  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | 'E')) || tid  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | 'P')) || tid  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | 'J')) || tid  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | 'K')))
	{
		if ((tmd_data->num_contents == 3) && (tmd_data->contents[0].type == 1 && tmd_data->contents[1].type == 0x8001 && tmd_data->contents[2].type == 0x8001))
		{
			printf("\n    I won't install a stub rgsel IOS\n");
			ret = -999;
			goto err;
		}
	}
	if (tid == get_title_ios(TITLE_ID(0x10001, 0x48415858)) || tid == get_title_ios(TITLE_ID(0x10001, 0x4A4F4449)))
	{
		if ( ( tmd_data->num_contents == 3) && (tmd_data->contents[0].type == 1 && tmd_data->contents[1].type == 0x8001 && tmd_data->contents[2].type == 0x8001) )
		{
			printf("\n    Are you sure you wan't to install a stub HBC IOS?\n");
			printf("\n    Press A to continue.\n");
			printf("    Press B skip.");
		
			u32 buttons = WaitButtons();
		
			if (!(buttons & WPAD_BUTTON_A))
			{
				ret = -998;
				goto err;
			}
		}
	}
	
	if (tid == TITLE_ID(1, 2))
	{
		if (skipRegionSafetyCheck || gForcedInstall)
			goto skipChecks;

		char region = 0;
		u16 version = 0;

		GetSysMenuRegion(&version, &region);
		
		if (tmd_data->vwii_title)
		{
			printf("\n    I won't install a vWii SM by default.\n\n");
			printf("\n    If you're really sure what you're doing, next time\n");
			printf("    select your device using Konami...\n\n");

			ret = -999;
			goto err;
		}
		
		if(region == 0)
		{
			printf("\n    Unkown System menu region\n    Please check the site for updates\n");
			
			ret = -999;
			goto err;
		}

		if (!VersionIsOriginal(tmd_data->title_version))
		{
			printf("\n    I won't install an unkown SM versions by default.\n\n");
			printf("\n    Are you installing a tweaked system menu?\n");
			printf("\n    If you're really sure what you're doing, next time\n");
			printf("    select your device using Konami...\n\n");

			ret = -999;
			goto err;
		}

		if(region != RegionLookupTable[(tmd_data->title_version & 0x0F)])
		{
			printf("\n    I won't install the wrong regions SM by default.\n\n");
			printf("\n    Are you region changing?\n");
			printf("\n    If you're really sure what you're doing, next time\n");
			printf("    select your device using Konami...\n\n");
			
			ret = -999;
			goto err;
		}
skipChecks:
		if(tmd_data->title_version < 416)
		{
			if(boot2version == 4)
			{
				printf("\n    This version of the System Menu\n    is not compatible with your Wii\n");
				ret = -999;
				goto err;
			}
		}

		if (!gForcedInstall && AHBPROT_DISABLED && IsPriiloaderInstalled())
		{
			cleanupPriiloader = true;
			printf("\n    Priiloader is installed next to the system menu.\n\n");
			printf("    It is recommended to retain Priiloader as it can\n");
			printf("    protect your console from being bricked.\n\n");
			printf("    Press A to retain Priiloader or B to remove.");

			u32 buttons = WaitButtons();

			if ((buttons & WPAD_BUTTON_A))
			{
				retainPriiloader = (BackUpPriiloader() && CompareHashes(true));
				if (retainPriiloader)
				{
					SetPriiloaderOption(true);
					Con_ClearLine();
					printf("\r[+] Priiloader will be retained.\n");
					fflush(stdout);
				}
				else
				{
					Con_ClearLine();
					printf("\r    Couldn't backup Priiloader.\n");
					fflush(stdout);
					printf("\n    Press A to continue or B to skip");

					u32 buttons = WaitButtons();

					if (!(buttons & WPAD_BUTTON_A))
					{
						ret = -990;
						goto err;
					}
				}
			}

			if (!retainPriiloader)
			{
				SetPriiloaderOption(false);
				Con_ClearLine();
				printf("\r[+] Priiloader will be removed.\n");
				fflush(stdout);
			}
		}
		else
		{
			SetPriiloaderOption(false);
		}
	}
	
	if (gForcedInstall)
	{
		gForcedInstall = false;
		cleanupPriiloader = true;
	}

	/* Fix ticket */
	__Wad_FixTicket(p_tik);

	printf("\t\t>> Installing ticket...");
	fflush(stdout);

	/* Install ticket */
	ret = ES_AddTicket(p_tik, header->tik_len, p_certs, header->certs_len, p_crl, header->crl_len);
	if (ret < 0)
		goto err;

	Con_ClearLine();

	printf("\r\t\t>> Installing title...");
	fflush(stdout);

	/* Install title */
	ret = ES_AddTitleStart(p_tmd, header->tmd_len, p_certs, header->certs_len, p_crl, header->crl_len);
	if (ret < 0)
		goto err;
	
	/* Install contents */
	for (cnt = 0; cnt < tmd_data->num_contents; cnt++) 
	{
		tmd_content *content = &tmd_data->contents[cnt];

		u32 idx = 0, len;
		s32 cfd;

		Con_ClearLine();

		printf("\r\t\t>> Installing content #%02d...", content->cid);
		fflush(stdout);

		/* Encrypted content size */
		len = round_up(content->size, 64);

		/* Install content */
		cfd = ES_AddContentStart(tmd_data->title_id, content->cid);
		if (cfd < 0) 
		{
			ret = cfd;
			goto err;
		}

		/* Install content data */
		while (idx < len) 
		{
			u32 size;

			/* Data length */
			size = (len - idx);
			if (size > BLOCK_SIZE)
				size = BLOCK_SIZE;

			/* Read data */
			ret = FSOPReadOpenFile(fp, &wadBuffer, offset, size);
			if (ret != 1)
			{
				ES_AddContentFinish(cfd);
				goto err;
			}
				
			/* Install data */
			ret = ES_AddContentData(cfd, wadBuffer, size);
			if (ret < 0)
			{
				ret = ES_AddContentFinish(cfd);
				goto err;
			}

			/* Increase variables */
			idx    += size;
			offset += size;
		}

		/* Finish content installation */
		ret = ES_AddContentFinish(cfd);
		if (ret < 0)
			goto err;
	}
	
	Con_ClearLine();

	printf("\r\t\t>> Finishing installation...");
	fflush(stdout);

	/* Finish title install */
	ret = ES_AddTitleFinish();

	if (ret >= 0) 
	{
		printf(" OK!\n");

		if (retainPriiloader)
		{
			printf("\r\t\t>> Moving System Menu...");
			if (MoveMenu(false))
			{
				printf(" OK!\n");

				printf("\r\t\t>> Check System Menu executable hashes...");

				s32 restoreMenu = 0;
				
				if (CompareHashes(false))
				{
					printf(" OK!\n");
				}
				else
				{
					printf(" Failed!\n");
					restoreMenu = 1;
				}

				printf("\r\t\t>> Restore Priiloader...");
				if (!restoreMenu && RestorePriiloader())
				{
					printf(" OK!\n");
					printf("\r\t\t>> Check Priiloader executable hashes...");
					if (CompareHashes(true))
					{
						printf(" OK!\n");
					}
					else
					{
						printf(" Failed!\n");
						restoreMenu = 2;
					}
				}
				else
				{
					printf(" Failed!\n");
					restoreMenu = 2;
				}

				if (restoreMenu)
				{
					printf("\r\t\t>> Restore System Menu...");
					bool restored = true;
					switch (restoreMenu)
					{
						case 2:
						{
							restored = (MoveMenu(true) && CompareHashes(false));
						}
						case 1:
						{
							if (restored)
							{
								char path[ISFS_MAXPATH] ATTRIBUTE_ALIGN(0x20);
								GetSysMenuExecPath(path, true);
								NANDDeleteFile(path);
							}
						}
					}
					
					if (restored)
					{
						printf(" OK!\n");
					}
					else
					{
						printf(" Failed!\n");
						printf("\n\t\t>> Reinstalling System Menu...\n\n");
						sleep(3);
						printf("\t\t>> Priiloader will be removed!\n\n");

						gForcedInstall = true;
						cleanupPriiloader = false;
					}
				}
			}
			else
			{
				printf(" Failed!\n");
				printf("\n\t\t>> Priiloader will be removed!\n\n");
			}
		}

		if (cleanupPriiloader)
		{
			CleanupPriiloaderLeftOvers(retainPriiloader);
		}

		goto out;
	}

err:
	printf("\n    ERROR! (ret = %d)\n", ret);

	if (retainPriiloader)
		SetPriiloaderOption(false);

	if (ret == 0)
		ret = -996;
	/* Cancel install */
	ES_AddTitleCancel();

out:
	/* Free memory */
	free(header);
	free(p_certs);
	free(p_crl);
	free(p_tik);
	free(p_tmd);

	if (gForcedInstall)
		return Wad_Install(fp);
	
	SetPRButtons(true);
	return ret;
}

s32 Wad_Uninstall(FILE *fp)
{
	SetPRButtons(false);
	wadHeader *header   = NULL;
	tikview   *viewData = NULL;

	u64 tid;
	u32 viewCnt;
	int ret;

	printf("\t\t>> Reading WAD data...");
	fflush(stdout);

	/* WAD header */
	ret = FSOPReadOpenFileA(fp, (void*)&header, 0, sizeof(wadHeader));
	if (ret != 1)
	{
		printf(" ERROR! (ret = %d)\n", ret);
		goto out;
	}

	/* Get title ID */
	ret =  __Wad_GetTitleID(fp, header, &tid);
	if (ret < 0) {
		printf(" ERROR! (ret = %d)\n", ret);
		goto out;
	}
	//Assorted Checks
	if (TITLE_UPPER(tid) == 1 && get_title_ios(TITLE_ID(1, 2)) == 0)
	{
		printf("\n    I can't determine the System Menus IOS\nDeleting system titles is disabled\n");
		ret = -999;
		goto out;
	}
	if (tid == TITLE_ID(1, 1))
	{
		printf("\n    I won't try to uninstall boot2\n");
		ret = -999;
		goto out;
	}
	if (tid == TITLE_ID(1, 2))
	{
		printf("\n    I won't uninstall the System Menu\n");
		ret = -999;
		goto out;
	}
	if(get_title_ios(TITLE_ID(1, 2)) == tid)
	{
		printf("\n    I won't uninstall the System Menus IOS\n");
		ret = -999;
		goto out;
	}
	if (tid == get_title_ios(TITLE_ID(0x10001, 0x48415858)) || tid == get_title_ios(TITLE_ID(0x10001, 0x4A4F4449)))
	{
		printf("\n    This is the HBCs IOS, uninstalling will break the HBC!\n");
		printf("\n    Press A to continue.\n");
		printf("    Press B skip.");
		
		u32 buttons = WaitButtons();
		
		if (!(buttons & WPAD_BUTTON_A))
		{
			ret = -998;
			goto out;
		}
	}
	
	char region = 0;
	GetSysMenuRegion(NULL, &region);
	
	if((tid  == TITLE_ID(0x10008, 0x48414B00 | 'E') || tid  == TITLE_ID(0x10008, 0x48414B00 | 'P') || tid  == TITLE_ID(0x10008, 0x48414B00 | 'J') || tid  == TITLE_ID(0x10008, 0x48414B00 | 'K') 
		|| (tid  == TITLE_ID(0x10008, 0x48414C00 | 'E') || tid  == TITLE_ID(0x10008, 0x48414C00 | 'P') || tid  == TITLE_ID(0x10008, 0x48414C00 | 'J') || tid  == TITLE_ID(0x10008, 0x48414C00 | 'K'))) && region == 0)
	{
		printf("\n    Unkown SM region\n    Please check the site for updates\n");
		ret = -999;
		goto out;
	}
	if(tid  == TITLE_ID(0x10008, 0x48414B00 | region))
	{
		printf("\n    I won't uninstall the EULA\n");
		ret = -999;
		goto out;
	}	
	if(tid  == TITLE_ID(0x10008, 0x48414C00 | region))
	{
		printf("\n    I won't uninstall rgsel\n");
		ret = -999;
		goto out;
	}	
	if(tid  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | region)))
	{
		printf("\n    I won't uninstall the EULAs IOS\n");
		ret = -999;
		goto out;
	}	
	if(tid  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | region)))
	{
		printf("\n    I won't uninstall the rgsel IOS\n");
		ret = -999;
		goto out;
	}

	Con_ClearLine();

	printf("\t\t>> Deleting tickets...");
	fflush(stdout);

	/* Get ticket views */
	ret = Title_GetTicketViews(tid, &viewData, &viewCnt);
	if (ret < 0)
		printf(" ERROR! (ret = %d)\n", ret);

	/* Delete tickets */
	if (ret >= 0) {
		u32 cnt;

		/* Delete all tickets */
		for (cnt = 0; cnt < viewCnt; cnt++) {
			ret = ES_DeleteTicket(&viewData[cnt]);
			if (ret < 0)
				break;
		}

		if (ret < 0)
			printf(" ERROR! (ret = %d\n", ret);
		else
			printf(" OK!\n");
	}

	printf("\t\t>> Deleting title contents...");
	fflush(stdout);

	/* Delete title contents */
	ret = ES_DeleteTitleContent(tid);
	if (ret < 0)
		printf(" ERROR! (ret = %d)\n", ret);
	else
		printf(" OK!\n");


	printf("\t\t>> Deleting title...");
	fflush(stdout);

	/* Delete title */
	ret = ES_DeleteTitle(tid);
	if (ret < 0)
		printf(" ERROR! (ret = %d)\n", ret);
	else
		printf(" OK!\n");

out:
	/* Free memory */
	free(header);

	SetPRButtons(true);
	return ret;
}
