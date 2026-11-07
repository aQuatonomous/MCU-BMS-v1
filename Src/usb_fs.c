#include "stm32f205xx.h"
#include "usb_fs.h"
#include "clock.h"

#define OTG_DEV  ((USB_OTG_DeviceTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE))

void usb_fs_core_init(void)
{
    /* GPIOA clock, then PA11/PA12 -> AF10 (OTG_FS_DM / OTG_FS_DP), very high speed,
     * no pull (the OTG core provides the D+ pull-up internally). */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    GPIOA->MODER   = (GPIOA->MODER   & ~((3UL << 22) | (3UL << 24))) | ((2UL << 22) | (2UL << 24));
    GPIOA->OSPEEDR |= (3UL << 22) | (3UL << 24);
    GPIOA->PUPDR   &= ~((3UL << 22) | (3UL << 24));
    GPIOA->AFR[1]   = (GPIOA->AFR[1] & ~((0xFUL << 12) | (0xFUL << 16))) | ((10UL << 12) | (10UL << 16));

    /* Peripheral clock for the OTG_FS core. */
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;

    /* Select the on-chip full-speed PHY, then core soft reset. */
    USB_OTG_FS->GUSBCFG |= USB_OTG_GUSBCFG_PHYSEL;
    while (!(USB_OTG_FS->GRSTCTL & USB_OTG_GRSTCTL_AHBIDL)) { }
    USB_OTG_FS->GRSTCTL |= USB_OTG_GRSTCTL_CSRST;
    while (USB_OTG_FS->GRSTCTL & USB_OTG_GRSTCTL_CSRST) { }

    /* Transceiver on; VBUS sensing off (PA9 is USART1_TX on this board). */
    USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_PWRDWN | USB_OTG_GCCFG_NOVBUSSENS;
    USB_OTG_FS->GCCFG &= ~(USB_OTG_GCCFG_VBUSASEN | USB_OTG_GCCFG_VBUSBSEN);

    /* Force device mode; the core needs >= 25 ms to switch. USB turnaround time
     * TRDT = 6 for an AHB clock >= 32 MHz (we run at 120 MHz). */
    USB_OTG_FS->GUSBCFG = (USB_OTG_FS->GUSBCFG & ~(USB_OTG_GUSBCFG_FHMOD | (0xFUL << USB_OTG_GUSBCFG_TRDT_Pos)))
                        | USB_OTG_GUSBCFG_FDMOD | (6UL << USB_OTG_GUSBCFG_TRDT_Pos);
    delay_ms(50);

    /* Device config: full speed (DSPD = 11). Stay soft-disconnected, no interrupts. */
    OTG_DEV->DCFG |= USB_OTG_DCFG_DSPD;
    OTG_DEV->DCTL |= USB_OTG_DCTL_SDIS;
    USB_OTG_FS->GINTMSK = 0UL;
    USB_OTG_FS->GINTSTS = 0xFFFFFFFFUL; /* clear pending (write-1-to-clear) */
}
