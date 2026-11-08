#ifndef BMS_AUTOCONFIG_H
#define BMS_AUTOCONFIG_H

#include <stdint.h>

/* Start-up (and keep-alive) configuration of the BQ76952 from config/bms_settings.csv.
 *
 * What it does: shortly after the MCU starts, and then every BMS_CFG_CHECK_PERIOD_S seconds, it READS every listed setting
 * from the BQ. Only if a value differs does it write the differences (all in ONE CONFIG_UPDATE session) and read them
 * back to verify. Because the BQ forgets RAM settings whenever it is reset / power-cycled, this also repairs the BQ after a BMS power cycle.
 *
 * Safety rules (all enforced in the code):
 *   - it NEVER enables any FET (FET_ENABLE / ALL_FETS_ON are never sent by this module);
 *   - it does nothing while any FET (CHG/PCHG/DSG/PDSG) is on, because CONFIG_UPDATE would interrupt a live load;
 *   - it does nothing while the logger is running, or while g_autocfg_pause != 0 (used by studies that change settings themselves);
 *   - it never writes based on a read that failed validation (status + checksum + CRC);
 *   - it only touches the addresses listed in the generated table (the generator blocks security / fuse registers).
 * Status for humans and scripts: g_autocfg (read over SWD; `python3 tools/bms.py status`). */

#define BMS_AUTOCFG_MAX         32U
#define BMS_AUTOCFG_NOW_MAGIC   0xFE7E0500UL    /* write to g_autocfg_req: run one check immediately */

typedef enum {
    AUTOCFG_NOT_RUN = 0,
    AUTOCFG_OK_MATCH = 1,           /* every setting already matched - nothing written          */
    AUTOCFG_APPLIED = 2,            /* differences were written and verified by read-back        */
    AUTOCFG_VERIFY_FAIL = 3,        /* written, but a value did not read back correctly          */
    AUTOCFG_NO_BQ = 4,              /* the BQ did not answer (powered off / asleep / bus problem)*/
    AUTOCFG_DEFERRED_FETS_ON = 5,   /* a FET is on: left alone on purpose                        */
    AUTOCFG_PAUSED = 6,             /* g_autocfg_pause != 0                                      */
    AUTOCFG_DEFERRED_LOGGING = 7,   /* logger running: left alone on purpose                     */
    AUTOCFG_WRITE_FAIL = 8,         /* an I2C write failed                                       */
    AUTOCFG_CFGUPDATE_FAIL = 9,     /* BQ never entered CONFIG_UPDATE (nothing written)          */
    AUTOCFG_READ_FAIL = 10          /* a setting could not be read validly (nothing written)     */
} autocfg_state_t;

typedef struct {
    uint16_t addr;
    uint8_t  nbytes;
    uint8_t  flags;     /* bit0: last read of 'live' was valid, bit1: live == want */
    uint16_t want;      /* value from config/bms_settings.csv (compiled in) */
    uint16_t live;      /* value last read from the BQ                      */
} bms_autocfg_item_t;   /* 8 bytes */

typedef struct {
    uint32_t state;         /* what the module is doing / why it is idle right now (autocfg_state_t) */
    uint32_t last_result;   /* result of the last real check (OK_MATCH / APPLIED / failure)           */
    uint32_t runs;          /* completed checks that read all settings                                */
    uint32_t applied;       /* times differences were written and verified                            */
    uint32_t verify_fails;
    uint32_t errors;        /* NO_BQ / READ_FAIL / WRITE_FAIL / CFGUPDATE_FAIL occurrences            */
    uint32_t n_items;
    uint32_t mismatches;    /* number of settings that differed at the last check                     */
    uint32_t sleep_resends; /* SLEEP_DISABLE keep-alive sends                                         */
    bms_autocfg_item_t item[BMS_AUTOCFG_MAX];
} bms_autocfg_t;

extern volatile bms_autocfg_t g_autocfg;
extern volatile uint32_t g_autocfg_pause;   /* non-zero = hands off (cleared by an MCU reset) */
extern volatile uint32_t g_autocfg_req;

void bms_autoconfig_init(void);
void bms_autoconfig_service(void);          /* call from the main loop */

#endif /* BMS_AUTOCONFIG_H */
