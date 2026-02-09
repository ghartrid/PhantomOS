/*
 * PhantomOS VM Detection
 * "To Create, Not To Destroy"
 *
 * Detects hypervisor presence and type via CPUID.
 * Used to optimize rendering and I/O paths for virtual machines.
 */

#ifndef PHANTOMOS_VM_DETECT_H
#define PHANTOMOS_VM_DETECT_H

#include <stdint.h>

/* Hypervisor type */
enum vm_type {
    VM_TYPE_NONE = 0,       /* Bare metal */
    VM_TYPE_KVM,            /* KVM / QEMU */
    VM_TYPE_VMWARE,         /* VMware */
    VM_TYPE_HYPERV,         /* Microsoft Hyper-V */
    VM_TYPE_XEN,            /* Xen */
    VM_TYPE_UNKNOWN_HV,     /* Unknown hypervisor */
};

/* Detect hypervisor (call once during boot, before GPU HAL init) */
void vm_detect_init(void);

/* Returns 1 if running inside a virtual machine */
int vm_is_virtualized(void);

/* Get detected hypervisor type */
enum vm_type vm_get_type(void);

/* Get human-readable hypervisor name */
const char *vm_get_type_name(void);

#endif /* PHANTOMOS_VM_DETECT_H */
