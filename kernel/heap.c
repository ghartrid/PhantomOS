/*
 * PhantomOS Kernel Heap
 * "To Create, Not To Destroy"
 *
 * First-fit free list allocator with block coalescing.
 */

#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void kpanic(const char *msg);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

/* Linker symbols */
extern char __kernel_end[];

/*============================================================================
 * Heap State
 *============================================================================*/

static struct heap_block *heap_free_list = NULL;
static struct heap_stats heap_stats;
static int heap_initialized = 0;

/* Heap boundaries */
static uint64_t heap_start = 0;
static uint64_t heap_end = 0;
static uint64_t heap_max = 0;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/* Align size up to minimum block size */
static inline size_t align_size(size_t size)
{
    size_t total = size + HEAP_HEADER_SIZE;
    if (total < HEAP_MIN_ALLOC) {
        total = HEAP_MIN_ALLOC;
    }
    /* Align to 16 bytes for better performance */
    return (total + 15) & ~15ULL;
}

/* Get next block in memory (not free list) */
static inline struct heap_block *next_block_in_memory(struct heap_block *block)
{
    return (struct heap_block *)((uint8_t *)block + HEAP_BLOCK_SIZE(block));
}

/* Check if block is within heap bounds */
static inline int is_valid_block(struct heap_block *block)
{
    uint64_t addr = (uint64_t)block;
    return addr >= heap_start && addr < heap_end;
}

/*============================================================================
 * Free List Management
 *============================================================================*/

/* Remove block from free list */
static void free_list_remove(struct heap_block *block)
{
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        heap_free_list = block->next;
    }

    if (block->next) {
        block->next->prev = block->prev;
    }

    block->next = NULL;
    block->prev = NULL;
}

/* Insert block into free list (sorted by address for coalescing) */
static void free_list_insert(struct heap_block *block)
{
    block->magic = HEAP_MAGIC_FREE_ACTUAL;

    if (!heap_free_list) {
        heap_free_list = block;
        block->next = NULL;
        block->prev = NULL;
        return;
    }

    /* Insert in address order */
    struct heap_block *curr = heap_free_list;
    struct heap_block *prev = NULL;

    while (curr && curr < block) {
        prev = curr;
        curr = curr->next;
    }

    block->next = curr;
    block->prev = prev;

    if (prev) {
        prev->next = block;
    } else {
        heap_free_list = block;
    }

    if (curr) {
        curr->prev = block;
    }
}

/* Coalesce adjacent free blocks */
static void coalesce(struct heap_block *block)
{
    /* Try to merge with next block */
    struct heap_block *next = block->next;
    if (next && (uint8_t *)block + HEAP_BLOCK_SIZE(block) == (uint8_t *)next) {
        /* Blocks are adjacent - merge */
        block->size = HEAP_BLOCK_SIZE(block) + HEAP_BLOCK_SIZE(next);
        block->next = next->next;
        if (next->next) {
            next->next->prev = block;
        }
    }

    /* Try to merge with previous block */
    struct heap_block *prev = block->prev;
    if (prev && (uint8_t *)prev + HEAP_BLOCK_SIZE(prev) == (uint8_t *)block) {
        /* Blocks are adjacent - merge */
        prev->size = HEAP_BLOCK_SIZE(prev) + HEAP_BLOCK_SIZE(block);
        prev->next = block->next;
        if (block->next) {
            block->next->prev = prev;
        }
    }
}

/*============================================================================
 * Heap Expansion
 *============================================================================*/

static int heap_expand(size_t min_size)
{
    /* Calculate pages needed */
    size_t expand_size = HEAP_EXPAND_SIZE;
    if (expand_size < min_size) {
        expand_size = min_size;
    }

    size_t pages = (expand_size + PAGE_SIZE - 1) / PAGE_SIZE;

    /* Check if we can expand */
    uint64_t new_end = heap_end + (pages * PAGE_SIZE);
    if (new_end > heap_max) {
        kprintf("Heap: cannot expand beyond max (0x%lx)\n",
                (unsigned long)heap_max);
        return -1;
    }

    /*
     * Allocate contiguous physical pages using identity mapping.
     * Like heap_init, we rely on the boot code's 2MB huge page identity
     * mapping (virt == phys) for the first 1GB. We just need to ensure
     * the PMM gives us pages at the right physical addresses.
     */
    void *phys = pmm_alloc_pages(pages);
    if (!phys) {
        kprintf("Heap: PMM allocation failed (%lu pages)\n",
                (unsigned long)pages);
        return -1;
    }

    /* Use identity mapping - the pages are already mapped by boot code */
    uint64_t alloc_addr = (uint64_t)phys;

    /* Check that the allocation is within the identity-mapped region */
    if (alloc_addr + (pages * PAGE_SIZE) > 0x40000000) {
        kprintf("Heap: expansion outside identity-mapped region\n");
        return -1;
    }

    /* Zero the new region */
    memset(phys, 0, pages * PAGE_SIZE);

    /* Create new free block at the allocated region */
    struct heap_block *new_block = (struct heap_block *)alloc_addr;
    new_block->size = pages * PAGE_SIZE;
    new_block->next = NULL;
    new_block->prev = NULL;

    /* Update heap end if this extends the heap */
    uint64_t alloc_end = alloc_addr + (pages * PAGE_SIZE);
    if (alloc_end > heap_end) {
        heap_end = alloc_end;
    }
    heap_stats.heap_end = heap_end;
    heap_stats.total_size += pages * PAGE_SIZE;
    heap_stats.free_size += pages * PAGE_SIZE;

    /* Add to free list and coalesce with neighbors if adjacent */
    free_list_insert(new_block);
    coalesce(new_block);

    return 0;
}

