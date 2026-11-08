#include "stm32f205xx.h"
#include "bq_dm.h"
#include "bms_probe.h"
#include "i2c_bb.h"
#include "clock.h"

/* The ONLY things this module is allowed to request (read-only). Names: TI's BQ769x2 header
 * (STM32_I2C/BQ769x2Header.h); 0x91C6 and 0x922D come from the owner's parameter table and
 * 0x929B (OTC delay) from the library (the TI header lists 0x920B, which looks like a typo).
 * 0x0001, 0x0057, 0x0083, 0x0085-0x0087 are read-only subcommand READS (never CB_SET_LVL 0x0084). Entry 1 (0x0057) is relied
 * on by the FET services; the table is APPEND-ONLY because the guarded writers use fixed indices into it. */
static const uint16_t k_table[BQ_DM_NENTRIES] = {
    0x0001, 0x0057, 0x9308, 0x9309, 0x930E, 0x930F, 0x9269, 0x926A,
    0x926B, 0x9265, 0x9266, 0x9267, 0x9261, 0x9262, 0x9263, 0x925F,
    0x92FA, 0x92FB, 0x92FC, 0x9300, 0x9301, 0x9302, 0x9303, 0x9234,
    0x9236, 0x9237, 0x9241, 0x9343, 0x9278, 0x9279, 0x927C, 0x9275,
    0x9276, 0x927B, 0x9280, 0x9281, 0x9288, 0x9282, 0x9283, 0x9284,
    0x9285, 0x9286, 0x9287, 0x9294, 0x928A, 0x928C, 0x928D, 0x929A,
    0x929B, 0x929C, 0x929D, 0x929E, 0x92A0, 0x92A1, 0x92A3, 0x92A4,
    0x92A6, 0x92A7, 0x92A9, 0x92AA, 0x92AC, 0x92AD, 0x923F, 0x9243,
    0x9244, 0x9252, 0x9253, 0x9254, 0x9248, 0x91A8, 0x91AC, 0x91C8,
    0x91C6, 0x922D, 0x926D, 0x926F, 0x9270, 0x9271, 0x0083, 0x0085,
    0x0086, 0x0087, 0x9335, 0x9336, 0x9337, 0x9338, 0x9339, 0x933A,
    0x933B, 0x933D, 0x933E, 0x933F, 0x9341, 0x9342, 0x92C0, 0x92C1,
    0x92C2, 0x92C3, 0x92C4, 0x92C5, 0x92C6, 0x92C7, 0x92CB, 0x92CD,
    0x92CE, 0x92D0, 0x9231, 0x9233, 0x92FD, 0x92FE, 0x92FF, 0x9304
};
_Static_assert(sizeof(k_table) / sizeof(k_table[0]) == BQ_DM_NENTRIES, "BQ_DM_NENTRIES must match k_table");

volatile bq_dm_dbg_t g_bq_dm;

uint8_t bq_crc8(const uint8_t *d, uint32_t n)
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
    if (bq_crc8(hdr, 4) != w[1]) { *crc_ok = 0; }
    for (i = 0; i < ndata; i++) {
        out[i] = w[2U * i];
        if (i > 0U && bq_crc8(&w[2U * i], 1) != w[2U * i + 1U]) { *crc_ok = 0; }
    }
    return I2C_OK;
}

static void dm_read_core(uint16_t cmd, bq_dm_rec_t *out)
{
    bq_dm_rec_t r;
    uint8_t req[4], lo, hi, attempt;
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
        req[0] = lo; req[1] = bq_crc8(h, 3);
        req[2] = hi; req[3] = bq_crc8(&hi, 1);
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
    *out = r;
}

static void read_entry(uint32_t idx)
{
    bq_dm_rec_t r;
    dm_read_core(k_table[idx], &r);
    g_bq_dm.rec[idx] = r;
}

