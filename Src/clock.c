#include "stm32f205xx.h"
#include "clock.h"

/* PLL factors (agreed with project owner):
 *   VCO in  = 16 MHz / M(16)  =   1 MHz
 *   VCO out = 1 MHz  * N(240) = 240 MHz
 *   SYSCLK  = 240 MHz / P(2)  = 120 MHz
 *   USB     = 240 MHz / Q(5)  =  48 MHz
 */
#define PLL_M   16UL
#define PLL_N   240UL
#define PLL_P_DIV2  0UL   /* PLLP field: 00 = /2 */
#define PLL_Q   5UL

#define CLK_TIMEOUT  1000000UL  /* loop iterations; generous at 16 MHz HSI */

volatile clock_status_t g_clock_status = CLOCK_ERR_HSE;

clock_status_t clock_init_hse_pll(void)
{
    uint32_t t;

    /* 1. Start the crystal oscillator and wait for it. */
    RCC->CR |= RCC_CR_HSEON;
    for (t = CLK_TIMEOUT; t && !(RCC->CR & RCC_CR_HSERDY); t--) { }
    if (!(RCC->CR & RCC_CR_HSERDY)) {
        RCC->CR &= ~RCC_CR_HSEON;
        g_clock_status = CLOCK_ERR_HSE;
        return g_clock_status;
    }

    /* 2. Flash wait states BEFORE raising the clock: 3 WS for 120 MHz at 2.7-3.6 V.
     *    Enable prefetch + instruction/data cache. */
    FLASH->ACR = FLASH_ACR_LATENCY_3WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    /* 3. Bus prescalers: AHB /1, APB1 /4 (30 MHz max), APB2 /2 (60 MHz max). */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2))
              | RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

    /* 4. Configure and start the PLL (PLL must be off while PLLCFGR is written). */
    RCC->PLLCFGR = (PLL_M << 0) | (PLL_N << 6) | (PLL_P_DIV2 << 16)
                 | RCC_PLLCFGR_PLLSRC_HSE | (PLL_Q << 24);
    RCC->CR |= RCC_CR_PLLON;
    for (t = CLK_TIMEOUT; t && !(RCC->CR & RCC_CR_PLLRDY); t--) { }
    if (!(RCC->CR & RCC_CR_PLLRDY)) {
        RCC->CR &= ~RCC_CR_PLLON;
        g_clock_status = CLOCK_ERR_PLL;
        return g_clock_status;
    }

    /* 5. Switch SYSCLK to the PLL and confirm. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    for (t = CLK_TIMEOUT; t && ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL); t--) { }
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
        g_clock_status = CLOCK_ERR_SWITCH;
        return g_clock_status;
    }

    SystemCoreClockUpdate();
    g_clock_status = CLOCK_OK;
    return g_clock_status;
}

void delay_ms(uint32_t ms)
{
    SysTick->LOAD = (SystemCoreClock / 1000UL) - 1UL;
    SysTick->VAL  = 0UL;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; /* no interrupt */
    while (ms--) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)) { }
    }
    SysTick->CTRL = 0UL;
}
