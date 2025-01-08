#ifndef _TITLE_H_
#define _TITLE_H_

#include <ogc/es.h>

/* Constants */
#define BLOCK_SIZE	0x4000

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
_Static_assert(sizeof(cIOSInfo) == 0x40, "cIOSInfo struct size wrong");

/* Variables */
extern aeskey WiiCommonKey, vWiiCommonKey;

/* Prototypes */
s32 Title_ZeroSignature(signed_blob *);
s32 Title_FakesignTik(signed_blob *);
s32 Title_FakesignTMD(signed_blob *);
s32 Title_GetList(u64 **, u32 *);
s32 Title_GetTicketViews(u64, tikview **, u32 *);
s32 Title_GetTMDView(u64, tmd_view **, u32 *);
s32 Title_GetTMD(u64, signed_blob **, u32 *);
s32 Title_GetVersion(u64, u16 *);
s32 Title_GetSysVersion(u64, u64 *);
s32 Title_GetSize(u64, u32 *);
s32 Title_GetIOSVersions(u8 **, u32 *);
s32 Title_GetSharedContents(SharedContent** out, u32* count);
bool Title_SharedContentPresent(tmd_content* content, SharedContent shared[], u32 count);
bool Title_GetcIOSInfo(int IOS, cIOSInfo*);

void Title_SetupCommonKeys(void);

#endif
