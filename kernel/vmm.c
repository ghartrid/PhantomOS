/*
 * PhantomOS Virtual Memory Manager
 * "To Create, Not To Destroy"
 *
 * 4-level page table management for x86-64.
 */

#include "vmm.h"
#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void kpanic(const char *msg);
extern void *memset(void *s, int c, size_t n);

/*============================================================================
 * VMM State
 *============================================================================*/

static uint64_t *vmm_pml4 = NULL;           /* Pointer to PML4 table */
static uint64_t vmm_pages_mapped = 0;       /* Total pages mapped */
static uint64_t vmm_tables_allocated = 0;   /* Page tables allocated */
static int vmm_initialized = 0;

/*============================================================================
 * Assembly Helpers
 *============================================================================*/

static inline uint64_t read_cr3(void)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void write_cr3(uint64_t cr3)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

void vmm_flush_tlb(uint64_t addr)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

void vmm_flush_tlb_all(void)
{
    uint64_t cr3 = read_cr3();
    write_cr3(cr3);
}

/*============================================================================
 * Page Table Helpers
 *============================================================================*/

/*
 * Get entry from a page table
 */
static inline uint64_t *get_entry(uint64_t *table, int index)
{
    return &table[index];
}

/*
 * Allocate a new page table (zeroed)
 */
static uint64_t *alloc_page_table(void)
{
    void *page = pmm_alloc_page();
    if (!page) {
        return NULL;
    }
    memset(page, 0, PAGE_SIZE_4K);
    vmm_tables_allocated++;
    return (uint64_t *)page;
}

/*
 * Get or create a page table at the given level
 * Returns pointer to the next-level table
 */
static uint64_t *get_or_create_table(uint64_t *table, int index, uint64_t flags)
{
    uint64_t *entry = get_entry(table, index);

    if (*entry & PTE_PRESENT) {
        /* Table exists, return pointer to it */
        return (uint64_t *)(*entry & PTE_ADDR_MASK);
    }

    /* Need to allocate new table */
    uint64_t *new_table = alloc_page_table();
    if (!new_table) {
        return NULL;
    }

    /* Set the entry to point to new table */
    *entry = ((uint64_t)new_table & PTE_ADDR_MASK) | flags | PTE_PRESENT;

    return new_table;
}

/*
 * Walk page tables to find the page table entry for a virtual address
 * Does not create tables - returns NULL if any level is not present
 */
static uint64_t *walk_page_tables(uint64_t virt)
{
    if (!vmm_pml4) {
        return NULL;
    }

    /* Get PML4 entry */
    uint64_t *pml4e = get_entry(vmm_pml4, PML4_INDEX(virt));
    if (!(*pml4e & PTE_PRESENT)) {
        return NULL;
    }

    /* Get PDPT */
    uint64_t *pdpt = (uint64_t *)(*pml4e & PTE_ADDR_MASK);
    uint64_t *pdpte = get_entry(pdpt, PDPT_INDEX(virt));
    if (!(*pdpte & PTE_PRESENT)) {
        return NULL;
    }

    /* Check for 1GB huge page */
    if (*pdpte & PTE_HUGE) {
        return pdpte;
    }

    /* Get PD */
    uint64_t *pd = (uint64_t *)(*pdpte & PTE_ADDR_MASK);
    uint64_t *pde = get_entry(pd, PD_INDEX(virt));
    if (!(*pde & PTE_PRESENT)) {
        return NULL;
    }

    /* Check for 2MB huge page */
    if (*pde & PTE_HUGE) {
        return pde;
    }

    /* Get PT */
    uint64_t *pt = (uint64_t *)(*pde & PTE_ADDR_MASK);
    uint64_t *pte = get_entry(pt, PT_INDEX(virt));

    return pte;
}

/*============================================================================
 * VMM Implementation
 *============================================================================*/

void vmm_init(void)
{
    if (vmm_initialized) {
        return;
    }

    /* Get current PML4 from CR3 */
    uint64_t cr3 = read_cr3();
    vmm_pml4 = (uint64_t *)(cr3 & PTE_ADDR_MASK);

    vmm_initialized = 1;

    kprintf("  VMM: PML4 at 0x%lx\n", (unsigned long)vmm_pml4);
}

