#include <string.h>
#include "stm32f205xx.h"
#include "usb_device.h"

#define OTG_DEV   ((USB_OTG_DeviceTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE))
#define OTG_IN0   ((USB_OTG_INEndpointTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_IN_ENDPOINT_BASE))
#define OTG_OUT0  ((USB_OTG_OUTEndpointTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_OUT_ENDPOINT_BASE))
#define FIFO0     ((volatile uint32_t *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_FIFO_BASE))

#define WAIT_LOOPS 200000UL

volatile usb_dbg_t g_usb_dbg;

/* ---- Descriptors ------------------------------------------------------------ */

/* VID/PID 0x1209:0x0001 is the pid.codes shared "for testing only" pair. Replace with
 * a real, owned VID/PID before this board is used or shipped beyond the bench. */
static const uint8_t dev_desc[18] = {
    18, 0x01,             /* bLength, DEVICE                     */
    0x00, 0x02,           /* bcdUSB 2.00                         */
    0x00, 0x00, 0x00,     /* class/subclass/protocol: per-interface */
    64,                   /* bMaxPacketSize0                     */
    0x09, 0x12,           /* idVendor  0x1209                    */
    0x01, 0x00,           /* idProduct 0x0001                    */
    0x00, 0x01,           /* bcdDevice 1.00                      */
    1, 2, 3,              /* iManufacturer, iProduct, iSerial    */
    1                     /* bNumConfigurations                  */
};

static const uint8_t cfg_desc[18] = {
    9, 0x02, 18, 0,       /* CONFIGURATION, wTotalLength = 18    */
    1, 1, 0,              /* 1 interface, config value 1, no string */
    0x80, 50,             /* bus powered, 100 mA                 */
    9, 0x04, 0, 0, 0,     /* INTERFACE 0, alt 0, 0 endpoints     */
    0xFF, 0x00, 0x00, 0  /* vendor-specific class               */
};

static const char *const strings[] = { "Aquatonomous", "BMS MCU V1", "0001" };

/* ---- Low-level helpers ------------------------------------------------------ */

static void ep0_arm_out(void)
{
    /* Ready to receive up to 3 back-to-back SETUP packets (24 bytes) or a status ZLP. */
    OTG_OUT0->DOEPTSIZ = (3UL << 29) | (1UL << 19) | 24UL;
    OTG_OUT0->DOEPCTL |= USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_CNAK;
}

static void ep0_stall(void)
{
    OTG_IN0->DIEPCTL  |= USB_OTG_DIEPCTL_STALL;
    OTG_OUT0->DOEPCTL |= USB_OTG_DOEPCTL_STALL;
    g_usb_dbg.stalls++;
}

/* Send len bytes (len <= 127 and fits the 256-byte EP0 TX FIFO) on EP0 IN. */
static void ep0_send(const uint8_t *data, uint32_t len)
{
    uint32_t pkts = (len == 0U) ? 1U : (len + 63U) / 64U;
    uint32_t words = (len + 3U) / 4U;
    uint32_t i, t;

    OTG_IN0->DIEPINT  = USB_OTG_DIEPINT_XFRC;
    OTG_IN0->DIEPTSIZ = (pkts << 19) | len;
    OTG_IN0->DIEPCTL |= USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_CNAK;

    for (i = 0; i < words; i++) {
        uint32_t w = 0;
        uint32_t n = len - i * 4U;
        memcpy(&w, data + i * 4U, n > 4U ? 4U : n);
        FIFO0[0] = w;
    }

    for (t = WAIT_LOOPS; t && !(OTG_IN0->DIEPINT & USB_OTG_DIEPINT_XFRC); t--) { }
    OTG_IN0->DIEPINT = USB_OTG_DIEPINT_XFRC;
}

static void flush_fifos(void)
{
    uint32_t t;
    USB_OTG_FS->GRSTCTL = USB_OTG_GRSTCTL_TXFFLSH | (0x10UL << USB_OTG_GRSTCTL_TXFNUM_Pos);
    for (t = WAIT_LOOPS; t && (USB_OTG_FS->GRSTCTL & USB_OTG_GRSTCTL_TXFFLSH); t--) { }
    USB_OTG_FS->GRSTCTL = USB_OTG_GRSTCTL_RXFFLSH;
    for (t = WAIT_LOOPS; t && (USB_OTG_FS->GRSTCTL & USB_OTG_GRSTCTL_RXFFLSH); t--) { }
}

/* ---- Request handling --------------------------------------------------------- */

static void send_string(uint8_t idx, uint16_t wlen)
{
    uint8_t buf[64];
    uint32_t n = 0;

    if (idx == 0) {                         /* supported languages: US English */
        buf[0] = 4; buf[1] = 0x03; buf[2] = 0x09; buf[3] = 0x04;
        n = 4;
    } else if (idx <= 3) {
        const char *s = strings[idx - 1];
        uint32_t i;
        for (i = 0; s[i] && i < 30U; i++) {
            buf[2 + 2 * i] = (uint8_t)s[i];
            buf[3 + 2 * i] = 0;
        }
        n = 2U + 2U * i;
        buf[0] = (uint8_t)n; buf[1] = 0x03;
    } else {
        ep0_stall();
        return;
    }
    ep0_send(buf, n < wlen ? n : wlen);
}

