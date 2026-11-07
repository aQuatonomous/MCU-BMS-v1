#ifndef USB_FS_H
#define USB_FS_H

/* Brings up the USB OTG full-speed core in DEVICE mode on PA11 (D-) / PA12 (D+).
 *
 * VBUS sensing is DISABLED (GCCFG.NOVBUSSENS): on this board PA9 is wired as
 * USART1_TX, not as the OTG_FS VBUS pin, so the core must not wait for VBUS.
 *
 * The device is deliberately left SOFT-DISCONNECTED (DCTL.SDIS = 1) so the host
 * does not see it yet - there is no USB stack/descriptors behind it.
 * Must be called after clock_init_hse_pll() returned CLOCK_OK (needs 48 MHz). */
void usb_fs_core_init(void);

#endif /* USB_FS_H */
