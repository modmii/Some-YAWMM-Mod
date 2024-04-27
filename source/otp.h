#ifndef __OTP_H__
#define __OTP_H__

#define OTP_SIZE    0x80

typedef struct
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
} otp_t;

u8 otp_read(void *dst, u8 offset, u8 size);

#endif /* __OTP_H__ */