void bq_dm_pass(void)
{
    uint32_t i;
    g_bq_dm.n = BQ_DM_NENTRIES;
    for (i = 0; i < BQ_DM_NENTRIES; i++) { read_entry(i); }
    g_bq_dm.pass_count++;
}

/* ---- One-shot FET_ENABLE -------------------------------------------------------- */
volatile uint32_t g_bq_fet_enable_req;
volatile bq_fet_enable_res_t g_bq_fet_enable_res;

/* Returns Manufacturing Status, or 0xFFFFFFFF if the read did not validate. */
static uint32_t read_mfg_status(void)
{
    const bq_dm_rec_t *r;
    read_entry(1);                       /* table entry 1 = 0x0057 MANUFACTURINGSTATUS */
    r = (const bq_dm_rec_t *)&g_bq_dm.rec[1];
    if (r->cmd != 0x0057U || r->status != 0U || r->len != 2U || r->chk_ok != 1U || r->crc_ok != 1U) {
        return 0xFFFFFFFFUL;
    }
    return (uint32_t)(r->data[0] | (r->data[1] << 8));
}

void bq_fet_enable_service(void)
{
    uint32_t before, after;
    uint8_t req[4], h[3], z = 0;

    if (g_bq_fet_enable_req != BQ_FET_ENABLE_MAGIC) { return; }
    g_bq_fet_enable_req = 0;             /* clear FIRST: a request can never run twice */

    g_bq_fet_enable_res.done = 0; g_bq_fet_enable_res.sent = 0;
    g_bq_fet_enable_res.mfg_before = g_bq_fet_enable_res.mfg_after = 0xFFFFFFFFUL;

    before = read_mfg_status();
    g_bq_fet_enable_res.mfg_before = before;
    if (before == 0xFFFFFFFFUL) { g_bq_fet_enable_res.reason = 2; g_bq_fet_enable_res.done = 1; return; }
    if (before & 0x10UL)         { g_bq_fet_enable_res.reason = 1; g_bq_fet_enable_res.done = 1; return; }

    /* FET_ENABLE = subcommand 0x0022, no data: register 0x3E <- [0x22, crc, 0x00, crc] */
    h[0] = 0x10; h[1] = 0x3E; h[2] = 0x22;
    req[0] = 0x22; req[1] = bq_crc8(h, 3);
    req[2] = 0x00; req[3] = bq_crc8(&z, 1);
    if (i2c_bb_write(BMS_I2C_ADDR7, 0x3E, req, 4) != I2C_OK) {
        g_bq_fet_enable_res.reason = 3; g_bq_fet_enable_res.done = 1; return;
    }
    g_bq_fet_enable_res.sent = 1;
    g_bq_fet_enable_res.reason = 0;

    delay_ms(20);
    after = read_mfg_status();
    g_bq_fet_enable_res.mfg_after = after;
    g_bq_fet_enable_res.done = 1;
}

/* ---- One-shot ALL_FETS_ON ----------------------------------------------------------- */
volatile uint32_t g_bq_fets_on_req;
volatile bq_fets_on_res_t g_bq_fets_on_res;

void bq_fets_on_service(void)
{
    uint32_t before;
    uint8_t req[4], h[3], z = 0, fs[2], ok = 1U;

    if (g_bq_fets_on_req != BQ_FETS_ON_MAGIC) { return; }
    g_bq_fets_on_req = 0;                /* clear FIRST: a request can never run twice */

    g_bq_fets_on_res.done = 0; g_bq_fets_on_res.sent = 0;
    g_bq_fets_on_res.mfg_before = 0xFFFFFFFFUL; g_bq_fets_on_res.fetstatus_after = 0xFFFFFFFFUL;

    before = read_mfg_status();
    g_bq_fets_on_res.mfg_before = before;
    if (before == 0xFFFFFFFFUL) { g_bq_fets_on_res.reason = 2; g_bq_fets_on_res.done = 1; return; }
    if (!(before & 0x10UL))      { g_bq_fets_on_res.reason = 1; g_bq_fets_on_res.done = 1; return; }

    /* ALL_FETS_ON = subcommand 0x0096, no data: register 0x3E <- [0x96, crc, 0x00, crc] */
    h[0] = 0x10; h[1] = 0x3E; h[2] = 0x96;
    req[0] = 0x96; req[1] = bq_crc8(h, 3);
    req[2] = 0x00; req[3] = bq_crc8(&z, 1);
    if (i2c_bb_write(BMS_I2C_ADDR7, 0x3E, req, 4) != I2C_OK) {
        g_bq_fets_on_res.reason = 3; g_bq_fets_on_res.done = 1; return;
    }
    g_bq_fets_on_res.sent = 1;
    g_bq_fets_on_res.reason = 0;

    delay_ms(50);
    if (read_crc(0x7F, fs, 1, &ok) == I2C_OK && ok) { g_bq_fets_on_res.fetstatus_after = fs[0]; }
    g_bq_fets_on_res.done = 1;
}

