#include <stdio.h>
#include <string.h>

#include "fileops.h"
#include "malloc.h"

static struct stat st;

bool FSOPFileExists(const char* file)
{
	return !stat(file, &st) && !S_ISDIR(st.st_mode);
}

bool FSOPFolderExists(const char* path)
{
	return !stat(path, &st) && S_ISDIR(st.st_mode);
}

size_t FSOPGetFileSizeBytes(const char* path)
{
	if (stat(path, &st) < 0) return 0;

	return st.st_size;
}

void FSOPDeleteFile(const char* file)
{
	if (FSOPFileExists(file))
		remove(file);
}

void FSOPMakeFolder(const char* path)
{
	if (FSOPFolderExists(path))
		return;

	char* pos = strchr(path, '/');
	s32 current = pos - path;
	current++;
	pos = strchr(path + current, '/');

	while (pos)
	{
		*pos = 0;
		mkdir(path, S_IREAD | S_IWRITE);
		*pos = '/';
		
		current = pos - path;
		current++;
		pos = strchr(path + current, '/');
	}

	mkdir(path, S_IREAD | S_IWRITE);
}

s32 FSOPReadOpenFile(FILE* fp, void* buffer, u32 offset, u32 length)
{
	fseek(fp, offset, SEEK_SET);
	return fread(buffer, length, 1, fp);
}

s32 FSOPReadOpenFileA(FILE* fp, void** buffer, u32 offset, u32 length)
{
	*buffer = memalign32(length);
	if (!*buffer)
		return -1;

	s32 ret = FSOPReadOpenFile(fp, *buffer, offset, length);
	if (ret <= 0)
	{
		free(*buffer);
		*buffer = NULL;
	}

	return ret;
}
