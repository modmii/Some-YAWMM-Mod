#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ogcsys.h>
#include <ogc/es.h>
#include <ogc/aes.h>

#include "title.h"
#include "nand.h"
#include "sha1.h"
#include "utils.h"
#include "otp.h"
#include "malloc.h"

s32 Title_ZeroSignature(signed_blob *p_sig)
{
	u8 *ptr = (u8 *)p_sig;

	/* Fill signature with zeroes */
	memset(ptr + 4, 0, SIGNATURE_SIZE(p_sig) - 4);

	return 0;
}

s32 Title_FakesignTik(signed_blob *p_tik)
{
	tik *tik_data = NULL;
	u16 fill;

	/* Zero signature */
	Title_ZeroSignature(p_tik);

	/* Ticket data */
	tik_data = (tik *)SIGNATURE_PAYLOAD(p_tik);

	for (fill = 0; fill < USHRT_MAX; fill++) {
		sha1 hash;

		/* Modify ticket padding field */
		tik_data->padding = fill;

		/* Calculate hash */
		SHA1((u8 *)tik_data, sizeof(tik), hash);

		/* Found valid hash */
		if (!hash[0])
			return 0;
	}

	return -1;
}

s32 Title_FakesignTMD(signed_blob *p_tmd)
{
	tmd *tmd_data = NULL;
	u16 fill;

	/* Zero signature */
	Title_ZeroSignature(p_tmd);

	/* TMD data */
	tmd_data = (tmd *)SIGNATURE_PAYLOAD(p_tmd);

	for (fill = 0; fill < USHRT_MAX; fill++) {
		sha1 hash;

		/* Modify TMD fill field */
		tmd_data->fill3 = fill;

		/* Calculate hash */
		SHA1((u8 *)tmd_data, TMD_SIZE(tmd_data), hash);

		/* Found valid hash */
		if (!hash[0])
			return 0;
	}

	return -1;
}

s32 Title_GetList(u64 **outbuf, u32 *outlen)
{
	u64 *titles = NULL;

	u32 nb_titles;
	s32 ret;

	/* Get number of titles */
	ret = ES_GetNumTitles(&nb_titles);
	if (ret < 0)
		return ret;


	/* Allocate memory */
	titles = memalign32(nb_titles * sizeof(u64));
	if (!titles)
		return -1;

	/* Get titles */
	ret = ES_GetTitles(titles, nb_titles);
	if (ret < 0)
		goto err;

	/* Set values */
	*outbuf = titles;
	*outlen = nb_titles;

	return 0;

err:
	/* Free memory */
	free(titles);

	return ret;
}

s32 Title_GetTicketViews(u64 tid, tikview **outbuf, u32 *outlen)
{
	tikview *views = NULL;

	u32 nb_views;
	s32 ret;

	/* Get number of ticket views */
	ret = ES_GetNumTicketViews(tid, &nb_views);
	if (ret < 0)
		return ret;

	/* Allocate memory */
	views = memalign32(sizeof(tikview) * nb_views);
	if (!views)
		return -1;

	/* Get ticket views */
	ret = ES_GetTicketViews(tid, views, nb_views);
	if (ret < 0)
		goto err;

	/* Set values */
	*outbuf = views;
	*outlen = nb_views;

	return 0;

err:
	/* Free memory */
	free(views);

	return ret;
}

s32 Title_GetTMD(u64 tid, signed_blob **outbuf, u32 *outlen)
{
	void *p_tmd = NULL;

	u32 len;
	s32 ret;

	/* Get TMD size */
	ret = ES_GetStoredTMDSize(tid, &len);
	if (ret < 0)
		return ret;

	/* Allocate memory */
	p_tmd = memalign32(len);
	if (!p_tmd)
		return -1;

	/* Read TMD */
	ret = ES_GetStoredTMD(tid, p_tmd, len);
	if (ret < 0)
		goto err;

	/* Set values */
	*outbuf = p_tmd;
	*outlen = len;

	return 0;

err:
	/* Free memory */
	free(p_tmd);

	return ret;
}

