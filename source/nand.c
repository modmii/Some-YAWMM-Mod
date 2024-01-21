#include <stdio.h>
#include <stdarg.h>
#include <ogcsys.h>
#include <string.h>

#include "nand.h"
#include "malloc.h"
#include "fileops.h"

/* Buffer */
static u32 inbuf[8] ATTRIBUTE_ALIGN(32);
static bool gNandInitialized = false;


s32 Nand_Mount(nandDevice *dev)
{
	s32 fd, ret;

	/* Open FAT module */
	fd = IOS_Open("fat", 0);
	if (fd < 0)
		return fd;

	/* Mount device */
	ret = IOS_Ioctlv(fd, dev->mountCmd, 0, 0, NULL);

	/* Close FAT module */
	IOS_Close(fd);

	return ret;
}

s32 Nand_Unmount(nandDevice *dev)
{
	s32 fd, ret;

	/* Open FAT module */
	fd = IOS_Open("fat", 0);
	if (fd < 0)
		return fd;

	/* Unmount device */
	ret = IOS_Ioctlv(fd, dev->umountCmd, 0, 0, NULL);

	/* Close FAT module */
	IOS_Close(fd);

	return ret;
}

s32 Nand_Enable(nandDevice *dev)
{
	s32 fd, ret;

	/* Open /dev/fs */
	fd = IOS_Open("/dev/fs", 0);
	if (fd < 0)
		return fd;

	/* Set input buffer */
	inbuf[0] = dev->mode;

	/* Enable NAND emulator */
	ret = IOS_Ioctl(fd, 100, inbuf, sizeof(inbuf), NULL, 0);

	/* Close /dev/fs */
	IOS_Close(fd);

	return ret;
} 

s32 Nand_Disable(void)
{
	s32 fd, ret;

	/* Open /dev/fs */
	fd = IOS_Open("/dev/fs", 0);
	if (fd < 0)
		return fd;

	/* Set input buffer */
	inbuf[0] = 0;

	/* Disable NAND emulator */
	ret = IOS_Ioctl(fd, 100, inbuf, sizeof(inbuf), NULL, 0);

	/* Close /dev/fs */
	IOS_Close(fd);

	return ret;
}

bool NANDInitialize()
{
	if(!gNandInitialized)
	{
		if (ISFS_Initialize() == ISFS_OK)
			gNandInitialized = true;
	}

	return gNandInitialized;
}

u8* NANDReadFromFile(const char* path, u32 offset, u32 length, u32* size)
{
	*size = ISFS_EINVAL;

	if (NANDInitialize())
	{
		s32 fd = IOS_Open(path, 1);

		if (fd < 0)
		{
			*size = fd;
			return NULL;
		}

		if (!length)
			length = IOS_Seek(fd, 0, SEEK_END);

		u8* data = memalign64(length);
		if (!data)
		{
			*size = 0;
			IOS_Close(fd);
			return NULL;
		}

		*size = IOS_Seek(fd, offset, SEEK_SET);
		if (*size < 0)
		{
			IOS_Close(fd);
			free(data);
			return NULL;
		}

		*size = IOS_Read(fd, data, length);
		IOS_Close(fd);
		if (*size != length)
		{
			free(data);
			return NULL;
		}

		return data;
	}
	
	return NULL;
}

u8* NANDLoadFile(const char* path, u32* size)
{
	return NANDReadFromFile(path, 0, 0, size);
}

s32 NANDWriteFileSafe(const char* path, u8* data, u32 size)
{
	char tmpPath[ISFS_MAXPATH] ATTRIBUTE_ALIGN(64);

	NANDInitialize();

	u32 i;

	for (i = strlen(path); i > 0; --i)
	{
		if (path[i] == '/')
			break;
	}

	sprintf(tmpPath, "/tmp%s", path + i);

	s32 ret = ISFS_CreateFile(tmpPath, 0, 3, 3, 3);
	if (ret == -105)
	{
		ISFS_Delete(tmpPath);
		ret = ISFS_CreateFile(tmpPath, 0, 3, 3, 3);

		if (ret < 0)
			return ret;
	}
	else if (ret < 0)
	{
		return ret;
	}

	s32 fd = IOS_Open(tmpPath, 2);
	if (fd < 0)
		return fd;

	ret = IOS_Write(fd, data, size);


	IOS_Close(fd);
	if (ret != size)
		return ret - 3;

	if (strcmp(tmpPath, path))
		ret = ISFS_Rename(tmpPath, path);
	else
		ret = 0;

	return ret;
}

s32 NANDBackUpFile(const char* src, const char* dst, u32* size)
{
	NANDInitialize();
	u8* buffer = NANDLoadFile(src, size);
	if (!buffer)
		return *size;

	s32 ret = NANDWriteFileSafe(dst, buffer, *size);

	free(buffer);
	return ret;
}

s32 NANDGetFileSize(const char* path, u32* size)
{
	NANDInitialize();
	s32 fd = IOS_Open(path, 1);

	if (fd < 0)
	{
		*size = 0;
		return fd;
	}

	*size = IOS_Seek(fd, 0, SEEK_END);
	return IOS_Close(fd);
}

s32 NANDDeleteFile(const char* path)
{
	NANDInitialize();
	return ISFS_Delete(path);
}
