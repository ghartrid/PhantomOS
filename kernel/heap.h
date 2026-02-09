/*
 * PhantomOS Kernel Heap
 * "To Create, Not To Destroy"
 *
 * Simple first-fit free list allocator for kernel dynamic memory.
 */

#ifndef PHANTOMOS_HEAP_H
#define PHANTOMOS_HEAP_H

#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define HEAP_MIN_ALLOC          32                      /* Minimum allocation size */
#define HEAP_INITIAL_SIZE       (1024 * 1024)           /* 1MB initial heap */
#define HEAP_MAX_SIZE           (16 * 1024 * 1024)      /* 16MB maximum heap */
#define HEAP_EXPAND_SIZE        (256 * 1024)            /* 256KB expansion increment */

/* Magic numbers for debugging */
#define HEAP_MAGIC_USED         0xDEADBEEFDEADBEEFULL
#define HEAP_MAGIC_FREE         0xFREEFREEFREEFREEULL

/* Note: 0xFREEFREE... is not valid hex, using this instead */
#define HEAP_MAGIC_FREE_ACTUAL  0xF4EEF4EEF4EEF4EEULL

/*============================================================================
 * Heap Block Structure
 *============================================================================*/

/*
 * Block header structure
 * Each allocation has this header preceding the user data
 */
struct heap_block {
    uint64_t size;              /* Size of block including header */
                                /* Low bit: 0 = free, 1 = used */
    struct heap_block *next;    /* Next block in free list (only if free) */
    struct heap_block *prev;    /* Previous block in free list (only if free) */
    uint64_t magic;             /* Debug magic number */
};

#define HEAP_BLOCK_USED         1ULL
#define HEAP_BLOCK_SIZE(b)      ((b)->size & ~HEAP_BLOCK_USED)
#define HEAP_BLOCK_IS_USED(b)   ((b)->size & HEAP_BLOCK_USED)
#define HEAP_HEADER_SIZE        sizeof(struct heap_block)

/*============================================================================
 * Heap Statistics
 *============================================================================*/

struct heap_stats {
    uint64_t heap_start;            /* Heap start address */
    uint64_t heap_end;              /* Current heap end */
    uint64_t heap_max;              /* Maximum heap address */
    uint64_t total_size;            /* Total heap size */
    uint64_t used_size;             /* Currently allocated bytes */
    uint64_t free_size;             /* Currently free bytes */

    /* Historical (never decrease in PhantomOS spirit) */
    uint64_t total_allocations;     /* Number of kmalloc calls */
    uint64_t total_frees;           /* Number of kfree calls */
    uint64_t total_bytes_allocated; /* Total bytes ever allocated */
    uint64_t peak_usage;            /* High water mark */
};

/*============================================================================
 * Heap Functions
 *============================================================================*/

/*
 * Initialize the kernel heap
 * Allocates initial heap pages using PMM and VMM
 */
void heap_init(void);

/*
 * Allocate memory from kernel heap
 *
 * @size: Number of bytes to allocate
 * @return: Pointer to allocated memory, or NULL on failure
 */
void *kmalloc(size_t size);

/*
 * Allocate zeroed memory from kernel heap
 *
 * @nmemb: Number of elements
 * @size: Size of each element
 * @return: Pointer to zeroed memory, or NULL on failure
 */
void *kcalloc(size_t nmemb, size_t size);

/*
 * Reallocate memory
 *
 * @ptr: Pointer to existing allocation (or NULL)
 * @size: New size
 * @return: Pointer to new allocation, or NULL on failure
 */
void *krealloc(void *ptr, size_t size);

/*
 * Free memory back to kernel heap
 *
 * @ptr: Pointer to memory to free (may be NULL)
 */
void kfree(void *ptr);

/*
 * Get heap statistics
 *
 * @return: Pointer to statistics structure
 */
const struct heap_stats *heap_get_stats(void);

/*
 * Print heap statistics and state
 */
void heap_dump_stats(void);

/*
 * Check heap integrity (debug function)
 *
 * @return: 0 if heap is valid, -1 if corruption detected
 */
int heap_check(void);

#endif /* PHANTOMOS_HEAP_H */
