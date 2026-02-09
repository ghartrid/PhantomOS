/*
 * PhantomOS Physical Memory Manager
 * "To Create, Not To Destroy"
 *
 * Bitmap-based physical page allocator.
 */

#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void kpanic(const char *msg);
extern void *memset(void *s, int c, size_t n);

/* Linker symbols */
extern char __kernel_start[];
extern char __kernel_end[];

/*============================================================================
 * Multiboot2 Structures (copied from kmain.c for self-containment)
 *============================================================================*/

#define MULTIBOOT_TAG_END   0
#define MULTIBOOT_TAG_MMAP  6

struct multiboot_info {
    uint32_t total_size;
    uint32_t reserved;
};

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    /* Entries follow */
};

/*============================================================================
 * PMM State
 *============================================================================*/

/*
 * Bootstrap bitmap for first 1GB (covers 262144 pages = 32KB bitmap)
 * This is statically allocated so we don't need heap to initialize PMM
 */
#define PMM_BOOTSTRAP_PAGES     (1024 * 1024 * 1024 / PAGE_SIZE)  /* 1GB */
#define PMM_BOOTSTRAP_BITMAP_SIZE (PMM_BOOTSTRAP_PAGES / 8)       /* 32KB */

static uint64_t pmm_bitmap[PMM_BOOTSTRAP_BITMAP_SIZE / sizeof(uint64_t)];
static struct pmm_stats pmm_stats;
static uint64_t pmm_memory_end = 0;     /* Highest usable address */
static int pmm_initialized = 0;

/*============================================================================
 * Bitmap Helpers
 *============================================================================*/

static inline void bitmap_set(uint64_t page)
{
    if (page < PMM_BOOTSTRAP_PAGES) {
        pmm_bitmap[page / 64] |= (1ULL << (page % 64));
    }
}

static inline void bitmap_clear(uint64_t page)
{
    if (page < PMM_BOOTSTRAP_PAGES) {
        pmm_bitmap[page / 64] &= ~(1ULL << (page % 64));
    }
}

static inline int bitmap_test(uint64_t page)
{
    if (page >= PMM_BOOTSTRAP_PAGES) {
        return 1;  /* Treat out-of-range as used */
    }
    return (pmm_bitmap[page / 64] & (1ULL << (page % 64))) != 0;
}

/*============================================================================
 * Multiboot Parsing Helpers
 *============================================================================*/

static struct multiboot_tag *next_tag(struct multiboot_tag *tag)
{
    uintptr_t addr = (uintptr_t)tag + tag->size;
    addr = (addr + 7) & ~7;  /* 8-byte alignment */
    return (struct multiboot_tag *)addr;
}

static struct multiboot_tag_mmap *find_mmap_tag(struct multiboot_info *mb_info)
{
    struct multiboot_tag *tag = (struct multiboot_tag *)
        ((uint8_t *)mb_info + 8);

    while (tag->type != MULTIBOOT_TAG_END) {
        if (tag->type == MULTIBOOT_TAG_MMAP) {
            return (struct multiboot_tag_mmap *)tag;
        }
        tag = next_tag(tag);
    }
    return NULL;
}

/*============================================================================
 * PMM Implementation
 *============================================================================*/

