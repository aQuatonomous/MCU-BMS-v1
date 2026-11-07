/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : BMS MCU V1 - clock + USB device + read-only BQ76952 I2C probe
 ******************************************************************************
 */

#include <stdint.h>
#include "stm32f205xx.h"
#include "clock.h"
#include "usb_fs.h"
#include "usb_device.h"
#include "bms_probe.h"
#include "bq_dm.h"

#define BMS_PASS_PERIOD_CYCLES  (SYSCLK_HZ / 2UL)   /* one BMS read pass every 0.5 s */
#define BQ_DM_PERIOD_CYCLES     (SYSCLK_HZ * 10UL)  /* config read-out every 10 s    */

int main(void)
{
    /* HSE 16 MHz -> PLL -> 120 MHz core, 48 MHz USB. On failure we stay on HSI and
     * g_clock_status says why (readable over SWD); nothing else is started. */
    if (clock_init_hse_pll() == CLOCK_OK) {
        uint32_t last, last_dm;

        usb_fs_core_init();
        usb_device_connect();
        bms_probe_init();           /* also starts the DWT cycle counter */
        last = DWT->CYCCNT;
        last_dm = last - BQ_DM_PERIOD_CYCLES + SYSCLK_HZ * 2UL;   /* first config read ~2 s after boot */

        for (;;) {
            usb_device_poll();
            if ((DWT->CYCCNT - last) >= BMS_PASS_PERIOD_CYCLES) {
                last = DWT->CYCCNT;
                bms_probe_pass();
            }
            if ((DWT->CYCCNT - last_dm) >= BQ_DM_PERIOD_CYCLES) {
                last_dm = DWT->CYCCNT;
                bq_dm_pass();
            }
        }
    }

    for (;;) { }
}