/* ---- Direct-command read helper (used by the logger) -------------------------------- */
/* Reads a 1- or 2-byte direct command. Refuses the subcommand-buffer window 0x3E-0x61 and
 * anything above 0x7F: only real direct commands are readable here. Returns 1 on a fully valid reply. */
int bq_read_direct(uint8_t cmd, uint8_t len, uint16_t *out)
{
    uint8_t d[2] = {0, 0}, ok = 1U;
    if ((cmd >= 0x3EU && cmd <= 0x61U) || cmd > 0x7FU || (len != 1U && len != 2U)) { return 0; }
    if (read_crc(cmd, d, len, &ok) != I2C_OK || !ok) { return 0; }
    *out = (len == 2U) ? (uint16_t)(d[0] | (d[1] << 8)) : (uint16_t)d[0];
    return 1;
}

/* ---- Generic one-shot command (fixed table, owner-triggered) -------------------------- */
volatile uint32_t g_bq_cmd_req;
volatile bq_cmd_res_t g_bq_cmd_res;

static const uint16_t k_cmd_table[BQ_CMD_COUNT] = {
    0x009A,   /* index 0: SLEEP_DISABLE (TI header). Volatile: BQ default returns after a reset. */
    0x0095,   /* index 1: ALL_FETS_OFF  (TI header). Reversible with ALL_FETS_ON.               */
    0x0012    /* index 2: BQ769x2_RESET (TI header; TI's example sends it at init and waits 60 ms).
                 A SOFTWARE reset of the BQ: same effect as a power cycle on its RAM (FET_EN, sleep
                 setting and safety flags return to power-up values). I2C is silent for a short time. */
};

void bq_cmd_service(void)
{
    uint32_t req = g_bq_cmd_req;
    uint32_t idx;
    uint16_t cmd;
    uint8_t w[4], h[3], lo, hi;

    if ((req & 0xFFFFFF00UL) != BQ_CMD_MAGIC_BASE) { return; }
    idx = req & 0xFFUL;
    g_bq_cmd_req = 0;                                  /* clear FIRST */
    g_bq_cmd_res.done = 0; g_bq_cmd_res.sent = 0; g_bq_cmd_res.cmd = 0;
    if (idx >= BQ_CMD_COUNT) { g_bq_cmd_res.reason = 2; g_bq_cmd_res.done = 1; return; }

    cmd = k_cmd_table[idx];
    lo = (uint8_t)(cmd & 0xFFU); hi = (uint8_t)(cmd >> 8);
    g_bq_cmd_res.cmd = cmd;
    h[0] = 0x10; h[1] = 0x3E; h[2] = lo;
    w[0] = lo; w[1] = bq_crc8(h, 3); w[2] = hi; w[3] = bq_crc8(&hi, 1);
    if (i2c_bb_write(BMS_I2C_ADDR7, 0x3E, w, 4) != I2C_OK) { g_bq_cmd_res.reason = 3; g_bq_cmd_res.done = 1; return; }
    g_bq_cmd_res.sent = 1; g_bq_cmd_res.reason = 0; g_bq_cmd_res.done = 1;
}

