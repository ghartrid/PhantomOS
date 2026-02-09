/*
 * PhantomOS Physical Memory Manager
 * "To Create, Not To Destroy"
 *
 * Bitmap-based physical page allocator for the x86-64 kernel.
 * Tracks 4KB physical pages using a bitmap where:
 *   - 1 = page is used/allocated
 *   - 0 = page is free
 */

#ifndef PHANTOMOS_PMM_H
#define PHANTOMOS_PMM_H

#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define PAGE_SIZE           4096
#define PAGE_SHIFT          12
#define PAGES_PER_BYTE      8
#define PAGES_PER_QWORD     64

/* Align address up to page boundary */
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))

/* Convert between addresses and page numbers */
#define ADDR_TO_PAGE(addr)    ((addr) >> PAGE_SHIFT)
#define PAGE_TO_ADDR(page)    ((page) << PAGE_SHIFT)

/*============================================================================
 * Multiboot2 Structures (for memory map parsing)
 *============================================================================*/

/* Memory map entry types */
#define MULTIBOOT_MEMORY_AVAILABLE        1
#define MULTIBOOT_MEMORY_RESERVED         2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS              4
#define MULTIBOOT_MEMORY_BADRAM           5

/* Forward declarations from kmain.c */
struct multiboot_info;
struct multiboot_tag_mmap;

/*============================================================================
 * PMM Statistics (PhantomOS: append-only, never reset)
 *============================================================================*/

struct pmm_stats {
    uint64_t total_pages;           /* Total physical pages in system */
    uint64_t used_pages;            /* Currently allocated pages */
    uint64_t free_pages;            /* Currently free pages */
    uint64_t reserved_pages;        /* Reserved/unusable pages */

    /* Historical (never decrease) */
    uint64_t total_allocations;     /* Total pages ever allocated */
    uint64_t total_frees;           /* Total pages ever freed */
    uint64_t peak_usage;            /* High water mark */
};

/*============================================================================
 * PMM Functions
 *============================================================================*/

/*
 * Initialize the physical memory manager
 * Parses multiboot memory map and sets up the page bitmap
 *
 * @mb_info: Pointer to multiboot2 info structure
 */
void pmm_init(struct multiboot_info *mb_info);

/*
 * Allocate a single physical page
 *
 * @return: Physical address of allocated page, or 0 on failure
 */
void *pmm_alloc_page(void);

/*
 * Allocate multiple contiguous physical pages
 *
 * @count: Number of pages to allocate
 * @return: Physical address of first page, or 0 on failure
 */
void *pmm_alloc_pages(size_t count);

/*
 * Free a single physical page
 *
 * @addr: Physical address of page to free (must be page-aligned)
 */
void pmm_free_page(void *addr);

/*
 * Free multiple contiguous physical pages
 *
 * @addr: Physical address of first page
 * @count: Number of pages to free
 */
void pmm_free_pages(void *addr, size_t count);

/*
 * Mark a physical page as used (for reserved regions)
 *
 * @addr: Physical address of page to mark
 */
void pmm_mark_used(uint64_t addr);

/*
 * Mark a range of physical pages as used
 *
 * @start: Start physical address
 * @end: End physical address (exclusive)
 */
void pmm_mark_range_used(uint64_t start, uint64_t end);

/*
 * Get PMM statistics
 *
 * @return: Pointer to statistics structure
 */
const struct pmm_stats *pmm_get_stats(void);

/*
 * Get number of free pages
 *
 * @return: Number of currently free pages
 */
uint64_t pmm_get_free_pages(void);

/*
 * Get total number of pages
 *
 * @return: Total pages in system
 */
uint64_t pmm_get_total_pages(void);

/*
 * Print PMM statistics to console
 */
void pmm_dump_stats(void);

#endif /* PHANTOMOS_PMM_H */
