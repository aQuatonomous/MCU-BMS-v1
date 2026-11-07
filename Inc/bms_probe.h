#ifndef BMS_PROBE_H
#define BMS_PROBE_H

#include <stdint.h>

/* READ-ONLY first contact with the BQ76952 over I2C.
 *
 * Only BQ direct-command registers below 0x3E are read (register-pointer write,
 * repeated START, read). Nothing is ever written to the BQ: no 0x3E/0x40/0x60
 * subcommand or data-memory access, no FET control. PC0-PC5 are not touched.
 *
 * Results are kept in globals for inspection over SWD; each register is read as
 * 4 raw bytes so the data can be decoded afterwards whether the part is running
 * with or without CRC on its I2C replies. */

#define BMS_I2C_ADDR7   0x08U     /* 0x10 in TI's 8-bit notation */

/* Direct commands read (see TI BQ769x2 TRM). */
#define BMS_NCMDS  26U
extern const uint8_t g_bms_cmds[BMS_NCMDS];

typedef struct {
    uint32_t pass_count;          /* completed scan passes                     */
    uint32_t idle_levels;         /* bit0 SCL, bit1 SDA before first transfer (3 = both high) */
    uint32_t primary_status;      /* i2c_status_t of the last probe of BMS_I2C_ADDR7 */
    uint32_t found[4];            /* bitmap of addresses that ACKed in the fallback scan (bit n of word n/32) */
    uint32_t status[BMS_NCMDS];   /* i2c_status_t per register read            */
    uint8_t  raw[BMS_NCMDS][4];   /* 4 raw bytes per register                  */
} bms_dbg_t;

extern volatile bms_dbg_t g_bms_dbg;

void bms_probe_init(void);
void bms_probe_pass(void);        /* one full read pass (~tens of ms) */

#endif /* BMS_PROBE_H */