static void handle_setup(const uint8_t *s)
{
    uint8_t  type  = s[0];
    uint8_t  req   = s[1];
    uint16_t value = (uint16_t)(s[2] | (s[3] << 8));
    uint16_t wlen  = (uint16_t)(s[6] | (s[7] << 8));

    if (type == 0x80 && req == 0x06) {                  /* GET_DESCRIPTOR */
        switch (value >> 8) {
        case 0x01: ep0_send(dev_desc, wlen < sizeof dev_desc ? wlen : sizeof dev_desc); break;
        case 0x02: ep0_send(cfg_desc, wlen < sizeof cfg_desc ? wlen : sizeof cfg_desc); break;
        case 0x03: send_string((uint8_t)(value & 0xFF), wlen); break;
        default:   ep0_stall(); break;                   /* qualifier, BOS, ... */
        }
    } else if (type == 0x00 && req == 0x05) {           /* SET_ADDRESS */
        OTG_DEV->DCFG = (OTG_DEV->DCFG & ~USB_OTG_DCFG_DAD) | (((uint32_t)(value & 0x7F)) << USB_OTG_DCFG_DAD_Pos);
        g_usb_dbg.address = value & 0x7F;
        ep0_send(0, 0);
    } else if (type == 0x00 && req == 0x09) {           /* SET_CONFIGURATION */
        g_usb_dbg.configured = (value & 0xFF) ? 1U : 0U;
        ep0_send(0, 0);
    } else if (type == 0x80 && req == 0x08) {           /* GET_CONFIGURATION */
        uint8_t c = (uint8_t)g_usb_dbg.configured;
        ep0_send(&c, 1);
    } else if ((type & 0x7F) <= 0x02 && (type & 0x80) && req == 0x00) { /* GET_STATUS */
        static const uint8_t status[2] = {0, 0};
        ep0_send(status, 2);
    } else if (type == 0x01 && req == 0x0B) {           /* SET_INTERFACE (alt 0 only) */
        ep0_send(0, 0);
    } else {
        ep0_stall();
    }
}

/* ---- Public API --------------------------------------------------------------- */

void usb_device_connect(void)
{
    /* FIFO layout (32-bit words, 320 total): RX 128 | EP0 TX 64 (256 B) */
    USB_OTG_FS->GRXFSIZ = 128UL;
    USB_OTG_FS->DIEPTXF0_HNPTXFSIZ = (64UL << 16) | 128UL;

    OTG_DEV->DCTL &= ~USB_OTG_DCTL_SDIS;   /* present the D+ pull-up to the host */
}

void usb_device_poll(void)
{
    uint32_t sts = USB_OTG_FS->GINTSTS;

    if (sts & USB_OTG_GINTSTS_USBRST) {
        USB_OTG_FS->GINTSTS = USB_OTG_GINTSTS_USBRST;
        g_usb_dbg.usb_resets++;
        g_usb_dbg.address = 0;
        g_usb_dbg.configured = 0;
        OTG_DEV->DCFG &= ~USB_OTG_DCFG_DAD;
        flush_fifos();
        ep0_arm_out();
    }

    if (sts & USB_OTG_GINTSTS_ENUMDNE) {
        USB_OTG_FS->GINTSTS = USB_OTG_GINTSTS_ENUMDNE;
        g_usb_dbg.enum_done++;
        OTG_IN0->DIEPCTL &= ~USB_OTG_DIEPCTL_MPSIZ;   /* 0 = 64-byte EP0 packets */
        OTG_DEV->DCTL |= USB_OTG_DCTL_CGINAK;
    }

    if (sts & USB_OTG_GINTSTS_RXFLVL) {
        uint32_t r      = USB_OTG_FS->GRXSTSP;          /* pop status */
        uint32_t bcnt   = (r >> 4) & 0x7FFUL;
        uint32_t pktsts = (r >> 17) & 0xFUL;
        static uint32_t setup[2];

        if (pktsts == 6U) {                             /* SETUP data received */
            setup[0] = FIFO0[0];
            setup[1] = FIFO0[0];
            memcpy((void *)g_usb_dbg.last_setup, setup, 8);
            g_usb_dbg.setup_packets++;
        } else if (pktsts == 2U) {                      /* OUT data: not used, drain */
            uint32_t i;
            for (i = 0; i < (bcnt + 3U) / 4U; i++) { (void)FIFO0[0]; }
        } else if (pktsts == 4U) {                      /* SETUP stage complete */
            OTG_OUT0->DOEPINT = USB_OTG_DOEPINT_STUP | USB_OTG_DOEPINT_XFRC;
            handle_setup((const uint8_t *)setup);
            ep0_arm_out();
        } else if (pktsts == 3U) {                      /* OUT transfer complete (status ZLP) */
            OTG_OUT0->DOEPINT = USB_OTG_DOEPINT_XFRC;
            ep0_arm_out();
        }
    }
}