s32 Title_GetTMDView(u64 tid, tmd_view** outbuf, u32* outlen)
{
	s32 ret;
	u32 view_sz = 0;

	*outbuf = NULL;
	*outlen = 0;

	ret = ES_GetTMDViewSize(tid, &view_sz);
	if (ret < 0)
		return ret;

	tmd_view* view = memalign32(view_sz);
	if (!view)
		return -1;

	ret = ES_GetTMDView(tid, (u8*) view, view_sz);
	if (ret < 0)
		goto fail;

	*outbuf = view;
	*outlen = view_sz;
	return 0;

fail:
	free(view);
	return ret;
}

s32 Title_GetVersion(u64 tid, u16 *outbuf)
{
	signed_blob *p_tmd = NULL;
	tmd      *tmd_data = NULL;

	u32 len;
	s32 ret;

	/* Get title TMD */
	ret = Title_GetTMD(tid, &p_tmd, &len);
	if (ret < 0)
		return ret;

	/* Retrieve TMD info */
	tmd_data = (tmd *)SIGNATURE_PAYLOAD(p_tmd);

	/* Set values */
	*outbuf = tmd_data->title_version;

	/* Free memory */
	free(p_tmd);

	return 0;
}

s32 Title_GetSysVersion(u64 tid, u64 *outbuf)
{
	signed_blob *p_tmd = NULL;
	tmd      *tmd_data = NULL;

	u32 len;
	s32 ret;

	/* Get title TMD */
	ret = Title_GetTMD(tid, &p_tmd, &len);
	if (ret < 0)
		return ret;

	/* Retrieve TMD info */
	tmd_data = (tmd *)SIGNATURE_PAYLOAD(p_tmd);

	/* Set values */
	*outbuf = tmd_data->sys_version;

	/* Free memory */
	free(p_tmd);

	return 0;
}

s32 Title_GetSize(u64 tid, u32 *outbuf)
{
	signed_blob *p_tmd = NULL;
	tmd      *tmd_data = NULL;

	u32 cnt, len, size = 0;
	s32 ret;

	/* Get title TMD */
	ret = Title_GetTMD(tid, &p_tmd, &len);
	if (ret < 0)
		return ret;

	/* Retrieve TMD info */
	tmd_data = (tmd *)SIGNATURE_PAYLOAD(p_tmd);

	/* Calculate title size */
	for (cnt = 0; cnt < tmd_data->num_contents; cnt++) {
		tmd_content *content = &tmd_data->contents[cnt];

		/* Add content size */
		size += content->size;
	}

	/* Set values */
	*outbuf = size;

	/* Free memory */
	free(view);

	return 0;
}

s32 Title_GetIOSVersions(u8 **outbuf, u32 *outlen)
{
	u8  *buffer = NULL;
	u64 *list   = NULL;

	u32 count, cnt, idx;
	s32 ret;

	/* Get title list */
	ret = Title_GetList(&list, &count);
	if (ret < 0)
		return ret;

	/* Count IOS */
	for (cnt = idx = 0; idx < count; idx++) {
		u32 tidh = (list[idx] >> 32);
		u32 tidl = (list[idx] &  0xFFFFFFFF);

		/* Title is IOS */
		if ((tidh == 0x1) && (tidl >= 3) && (tidl <= 255))
			cnt++;
	}

	/* Allocate memory */
	buffer = memalign32(cnt);
	if (!buffer) {
		ret = -1;
		goto out;
	}

	/* Copy IOS */
	for (cnt = idx = 0; idx < count; idx++) {
		u32 tidh = (list[idx] >> 32);
		u32 tidl = (list[idx] &  0xFFFFFFFF);

		/* Title is IOS */
		if ((tidh == 0x1) && (tidl >= 3) && (tidl <= 255))
			buffer[cnt++] = (u8)(tidl & 0xFF);
	}

	/* Set values */
	*outbuf = buffer;
	*outlen = cnt;

	goto out;

out:
	/* Free memory */
	free(list);

	return ret;
}