/* ---- Owner-triggered data-memory write (RAM only), fixed table ------------------------- */
volatile uint32_t g_bq_cfg_req;
volatile bq_cfg_res_t g_bq_cfg_res;

static const struct { uint16_t addr; uint8_t expect_before; uint8_t value; uint8_t table_idx; } k_cfg[BQ_CFG_COUNT] = {
    { 0x9308, 0x0D, 0x1D, 2 },   /* 0: FET Options, set bit 4 (k_table[2] == 0x9308) */
    { 0x9308, 0x1D, 0x0D, 2 },   /* 1: FET Options, restore                           */
    { 0x930E, 0x05, 0x32, 4 },   /* 2: Predischarge Timeout 50 ms -> 500 ms (units 10 ms; k_table[4] == 0x930E) */
    { 0x930E, 0x32, 0x05, 4 },   /* 3: Predischarge Timeout restore                   */
    { 0x930F, 0x32, 0x00, 5 },   /* 4: Predischarge Stop Delta 500 mV -> 0 (voltage check DISABLED: timing only; k_table[5] == 0x930F) */
    { 0x930F, 0x00, 0x32, 5 },   /* 5: Predischarge Stop Delta restore                */
    { 0x9286, 0x00, 0x05, 41 },   /* 6: SCD Threshold index 0 (10 mV = 10 A) -> index 5 (100 mV = 100 A @ 1 mOhm); owner's decision, supply has its own SC protection */
    { 0x9286, 0x05, 0x00, 41 }    /* 7: SCD Threshold restore (10 A)                   */,
    { 0x9278, 0x56, 0x54, 28 },   /* 8: COV Threshold raw 86 (4351.6 mV) -> raw 84 (4250.4 mV) [50.6 mV/count] */
    { 0x9278, 0x54, 0x56, 28 },   /* 9: COV Threshold restore */
    { 0x9275, 0x32, 0x41, 31 },   /* 10: CUV Threshold raw 50 (2530.0 mV) -> raw 65 (3289.0 mV) */
    { 0x9275, 0x41, 0x32, 31 },   /* 11: CUV Threshold restore */
    { 0x9261, 0x88, 0x8C, 12 },   /* 12: Enabled Protections A 0x88 -> 0x8C (adds CUV, bit 2) */
    { 0x9261, 0x8C, 0x88, 12 }   /* 13: Enabled Protections A restore */
};
_Static_assert(sizeof(k_cfg) / sizeof(k_cfg[0]) == BQ_CFG_COUNT, "BQ_CFG_COUNT must match k_cfg");

/* First data byte of table entry idx, or -1 if the read did not fully validate. */
static int read_table_byte(uint32_t idx)
{
    const bq_dm_rec_t *r;
    read_entry(idx);
    r = (const bq_dm_rec_t *)&g_bq_dm.rec[idx];
    if (r->status != 0U || r->len == 0U || r->chk_ok != 1U || r->crc_ok != 1U) { return -1; }
    return r->data[0];
}

/* Send a command-only subcommand (no data), same framing as bq_cmd_service. */
static int send_subcmd(uint16_t cmd)
{
    uint8_t w[4], h[3], lo = (uint8_t)(cmd & 0xFFU), hi = (uint8_t)(cmd >> 8);
    h[0] = 0x10; h[1] = 0x3E; h[2] = lo;
    w[0] = lo; w[1] = bq_crc8(h, 3); w[2] = hi; w[3] = bq_crc8(&hi, 1);
    return i2c_bb_write(BMS_I2C_ADDR7, 0x3E, w, 4) == I2C_OK;
}