int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    if (!vmm_initialized) {
        return -1;
    }

    /* Ensure addresses are page-aligned */
    virt &= ~0xFFFULL;
    phys &= ~0xFFFULL;

    /*
     * Special case: first 1GB is identity-mapped using 2MB pages by boot code.
     * If we're doing an identity mapping (virt == phys) within this range,
     * it's already mapped - just return success.
     */
    if (virt < 0x40000000 && virt == phys) {
        /* Already identity-mapped by boot 2MB pages */
        return 0;
    }

    /* Walk/create page tables */
    uint64_t *pdpt = get_or_create_table(vmm_pml4, PML4_INDEX(virt),
                                          PTE_PRESENT | PTE_WRITABLE);
    if (!pdpt) {
        return -1;
    }

    /* Check if this is a 1GB huge page */
    uint64_t *pdpte = get_entry(pdpt, PDPT_INDEX(virt));
    if ((*pdpte & PTE_PRESENT) && (*pdpte & PTE_HUGE)) {
        /* 1GB huge page - can't map 4KB within it easily */
        if (virt == phys) return 0;  /* Identity map already exists */
        kprintf("VMM: Cannot map within 1GB huge page at 0x%lx\n", (unsigned long)virt);
        return -1;
    }

    uint64_t *pd = get_or_create_table(pdpt, PDPT_INDEX(virt),
                                        PTE_PRESENT | PTE_WRITABLE);
    if (!pd) {
        return -1;
    }

    /* Check if this is a 2MB huge page */
    uint64_t *pde = get_entry(pd, PD_INDEX(virt));
    if ((*pde & PTE_PRESENT) && (*pde & PTE_HUGE)) {
        /* 2MB huge page - can't map 4KB within it easily */
        if (virt == phys) return 0;  /* Identity map already exists */
        kprintf("VMM: Cannot map within 2MB huge page at 0x%lx\n", (unsigned long)virt);
        return -1;
    }

    uint64_t *pt = get_or_create_table(pd, PD_INDEX(virt),
                                        PTE_PRESENT | PTE_WRITABLE);
    if (!pt) {
        return -1;
    }

    /* Set the page table entry */
    uint64_t *pte = get_entry(pt, PT_INDEX(virt));

    /* Check if already mapped */
    if (*pte & PTE_PRESENT) {
        /* Already mapped - update mapping */
        *pte = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
        vmm_flush_tlb(virt);
        return 0;
    }

    /* New mapping */
    *pte = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    vmm_pages_mapped++;
    vmm_flush_tlb(virt);

    return 0;
}

int vmm_unmap_page(uint64_t virt)
{
    if (!vmm_initialized) {
        return -1;
    }

    virt &= ~0xFFFULL;

    uint64_t *pte = walk_page_tables(virt);
    if (!pte || !(*pte & PTE_PRESENT)) {
        return -1;  /* Not mapped */
    }

    /* Check for huge page (can't unmap partial huge page) */
    if (*pte & PTE_HUGE) {
        kprintf("VMM: Warning: cannot unmap huge page at 0x%lx\n",
                (unsigned long)virt);
        return -1;
    }

    /* Clear the entry */
    *pte = 0;
    vmm_pages_mapped--;
    vmm_flush_tlb(virt);

    return 0;
}

uint64_t vmm_get_physical(uint64_t virt)
{
    if (!vmm_initialized) {
        return 0;
    }

    uint64_t *pte = walk_page_tables(virt);
    if (!pte || !(*pte & PTE_PRESENT)) {
        return 0;
    }

    uint64_t phys_base;
    uint64_t offset;

    if (*pte & PTE_HUGE) {
        /* Could be 2MB or 1GB page - check which level */
        /* For simplicity, assume 2MB (PD level) */
        phys_base = *pte & 0x000FFFFFFFE00000ULL;  /* 2MB aligned */
        offset = virt & 0x1FFFFF;  /* Offset within 2MB */
    } else {
        phys_base = *pte & PTE_ADDR_MASK;
        offset = virt & 0xFFF;  /* Offset within 4KB */
    }

    return phys_base | offset;
}

int vmm_is_mapped(uint64_t virt)
{
    uint64_t *pte = walk_page_tables(virt);
    return (pte && (*pte & PTE_PRESENT));
}

uint64_t vmm_get_pml4(void)
{
    return (uint64_t)vmm_pml4;
}

void vmm_dump_stats(void)
{
    kprintf("VMM Statistics:\n");
    kprintf("  PML4 address:      0x%lx\n", (unsigned long)vmm_pml4);
    kprintf("  Pages mapped:      %lu\n", (unsigned long)vmm_pages_mapped);
    kprintf("  Tables allocated:  %lu\n", (unsigned long)vmm_tables_allocated);
}
