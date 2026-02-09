/*
 * PhantomOS Virtual Memory Manager
 * "To Create, Not To Destroy"
 *
 * Manages x86-64 4-level page tables for the kernel.
 * Provides 4KB page granularity mapping capabilities.
 */

#ifndef PHANTOMOS_VMM_H
#define PHANTOMOS_VMM_H

#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * Page Table Entry Flags (x86-64)
 *============================================================================*/

#define PTE_PRESENT         (1ULL << 0)     /* Page is present in memory */
#define PTE_WRITABLE        (1ULL << 1)     /* Page is writable */
#define PTE_USER            (1ULL << 2)     /* Page accessible from user mode */
#define PTE_WRITETHROUGH    (1ULL << 3)     /* Write-through caching */
#define PTE_NOCACHE         (1ULL << 4)     /* Disable caching */
#define PTE_ACCESSED        (1ULL << 5)     /* Page has been accessed */
#define PTE_DIRTY           (1ULL << 6)     /* Page has been written */
#define PTE_HUGE            (1ULL << 7)     /* Huge page (2MB in PD, 1GB in PDPT) */
#define PTE_GLOBAL          (1ULL << 8)     /* Global page (not flushed on CR3 reload) */
#define PTE_NX              (1ULL << 63)    /* No-execute (requires EFER.NXE) */

/* Mask to extract physical address from PTE (bits 12-51) */
#define PTE_ADDR_MASK       0x000FFFFFFFFFF000ULL

/* Common flag combinations */
#define PTE_KERNEL_RW       (PTE_PRESENT | PTE_WRITABLE)
#define PTE_KERNEL_RO       (PTE_PRESENT)
#define PTE_KERNEL_RWX      (PTE_PRESENT | PTE_WRITABLE)
#define PTE_KERNEL_DATA     (PTE_PRESENT | PTE_WRITABLE | PTE_NX)

/*============================================================================
 * Page Table Index Macros
 *============================================================================*/

/* Each level handles 9 bits of the address (512 entries per table) */
#define PML4_INDEX(addr)    (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr)    (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)      (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)      (((addr) >> 12) & 0x1FF)

/* Page sizes */
#define PAGE_SIZE_4K        0x1000          /* 4 KB */
#define PAGE_SIZE_2M        0x200000        /* 2 MB */
#define PAGE_SIZE_1G        0x40000000      /* 1 GB */

/*============================================================================
 * VMM Functions
 *============================================================================*/

/*
 * Initialize the virtual memory manager
 * Sets up VMM state and reads current CR3
 */
void vmm_init(void);

/*
 * Map a virtual address to a physical address
 *
 * @virt: Virtual address to map (will be page-aligned)
 * @phys: Physical address to map to (must be page-aligned)
 * @flags: Page table entry flags (PTE_PRESENT, PTE_WRITABLE, etc.)
 * @return: 0 on success, -1 on failure
 */
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);

/*
 * Unmap a virtual address
 *
 * @virt: Virtual address to unmap
 * @return: 0 on success, -1 if not mapped
 */
int vmm_unmap_page(uint64_t virt);

/*
 * Get the physical address for a virtual address
 *
 * @virt: Virtual address to translate
 * @return: Physical address, or 0 if not mapped
 */
uint64_t vmm_get_physical(uint64_t virt);

/*
 * Check if a virtual address is mapped
 *
 * @virt: Virtual address to check
 * @return: 1 if mapped, 0 if not
 */
int vmm_is_mapped(uint64_t virt);

/*
 * Flush TLB entry for a specific address
 *
 * @addr: Virtual address to flush
 */
void vmm_flush_tlb(uint64_t addr);

/*
 * Flush entire TLB (reload CR3)
 */
void vmm_flush_tlb_all(void);

/*
 * Get current PML4 physical address (CR3)
 *
 * @return: Physical address of PML4 table
 */
uint64_t vmm_get_pml4(void);

/*
 * Print VMM statistics and state
 */
void vmm_dump_stats(void);

#endif /* PHANTOMOS_VMM_H */
