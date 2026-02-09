/*
 * PhantomOS KVM Paravirtualized Clock
 * "To Create, Not To Destroy"
 *
 * Uses KVM's pvclock for nanosecond-precision timekeeping.
 * Reduces VM exits by avoiding PIT-based time queries.
 * Falls back gracefully on non-KVM hypervisors or bare metal.
 */

#ifndef PHANTOMOS_KVM_CLOCK_H
#define PHANTOMOS_KVM_CLOCK_H

#include <stdint.h>

/* MSR addresses */
#define MSR_KVM_SYSTEM_TIME_NEW     0x4b564d01
#define MSR_KVM_SYSTEM_TIME         0x4b564d00

/* CPUID leaf 0x40000001 feature bits */
#define KVM_FEATURE_CLOCKSOURCE     (1 << 0)
#define KVM_FEATURE_CLOCKSOURCE2    (1 << 3)

/* Initialize KVM paravirtualized clock (call after vm_detect_init) */
void kvm_clock_init(void);

/* Returns 1 if KVM pvclock is active */
int kvm_clock_available(void);

/* Read current time in nanoseconds since boot */
uint64_t kvm_clock_read_ns(void);

#endif /* PHANTOMOS_KVM_CLOCK_H */
