/*
 * PhantomOS Timer (PIT) Implementation
 * "To Create, Not To Destroy"
 */

#include "timer.h"
#include "kvm_clock.h"
#include "idt.h"
#include "pic.h"

/* External functions */
extern int kprintf(const char *fmt, ...);

/* Port I/O helpers */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Tick counter */
static volatile uint64_t timer_ticks = 0;

/* Forward declaration for scheduler */
extern void scheduler_tick(void);
__attribute__((weak)) void scheduler_tick(void) { }

/*
 * Timer interrupt handler (IRQ0)
 */
static void timer_handler(struct interrupt_frame *frame)
{
    (void)frame;

    timer_ticks++;

    /* Call scheduler tick (if scheduler is initialized) */
    scheduler_tick();

    /* Send EOI */
    pic_send_eoi(0);
}

/*
 * Initialize the PIT
 */
void timer_init(void)
{
    /* Calculate divisor for desired frequency */
    uint16_t divisor = PIT_BASE_FREQ / TIMER_FREQUENCY;

    /* Set PIT to mode 3 (square wave generator) on channel 0 */
    outb(PIT_COMMAND, 0x36);    /* Channel 0, lobyte/hibyte, mode 3 */

    /* Send divisor */
    outb(PIT_CHANNEL0, divisor & 0xFF);         /* Low byte */
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);  /* High byte */

    /* Register interrupt handler */
    register_interrupt_handler(IRQ_TIMER, timer_handler);

    /* Enable timer IRQ */
    pic_enable_irq(0);

    kprintf("  [OK] Timer initialized (%d Hz)\n", TIMER_FREQUENCY);
}

/*
 * Get current tick count
 */
uint64_t timer_get_ticks(void)
{
    return timer_ticks;
}

/*
 * Sleep for specified milliseconds (busy wait)
 */
void timer_sleep_ms(uint32_t ms)
{
    uint64_t target = timer_ticks + (ms * TIMER_FREQUENCY / 1000);
    if (target == timer_ticks) target++;  /* At least one tick */

    while (timer_ticks < target) {
        __asm__ volatile ("hlt");  /* Wait for interrupt */
    }
}

/*
 * High-resolution nanosecond timer
 */
uint64_t timer_get_ns(void)
{
    if (kvm_clock_available())
        return kvm_clock_read_ns();
    /* Fallback: 10ms per tick = 10,000,000 ns per tick */
    return timer_ticks * 10000000ULL;
}

/*
 * Milliseconds since boot
 */
uint64_t timer_get_ms(void)
{
    return timer_get_ns() / 1000000ULL;
}

/*
 * PC Speaker - PIT Channel 2
 */
void speaker_play_tone(uint32_t freq_hz)
{
    if (freq_hz == 0) { speaker_stop(); return; }
    uint16_t div = (uint16_t)(PIT_BASE_FREQ / freq_hz);
    outb(PIT_COMMAND, 0xB6);                    /* Channel 2, lobyte/hibyte, mode 3 */
    outb(PIT_CHANNEL2, div & 0xFF);             /* Low byte of divisor */
    outb(PIT_CHANNEL2, (div >> 8) & 0xFF);      /* High byte of divisor */
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp | 0x03);                     /* Enable speaker gate (bits 0+1) */
}

void speaker_stop(void)
{
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp & 0xFC);                     /* Disable speaker gate */
}
