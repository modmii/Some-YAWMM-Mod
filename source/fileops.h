#ifndef __FILEOPS_H__
#define __FILEOPS_H__

#include <sys/stat.h>
#include <dirent.h>
#include <ogcsys.h>

bool FSOPFileExists(const char* file);
bool FSOPFolderExists(const char* path);
size_t FSOPGetFileSizeBytes(const char* path);

void FSOPDeleteFile(const char* file);
void FSOPMakeFolder(const char* path);

s32 FSOPReadOpenFile(FILE* fp, void* buffer, u32 offset, u32 length);
s32 FSOPReadOpenFileA(FILE* fp, void** buffer, u32 offset, u32 length);

#endif
