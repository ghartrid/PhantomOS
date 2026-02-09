/*
 * PhantomOS Timer (PIT)
 * "To Create, Not To Destroy"
 *
 * Programmable Interval Timer for system tick.
 */

#ifndef _TIMER_H
#define _TIMER_H

#include <stdint.h>

/* Timer frequency in Hz */
#define TIMER_FREQUENCY     100     /* 100 Hz = 10ms tick */

/* PIT ports */
#define PIT_CHANNEL0        0x40
#define PIT_CHANNEL1        0x41
#define PIT_CHANNEL2        0x42
#define PIT_COMMAND         0x43

/* PIT base frequency */
#define PIT_BASE_FREQ       1193182

/* Initialize timer */
void timer_init(void);

/* Get current tick count */
uint64_t timer_get_ticks(void);

/* Sleep for specified milliseconds (busy wait) */
void timer_sleep_ms(uint32_t ms);

/* High-resolution time (nanoseconds) - uses KVM pvclock when available */
uint64_t timer_get_ns(void);

/* Milliseconds since boot (higher precision than tick-based) */
uint64_t timer_get_ms(void);

/* PC Speaker (PIT Channel 2) */
void speaker_play_tone(uint32_t freq_hz);
void speaker_stop(void);

#endif /* _TIMER_H */
