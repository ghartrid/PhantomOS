/*
 * PhantomOS KVM Paravirtualized Clock
 * "To Create, Not To Destroy"
 *
 * Reads time from KVM's pvclock shared memory structure.
 * The hypervisor updates the structure with TSC scaling parameters,
 * allowing nanosecond-precision time reads without VM exits.
 *
 * Protocol:
 *   1. Write physical address of pvclock struct to MSR 0x4b564d01
 *   2. KVM fills in TSC parameters (scale, shift, system_time)
 *   3. Guest reads TSC, applies formula to get nanoseconds
 *   4. Seqlock (version field) ensures consistent reads
 */

#include "kvm_clock.h"
#include "vm_detect.h"
#include "io.h"

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);

/*============================================================================
 * pvclock Structure (KVM shared memory)
 *============================================================================*/

struct pvclock_vcpu_time_info {
    uint32_t    version;            /* Seqlock: odd = update in progress */
    uint32_t    pad0;
    uint64_t    tsc_timestamp;      /* TSC value at last update */
    uint64_t    system_time;        /* Nanoseconds at last update */
    uint32_t    tsc_to_system_mul;  /* TSC -> ns multiplier */
    int8_t      tsc_shift;          /* TSC shift (can be negative) */
    uint8_t     flags;
    uint8_t     pad1[2];
} __attribute__((packed));

/*============================================================================
 * State
 *============================================================================*/

/* Page-aligned for MSR registration (must be within first 1GB identity map) */
static struct pvclock_vcpu_time_info pvclock_data
    __attribute__((aligned(4096)));

static int pvclock_active = 0;

/*============================================================================
 * 128-bit Multiply Helper
 *============================================================================*/

/*
 * Compute (a * b) >> 32 using x86_64 mulq instruction.
 * Returns the high 64 bits of the 128-bit product.
 */
static inline uint64_t mul64_hi(uint64_t a, uint32_t b)
{
    uint64_t result;
    __asm__("mulq %2" : "=d"(result) : "a"(a), "r"((uint64_t)b));
    return result;
}

/*============================================================================
 * CPUID Helper (same pattern as vm_detect.c)
 *============================================================================*/

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                          uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0));
}

/*============================================================================
 * Initialization
 *============================================================================*/

void kvm_clock_init(void)
{
    /* Only works on KVM */
    if (vm_get_type() != VM_TYPE_KVM) {
        kprintf("[KVM Clock] Not available (not KVM)\n");
        return;
    }

    /* Check CPUID leaf 0x40000001 for clocksource features */
    uint32_t eax, ebx, ecx, edx;
    cpuid(0x40000001, &eax, &ebx, &ecx, &edx);

    int has_cs2 = (eax & KVM_FEATURE_CLOCKSOURCE2);
    int has_cs1 = (eax & KVM_FEATURE_CLOCKSOURCE);

    if (!has_cs2 && !has_cs1) {
        kprintf("[KVM Clock] No clocksource feature in CPUID\n");
        return;
    }

    /* Physical address of pvclock struct (identity-mapped, virt == phys) */
    uint64_t phys_addr = (uint64_t)(uintptr_t)&pvclock_data;

    /* Write address to MSR with bit 0 set (enable) */
    uint32_t msr = has_cs2 ? MSR_KVM_SYSTEM_TIME_NEW : MSR_KVM_SYSTEM_TIME;
    wrmsr(msr, phys_addr | 1);

    /* Verify KVM populated the structure (version should be non-zero) */
    __asm__ volatile("" ::: "memory");  /* barrier */
    if (pvclock_data.tsc_to_system_mul == 0) {
        kprintf("[KVM Clock] Failed: KVM did not populate pvclock\n");
        return;
    }

    pvclock_active = 1;

    /* Read initial time to verify */
    uint64_t ns = kvm_clock_read_ns();
    uint64_t ms = ns / 1000000ULL;
    kprintf("[KVM Clock] Active: mul=%u shift=%d time=%ums\n",
            pvclock_data.tsc_to_system_mul,
            (int)pvclock_data.tsc_shift,
            (uint32_t)ms);
}

/*============================================================================
 * API
 *============================================================================*/

int kvm_clock_available(void)
{
    return pvclock_active;
}

uint64_t kvm_clock_read_ns(void)
{
    if (!pvclock_active)
        return 0;

    uint32_t version;
    uint64_t ns;

    do {
        version = pvclock_data.version;
        __asm__ volatile("" ::: "memory");  /* read barrier */

        uint64_t tsc = rdtsc();
        uint64_t delta = tsc - pvclock_data.tsc_timestamp;

        /* Apply shift: positive = left shift, negative = right shift */
        if (pvclock_data.tsc_shift >= 0)
            delta <<= pvclock_data.tsc_shift;
        else
            delta >>= -(pvclock_data.tsc_shift);

        /* Scale: (delta * tsc_to_system_mul) >> 32 gives nanoseconds */
        ns = pvclock_data.system_time +
             mul64_hi(delta, pvclock_data.tsc_to_system_mul);

        __asm__ volatile("" ::: "memory");  /* read barrier */
    } while ((pvclock_data.version & 1) || pvclock_data.version != version);

    return ns;
}