/* Core guarded write of ONE data-memory byte. expect_before < 0 means "any value". Fills g_bq_cfg_res. */
static void cfg_write(uint16_t addr, uint8_t table_idx, int expect_before, uint8_t val)
{
    int before, after;
    uint16_t batt = 0;
    uint8_t lo = (uint8_t)(addr & 0xFFU), hi = (uint8_t)(addr >> 8), chk, len = 0x05U;
    uint8_t w[6], h[3], c[4], l;
    uint32_t poll;
    int ok = 1;

    g_bq_cfg_res.addr = addr;
    before = read_table_byte(table_idx);
    if (before < 0 || g_bq_dm.rec[table_idx].cmd != addr) { g_bq_cfg_res.reason = 2; g_bq_cfg_res.done = 1; return; }
    g_bq_cfg_res.before = (uint32_t)before;
    if (expect_before >= 0 && (uint8_t)before != (uint8_t)expect_before) { g_bq_cfg_res.reason = 3; g_bq_cfg_res.done = 1; return; }

    (void)send_subcmd(0x009A); delay_ms(20);          /* SLEEP_DISABLE first: config mode cannot be entered from SLEEP */
    /* Enter CONFIG_UPDATE and CONFIRM it (Battery Status bit0) before touching anything. */
    if (!send_subcmd(0x0090)) { g_bq_cfg_res.reason = 5; g_bq_cfg_res.done = 1; return; }
    for (poll = 0; poll < 40U; poll++) {            /* poll up to ~200 ms */
        delay_ms(5);
        if (bq_read_direct(0x12, 2, &batt) && (batt & 0x0001U)) { break; }
        batt = 0;
    }
    if (!(batt & 0x0001U)) {
        g_bq_cfg_res.reason = 4;
        (void)send_subcmd(0x0092);
        delay_ms(10);
        g_bq_cfg_res.done = 1; return;
    }
    g_bq_cfg_res.cfgupdate_seen = 1;

    h[0] = 0x10; h[1] = 0x3E; h[2] = lo;
    w[0] = lo;  w[1] = bq_crc8(h, 3);
    w[2] = hi;  w[3] = bq_crc8(&hi, 1);
    w[4] = val; w[5] = bq_crc8(&val, 1);
    ok = (i2c_bb_write(BMS_I2C_ADDR7, 0x3E, w, 6) == I2C_OK);
    delay_ms(3);
    if (ok) {
        chk = (uint8_t)~(uint8_t)(lo + hi + val);
        h[0] = 0x10; h[1] = 0x60; h[2] = chk;
        c[0] = chk; c[1] = bq_crc8(h, 3);
        l = len;    c[2] = l;   c[3] = bq_crc8(&l, 1);
        ok = (i2c_bb_write(BMS_I2C_ADDR7, 0x60, c, 4) == I2C_OK);
        delay_ms(3);
    }

    (void)send_subcmd(0x0092);                           /* always leave CONFIG_UPDATE */
    batt = 0xFFFFU;
    for (poll = 0; poll < 40U; poll++) {
        uint16_t b2 = 0xFFFFU;
        delay_ms(5);
        if (bq_read_direct(0x12, 2, &b2)) { batt = b2; if (!(b2 & 0x0001U)) { break; } }
    }
    delay_ms(10);

    after = read_table_byte(table_idx);
    g_bq_cfg_res.after = (after < 0) ? 0xFFFFFFFFUL : (uint32_t)after;

    if (!ok)                                         { g_bq_cfg_res.reason = 5; }
    else if (after < 0 || (uint8_t)after != val)     { g_bq_cfg_res.reason = 6; }
    else if (batt != 0xFFFFU && (batt & 0x0001U))    { g_bq_cfg_res.reason = 7; }
    else                                             { g_bq_cfg_res.reason = 0; }
    g_bq_cfg_res.done = 1;
}

