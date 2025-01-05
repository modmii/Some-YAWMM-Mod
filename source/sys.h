#ifndef _SYS_H_
#define _SYS_H_

#define IS_WIIU (*(vu16*)0xCD8005A0 == 0xCAFE)

extern u32 boot2version;

/* Prototypes */
bool isIOSstub(u8 ios_number);
bool tmdIsStubIOS(tmd*);
bool loadIOS(int ios);
bool ES_CheckHasKoreanKey(void);
void Sys_Init(void);
void Sys_Reboot(void);
void Sys_Shutdown(void);
s32  Sys_GetCerts(signed_blob **, u32 *);
void SetPRButtons(bool enabled);

#endif
