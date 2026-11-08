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

#define BQ_DM_NENTRIES  78U

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

/* ---- One-shot FET_ENABLE (subcommand 0x0022) ----------------------------------------
 * This is the ONLY write to the BQ in the project besides the read requests above.
 * It is never sent automatically. The owner's SWD tool writes BQ_FET_ENABLE_MAGIC to
 * g_bq_fet_enable_req; bq_fet_enable_service() clears the request FIRST, re-reads
 * Manufacturing Status, and sends 0x0022 only if that read validated AND bit 4 (FET_EN)
 * is currently 0 - because the subcommand TOGGLES FET_EN and must not be sent twice. */
#define BQ_FET_ENABLE_MAGIC  0xFE7E0022UL

typedef struct {
    uint32_t done;        /* 1 once a request has been handled                         */
    uint32_t sent;        /* 1 if 0x0022 was actually written to the BQ                */
    uint32_t reason;      /* 0 = sent, 1 = already enabled, 2 = could not verify state, 3 = I2C write failed */
    uint32_t mfg_before;  /* Manufacturing Status before (0xFFFFFFFF if unreadable)    */
    uint32_t mfg_after;   /* Manufacturing Status after  (0xFFFFFFFF if unreadable)    */
} bq_fet_enable_res_t;

extern volatile uint32_t g_bq_fet_enable_req;
extern volatile bq_fet_enable_res_t g_bq_fet_enable_res;

void bq_fet_enable_service(void);   /* call from the main loop */

/* ---- One-shot ALL_FETS_ON (subcommand 0x0096) ---------------------------------------
 * Same pattern: never automatic; the owner's SWD tool writes BQ_FETS_ON_MAGIC to
 * g_bq_fets_on_req, which is cleared before anything is sent. Sent only if Manufacturing
 * Status reads back valid AND FET_EN (bit 4) is set (otherwise the command is pointless
 * and nothing is sent). ALL_FETS_ON is not a toggle; ALL_FETS_OFF (0x0095) reverses it. */
#define BQ_FETS_ON_MAGIC  0xFE7E0096UL

typedef struct {
    uint32_t done;
    uint32_t sent;
    uint32_t reason;        /* 0 = sent, 1 = FET_EN not set (nothing sent), 2 = could not verify state, 3 = I2C write failed */
    uint32_t mfg_before;
    uint32_t fetstatus_after;   /* FET Status (0x7F) ~50 ms after the command; 0xFFFFFFFF if unreadable */
} bq_fets_on_res_t;

extern volatile uint32_t g_bq_fets_on_req;
extern volatile bq_fets_on_res_t g_bq_fets_on_res;

void bq_fets_on_service(void);      /* call from the main loop */

/* ---- Shared helpers ---------------------------------------------------------------- */
uint8_t bq_crc8(const uint8_t *d, uint32_t n);                       /* CRC-8, poly 0x07 */
int bq_read_direct(uint8_t cmd, uint8_t len, uint16_t *out);         /* 1 = valid reply  */

/* ---- Generic owner-triggered one-shot command (fixed table of 3) ----------------------
 * Write BQ_CMD_MAGIC_BASE | index to g_bq_cmd_req over SWD. The request is cleared before
 * anything is sent. index 0 = SLEEP_DISABLE (0x009A), index 1 = ALL_FETS_OFF (0x0095),
 * index 2 = BQ769x2_RESET (0x0012, software reset of the BQ). */
#define BQ_CMD_MAGIC_BASE  0xFE7E0100UL
#define BQ_CMD_COUNT       3U
typedef struct { uint32_t done; uint32_t sent; uint32_t reason; uint32_t cmd; } bq_cmd_res_t;
extern volatile uint32_t g_bq_cmd_req;
extern volatile bq_cmd_res_t g_bq_cmd_res;
void bq_cmd_service(void);

/* ---- Owner-triggered DATA-MEMORY WRITE (fixed table of 8, RAM only) --------------------
 * The ONLY config writes this firmware can make. Write BQ_CFG_MAGIC_BASE | index to g_bq_cfg_req.
 *   index 0 : FET Options (0x9308) 0x0D -> 0x1D   (sets bit 4; PDSG_EN per the library's own comment)
 *   index 1 : FET Options (0x9308) 0x1D -> 0x0D   (undo)
 *   index 2 : Predischarge Timeout (0x930E) 0x05 -> 0x32 (50 ms -> 500 ms)
 *   index 3 : Predischarge Timeout (0x930E) 0x32 -> 0x05 (undo)
 *   index 4 : Predischarge Stop Delta (0x930F) 0x32 -> 0x00 (voltage condition disabled; timing only)
 *   index 5 : Predischarge Stop Delta (0x930F) 0x00 -> 0x32 (undo)
 *   index 6 : SCD Threshold (0x9286) index 0 -> 5  (10 mV -> 100 mV = 10 A -> 100 A at 1 mOhm)
 *   index 7 : SCD Threshold (0x9286) index 5 -> 0  (undo)
 * Each entry has an EXPECTED current value: if the live value differs, nothing is written.
 * Sequence: verify current value -> SET_CFGUPDATE (0x0090) and confirm Battery Status bit0 ->
 * write value + checksum -> EXIT_CFGUPDATE (0x0092, always attempted once entered) -> read back.
 * The BQ keeps this only in RAM (OTP writes are blocked); it reverts on any BQ reset / power cycle. */
#define BQ_CFG_MAGIC_BASE  0xFE7E0200UL
#define BQ_CFG_COUNT       8U
typedef struct {
    uint32_t done;
    uint32_t reason;     /* 0 = written and verified, 1 = bad index, 2 = could not read current value,
                            3 = current value not the expected one (nothing written), 4 = BQ did not enter
                            CONFIG_UPDATE (nothing written), 5 = I2C write failed, 6 = read-back mismatch,
                            7 = BQ still in CONFIG_UPDATE after exit */
    uint32_t addr;
    uint32_t before;
    uint32_t after;
    uint32_t cfgupdate_seen;   /* Battery Status bit0 observed set while in config mode */
} bq_cfg_res_t;
extern volatile uint32_t g_bq_cfg_req;
extern volatile bq_cfg_res_t g_bq_cfg_res;
void bq_cfg_service(void);

/* ---- SCD delay writer for the delay sweep: address 0x9287 ONLY, value 1..31 only ----------
 * Write BQ_SCDD_MAGIC_BASE | value to g_bq_scdd_req. Same CONFIG_UPDATE sequence/verification as above
 * (result in g_bq_cfg_res). Library: unit ~15 us per count (formulas differ by one count). RAM only. */
#define BQ_SCDD_MAGIC_BASE  0xFE7E0300UL
extern volatile uint32_t g_bq_scdd_req;
void bq_scdd_service(void);

#endif /* BQ_DM_H */
