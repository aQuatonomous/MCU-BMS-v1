#ifndef BQ_DM_H
#define BQ_DM_H

#include <stdint.h>

/* Read-only look at the BQ76952's FET / DSG related configuration.
 *
 * This uses the documented subcommand READ mechanism: write the 2-byte subcommand or
 * data-memory address to registers 0x3E/0x3F, wait, then read the result buffer
 * (0x40...), its checksum (0x60) and length (0x61). It changes no setting.
 *
 * SAFETY: the only values ever written to 0x3E/0x3F come from the fixed table in
 * bq_dm.c (read-only subcommands 0x0001 and 0x0057, plus data-memory addresses).
 * There is deliberately no function that accepts an arbitrary address. No
 * CONFIG_UPDATE, no unseal, no FET control, no data-memory write is ever issued. */

#define BQ_DM_NENTRIES  28U

typedef struct {
    uint16_t cmd;          /* subcommand / data-memory address that was requested        */
    uint8_t  status;       /* i2c_status_t of the last step that failed (0 = all OK)      */
    uint8_t  len;          /* data bytes the device reported (length register - 4)        */
    uint8_t  chk_ok;       /* 1 = checksum matches using the REQUESTED address bytes      */
    uint8_t  crc_ok;       /* 1 = every per-byte CRC matched                              */
    uint8_t  chk_ok_echo;  /* 1 = checksum matches using the 0x3E/0x3F READBACK bytes     */
    uint8_t  chk_byte;     /* checksum byte the device returned (register 0x60)           */
    uint8_t  data[32];     /* data bytes (CRC bytes stripped); only first len are valid   */
    uint16_t echo;         /* what 0x3E/0x3F read back after the request                  */
    uint16_t pad;
} bq_dm_rec_t;

typedef struct {
    uint32_t pass_count;
    uint32_t n;
    bq_dm_rec_t rec[BQ_DM_NENTRIES];
} bq_dm_dbg_t;

extern volatile bq_dm_dbg_t g_bq_dm;

void bq_dm_pass(void);   /* one full read of the table (~0.3 s) */

#endif /* BQ_DM_H */
