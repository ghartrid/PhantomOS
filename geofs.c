/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                                 PHANTOM GeoFS
 *                        Geology FileSystem - Single File
 *
 *                          "To Create, Not To Destroy"
 *
 *    An append-only filesystem where nothing is ever deleted.
 *    This is the foundational storage layer for Phantom OS.
 *
 *    Build (standalone): gcc -Wall -O2 -DGEOFS_STANDALONE geofs.c -o geofs -lpthread
 *    Build (library):    gcc -Wall -O2 -c geofs.c -o geofs.o
 *    Usage:              ./geofs help
 *
 * ══════════════════════════════════════════════════════════════════════════════
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#include "geofs.h"

/* ══════════════════════════════════════════════════════════════════════════════
 * INTERNAL STRUCTURES
 * ══════════════════════════════════════════════════════════════════════════════ */

struct geofs_superblock {
    uint64_t    magic;
    uint16_t    version;
    uint16_t    flags;
    uint32_t    block_size;
    uint64_t    volume_id;
    geofs_time_t created;
    geofs_time_t last_modified;
    
    uint64_t    content_region_start;
    uint64_t    content_region_blocks;
    uint64_t    content_next_block;
    
    uint64_t    ref_region_start;
    uint64_t    ref_region_blocks;
    uint64_t    ref_next_id;
    
    uint64_t    view_region_start;
    uint64_t    view_region_blocks;
    uint64_t    view_next_id;
    geofs_view_t current_view;
    
    uint64_t    total_content_bytes;
    uint64_t    total_refs;
    uint64_t    total_views;
    
    uint8_t     reserved[424];
};

struct content_index_entry {
    geofs_hash_t    hash;
    uint64_t        offset;
    uint64_t        size;
    struct content_index_entry *next;
};

struct ref_index_entry {
    geofs_hash_t    path_hash;
    geofs_hash_t    content_hash;
    geofs_view_t    view_id;
    geofs_time_t    created;
    char            path[GEOFS_MAX_PATH];
    int             is_hidden;
    struct ref_index_entry *next;
};

struct view_index_entry {
    geofs_view_t    id;
    geofs_view_t    parent_id;
    geofs_time_t    created;
    char            label[64];
    struct view_index_entry *next;
};

/* On-disk ref record (fixed size for easy scanning) */
#define GEOFS_REF_RECORD_MAGIC  0x46455247UL  /* "GREF" */
#define GEOFS_REF_RECORD_SIZE   4224          /* Fits nicely in one block with padding */

struct geofs_ref_record {
    uint32_t        magic;
    uint32_t        flags;              /* bit 0 = is_hidden */
    geofs_hash_t    path_hash;
    geofs_hash_t    content_hash;
    geofs_view_t    view_id;
    geofs_time_t    created;
    uint16_t        path_len;
    char            path[GEOFS_MAX_PATH];
    uint8_t         reserved[58];       /* Pad to GEOFS_REF_RECORD_SIZE */
};

/* On-disk view record (geological strata - each view is a layer) */
#define GEOFS_VIEW_RECORD_MAGIC 0x57454956UL  /* "VIEW" */
#define GEOFS_VIEW_RECORD_SIZE  128

struct geofs_view_record {
    uint32_t        magic;
    uint32_t        flags;
    geofs_view_t    id;
    geofs_view_t    parent_id;
    geofs_time_t    created;
    char            label[64];
    uint8_t         reserved[24];       /* Pad to GEOFS_VIEW_RECORD_SIZE */
};