void bq_cfg_service(void)
{
    uint32_t req = g_bq_cfg_req, idx;

    if ((req & 0xFFFFFF00UL) != BQ_CFG_MAGIC_BASE) { return; }
    idx = req & 0xFFUL;
    g_bq_cfg_req = 0;                                   /* clear FIRST: a request can never run twice */

    g_bq_cfg_res.done = 0; g_bq_cfg_res.reason = 0; g_bq_cfg_res.addr = 0;
    g_bq_cfg_res.before = g_bq_cfg_res.after = 0xFFFFFFFFUL; g_bq_cfg_res.cfgupdate_seen = 0;
    if (idx >= BQ_CFG_COUNT) { g_bq_cfg_res.reason = 1; g_bq_cfg_res.done = 1; return; }
    cfg_write(k_cfg[idx].addr, k_cfg[idx].table_idx, k_cfg[idx].expect_before, k_cfg[idx].value);
}

/* ---- SCD delay (0x9287) sweep writer: ONLY this address, ONLY values 1..31 ------------------- */
volatile uint32_t g_bq_scdd_req;

void bq_scdd_service(void)
{
    uint32_t req = g_bq_scdd_req, v, i, idx = 0xFFFFFFFFUL;

    if ((req & 0xFFFFFF00UL) != BQ_SCDD_MAGIC_BASE) { return; }
    v = req & 0xFFUL;
    g_bq_scdd_req = 0;                                  /* clear FIRST */

    g_bq_cfg_res.done = 0; g_bq_cfg_res.reason = 0; g_bq_cfg_res.addr = 0x9287U;
    g_bq_cfg_res.before = g_bq_cfg_res.after = 0xFFFFFFFFUL; g_bq_cfg_res.cfgupdate_seen = 0;
    if (v < 1UL || v > 31UL) { g_bq_cfg_res.reason = 1; g_bq_cfg_res.done = 1; return; }
    for (i = 0; i < BQ_DM_NENTRIES; i++) { if (k_table[i] == 0x9287U) { idx = i; break; } }
    if (idx == 0xFFFFFFFFUL) { g_bq_cfg_res.reason = 2; g_bq_cfg_res.done = 1; return; }
    cfg_write(0x9287U, (uint8_t)idx, -1, (uint8_t)v);
}

/* ---- VCell Mode (0x9304, 2 bytes) guarded writer ---------------------------------------------------
 * TRM (SLUUBY2B 13.3.2.19): bit n = "Cell n+1 Mode": 1 = a real cell between VC(n+1) and VC(n) -> protections enabled,
 * 0 = shorted / interconnect input -> protections disabled; if ALL bits are 0 every input counts as a cell.
 * Owner wiring: real cells are 1, 2, 3 and 16 -> bits 0, 1, 2, 15 -> 0x8007. */
volatile uint32_t g_bq_vcell_req;

static int read_table_u16(uint32_t idx)
{
    const bq_dm_rec_t *r;
    read_entry(idx);
    r = (const bq_dm_rec_t *)&g_bq_dm.rec[idx];
    if (r->status != 0U || r->len < 2U || r->chk_ok != 1U || r->crc_ok != 1U) { return -1; }
    return (int)(r->data[0] | (r->data[1] << 8));
}

