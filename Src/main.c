/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : BMS MCU V1 - clock + USB core bring-up
 ******************************************************************************
 */

#include <stdint.h>
#include "stm32f205xx.h"
#include "clock.h"
#include "usb_fs.h"
#include "usb_device.h"

int main(void)
{
    /* HSE 16 MHz -> PLL -> 120 MHz core, 48 MHz USB. On failure we stay on HSI and
     * g_clock_status says why (readable over SWD); USB is not touched in that case. */
    if (clock_init_hse_pll() == CLOCK_OK) {
        usb_fs_core_init();
        usb_device_connect();
        for (;;) {
            usb_device_poll();
        }
    }

    for (;;) { }
}