/* Full definition of opaque geofs_volume type */
struct geofs_volume {
    int             fd;
    char            path[GEOFS_MAX_PATH];
    struct geofs_superblock sb;
    struct content_index_entry *content_index;
    struct ref_index_entry *ref_index;
    struct view_index_entry *view_index;
    geofs_view_t    current_view;
    pthread_mutex_t lock;
    int             dirty;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * SHA-256 IMPLEMENTATION (standalone, no OpenSSL needed)
 * ══════════════════════════════════════════════════════════════════════════════ */

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

static void sha256_transform(uint32_t state[8], const uint8_t data[64]) {
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

static void sha256(const void *data, size_t len, uint8_t hash[32]) {
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

/* ══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static geofs_time_t geofs_time_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void geofs_time_format(geofs_time_t t, char *buf, size_t len) {
    time_t secs = t / 1000000000ULL;
    struct tm *tm = localtime(&secs);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", tm);
}

void geofs_hash_to_string(const geofs_hash_t hash, char *buf) {
    for (int i = 0; i < GEOFS_HASH_SIZE; i++) {
        sprintf(buf + (i * 2), "%02x", hash[i]);
    }
    buf[64] = '\0';
}

static int hash_equal(const geofs_hash_t a, const geofs_hash_t b) {
    return memcmp(a, b, GEOFS_HASH_SIZE) == 0;
}

static void hash_path(const char *path, geofs_hash_t hash) {
    sha256(path, strlen(path), hash);
}

const char *geofs_strerror(geofs_error_t err) {
    switch (err) {
        case GEOFS_OK:          return "Success";
        case GEOFS_ERR_IO:      return "I/O error";
        case GEOFS_ERR_NOMEM:   return "Out of memory";
        case GEOFS_ERR_NOTFOUND: return "Not found";
        case GEOFS_ERR_EXISTS:  return "Already exists";
        case GEOFS_ERR_INVALID: return "Invalid argument";
        case GEOFS_ERR_CORRUPT: return "Data corruption";
        case GEOFS_ERR_FULL:    return "Volume full";
        default:                return "Unknown error";
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * INDEX FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static struct content_index_entry *find_content(geofs_volume_t *vol,
                                                 const geofs_hash_t hash) {
    struct content_index_entry *entry = vol->content_index;
    while (entry) {
        if (hash_equal(entry->hash, hash)) return entry;
        entry = entry->next;
    }
    return NULL;
}

static struct ref_index_entry *find_ref(geofs_volume_t *vol, const char *path) {
    geofs_hash_t path_hash;
    hash_path(path, path_hash);

    struct ref_index_entry *best = NULL;
    geofs_time_t best_time = 0;

    struct ref_index_entry *entry = vol->ref_index;
    while (entry) {
        if (hash_equal(entry->path_hash, path_hash)) {
            if (entry->view_id <= vol->current_view && entry->created > best_time) {
                best = entry;
                best_time = entry->created;
            }
        }
        entry = entry->next;
    }
    return best;
}

/* Write a ref record to the ref region on disk */
static geofs_error_t geofs_ref_write_record(geofs_volume_t *vol,
                                             const struct ref_index_entry *entry) {
    /* Calculate offset in ref region based on ref_next_id */
    uint64_t ref_offset = vol->sb.ref_region_start * GEOFS_BLOCK_SIZE +
                          (vol->sb.ref_next_id - 1) * GEOFS_REF_RECORD_SIZE;

    /* Check if we have space in the ref region */
    uint64_t ref_region_end = (vol->sb.ref_region_start + vol->sb.ref_region_blocks) * GEOFS_BLOCK_SIZE;
    if (ref_offset + GEOFS_REF_RECORD_SIZE > ref_region_end) {
        return GEOFS_ERR_FULL;
    }

    /* Build the on-disk record */
    struct geofs_ref_record record = {0};
    record.magic = GEOFS_REF_RECORD_MAGIC;
    record.flags = entry->is_hidden ? 1 : 0;
    memcpy(record.path_hash, entry->path_hash, GEOFS_HASH_SIZE);
    memcpy(record.content_hash, entry->content_hash, GEOFS_HASH_SIZE);
    record.view_id = entry->view_id;
    record.created = entry->created;
    record.path_len = (uint16_t)strlen(entry->path);
    strncpy(record.path, entry->path, GEOFS_MAX_PATH - 1);

    /* Write to disk */
    if (lseek(vol->fd, ref_offset, SEEK_SET) != (off_t)ref_offset) {
        return GEOFS_ERR_IO;
    }
    if (write(vol->fd, &record, sizeof(record)) != sizeof(record)) {
        return GEOFS_ERR_IO;
    }

    vol->sb.ref_next_id++;
    return GEOFS_OK;
}

/* Rebuild the content index by scanning the content region on disk */
static geofs_error_t geofs_content_rebuild(geofs_volume_t *vol) {
    uint64_t offset = vol->sb.content_region_start * GEOFS_BLOCK_SIZE;
    uint64_t end_offset = vol->sb.content_next_block * GEOFS_BLOCK_SIZE;

    while (offset < end_offset) {
        uint8_t header[GEOFS_BLOCK_SIZE];
        if (lseek(vol->fd, offset, SEEK_SET) != (off_t)offset) {
            return GEOFS_ERR_IO;
        }
        if (read(vol->fd, header, GEOFS_BLOCK_SIZE) != GEOFS_BLOCK_SIZE) {
            return GEOFS_ERR_IO;
        }

        /* Validate content header magic */
        if (memcmp(header, "CONT", 4) != 0) {
            break;  /* End of valid content */
        }

        uint64_t size;
        memcpy(&size, header + 8, 8);

        geofs_hash_t hash;
        memcpy(hash, header + 16, GEOFS_HASH_SIZE);

        /* Create in-memory index entry */
        struct content_index_entry *entry = calloc(1, sizeof(struct content_index_entry));
        if (!entry) {
            return GEOFS_ERR_NOMEM;
        }

        memcpy(entry->hash, hash, GEOFS_HASH_SIZE);
        entry->offset = offset;
        entry->size = size;

        /* Add to index (prepend to list) */
        entry->next = vol->content_index;
        vol->content_index = entry;

        /* Move to next content block */
        uint64_t data_blocks = (size + GEOFS_BLOCK_SIZE - 1) / GEOFS_BLOCK_SIZE;
        offset += (1 + data_blocks) * GEOFS_BLOCK_SIZE;
    }

    return GEOFS_OK;
}

/* Rebuild the ref index by scanning the ref region on disk */
static geofs_error_t geofs_refs_rebuild(geofs_volume_t *vol) {
    uint64_t ref_region_start = vol->sb.ref_region_start * GEOFS_BLOCK_SIZE;
    uint64_t num_refs = vol->sb.ref_next_id - 1;  /* ref_next_id starts at 1 */

    for (uint64_t i = 0; i < num_refs; i++) {
        uint64_t offset = ref_region_start + i * GEOFS_REF_RECORD_SIZE;

        struct geofs_ref_record record;
        if (lseek(vol->fd, offset, SEEK_SET) != (off_t)offset) {
            return GEOFS_ERR_IO;
        }
        if (read(vol->fd, &record, sizeof(record)) != sizeof(record)) {
            return GEOFS_ERR_IO;
        }

        /* Validate magic */
        if (record.magic != GEOFS_REF_RECORD_MAGIC) {
            continue;  /* Skip invalid/corrupted records */
        }

        /* Create in-memory index entry */
        struct ref_index_entry *entry = calloc(1, sizeof(struct ref_index_entry));
        if (!entry) {
            return GEOFS_ERR_NOMEM;
        }

        memcpy(entry->path_hash, record.path_hash, GEOFS_HASH_SIZE);
        memcpy(entry->content_hash, record.content_hash, GEOFS_HASH_SIZE);
        entry->view_id = record.view_id;
        entry->created = record.created;
        strncpy(entry->path, record.path, GEOFS_MAX_PATH - 1);
        entry->is_hidden = (record.flags & 1) ? 1 : 0;

        /* Add to index (prepend to list) */
        entry->next = vol->ref_index;
        vol->ref_index = entry;
    }

    return GEOFS_OK;
}

/* Write a view record to the view region on disk */
static geofs_error_t geofs_view_write_record(geofs_volume_t *vol,
                                              const struct view_index_entry *entry) {
    /* Calculate offset in view region based on view id */
    uint64_t view_offset = vol->sb.view_region_start * GEOFS_BLOCK_SIZE +
                           (entry->id - 1) * GEOFS_VIEW_RECORD_SIZE;

    /* Check if we have space in the view region */
    uint64_t view_region_end = (vol->sb.view_region_start + vol->sb.view_region_blocks) * GEOFS_BLOCK_SIZE;
    if (view_offset + GEOFS_VIEW_RECORD_SIZE > view_region_end) {
        return GEOFS_ERR_FULL;
    }

    /* Build the on-disk record */
    struct geofs_view_record record = {0};
    record.magic = GEOFS_VIEW_RECORD_MAGIC;
    record.flags = 0;
    record.id = entry->id;
    record.parent_id = entry->parent_id;
    record.created = entry->created;
    strncpy(record.label, entry->label, 63);

    /* Write to disk */
    if (lseek(vol->fd, view_offset, SEEK_SET) != (off_t)view_offset) {
        return GEOFS_ERR_IO;
    }
    if (write(vol->fd, &record, sizeof(record)) != sizeof(record)) {
        return GEOFS_ERR_IO;
    }

    return GEOFS_OK;
}

/* Rebuild the view index by scanning the view region on disk */
static geofs_error_t geofs_views_rebuild(geofs_volume_t *vol) {
    uint64_t view_region_start = vol->sb.view_region_start * GEOFS_BLOCK_SIZE;
    uint64_t num_views = vol->sb.view_next_id - 1;  /* view_next_id starts at 1 */

    for (uint64_t i = 0; i < num_views; i++) {
        uint64_t offset = view_region_start + i * GEOFS_VIEW_RECORD_SIZE;

        struct geofs_view_record record;
        if (lseek(vol->fd, offset, SEEK_SET) != (off_t)offset) {
            return GEOFS_ERR_IO;
        }
        if (read(vol->fd, &record, sizeof(record)) != sizeof(record)) {
            return GEOFS_ERR_IO;
        }

        /* Validate magic */
        if (record.magic != GEOFS_VIEW_RECORD_MAGIC) {
            continue;  /* Skip invalid/corrupted records */
        }

        /* Create in-memory index entry */
        struct view_index_entry *entry = calloc(1, sizeof(struct view_index_entry));
        if (!entry) {
            return GEOFS_ERR_NOMEM;
        }

        entry->id = record.id;
        entry->parent_id = record.parent_id;
        entry->created = record.created;
        strncpy(entry->label, record.label, 63);

        /* Add to index (prepend to list) */
        entry->next = vol->view_index;
        vol->view_index = entry;
    }

    return GEOFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * VOLUME OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static geofs_error_t write_superblock(geofs_volume_t *vol) {
    vol->sb.last_modified = geofs_time_now();
    if (lseek(vol->fd, 0, SEEK_SET) != 0) return GEOFS_ERR_IO;
    if (write(vol->fd, &vol->sb, sizeof(vol->sb)) != sizeof(vol->sb)) return GEOFS_ERR_IO;
    return GEOFS_OK;
}

static geofs_error_t read_superblock(geofs_volume_t *vol) {
    if (lseek(vol->fd, 0, SEEK_SET) != 0) return GEOFS_ERR_IO;
    if (read(vol->fd, &vol->sb, sizeof(vol->sb)) != sizeof(vol->sb)) return GEOFS_ERR_IO;
    if (vol->sb.magic != GEOFS_MAGIC) return GEOFS_ERR_CORRUPT;
    return GEOFS_OK;
}

geofs_error_t geofs_volume_create(const char *path, uint64_t size_mb,
                                   geofs_volume_t **vol_out) {
    geofs_volume_t *vol = calloc(1, sizeof(geofs_volume_t));
    if (!vol) return GEOFS_ERR_NOMEM;
    
    strncpy(vol->path, path, GEOFS_MAX_PATH - 1);
    pthread_mutex_init(&vol->lock, NULL);
    
    vol->fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (vol->fd < 0) {
        free(vol);
        return GEOFS_ERR_IO;
    }
    
    uint64_t total_blocks = (size_mb * 1024 * 1024) / GEOFS_BLOCK_SIZE;
    uint64_t content_blocks = total_blocks * 70 / 100;
    uint64_t ref_blocks = total_blocks * 20 / 100;
    uint64_t view_blocks = total_blocks * 10 / 100;
    
    vol->sb.magic = GEOFS_MAGIC;
    vol->sb.version = GEOFS_VERSION;
    vol->sb.block_size = GEOFS_BLOCK_SIZE;
    vol->sb.volume_id = geofs_time_now();
    vol->sb.created = geofs_time_now();
    vol->sb.last_modified = vol->sb.created;
    
    vol->sb.content_region_start = 1;
    vol->sb.content_region_blocks = content_blocks;
    vol->sb.content_next_block = 1;
    
    vol->sb.ref_region_start = 1 + content_blocks;
    vol->sb.ref_region_blocks = ref_blocks;
    vol->sb.ref_next_id = 1;
    
    vol->sb.view_region_start = 1 + content_blocks + ref_blocks;
    vol->sb.view_region_blocks = view_blocks;
    vol->sb.view_next_id = 1;
    vol->sb.current_view = 1;
    
    if (ftruncate(vol->fd, total_blocks * GEOFS_BLOCK_SIZE) != 0) {
        close(vol->fd);
        unlink(path);
        free(vol);
        return GEOFS_ERR_IO;
    }
    
    write_superblock(vol);

    /* Create root view (Genesis - the first geological stratum) */
    struct view_index_entry *root = calloc(1, sizeof(struct view_index_entry));
    root->id = 1;
    root->parent_id = 0;
    root->created = geofs_time_now();
    strcpy(root->label, "Genesis");

    /* Persist Genesis view to disk */
    vol->sb.view_next_id = 2;  /* Next view will be 2 */
    geofs_view_write_record(vol, root);

    vol->view_index = root;
    vol->current_view = 1;
    vol->sb.total_views = 1;

    write_superblock(vol);
    
    *vol_out = vol;
    return GEOFS_OK;
}

geofs_error_t geofs_volume_open(const char *path, geofs_volume_t **vol_out) {
    geofs_volume_t *vol = calloc(1, sizeof(geofs_volume_t));
    if (!vol) return GEOFS_ERR_NOMEM;

    strncpy(vol->path, path, GEOFS_MAX_PATH - 1);
    pthread_mutex_init(&vol->lock, NULL);

    vol->fd = open(path, O_RDWR);
    if (vol->fd < 0) {
        free(vol);
        return GEOFS_ERR_IO;
    }

    geofs_error_t err = read_superblock(vol);
    if (err != GEOFS_OK) {
        close(vol->fd);
        free(vol);
        return err;
    }

    vol->current_view = vol->sb.current_view;

    /* Rebuild content index from disk */
    err = geofs_content_rebuild(vol);
    if (err != GEOFS_OK) {
        close(vol->fd);
        free(vol);
        return err;
    }

    /* Rebuild ref index from disk */
    err = geofs_refs_rebuild(vol);
    if (err != GEOFS_OK) {
        close(vol->fd);
        free(vol);
        return err;
    }

    /* Rebuild view index from disk (geological strata) */
    err = geofs_views_rebuild(vol);
    if (err != GEOFS_OK) {
        close(vol->fd);
        free(vol);
        return err;
    }

    /* Create default view if index is empty (shouldn't happen with proper persistence) */
    if (!vol->view_index) {
        struct view_index_entry *root = calloc(1, sizeof(struct view_index_entry));
        root->id = vol->current_view;
        root->parent_id = 0;
        root->created = vol->sb.created;
        strcpy(root->label, "Genesis");
        vol->view_index = root;
    }

    *vol_out = vol;
    return GEOFS_OK;
}

void geofs_volume_close(geofs_volume_t *vol) {
    if (!vol) return;
    
    pthread_mutex_lock(&vol->lock);
    if (vol->dirty) {
        write_superblock(vol);
        fsync(vol->fd);
    }
    close(vol->fd);
    
    /* Free indices */
    struct content_index_entry *ce = vol->content_index;
    while (ce) {
        struct content_index_entry *next = ce->next;
        free(ce);
        ce = next;
    }
    
    struct ref_index_entry *re = vol->ref_index;
    while (re) {
        struct ref_index_entry *next = re->next;
        free(re);
        re = next;
    }
    
    struct view_index_entry *ve = vol->view_index;
    while (ve) {
        struct view_index_entry *next = ve->next;
        free(ve);
        ve = next;
    }
    
    pthread_mutex_unlock(&vol->lock);
    pthread_mutex_destroy(&vol->lock);
    free(vol);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CONTENT OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

geofs_error_t geofs_content_store(geofs_volume_t *vol, const void *data,
                                   size_t size, geofs_hash_t hash_out) {
    geofs_hash_t hash;
    sha256(data, size, hash);
    
    pthread_mutex_lock(&vol->lock);
    
    /* Deduplication check */
    if (find_content(vol, hash)) {
        memcpy(hash_out, hash, GEOFS_HASH_SIZE);
        pthread_mutex_unlock(&vol->lock);
        return GEOFS_OK;
    }
    
    uint64_t data_blocks = (size + GEOFS_BLOCK_SIZE - 1) / GEOFS_BLOCK_SIZE;
    uint64_t total_blocks = 1 + data_blocks;
    
    uint64_t available = vol->sb.content_region_start + 
                         vol->sb.content_region_blocks - 
                         vol->sb.content_next_block;
    if (total_blocks > available) {
        pthread_mutex_unlock(&vol->lock);
        return GEOFS_ERR_FULL;
    }
    
    /* Write content header */
    uint8_t header[GEOFS_BLOCK_SIZE] = {0};
    memcpy(header, "CONT", 4);
    memcpy(header + 8, &size, 8);
    memcpy(header + 16, hash, 32);
    
    uint64_t offset = vol->sb.content_next_block * GEOFS_BLOCK_SIZE;
    lseek(vol->fd, offset, SEEK_SET);
    write(vol->fd, header, GEOFS_BLOCK_SIZE);
    
    if (size > 0) {
        write(vol->fd, data, size);
    }
    
    /* Add to index */
    struct content_index_entry *entry = calloc(1, sizeof(struct content_index_entry));
    if (entry) {
        memcpy(entry->hash, hash, GEOFS_HASH_SIZE);
        entry->offset = offset;
        entry->size = size;
        entry->next = vol->content_index;
        vol->content_index = entry;
    }
    
    vol->sb.content_next_block += total_blocks;
    vol->sb.total_content_bytes += size;
    vol->dirty = 1;
    
    memcpy(hash_out, hash, GEOFS_HASH_SIZE);
    pthread_mutex_unlock(&vol->lock);
    return GEOFS_OK;
}

geofs_error_t geofs_content_read(geofs_volume_t *vol, const geofs_hash_t hash,
                                  void *buf, size_t buf_size, size_t *size_out) {
    pthread_mutex_lock(&vol->lock);
    
    struct content_index_entry *entry = find_content(vol, hash);
    if (!entry) {
        pthread_mutex_unlock(&vol->lock);
        return GEOFS_ERR_NOTFOUND;
    }
    
    size_t to_read = entry->size;
    if (to_read > buf_size) to_read = buf_size;
    
    lseek(vol->fd, entry->offset + GEOFS_BLOCK_SIZE, SEEK_SET);
    ssize_t got = read(vol->fd, buf, to_read);
    
    if (got < 0) {
        pthread_mutex_unlock(&vol->lock);
        return GEOFS_ERR_IO;
    }
    
    if (size_out) *size_out = (size_t)got;
    pthread_mutex_unlock(&vol->lock);
    return GEOFS_OK;
}

geofs_error_t geofs_content_size(geofs_volume_t *vol, const geofs_hash_t hash,
                                  uint64_t *size_out) {
    pthread_mutex_lock(&vol->lock);
    struct content_index_entry *entry = find_content(vol, hash);
    if (!entry) {
        pthread_mutex_unlock(&vol->lock);
        return GEOFS_ERR_NOTFOUND;
    }
    *size_out = entry->size;
    pthread_mutex_unlock(&vol->lock);
    return GEOFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * REFERENCE OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

geofs_error_t geofs_ref_create(geofs_volume_t *vol, const char *path,
                                const geofs_hash_t content_hash) {
    pthread_mutex_lock(&vol->lock);

    struct ref_index_entry *entry = calloc(1, sizeof(struct ref_index_entry));
    if (!entry) {
        pthread_mutex_unlock(&vol->lock);
        return GEOFS_ERR_NOMEM;
    }

    hash_path(path, entry->path_hash);
    memcpy(entry->content_hash, content_hash, GEOFS_HASH_SIZE);
    entry->view_id = vol->current_view;
    entry->created = geofs_time_now();
    strncpy(entry->path, path, GEOFS_MAX_PATH - 1);
    entry->is_hidden = 0;

    /* Persist ref to disk */
    geofs_error_t err = geofs_ref_write_record(vol, entry);
    if (err != GEOFS_OK) {
        free(entry);
        pthread_mutex_unlock(&vol->lock);
        return err;
    }

    entry->next = vol->ref_index;
    vol->ref_index = entry;

    vol->sb.total_refs++;
    vol->dirty = 1;

    pthread_mutex_unlock(&vol->lock);
    return GEOFS_OK;
}

geofs_error_t geofs_ref_resolve(geofs_volume_t *vol, const char *path,
                                 geofs_hash_t hash_out) {
    pthread_mutex_lock(&vol->lock);
    
    struct ref_index_entry *entry = find_ref(vol, path);
    if (!entry || entry->is_hidden) {
        pthread_mutex_unlock(&vol->lock);
        return GEOFS_ERR_NOTFOUND;
    }
    
    memcpy(hash_out, entry->content_hash, GEOFS_HASH_SIZE);
    pthread_mutex_unlock(&vol->lock);
    return GEOFS_OK;
}

int geofs_ref_list(geofs_volume_t *vol, const char *dir_path,
                   geofs_dir_callback callback, void *ctx) {
    pthread_mutex_lock(&vol->lock);

    size_t dir_len = strlen(dir_path);
    int count = 0;
    char seen[256][GEOFS_MAX_NAME];
    int seen_count = 0;

    struct ref_index_entry *entry = vol->ref_index;
    while (entry) {
        if (entry->view_id <= vol->current_view) {
            const char *entry_path = entry->path;
            int in_dir = 0;

            if (strcmp(dir_path, "/") == 0) {
                if (entry_path[0] == '/' && strchr(entry_path + 1, '/') == NULL) {
                    in_dir = 1;
                }
            } else if (strncmp(entry_path, dir_path, dir_len) == 0 &&
                       entry_path[dir_len] == '/') {
                if (strchr(entry_path + dir_len + 1, '/') == NULL) {
                    in_dir = 1;
                }
            }

            if (in_dir) {
                const char *name = strrchr(entry_path, '/');
                if (name) name++;
                else name = entry_path;

                int already_seen = 0;
                for (int i = 0; i < seen_count && i < 256; i++) {
                    if (strcmp(seen[i], name) == 0) {
                        already_seen = 1;
                        break;
                    }
                }

                if (!already_seen && seen_count < 256) {
                    strncpy(seen[seen_count], name, GEOFS_MAX_NAME - 1);
                    seen_count++;

                    /* Check if this path is currently visible (not hidden) */
                    struct ref_index_entry *current = find_ref(vol, entry_path);
                    if (current && !current->is_hidden) {
                        struct content_index_entry *content = find_content(vol, current->content_hash);

                        struct geofs_dirent dirent = {0};
                        strncpy(dirent.name, name, GEOFS_MAX_NAME);
                        memcpy(dirent.content_hash, current->content_hash, GEOFS_HASH_SIZE);
                        dirent.size = content ? content->size : 0;
                        dirent.created = current->created;

                        callback(&dirent, ctx);
                        count++;
                    }
                }
            }
        }
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&vol->lock);
    return count;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * VIEW OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

geofs_error_t geofs_view_create(geofs_volume_t *vol, const char *label,
                                 geofs_view_t *view_out) {
    pthread_mutex_lock(&vol->lock);

    struct view_index_entry *view = calloc(1, sizeof(struct view_index_entry));
    if (!view) {
        pthread_mutex_unlock(&vol->lock);
        return GEOFS_ERR_NOMEM;
    }

    view->id = vol->sb.view_next_id++;
    view->parent_id = vol->current_view;
    view->created = geofs_time_now();
    if (label) strncpy(view->label, label, 63);

    /* Persist view to disk (geological strata are permanent) */
    geofs_error_t err = geofs_view_write_record(vol, view);
    if (err != GEOFS_OK) {
        vol->sb.view_next_id--;  /* Rollback */
        free(view);
        pthread_mutex_unlock(&vol->lock);
        return err;
    }

    view->next = vol->view_index;
    vol->view_index = view;

    vol->sb.total_views++;
    vol->dirty = 1;

    *view_out = view->id;
    pthread_mutex_unlock(&vol->lock);
    return GEOFS_OK;
}

geofs_error_t geofs_view_switch(geofs_volume_t *vol, geofs_view_t view_id) {
    pthread_mutex_lock(&vol->lock);
    
    int found = 0;
    struct view_index_entry *view = vol->view_index;
    while (view) {
        if (view->id == view_id) {
            found = 1;
            break;
        }
        view = view->next;
    }
    
    if (!found) {
        pthread_mutex_unlock(&vol->lock);
        return GEOFS_ERR_NOTFOUND;
    }
    
    vol->current_view = view_id;
    vol->sb.current_view = view_id;
    vol->dirty = 1;
    
    pthread_mutex_unlock(&vol->lock);
    return GEOFS_OK;
}

geofs_view_t geofs_view_current(geofs_volume_t *vol) {
    return vol->current_view;
}

int geofs_view_list(geofs_volume_t *vol, geofs_view_callback callback, void *ctx) {
    pthread_mutex_lock(&vol->lock);
    
    int count = 0;
    struct view_index_entry *view = vol->view_index;
    while (view) {
        struct geofs_view_info info = {0};
        info.id = view->id;
        info.parent_id = view->parent_id;
        info.created = view->created;
        strncpy(info.label, view->label, 63);
        
        callback(&info, ctx);
        count++;
        view = view->next;
    }
    
    pthread_mutex_unlock(&vol->lock);
    return count;
}

geofs_error_t geofs_view_hide(geofs_volume_t *vol, const char *path) {
    pthread_mutex_lock(&vol->lock);

    struct ref_index_entry *existing = find_ref(vol, path);
    if (!existing) {
        pthread_mutex_unlock(&vol->lock);
        return GEOFS_ERR_NOTFOUND;
    }

    pthread_mutex_unlock(&vol->lock);

    /* Create new view */
    geofs_view_t new_view = 0;
    char label[64];
    snprintf(label, sizeof(label), "Hide: %.50s", path);
    geofs_error_t view_err = geofs_view_create(vol, label, &new_view);
    if (view_err == GEOFS_OK && new_view != 0) {
        geofs_view_switch(vol, new_view);
    }

    pthread_mutex_lock(&vol->lock);

    /* Create hidden marker */
    struct ref_index_entry *hidden = calloc(1, sizeof(struct ref_index_entry));
    if (!hidden) {
        pthread_mutex_unlock(&vol->lock);
        return GEOFS_ERR_NOMEM;
    }

    hash_path(path, hidden->path_hash);
    memset(hidden->content_hash, 0, GEOFS_HASH_SIZE);
    hidden->view_id = new_view;
    hidden->created = geofs_time_now();
    strncpy(hidden->path, path, GEOFS_MAX_PATH - 1);
    hidden->is_hidden = 1;

    /* Persist hidden ref to disk */
    geofs_error_t err = geofs_ref_write_record(vol, hidden);
    if (err != GEOFS_OK) {
        free(hidden);
        pthread_mutex_unlock(&vol->lock);
        return err;
    }

    hidden->next = vol->ref_index;
    vol->ref_index = hidden;
    vol->dirty = 1;

    pthread_mutex_unlock(&vol->lock);
    return GEOFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FILE HISTORY OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

int geofs_ref_history(geofs_volume_t *vol, geofs_history_callback callback, void *ctx) {
    if (!vol || !callback) return 0;

    pthread_mutex_lock(&vol->lock);

    int count = 0;
    struct ref_index_entry *entry = vol->ref_index;

    while (entry) {
        struct geofs_history_entry info = {0};
        strncpy(info.path, entry->path, GEOFS_MAX_PATH - 1);
        memcpy(info.content_hash, entry->content_hash, GEOFS_HASH_SIZE);
        info.view_id = entry->view_id;
        info.created = entry->created;
        info.is_hidden = entry->is_hidden;

        /* Get content size if not hidden */
        if (!entry->is_hidden) {
            struct content_index_entry *content = find_content(vol, entry->content_hash);
            if (content) {
                info.size = content->size;
            }
        }

        callback(&info, ctx);
        count++;
        entry = entry->next;
    }

    pthread_mutex_unlock(&vol->lock);
    return count;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CLI COMMANDS (only compiled in standalone mode)
 * ══════════════════════════════════════════════════════════════════════════════ */

#ifdef GEOFS_STANDALONE

static int cmd_create(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: geofs create <volume> <size_mb>\n");
        return 1;
    }
    
    uint64_t size_mb = strtoull(argv[3], NULL, 10);
    if (size_mb < 1) {
        fprintf(stderr, "Error: Size must be at least 1 MB\n");
        return 1;
    }
    
    geofs_volume_t *vol;
    geofs_error_t err = geofs_volume_create(argv[2], size_mb, &vol);
    
    if (err != GEOFS_OK) {
        fprintf(stderr, "Error: %s\n", geofs_strerror(err));
        return 1;
    }
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║              PHANTOM GeoFS Volume Created             ║\n");
    printf("║                                                       ║\n");
    printf("║  Nothing stored here will ever be truly deleted.      ║\n");
    printf("║  The geology remembers everything.                    ║\n");
    printf("║                                                       ║\n");
    printf("║              \"To Create, Not To Destroy\"              ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Volume: %s\n", argv[2]);
    printf("  Size:   %lu MB\n", size_mb);
    printf("\n");
    
    geofs_volume_close(vol);
    return 0;
}

static void ls_callback(const struct geofs_dirent *entry, void *ctx) {
    (void)ctx;
    char hash_str[65];
    geofs_hash_to_string(entry->content_hash, hash_str);
    
    char time_str[32];
    geofs_time_format(entry->created, time_str, sizeof(time_str));
    
    printf("  %s  %8lu  %.16s...  %s\n",
           time_str, entry->size, hash_str, entry->name);
}

static int cmd_ls(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: geofs ls <volume> [path]\n");
        return 1;
    }
    
    const char *dir_path = argc > 3 ? argv[3] : "/";
    
    geofs_volume_t *vol;
    geofs_error_t err = geofs_volume_open(argv[2], &vol);
    if (err != GEOFS_OK) {
        fprintf(stderr, "Error: %s\n", geofs_strerror(err));
        return 1;
    }
    
    printf("\n  View: %lu | Path: %s\n", geofs_view_current(vol), dir_path);
    printf("  ────────────────────────────────────────────────────────────────\n");
    
    int count = geofs_ref_list(vol, dir_path, ls_callback, NULL);
    
    printf("  ────────────────────────────────────────────────────────────────\n");
    printf("  %d entries\n\n", count);
    
    geofs_volume_close(vol);
    return 0;
}

static int cmd_cat(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: geofs cat <volume> <path>\n");
        return 1;
    }
    
    geofs_volume_t *vol;
    geofs_error_t err = geofs_volume_open(argv[2], &vol);
    if (err != GEOFS_OK) {
        fprintf(stderr, "Error: %s\n", geofs_strerror(err));
        return 1;
    }
    
    geofs_hash_t hash;
    err = geofs_ref_resolve(vol, argv[3], hash);
    if (err != GEOFS_OK) {
        fprintf(stderr, "Error: %s\n", geofs_strerror(err));
        geofs_volume_close(vol);
        return 1;
    }
    
    uint64_t size;
    geofs_content_size(vol, hash, &size);
    
    char *buf = malloc(size + 1);
    if (!buf) {
        fprintf(stderr, "Error: Out of memory\n");
        geofs_volume_close(vol);
        return 1;
    }
    
    size_t got;
    geofs_content_read(vol, hash, buf, size, &got);
    buf[got] = '\0';
    printf("%s", buf);
    
    free(buf);
    geofs_volume_close(vol);
    return 0;
}

static int cmd_write(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: echo 'data' | geofs write <volume> <path>\n");
        return 1;
    }
    
    /* Read stdin */
    char *data = NULL;
    size_t size = 0, cap = 0;
    char buf[4096];
    ssize_t n;
    
    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        if (size + n > cap) {
            cap = cap ? cap * 2 : 4096;
            data = realloc(data, cap);
        }
        memcpy(data + size, buf, n);
        size += n;
    }
    
    geofs_volume_t *vol;
    geofs_error_t err = geofs_volume_open(argv[2], &vol);
    if (err != GEOFS_OK) {
        fprintf(stderr, "Error: %s\n", geofs_strerror(err));
        free(data);
        return 1;
    }
    
    geofs_hash_t hash;
    err = geofs_content_store(vol, data, size, hash);
    free(data);
    
    if (err != GEOFS_OK) {
        fprintf(stderr, "Error: %s\n", geofs_strerror(err));
        geofs_volume_close(vol);
        return 1;
    }
    
    geofs_ref_create(vol, argv[3], hash);
    
    char hash_str[65];
    geofs_hash_to_string(hash, hash_str);
    
    printf("\n  Created: %s\n", argv[3]);
    printf("  Size:    %zu bytes\n", size);
    printf("  Hash:    %s\n\n", hash_str);
    
    geofs_volume_close(vol);
    return 0;
}

static void view_callback(const struct geofs_view_info *info, void *ctx) {
    geofs_view_t *current = ctx;
    char marker = (info->id == *current) ? '*' : ' ';
    char time_str[32];
    geofs_time_format(info->created, time_str, sizeof(time_str));
    
    printf("  %c %3lu  parent:%-3lu  %s  %s\n",
           marker, info->id, info->parent_id, time_str,
           info->label[0] ? info->label : "(unlabeled)");
}

static int cmd_views(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: geofs views <volume>\n");
        return 1;
    }
    
    geofs_volume_t *vol;
    geofs_error_t err = geofs_volume_open(argv[2], &vol);
    if (err != GEOFS_OK) {
        fprintf(stderr, "Error: %s\n", geofs_strerror(err));
        return 1;
    }
    
    printf("\n  Views (* = current):\n");
    printf("  ────────────────────────────────────────────────────────\n");
    
    geofs_view_t current = geofs_view_current(vol);
    geofs_view_list(vol, view_callback, &current);
    
    printf("\n");
    geofs_volume_close(vol);
    return 0;
}

static int cmd_view(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: geofs view <volume> <view_id>\n");
        return 1;
    }
    
    geofs_view_t view_id = strtoull(argv[3], NULL, 10);
    
    geofs_volume_t *vol;
    geofs_error_t err = geofs_volume_open(argv[2], &vol);
    if (err != GEOFS_OK) {
        fprintf(stderr, "Error: %s\n", geofs_strerror(err));
        return 1;
    }
    
    err = geofs_view_switch(vol, view_id);
    if (err != GEOFS_OK) {
        fprintf(stderr, "Error: %s\n", geofs_strerror(err));
        geofs_volume_close(vol);
        return 1;
    }
    
    write_superblock(vol);
    fsync(vol->fd);
    
    printf("\n  Switched to view %lu\n\n", view_id);
    
    geofs_volume_close(vol);
    return 0;
}

static int cmd_hide(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: geofs hide <volume> <path>\n");
        return 1;
    }
    
    geofs_volume_t *vol;
    geofs_error_t err = geofs_volume_open(argv[2], &vol);
    if (err != GEOFS_OK) {
        fprintf(stderr, "Error: %s\n", geofs_strerror(err));
        return 1;
    }
    
    err = geofs_view_hide(vol, argv[3]);
    if (err != GEOFS_OK) {
        fprintf(stderr, "Error: %s\n", geofs_strerror(err));
        geofs_volume_close(vol);
        return 1;
    }
    
    printf("\n");
    printf("  File hidden from current view.\n");
    printf("\n");
    printf("  NOTE: The content has NOT been deleted.\n");
    printf("        It still exists in the geology.\n");
    printf("        Switch to an earlier view to see it again.\n");
    printf("\n");
    
    geofs_volume_close(vol);
    return 0;
}

static int cmd_stats(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: geofs stats <volume>\n");
        return 1;
    }
    
