#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

/* Result of clock_init_hse_pll(). Kept in a global (g_clock_status) so it can be
 * read over SWD without a debugger-attached printf. */
typedef enum {
    CLOCK_OK            = 0,  /* running from PLL: SYSCLK 120 MHz, USB 48 MHz */
    CLOCK_ERR_HSE       = 1,  /* HSE never became ready; still on HSI 16 MHz  */
    CLOCK_ERR_PLL       = 2,  /* PLL never locked; still on HSI 16 MHz        */
    CLOCK_ERR_SWITCH    = 3   /* PLL ready but SYSCLK switch did not happen   */
} clock_status_t;

#define HSE_HZ        16000000UL   /* 16 MHz crystal on PH0/PH1 */
#define SYSCLK_HZ    120000000UL
#define USB_CLK_HZ    48000000UL

extern volatile clock_status_t g_clock_status;

/* HSE 16 MHz -> PLL (M=16, N=240, P=2, Q=5) -> SYSCLK 120 MHz, USB 48 MHz.
 * AHB /1 = 120, APB1 /4 = 30, APB2 /2 = 60 MHz, flash 3 wait states.
 * On any failure the MCU is left on HSI and a status code is returned. */
clock_status_t clock_init_hse_pll(void);

/* Busy-wait using SysTick. Only valid after clock_init_hse_pll(). */
void delay_ms(uint32_t ms);

#endif /* CLOCK_H */
