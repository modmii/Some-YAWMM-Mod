#ifndef _SYS_H_
#define _SYS_H_

/* /shared1/content.map entry */
typedef struct
{
	char filename[8];
	sha1 hash;
} ATTRIBUTE_PACKED SharedContent;

/* "cIOS build tag" */
enum
{
	CIOS_INFO_MAGIC = 0x1EE7C105,
	CIOS_INFO_VERSION = 1
};

typedef struct
{
	u32 hdr_magic;		// 0x1EE7C105
	u32 hdr_version;	// 1
	u32 cios_version;	// Eg. 11
	u32 ios_base;		// Eg. 60

	char name[16];
	char cios_version_str[16];

	char _padding[16];
} cIOSInfo;
// _Static_assert(sizeof(cIOSInfo) == 0x40, "Incorrect cIOSInfo struct size, do i really need to pack this..?");

#define IS_WIIU (*(vu16*)0xCD0005A0 == 0xCAFE)

extern u32 boot2version;

/* Prototypes */
bool isIOSstub(u8 ios_number);
bool tmdIsStubIOS(tmd*);
bool loadIOS(int ios);
bool ES_CheckHasKoreanKey(void);
void Sys_Init(void);
void Sys_Reboot(void);
void Sys_Shutdown(void);
void Sys_LoadMenu(void);
s32  Sys_GetCerts(signed_blob **, u32 *);
bool Sys_GetcIOSInfo(int IOS, cIOSInfo*);
s32  Sys_GetSharedContents(SharedContent** out, u32* count);
bool Sys_SharedContentPresent(tmd_content* content, SharedContent shared[], u32 count);
void SetPRButtons(bool enabled);

#endif
