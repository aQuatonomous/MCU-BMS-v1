#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>

/* Timestamped RAM logger for load tests. READ-ONLY on the BQ (direct commands only);
 * every sample is stored in the MCU's RAM - nothing is ever stored in, or written to, the BQ.
 *
 * LEAN READS: each sample reads only the fast channel (CC2 current 0x3A, FET status 0x7F).
 * The slow channel (cells, stack, pack, LD, alarm, battery status, temperature) is read once
 * every g_log_slow_div samples (default 5 -> every 250 ms at 50 ms) and held in between,
 * because the BQ itself refreshes those values slowly. That is ~80 reads/s instead of 240/s.
 * Start/stop is owner-triggered over SWD; the sample array is dumped afterwards in one
 * SWD read (tools/dump_log_csv.py). While running, the slower background passes
 * (bms_probe_pass / bq_dm_pass) are paused so the sample period stays steady. */

#define LOG_START_MAGIC  0xFE7E1001UL
#define LOG_STOP_MAGIC   0xFE7E1002UL
#define LOG_CAPACITY     2048U

typedef struct {
    uint32_t t_ms;      /* ms since start                                            */
    uint16_t cell1, cell2, cell3, cell16;   /* 0x14, 0x16, 0x18, 0x32 (mV)           */
    uint16_t stack, pack, ld;               /* 0x34, 0x36, 0x38 (raw, unsigned)      */
    uint16_t cc2;                           /* 0x3A (raw; interpret as signed)        */
    uint16_t alarm_raw;                     /* 0x64                                  */
    uint16_t battery;                       /* 0x12                                  */
    uint16_t temp;                          /* 0x68                                  */
    uint8_t  fet;                           /* 0x7F                                  */
    uint8_t  flags;                         /* bit0: a read failed (value = 0xFFFF); bit1: slow channel is FRESH in this sample */
} log_sample_t;                             /* 28 bytes */

typedef struct {
    uint32_t running;
    uint32_t count;         /* valid samples                                          */
    uint32_t capacity;
    uint32_t period_ms;     /* effective period (clamped 20..1000)                    */
    uint32_t read_errors;   /* samples with flags bit0                                */
    uint32_t full;          /* 1 once capacity reached (logging stops by itself)      */
} log_hdr_t;

extern volatile uint32_t g_log_req;
extern volatile uint32_t g_log_period_ms;   /* default 50 (20 Hz) */
extern volatile uint32_t g_log_slow_div;    /* slow channel every N samples, default 5 (clamped 1..100) */
extern volatile log_hdr_t g_log;
extern log_sample_t g_log_samples[LOG_CAPACITY];

void logger_service(void);   /* call from the main loop */

#endif /* LOGGER_H */
