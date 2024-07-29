#ifndef _WAD_H_
#define _WAD_H_

/* Prototypes */
s32 Wad_Install(FILE* fp);
s32 Wad_Uninstall(FILE* fp);
const char* wad_strerror(int ec);

s32 GetSysMenuRegion(u16* version, char* region);
bool VersionIsOriginal(u16 version);
const char* GetSysMenuRegionString(const char region);
const char* GetSysMenuVersionString(u16 version);
bool IsPriiloaderInstalled();

#endif
