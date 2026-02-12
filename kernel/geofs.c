/*
 * PhantomOS Kernel GeoFS
 * "To Create, Not To Destroy"
 *
 * Kernel-mode append-only, content-addressed filesystem.
 * RAM-disk backed using PMM pages.
 */

#include "geofs.h"
#include "pmm.h"
#include "heap.h"
#include "ata.h"
#include "lz4.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void kpanic(const char *msg);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);
extern size_t strlen(const char *s);
extern char *strcpy(char *dest, const char *src);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);
extern char *strncpy(char *dest, const char *src, size_t n);
extern char *strchr(const char *s, int c);
extern char *strstr(const char *haystack, const char *needle);

/* Timer for timestamps */
extern uint64_t timer_get_ticks(void);

/*============================================================================
 * SHA-256 Implementation (standalone, no dependencies)
 *============================================================================*/

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(uint32_t state[8], const uint8_t data[64])
{
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (; i < 64; i++) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + sha256_k[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256(const void *data, size_t len, uint8_t hash[32])
{
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    const uint8_t *msg = data;
    size_t remaining = len;
    uint8_t block[64];

    while (remaining >= 64) {
        sha256_transform(state, msg);
        msg += 64;
        remaining -= 64;
    }

    memset(block, 0, 64);
    memcpy(block, msg, remaining);
    block[remaining] = 0x80;

    if (remaining >= 56) {
        sha256_transform(state, block);
        memset(block, 0, 64);
    }

    uint64_t bits = len * 8;
    block[63] = bits & 0xff;
    block[62] = (bits >> 8) & 0xff;
    block[61] = (bits >> 16) & 0xff;
    block[60] = (bits >> 24) & 0xff;
    block[59] = (bits >> 32) & 0xff;
    block[58] = (bits >> 40) & 0xff;
    block[57] = (bits >> 48) & 0xff;
    block[56] = (bits >> 56) & 0xff;

    sha256_transform(state, block);

    for (int i = 0; i < 8; i++) {
        hash[i * 4] = (state[i] >> 24) & 0xff;
        hash[i * 4 + 1] = (state[i] >> 16) & 0xff;
        hash[i * 4 + 2] = (state[i] >> 8) & 0xff;
        hash[i * 4 + 3] = state[i] & 0xff;
    }
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

void kgeofs_hash_compute(const void *data, size_t len, kgeofs_hash_t hash_out)
{
    sha256(data, len, hash_out);
}

int kgeofs_hash_equal(const kgeofs_hash_t a, const kgeofs_hash_t b)
{
    return memcmp(a, b, KGEOFS_HASH_SIZE) == 0;
}

void kgeofs_hash_to_string(const kgeofs_hash_t hash, char *buf)
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < KGEOFS_HASH_SIZE; i++) {
        buf[i * 2] = hex[(hash[i] >> 4) & 0xf];
        buf[i * 2 + 1] = hex[hash[i] & 0xf];
    }
    buf[KGEOFS_HASH_SIZE * 2] = '\0';
}

const char *kgeofs_strerror(kgeofs_error_t err)
{
    switch (err) {
        case KGEOFS_OK:         return "Success";
        case KGEOFS_ERR_IO:     return "I/O error";
        case KGEOFS_ERR_NOMEM:  return "Out of memory";
        case KGEOFS_ERR_NOTFOUND: return "Not found";
        case KGEOFS_ERR_EXISTS: return "Already exists";
        case KGEOFS_ERR_INVALID: return "Invalid argument";
        case KGEOFS_ERR_FULL:   return "Volume full";
        case KGEOFS_ERR_CORRUPT: return "Data corruption";
        case KGEOFS_ERR_ISDIR:  return "Is a directory";
        case KGEOFS_ERR_NOTDIR: return "Not a directory";
        case KGEOFS_ERR_PERM:   return "Permission denied";
        case KGEOFS_ERR_QUOTA:  return "Quota exceeded";
        case KGEOFS_ERR_CONFLICT: return "Merge conflict";
        case KGEOFS_ERR_SYMLOOP: return "Symlink loop detected";
        default:                return "Unknown error";
    }
}

kgeofs_time_t kgeofs_time_now(void)
{
    return timer_get_ticks();
}

/*============================================================================
 * RAM Region Management
 *============================================================================*/

static struct kgeofs_ram_region *alloc_region(size_t pages)
{
    struct kgeofs_ram_region *region = kmalloc(sizeof(*region));
    if (!region) {
        return NULL;
    }

    region->base = pmm_alloc_pages(pages);
    if (!region->base) {
        kfree(region);
        return NULL;
    }

    region->size = pages * KGEOFS_BLOCK_SIZE;
    region->used = 0;
    region->next = NULL;

    memset(region->base, 0, region->size);
    return region;
}

static void free_region(struct kgeofs_ram_region *region)
{
    while (region) {
        struct kgeofs_ram_region *next = region->next;
        if (region->base) {
            pmm_free_pages(region->base, region->size / KGEOFS_BLOCK_SIZE);
        }
        kfree(region);
        region = next;
    }
}

static size_t region_total_size(struct kgeofs_ram_region *region)
{
    size_t total = 0;
    while (region) {
        total += region->size;
        region = region->next;
    }
    return total;
}

static size_t region_total_used(struct kgeofs_ram_region *region)
{
    size_t total = 0;
    while (region) {
        total += region->used;
        region = region->next;
    }
    return total;
}

/*
 * Auto-grow: extend a region chain by allocating more pages.
 * Appends a new chunk to the end of the linked list.
 * Returns pointer to the new region with available space, or NULL on failure.
 */
static struct kgeofs_ram_region *region_grow(struct kgeofs_ram_region *head,
                                              size_t needed)
{
    /* Calculate pages needed (at least 16 pages = 64KB, or enough for needed) */
    size_t pages = 16;
    size_t needed_pages = (needed + KGEOFS_BLOCK_SIZE - 1) / KGEOFS_BLOCK_SIZE;
    if (needed_pages > pages) pages = needed_pages;

    struct kgeofs_ram_region *chunk = alloc_region(pages);
    if (!chunk) return NULL;

    /* Append to end of list */
    struct kgeofs_ram_region *tail = head;
    while (tail->next) tail = tail->next;
    tail->next = chunk;

    return chunk;
}

/*
 * Find or grow: find a region with enough space, or auto-grow.
 */
static struct kgeofs_ram_region *region_find_or_grow(
    struct kgeofs_ram_region *head, size_t needed)
{
    struct kgeofs_ram_region *r = head;
    while (r) {
        if (r->used + needed <= r->size) return r;
        r = r->next;
    }
    /* No space found, grow */
    return region_grow(head, needed);
}

/*============================================================================
 * Volume Functions
 *============================================================================*/

kgeofs_error_t kgeofs_volume_create(size_t content_pages,
                                    size_t ref_pages,
                                    size_t view_pages,
                                    kgeofs_volume_t **vol_out)
{
    if (!vol_out) {
        return KGEOFS_ERR_INVALID;
    }

    /* Use defaults if not specified */
    if (content_pages == 0) content_pages = KGEOFS_DEFAULT_CONTENT_PAGES;
    if (ref_pages == 0) ref_pages = KGEOFS_DEFAULT_REF_PAGES;
    if (view_pages == 0) view_pages = KGEOFS_DEFAULT_VIEW_PAGES;

    /* Allocate volume structure */
    kgeofs_volume_t *vol = kmalloc(sizeof(*vol));
    if (!vol) {
        return KGEOFS_ERR_NOMEM;
    }
    memset(vol, 0, sizeof(*vol));

    vol->magic = KGEOFS_MAGIC;
    vol->version = KGEOFS_VERSION;
    vol->created = kgeofs_time_now();

    /* Allocate RAM regions */
    vol->content_region = alloc_region(content_pages);
    if (!vol->content_region) {
        kfree(vol);
        return KGEOFS_ERR_NOMEM;
    }

    vol->ref_region = alloc_region(ref_pages);
    if (!vol->ref_region) {
        free_region(vol->content_region);
        kfree(vol);
        return KGEOFS_ERR_NOMEM;
    }

    vol->view_region = alloc_region(view_pages);
    if (!vol->view_region) {
        free_region(vol->ref_region);
        free_region(vol->content_region);
        kfree(vol);
        return KGEOFS_ERR_NOMEM;
    }

    /* Initialize state */
    vol->current_view = 0;
    vol->next_view_id = 1;
    vol->current_branch = 0;
    vol->next_branch_id = 1;
    vol->ancestry_count = 0;

    /* Default access context: kernel (full access) */
    vol->current_ctx.uid = 0;
    vol->current_ctx.gid = 0;
    vol->current_ctx.caps = 0x80000000; /* GOV_CAP_KERNEL */

    /* Create "main" branch (id=0) before Genesis view */
    {
        size_t br_size = sizeof(struct kgeofs_branch_record);
        struct kgeofs_ram_region *br_region = region_find_or_grow(
            vol->view_region, br_size);
        if (!br_region) {
            free_region(vol->view_region);
            free_region(vol->ref_region);
            free_region(vol->content_region);
            kfree(vol);
            return KGEOFS_ERR_FULL;
        }
        struct kgeofs_branch_record *br =
            (struct kgeofs_branch_record *)((uint8_t *)br_region->base + br_region->used);
        br->magic = KGEOFS_BRANCH_MAGIC;
        br->flags = 0;
        br->id = 0;
        br->base_view = 0;
        br->head_view = 0;  /* Will be updated by view_create */
        br->created = kgeofs_time_now();
        strcpy(br->name, "main");
        br_region->used += br_size;

        struct kgeofs_branch_entry *be = kmalloc(sizeof(*be));
        if (be) {
            be->id = 0;
            be->base_view = 0;
            be->head_view = 0;
            be->created = br->created;
            strcpy(be->name, "main");
            be->next = NULL;
            vol->branch_index = be;
        }
        vol->total_branches = 1;
    }

    /* Create Genesis view (view 1) — will update branch head */
    kgeofs_view_t genesis;
    kgeofs_error_t err = kgeofs_view_create(vol, "Genesis", &genesis);
    if (err != KGEOFS_OK) {
        free_region(vol->view_region);
        free_region(vol->ref_region);
        free_region(vol->content_region);
        kfree(vol);
        return err;
    }

    *vol_out = vol;
    return KGEOFS_OK;
}

void kgeofs_volume_destroy(kgeofs_volume_t *vol)
{
    if (!vol) return;

    /* Free index entries */
    for (int i = 0; i < KGEOFS_HASH_BUCKETS; i++) {
        struct kgeofs_content_entry *ce = vol->content_hash[i];
        while (ce) {
            struct kgeofs_content_entry *next = ce->next;
            kfree(ce);
            ce = next;
        }
    }

    struct kgeofs_ref_entry *re = vol->ref_index;
    while (re) {
        struct kgeofs_ref_entry *next = re->next;
        kfree(re);
        re = next;
    }

    struct kgeofs_view_entry *ve = vol->view_index;
    while (ve) {
        struct kgeofs_view_entry *next = ve->next;
        kfree(ve);
        ve = next;
    }

    /* Free regions */
    free_region(vol->content_region);
    free_region(vol->ref_region);
    free_region(vol->view_region);

    kfree(vol);
}

void kgeofs_volume_stats(kgeofs_volume_t *vol, struct kgeofs_stats *stats)
{
    if (!vol || !stats) return;

    memset(stats, 0, sizeof(*stats));

    stats->content_bytes = vol->total_content_bytes;
    stats->content_region_size = region_total_size(vol->content_region);
    stats->content_region_used = region_total_used(vol->content_region);

    stats->ref_count = vol->total_refs;
    stats->ref_region_size = region_total_size(vol->ref_region);
    stats->ref_region_used = region_total_used(vol->ref_region);

    stats->view_count = vol->total_views;
    stats->view_region_size = region_total_size(vol->view_region);
    stats->view_region_used = region_total_used(vol->view_region);

    stats->dedup_hits = vol->dedup_hits;
    stats->current_view = vol->current_view;
    stats->compressed_bytes = vol->compressed_bytes;
    stats->compressed_count = vol->compressed_count;
}

/*============================================================================
 * Content Functions
 *============================================================================*/

/* Find content entry by hash */
static struct kgeofs_content_entry *content_find(kgeofs_volume_t *vol,
                                                  const kgeofs_hash_t hash)
{
    uint8_t bucket = KGEOFS_HASH_BUCKET(hash);
    struct kgeofs_content_entry *entry = vol->content_hash[bucket];

    while (entry) {
        if (kgeofs_hash_equal(entry->hash, hash)) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

kgeofs_error_t kgeofs_content_store(kgeofs_volume_t *vol,
                                    const void *data,
                                    size_t size,
                                    kgeofs_hash_t hash_out)
{
    if (!vol || !data || !hash_out) {
        return KGEOFS_ERR_INVALID;
    }

    /* Compute hash */
    kgeofs_hash_t hash;
    kgeofs_hash_compute(data, size, hash);

    /* Check for duplicate (deduplication) */
    struct kgeofs_content_entry *existing = content_find(vol, hash);
    if (existing) {
        memcpy(hash_out, hash, KGEOFS_HASH_SIZE);
        vol->dedup_hits++;
        return KGEOFS_OK;
    }

    /* Try LZ4 compression for blocks >= 64 bytes */
    size_t header_size = sizeof(struct kgeofs_content_header);
    uint8_t *compressed_buf = NULL;
    size_t compressed_len = 0;
    int use_compression = 0;

    if (size >= 64) {
        compressed_buf = kmalloc(size);
        if (compressed_buf) {
            if (lz4_compress((const uint8_t *)data, size,
                             compressed_buf, size, &compressed_len) == 0 &&
                compressed_len < (size * 9) / 10) {
                use_compression = 1;
            }
        }
    }

    size_t store_size = use_compression ? compressed_len : size;
    size_t total_size = header_size + store_size;

    /* Find space or auto-grow */
    struct kgeofs_ram_region *region = region_find_or_grow(
        vol->content_region, total_size);
    if (!region) {
        if (compressed_buf) kfree(compressed_buf);
        return KGEOFS_ERR_FULL;
    }

    /* Write header */
    struct kgeofs_content_header *hdr =
        (struct kgeofs_content_header *)((uint8_t *)region->base + region->used);
    hdr->magic = KGEOFS_CONTENT_MAGIC;
    hdr->flags = use_compression ? KGEOFS_CONTENT_FLAG_COMPRESSED : 0;
    hdr->size = store_size;
    memcpy(hdr->hash, hash, KGEOFS_HASH_SIZE);
    memset(hdr->reserved, 0, sizeof(hdr->reserved));

    /* Store original size in reserved[0..7] when compressed */
    if (use_compression) {
        uint64_t orig_size = (uint64_t)size;
        memcpy(hdr->reserved, &orig_size, sizeof(orig_size));
    }

    /* Write data */
    memcpy((uint8_t *)hdr + header_size,
           use_compression ? compressed_buf : data, store_size);

    if (compressed_buf) kfree(compressed_buf);

    /* Add to index (size = decompressed size for correct reporting) */
    struct kgeofs_content_entry *entry = kmalloc(sizeof(*entry));
    if (entry) {
        memcpy(entry->hash, hash, KGEOFS_HASH_SIZE);
        entry->offset = region->used;
        entry->size = size;  /* Always report decompressed size */

        uint8_t bucket = KGEOFS_HASH_BUCKET(hash);
        entry->next = vol->content_hash[bucket];
        vol->content_hash[bucket] = entry;
    }

    region->used += total_size;
    vol->total_content_bytes += size;

    if (use_compression) {
        vol->compressed_bytes += (size - compressed_len);
        vol->compressed_count++;
    }

    memcpy(hash_out, hash, KGEOFS_HASH_SIZE);
    return KGEOFS_OK;
}

kgeofs_error_t kgeofs_content_read(kgeofs_volume_t *vol,
                                   const kgeofs_hash_t hash,
                                   void *buf,
                                   size_t buf_size,
                                   size_t *size_out)
{
    if (!vol || !hash || !buf) {
        return KGEOFS_ERR_INVALID;
    }

    vol->total_lookups++;

    struct kgeofs_content_entry *entry = content_find(vol, hash);
    if (!entry) {
        return KGEOFS_ERR_NOTFOUND;
    }

    /* Find the region containing this content */
    struct kgeofs_ram_region *region = vol->content_region;
    uint64_t offset = entry->offset;

    while (region && offset >= region->size) {
        offset -= region->size;
        region = region->next;
    }

    if (!region) {
        return KGEOFS_ERR_CORRUPT;
    }

    /* Read data (skip header) — decompress if needed */
    struct kgeofs_content_header *hdr =
        (struct kgeofs_content_header *)((uint8_t *)region->base + offset);

    if (hdr->flags & KGEOFS_CONTENT_FLAG_COMPRESSED) {
        /* Compressed: read original size from reserved[0..7] */
        uint64_t original_size;
        memcpy(&original_size, hdr->reserved, sizeof(original_size));

        size_t compressed_size = (size_t)hdr->size;
        const uint8_t *compressed_data = (uint8_t *)hdr + sizeof(*hdr);

        uint8_t *decomp_buf = kmalloc((size_t)original_size);
        if (!decomp_buf) return KGEOFS_ERR_NOMEM;

        size_t decompressed_len;
        if (lz4_decompress(compressed_data, compressed_size,
                            decomp_buf, (size_t)original_size,
                            &decompressed_len) != 0) {
            kfree(decomp_buf);
            return KGEOFS_ERR_CORRUPT;
        }

        size_t to_read = decompressed_len;
        if (to_read > buf_size) to_read = buf_size;
        memcpy(buf, decomp_buf, to_read);
        kfree(decomp_buf);

        if (size_out) *size_out = decompressed_len;
    } else {
        /* Uncompressed: direct copy */
        size_t to_read = entry->size;
        if (to_read > buf_size) to_read = buf_size;
        memcpy(buf, (uint8_t *)hdr + sizeof(*hdr), to_read);

        if (size_out) *size_out = entry->size;
    }

    return KGEOFS_OK;
}

kgeofs_error_t kgeofs_content_size(kgeofs_volume_t *vol,
                                   const kgeofs_hash_t hash,
                                   uint64_t *size_out)
{
    if (!vol || !hash || !size_out) {
        return KGEOFS_ERR_INVALID;
    }

    struct kgeofs_content_entry *entry = content_find(vol, hash);
    if (!entry) {
        return KGEOFS_ERR_NOTFOUND;
    }

    *size_out = entry->size;
    return KGEOFS_OK;
}

/*============================================================================
 * Reference Functions
 *============================================================================*/

/* Hash a path string */
static void hash_path(const char *path, kgeofs_hash_t hash_out)
{
    kgeofs_hash_compute(path, strlen(path), hash_out);
}

/*============================================================================
 * Ancestry Cache (branch-aware visibility)
 *============================================================================*/

/* Rebuild ancestry cache by walking parent_id chain from current_view */
static void rebuild_ancestry_cache(kgeofs_volume_t *vol)
{
    vol->ancestry_count = 0;
    kgeofs_view_t walk = vol->current_view;

    while (walk != 0 && vol->ancestry_count < KGEOFS_MAX_ANCESTRY) {
        vol->ancestry_cache[vol->ancestry_count++] = walk;

        /* Find parent of this view */
        struct kgeofs_view_entry *ve = vol->view_index;
        kgeofs_view_t parent = 0;
        while (ve) {
            if (ve->id == walk) {
                parent = ve->parent_id;
                break;
            }
            ve = ve->next;
        }
        walk = parent;
    }
}

/* Check if a view_id is in the current ancestry chain */
static int view_in_ancestry(kgeofs_volume_t *vol, kgeofs_view_t view_id)
{
    for (int i = 0; i < vol->ancestry_count; i++) {
        if (vol->ancestry_cache[i] == view_id)
            return 1;
    }
    return 0;
}

/* Insert ref entry into hash table for O(1) path lookups */
static void ref_hash_insert(kgeofs_volume_t *vol, struct kgeofs_ref_entry *entry)
{
    uint8_t bucket = KGEOFS_HASH_BUCKET(entry->path_hash);
    entry->hash_next = vol->ref_hash[bucket];
    vol->ref_hash[bucket] = entry;
}

/* Find best matching ref for path in current view (branch-aware) */
static struct kgeofs_ref_entry *ref_find_best(kgeofs_volume_t *vol,
                                               const char *path)
{
    kgeofs_hash_t path_hash;
    hash_path(path, path_hash);

    struct kgeofs_ref_entry *best = NULL;
    kgeofs_time_t best_time = 0;

    /* Use ref_hash for O(1) bucket lookup instead of scanning ref_index */
    uint8_t bucket = KGEOFS_HASH_BUCKET(path_hash);
    struct kgeofs_ref_entry *entry = vol->ref_hash[bucket];
    while (entry) {
        if (kgeofs_hash_equal(entry->path_hash, path_hash)) {
            if (view_in_ancestry(vol, entry->view_id)) {
                if (entry->created > best_time) {
                    best = entry;
                    best_time = entry->created;
                }
            }
        }
        entry = entry->hash_next;
    }

    return best;
}

kgeofs_error_t kgeofs_ref_create(kgeofs_volume_t *vol,
                                 const char *path,
                                 const kgeofs_hash_t content_hash)
{
    if (!vol || !path || !content_hash) {
        return KGEOFS_ERR_INVALID;
    }

    size_t path_len = strlen(path);
    if (path_len >= KGEOFS_MAX_PATH) {
        return KGEOFS_ERR_INVALID;
    }

    /* Find space or auto-grow ref region */
    size_t record_size = sizeof(struct kgeofs_ref_record);
    struct kgeofs_ram_region *region = region_find_or_grow(
        vol->ref_region, record_size);
    if (!region) {
        return KGEOFS_ERR_FULL;
    }

    /* Write record to region */
    struct kgeofs_ref_record *record =
        (struct kgeofs_ref_record *)((uint8_t *)region->base + region->used);

    record->magic = KGEOFS_REF_MAGIC;
    record->flags = 0;
    hash_path(path, record->path_hash);
    memcpy(record->content_hash, content_hash, KGEOFS_HASH_SIZE);
    record->view_id = vol->current_view;
    record->created = kgeofs_time_now();
    record->path_len = path_len;
    record->file_type = KGEOFS_TYPE_FILE;
    record->permissions = KGEOFS_PERM_DEFAULT;
    record->owner_id = vol->current_ctx.uid;
    record->reserved_pad = 0;
    strcpy(record->path, path);

    /* Add to index */
    struct kgeofs_ref_entry *entry = kmalloc(sizeof(*entry));
    if (entry) {
        memcpy(entry->path_hash, record->path_hash, KGEOFS_HASH_SIZE);
        memcpy(entry->content_hash, content_hash, KGEOFS_HASH_SIZE);
        entry->view_id = record->view_id;
        entry->created = record->created;
        strcpy(entry->path, path);
        entry->is_hidden = 0;
        entry->file_type = KGEOFS_TYPE_FILE;
        entry->permissions = KGEOFS_PERM_DEFAULT;
        entry->owner_id = 0;
        entry->hash_next = NULL;

        entry->next = vol->ref_index;
        vol->ref_index = entry;
        ref_hash_insert(vol, entry);
    }

    region->used += record_size;
    vol->total_refs++;

    return KGEOFS_OK;
}

kgeofs_error_t kgeofs_ref_resolve(kgeofs_volume_t *vol,
                                  const char *path,
                                  kgeofs_hash_t hash_out)
{
    if (!vol || !path || !hash_out) {
        return KGEOFS_ERR_INVALID;
    }

    const char *current_path = path;
    char resolved_buf[KGEOFS_MAX_PATH];
    int hops = 0;

    while (hops < KGEOFS_SYMLINK_MAX_HOPS) {
        struct kgeofs_ref_entry *entry = ref_find_best(vol, current_path);
        if (!entry || entry->is_hidden) {
            return KGEOFS_ERR_NOTFOUND;
        }

        if (entry->file_type != KGEOFS_TYPE_LINK) {
            memcpy(hash_out, entry->content_hash, KGEOFS_HASH_SIZE);
            return KGEOFS_OK;
        }

        /* Symlink: read target path from content */
        size_t got;
        kgeofs_error_t err = kgeofs_content_read(vol, entry->content_hash,
                                                   resolved_buf,
                                                   KGEOFS_MAX_PATH - 1, &got);
        if (err != KGEOFS_OK) return err;
        resolved_buf[got] = '\0';

        current_path = resolved_buf;
        hops++;
    }

    return KGEOFS_ERR_SYMLOOP;
}

int kgeofs_ref_list(kgeofs_volume_t *vol,
                    const char *dir_path,
                    kgeofs_dir_callback_t callback,
                    void *ctx)
{
    if (!vol || !dir_path || !callback) {
        return 0;
    }

    size_t dir_len = strlen(dir_path);
    int count = 0;

    /* Track which names we've already reported (dedup within current view) */
    /* Simple approach: just iterate and let callback filter duplicates */

    struct kgeofs_ref_entry *entry = vol->ref_index;
    while (entry) {
        /* Check if visible in current view */
        if (view_in_ancestry(vol, entry->view_id) && !entry->is_hidden) {
            /* Check if path is in this directory */
            if (strncmp(entry->path, dir_path, dir_len) == 0) {
                const char *rest = entry->path + dir_len;

                /* Skip leading slash if present */
                if (*rest == '/') rest++;

                /* Check if it's a direct child (no more slashes) */
                const char *slash = rest;
                while (*slash && *slash != '/') slash++;

                if (*rest && (*slash == '\0' || *(slash + 1) == '\0')) {
                    struct kgeofs_dirent dirent;
                    memset(&dirent, 0, sizeof(dirent));

                    /* Extract name */
                    size_t name_len = slash - rest;
                    if (name_len >= KGEOFS_MAX_NAME) {
                        name_len = KGEOFS_MAX_NAME - 1;
                    }
                    memcpy(dirent.name, rest, name_len);
                    dirent.name[name_len] = '\0';

                    memcpy(dirent.content_hash, entry->content_hash, KGEOFS_HASH_SIZE);
                    dirent.created = entry->created;
                    dirent.permissions = entry->permissions;
                    dirent.owner_id = entry->owner_id;
                    dirent.file_type = entry->file_type;

                    /* Check if directory */
                    uint64_t size;
                    if (kgeofs_content_size(vol, entry->content_hash, &size) == KGEOFS_OK) {
                        dirent.size = size;
                        /* Check for directory marker */
                        if (size == KGEOFS_DIR_MARKER_LEN) {
                            char marker[KGEOFS_DIR_MARKER_LEN + 1];
                            size_t got;
                            if (kgeofs_content_read(vol, entry->content_hash,
                                                    marker, sizeof(marker), &got) == KGEOFS_OK) {
                                marker[got] = '\0';
                                if (strcmp(marker, KGEOFS_DIR_MARKER) == 0) {
                                    dirent.is_directory = 1;
                                }
                            }
                        }
                    }

                    if (callback(&dirent, ctx) != 0) {
                        break;  /* Callback requested stop */
                    }
                    count++;
                }
            }
        }
        entry = entry->next;
    }

    return count;
}

/*============================================================================
 * View Functions
 *============================================================================*/

kgeofs_error_t kgeofs_view_create(kgeofs_volume_t *vol,
                                  const char *label,
                                  kgeofs_view_t *view_out)
{
    if (!vol || !label || !view_out) {
        return KGEOFS_ERR_INVALID;
    }

    /* Use V2 view records (with branch_id) */
    size_t record_size = sizeof(struct kgeofs_view2_record);
    struct kgeofs_ram_region *region = region_find_or_grow(
        vol->view_region, record_size);

    if (!region) {
        return KGEOFS_ERR_FULL;
    }

    /* Create V2 view record */
    struct kgeofs_view2_record *record =
        (struct kgeofs_view2_record *)((uint8_t *)region->base + region->used);

    record->magic = KGEOFS_VIEW2_MAGIC;
    record->flags = 0;
    record->id = vol->next_view_id++;
    record->parent_id = vol->current_view;
    record->branch_id = vol->current_branch;
    record->created = kgeofs_time_now();

    size_t label_len = strlen(label);
    if (label_len >= sizeof(record->label)) {
        label_len = sizeof(record->label) - 1;
    }
    memcpy(record->label, label, label_len);
    record->label[label_len] = '\0';

    /* Add to index */
    struct kgeofs_view_entry *entry = kmalloc(sizeof(*entry));
    if (entry) {
        entry->id = record->id;
        entry->parent_id = record->parent_id;
        entry->branch_id = record->branch_id;
        entry->created = record->created;
        strcpy(entry->label, record->label);

        entry->next = vol->view_index;
        vol->view_index = entry;
    }

    region->used += record_size;
    vol->total_views++;

    /* Switch to new view */
    vol->current_view = record->id;

    /* Update branch head */
    struct kgeofs_branch_entry *be = vol->branch_index;
    while (be) {
        if (be->id == vol->current_branch) {
            be->head_view = record->id;
            /* Append updated branch record (append-only) */
            size_t br_size = sizeof(struct kgeofs_branch_record);
            struct kgeofs_ram_region *br_region = region_find_or_grow(
                vol->view_region, br_size);
            if (br_region) {
                struct kgeofs_branch_record *br =
                    (struct kgeofs_branch_record *)((uint8_t *)br_region->base + br_region->used);
                br->magic = KGEOFS_BRANCH_MAGIC;
                br->flags = 0;
                br->id = be->id;
                br->base_view = be->base_view;
                br->head_view = record->id;
                br->created = be->created;
                strncpy(br->name, be->name, KGEOFS_BRANCH_NAME_MAX - 1);
                br->name[KGEOFS_BRANCH_NAME_MAX - 1] = '\0';
                br_region->used += br_size;
            }
            break;
        }
        be = be->next;
    }

    /* Rebuild ancestry cache */
    rebuild_ancestry_cache(vol);

    *view_out = record->id;
    return KGEOFS_OK;
}

kgeofs_error_t kgeofs_view_switch(kgeofs_volume_t *vol, kgeofs_view_t view_id)
{
    if (!vol) {
        return KGEOFS_ERR_INVALID;
    }

    /* Verify view exists */
    struct kgeofs_view_entry *entry = vol->view_index;
    while (entry) {
        if (entry->id == view_id) {
            vol->current_view = view_id;
            vol->current_branch = entry->branch_id;
            rebuild_ancestry_cache(vol);
            return KGEOFS_OK;
        }
        entry = entry->next;
    }

    return KGEOFS_ERR_NOTFOUND;
}

kgeofs_view_t kgeofs_view_current(kgeofs_volume_t *vol)
{
    return vol ? vol->current_view : 0;
}

/*============================================================================
 * Branch Management (Tectonic Divergence)
 *============================================================================*/

kgeofs_error_t kgeofs_branch_create(kgeofs_volume_t *vol,
                                     const char *name,
                                     kgeofs_branch_t *branch_out)
{
    if (!vol || !name || !branch_out) return KGEOFS_ERR_INVALID;

    /* Check for duplicate name */
    struct kgeofs_branch_entry *be = vol->branch_index;
    while (be) {
        if (strcmp(be->name, name) == 0) return KGEOFS_ERR_EXISTS;
        be = be->next;
    }

    kgeofs_branch_t new_id = vol->next_branch_id++;

    /* Write branch record to view_region */
    size_t br_size = sizeof(struct kgeofs_branch_record);
    struct kgeofs_ram_region *region = region_find_or_grow(vol->view_region, br_size);
    if (!region) return KGEOFS_ERR_FULL;

    struct kgeofs_branch_record *br =
        (struct kgeofs_branch_record *)((uint8_t *)region->base + region->used);
    br->magic = KGEOFS_BRANCH_MAGIC;
    br->flags = 0;
    br->id = new_id;
    br->base_view = vol->current_view;
    br->head_view = vol->current_view;
    br->created = kgeofs_time_now();
    strncpy(br->name, name, KGEOFS_BRANCH_NAME_MAX - 1);
    br->name[KGEOFS_BRANCH_NAME_MAX - 1] = '\0';
    region->used += br_size;

    /* Add to in-memory index */
    struct kgeofs_branch_entry *entry = kmalloc(sizeof(*entry));
    if (!entry) return KGEOFS_ERR_NOMEM;
    entry->id = new_id;
    entry->base_view = vol->current_view;
    entry->head_view = vol->current_view;
    entry->created = br->created;
    strcpy(entry->name, br->name);
    entry->next = vol->branch_index;
    vol->branch_index = entry;

    vol->total_branches++;

    /* Switch to the new branch */
    vol->current_branch = new_id;
    /* current_view stays the same (fork point) */
    rebuild_ancestry_cache(vol);

    *branch_out = new_id;
    kprintf("[GeoFS] Branch '%s' (id=%lu) created from view %lu\n",
            name, (unsigned long)new_id, (unsigned long)vol->current_view);
    return KGEOFS_OK;
}

kgeofs_error_t kgeofs_branch_switch(kgeofs_volume_t *vol,
                                     kgeofs_branch_t branch_id)
{
    if (!vol) return KGEOFS_ERR_INVALID;

    struct kgeofs_branch_entry *be = vol->branch_index;
    while (be) {
        if (be->id == branch_id) {
            vol->current_branch = branch_id;
            vol->current_view = be->head_view;
            rebuild_ancestry_cache(vol);
            return KGEOFS_OK;
        }
        be = be->next;
    }
    return KGEOFS_ERR_NOTFOUND;
}

kgeofs_error_t kgeofs_branch_switch_name(kgeofs_volume_t *vol,
                                          const char *name)
{
    if (!vol || !name) return KGEOFS_ERR_INVALID;

    struct kgeofs_branch_entry *be = vol->branch_index;
    while (be) {
        if (strcmp(be->name, name) == 0)
            return kgeofs_branch_switch(vol, be->id);
        be = be->next;
    }
    return KGEOFS_ERR_NOTFOUND;
}

kgeofs_branch_t kgeofs_branch_current(kgeofs_volume_t *vol)
{
    return vol ? vol->current_branch : 0;
}

int kgeofs_branch_list(kgeofs_volume_t *vol,
                        kgeofs_branch_callback_t callback,
                        void *ctx)
{
    if (!vol || !callback) return 0;
    int count = 0;
    struct kgeofs_branch_entry *be = vol->branch_index;
    while (be) {
        callback(be->id, be->name, be->base_view, be->head_view, be->created, ctx);
        count++;
        be = be->next;
    }
    return count;
}

/*============================================================================
 * Access Control
 *============================================================================*/

void kgeofs_set_context(kgeofs_volume_t *vol, const struct kgeofs_access_ctx *ctx)
{
    if (vol && ctx) vol->current_ctx = *ctx;
}

const struct kgeofs_access_ctx *kgeofs_get_context(kgeofs_volume_t *vol)
{
    return vol ? &vol->current_ctx : NULL;
}

/* Check permission for a file operation */
static kgeofs_error_t check_permission(kgeofs_volume_t *vol,
                                        struct kgeofs_ref_entry *ref,
                                        uint8_t required_perm)
{
    const struct kgeofs_access_ctx *ctx = &vol->current_ctx;

    /* Kernel context always passes */
    if (ctx->uid == 0 || (ctx->caps & 0x80000000))  /* GOV_CAP_KERNEL */
        return KGEOFS_OK;

    /* FS admin capability bypasses */
    if (ctx->caps & 0x00000200)  /* GOV_CAP_FS_ADMIN */
        return KGEOFS_OK;

    /* Check if permission bit is set */
    if (ref->permissions & required_perm)
        return KGEOFS_OK;

    return KGEOFS_ERR_PERM;
}

/* Check quota before writing */
static kgeofs_error_t check_quota(kgeofs_volume_t *vol, size_t new_bytes)
{
    struct kgeofs_quota_entry *branch_quota = NULL;
    struct kgeofs_quota_entry *volume_quota = NULL;

    struct kgeofs_quota_entry *qe = vol->quota_index;
    while (qe) {
        if (qe->branch_id == vol->current_branch)
            branch_quota = qe;
        if (qe->branch_id == KGEOFS_QUOTA_VOLUME)
            volume_quota = qe;
        qe = qe->next;
    }

    /* Check branch quota */
    if (branch_quota && branch_quota->limits.max_content_bytes > 0) {
        if (vol->total_content_bytes + new_bytes > branch_quota->limits.max_content_bytes)
            return KGEOFS_ERR_QUOTA;
    }

    /* Check volume quota */
    if (volume_quota && volume_quota->limits.max_content_bytes > 0) {
        if (vol->total_content_bytes + new_bytes > volume_quota->limits.max_content_bytes)
            return KGEOFS_ERR_QUOTA;
    }

    return KGEOFS_OK;
}

kgeofs_error_t kgeofs_view_hide(kgeofs_volume_t *vol, const char *path)
{
    if (!vol || !path) {
        return KGEOFS_ERR_INVALID;
    }

    /* Check if file exists */
    struct kgeofs_ref_entry *existing = ref_find_best(vol, path);
    if (!existing || existing->is_hidden) {
        return KGEOFS_ERR_NOTFOUND;
    }

    /* Create new view for the hide operation */
    char label[64];
    kprintf("Hide: ");  /* Build label manually */
    size_t path_len = strlen(path);
    if (path_len > 50) path_len = 50;

    strcpy(label, "Hide: ");
    memcpy(label + 6, path, path_len);
    label[6 + path_len] = '\0';

    kgeofs_view_t new_view;
    kgeofs_error_t err = kgeofs_view_create(vol, label, &new_view);
    if (err != KGEOFS_OK) {
        return err;
    }

    /* Create hidden marker ref (auto-grow if needed) */
    size_t record_size = sizeof(struct kgeofs_ref_record);
    struct kgeofs_ram_region *region = region_find_or_grow(
        vol->ref_region, record_size);
    if (!region) {
        return KGEOFS_ERR_FULL;
    }

    struct kgeofs_ref_record *record =
        (struct kgeofs_ref_record *)((uint8_t *)region->base + region->used);

    record->magic = KGEOFS_REF_MAGIC;
    record->flags = KGEOFS_REF_FLAG_HIDDEN;
    hash_path(path, record->path_hash);
    memset(record->content_hash, 0, KGEOFS_HASH_SIZE);  /* No content */
    record->view_id = new_view;
    record->created = kgeofs_time_now();
    record->path_len = strlen(path);
    record->file_type = existing->file_type;
    record->permissions = existing->permissions;
    record->owner_id = existing->owner_id;
    record->reserved_pad = 0;
    strcpy(record->path, path);

    /* Add hidden entry to index */
    struct kgeofs_ref_entry *entry = kmalloc(sizeof(*entry));
    if (entry) {
        memcpy(entry->path_hash, record->path_hash, KGEOFS_HASH_SIZE);
        memset(entry->content_hash, 0, KGEOFS_HASH_SIZE);
        entry->view_id = new_view;
        entry->created = record->created;
        strcpy(entry->path, path);
        entry->is_hidden = 1;
        entry->file_type = existing->file_type;
        entry->permissions = existing->permissions;
        entry->owner_id = existing->owner_id;

        entry->next = vol->ref_index;
        vol->ref_index = entry;
    }

    region->used += record_size;
    vol->total_refs++;

    return KGEOFS_OK;
}

int kgeofs_view_list(kgeofs_volume_t *vol,
                     kgeofs_view_callback_t callback,
                     void *ctx)
{
    if (!vol || !callback) {
        return 0;
    }

    int count = 0;
    struct kgeofs_view_entry *entry = vol->view_index;

    while (entry) {
        callback(entry->id, entry->parent_id, entry->label, entry->created, ctx);
        count++;
        entry = entry->next;
    }

    return count;
}

/*============================================================================
 * High-Level File Functions
 *============================================================================*/

kgeofs_error_t kgeofs_file_write(kgeofs_volume_t *vol,
                                 const char *path,
                                 const void *data,
                                 size_t size)
{
    if (!vol || !path || (!data && size > 0)) {
        return KGEOFS_ERR_INVALID;
    }

    /* Permission check on existing file */
    struct kgeofs_ref_entry *existing = ref_find_best(vol, path);
    if (existing && !existing->is_hidden) {
        kgeofs_error_t perr = check_permission(vol, existing, KGEOFS_PERM_WRITE);
        if (perr != KGEOFS_OK) return perr;
    }

    /* Quota check */
    kgeofs_error_t qerr = check_quota(vol, size);
    if (qerr != KGEOFS_OK) return qerr;

    /* Store content */
    kgeofs_hash_t hash;
    kgeofs_error_t err = kgeofs_content_store(vol, data, size, hash);
    if (err != KGEOFS_OK) {
        return err;
    }

    /* Create reference */
    return kgeofs_ref_create(vol, path, hash);
}

kgeofs_error_t kgeofs_file_read(kgeofs_volume_t *vol,
                                const char *path,
                                void *buf,
                                size_t buf_size,
                                size_t *size_out)
{
    if (!vol || !path || !buf) {
        return KGEOFS_ERR_INVALID;
    }

    /* Permission check */
    struct kgeofs_ref_entry *ref = ref_find_best(vol, path);
    if (ref) {
        kgeofs_error_t perr = check_permission(vol, ref, KGEOFS_PERM_READ);
        if (perr != KGEOFS_OK) return perr;
    }

    /* Resolve path to hash */
    kgeofs_hash_t hash;
    kgeofs_error_t err = kgeofs_ref_resolve(vol, path, hash);
    if (err != KGEOFS_OK) {
        return err;
    }

    /* Read content */
    return kgeofs_content_read(vol, hash, buf, buf_size, size_out);
}

kgeofs_error_t kgeofs_file_stat(kgeofs_volume_t *vol,
                                const char *path,
                                uint64_t *size_out,
                                int *is_dir_out)
{
    if (!vol || !path) {
        return KGEOFS_ERR_INVALID;
    }

    /* Resolve path */
    kgeofs_hash_t hash;
    kgeofs_error_t err = kgeofs_ref_resolve(vol, path, hash);
    if (err != KGEOFS_OK) {
        return err;
    }

    /* Get size */
    uint64_t size;
    err = kgeofs_content_size(vol, hash, &size);
    if (err != KGEOFS_OK) {
        return err;
    }

    if (size_out) {
        *size_out = size;
    }

    /* Check if directory */
    if (is_dir_out) {
        *is_dir_out = 0;
        if (size == KGEOFS_DIR_MARKER_LEN) {
            char marker[KGEOFS_DIR_MARKER_LEN + 1];
            size_t got;
            if (kgeofs_content_read(vol, hash, marker, sizeof(marker), &got) == KGEOFS_OK) {
                marker[got] = '\0';
                if (strcmp(marker, KGEOFS_DIR_MARKER) == 0) {
                    *is_dir_out = 1;
                }
            }
        }
    }

    return KGEOFS_OK;
}

kgeofs_error_t kgeofs_mkdir(kgeofs_volume_t *vol, const char *path)
{
    if (!vol || !path) {
        return KGEOFS_ERR_INVALID;
    }

    /* Check if already exists */
    kgeofs_hash_t existing;
    if (kgeofs_ref_resolve(vol, path, existing) == KGEOFS_OK) {
        return KGEOFS_ERR_EXISTS;
    }

    /* Create directory marker */
    return kgeofs_file_write(vol, path, KGEOFS_DIR_MARKER, KGEOFS_DIR_MARKER_LEN);
}

int kgeofs_exists(kgeofs_volume_t *vol, const char *path)
{
    if (!vol || !path) {
        return 0;
    }

    kgeofs_hash_t hash;
    return kgeofs_ref_resolve(vol, path, hash) == KGEOFS_OK;
}

/*============================================================================
 * Debug Functions
 *============================================================================*/

void kgeofs_dump_stats(kgeofs_volume_t *vol)
{
    if (!vol) return;

    struct kgeofs_stats stats;
    kgeofs_volume_stats(vol, &stats);

    kprintf("GeoFS Statistics:\n");
    kprintf("  Content:  %lu bytes in %lu/%lu bytes region\n",
            (unsigned long)stats.content_bytes,
            (unsigned long)stats.content_region_used,
            (unsigned long)stats.content_region_size);
    kprintf("  Refs:     %lu total, %lu/%lu bytes region\n",
            (unsigned long)stats.ref_count,
            (unsigned long)stats.ref_region_used,
            (unsigned long)stats.ref_region_size);
    kprintf("  Views:    %lu total, %lu/%lu bytes region\n",
            (unsigned long)stats.view_count,
            (unsigned long)stats.view_region_used,
            (unsigned long)stats.view_region_size);
    kprintf("  Dedup:    %lu hits\n", (unsigned long)stats.dedup_hits);
    kprintf("  Current:  view %lu\n", (unsigned long)stats.current_view);
}

void kgeofs_dump_refs(kgeofs_volume_t *vol)
{
    if (!vol) return;

    kprintf("GeoFS References:\n");

    struct kgeofs_ref_entry *entry = vol->ref_index;
    while (entry) {
        char hash_str[65];
        kgeofs_hash_to_string(entry->content_hash, hash_str);
        hash_str[16] = '\0';  /* Truncate for display */

        kprintf("  [v%lu] %s -> %s...%s\n",
                (unsigned long)entry->view_id,
                entry->path,
                hash_str,
                entry->is_hidden ? " (hidden)" : "");

        entry = entry->next;
    }
}

void kgeofs_dump_views(kgeofs_volume_t *vol)
{
    if (!vol) return;

    kprintf("GeoFS Views (Geological Strata):\n");

    struct kgeofs_view_entry *entry = vol->view_index;
    while (entry) {
        kprintf("  [%lu] <- [%lu] \"%s\"%s\n",
                (unsigned long)entry->id,
                (unsigned long)entry->parent_id,
                entry->label,
                entry->id == vol->current_view ? " *CURRENT*" : "");

        entry = entry->next;
    }
}

/*============================================================================
 * Extended File Functions
 *============================================================================*/

/* Internal: case-insensitive substring match */
static int geofs_str_contains_ci(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) return 1;
    int nlen = (int)strlen(needle);
    int hlen = (int)strlen(haystack);
    for (int i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (int j = 0; j < nlen; j++) {
            char a = haystack[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/*
 * File append: read existing content, concatenate new data, write as new version
 */
kgeofs_error_t kgeofs_file_append(kgeofs_volume_t *vol,
                                   const char *path,
                                   const void *data,
                                   size_t size)
{
    if (!vol || !path || (!data && size > 0)) {
        return KGEOFS_ERR_INVALID;
    }

    /* Permission + quota check */
    {
        struct kgeofs_ref_entry *existing = ref_find_best(vol, path);
        if (existing && !existing->is_hidden) {
            kgeofs_error_t perr = check_permission(vol, existing, KGEOFS_PERM_WRITE);
            if (perr != KGEOFS_OK) return perr;
        }
        kgeofs_error_t qerr = check_quota(vol, size);
        if (qerr != KGEOFS_OK) return qerr;
    }

    /* Read existing file content */
    kgeofs_hash_t old_hash;
    kgeofs_error_t err = kgeofs_ref_resolve(vol, path, old_hash);

    if (err == KGEOFS_ERR_NOTFOUND) {
        /* File doesn't exist, just create it */
        return kgeofs_file_write(vol, path, data, size);
    }
    if (err != KGEOFS_OK) return err;

    /* Get old size */
    uint64_t old_size;
    err = kgeofs_content_size(vol, old_hash, &old_size);
    if (err != KGEOFS_OK) return err;

    /* Allocate temp buffer for old + new content */
    size_t new_total = (size_t)old_size + size;
    uint8_t *buf = kmalloc(new_total);
    if (!buf) return KGEOFS_ERR_NOMEM;

    /* Read old content */
    size_t got;
    err = kgeofs_content_read(vol, old_hash, buf, (size_t)old_size, &got);
    if (err != KGEOFS_OK) {
        kfree(buf);
        return err;
    }

    /* Append new data */
    memcpy(buf + got, data, size);

    /* Write combined content */
    err = kgeofs_file_write(vol, path, buf, new_total);
    kfree(buf);
    return err;
}

/*
 * File rename/move: resolve old path, create ref at new path, hide old path
 */
kgeofs_error_t kgeofs_file_rename(kgeofs_volume_t *vol,
                                   const char *old_path,
                                   const char *new_path)
{
    if (!vol || !old_path || !new_path) {
        return KGEOFS_ERR_INVALID;
    }

    /* Resolve old path to content hash */
    struct kgeofs_ref_entry *old_entry = ref_find_best(vol, old_path);
    if (!old_entry || old_entry->is_hidden) {
        return KGEOFS_ERR_NOTFOUND;
    }

    /* Permission check: need write on source */
    {
        kgeofs_error_t perr = check_permission(vol, old_entry, KGEOFS_PERM_WRITE);
        if (perr != KGEOFS_OK) return perr;
    }

    /* Check if new path already exists */
    kgeofs_hash_t check;
    if (kgeofs_ref_resolve(vol, new_path, check) == KGEOFS_OK) {
        return KGEOFS_ERR_EXISTS;
    }

    /* Create ref at new path pointing to same content */
    kgeofs_error_t err = kgeofs_ref_create(vol, new_path, old_entry->content_hash);
    if (err != KGEOFS_OK) return err;

    /* Copy metadata to new ref */
    struct kgeofs_ref_entry *new_entry = ref_find_best(vol, new_path);
    if (new_entry) {
        new_entry->file_type = old_entry->file_type;
        new_entry->permissions = old_entry->permissions;
        new_entry->owner_id = old_entry->owner_id;
    }

    /* Hide old path */
    return kgeofs_view_hide(vol, old_path);
}

/*
 * File copy: creates ref at dest pointing to same content hash (zero-copy dedup)
 */
kgeofs_error_t kgeofs_file_copy(kgeofs_volume_t *vol,
                                 const char *src_path,
                                 const char *dst_path)
{
    if (!vol || !src_path || !dst_path) {
        return KGEOFS_ERR_INVALID;
    }

    /* Resolve source */
    struct kgeofs_ref_entry *src = ref_find_best(vol, src_path);
    if (!src || src->is_hidden) {
        return KGEOFS_ERR_NOTFOUND;
    }

    /* Permission check: need read on source */
    {
        kgeofs_error_t perr = check_permission(vol, src, KGEOFS_PERM_READ);
        if (perr != KGEOFS_OK) return perr;
    }

    /* Check if dest already exists */
    kgeofs_hash_t check;
    if (kgeofs_ref_resolve(vol, dst_path, check) == KGEOFS_OK) {
        return KGEOFS_ERR_EXISTS;
    }

    /* Create ref at dest pointing to same content (zero-copy!) */
    kgeofs_error_t err = kgeofs_ref_create(vol, dst_path, src->content_hash);
    if (err != KGEOFS_OK) return err;

    /* Copy metadata */
    struct kgeofs_ref_entry *dst = ref_find_best(vol, dst_path);
    if (dst) {
        dst->file_type = src->file_type;
        dst->permissions = src->permissions;
        dst->owner_id = src->owner_id;
    }

    return KGEOFS_OK;
}

/*
 * Recursive directory listing
 */
static int tree_recurse(kgeofs_volume_t *vol, const char *dir_path,
                         int depth, int max_depth,
                         kgeofs_tree_callback_t callback, void *ctx)
{
    if (depth > max_depth) return 0;

    size_t dir_len = strlen(dir_path);
    int count = 0;

    struct kgeofs_ref_entry *entry = vol->ref_index;
    while (entry) {
        if (view_in_ancestry(vol, entry->view_id) && !entry->is_hidden) {
            if (strncmp(entry->path, dir_path, dir_len) == 0) {
                const char *rest = entry->path + dir_len;
                if (*rest == '/') rest++;

                /* Check if it's a direct child */
                const char *slash = rest;
                while (*slash && *slash != '/') slash++;

                if (*rest && (*slash == '\0' || *(slash + 1) == '\0')) {
                    struct kgeofs_dirent dirent;
                    memset(&dirent, 0, sizeof(dirent));

                    size_t name_len = slash - rest;
                    if (name_len >= KGEOFS_MAX_NAME) name_len = KGEOFS_MAX_NAME - 1;
                    memcpy(dirent.name, rest, name_len);
                    dirent.name[name_len] = '\0';
                    memcpy(dirent.content_hash, entry->content_hash, KGEOFS_HASH_SIZE);
                    dirent.created = entry->created;
                    dirent.permissions = entry->permissions;
                    dirent.owner_id = entry->owner_id;
                    dirent.file_type = entry->file_type;

                    /* Check if directory */
                    uint64_t size;
                    if (kgeofs_content_size(vol, entry->content_hash, &size) == KGEOFS_OK) {
                        dirent.size = size;
                        if (size == KGEOFS_DIR_MARKER_LEN) {
                            char marker[KGEOFS_DIR_MARKER_LEN + 1];
                            size_t got;
                            if (kgeofs_content_read(vol, entry->content_hash,
                                                    marker, sizeof(marker), &got) == KGEOFS_OK) {
                                marker[got] = '\0';
                                if (strcmp(marker, KGEOFS_DIR_MARKER) == 0)
                                    dirent.is_directory = 1;
                            }
                        }
                    }

                    if (callback(entry->path, &dirent, depth, ctx) != 0)
                        return count;
                    count++;

                    /* Recurse into subdirectories */
                    if (dirent.is_directory && depth < max_depth) {
                        count += tree_recurse(vol, entry->path,
                                             depth + 1, max_depth,
                                             callback, ctx);
                    }
                }
            }
        }
        entry = entry->next;
    }
    return count;
}

int kgeofs_ref_list_recursive(kgeofs_volume_t *vol,
                               const char *dir_path,
                               int max_depth,
                               kgeofs_tree_callback_t callback,
                               void *ctx)
{
    if (!vol || !dir_path || !callback) return 0;
    return tree_recurse(vol, dir_path, 0, max_depth, callback, ctx);
}

/*
 * File search/find: iterate all refs, match by name pattern
 */
int kgeofs_file_find(kgeofs_volume_t *vol,
                      const char *start_path,
                      const char *pattern,
                      kgeofs_find_callback_t callback,
                      void *ctx)
{
    if (!vol || !pattern || !callback) return 0;

    size_t start_len = start_path ? strlen(start_path) : 0;
    int count = 0;

    struct kgeofs_ref_entry *entry = vol->ref_index;
    while (entry) {
        if (view_in_ancestry(vol, entry->view_id) && !entry->is_hidden) {
            /* Check if under start_path */
            int under = 1;
            if (start_path && start_len > 0) {
                if (strncmp(entry->path, start_path, start_len) != 0)
                    under = 0;
            }

            if (under) {
                /* Extract filename from path */
                const char *name = entry->path;
                const char *p = entry->path;
                while (*p) {
                    if (*p == '/') name = p + 1;
                    p++;
                }

                if (geofs_str_contains_ci(name, pattern)) {
                    uint64_t size = 0;
                    kgeofs_content_size(vol, entry->content_hash, &size);

                    int is_dir = (entry->file_type == KGEOFS_TYPE_DIR);
                    if (!is_dir && size == KGEOFS_DIR_MARKER_LEN) {
                        char marker[KGEOFS_DIR_MARKER_LEN + 1];
                        size_t got;
                        if (kgeofs_content_read(vol, entry->content_hash,
                                                marker, sizeof(marker), &got) == KGEOFS_OK) {
                            marker[got] = '\0';
                            if (strcmp(marker, KGEOFS_DIR_MARKER) == 0)
                                is_dir = 1;
                        }
                    }

                    if (callback(entry->path, size, is_dir, ctx) != 0)
                        return count;
                    count++;
                }
            }
        }
        entry = entry->next;
    }
    return count;
}

/*============================================================================
 * Symlinks & Hardlinks
 *============================================================================*/

/* Internal: create a ref with explicit type/permissions/owner */
static kgeofs_error_t ref_create_typed(kgeofs_volume_t *vol,
                                        const char *path,
                                        const kgeofs_hash_t content_hash,
                                        uint8_t file_type,
                                        uint8_t permissions,
                                        uint16_t owner_id)
{
    if (!vol || !path || !content_hash) return KGEOFS_ERR_INVALID;

    size_t path_len = strlen(path);
    if (path_len >= KGEOFS_MAX_PATH) return KGEOFS_ERR_INVALID;

    size_t record_size = sizeof(struct kgeofs_ref_record);
    struct kgeofs_ram_region *region = region_find_or_grow(
        vol->ref_region, record_size);
    if (!region) return KGEOFS_ERR_FULL;

    struct kgeofs_ref_record *record =
        (struct kgeofs_ref_record *)((uint8_t *)region->base + region->used);

    record->magic = KGEOFS_REF_MAGIC;
    record->flags = 0;
    hash_path(path, record->path_hash);
    memcpy(record->content_hash, content_hash, KGEOFS_HASH_SIZE);
    record->view_id = vol->current_view;
    record->created = kgeofs_time_now();
    record->path_len = path_len;
    record->file_type = file_type;
    record->permissions = permissions;
    record->owner_id = owner_id;
    record->reserved_pad = 0;
    strcpy(record->path, path);

    struct kgeofs_ref_entry *entry = kmalloc(sizeof(*entry));
    if (entry) {
        memcpy(entry->path_hash, record->path_hash, KGEOFS_HASH_SIZE);
        memcpy(entry->content_hash, content_hash, KGEOFS_HASH_SIZE);
        entry->view_id = record->view_id;
        entry->created = record->created;
        strcpy(entry->path, path);
        entry->is_hidden = 0;
        entry->file_type = file_type;
        entry->permissions = permissions;
        entry->owner_id = owner_id;
        entry->hash_next = NULL;

        entry->next = vol->ref_index;
        vol->ref_index = entry;
        ref_hash_insert(vol, entry);
    }

    region->used += record_size;
    vol->total_refs++;
    return KGEOFS_OK;
}

/* Count visible refs sharing the same content hash (link count) */
static int count_links(kgeofs_volume_t *vol, const kgeofs_hash_t content_hash)
{
    int count = 0;
    struct kgeofs_ref_entry *entry = vol->ref_index;
    while (entry) {
        if (!entry->is_hidden &&
            view_in_ancestry(vol, entry->view_id) &&
            entry->file_type != KGEOFS_TYPE_LINK &&
            kgeofs_hash_equal(entry->content_hash, content_hash)) {
            count++;
        }
        entry = entry->next;
    }
    return count;
}

kgeofs_error_t kgeofs_file_link(kgeofs_volume_t *vol,
                                 const char *existing_path,
                                 const char *new_path)
{
    if (!vol || !existing_path || !new_path) return KGEOFS_ERR_INVALID;

    struct kgeofs_ref_entry *src = ref_find_best(vol, existing_path);
    if (!src || src->is_hidden) return KGEOFS_ERR_NOTFOUND;
    if (src->file_type == KGEOFS_TYPE_LINK) return KGEOFS_ERR_INVALID;

    kgeofs_error_t perr = check_permission(vol, src, KGEOFS_PERM_READ);
    if (perr != KGEOFS_OK) return perr;

    /* Check dest does not exist */
    struct kgeofs_ref_entry *dst = ref_find_best(vol, new_path);
    if (dst && !dst->is_hidden) return KGEOFS_ERR_EXISTS;

    return ref_create_typed(vol, new_path, src->content_hash,
                            src->file_type, src->permissions, src->owner_id);
}

kgeofs_error_t kgeofs_file_symlink(kgeofs_volume_t *vol,
                                     const char *target_path,
                                     const char *link_path)
{
    if (!vol || !target_path || !link_path) return KGEOFS_ERR_INVALID;

    /* Check link_path does not already exist */
    struct kgeofs_ref_entry *existing = ref_find_best(vol, link_path);
    if (existing && !existing->is_hidden) return KGEOFS_ERR_EXISTS;

    /* Store target path string as content */
    size_t target_len = strlen(target_path);
    kgeofs_hash_t hash;
    kgeofs_error_t err = kgeofs_content_store(vol, target_path, target_len, hash);
    if (err != KGEOFS_OK) return err;

    return ref_create_typed(vol, link_path, hash,
                            KGEOFS_TYPE_LINK, KGEOFS_PERM_DEFAULT,
                            vol->current_ctx.uid);
}

kgeofs_error_t kgeofs_readlink(kgeofs_volume_t *vol,
                                const char *path,
                                char *buf,
                                size_t buf_size)
{
    if (!vol || !path || !buf) return KGEOFS_ERR_INVALID;

    struct kgeofs_ref_entry *entry = ref_find_best(vol, path);
    if (!entry || entry->is_hidden) return KGEOFS_ERR_NOTFOUND;
    if (entry->file_type != KGEOFS_TYPE_LINK) return KGEOFS_ERR_INVALID;

    size_t got;
    kgeofs_error_t err = kgeofs_content_read(vol, entry->content_hash,
                                              buf, buf_size - 1, &got);
    if (err != KGEOFS_OK) return err;
    buf[got] = '\0';
    return KGEOFS_OK;
}

kgeofs_error_t kgeofs_file_stat_full(kgeofs_volume_t *vol,
                                      const char *path,
                                      uint64_t *size_out,
                                      int *is_dir_out,
                                      uint8_t *file_type_out,
                                      uint8_t *permissions_out,
                                      uint16_t *owner_id_out,
                                      kgeofs_time_t *created_out,
                                      int *link_count_out)
{
    if (!vol || !path) return KGEOFS_ERR_INVALID;

    struct kgeofs_ref_entry *entry = ref_find_best(vol, path);
    if (!entry || entry->is_hidden) return KGEOFS_ERR_NOTFOUND;

    if (file_type_out) *file_type_out = entry->file_type;
    if (permissions_out) *permissions_out = entry->permissions;
    if (owner_id_out) *owner_id_out = entry->owner_id;
    if (created_out) *created_out = entry->created;

    /* For symlinks, stat the target */
    if (entry->file_type == KGEOFS_TYPE_LINK) {
        if (is_dir_out) *is_dir_out = 0;
        if (size_out) {
            kgeofs_content_size(vol, entry->content_hash, size_out);
        }
        if (link_count_out) *link_count_out = 1;
        return KGEOFS_OK;
    }

    uint64_t sz = 0;
    kgeofs_content_size(vol, entry->content_hash, &sz);
    if (size_out) *size_out = sz;

    int is_dir = (entry->file_type == KGEOFS_TYPE_DIR);
    if (!is_dir && sz == KGEOFS_DIR_MARKER_LEN) {
        char marker[KGEOFS_DIR_MARKER_LEN + 1];
        size_t got;
        if (kgeofs_content_read(vol, entry->content_hash,
                                marker, sizeof(marker), &got) == KGEOFS_OK) {
            marker[got] = '\0';
            if (strcmp(marker, KGEOFS_DIR_MARKER) == 0) is_dir = 1;
        }
    }
    if (is_dir_out) *is_dir_out = is_dir;

    if (link_count_out) *link_count_out = count_links(vol, entry->content_hash);

    return KGEOFS_OK;
}

/*============================================================================
 * Full-Text Content Search (grep)
 *============================================================================*/

int kgeofs_file_grep(kgeofs_volume_t *vol,
                      const char *dir_path,
                      const char *pattern,
                      int case_insensitive,
                      kgeofs_grep_callback_t callback,
                      void *ctx)
{
    if (!vol || !pattern || !callback) return 0;

    size_t dir_len = dir_path ? strlen(dir_path) : 0;
    int match_count = 0;

    struct kgeofs_ref_entry *entry = vol->ref_index;
    while (entry) {
        if (!view_in_ancestry(vol, entry->view_id) || entry->is_hidden ||
            entry->file_type == KGEOFS_TYPE_DIR ||
            entry->file_type == KGEOFS_TYPE_LINK) {
            entry = entry->next;
            continue;
        }

        /* Check if under dir_path */
        if (dir_path && dir_len > 0) {
            if (strncmp(entry->path, dir_path, dir_len) != 0) {
                entry = entry->next;
                continue;
            }
        }

        /* Read file content (limit to 64KB) */
        uint64_t file_size;
        if (kgeofs_content_size(vol, entry->content_hash, &file_size) != KGEOFS_OK ||
            file_size == 0 || file_size > 65536) {
            entry = entry->next;
            continue;
        }

        uint8_t *buf = kmalloc((size_t)file_size + 1);
        if (!buf) { entry = entry->next; continue; }

        size_t got;
        if (kgeofs_content_read(vol, entry->content_hash,
                                buf, (size_t)file_size, &got) != KGEOFS_OK) {
            kfree(buf);
            entry = entry->next;
            continue;
        }
        buf[got] = '\0';

        /* Scan line by line */
        int line_num = 1;
        char *line_start = (char *)buf;
        for (size_t i = 0; i <= got; i++) {
            if (i == got || buf[i] == '\n') {
                char saved = buf[i];
                buf[i] = '\0';

                int match;
                if (case_insensitive)
                    match = geofs_str_contains_ci(line_start, pattern);
                else
                    match = (strstr(line_start, pattern) != NULL);

                if (match) {
                    if (callback(entry->path, line_num, line_start, ctx) != 0) {
                        kfree(buf);
                        return match_count;
                    }
                    match_count++;
                }

                buf[i] = saved;
                line_start = (char *)buf + i + 1;
                line_num++;
            }
        }

        kfree(buf);
        entry = entry->next;
    }

    return match_count;
}

/*============================================================================
 * Enhanced File Find with Filters
 *============================================================================*/

int kgeofs_file_find_filtered(kgeofs_volume_t *vol,
                                const char *start_path,
                                const char *name_pattern,
                                const struct kgeofs_find_filter *filter,
                                kgeofs_find_callback_t callback,
                                void *ctx)
{
    if (!vol || !callback) return 0;

    size_t start_len = start_path ? strlen(start_path) : 0;
    int count = 0;

    struct kgeofs_ref_entry *entry = vol->ref_index;
    while (entry) {
        if (view_in_ancestry(vol, entry->view_id) && !entry->is_hidden) {
            int under = 1;
            if (start_path && start_len > 0) {
                if (strncmp(entry->path, start_path, start_len) != 0)
                    under = 0;
            }

            if (under) {
                /* Name pattern match (if provided) */
                if (name_pattern && name_pattern[0]) {
                    const char *name = entry->path;
                    const char *p = entry->path;
                    while (*p) { if (*p == '/') name = p + 1; p++; }
                    if (!geofs_str_contains_ci(name, name_pattern)) {
                        entry = entry->next;
                        continue;
                    }
                }

                /* Apply filters */
                if (filter) {
                    uint64_t size = 0;
                    kgeofs_content_size(vol, entry->content_hash, &size);

                    if (filter->min_size > 0 && size < filter->min_size) {
                        entry = entry->next; continue;
                    }
                    if (filter->max_size > 0 && size > filter->max_size) {
                        entry = entry->next; continue;
                    }
                    if (filter->file_type != 0xFF &&
                        entry->file_type != filter->file_type) {
                        entry = entry->next; continue;
                    }
                    if (filter->owner_id != 0xFFFF &&
                        entry->owner_id != filter->owner_id) {
                        entry = entry->next; continue;
                    }
                }

                uint64_t size = 0;
                kgeofs_content_size(vol, entry->content_hash, &size);
                int is_dir = (entry->file_type == KGEOFS_TYPE_DIR);

                if (callback(entry->path, size, is_dir, ctx) != 0)
                    return count;
                count++;
            }
        }
        entry = entry->next;
    }
    return count;
}

/*
 * Set file permissions (creates new ref version with updated permissions)
 */
kgeofs_error_t kgeofs_file_chmod(kgeofs_volume_t *vol,
                                  const char *path,
                                  uint8_t permissions)
{
    if (!vol || !path) return KGEOFS_ERR_INVALID;

    struct kgeofs_ref_entry *entry = ref_find_best(vol, path);
    if (!entry || entry->is_hidden) return KGEOFS_ERR_NOTFOUND;

    /* Only owner or admin can chmod */
    if (vol->current_ctx.uid != 0 &&
        !(vol->current_ctx.caps & 0x80000200) &&  /* KERNEL|FS_ADMIN */
        vol->current_ctx.uid != entry->owner_id)
        return KGEOFS_ERR_PERM;

    /* Create a new ref with the same content but updated permissions */
    kgeofs_error_t err = kgeofs_ref_create(vol, path, entry->content_hash);
    if (err != KGEOFS_OK) return err;

    /* Update the new entry's permissions */
    struct kgeofs_ref_entry *new_entry = ref_find_best(vol, path);
    if (new_entry) {
        new_entry->permissions = permissions;
        new_entry->file_type = entry->file_type;
        new_entry->owner_id = entry->owner_id;
    }
    return KGEOFS_OK;
}

/*
 * Set file owner
 */
kgeofs_error_t kgeofs_file_chown(kgeofs_volume_t *vol,
                                  const char *path,
                                  uint16_t owner_id)
{
    if (!vol || !path) return KGEOFS_ERR_INVALID;

    struct kgeofs_ref_entry *entry = ref_find_best(vol, path);
    if (!entry || entry->is_hidden) return KGEOFS_ERR_NOTFOUND;

    /* Only admin can chown */
    if (vol->current_ctx.uid != 0 && !(vol->current_ctx.caps & 0x80000200))
        return KGEOFS_ERR_PERM;

    kgeofs_error_t err = kgeofs_ref_create(vol, path, entry->content_hash);
    if (err != KGEOFS_OK) return err;

    struct kgeofs_ref_entry *new_entry = ref_find_best(vol, path);
    if (new_entry) {
        new_entry->owner_id = owner_id;
        new_entry->file_type = entry->file_type;
        new_entry->permissions = entry->permissions;
    }
    return KGEOFS_OK;
}

/*============================================================================
 * View Diff Functions
 *============================================================================*/

int kgeofs_view_diff(kgeofs_volume_t *vol,
                      kgeofs_view_t view_a,
                      kgeofs_view_t view_b,
                      kgeofs_diff_callback_t callback,
                      void *ctx)
{
    if (!vol || !callback) return 0;

    /* Ensure lo < hi for ordering */
    kgeofs_view_t lo = view_a < view_b ? view_a : view_b;
    kgeofs_view_t hi = view_a < view_b ? view_b : view_a;

    int count = 0;
    struct kgeofs_ref_entry *entry = vol->ref_index;

    while (entry) {
        /* Find refs that were created between the two views */
        if (entry->view_id > lo && entry->view_id <= hi) {
            struct kgeofs_diff_entry diff;
            memset(&diff, 0, sizeof(diff));
            strcpy(diff.path, entry->path);
            diff.view_id = entry->view_id;
            diff.timestamp = entry->created;

            if (entry->is_hidden) {
                diff.change_type = 2; /* hidden */
            } else {
                /* Check if there was a prior ref for this path */
                kgeofs_hash_t path_hash;
                hash_path(entry->path, path_hash);

                int had_prior = 0;
                struct kgeofs_ref_entry *scan = vol->ref_index;
                while (scan) {
                    if (scan != entry &&
                        kgeofs_hash_equal(scan->path_hash, path_hash) &&
                        scan->view_id <= lo &&
                        !scan->is_hidden) {
                        had_prior = 1;
                        break;
                    }
                    scan = scan->next;
                }

                diff.change_type = had_prior ? 1 : 0; /* 1=modified, 0=added */
            }

            if (callback(&diff, ctx) != 0)
                return count;
            count++;
        }
        entry = entry->next;
    }
    return count;
}

/*============================================================================
 * ATA Import/Export Functions
 *============================================================================*/

#define ATA_SECTOR_SIZE 512

kgeofs_error_t kgeofs_file_export_ata(kgeofs_volume_t *vol,
                                       const char *path,
                                       uint8_t drive,
                                       uint64_t start_sector,
                                       uint64_t *sectors_written)
{
    if (!vol || !path) return KGEOFS_ERR_INVALID;

    /* Resolve file */
    kgeofs_hash_t hash;
    kgeofs_error_t err = kgeofs_ref_resolve(vol, path, hash);
    if (err != KGEOFS_OK) return err;

    uint64_t file_size;
    err = kgeofs_content_size(vol, hash, &file_size);
    if (err != KGEOFS_OK) return err;

    /* Read file content into temp buffer */
    uint8_t *buf = kmalloc((size_t)file_size);
    if (!buf) return KGEOFS_ERR_NOMEM;

    size_t got;
    err = kgeofs_content_read(vol, hash, buf, (size_t)file_size, &got);
    if (err != KGEOFS_OK) {
        kfree(buf);
        return err;
    }

    /* Write to ATA in sector-sized chunks */
    uint64_t total_sectors = (got + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    uint8_t sector_buf[ATA_SECTOR_SIZE];

    for (uint64_t i = 0; i < total_sectors; i++) {
        memset(sector_buf, 0, ATA_SECTOR_SIZE);
        size_t offset = (size_t)(i * ATA_SECTOR_SIZE);
        size_t chunk = got - offset;
        if (chunk > ATA_SECTOR_SIZE) chunk = ATA_SECTOR_SIZE;
        memcpy(sector_buf, buf + offset, chunk);

        if (ata_write_sectors(drive, start_sector + i, 1, sector_buf) != 0) {
            kfree(buf);
            if (sectors_written) *sectors_written = i;
            return KGEOFS_ERR_IO;
        }
    }

    kfree(buf);
    if (sectors_written) *sectors_written = total_sectors;
    return KGEOFS_OK;
}

kgeofs_error_t kgeofs_file_import_ata(kgeofs_volume_t *vol,
                                       const char *path,
                                       uint8_t drive,
                                       uint64_t start_sector,
                                       uint64_t num_sectors)
{
    if (!vol || !path || num_sectors == 0) return KGEOFS_ERR_INVALID;

    size_t total_bytes = (size_t)(num_sectors * ATA_SECTOR_SIZE);
    uint8_t *buf = kmalloc(total_bytes);
    if (!buf) return KGEOFS_ERR_NOMEM;

    /* Read from ATA */
    uint8_t sector_buf[ATA_SECTOR_SIZE];
    for (uint64_t i = 0; i < num_sectors; i++) {
        if (ata_read_sectors(drive, start_sector + i, 1, sector_buf) != 0) {
            kfree(buf);
            return KGEOFS_ERR_IO;
        }
        memcpy(buf + (size_t)(i * ATA_SECTOR_SIZE), sector_buf, ATA_SECTOR_SIZE);
    }

    /* Write as file to GeoFS */
    kgeofs_error_t err = kgeofs_file_write(vol, path, buf, total_bytes);
    kfree(buf);
    return err;
}

/*============================================================================
 * Volume Persistence (Save/Restore to ATA Disk)
 *============================================================================*/

/*
 * Write a region chain to consecutive ATA sectors.
 * Streams through the linked-list chunks using a 512-byte staging buffer.
 */
static kgeofs_error_t persist_write_region(struct kgeofs_ram_region *region,
                                            uint8_t drive,
                                            uint64_t start_sector,
                                            uint64_t *sectors_written)
{
    uint8_t sector_buf[ATA_SECTOR_SIZE];
    uint64_t sector = start_sector;
    int buf_pos = 0;

    memset(sector_buf, 0, ATA_SECTOR_SIZE);

    while (region) {
        uint8_t *src = (uint8_t *)region->base;
        size_t remaining = region->used;

        while (remaining > 0) {
            size_t chunk = (size_t)(ATA_SECTOR_SIZE - buf_pos);
            if (chunk > remaining) chunk = remaining;
            memcpy(sector_buf + buf_pos, src, chunk);
            buf_pos += (int)chunk;
            src += chunk;
            remaining -= chunk;

            if (buf_pos == ATA_SECTOR_SIZE) {
                if (ata_write_sectors(drive, sector, 1, sector_buf) != 0)
                    return KGEOFS_ERR_IO;
                sector++;
                buf_pos = 0;
                memset(sector_buf, 0, ATA_SECTOR_SIZE);
            }
        }
        region = region->next;
    }

    /* Flush partial final sector */
    if (buf_pos > 0) {
        if (ata_write_sectors(drive, sector, 1, sector_buf) != 0)
            return KGEOFS_ERR_IO;
        sector++;
    }

    if (sectors_written) *sectors_written = sector - start_sector;
    return KGEOFS_OK;
}

/*
 * Read ATA sectors into a newly allocated region.
 */
static kgeofs_error_t persist_read_region(uint8_t drive,
                                           uint64_t start_sector,
                                           uint64_t sector_count,
                                           uint64_t used_bytes,
                                           struct kgeofs_ram_region **region_out)
{
    size_t pages = (size_t)((used_bytes + 4095) / 4096);
    if (pages == 0) pages = 1;

    struct kgeofs_ram_region *region = alloc_region(pages);
    if (!region) return KGEOFS_ERR_NOMEM;

    uint8_t sector_buf[ATA_SECTOR_SIZE];
    uint8_t *dst = (uint8_t *)region->base;
    uint64_t bytes_left = used_bytes;

    for (uint64_t s = 0; s < sector_count && bytes_left > 0; s++) {
        if (ata_read_sectors(drive, start_sector + s, 1, sector_buf) != 0) {
            free_region(region);
            return KGEOFS_ERR_IO;
        }
        size_t chunk = bytes_left > ATA_SECTOR_SIZE ? ATA_SECTOR_SIZE : (size_t)bytes_left;
        memcpy(dst, sector_buf, chunk);
        dst += chunk;
        bytes_left -= chunk;
    }

    region->used = (size_t)used_bytes;
    *region_out = region;
    return KGEOFS_OK;
}

/*
 * Rebuild in-memory indices by scanning raw region data.
 * Called after loading regions from disk.
 */
static kgeofs_error_t rebuild_indices(kgeofs_volume_t *vol)
{
    /* Pass 1: Scan content region */
    {
        struct kgeofs_ram_region *r = vol->content_region;
        while (r) {
            uint8_t *base = (uint8_t *)r->base;
            size_t pos = 0;

            while (pos + sizeof(struct kgeofs_content_header) <= r->used) {
                struct kgeofs_content_header *hdr =
                    (struct kgeofs_content_header *)(base + pos);

                if (hdr->magic != KGEOFS_CONTENT_MAGIC)
                    break;  /* End of valid records */

                size_t total = sizeof(struct kgeofs_content_header) + (size_t)hdr->size;
                if (pos + total > r->used)
                    break;  /* Truncated record */

                struct kgeofs_content_entry *entry = kmalloc(sizeof(*entry));
                if (entry) {
                    memcpy(entry->hash, hdr->hash, KGEOFS_HASH_SIZE);
                    entry->offset = pos;

                    /* For compressed content, report decompressed size */
                    if (hdr->flags & KGEOFS_CONTENT_FLAG_COMPRESSED) {
                        uint64_t original_size;
                        memcpy(&original_size, hdr->reserved, sizeof(original_size));
                        entry->size = original_size;
                    } else {
                        entry->size = hdr->size;
                    }

                    uint8_t bucket = KGEOFS_HASH_BUCKET(entry->hash);
                    entry->next = vol->content_hash[bucket];
                    vol->content_hash[bucket] = entry;
                }

                pos += total;
            }
            r = r->next;
        }
    }

    /* Pass 2: Scan ref region */
    {
        struct kgeofs_ram_region *r = vol->ref_region;
        while (r) {
            uint8_t *base = (uint8_t *)r->base;
            size_t pos = 0;

            while (pos + sizeof(struct kgeofs_ref_record) <= r->used) {
                struct kgeofs_ref_record *rec =
                    (struct kgeofs_ref_record *)(base + pos);

                if (rec->magic != KGEOFS_REF_MAGIC)
                    break;

                struct kgeofs_ref_entry *entry = kmalloc(sizeof(*entry));
                if (entry) {
                    memcpy(entry->path_hash, rec->path_hash, KGEOFS_HASH_SIZE);
                    memcpy(entry->content_hash, rec->content_hash, KGEOFS_HASH_SIZE);
                    entry->view_id = rec->view_id;
                    entry->created = rec->created;
                    strcpy(entry->path, rec->path);
                    entry->is_hidden = (rec->flags & KGEOFS_REF_FLAG_HIDDEN) ? 1 : 0;
                    entry->file_type = rec->file_type;
                    entry->permissions = rec->permissions;
                    entry->owner_id = rec->owner_id;
                    entry->hash_next = NULL;

                    entry->next = vol->ref_index;
                    vol->ref_index = entry;
                    ref_hash_insert(vol, entry);
                }

                pos += sizeof(struct kgeofs_ref_record);
            }
            r = r->next;
        }
    }

    /* Pass 3: Scan view region (views, branches, quotas — dispatch on magic) */
    {
        struct kgeofs_ram_region *r = vol->view_region;
        while (r) {
            uint8_t *base = (uint8_t *)r->base;
            size_t pos = 0;

            while (pos + 4 <= r->used) {
                uint32_t magic = *(uint32_t *)(base + pos);

                if (magic == KGEOFS_VIEW_MAGIC) {
                    /* V1 view record (96 bytes) */
                    if (pos + sizeof(struct kgeofs_view_record) > r->used) break;
                    struct kgeofs_view_record *rec =
                        (struct kgeofs_view_record *)(base + pos);

                    struct kgeofs_view_entry *entry = kmalloc(sizeof(*entry));
                    if (entry) {
                        entry->id = rec->id;
                        entry->parent_id = rec->parent_id;
                        entry->branch_id = 0;  /* V1 = main branch */
                        entry->created = rec->created;
                        strcpy(entry->label, rec->label);
                        entry->next = vol->view_index;
                        vol->view_index = entry;
                    }
                    pos += sizeof(struct kgeofs_view_record);

                } else if (magic == KGEOFS_VIEW2_MAGIC) {
                    /* V2 view record (104 bytes, has branch_id) */
                    if (pos + sizeof(struct kgeofs_view2_record) > r->used) break;
                    struct kgeofs_view2_record *rec =
                        (struct kgeofs_view2_record *)(base + pos);

                    struct kgeofs_view_entry *entry = kmalloc(sizeof(*entry));
                    if (entry) {
                        entry->id = rec->id;
                        entry->parent_id = rec->parent_id;
                        entry->branch_id = rec->branch_id;
                        entry->created = rec->created;
                        strcpy(entry->label, rec->label);
                        entry->next = vol->view_index;
                        vol->view_index = entry;
                    }
                    pos += sizeof(struct kgeofs_view2_record);

                } else if (magic == KGEOFS_BRANCH_MAGIC) {
                    /* Branch record (104 bytes, last-writer-wins) */
                    if (pos + sizeof(struct kgeofs_branch_record) > r->used) break;
                    struct kgeofs_branch_record *rec =
                        (struct kgeofs_branch_record *)(base + pos);

                    /* Update existing or create new */
                    struct kgeofs_branch_entry *existing = NULL;
                    struct kgeofs_branch_entry *be = vol->branch_index;
                    while (be) {
                        if (be->id == rec->id) { existing = be; break; }
                        be = be->next;
                    }
                    if (existing) {
                        existing->head_view = rec->head_view;
                    } else {
                        struct kgeofs_branch_entry *entry = kmalloc(sizeof(*entry));
                        if (entry) {
                            entry->id = rec->id;
                            entry->base_view = rec->base_view;
                            entry->head_view = rec->head_view;
                            entry->created = rec->created;
                            strncpy(entry->name, rec->name, KGEOFS_BRANCH_NAME_MAX);
                            entry->name[KGEOFS_BRANCH_NAME_MAX - 1] = '\0';
                            entry->next = vol->branch_index;
                            vol->branch_index = entry;
                        }
                    }
                    pos += sizeof(struct kgeofs_branch_record);

                } else if (magic == KGEOFS_QUOTA_MAGIC) {
                    /* Quota record (last-writer-wins per branch_id) */
                    if (pos + sizeof(struct kgeofs_quota_record) > r->used) break;
                    struct kgeofs_quota_record *rec =
                        (struct kgeofs_quota_record *)(base + pos);

                    struct kgeofs_quota_entry *existing = NULL;
                    struct kgeofs_quota_entry *qe = vol->quota_index;
                    while (qe) {
                        if (qe->branch_id == rec->branch_id) { existing = qe; break; }
                        qe = qe->next;
                    }
                    if (existing) {
                        existing->limits = rec->limits;
                    } else {
                        struct kgeofs_quota_entry *entry = kmalloc(sizeof(*entry));
                        if (entry) {
                            entry->branch_id = rec->branch_id;
                            entry->limits = rec->limits;
                            entry->next = vol->quota_index;
                            vol->quota_index = entry;
                        }
                    }
                    pos += sizeof(struct kgeofs_quota_record);

                } else {
                    break;  /* Unknown magic, end of valid records */
                }
            }
            r = r->next;
        }
    }

    /* Rebuild ancestry cache */
    rebuild_ancestry_cache(vol);

    return KGEOFS_OK;
}

/*
 * Save entire volume to ATA disk.
 */
kgeofs_error_t kgeofs_volume_save(kgeofs_volume_t *vol,
                                   uint8_t drive,
                                   uint64_t start_sector)
{
    if (!vol) return KGEOFS_ERR_INVALID;

    /* Compute region sizes */
    uint64_t content_used = region_total_used(vol->content_region);
    uint64_t ref_used = region_total_used(vol->ref_region);
    uint64_t view_used = region_total_used(vol->view_region);

    uint64_t content_sectors = (content_used + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    uint64_t ref_sectors = (ref_used + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    uint64_t view_sectors = (view_used + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;

    /* Build superblock */
    struct kgeofs_persist_header hdr;
    memset(&hdr, 0, sizeof(hdr));

    hdr.magic = KGEOFS_PERSIST_MAGIC;
    hdr.version = KGEOFS_PERSIST_VERSION;
    hdr.flags = 0;
    hdr.current_view = vol->current_view;
    hdr.next_view_id = vol->next_view_id;
    hdr.created = vol->created;
    hdr.total_content_bytes = vol->total_content_bytes;
    hdr.total_refs = vol->total_refs;
    hdr.total_views = vol->total_views;
    hdr.dedup_hits = vol->dedup_hits;
    hdr.total_lookups = vol->total_lookups;
    hdr.current_branch = vol->current_branch;
    hdr.next_branch_id = vol->next_branch_id;
    hdr.total_branches = vol->total_branches;
    hdr.content_used = content_used;
    hdr.ref_used = ref_used;
    hdr.view_used = view_used;
    hdr.content_start_sector = 1;
    hdr.content_sector_count = content_sectors;
    hdr.ref_start_sector = 1 + content_sectors;
    hdr.ref_sector_count = ref_sectors;
    hdr.view_start_sector = 1 + content_sectors + ref_sectors;
    hdr.view_sector_count = view_sectors;

    /* Write superblock */
    if (ata_write_sectors(drive, start_sector, 1, &hdr) != 0)
        return KGEOFS_ERR_IO;

    /* Write content region */
    kgeofs_error_t err;
    uint64_t written;

    err = persist_write_region(vol->content_region, drive,
                                start_sector + hdr.content_start_sector, &written);
    if (err != KGEOFS_OK) return err;

    /* Write ref region */
    err = persist_write_region(vol->ref_region, drive,
                                start_sector + hdr.ref_start_sector, &written);
    if (err != KGEOFS_OK) return err;

    /* Write view region */
    err = persist_write_region(vol->view_region, drive,
                                start_sector + hdr.view_start_sector, &written);
    if (err != KGEOFS_OK) return err;

    ata_flush(drive);

    uint64_t total_sectors = 1 + content_sectors + ref_sectors + view_sectors;
    kprintf("[GeoFS] Saved: %lu sectors (%lu KB) to drive %u sector %lu\n",
            (unsigned long)total_sectors,
            (unsigned long)(total_sectors * ATA_SECTOR_SIZE / 1024),
            (unsigned)drive, (unsigned long)start_sector);
    kprintf("  Content: %lu bytes (%lu sectors)\n",
            (unsigned long)content_used, (unsigned long)content_sectors);
    kprintf("  Refs:    %lu bytes (%lu sectors)\n",
            (unsigned long)ref_used, (unsigned long)ref_sectors);
    kprintf("  Views:   %lu bytes (%lu sectors)\n",
            (unsigned long)view_used, (unsigned long)view_sectors);

    return KGEOFS_OK;
}

/*
 * Load volume from ATA disk.
 */
kgeofs_error_t kgeofs_volume_load(uint8_t drive,
                                   uint64_t start_sector,
                                   kgeofs_volume_t **vol_out)
{
    if (!vol_out) return KGEOFS_ERR_INVALID;

    /* Read superblock */
    struct kgeofs_persist_header hdr;
    memset(&hdr, 0, sizeof(hdr));

    if (ata_read_sectors(drive, start_sector, 1, &hdr) != 0)
        return KGEOFS_ERR_IO;

    /* Validate */
    if (hdr.magic != KGEOFS_PERSIST_MAGIC) {
        kprintf("[GeoFS] Load: bad magic (no saved volume at sector %lu)\n",
                (unsigned long)start_sector);
        return KGEOFS_ERR_CORRUPT;
    }
    if (hdr.version != 1 && hdr.version != 2) {
        kprintf("[GeoFS] Load: unsupported version %u\n", hdr.version);
        return KGEOFS_ERR_CORRUPT;
    }

    /* Allocate volume */
    kgeofs_volume_t *vol = kmalloc(sizeof(*vol));
    if (!vol) return KGEOFS_ERR_NOMEM;
    memset(vol, 0, sizeof(*vol));

    vol->magic = KGEOFS_MAGIC;
    vol->version = KGEOFS_VERSION;
    vol->created = hdr.created;
    vol->current_view = hdr.current_view;
    vol->next_view_id = hdr.next_view_id;
    vol->total_content_bytes = hdr.total_content_bytes;
    vol->total_refs = hdr.total_refs;
    vol->total_views = hdr.total_views;
    vol->dedup_hits = hdr.dedup_hits;
    vol->total_lookups = hdr.total_lookups;

    /* v2 branch fields (default to main branch if v1) */
    if (hdr.version >= 2) {
        vol->current_branch = hdr.current_branch;
        vol->next_branch_id = hdr.next_branch_id;
        vol->total_branches = hdr.total_branches;
    } else {
        vol->current_branch = 0;
        vol->next_branch_id = 1;
        vol->total_branches = 0;
    }

    /* Default access context: kernel */
    vol->current_ctx.uid = 0;
    vol->current_ctx.gid = 0;
    vol->current_ctx.caps = 0x80000000;

    /* Read content region */
    kgeofs_error_t err;
    err = persist_read_region(drive,
                               start_sector + hdr.content_start_sector,
                               hdr.content_sector_count,
                               hdr.content_used,
                               &vol->content_region);
    if (err != KGEOFS_OK) {
        kfree(vol);
        return err;
    }

    /* Read ref region */
    err = persist_read_region(drive,
                               start_sector + hdr.ref_start_sector,
                               hdr.ref_sector_count,
                               hdr.ref_used,
                               &vol->ref_region);
    if (err != KGEOFS_OK) {
        free_region(vol->content_region);
        kfree(vol);
        return err;
    }

    /* Read view region */
    err = persist_read_region(drive,
                               start_sector + hdr.view_start_sector,
                               hdr.view_sector_count,
                               hdr.view_used,
                               &vol->view_region);
    if (err != KGEOFS_OK) {
        free_region(vol->ref_region);
        free_region(vol->content_region);
        kfree(vol);
        return err;
    }

    /* Rebuild in-memory indices from raw region data */
    err = rebuild_indices(vol);
    if (err != KGEOFS_OK) {
        free_region(vol->view_region);
        free_region(vol->ref_region);
        free_region(vol->content_region);
        kfree(vol);
        return err;
    }

    *vol_out = vol;

    uint64_t total_sectors = 1 + hdr.content_sector_count +
                             hdr.ref_sector_count + hdr.view_sector_count;
    kprintf("[GeoFS] Loaded: %lu sectors (%lu KB) from drive %u sector %lu\n",
            (unsigned long)total_sectors,
            (unsigned long)(total_sectors * ATA_SECTOR_SIZE / 1024),
            (unsigned)drive, (unsigned long)start_sector);
    kprintf("  Content: %lu bytes, Refs: %lu, Views: %lu\n",
            (unsigned long)hdr.content_used,
            (unsigned long)hdr.total_refs,
            (unsigned long)hdr.total_views);

    return KGEOFS_OK;
}

/*============================================================================
 * Branch Diff & Merge
 *============================================================================*/

/* Build ancestry chain for a given view into a buffer. Returns count. */
static int build_ancestry(kgeofs_volume_t *vol, kgeofs_view_t view_id,
                           kgeofs_view_t *buf, int max)
{
    int count = 0;
    kgeofs_view_t walk = view_id;
    while (walk != 0 && count < max) {
        buf[count++] = walk;
        struct kgeofs_view_entry *ve = vol->view_index;
        kgeofs_view_t parent = 0;
        while (ve) {
            if (ve->id == walk) { parent = ve->parent_id; break; }
            ve = ve->next;
        }
        walk = parent;
    }
    return count;
}

/* Find common ancestor of two views */
static kgeofs_view_t find_common_ancestor(kgeofs_volume_t *vol,
                                           kgeofs_view_t view_a,
                                           kgeofs_view_t view_b)
{
    kgeofs_view_t chain_a[KGEOFS_MAX_ANCESTRY];
    int len_a = build_ancestry(vol, view_a, chain_a, KGEOFS_MAX_ANCESTRY);

    kgeofs_view_t walk = view_b;
    while (walk != 0) {
        for (int i = 0; i < len_a; i++) {
            if (chain_a[i] == walk) return walk;
        }
        struct kgeofs_view_entry *ve = vol->view_index;
        kgeofs_view_t parent = 0;
        while (ve) {
            if (ve->id == walk) { parent = ve->parent_id; break; }
            ve = ve->next;
        }
        walk = parent;
    }
    return 0; /* No common ancestor (shouldn't happen with valid volumes) */
}

/* Check if view_id is in a given ancestry chain */
static int view_in_chain(kgeofs_view_t *chain, int count, kgeofs_view_t view_id)
{
    for (int i = 0; i < count; i++) {
        if (chain[i] == view_id) return 1;
    }
    return 0;
}

int kgeofs_branch_diff(kgeofs_volume_t *vol,
                        kgeofs_branch_t branch_a,
                        kgeofs_branch_t branch_b,
                        kgeofs_diff_callback_t callback,
                        void *ctx)
{
    if (!vol || !callback) return 0;

    /* Find branch heads */
    kgeofs_view_t head_a = 0, head_b = 0;
    struct kgeofs_branch_entry *be = vol->branch_index;
    while (be) {
        if (be->id == branch_a) head_a = be->head_view;
        if (be->id == branch_b) head_b = be->head_view;
        be = be->next;
    }
    if (head_a == 0 || head_b == 0) return 0;

    kgeofs_view_t ancestor = find_common_ancestor(vol, head_a, head_b);

    /* Build ancestry chains for both branches */
    kgeofs_view_t chain_a[KGEOFS_MAX_ANCESTRY], chain_b[KGEOFS_MAX_ANCESTRY];
    int len_a = build_ancestry(vol, head_a, chain_a, KGEOFS_MAX_ANCESTRY);
    int len_b = build_ancestry(vol, head_b, chain_b, KGEOFS_MAX_ANCESTRY);

    int count = 0;

    /* Find refs that are on branch_b but not branch_a (since ancestor) */
    struct kgeofs_ref_entry *entry = vol->ref_index;
    while (entry) {
        /* Check if ref is on branch_b's lineage but after ancestor */
        if (view_in_chain(chain_b, len_b, entry->view_id) &&
            entry->view_id != ancestor &&
            !view_in_chain(chain_a, len_a, entry->view_id)) {

            struct kgeofs_diff_entry de;
            memset(&de, 0, sizeof(de));
            strncpy(de.path, entry->path, KGEOFS_MAX_PATH - 1);
            de.view_id = entry->view_id;
            de.timestamp = entry->created;

            if (entry->is_hidden) {
                de.change_type = 2; /* hidden */
            } else {
                /* Check if path exists on branch_a */
                int exists_on_a = 0;
                struct kgeofs_ref_entry *check = vol->ref_index;
                while (check) {
                    if (view_in_chain(chain_a, len_a, check->view_id) &&
                        strcmp(check->path, entry->path) == 0 &&
                        !check->is_hidden) {
                        exists_on_a = 1;
                        break;
                    }
                    check = check->next;
                }
                de.change_type = exists_on_a ? 1 : 0; /* modified or added */
            }

            if (callback(&de, ctx) != 0) break;
            count++;
        }
        entry = entry->next;
    }
    return count;
}

kgeofs_error_t kgeofs_branch_merge(kgeofs_volume_t *vol,
                                    kgeofs_branch_t source,
                                    const char *label,
                                    int *conflict_count)
{
    if (!vol || !label || !conflict_count) return KGEOFS_ERR_INVALID;
    *conflict_count = 0;

    /* Find source branch head */
    kgeofs_view_t source_head = 0;
    char source_name[KGEOFS_BRANCH_NAME_MAX] = {0};
    struct kgeofs_branch_entry *be = vol->branch_index;
    while (be) {
        if (be->id == source) {
            source_head = be->head_view;
            strncpy(source_name, be->name, KGEOFS_BRANCH_NAME_MAX - 1);
            break;
        }
        be = be->next;
    }
    if (source_head == 0) return KGEOFS_ERR_NOTFOUND;

    /* Find current branch head */
    kgeofs_view_t our_head = vol->current_view;
    kgeofs_view_t ancestor = find_common_ancestor(vol, our_head, source_head);

    /* Build ancestry chains */
    kgeofs_view_t chain_ours[KGEOFS_MAX_ANCESTRY], chain_theirs[KGEOFS_MAX_ANCESTRY];
    int len_ours = build_ancestry(vol, our_head, chain_ours, KGEOFS_MAX_ANCESTRY);
    int len_theirs = build_ancestry(vol, source_head, chain_theirs, KGEOFS_MAX_ANCESTRY);

    /* Create merge view */
    char merge_label[64];
    int mlen = 0;
    const char *prefix = "Merge: ";
    while (*prefix && mlen < 63) merge_label[mlen++] = *prefix++;
    for (int i = 0; source_name[i] && mlen < 63; i++) merge_label[mlen++] = source_name[i];
    merge_label[mlen] = '\0';

    kgeofs_view_t merge_view;
    kgeofs_error_t err = kgeofs_view_create(vol, merge_label, &merge_view);
    if (err != KGEOFS_OK) return err;

    /* Apply non-conflicting changes from source branch */
    struct kgeofs_ref_entry *entry = vol->ref_index;
    while (entry) {
        /* Is this ref on the source branch's lineage, after ancestor? */
        if (view_in_chain(chain_theirs, len_theirs, entry->view_id) &&
            entry->view_id != ancestor &&
            !view_in_chain(chain_ours, len_ours, entry->view_id) &&
            !entry->is_hidden) {

            /* Check if same path was also modified on our branch */
            int conflict = 0;
            struct kgeofs_ref_entry *check = vol->ref_index;
            while (check) {
                if (view_in_chain(chain_ours, len_ours, check->view_id) &&
                    check->view_id != ancestor &&
                    strcmp(check->path, entry->path) == 0 &&
                    !check->is_hidden) {
                    /* Same path modified on both branches */
                    if (!kgeofs_hash_equal(check->content_hash, entry->content_hash)) {
                        conflict = 1;
                        (*conflict_count)++;
                        kprintf("[GeoFS] CONFLICT: %s (different content on both branches)\n",
                                entry->path);
                    }
                    break;
                }
                check = check->next;
            }

            if (!conflict) {
                /* Apply: create ref in current view pointing to their content */
                kgeofs_ref_create(vol, entry->path, entry->content_hash);
            }
        }
        entry = entry->next;
    }

    if (*conflict_count > 0) {
        kprintf("[GeoFS] Merge completed with %d conflict(s)\n", *conflict_count);
        return KGEOFS_ERR_CONFLICT;
    }

    kprintf("[GeoFS] Merge '%s' complete (no conflicts)\n", source_name);
    return KGEOFS_OK;
}

/*============================================================================
 * Quota Management
 *============================================================================*/

kgeofs_error_t kgeofs_quota_set(kgeofs_volume_t *vol,
                                 kgeofs_branch_t branch_id,
                                 const struct kgeofs_quota *limits)
{
    if (!vol || !limits) return KGEOFS_ERR_INVALID;

    /* Write quota record to view_region (append-only) */
    size_t rec_size = sizeof(struct kgeofs_quota_record);
    struct kgeofs_ram_region *region = region_find_or_grow(vol->view_region, rec_size);
    if (!region) return KGEOFS_ERR_FULL;

    struct kgeofs_quota_record *rec =
        (struct kgeofs_quota_record *)((uint8_t *)region->base + region->used);
    rec->magic = KGEOFS_QUOTA_MAGIC;
    rec->flags = 0;
    rec->branch_id = branch_id;
    rec->limits = *limits;
    rec->created = kgeofs_time_now();
    region->used += rec_size;

    /* Update in-memory index (last-writer-wins) */
    struct kgeofs_quota_entry *existing = NULL;
    struct kgeofs_quota_entry *qe = vol->quota_index;
    while (qe) {
        if (qe->branch_id == branch_id) { existing = qe; break; }
        qe = qe->next;
    }
    if (existing) {
        existing->limits = *limits;
    } else {
        struct kgeofs_quota_entry *entry = kmalloc(sizeof(*entry));
        if (!entry) return KGEOFS_ERR_NOMEM;
        entry->branch_id = branch_id;
        entry->limits = *limits;
        entry->next = vol->quota_index;
        vol->quota_index = entry;
    }

    return KGEOFS_OK;
}

kgeofs_error_t kgeofs_quota_get(kgeofs_volume_t *vol,
                                 kgeofs_branch_t branch_id,
                                 struct kgeofs_quota *limits_out)
{
    if (!vol || !limits_out) return KGEOFS_ERR_INVALID;

    struct kgeofs_quota_entry *qe = vol->quota_index;
    while (qe) {
        if (qe->branch_id == branch_id) {
            *limits_out = qe->limits;
            return KGEOFS_OK;
        }
        qe = qe->next;
    }

    /* No quota set — return unlimited */
    memset(limits_out, 0, sizeof(*limits_out));
    return KGEOFS_OK;
}

kgeofs_error_t kgeofs_quota_usage(kgeofs_volume_t *vol,
                                   kgeofs_branch_t branch_id,
                                   uint64_t *content_bytes_out,
                                   uint64_t *ref_count_out,
                                   uint64_t *view_count_out)
{
    if (!vol) return KGEOFS_ERR_INVALID;

    uint64_t bytes = 0, refs = 0, views = 0;

    if (branch_id == KGEOFS_QUOTA_VOLUME) {
        /* Volume-wide usage */
        bytes = vol->total_content_bytes;
        refs = vol->total_refs;
        views = vol->total_views;
    } else {
        /* Per-branch: count refs and views on this branch */
        struct kgeofs_ref_entry *re = vol->ref_index;
        while (re) {
            /* Find which branch this ref's view belongs to */
            struct kgeofs_view_entry *ve = vol->view_index;
            while (ve) {
                if (ve->id == re->view_id && ve->branch_id == branch_id) {
                    refs++;
                    break;
                }
                ve = ve->next;
            }
            re = re->next;
        }
        struct kgeofs_view_entry *ve = vol->view_index;
        while (ve) {
            if (ve->branch_id == branch_id) views++;
            ve = ve->next;
        }
        bytes = vol->total_content_bytes; /* Content is shared across branches */
    }

    if (content_bytes_out) *content_bytes_out = bytes;
    if (ref_count_out) *ref_count_out = refs;
    if (view_count_out) *view_count_out = views;
    return KGEOFS_OK;
}