static void cfg_write16(uint16_t addr, uint32_t idx, int expect_before, uint16_t val)
{
    int before, after;
    uint16_t batt = 0;
    uint8_t lo = (uint8_t)(addr & 0xFFU), hi = (uint8_t)(addr >> 8);
    uint8_t d0 = (uint8_t)(val & 0xFFU), d1 = (uint8_t)(val >> 8), chk, len = 0x06U;
    uint8_t w[8], h[3], c[4], l;
    uint32_t poll;
    int ok;

    g_bq_cfg_res.addr = addr;
    before = read_table_u16(idx);
    if (before < 0 || g_bq_dm.rec[idx].cmd != addr) { g_bq_cfg_res.reason = 2; g_bq_cfg_res.done = 1; return; }
    g_bq_cfg_res.before = (uint32_t)before;
    if (expect_before >= 0 && before != expect_before) { g_bq_cfg_res.reason = 3; g_bq_cfg_res.done = 1; return; }

    (void)send_subcmd(0x009A); delay_ms(20);          /* SLEEP_DISABLE first (see cfg_write) */
    if (!send_subcmd(0x0090)) { g_bq_cfg_res.reason = 5; g_bq_cfg_res.done = 1; return; }
    for (poll = 0; poll < 40U; poll++) {
        delay_ms(5);
        if (bq_read_direct(0x12, 2, &batt) && (batt & 0x0001U)) { break; }
        batt = 0;
    }
    if (!(batt & 0x0001U)) {
        g_bq_cfg_res.reason = 4; (void)send_subcmd(0x0092); delay_ms(10); g_bq_cfg_res.done = 1; return;
    }
    g_bq_cfg_res.cfgupdate_seen = 1;

    h[0] = 0x10; h[1] = 0x3E; h[2] = lo;
    w[0] = lo; w[1] = bq_crc8(h, 3);
    w[2] = hi; w[3] = bq_crc8(&hi, 1);
    w[4] = d0; w[5] = bq_crc8(&d0, 1);
    w[6] = d1; w[7] = bq_crc8(&d1, 1);
    ok = (i2c_bb_write(BMS_I2C_ADDR7, 0x3E, w, 8) == I2C_OK);
    delay_ms(3);
    if (ok) {
        chk = (uint8_t)~(uint8_t)(lo + hi + d0 + d1);
        h[0] = 0x10; h[1] = 0x60; h[2] = chk;
        c[0] = chk; c[1] = bq_crc8(h, 3);
        l = len;    c[2] = l;   c[3] = bq_crc8(&l, 1);
        ok = (i2c_bb_write(BMS_I2C_ADDR7, 0x60, c, 4) == I2C_OK);
        delay_ms(3);
    }

    (void)send_subcmd(0x0092);
    batt = 0xFFFFU;
    for (poll = 0; poll < 40U; poll++) {
        uint16_t b2 = 0xFFFFU;
        delay_ms(5);
        if (bq_read_direct(0x12, 2, &b2)) { batt = b2; if (!(b2 & 0x0001U)) { break; } }
    }
    delay_ms(10);

    after = read_table_u16(idx);
    g_bq_cfg_res.after = (after < 0) ? 0xFFFFFFFFUL : (uint32_t)after;
    if (!ok)                                         { g_bq_cfg_res.reason = 5; }
    else if (after < 0 || (uint16_t)after != val)    { g_bq_cfg_res.reason = 6; }
    else if (batt != 0xFFFFU && (batt & 0x0001U))    { g_bq_cfg_res.reason = 7; }
    else                                             { g_bq_cfg_res.reason = 0; }
    g_bq_cfg_res.done = 1;
}

/* g_bq_vcell_req = BQ_VCELL_MAGIC_BASE | 0 : 0x0000 -> 0x8007 (cells 1,2,3,16);  | 1 : 0x8007 -> 0x0000 (undo). */
void bq_vcell_service(void)
{
    uint32_t req = g_bq_vcell_req, k, idx = 0xFFFFFFFFUL;

    if ((req & 0xFFFFFF00UL) != BQ_VCELL_MAGIC_BASE) { return; }
    g_bq_vcell_req = 0;                                 /* clear FIRST */
    g_bq_cfg_res.done = 0; g_bq_cfg_res.reason = 0; g_bq_cfg_res.addr = 0x9304U;
    g_bq_cfg_res.before = g_bq_cfg_res.after = 0xFFFFFFFFUL; g_bq_cfg_res.cfgupdate_seen = 0;
    if ((req & 0xFFUL) > 1UL) { g_bq_cfg_res.reason = 1; g_bq_cfg_res.done = 1; return; }
    for (k = 0; k < BQ_DM_NENTRIES; k++) { if (k_table[k] == 0x9304U) { idx = k; break; } }
    if (idx == 0xFFFFFFFFUL) { g_bq_cfg_res.reason = 2; g_bq_cfg_res.done = 1; return; }
    if ((req & 0xFFUL) == 0UL) { cfg_write16(0x9304U, idx, 0x0000, 0x8007U); }
    else                       { cfg_write16(0x9304U, idx, 0x8007, 0x0000U); }
}

