#include "stm32f205xx.h"
#include "bms_autoconfig.h"
#include "bms_config_generated.h"
#include "bq_dm.h"
#include "logger.h"

volatile bms_autocfg_t g_autocfg;
volatile uint32_t g_autocfg_pause;
volatile uint32_t g_autocfg_req;

static uint32_t boot_cyc, last_run_cyc, last_sd_cyc;
static uint32_t first_done;

static uint32_t secs_to_cycles(uint32_t s) { return s * SystemCoreClock; }   /* fine for s <= 30 at 120 MHz */

void bms_autoconfig_init(void)
{
    uint32_t i;
    g_autocfg.n_items = BMS_CFG_COUNT;
    for (i = 0; i < BMS_CFG_COUNT && i < BMS_AUTOCFG_MAX; i++) {
        g_autocfg.item[i].addr   = k_bms_cfg[i].addr;
        g_autocfg.item[i].nbytes = k_bms_cfg[i].nbytes;
        g_autocfg.item[i].want   = k_bms_cfg[i].value;
        g_autocfg.item[i].live   = 0xFFFFU;
        g_autocfg.item[i].flags  = 0;
    }
    boot_cyc = last_run_cyc = last_sd_cyc = DWT->CYCCNT;
    first_done = 0;
}

/* One full check (+ repair). Never enables a FET; leaves the BQ alone if a FET is on. */
static void run_check(void)
{
    uint16_t fs = 0, v = 0, a[BMS_AUTOCFG_MAX], w[BMS_AUTOCFG_MAX];
    uint8_t  nb[BMS_AUTOCFG_MAX];
    uint32_t idx[BMS_AUTOCFG_MAX], i, nmis = 0;
    int rc, all_ok = 1;

    if (!bq_read_direct(0x7F, 1, &fs)) { g_autocfg.state = g_autocfg.last_result = AUTOCFG_NO_BQ; g_autocfg.errors++; return; }
    if (fs & 0x0FU)                    { g_autocfg.state = AUTOCFG_DEFERRED_FETS_ON; return; }

    for (i = 0; i < BMS_CFG_COUNT; i++) {
        if (!bq_dm_read_addr(k_bms_cfg[i].addr, k_bms_cfg[i].nbytes, &v)) {
            g_autocfg.item[i].flags = 0;
            g_autocfg.state = g_autocfg.last_result = AUTOCFG_READ_FAIL; g_autocfg.errors++;
            return;                                   /* never write on the strength of an invalid read */
        }
        g_autocfg.item[i].live = v;
        if (v == k_bms_cfg[i].value) {
            g_autocfg.item[i].flags = 3;
        } else {
            g_autocfg.item[i].flags = 1;
            idx[nmis] = i; a[nmis] = k_bms_cfg[i].addr; nb[nmis] = k_bms_cfg[i].nbytes; w[nmis] = k_bms_cfg[i].value; nmis++;
        }
    }
    g_autocfg.runs++;
    g_autocfg.mismatches = nmis;
    if (nmis == 0U) { g_autocfg.state = g_autocfg.last_result = AUTOCFG_OK_MATCH; return; }

    rc = bq_cfg_batch_write(a, nb, w, nmis);
    if (rc == 4) { g_autocfg.state = g_autocfg.last_result = AUTOCFG_CFGUPDATE_FAIL; g_autocfg.errors++; return; }
    if (rc != 0) { g_autocfg.state = g_autocfg.last_result = AUTOCFG_WRITE_FAIL;     g_autocfg.errors++; return; }

    for (i = 0; i < nmis; i++) {                       /* verify every written value by reading it back */
        uint32_t k = idx[i];
        if (bq_dm_read_addr(k_bms_cfg[k].addr, k_bms_cfg[k].nbytes, &v)) {
            g_autocfg.item[k].live = v;
            g_autocfg.item[k].flags = (uint8_t)(1U | ((v == k_bms_cfg[k].value) ? 2U : 0U));
            if (v != k_bms_cfg[k].value) { all_ok = 0; }
        } else {
            g_autocfg.item[k].flags = 0; all_ok = 0;
        }
    }
    if (all_ok) { g_autocfg.state = g_autocfg.last_result = AUTOCFG_APPLIED; g_autocfg.applied++; }
    else        { g_autocfg.state = g_autocfg.last_result = AUTOCFG_VERIFY_FAIL; g_autocfg.verify_fails++; }
}

void bms_autoconfig_service(void)
{
    uint32_t now = DWT->CYCCNT;
    int forced = 0;

    if (g_autocfg_req == BMS_AUTOCFG_NOW_MAGIC) { g_autocfg_req = 0; forced = 1; }

    /* keep the BQ in NORMAL mode (it falls back to SLEEP otherwise); skipped while logging to keep log timing clean */
    if (BMS_CFG_KEEP_AWAKE_PERIOD_S > 0U && !g_log.running &&
        (now - last_sd_cyc) >= secs_to_cycles(BMS_CFG_KEEP_AWAKE_PERIOD_S)) {
        last_sd_cyc = now;
        (void)bq_send_subcmd(0x009A);
        g_autocfg.sleep_resends++;
    }

    if (!BMS_CFG_AUTOCONFIG_ENABLED && !forced) { return; }
    if (!forced) {
        if (g_autocfg_pause)  { g_autocfg.state = AUTOCFG_PAUSED; return; }
        if (g_log.running)    { g_autocfg.state = AUTOCFG_DEFERRED_LOGGING; return; }
        if (!first_done) { if ((now - boot_cyc) < secs_to_cycles(BMS_CFG_FIRST_RUN_DELAY_S)) { return; } }
        else if ((now - last_run_cyc) < secs_to_cycles(BMS_CFG_CHECK_PERIOD_S)) { return; }
    }
    first_done = 1;
    last_run_cyc = now;
    run_check();
}
