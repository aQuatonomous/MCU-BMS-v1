#include "stm32f205xx.h"
#include "i2c_bb.h"

#define SCL_PIN   6U   /* PB6 */
#define SDA_PIN   7U   /* PB7 */

/* Quarter bit period in CPU cycles. 120 MHz / 600 = 5 us -> ~50 kHz after overhead:
 * deliberately slow for first bring-up. */
#define QUARTER_CYCLES   600UL
#define STRETCH_TIMEOUT  (120000UL * 2UL)   /* ~2 ms in cycles */

static inline void delay_q(void)
{
    uint32_t s = DWT->CYCCNT;
    while ((DWT->CYCCNT - s) < QUARTER_CYCLES) { }
}

static inline void sda_hi(void) { GPIOB->BSRR = 1UL << SDA_PIN; }
static inline void sda_lo(void) { GPIOB->BSRR = 1UL << (SDA_PIN + 16U); }
static inline void scl_lo(void) { GPIOB->BSRR = 1UL << (SCL_PIN + 16U); }
static inline uint32_t sda_rd(void) { return (GPIOB->IDR >> SDA_PIN) & 1UL; }
static inline uint32_t scl_rd(void) { return (GPIOB->IDR >> SCL_PIN) & 1UL; }

/* Release SCL and wait for it to actually go high (slave may stretch). */
static i2c_status_t scl_hi(void)
{
    uint32_t s = DWT->CYCCNT;
    GPIOB->BSRR = 1UL << SCL_PIN;
    while (!scl_rd()) {
        if ((DWT->CYCCNT - s) > STRETCH_TIMEOUT) { return I2C_ERR_STRETCH; }
    }
    return I2C_OK;
}

void i2c_bb_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;                       /* let the clock settle */

    GPIOB->BSRR = (1UL << SCL_PIN) | (1UL << SDA_PIN);   /* high (released) before enabling outputs */
    GPIOB->OTYPER  |= (1UL << SCL_PIN) | (1UL << SDA_PIN);              /* open drain */
    GPIOB->PUPDR   &= ~((3UL << (SCL_PIN * 2U)) | (3UL << (SDA_PIN * 2U))); /* no internal pulls */
    GPIOB->OSPEEDR  = (GPIOB->OSPEEDR & ~((3UL << (SCL_PIN * 2U)) | (3UL << (SDA_PIN * 2U))))
                    | (1UL << (SCL_PIN * 2U)) | (1UL << (SDA_PIN * 2U)); /* medium speed */
    GPIOB->MODER    = (GPIOB->MODER & ~((3UL << (SCL_PIN * 2U)) | (3UL << (SDA_PIN * 2U))))
                    | (1UL << (SCL_PIN * 2U)) | (1UL << (SDA_PIN * 2U)); /* general-purpose output */

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t i2c_bb_line_levels(void)
{
    return scl_rd() | (sda_rd() << 1);
}

static i2c_status_t start(void)
{
    i2c_status_t st;
    sda_hi();
    st = scl_hi();
    if (st != I2C_OK) { return st; }
    delay_q();
    sda_lo();            /* SDA falls while SCL high = START (or repeated START) */
    delay_q();
    scl_lo();
    return I2C_OK;
}

static void stop(void)
{
    sda_lo();
    delay_q();
    (void)scl_hi();
    delay_q();
    sda_hi();            /* SDA rises while SCL high = STOP */
    delay_q();
}

/* Clock out one byte, return 1 if the slave ACKed, 0 if NACK, -1 on stretch timeout. */
static int write_byte(uint8_t b)
{
    int i, ack;
    for (i = 7; i >= 0; i--) {
        if (b & (1U << i)) { sda_hi(); } else { sda_lo(); }
        delay_q();
        if (scl_hi() != I2C_OK) { return -1; }
        delay_q();
        scl_lo();
        delay_q();
    }
    sda_hi();                       /* release SDA for the ACK bit */
    delay_q();
    if (scl_hi() != I2C_OK) { return -1; }
    delay_q();
    ack = (sda_rd() == 0U);         /* slave pulls SDA low to ACK */
    scl_lo();
    delay_q();
    return ack;
}

static int read_byte(uint8_t *out, int send_ack)
{
    int i;
    uint8_t v = 0;
    sda_hi();                       /* release SDA so the slave can drive it */
    for (i = 0; i < 8; i++) {
        delay_q();
        if (scl_hi() != I2C_OK) { return -1; }
        delay_q();
        v = (uint8_t)((v << 1) | (uint8_t)sda_rd());
        scl_lo();
    }
    if (send_ack) { sda_lo(); } else { sda_hi(); }
    delay_q();
    if (scl_hi() != I2C_OK) { return -1; }
    delay_q();
    scl_lo();
    sda_hi();
    delay_q();
    *out = v;
    return 0;
}

static i2c_status_t bus_idle_check(void)
{
    return (scl_rd() && sda_rd()) ? I2C_OK : I2C_ERR_BUS_BUSY;
}

i2c_status_t i2c_bb_probe(uint8_t addr7)
{
    i2c_status_t st = bus_idle_check();
    int a;
    if (st != I2C_OK) { return st; }
    st = start();
    if (st != I2C_OK) { stop(); return st; }
    a = write_byte((uint8_t)(addr7 << 1));          /* write direction: address-only probe */
    stop();
    if (a < 0) { return I2C_ERR_STRETCH; }
    return a ? I2C_OK : I2C_ERR_NACK_ADDR;
}

i2c_status_t i2c_bb_read(uint8_t addr7, uint8_t reg, uint8_t *buf, uint32_t n)
{
    i2c_status_t st = bus_idle_check();
    uint32_t i;
    int a;

    if (st != I2C_OK) { return st; }

    st = start();
    if (st != I2C_OK) { stop(); return st; }
    a = write_byte((uint8_t)(addr7 << 1));
    if (a < 0) { stop(); return I2C_ERR_STRETCH; }
    if (!a)    { stop(); return I2C_ERR_NACK_ADDR; }

    a = write_byte(reg);                            /* register pointer only - no data written */
    if (a < 0) { stop(); return I2C_ERR_STRETCH; }
    if (!a)    { stop(); return I2C_ERR_NACK_DATA; }

    st = start();                                   /* repeated START */
    if (st != I2C_OK) { stop(); return st; }
    a = write_byte((uint8_t)((addr7 << 1) | 1U));
    if (a < 0) { stop(); return I2C_ERR_STRETCH; }
    if (!a)    { stop(); return I2C_ERR_NACK_ADDR; }

    for (i = 0; i < n; i++) {
        if (read_byte(&buf[i], (i + 1U) < n) < 0) { stop(); return I2C_ERR_STRETCH; }
    }
    stop();
    return I2C_OK;
}
