#include "stm32f205xx.h"
#include "logger.h"
#include "bq_dm.h"

volatile uint32_t g_log_req;
volatile uint32_t g_log_period_ms = 50;
volatile uint32_t g_log_slow_div = 5;
volatile log_hdr_t g_log;
log_sample_t g_log_samples[LOG_CAPACITY];

static uint64_t elapsed_cycles;
static uint32_t last_cyc;
static uint32_t next_due_ms;
static uint32_t slow_ctr;
static log_sample_t held;      /* last slow-channel values */

static void update_clock(void)
{
    uint32_t now = DWT->CYCCNT;
    elapsed_cycles += (uint32_t)(now - last_cyc);   /* wrap-safe while called more often than every ~35 s */
    last_cyc = now;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(elapsed_cycles / (SystemCoreClock / 1000UL));
}

static uint16_t rd(uint8_t cmd, uint8_t len, uint8_t *err)
{
    uint16_t v = 0xFFFFU;
    if (!bq_read_direct(cmd, len, &v)) { *err = 1U; v = 0xFFFFU; }
    return v;
}

void logger_service(void)
{
    uint32_t req = g_log_req;

    if (req == LOG_START_MAGIC) {
        uint32_t p = g_log_period_ms;
        g_log_req = 0;
        if (p < 20U) { p = 20U; }
        if (p > 1000U) { p = 1000U; }
        g_log.period_ms = p; g_log.capacity = LOG_CAPACITY;
        g_log.count = 0; g_log.read_errors = 0; g_log.full = 0;
        elapsed_cycles = 0; last_cyc = DWT->CYCCNT; next_due_ms = 0; slow_ctr = 0;
        g_log.running = 1;
    } else if (req == LOG_STOP_MAGIC) {
        g_log_req = 0;
        g_log.running = 0;
    }

    if (!g_log.running) { return; }

    update_clock();
    if (now_ms() < next_due_ms) { return; }

    {
        log_sample_t *s = &g_log_samples[g_log.count];
        uint8_t err = 0;
        uint32_t div = g_log_slow_div;
        uint8_t fresh = 0;
        if (div < 1U) { div = 1U; }
        if (div > 100U) { div = 100U; }
        s->t_ms = now_ms();
        s->cc2  = rd(0x3A, 2, &err);                 /* fast channel */
        s->fet  = (uint8_t)rd(0x7F, 1, &err);
        if (slow_ctr == 0U) {                        /* slow channel, then held until the next refresh */
            held.cell1     = rd(0x14, 2, &err);
            held.cell2     = rd(0x16, 2, &err);
            held.cell3     = rd(0x18, 2, &err);
            held.cell16    = rd(0x32, 2, &err);
            held.stack     = rd(0x34, 2, &err);
            held.pack      = rd(0x36, 2, &err);
            held.ld        = rd(0x38, 2, &err);
            held.alarm_raw = rd(0x64, 2, &err);
            held.battery   = rd(0x12, 2, &err);
            held.temp      = rd(0x68, 2, &err);
            fresh = 1U;
        }
        slow_ctr = (slow_ctr + 1U) % div;
        s->cell1 = held.cell1; s->cell2 = held.cell2; s->cell3 = held.cell3; s->cell16 = held.cell16;
        s->stack = held.stack; s->pack = held.pack;   s->ld = held.ld;
        s->alarm_raw = held.alarm_raw; s->battery = held.battery; s->temp = held.temp;
        s->flags     = (uint8_t)(err | (fresh << 1));
        if (err & 1U) { g_log.read_errors++; }
        g_log.count++;
    }
    next_due_ms += g_log.period_ms;
    if (g_log.count >= LOG_CAPACITY) { g_log.running = 0; g_log.full = 1; }
}
