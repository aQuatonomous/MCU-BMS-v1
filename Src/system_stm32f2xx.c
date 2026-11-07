/* Minimal CMSIS system file. The startup code calls SystemInit() first thing
 * after reset; the real clock setup is done later by clock_init_hse_pll(). */
#include "stm32f205xx.h"

/* Updated by SystemCoreClockUpdate(). Reset value is HSI = 16 MHz. */
uint32_t SystemCoreClock = 16000000UL;

const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8]  = {0, 0, 0, 0, 1, 2, 3, 4};

void SystemInit(void)
{
    /* Vector table is at the start of flash. */
    SCB->VTOR = FLASH_BASE;
}

/* Recomputes SystemCoreClock from the RCC registers (HSE assumed 16 MHz). */
void SystemCoreClockUpdate(void)
{
    uint32_t sysclk;
    uint32_t sws = RCC->CFGR & RCC_CFGR_SWS;

    if (sws == RCC_CFGR_SWS_HSE) {
        sysclk = 16000000UL;
    } else if (sws == RCC_CFGR_SWS_PLL) {
        uint32_t pllcfgr = RCC->PLLCFGR;
        uint32_t m   = pllcfgr & 0x3FUL;
        uint32_t n   = (pllcfgr >> 6) & 0x1FFUL;
        uint32_t p   = (((pllcfgr >> 16) & 0x3UL) + 1UL) * 2UL;
        /* PLL input is 16 MHz either way: HSI is 16 MHz and the HSE crystal is 16 MHz. */
        sysclk = (16000000UL / m) * n / p;
    } else {
        sysclk = 16000000UL; /* HSI */
    }

    SystemCoreClock = sysclk >> AHBPrescTable[(RCC->CFGR & RCC_CFGR_HPRE) >> 4];
}