/*============================================================================
 * Heap Implementation
 *============================================================================*/

void heap_init(void)
{
    if (heap_initialized) {
        return;
    }

    /* Initialize statistics */
    memset(&heap_stats, 0, sizeof(heap_stats));

    /*
     * For the heap, we use IDENTITY MAPPING (virt == phys).
     * This works because our boot code identity-maps the first 1GB using 2MB pages.
     * We allocate contiguous physical pages and use them directly as virtual addresses.
     *
     * Find the first available contiguous region for the heap.
     */

    /* Allocate initial heap pages contiguously */
    size_t initial_pages = HEAP_INITIAL_SIZE / PAGE_SIZE;
    void *heap_phys = pmm_alloc_pages(initial_pages);
    if (!heap_phys) {
        kpanic("Heap: failed to allocate initial pages");
    }

    /* Use identity mapping - virtual address == physical address */
    heap_start = (uint64_t)heap_phys;
    heap_end = heap_start + HEAP_INITIAL_SIZE;
    heap_max = heap_start + HEAP_MAX_SIZE;

    /* Ensure heap is within identity-mapped region (first 1GB) */
    if (heap_end > 0x40000000) {
        kpanic("Heap: heap would exceed identity-mapped region");
    }

    heap_stats.heap_start = heap_start;
    heap_stats.heap_end = heap_end;
    heap_stats.heap_max = heap_max;
    heap_stats.total_size = HEAP_INITIAL_SIZE;
    heap_stats.free_size = HEAP_INITIAL_SIZE;

    /* Zero the heap region (already identity-mapped by boot code) */
    memset((void *)heap_start, 0, HEAP_INITIAL_SIZE);

    /* Create initial free block spanning entire heap */
    struct heap_block *initial_block = (struct heap_block *)heap_start;
    initial_block->size = HEAP_INITIAL_SIZE;
    initial_block->next = NULL;
    initial_block->prev = NULL;
    initial_block->magic = HEAP_MAGIC_FREE_ACTUAL;

    heap_free_list = initial_block;
    heap_initialized = 1;

    kprintf("  Heap: 0x%lx - 0x%lx (%lu KB initial)\n",
            (unsigned long)heap_start,
            (unsigned long)heap_end,
            (unsigned long)(HEAP_INITIAL_SIZE / 1024));
}

void *kmalloc(size_t size)
{
    if (!heap_initialized || size == 0) {
        return NULL;
    }

    size_t needed = align_size(size);

    /* Search free list (first-fit) */
    struct heap_block *block = heap_free_list;

    while (block) {
        uint64_t block_size = HEAP_BLOCK_SIZE(block);

        if (block_size >= needed) {
            /* Found suitable block */

            /* Split if large enough to leave a useful remainder */
            if (block_size >= needed + HEAP_MIN_ALLOC + HEAP_HEADER_SIZE) {
                struct heap_block *new_block =
                    (struct heap_block *)((uint8_t *)block + needed);
                new_block->size = block_size - needed;
                new_block->magic = HEAP_MAGIC_FREE_ACTUAL;

                /* Insert new block after current in free list */
                new_block->next = block->next;
                new_block->prev = block->prev;
                if (new_block->prev) {
                    new_block->prev->next = new_block;
                } else {
                    heap_free_list = new_block;
                }
                if (new_block->next) {
                    new_block->next->prev = new_block;
                }

                block->size = needed;
            } else {
                /* Use whole block - remove from free list */
                free_list_remove(block);
                needed = block_size;  /* Use actual block size */
            }

            /* Mark as used */
            block->size = needed | HEAP_BLOCK_USED;
            block->magic = HEAP_MAGIC_USED;
            block->next = NULL;
            block->prev = NULL;

            /* Update statistics */
            heap_stats.used_size += needed;
            heap_stats.free_size -= needed;
            heap_stats.total_allocations++;
            heap_stats.total_bytes_allocated += needed;

            if (heap_stats.used_size > heap_stats.peak_usage) {
                heap_stats.peak_usage = heap_stats.used_size;
            }

            /* Return pointer to user data (after header) */
            return (void *)((uint8_t *)block + HEAP_HEADER_SIZE);
        }

        block = block->next;
    }

    /* No suitable block - try to expand heap */
    if (heap_expand(needed) == 0) {
        return kmalloc(size);  /* Retry */
    }

    return NULL;  /* Out of memory */
}

