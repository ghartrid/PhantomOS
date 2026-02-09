/*
 * PhantomOS VM Detection
 * "To Create, Not To Destroy"
 *
 * Uses CPUID to detect if running inside a hypervisor and identify it.
 * CPUID leaf 1, ECX bit 31 = hypervisor present bit (set by all major VMs).
 * CPUID leaf 0x40000000 = hypervisor vendor signature (12-byte string).
 */

#include "vm_detect.h"

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern int memcmp(const void *s1, const void *s2, unsigned long n);

/*============================================================================
 * CPUID Helper
 *============================================================================*/

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                          uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0));
}

/*============================================================================
 * State
 *============================================================================*/

static enum vm_type detected_type = VM_TYPE_NONE;
static int detection_done = 0;

/*============================================================================
 * Detection
 *============================================================================*/

void vm_detect_init(void)
{
    uint32_t eax, ebx, ecx, edx;

    /* CPUID leaf 1: check hypervisor present bit (ECX bit 31) */
    cpuid(1, &eax, &ebx, &ecx, &edx);

    if (!(ecx & (1U << 31))) {
        detected_type = VM_TYPE_NONE;
        detection_done = 1;
        kprintf("[VM] Bare metal detected (no hypervisor)\n");
        return;
    }

    /* Hypervisor present - get vendor signature from leaf 0x40000000 */
    cpuid(0x40000000, &eax, &ebx, &ecx, &edx);

    /* EBX:ECX:EDX = 12-byte vendor string */
    char sig[13];
    *(uint32_t *)&sig[0] = ebx;
    *(uint32_t *)&sig[4] = ecx;
    *(uint32_t *)&sig[8] = edx;
    sig[12] = '\0';

    if (memcmp(sig, "KVMKVMKVM", 9) == 0) {
        detected_type = VM_TYPE_KVM;
    } else if (memcmp(sig, "VMwareVMware", 12) == 0) {
        detected_type = VM_TYPE_VMWARE;
    } else if (memcmp(sig, "Microsoft Hv", 12) == 0) {
        detected_type = VM_TYPE_HYPERV;
    } else if (memcmp(sig, "XenVMMXenVMM", 12) == 0) {
        detected_type = VM_TYPE_XEN;
    } else {
        detected_type = VM_TYPE_UNKNOWN_HV;
    }

    detection_done = 1;
    kprintf("[VM] Detected: %s (sig: %.12s)\n", vm_get_type_name(), sig);
}

int vm_is_virtualized(void)
{
    return detected_type != VM_TYPE_NONE;
}

enum vm_type vm_get_type(void)
{
    return detected_type;
}

const char *vm_get_type_name(void)
{
    switch (detected_type) {
    case VM_TYPE_NONE:       return "Bare Metal";
    case VM_TYPE_KVM:        return "KVM/QEMU";
    case VM_TYPE_VMWARE:     return "VMware";
    case VM_TYPE_HYPERV:     return "Hyper-V";
    case VM_TYPE_XEN:        return "Xen";
    case VM_TYPE_UNKNOWN_HV: return "Unknown Hypervisor";
    default:                 return "Unknown";
    }
}