    geofs_volume_t *vol;
    geofs_error_t err = geofs_volume_open(argv[2], &vol);
    if (err != GEOFS_OK) {
        fprintf(stderr, "Error: %s\n", geofs_strerror(err));
        return 1;
    }
    
    char created[32], modified[32];
    geofs_time_format(vol->sb.created, created, sizeof(created));
    geofs_time_format(vol->sb.last_modified, modified, sizeof(modified));
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║              GeoFS Volume Statistics                  ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Volume:        %s\n", argv[2]);
    printf("  Volume ID:     %016lx\n", vol->sb.volume_id);
    printf("  Created:       %s\n", created);
    printf("  Modified:      %s\n", modified);
    printf("\n");
    printf("  Content:       %lu bytes\n", vol->sb.total_content_bytes);
    printf("  References:    %lu\n", vol->sb.total_refs);
    printf("  Views:         %lu\n", vol->sb.total_views);
    printf("  Current View:  %lu\n", vol->sb.current_view);
    printf("\n");
    
    geofs_volume_close(vol);
    return 0;
}

static void usage(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║                   PHANTOM GeoFS                       ║\n");
    printf("║            \"To Create, Not To Destroy\"                ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Geology FileSystem - where nothing is ever deleted.\n");
    printf("\n");
    printf("  COMMANDS:\n");
    printf("\n");
    printf("    geofs create <volume> <size_mb>   Create new volume\n");
    printf("    geofs ls <volume> [path]          List directory\n");
    printf("    geofs cat <volume> <path>         Read file contents\n");
    printf("    geofs write <volume> <path>       Write from stdin\n");
    printf("    geofs views <volume>              List all views\n");
    printf("    geofs view <volume> <id>          Switch to view\n");
    printf("    geofs hide <volume> <path>        Hide file from view\n");
    printf("    geofs stats <volume>              Volume statistics\n");
    printf("\n");
    printf("  EXAMPLES:\n");
    printf("\n");
    printf("    geofs create mydata.geo 100\n");
    printf("    echo 'Hello, Phantom!' | geofs write mydata.geo /hello.txt\n");
    printf("    geofs cat mydata.geo /hello.txt\n");
    printf("    geofs hide mydata.geo /hello.txt\n");
    printf("    geofs views mydata.geo\n");
    printf("    geofs view mydata.geo 1          # Go back in time\n");
    printf("\n");
    printf("  Nothing is ever truly deleted. The geology remembers.\n");
    printf("\n");
}

/* ══════════════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 0;
    }
    
    const char *cmd = argv[1];
    
    if (strcmp(cmd, "create") == 0) return cmd_create(argc, argv);
    if (strcmp(cmd, "ls") == 0)     return cmd_ls(argc, argv);
    if (strcmp(cmd, "cat") == 0)    return cmd_cat(argc, argv);
    if (strcmp(cmd, "write") == 0)  return cmd_write(argc, argv);
    if (strcmp(cmd, "views") == 0)  return cmd_views(argc, argv);
    if (strcmp(cmd, "view") == 0)   return cmd_view(argc, argv);
    if (strcmp(cmd, "hide") == 0)   return cmd_hide(argc, argv);
    if (strcmp(cmd, "stats") == 0)  return cmd_stats(argc, argv);
    if (strcmp(cmd, "help") == 0)   { usage(); return 0; }
    
    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage();
    return 1;
}

#endif /* GEOFS_STANDALONE */
