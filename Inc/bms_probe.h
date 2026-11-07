#ifndef BMS_PROBE_H
#define BMS_PROBE_H

#include <stdint.h>

/* READ-ONLY first contact with the BQ76952 over I2C.
 *
 * Only BQ direct-command registers below 0x3E are read (register-pointer write,
 * repeated START, read). Nothing is ever written to the BQ: no 0x3E/0x40/0x60
 * subcommand or data-memory access, no FET control. PC0-PC5 are not touched.
 *
 * Results are kept in g_bms_dbg for inspection over SWD (raw bytes, decoded offline). */

#define BMS_I2C_ADDR7   0x08U     /* 0x10 in TI's 8-bit notation */

/* One record per direct command read. The record carries its own command id and data
 * length so tools reading g_bms_dbg over SWD need no copy of the command list.
 * Each register is read as 2*len bytes because this board's BQ replies with a CRC
 * byte after every data byte: [d0, crc0, d1, crc1]. */
typedef struct {
    uint8_t cmd;          /* direct command address (< 0x3E only)         */
    uint8_t len;          /* data bytes in the register: 1 or 2           */
    uint8_t status;       /* i2c_status_t of the last read                */
    uint8_t pad;
    uint8_t raw[4];       /* raw reply bytes; 0xEE-filled if the read failed */
} bms_reg_t;

#define BMS_NCMDS  33U

typedef struct {
    uint32_t pass_count;          /* completed scan passes                     */
    uint32_t idle_levels;         /* bit0 SCL, bit1 SDA before the pass (3 = both high) */
    uint32_t primary_status;      /* i2c_status_t of the last probe of BMS_I2C_ADDR7 */
    uint32_t found[4];            /* fallback scan: bitmap of ACKing addresses (bit n of word n/32) */
    bms_reg_t reg[BMS_NCMDS];
} bms_dbg_t;

extern volatile bms_dbg_t g_bms_dbg;

void bms_probe_init(void);
void bms_probe_pass(void);        /* one full read pass (~tens of ms) */

#endif /* BMS_PROBE_H */
