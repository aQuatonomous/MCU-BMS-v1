#ifndef RST_SHUT_H
#define RST_SHUT_H

#include <stdint.h>

/* Owner-directed, one-shot control of the BQ76952 RST_SHUT pin via PC0.
 *
 * Never automatic: the owner's SWD tool writes a magic value to g_rst_shut_req, which is
 * cleared before anything happens. PC0 is the ONLY pin touched (PC1-PC5 stay untouched
 * inputs). PC0 is left as a floating input until stage A is requested.
 *
 *   BQ_RST_A_MAGIC : PC0 -> push-pull output, driven LOW (output latch set low BEFORE the
 *                    pin is switched to output, so there is no high glitch).
 *   BQ_RST_B_MAGIC : only if stage A has run: PC0 HIGH for g_rst_shut_pulse_ms, then LOW.
 *                    Pulse length is clamped to 20..900 ms (owner: "less than 1 second").
 */
#define BQ_RST_A_MAGIC  0xFE7E0C01UL
#define BQ_RST_B_MAGIC  0xFE7E0C02UL

typedef struct {
    uint32_t stage_a_done;
    uint32_t pulses_done;
    uint32_t last_pulse_ms;
    uint32_t pc0_idr_after_a;      /* PC0 as read back by the MCU after stage A (0 = low)      */
    uint32_t pc0_idr_after_pulse;  /* PC0 readback after the pulse ended (0 = low)             */
    uint32_t pc0_idr_during_pulse; /* PC0 readback just before the pulse ended (1 = high)      */
    uint32_t rejected;             /* request refused (e.g. pulse before stage A)             */
} rst_shut_res_t;

extern volatile uint32_t g_rst_shut_req;
extern volatile uint32_t g_rst_shut_pulse_ms;       /* default 200 */
extern volatile rst_shut_res_t g_rst_shut_res;

void rst_shut_init_low(void);  /* boot default: PC0 output, held LOW */
void rst_shut_service(void);   /* call from the main loop */

#endif /* RST_SHUT_H */
