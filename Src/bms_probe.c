#include "stm32f205xx.h"
#include "bms_probe.h"
#include "i2c_bb.h"

/* {command, data length}. Direct commands only - all < 0x3E, read-only.
 *  0x00 ControlStatus   0x02/04/06 SafetyAlert A/B/C   0x03/05/07 SafetyStatus A/B/C
 *  0x12 BatteryStatus   0x14..0x32 Cell1..16 voltage   0x34 Stack  0x36 PACK  0x38 LD
 *  0x3A CC2 current     0x62 AlarmStatus  0x64 AlarmRawStatus  0x66 AlarmEnable
 *  0x68 InternalTemp    0x7F FETStatus (bit0 CHG, bit1 PCHG, bit2 DSG, bit3 PDSG) */
static const struct { uint8_t cmd; uint8_t len; } k_cmds[BMS_NCMDS] = {
    {0x00, 2}, {0x02, 1}, {0x03, 1}, {0x04, 1}, {0x05, 1}, {0x06, 1}, {0x07, 1},
    {0x12, 2},
    {0x14, 2}, {0x16, 2}, {0x18, 2}, {0x1A, 2}, {0x1C, 2}, {0x1E, 2}, {0x20, 2}, {0x22, 2},
    {0x24, 2}, {0x26, 2}, {0x28, 2}, {0x2A, 2}, {0x2C, 2}, {0x2E, 2}, {0x30, 2}, {0x32, 2},
    {0x34, 2}, {0x36, 2}, {0x38, 2}, {0x3A, 2},
    {0x62, 2}, {0x64, 2}, {0x66, 2},
    {0x68, 2},
    {0x7F, 1}
};

volatile bms_dbg_t g_bms_dbg;

void bms_probe_init(void)
{
    uint32_t i;
    i2c_bb_init();
    g_bms_dbg.idle_levels = i2c_bb_line_levels();
    for (i = 0; i < BMS_NCMDS; i++) {
        g_bms_dbg.reg[i].cmd = k_cmds[i].cmd;
        g_bms_dbg.reg[i].len = k_cmds[i].len;
    }
}

void bms_probe_pass(void)
{
    uint32_t i, a, k;
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
            uint32_t n = 2U * k_cmds[i].len;       /* data byte + CRC byte, per data byte */
            st = i2c_bb_read(BMS_I2C_ADDR7, k_cmds[i].cmd, buf, n);
            g_bms_dbg.reg[i].status = (uint8_t)st;
            for (k = 0; k < 4; k++) {
                g_bms_dbg.reg[i].raw[k] = (st == I2C_OK && k < n) ? buf[k] : (st == I2C_OK ? 0x00 : 0xEE);
            }
        }
    }
    g_bms_dbg.pass_count++;
}
