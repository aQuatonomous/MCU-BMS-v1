#ifndef I2C_BB_H
#define I2C_BB_H

#include <stdint.h>

/* Bit-banged I2C master on PB6 (SCL) / PB7 (SDA). Both pins are open-drain outputs with
 * NO internal pull-ups: the board's external pull-ups must supply the high level.
 * Used for first contact with the BQ76952; ~50-100 kHz. */

typedef enum {
    I2C_OK            = 0,
    I2C_ERR_BUS_BUSY  = 1,  /* SCL or SDA held low before we started - nothing driven */
    I2C_ERR_NACK_ADDR = 2,  /* no device acknowledged the address                     */
    I2C_ERR_NACK_DATA = 3,  /* device NACKed the register/pointer byte                */
    I2C_ERR_STRETCH   = 4   /* SCL held low too long (clock stretching timeout)       */
} i2c_status_t;

void i2c_bb_init(void);                       /* pins released (high); enables DWT timer */
uint32_t i2c_bb_line_levels(void);            /* bit0 = SCL, bit1 = SDA (as read from pins) */

/* Address-only probe: START, addr+W, check ACK, STOP. 7-bit address. */
i2c_status_t i2c_bb_probe(uint8_t addr7);

/* Read n bytes starting at register reg: START, addr+W, reg, REPEATED START, addr+R,
 * n bytes (ACK all but last), STOP. */
i2c_status_t i2c_bb_read(uint8_t addr7, uint8_t reg, uint8_t *buf, uint32_t n);

#endif /* I2C_BB_H */
