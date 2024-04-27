/*
        mini - a Free Software replacement for the Nintendo/BroadOn IOS.
        SEEPROM support

Copyright (C) 2008, 2009    Sven Peter <svenpeter@gmail.com>

# This code is licensed to you under the terms of the GNU GPL, version 2;
# see http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

#ifndef __MINI_SEEPROM_H__
#define __MINI_SEEPROM_H__

#define SEEPROM_SIZE    0x100

typedef struct
{
    union {
        struct {
            u8 boot2version;
            u8 unknown1;
            u8 unknown2;
            u8 pad;
            u32 update_tag;
        };
        u8 data[8];
    };
    u16 checksum; // sum of data[] elements?
} __attribute__((packed)) eep_boot2_ctr_t;

typedef struct
{
    union {
        u32 nand_gen; // matches offset 0x8 in nand SFFS blocks
        u8 data[4];
    };
    u16 checksum; // sum of data[] elements?
} __attribute__((packed)) eep_nand_ctr_t;

struct SEEPROM
{
    u32 ms_id; // 0x00000002
    u32 ca_id; // 0x00000001
    u32 ng_key_id;
    u8 ng_sig[60];
    eep_boot2_ctr_t boot2_counters[2];
    eep_nand_ctr_t nand_counters[3]; // current slot rotates on each write
    u8 pad0[6];
    u8 korean_key[16];
    u8 pad1[116];
    u16 prng_seed[2]; // u32 with lo word stored first, incremented every time IOS starts. Used with the PRNG key to setup IOS's PRNG (syscalls 73/74 etc.)
    u8 pad2[4];
};
_Static_assert(sizeof(struct SEEPROM) == SEEPROM_SIZE, "SEEPROM struct size incorrect!");

u16 seeprom_read(void *dst, u16 offset, u16 size);
// u16 seeprom_write(const void *src, u16 offset, u16 size);

#endif /* __MINI_SEEPROM_H__ */