void pmm_init(struct multiboot_info *mb_info)
{
    if (pmm_initialized) {
        return;
    }

    /* Initialize statistics */
    memset(&pmm_stats, 0, sizeof(pmm_stats));

    /* Mark all pages as used initially */
    memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap));

    /* Find memory map tag */
    struct multiboot_tag_mmap *mmap = find_mmap_tag(mb_info);
    if (!mmap) {
        kpanic("PMM: No memory map found in multiboot info");
    }

    /* First pass: find memory extent and mark available regions as free */
    struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)
        ((uint8_t *)mmap + 16);
    uint8_t *end = (uint8_t *)mmap + mmap->size;

    while ((uint8_t *)entry < end) {
        uint64_t region_start = entry->addr;
        uint64_t region_end = entry->addr + entry->len;

        /* Track highest memory address */
        if (region_end > pmm_memory_end) {
            pmm_memory_end = region_end;
        }

        /* Only process available memory */
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            /* Align to page boundaries */
            uint64_t start_page = PAGE_ALIGN_UP(region_start) >> PAGE_SHIFT;
            uint64_t end_page = PAGE_ALIGN_DOWN(region_end) >> PAGE_SHIFT;

            /* Mark pages as free (within our bitmap range) */
            for (uint64_t page = start_page; page < end_page && page < PMM_BOOTSTRAP_PAGES; page++) {
                bitmap_clear(page);
                pmm_stats.total_pages++;
            }
        } else {
            /* Count reserved pages */
            uint64_t pages = (entry->len + PAGE_SIZE - 1) / PAGE_SIZE;
            pmm_stats.reserved_pages += pages;
        }

        entry = (struct multiboot_mmap_entry *)
            ((uint8_t *)entry + mmap->entry_size);
    }

    /* Reserve first 1MB (real mode memory, BIOS, etc.) */
    pmm_mark_range_used(0, 0x100000);

    /* Reserve kernel memory */
    uint64_t kernel_start = (uint64_t)__kernel_start;
    uint64_t kernel_end = (uint64_t)__kernel_end;
    pmm_mark_range_used(kernel_start, kernel_end);

    /* Reserve page tables (0x106000 - 0x109000) */
    pmm_mark_range_used(0x106000, 0x109000);

    /* Reserve kernel stack area (0x109000 - 0x10d000) */
    pmm_mark_range_used(0x109000, 0x10d000);

    /* Reserve PMM bitmap itself (it's in BSS, but let's be safe) */
    uint64_t bitmap_start = (uint64_t)pmm_bitmap;
    uint64_t bitmap_end = bitmap_start + sizeof(pmm_bitmap);
    pmm_mark_range_used(bitmap_start, bitmap_end);

    /* Calculate free pages */
    pmm_stats.free_pages = 0;
    for (uint64_t i = 0; i < PMM_BOOTSTRAP_PAGES; i++) {
        if (!bitmap_test(i)) {
            pmm_stats.free_pages++;
        }
    }

    pmm_stats.used_pages = pmm_stats.total_pages - pmm_stats.free_pages;
    pmm_stats.peak_usage = pmm_stats.used_pages;

    pmm_initialized = 1;
}

void *pmm_alloc_page(void)
{
    if (!pmm_initialized) {
        return NULL;
    }

    /* Scan bitmap for first free page */
    for (uint64_t qword = 0; qword < sizeof(pmm_bitmap) / sizeof(uint64_t); qword++) {
        /* Quick check: if all bits set, skip this qword */
        if (pmm_bitmap[qword] == ~0ULL) {
            continue;
        }

        /* Find first clear bit in this qword */
        for (int bit = 0; bit < 64; bit++) {
            uint64_t page = qword * 64 + bit;
            if (page >= PMM_BOOTSTRAP_PAGES) {
                break;
            }

            if (!bitmap_test(page)) {
                /* Mark page as used */
                bitmap_set(page);
                pmm_stats.used_pages++;
                pmm_stats.free_pages--;
                pmm_stats.total_allocations++;

                if (pmm_stats.used_pages > pmm_stats.peak_usage) {
                    pmm_stats.peak_usage = pmm_stats.used_pages;
                }

                return (void *)PAGE_TO_ADDR(page);
            }
        }
    }

    return NULL;  /* Out of memory */
}

