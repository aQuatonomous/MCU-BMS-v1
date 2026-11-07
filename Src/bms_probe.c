#include "stm32f205xx.h"
#include "bms_probe.h"
#include "i2c_bb.h"

/* ControlStatus, SafetyStatus A/B/C, BatteryStatus, Cell1..16 voltage, Stack, Pack, LD,
 * CC2 current, internal temperature. */
const uint8_t g_bms_cmds[BMS_NCMDS] = {
    0x00, 0x03, 0x05, 0x07, 0x12,
    0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x22,
    0x24, 0x26, 0x28, 0x2A, 0x2C, 0x2E, 0x30, 0x32,
    0x34, 0x36, 0x38, 0x3A, 0x68
};

volatile bms_dbg_t g_bms_dbg;

void bms_probe_init(void)
{
    i2c_bb_init();
    g_bms_dbg.idle_levels = i2c_bb_line_levels();
}

void bms_probe_pass(void)
{
    uint32_t i, a;
    uint8_t buf[4];
    i2c_status_t st;

    g_bms_dbg.idle_levels = i2c_bb_line_levels();

    st = i2c_bb_probe(BMS_I2C_ADDR7);
    g_bms_dbg.primary_status = (uint32_t)st;

    if (st == I2C_ERR_NACK_ADDR) {
        /* Expected address silent: look for any other responder (address-only probes). */
        for (i = 0; i < 4; i++) { g_bms_dbg.found[i] = 0; }
        for (a = 0x03; a <= 0x77; a++) {
            if (i2c_bb_probe((uint8_t)a) == I2C_OK) {
                g_bms_dbg.found[a / 32U] |= 1UL << (a % 32U);
            }
        }
    } else if (st == I2C_OK) {
        for (i = 0; i < BMS_NCMDS; i++) {
            uint32_t k;
            st = i2c_bb_read(BMS_I2C_ADDR7, g_bms_cmds[i], buf, 4);
            g_bms_dbg.status[i] = (uint32_t)st;
            for (k = 0; k < 4; k++) { g_bms_dbg.raw[i][k] = (st == I2C_OK) ? buf[k] : 0xEE; }
        }
    }
    g_bms_dbg.pass_count++;
}
