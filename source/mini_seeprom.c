/*
    mini - a Free Software replacement for the Nintendo/BroadOn IOS.
    SEEPROM support

Copyright (C) 2008, 2009    Sven Peter <svenpeter@gmail.com>
Copyright (C) 2008, 2009    Haxx Enterprises <bushing@gmail.com>
Copyright (C) 2008, 2009    Hector Martin "marcan" <marcan@marcansoft.com>
Copyright (C) 2008, 2009    John Kelley <wiidev@kelley.ca>
Copyright (C) 2020          Pablo Curiel "DarkMatterCore" <pabloacurielz@gmail.com>

# This code is licensed to you under the terms of the GNU GPL, version 2;
# see http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

#include <ogc/machine/processor.h>
#include <unistd.h>
#include <string.h>

#include "mini_seeprom.h"

#define HW_REG_BASE     0xd800000
#define HW_GPIO1OUT     (HW_REG_BASE + 0x0e0)
#define HW_GPIO1IN      (HW_REG_BASE + 0x0e8)

#define HW_SEEPROM_BLK_SIZE 2
#define HW_SEEPROM_BLK_CNT  (SEEPROM_SIZE / HW_SEEPROM_BLK_SIZE)

#define eeprom_delay()  usleep(5)

enum {
    GP_EEP_CS = 0x000400,
    GP_EEP_CLK = 0x000800,
    GP_EEP_MOSI = 0x001000,
    GP_EEP_MISO = 0x002000
};

static void seeprom_send_bits(u16 value, u8 bits)
{
    if (!bits || bits > 16) return;

    while(bits--)
    {
        if (value & (1 << bits))
        {
            mask32(HW_GPIO1OUT, 0, GP_EEP_MOSI);
        } else {
            mask32(HW_GPIO1OUT, GP_EEP_MOSI, 0);
        }

        eeprom_delay();

        mask32(HW_GPIO1OUT, 0, GP_EEP_CLK);
        eeprom_delay();

        mask32(HW_GPIO1OUT, GP_EEP_CLK, 0);
        eeprom_delay();
    }
}

static u16 seeprom_recv_bits(u8 bits)
{
    if (!bits || bits > 16) return 0;

    int res = 0;

    while(bits--)
    {
        res <<= 1;

        mask32(HW_GPIO1OUT, 0, GP_EEP_CLK);
        eeprom_delay();

        mask32(HW_GPIO1OUT, GP_EEP_CLK, 0);
        eeprom_delay();

        res |= !!(read32(HW_GPIO1IN) & GP_EEP_MISO);
    }

    return (u16)res;
}

u16 seeprom_read(void *dst, u16 offset, u16 size)
{
    /*
     * WiiUBrew told me that you interact with the SEEPROM the exact same way you do on Wii.
     * However the contents are way different. Like there's absolutely no vWii stuff here.
     */
    if (read16(0xCD8005A0) == 0xCAFE) return 0;

    if (!dst || offset >= SEEPROM_SIZE || !size || (offset + size) > SEEPROM_SIZE) return 0;

    u16 cur_offset = 0;

    u8 *ptr = (u8*)dst;
    u8 val[HW_SEEPROM_BLK_SIZE] = {0};

    // Calculate block offsets and sizes
    u8 start_addr = (u8)(offset / HW_SEEPROM_BLK_SIZE);
    u8 start_addr_offset = (u8)(offset % HW_SEEPROM_BLK_SIZE);

    u8 end_addr = (u8)((offset + size) / HW_SEEPROM_BLK_SIZE);
    u8 end_addr_size = (u8)((offset + size) % HW_SEEPROM_BLK_SIZE);

    if (!end_addr_size)
    {
        end_addr--;
        end_addr_size = HW_SEEPROM_BLK_SIZE;
    }

    if (end_addr == start_addr) end_addr_size -= start_addr_offset;

    mask32(HW_GPIO1OUT, GP_EEP_CLK, 0);
    mask32(HW_GPIO1OUT, GP_EEP_CS, 0);
    eeprom_delay();

    for(u16 i = start_addr; i <= end_addr; i++)
    {
        if (cur_offset >= size) break;

        // Start command cycle
        mask32(HW_GPIO1OUT, 0, GP_EEP_CS);

        // Send read command + address
        seeprom_send_bits(0x600 | i, 11);

        // Receive data
        *((u16*)val) = seeprom_recv_bits(16);

        // End of command cycle
        mask32(HW_GPIO1OUT, GP_EEP_CS, 0);
        eeprom_delay();

        // Copy read data to destination buffer
        if (i == start_addr && start_addr_offset != 0)
        {
            // Handle unaligned read at start address
            memcpy(ptr + cur_offset, val + start_addr_offset, HW_SEEPROM_BLK_SIZE - start_addr_offset);
            cur_offset += (HW_SEEPROM_BLK_SIZE - start_addr_offset);
        } else
        if (i == end_addr && end_addr_size != HW_SEEPROM_BLK_SIZE)
        {
            // Handle unaligned read at end address
            memcpy(ptr + cur_offset, val, end_addr_size);
            cur_offset += end_addr_size;
        } else {
            // Normal read
            memcpy(ptr + cur_offset, val, HW_SEEPROM_BLK_SIZE);
            cur_offset += HW_SEEPROM_BLK_SIZE;
        }
    }

    return cur_offset;
}

