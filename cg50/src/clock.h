/* Time: a free-running TMU for microseconds (profiling) and a 1 kHz timer that counts
   milliseconds (pacing) and wakes the CPU from sleep(). */
#ifndef PPL_CLOCK_H
#define PPL_CLOCK_H

#include <stdint.h>

extern volatile uint32_t clock_ms; /* +1 every ~ms (1 kHz timer, an ETMU: about 0.7 % slow) */

void clock_init(void);
void clock_quit(void);
uint32_t clock_now(void);                          /* TMU timestamp (counts down) */
uint32_t clock_us(uint32_t start, uint32_t end);   /* microseconds between two (< ~9 min) */
/* real milliseconds since the first call, from the TMU (call it at least every 9 minutes;
   it stands still while the OS runs a file operation) */
uint32_t clock_real_ms(void);

/* the CG50's clock (RTC) in seconds since 2000-01-01, 0 if it isn't plausibly set */
uint32_t clock_cg_seconds(void);

#endif
