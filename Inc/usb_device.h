#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <stdint.h>

/* Minimal polled USB full-speed device: control endpoint 0 only, enough for the host
 * to enumerate it. Shows up on the host as "BMS MCU V1" (vendor-specific class, no
 * data endpoints). Proves clock + wiring + D+ pull-up; data transfer comes later.
 *
 * Call usb_device_connect() once after usb_fs_core_init(), then usb_device_poll()
 * as often as possible from the main loop (no interrupts are used). */

/* Visible over SWD for debugging (find the address with arm-none-eabi-nm). */
typedef struct {
    uint32_t usb_resets;       /* bus resets seen                          */
    uint32_t enum_done;        /* enumeration-done events (speed detected) */
    uint32_t setup_packets;    /* SETUP packets received on EP0            */
    uint32_t stalls;           /* requests we did not support (STALLed)    */
    uint32_t configured;       /* 1 once host sent SET_CONFIGURATION       */
    uint32_t address;          /* USB address assigned by host             */
    uint8_t  last_setup[8];    /* most recent SETUP packet                 */
} usb_dbg_t;

extern volatile usb_dbg_t g_usb_dbg;

void usb_device_connect(void);  /* clears soft-disconnect: host now sees the D+ pull-up */
void usb_device_poll(void);

#endif /* USB_DEVICE_H */