#if 0
u16 seeprom_write(const void *src, u16 offset, u16 size)
{
    if (!src || offset >= SEEPROM_SIZE || !size || (offset + size) > SEEPROM_SIZE) return 0;

    u32 level = 0;
    u16 cur_offset = 0;

    const u8 *ptr = (const u8*)src;
    u8 val[HW_SEEPROM_BLK_SIZE] = {0};

    // Calculate block offsets and sizes
    u8 start_addr = (u8)(offset / HW_SEEPROM_BLK_SIZE);
    u8 start_addr_offset = (u8)(offset % HW_SEEPROM_BLK_SIZE);

    u8 end_addr = (u8)((offset + size) / HW_SEEPROM_BLK_SIZE);
    u8 end_addr_size = (u8)((offset + size) % HW_SEEPROM_BLK_SIZE);

    if (!end_addr_size)
    {
        end_addr--;
        end_addr_size = HW_SEEPROM_BLK_SIZE;
    }

    if (end_addr == start_addr) end_addr_size -= start_addr_offset;

    // Disable CPU interruptions
    _CPU_ISR_Disable(level);

    mask32(HW_GPIO1OUT, GP_EEP_CLK, 0);
    mask32(HW_GPIO1OUT, GP_EEP_CS, 0);
    eeprom_delay();

    // EWEN - Enable programming commands
    mask32(HW_GPIO1OUT, 0, GP_EEP_CS);
    seeprom_send_bits(0x4FF, 11);
    mask32(HW_GPIO1OUT, GP_EEP_CS, 0);
    eeprom_delay();

    for(u16 i = start_addr; i <= end_addr; i++)
    {
        if (cur_offset >= size) break;

        // Copy data to write from source buffer
        if ((i == start_addr && start_addr_offset != 0) || (i == end_addr && end_addr_size != HW_SEEPROM_BLK_SIZE))
        {
            // Read data from SEEPROM to handle unaligned writes

            // Start command cycle
            mask32(HW_GPIO1OUT, 0, GP_EEP_CS);

            // Send read command + address
            seeprom_send_bits(0x600 | i, 11);

            // Receive data
            *((u16*)val) = seeprom_recv_bits(16);

            // End of command cycle
            mask32(HW_GPIO1OUT, GP_EEP_CS, 0);
            eeprom_delay();

            if (i == start_addr && start_addr_offset != 0)
            {
                // Handle unaligned write at start address
                memcpy(val + start_addr_offset, ptr + cur_offset, HW_SEEPROM_BLK_SIZE - start_addr_offset);
                cur_offset += (HW_SEEPROM_BLK_SIZE - start_addr_offset);
            } else {
                // Handle unaligned write at end address
                memcpy(val, ptr + cur_offset, end_addr_size);
                cur_offset += end_addr_size;
            }
        } else {
            // Normal write
            memcpy(val, ptr + cur_offset, HW_SEEPROM_BLK_SIZE);
            cur_offset += HW_SEEPROM_BLK_SIZE;
        }

        // Start command cycle
        mask32(HW_GPIO1OUT, 0, GP_EEP_CS);

        // Send write command + address
        seeprom_send_bits(0x500 | i, 11);

        // Send data
        seeprom_send_bits(*((u16*)val), 16);

        // End of command cycle
        mask32(HW_GPIO1OUT, GP_EEP_CS, 0);
        eeprom_delay();

        // Wait until SEEPROM is ready (write cycle is self-timed so no clocking needed)
        mask32(HW_GPIO1OUT, 0, GP_EEP_CS);

        do {
            eeprom_delay();
        } while(!(read32(HW_GPIO1IN) & GP_EEP_MISO));

        mask32(HW_GPIO1OUT, GP_EEP_CS, 0);
        eeprom_delay();
    }

    // EWDS - Disable programming commands
    mask32(HW_GPIO1OUT, 0, GP_EEP_CS);
    seeprom_send_bits(0x400, 11);
    mask32(HW_GPIO1OUT, GP_EEP_CS, 0);
    eeprom_delay();

    // Enable CPU interruptions
    _CPU_ISR_Restore(level);

    return cur_offset;
}
#endif
