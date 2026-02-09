/*
 * PhantomOS Port I/O Primitives
 * "To Create, Not To Destroy"
 *
 * Shared header for x86 port I/O operations.
 * Replaces duplicate inline definitions across drivers.
 */

#ifndef PHANTOMOS_IO_H
#define PHANTOMOS_IO_H

#include <stdint.h>

/*============================================================================
 * 8-bit Port I/O
 *============================================================================*/

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/*============================================================================
 * 16-bit Port I/O
 *============================================================================*/

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/*============================================================================
 * 32-bit Port I/O
 *============================================================================*/

static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/*============================================================================
 * I/O Wait (short delay for PIC/PIT programming)
 *============================================================================*/

static inline void io_wait(void)
{
    outb(0x80, 0);
}

/*============================================================================
 * Model-Specific Registers (MSR)
 *============================================================================*/

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val)
{
    __asm__ volatile("wrmsr" : : "c"(msr),
        "a"((uint32_t)(val & 0xFFFFFFFF)),
        "d"((uint32_t)(val >> 32)));
}

/*============================================================================
 * Timestamp Counter (TSC)
 *============================================================================*/

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#endif /* PHANTOMOS_IO_H */