void *kcalloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;

    /* Check for overflow */
    if (nmemb != 0 && total / nmemb != size) {
        return NULL;
    }

    void *ptr = kmalloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr) {
        return kmalloc(size);
    }

    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    /* Get current block */
    struct heap_block *block =
        (struct heap_block *)((uint8_t *)ptr - HEAP_HEADER_SIZE);

    uint64_t current_size = HEAP_BLOCK_SIZE(block) - HEAP_HEADER_SIZE;

    /* If new size fits in current block, just return */
    if (size <= current_size) {
        return ptr;
    }

    /* Allocate new block */
    void *new_ptr = kmalloc(size);
    if (!new_ptr) {
        return NULL;
    }

    /* Copy data */
    memcpy(new_ptr, ptr, current_size);

    /* Free old block */
    kfree(ptr);

    return new_ptr;
}

void kfree(void *ptr)
{
    if (!heap_initialized || !ptr) {
        return;
    }

    /* Get block header */
    struct heap_block *block =
        (struct heap_block *)((uint8_t *)ptr - HEAP_HEADER_SIZE);

    /* Validate block */
    if (!is_valid_block(block)) {
        kprintf("kfree: invalid pointer 0x%lx (out of heap bounds)\n",
                (unsigned long)ptr);
        return;
    }

    if (block->magic != HEAP_MAGIC_USED) {
        if (block->magic == HEAP_MAGIC_FREE_ACTUAL) {
            kprintf("kfree: double free detected at 0x%lx\n",
                    (unsigned long)ptr);
        } else {
            kprintf("kfree: heap corruption at 0x%lx (magic=0x%lx)\n",
                    (unsigned long)ptr, (unsigned long)block->magic);
        }
        return;
    }

    uint64_t block_size = HEAP_BLOCK_SIZE(block);

    /* Mark as free */
    block->size = block_size;  /* Clear used bit */
    block->magic = HEAP_MAGIC_FREE_ACTUAL;

    /* Update statistics */
    heap_stats.used_size -= block_size;
    heap_stats.free_size += block_size;
    heap_stats.total_frees++;

    /* Add to free list and coalesce */
    free_list_insert(block);
    coalesce(block);
}

const struct heap_stats *heap_get_stats(void)
{
    return &heap_stats;
}

void heap_dump_stats(void)
{
    kprintf("Heap Statistics:\n");
    kprintf("  Start:             0x%lx\n", (unsigned long)heap_stats.heap_start);
    kprintf("  End:               0x%lx\n", (unsigned long)heap_stats.heap_end);
    kprintf("  Max:               0x%lx\n", (unsigned long)heap_stats.heap_max);
    kprintf("  Total size:        %lu KB\n",
            (unsigned long)(heap_stats.total_size / 1024));
    kprintf("  Used:              %lu bytes\n",
            (unsigned long)heap_stats.used_size);
    kprintf("  Free:              %lu bytes\n",
            (unsigned long)heap_stats.free_size);
    kprintf("  Total allocations: %lu\n",
            (unsigned long)heap_stats.total_allocations);
    kprintf("  Total frees:       %lu\n",
            (unsigned long)heap_stats.total_frees);
    kprintf("  Peak usage:        %lu bytes\n",
            (unsigned long)heap_stats.peak_usage);
}

int heap_check(void)
{
    if (!heap_initialized) {
        return -1;
    }

    /* Walk free list and check for corruption */
    struct heap_block *block = heap_free_list;
    int count = 0;

    while (block) {
        if (!is_valid_block(block)) {
            kprintf("Heap check: block 0x%lx out of bounds\n",
                    (unsigned long)block);
            return -1;
        }

        if (block->magic != HEAP_MAGIC_FREE_ACTUAL) {
            kprintf("Heap check: block 0x%lx has wrong magic (0x%lx)\n",
                    (unsigned long)block, (unsigned long)block->magic);
            return -1;
        }

        if (HEAP_BLOCK_IS_USED(block)) {
            kprintf("Heap check: free list contains used block 0x%lx\n",
                    (unsigned long)block);
            return -1;
        }

        block = block->next;
        count++;

        /* Sanity check to prevent infinite loop */
        if (count > 100000) {
            kprintf("Heap check: possible cycle in free list\n");
            return -1;
        }
    }

    return 0;
}
