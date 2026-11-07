#include "stm32f205xx.h"
#include "bq_dm.h"
#include "bms_probe.h"
#include "i2c_bb.h"
#include "clock.h"

/* The ONLY things this module is allowed to request. Names are from TI's BQ769x2 header
 * (STM32_I2C/BQ769x2Header.h). 0x0001 and 0x0057 are read-only subcommands. */
static const uint16_t k_table[BQ_DM_NENTRIES] = {
    0x0001,  /* DEVICE_NUMBER            (sanity check of the mechanism) */
    0x0057,  /* MANUFACTURINGSTATUS                                       */
    0x9308,  /* FET Options                                               */
    0x9309,  /* Chg Pump Control                                          */
    0x930E,  /* Predischarge Timeout                                      */
    0x930F,  /* Predischarge Stop Delta                                   */
    0x9269,  /* DSG FET Protections A                                     */
    0x926A,  /* DSG FET Protections B                                     */
    0x926B,  /* DSG FET Protections C                                     */
    0x9265,  /* CHG FET Protections A                                     */
    0x9266,  /* CHG FET Protections B                                     */
    0x9267,  /* CHG FET Protections C                                     */
    0x9261,  /* Enabled Protections A                                     */
    0x9262,  /* Enabled Protections B                                     */
    0x9263,  /* Enabled Protections C                                     */
    0x925F,  /* Protection Configuration                                  */
    0x92FA,  /* CFETOFF Pin Config                                        */
    0x92FB,  /* DFETOFF Pin Config                                        */
    0x92FC,  /* ALERT Pin Config                                          */
    0x9300,  /* HDQ Pin Config                                            */
    0x9301,  /* DCHG Pin Config                                           */
    0x9302,  /* DDSG Pin Config                                           */
    0x9303,  /* DA Configuration                                          */
    0x9234,  /* Power Config                                              */
    0x9236,  /* REG12 Config                                              */
    0x9237,  /* REG0 Config                                               */
    0x9241,  /* Shutdown Stack Voltage                                    */
    0x9343   /* Mfg Status Init                                           */
};

volatile bq_dm_dbg_t g_bq_dm;

static uint8_t crc8(const uint8_t *d, uint32_t n)
{
    uint8_t c = 0;
    uint32_t i, b;
    for (i = 0; i < n; i++) {
        c ^= d[i];
        for (b = 0; b < 8; b++) { c = (c & 0x80U) ? (uint8_t)((c << 1) ^ 0x07U) : (uint8_t)(c << 1); }
    }
    return c;
}

/* Read ndata data bytes (<= 8) from register reg; replies carry a CRC after each byte. */
static i2c_status_t read_crc(uint8_t reg, uint8_t *out, uint32_t ndata, uint8_t *crc_ok)
{
    uint8_t w[64];
    uint8_t hdr[4];
    uint32_t i;
    i2c_status_t st = i2c_bb_read(BMS_I2C_ADDR7, reg, w, 2U * ndata);

    if (st != I2C_OK) { return st; }
    hdr[0] = 0x10; hdr[1] = reg; hdr[2] = 0x11; hdr[3] = w[0];
    if (crc8(hdr, 4) != w[1]) { *crc_ok = 0; }
    for (i = 0; i < ndata; i++) {
        out[i] = w[2U * i];
        if (i > 0U && crc8(&w[2U * i], 1) != w[2U * i + 1U]) { *crc_ok = 0; }
    }
    return I2C_OK;
}

static void read_entry(uint32_t idx)
{
    bq_dm_rec_t r;
    uint8_t req[4], lo, hi, attempt;
    uint16_t cmd = k_table[idx];
    i2c_status_t st;
    uint32_t i;

    r.cmd = cmd; r.status = 0; r.len = 0; r.chk_ok = 0; r.crc_ok = 0; r.chk_ok_echo = 0;
    r.chk_byte = 0; r.echo = 0xFFFFU; r.pad = 0;
    for (i = 0; i < 32; i++) { r.data[i] = 0xEE; }

    lo = (uint8_t)(cmd & 0xFFU);
    hi = (uint8_t)(cmd >> 8);

    /* Request: register 0x3E <- [lo, crc, hi, crc] (CRC scheme as for every write on this device) */
    {
        uint8_t h[3];
        h[0] = 0x10; h[1] = 0x3E; h[2] = lo;
        req[0] = lo; req[1] = crc8(h, 3);
        req[2] = hi; req[3] = crc8(&hi, 1);
    }
    st = i2c_bb_write(BMS_I2C_ADDR7, 0x3E, req, 4);
    if (st != I2C_OK) { r.status = (uint8_t)st; goto done; }

    for (attempt = 0; attempt < 4U; attempt++) {
        uint8_t len, chk, sum, e[2];
        uint8_t ok = 1U, ok_e = 1U;
        uint32_t nd;

        delay_ms(2);
        st = read_crc(0x61, &len, 1, &ok);
        if (st != I2C_OK) { r.status = (uint8_t)st; continue; }
        if (len < 4U || len > 36U) { r.status = 0; r.len = len; continue; }   /* not ready yet */

        nd = (uint32_t)len - 4U;
        r.len = (uint8_t)nd;
        if (nd > 0U) {
            st = read_crc(0x40, r.data, nd, &ok);
            if (st != I2C_OK) { r.status = (uint8_t)st; continue; }
        }
        st = read_crc(0x60, &chk, 1, &ok);
        if (st != I2C_OK) { r.status = (uint8_t)st; continue; }
        r.chk_byte = chk;

        /* Diagnostic: what do 0x3E/0x3F hold now? */
        if (read_crc(0x3E, e, 2, &ok_e) == I2C_OK) { r.echo = (uint16_t)(e[0] | (e[1] << 8)); }

        r.status = 0;
        r.crc_ok = ok;
        sum = (uint8_t)(lo + hi);
        for (i = 0; i < nd; i++) { sum = (uint8_t)(sum + r.data[i]); }
        r.chk_ok = (uint8_t)((uint8_t)~sum == chk);
        sum = (uint8_t)((r.echo & 0xFFU) + (r.echo >> 8));
        for (i = 0; i < nd; i++) { sum = (uint8_t)(sum + r.data[i]); }
        r.chk_ok_echo = (uint8_t)((uint8_t)~sum == chk);
        if ((r.chk_ok == 1U || r.chk_ok_echo == 1U) && ok) { break; }
    }

done:
    g_bq_dm.rec[idx] = r;
}

void bq_dm_pass(void)
{
    uint32_t i;
    g_bq_dm.n = BQ_DM_NENTRIES;
    for (i = 0; i < BQ_DM_NENTRIES; i++) { read_entry(i); }
    g_bq_dm.pass_count++;
}