void *pmm_alloc_pages(size_t count)
{
    if (!pmm_initialized || count == 0) {
        return NULL;
    }

    /* Scan for contiguous free pages */
    uint64_t start_page = 0;
    size_t found = 0;

    for (uint64_t page = 0; page < PMM_BOOTSTRAP_PAGES; page++) {
        if (!bitmap_test(page)) {
            if (found == 0) {
                start_page = page;
            }
            found++;

            if (found == count) {
                /* Found enough contiguous pages - allocate them */
                for (size_t i = 0; i < count; i++) {
                    bitmap_set(start_page + i);
                }

                pmm_stats.used_pages += count;
                pmm_stats.free_pages -= count;
                pmm_stats.total_allocations += count;

                if (pmm_stats.used_pages > pmm_stats.peak_usage) {
                    pmm_stats.peak_usage = pmm_stats.used_pages;
                }

                return (void *)PAGE_TO_ADDR(start_page);
            }
        } else {
            found = 0;  /* Reset search */
        }
    }

    return NULL;  /* Not enough contiguous memory */
}

void pmm_free_page(void *addr)
{
    if (!pmm_initialized || !addr) {
        return;
    }

    uint64_t page = ADDR_TO_PAGE((uint64_t)addr);
    if (page >= PMM_BOOTSTRAP_PAGES) {
        return;
    }

    /* Check if page was actually allocated */
    if (!bitmap_test(page)) {
        kprintf("PMM: Warning: double free at 0x%lx\n", (unsigned long)addr);
        return;
    }

    bitmap_clear(page);
    pmm_stats.used_pages--;
    pmm_stats.free_pages++;
    pmm_stats.total_frees++;
}

void pmm_free_pages(void *addr, size_t count)
{
    if (!pmm_initialized || !addr || count == 0) {
        return;
    }

    uint64_t start_page = ADDR_TO_PAGE((uint64_t)addr);
    for (size_t i = 0; i < count; i++) {
        pmm_free_page((void *)PAGE_TO_ADDR(start_page + i));
    }
}

void pmm_mark_used(uint64_t addr)
{
    uint64_t page = ADDR_TO_PAGE(addr);
    if (page < PMM_BOOTSTRAP_PAGES && !bitmap_test(page)) {
        bitmap_set(page);
        if (pmm_stats.free_pages > 0) {
            pmm_stats.free_pages--;
        }
        pmm_stats.used_pages++;
    }
}

void pmm_mark_range_used(uint64_t start, uint64_t end)
{
    uint64_t start_page = start >> PAGE_SHIFT;
    uint64_t end_page = (end + PAGE_SIZE - 1) >> PAGE_SHIFT;

    for (uint64_t page = start_page; page < end_page && page < PMM_BOOTSTRAP_PAGES; page++) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            if (pmm_stats.free_pages > 0) {
                pmm_stats.free_pages--;
            }
            pmm_stats.used_pages++;
        }
    }
}

const struct pmm_stats *pmm_get_stats(void)
{
    return &pmm_stats;
}

uint64_t pmm_get_free_pages(void)
{
    return pmm_stats.free_pages;
}

uint64_t pmm_get_total_pages(void)
{
    return pmm_stats.total_pages;
}

void pmm_dump_stats(void)
{
    kprintf("PMM Statistics:\n");
    kprintf("  Total pages:       %lu (%lu MB)\n",
            (unsigned long)pmm_stats.total_pages,
            (unsigned long)(pmm_stats.total_pages * PAGE_SIZE / 1024 / 1024));
    kprintf("  Free pages:        %lu (%lu MB)\n",
            (unsigned long)pmm_stats.free_pages,
            (unsigned long)(pmm_stats.free_pages * PAGE_SIZE / 1024 / 1024));
    kprintf("  Used pages:        %lu\n", (unsigned long)pmm_stats.used_pages);
    kprintf("  Reserved pages:    %lu\n", (unsigned long)pmm_stats.reserved_pages);
    kprintf("  Peak usage:        %lu\n", (unsigned long)pmm_stats.peak_usage);
    kprintf("  Total allocations: %lu\n", (unsigned long)pmm_stats.total_allocations);
    kprintf("  Total frees:       %lu\n", (unsigned long)pmm_stats.total_frees);
    kprintf("  Memory end:        0x%lx\n", (unsigned long)pmm_memory_end);
}