s32 Title_GetSharedContents(SharedContent** out, u32* count)
{
	if (!out || !count) return false;

	u32 size;
	SharedContent* buf = (SharedContent*)NANDLoadFile("/shared1/content.map", &size);

	if (!buf)
		return (s32)size;

	else if (size % sizeof(SharedContent) != 0) {
		free(buf);
		return -996;
	}

	*out = buf;
	*count = size / sizeof(SharedContent);

	return 0;
}

bool Title_SharedContentPresent(tmd_content* content, SharedContent shared[], u32 count)
{
	if (!shared || !content || !count)
		return false;

	if (!(content->type & 0x8000))
		return false;

	for (SharedContent* s_content = shared; s_content < shared + count; s_content++)
	{
		if (memcmp(s_content->hash, content->hash, sizeof(sha1)) == 0)
			return true;
	}

	return false;
}

bool Title_GetcIOSInfo(int IOS, cIOSInfo* out)
{
	u64 titleID = 0x0000000100000000ULL | IOS;
	tmd_view* view = NULL;
	u32 view_size = 0;
	char path[ISFS_MAXPATH];
	u32 size;
	cIOSInfo* buf = NULL;

	s32 ret = Title_GetTMDView(titleID, &view, &view_size);
	if (ret < 0)
		return ret;

	u32 content0 = 0;
	for (int i = 0; i < view->num_contents; i++)
	{
		if (view->contents[i].index == 0) {
			content0 = view->contents[i].cid;
			break;
		}
	}
	free(view);

	sprintf(path, "/title/00000001/%08x/content/%08x.app", IOS, content0);
	buf = (cIOSInfo*)NANDLoadFile(path, &size);

	if (!buf || size != 0x40 || buf->hdr_magic != CIOS_INFO_MAGIC || buf->hdr_version != CIOS_INFO_VERSION)
		goto fail;

	*out = *buf;
	free(buf);
	return true;

fail:
	free(buf);
	return false;
}

#if 0
void Title_GetFreeSpace(u32* free, s32* user_free)
{
	// Based off Dolphin Emulator's code. Cool stuff
	static const char* const userDirs[10] = {
		"/meta", "/ticket",
		"/title/00010000", "/title/00010001",
		"/title/00010003", "/title/00010004", "/title/00010005",
		"/title/00010006", "/title/00010007", "/shared2/title" /* Wtf */
	};

	u32 stats[8];
	ISFS_GetStats(stats);
	u32 cluster_size  = stats[0], // Can just hardcode 16384 here but eh
		free_clusters = stats[1],
		used_clusters = stats[2];

	*free = free_clusters * cluster_size;

	static const u32 user_blocks_max = 2176, // 1 block = 128KiB (131072 bytes)
	               user_clusters_max = user_blocks_max * 8; // 1 cluster = 16KiB (16384)

	u32 user_clusters_used = 0;
	for (int i = 0; i < 10; i++) {

		//                               Clusters   Inodes
		ret = ISFS_GetUsage(userDirs[i], &stats[0], &stats[1]);
		if (ret == 0)
			user_clusters_used += stats[0];
	}

	s32 user_clusters_free = user_clusters_max - user_clusters_used;
                *user_free = user_clusters_free * cluster_size;
}
#endif
__attribute__((aligned(0x10)))
aeskey WiiCommonKey, vWiiCommonKey;

void Title_SetupCommonKeys(void)
{
	static bool keys_ok = false;
	if (keys_ok)
		return;

	// Grab the Wii common key...
	otp_read(WiiCommonKey, offsetof(otp_t, common_key), sizeof(aeskey));

	// ...and decrypt the vWii common key with it.
	static const unsigned char vwii_key_enc_bin[0x10] = { 0x6e, 0x18, 0xdb, 0x23, 0x84, 0x7c, 0xba, 0x6c, 0x19, 0x31, 0xa4, 0x17, 0x9b, 0xaf, 0x8e, 0x09 };
	unsigned char iv[0x10] = {};

	memcpy(vWiiCommonKey, vwii_key_enc_bin, sizeof(vwii_key_enc_bin));
	AES_Decrypt(WiiCommonKey, sizeof(aeskey), iv, sizeof(iv), vWiiCommonKey, vWiiCommonKey, sizeof(aeskey));

	keys_ok = true;
	return;
};
