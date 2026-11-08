#include "stm32f205xx.h"
#include "rst_shut.h"

#define PC0_MASK  (1UL << 0)

volatile uint32_t g_rst_shut_req;
volatile uint32_t g_rst_shut_pulse_ms = 200;
volatile rst_shut_res_t g_rst_shut_res;

static void wait_ms(uint32_t ms)
{
    uint32_t s = DWT->CYCCNT;
    uint32_t n = ms * (SystemCoreClock / 1000UL);
    while ((DWT->CYCCNT - s) < n) { }
}

void rst_shut_service(void)
{
    uint32_t req = g_rst_shut_req;
    uint32_t ms;

    if (req != BQ_RST_A_MAGIC && req != BQ_RST_B_MAGIC) { return; }
    g_rst_shut_req = 0;                         /* clear FIRST: a request can never run twice */

    if (req == BQ_RST_A_MAGIC) {
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
        (void)RCC->AHB1ENR;
        GPIOC->BSRR    = PC0_MASK << 16;                    /* output latch low first      */
        GPIOC->OTYPER &= ~PC0_MASK;                         /* push-pull                   */
        GPIOC->PUPDR  &= ~(3UL << 0);                       /* no pull                     */
        GPIOC->OSPEEDR &= ~(3UL << 0);                      /* low speed                   */
        GPIOC->MODER   = (GPIOC->MODER & ~(3UL << 0)) | (1UL << 0);  /* PC0 = output      */
        g_rst_shut_res.pc0_idr_after_a = GPIOC->IDR & PC0_MASK;
        g_rst_shut_res.stage_a_done = 1;
        return;
    }

    /* Stage B: pulse. Refused unless PC0 is already an output held low. */
    if (!g_rst_shut_res.stage_a_done) { g_rst_shut_res.rejected++; return; }
    ms = g_rst_shut_pulse_ms;
    if (ms < 20UL)  { ms = 20UL; }
    if (ms > 900UL) { ms = 900UL; }

    GPIOC->BSRR = PC0_MASK;                                  /* high */
    wait_ms(ms);
    g_rst_shut_res.pc0_idr_during_pulse = GPIOC->IDR & PC0_MASK;
    GPIOC->BSRR = PC0_MASK << 16;                            /* back low */
    g_rst_shut_res.pc0_idr_after_pulse = GPIOC->IDR & PC0_MASK;
    g_rst_shut_res.last_pulse_ms = ms;
    g_rst_shut_res.pulses_done++;
}

/* Called first thing in main(): hold RST_SHUT (PC0) LOW from boot (owner-approved default).
 * Same glitch-free order as stage A: latch low, then switch the pin to output. */
void rst_shut_init_low(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;
    GPIOC->BSRR    = PC0_MASK << 16;
    GPIOC->OTYPER &= ~PC0_MASK;
    GPIOC->PUPDR  &= ~(3UL << 0);
    GPIOC->OSPEEDR &= ~(3UL << 0);
    GPIOC->MODER   = (GPIOC->MODER & ~(3UL << 0)) | (1UL << 0);
    g_rst_shut_res.pc0_idr_after_a = GPIOC->IDR & PC0_MASK;
    g_rst_shut_res.stage_a_done = 1;
}
