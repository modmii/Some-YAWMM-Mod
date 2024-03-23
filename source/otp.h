#ifndef __OTP_H__
#define __OTP_H__

#include <gctypes.h>

#define OTP_BANK_SIZE    0x80

// I want to add a structure for Bank 1 but the only thing that matters to me is the vWii common key in there.
#define VWII_COMMON_KEY_OFFSET 0x50
typedef enum
{
    BANK_WII,
    BANK_WIIU,
} OTPBank;

struct OTP
{
    u8 boot1_hash[20];
    u8 common_key[16];
    u8 ng_id[4];
    union { // first two bytes of nand_hmac overlap last two bytes of ng_priv
        struct {
            u8 ng_priv[30];
            u8 _wtf1[18];
        };
        struct {
            u8 _wtf2[28];
            u8 nand_hmac[20];
        };
    };
    u8 nand_key[16];
    u8 rng_key[16];
    u32 unk1;
    u32 unk2; // 0x00000007
};
_Static_assert(sizeof(struct OTP) == OTP_BANK_SIZE, "OTP struct size incorrect!");

u8 otp_read(void *dst, OTPBank bank, u8 offset, u8 size);

#endif /* __OTP_H__ */