/* ---- Helpers for bms_autoconfig.c ------------------------------------------------------------------- */
/* Read a 1- or 2-byte data-memory value at any address. Returns 1 only if status, checksum and CRC all validated. */
int bq_dm_read_addr(uint16_t addr, uint8_t nbytes, uint16_t *val)
{
    bq_dm_rec_t r;
    dm_read_core(addr, &r);
    if (r.status != 0U || r.len < nbytes || r.chk_ok != 1U || r.crc_ok != 1U) { return 0; }
    *val = (nbytes == 2U) ? (uint16_t)(r.data[0] | (r.data[1] << 8)) : (uint16_t)r.data[0];
    return 1;
}

/* Write n data-memory items inside ONE CONFIG_UPDATE session (SLEEP_DISABLE first, CFGUPDATE confirmed before any write,
 * EXIT always attempted). Returns 0 = all writes sent, 4 = BQ never entered CONFIG_UPDATE (nothing written),
 * 5 = an I2C write failed. The caller must read the values back to verify. */
int bq_cfg_batch_write(const uint16_t *addr, const uint8_t *nbytes, const uint16_t *val, uint32_t n)
{
    uint16_t batt = 0;
    uint32_t poll, i;
    int rc = 0;

    (void)send_subcmd(0x009A); delay_ms(20);
    if (!send_subcmd(0x0090)) { return 5; }
    for (poll = 0; poll < 40U; poll++) {
        delay_ms(5);
        if (bq_read_direct(0x12, 2, &batt) && (batt & 0x0001U)) { break; }
        batt = 0;
    }
    if (!(batt & 0x0001U)) { (void)send_subcmd(0x0092); delay_ms(10); return 4; }

    for (i = 0; i < n && rc == 0; i++) {
        uint8_t lo = (uint8_t)(addr[i] & 0xFFU), hi = (uint8_t)(addr[i] >> 8);
        uint8_t d0 = (uint8_t)(val[i] & 0xFFU), d1 = (uint8_t)(val[i] >> 8);
        uint8_t w[8], h[3], c[4], l, chk, wl, len;
        int ok;

        h[0] = 0x10; h[1] = 0x3E; h[2] = lo;
        w[0] = lo; w[1] = bq_crc8(h, 3);
        w[2] = hi; w[3] = bq_crc8(&hi, 1);
        w[4] = d0; w[5] = bq_crc8(&d0, 1);
        if (nbytes[i] == 2U) { w[6] = d1; w[7] = bq_crc8(&d1, 1); wl = 8U; len = 0x06U; chk = (uint8_t)~(uint8_t)(lo + hi + d0 + d1); }
        else                 { wl = 6U; len = 0x05U; chk = (uint8_t)~(uint8_t)(lo + hi + d0); }
        ok = (i2c_bb_write(BMS_I2C_ADDR7, 0x3E, w, wl) == I2C_OK);
        delay_ms(3);
        if (ok) {
            h[0] = 0x10; h[1] = 0x60; h[2] = chk;
            c[0] = chk; c[1] = bq_crc8(h, 3);
            l = len;    c[2] = l;   c[3] = bq_crc8(&l, 1);
            ok = (i2c_bb_write(BMS_I2C_ADDR7, 0x60, c, 4) == I2C_OK);
            delay_ms(3);
        }
        if (!ok) { rc = 5; }
    }

    (void)send_subcmd(0x0092);                           /* always leave CONFIG_UPDATE */
    for (poll = 0; poll < 40U; poll++) {
        uint16_t b2 = 0xFFFFU;
        delay_ms(5);
        if (bq_read_direct(0x12, 2, &b2) && !(b2 & 0x0001U)) { break; }
    }
    delay_ms(10);
    return rc;
}

/* Send a command-only subcommand (e.g. SLEEP_DISABLE 0x009A). */
int bq_send_subcmd(uint16_t cmd) { return send_subcmd(cmd); }
