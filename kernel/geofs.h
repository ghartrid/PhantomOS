/*
 * PhantomOS Kernel GeoFS
 * "To Create, Not To Destroy"
 *
 * Kernel-mode append-only filesystem with geological versioning.
 * RAM-disk backed, content-addressed storage.
 *
 * Key concepts:
 * - Content: Deduplicated data blocks indexed by SHA-256 hash
 * - Refs: Path -> content hash mappings, versioned by view
 * - Views: Geological strata representing filesystem snapshots
 * - Hide: Creates hidden marker in new view (nothing is ever deleted)
 */

#ifndef PHANTOMOS_KERNEL_GEOFS_H
#define PHANTOMOS_KERNEL_GEOFS_H

#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define KGEOFS_VERSION          0x0001
#define KGEOFS_MAGIC            0x53464F45474BULL    /* "KGEOF" */
#define KGEOFS_HASH_SIZE        32                   /* SHA-256 bytes */
#define KGEOFS_MAX_PATH         512
#define KGEOFS_MAX_NAME         128
#define KGEOFS_BLOCK_SIZE       4096                 /* Match page size */

/* Default RAM-disk sizes (in pages) */
#define KGEOFS_DEFAULT_CONTENT_PAGES    256     /* 1MB for content */
#define KGEOFS_DEFAULT_REF_PAGES        64      /* 256KB for refs */
#define KGEOFS_DEFAULT_VIEW_PAGES       32      /* 128KB for views */

/* Magic numbers for on-disk records */
#define KGEOFS_CONTENT_MAGIC    0x544E4F43      /* "CONT" */
#define KGEOFS_REF_MAGIC        0x46455247      /* "GREF" */
#define KGEOFS_VIEW_MAGIC       0x57454956      /* "VIEW" */
#define KGEOFS_VIEW2_MAGIC      0x32574956      /* "VIW2" - v2 with branch_id */
#define KGEOFS_BRANCH_MAGIC     0x48435242      /* "BRCH" */
#define KGEOFS_QUOTA_MAGIC      0x41544F51      /* "QOTA" */

/* Branch constants */
#define KGEOFS_BRANCH_NAME_MAX  64
#define KGEOFS_MAX_ANCESTRY     256
#define KGEOFS_QUOTA_VOLUME     0xFFFFFFFFFFFFFFFFULL   /* Volume-wide quota sentinel */

/* Hash table size for content deduplication */
#define KGEOFS_HASH_BUCKETS     256
#define KGEOFS_HASH_BUCKET(h)   ((h)[0])

/* Special content for directories */
#define KGEOFS_DIR_MARKER       "__PHANTOM_DIR__"
#define KGEOFS_DIR_MARKER_LEN   15

/*============================================================================
 * Types
 *============================================================================*/

/* Error codes */
typedef enum {
    KGEOFS_OK           =  0,
    KGEOFS_ERR_IO       = -1,
    KGEOFS_ERR_NOMEM    = -2,
    KGEOFS_ERR_NOTFOUND = -3,
    KGEOFS_ERR_EXISTS   = -4,
    KGEOFS_ERR_INVALID  = -5,
    KGEOFS_ERR_FULL     = -6,
    KGEOFS_ERR_CORRUPT  = -7,
    KGEOFS_ERR_ISDIR    = -8,
    KGEOFS_ERR_NOTDIR   = -9,
    KGEOFS_ERR_PERM     = -10,  /* Permission denied */
    KGEOFS_ERR_QUOTA    = -11,  /* Quota exceeded */
    KGEOFS_ERR_CONFLICT = -12,  /* Merge conflict */
} kgeofs_error_t;

/* Hash type (SHA-256) */
typedef uint8_t kgeofs_hash_t[KGEOFS_HASH_SIZE];

/* View ID (geological stratum identifier) */
typedef uint64_t kgeofs_view_t;

/* Branch ID (tectonic divergence identifier) */
typedef uint64_t kgeofs_branch_t;

/* Timestamp (timer ticks since boot) */
typedef uint64_t kgeofs_time_t;

/* Forward declaration */
typedef struct kgeofs_volume kgeofs_volume_t;

/*============================================================================
 * Data Structures
 *============================================================================*/

/* RAM region - linked list of PMM-allocated pages */
struct kgeofs_ram_region {
    void                        *base;      /* Virtual/physical address */
    size_t                       size;      /* Total size in bytes */
    size_t                       used;      /* Bytes used (append offset) */
    struct kgeofs_ram_region    *next;      /* Next region chunk */
};

/* Content header (stored at start of each content block) */
struct kgeofs_content_header {
    uint32_t        magic;                  /* KGEOFS_CONTENT_MAGIC */
    uint32_t        flags;
    uint64_t        size;                   /* Data size after header */
    kgeofs_hash_t   hash;                   /* SHA-256 of data */
    uint8_t         reserved[16];           /* Pad to 64 bytes */
};

/* Content index entry (in-memory, for fast lookup) */
struct kgeofs_content_entry {
    kgeofs_hash_t               hash;
    uint64_t                    offset;     /* Offset in content region */
    uint64_t                    size;       /* Data size (excluding header) */
    struct kgeofs_content_entry *next;      /* Hash chain */
};

/* File permissions (bitfield) */
#define KGEOFS_PERM_READ    (1 << 0)
#define KGEOFS_PERM_WRITE   (1 << 1)
#define KGEOFS_PERM_EXEC    (1 << 2)
#define KGEOFS_PERM_DEFAULT (KGEOFS_PERM_READ | KGEOFS_PERM_WRITE)

/* File type */
#define KGEOFS_TYPE_FILE    0
#define KGEOFS_TYPE_DIR     1
#define KGEOFS_TYPE_LINK    2

/* Reference record (stored in ref region) */
struct kgeofs_ref_record {
    uint32_t        magic;                  /* KGEOFS_REF_MAGIC */
    uint32_t        flags;                  /* bit 0 = is_hidden */
    kgeofs_hash_t   path_hash;              /* SHA-256 of path */
    kgeofs_hash_t   content_hash;           /* Points to content */
    kgeofs_view_t   view_id;                /* View this ref belongs to */
    kgeofs_time_t   created;                /* Creation timestamp */
    uint16_t        path_len;
    uint8_t         file_type;              /* KGEOFS_TYPE_* */
    uint8_t         permissions;            /* KGEOFS_PERM_* bitfield */
    uint16_t        owner_id;               /* Owner user ID */
    uint16_t        reserved_pad;
    char            path[KGEOFS_MAX_PATH];
};

#define KGEOFS_REF_FLAG_HIDDEN  (1 << 0)

/* Reference index entry (in-memory) */
struct kgeofs_ref_entry {
    kgeofs_hash_t               path_hash;
    kgeofs_hash_t               content_hash;
    kgeofs_view_t               view_id;
    kgeofs_time_t               created;
    char                        path[KGEOFS_MAX_PATH];
    int                         is_hidden;
    uint8_t                     file_type;      /* KGEOFS_TYPE_* */
    uint8_t                     permissions;    /* KGEOFS_PERM_* */
    uint16_t                    owner_id;
    struct kgeofs_ref_entry    *next;
};

/* View record (stored in view region) */
struct kgeofs_view_record {
    uint32_t        magic;                  /* KGEOFS_VIEW_MAGIC */
    uint32_t        flags;
    kgeofs_view_t   id;
    kgeofs_view_t   parent_id;
    kgeofs_time_t   created;
    char            label[64];
};

/* V2 view record (104 bytes, stored in view_region — has branch_id) */
struct kgeofs_view2_record {
    uint32_t        magic;                  /* KGEOFS_VIEW2_MAGIC */
    uint32_t        flags;
    kgeofs_view_t   id;
    kgeofs_view_t   parent_id;
    kgeofs_branch_t branch_id;              /* Branch this view belongs to */
    kgeofs_time_t   created;
    char            label[64];
};

/* Branch record (104 bytes, stored in view_region) */
struct kgeofs_branch_record {
    uint32_t        magic;                  /* KGEOFS_BRANCH_MAGIC */
    uint32_t        flags;
    kgeofs_branch_t id;
    kgeofs_view_t   base_view;              /* Fork point view */
    kgeofs_view_t   head_view;              /* Latest view on this branch */
    kgeofs_time_t   created;
    char            name[KGEOFS_BRANCH_NAME_MAX];
};

/* Branch index entry (in-memory) */
struct kgeofs_branch_entry {
    kgeofs_branch_t             id;
    kgeofs_view_t               base_view;
    kgeofs_view_t               head_view;
    kgeofs_time_t               created;
    char                        name[KGEOFS_BRANCH_NAME_MAX];
    struct kgeofs_branch_entry *next;
};

/* Quota limits */
struct kgeofs_quota {
    uint64_t    max_content_bytes;          /* 0 = unlimited */
    uint64_t    max_ref_count;              /* 0 = unlimited */
    uint64_t    max_view_count;             /* 0 = unlimited */
};

/* Quota record (stored in view_region) */
struct kgeofs_quota_record {
    uint32_t            magic;              /* KGEOFS_QUOTA_MAGIC */
    uint32_t            flags;
    kgeofs_branch_t     branch_id;          /* KGEOFS_QUOTA_VOLUME = volume-wide */
    struct kgeofs_quota limits;
    kgeofs_time_t       created;
};

/* Quota index entry (in-memory) */
struct kgeofs_quota_entry {
    kgeofs_branch_t             branch_id;
    struct kgeofs_quota         limits;
    struct kgeofs_quota_entry  *next;
};

/* Access context — identifies the caller of file operations */
struct kgeofs_access_ctx {
    uint16_t    uid;                        /* User ID (0 = root/kernel) */
    uint16_t    gid;                        /* Group ID (0 = root/kernel) */
    uint32_t    caps;                       /* Governor capabilities */
};

/* Merge conflict entry */
struct kgeofs_merge_conflict {
    char            path[KGEOFS_MAX_PATH];
    kgeofs_hash_t   content_ours;           /* Content on target branch */
    kgeofs_hash_t   content_theirs;         /* Content on source branch */
};

/* View index entry (in-memory) */
struct kgeofs_view_entry {
    kgeofs_view_t               id;
    kgeofs_view_t               parent_id;
    kgeofs_branch_t             branch_id;  /* 0 = main (backward compat) */
    kgeofs_time_t               created;
    char                        label[64];
    struct kgeofs_view_entry   *next;
};

/* Volume structure */
struct kgeofs_volume {
    uint64_t                    magic;
    uint16_t                    version;
    kgeofs_time_t               created;

    /* RAM-disk regions */
    struct kgeofs_ram_region   *content_region;
    struct kgeofs_ram_region   *ref_region;
    struct kgeofs_ram_region   *view_region;

    /* In-memory indices */
    struct kgeofs_content_entry *content_hash[KGEOFS_HASH_BUCKETS];
    struct kgeofs_ref_entry    *ref_index;
    struct kgeofs_view_entry   *view_index;
    struct kgeofs_branch_entry *branch_index;
    struct kgeofs_quota_entry  *quota_index;

    /* Current state */
    kgeofs_view_t               current_view;
    kgeofs_view_t               next_view_id;
    kgeofs_branch_t             current_branch;
    kgeofs_branch_t             next_branch_id;

    /* Ancestry cache (rebuilt on view/branch switch) */
    kgeofs_view_t               ancestry_cache[KGEOFS_MAX_ANCESTRY];
    int                         ancestry_count;

    /* Access control context */
    struct kgeofs_access_ctx    current_ctx;

    /* Statistics (append-only, never reset) */
    uint64_t                    total_content_bytes;
    uint64_t                    total_refs;
    uint64_t                    total_views;
    uint64_t                    total_branches;
    uint64_t                    dedup_hits;
    uint64_t                    total_lookups;
};

/* Statistics structure for external queries */
struct kgeofs_stats {
    uint64_t    content_bytes;
    uint64_t    content_region_size;
    uint64_t    content_region_used;
    uint64_t    ref_count;
    uint64_t    ref_region_size;
    uint64_t    ref_region_used;
    uint64_t    view_count;
    uint64_t    view_region_size;
    uint64_t    view_region_used;
    uint64_t    dedup_hits;
    uint64_t    current_view;
};

/* Directory entry for listing */
struct kgeofs_dirent {
    char            name[KGEOFS_MAX_NAME];
    kgeofs_hash_t   content_hash;
    uint64_t        size;
    int             is_directory;
    kgeofs_time_t   created;
    uint8_t         permissions;
    uint16_t        owner_id;
};

/* View diff entry */
struct kgeofs_diff_entry {
    char            path[KGEOFS_MAX_PATH];
    int             change_type;        /* 0=added, 1=modified, 2=hidden */
    kgeofs_view_t   view_id;
    kgeofs_time_t   timestamp;
};

/*============================================================================
 * Volume Functions
 *============================================================================*/

/*
 * Create a new kernel GeoFS volume
 * Allocates PMM pages for RAM-disk regions
 *
 * @content_pages: Pages for content region (0 = default 256)
 * @ref_pages:     Pages for reference region (0 = default 64)
 * @view_pages:    Pages for view region (0 = default 32)
 * @vol_out:       Output volume pointer
 * @return:        KGEOFS_OK on success
 */
kgeofs_error_t kgeofs_volume_create(size_t content_pages,
                                    size_t ref_pages,
                                    size_t view_pages,
                                    kgeofs_volume_t **vol_out);

/*
 * Destroy a volume (DEBUG ONLY - violates Phantom philosophy!)
 * Frees all PMM pages and index structures
 */
void kgeofs_volume_destroy(kgeofs_volume_t *vol);

/*
 * Get volume statistics
 */
void kgeofs_volume_stats(kgeofs_volume_t *vol, struct kgeofs_stats *stats);

/*============================================================================
 * Content Functions (low-level, content-addressed)
 *============================================================================*/

/*
 * Store content in the volume (deduplicated by hash)
 *
 * @vol:      Volume handle
 * @data:     Data to store
 * @size:     Size of data
 * @hash_out: Output: SHA-256 hash of content
 * @return:   KGEOFS_OK on success
 */
kgeofs_error_t kgeofs_content_store(kgeofs_volume_t *vol,
                                    const void *data,
                                    size_t size,
                                    kgeofs_hash_t hash_out);

/*
 * Read content by hash
 */
kgeofs_error_t kgeofs_content_read(kgeofs_volume_t *vol,
                                   const kgeofs_hash_t hash,
                                   void *buf,
                                   size_t buf_size,
                                   size_t *size_out);

/*
 * Get content size by hash (without reading)
 */
kgeofs_error_t kgeofs_content_size(kgeofs_volume_t *vol,
                                   const kgeofs_hash_t hash,
                                   uint64_t *size_out);

/*============================================================================
 * Reference Functions (path -> content mapping)
 *============================================================================*/

/*
 * Create a reference (path points to content hash in current view)
 */
kgeofs_error_t kgeofs_ref_create(kgeofs_volume_t *vol,
                                 const char *path,
                                 const kgeofs_hash_t content_hash);

/*
 * Resolve path to content hash in current view
 */
kgeofs_error_t kgeofs_ref_resolve(kgeofs_volume_t *vol,
                                  const char *path,
                                  kgeofs_hash_t hash_out);

/*
 * List directory contents
 * Callback receives each entry; return non-zero from callback to stop
 */
typedef int (*kgeofs_dir_callback_t)(const struct kgeofs_dirent *entry,
                                      void *ctx);

int kgeofs_ref_list(kgeofs_volume_t *vol,
                    const char *dir_path,
                    kgeofs_dir_callback_t callback,
                    void *ctx);

/*============================================================================
 * View Functions (geological strata)
 *============================================================================*/

/*
 * Create a new view (child of current view)
 */
kgeofs_error_t kgeofs_view_create(kgeofs_volume_t *vol,
                                  const char *label,
                                  kgeofs_view_t *view_out);

/*
 * Switch to a different view (time travel)
 */
kgeofs_error_t kgeofs_view_switch(kgeofs_volume_t *vol,
                                  kgeofs_view_t view_id);

/*
 * Get current view ID
 */
kgeofs_view_t kgeofs_view_current(kgeofs_volume_t *vol);

/*
 * Hide a file (creates new view with hidden marker)
 * The file is hidden in current and future views but accessible in past views
 */
kgeofs_error_t kgeofs_view_hide(kgeofs_volume_t *vol,
                                const char *path);

/*
 * List all views
 */
typedef void (*kgeofs_view_callback_t)(kgeofs_view_t id,
                                        kgeofs_view_t parent_id,
                                        const char *label,
                                        kgeofs_time_t created,
                                        void *ctx);

int kgeofs_view_list(kgeofs_volume_t *vol,
                     kgeofs_view_callback_t callback,
                     void *ctx);

/*============================================================================
 * High-Level File Functions
 *============================================================================*/

/*
 * Write a file (store content + create reference)
 */
kgeofs_error_t kgeofs_file_write(kgeofs_volume_t *vol,
                                 const char *path,
                                 const void *data,
                                 size_t size);

/*
 * Read a file (resolve reference + read content)
 */
kgeofs_error_t kgeofs_file_read(kgeofs_volume_t *vol,
                                const char *path,
                                void *buf,
                                size_t buf_size,
                                size_t *size_out);

/*
 * Get file info
 */
kgeofs_error_t kgeofs_file_stat(kgeofs_volume_t *vol,
                                const char *path,
                                uint64_t *size_out,
                                int *is_dir_out);

/*
 * Create a directory
 */
kgeofs_error_t kgeofs_mkdir(kgeofs_volume_t *vol,
                            const char *path);

/*
 * Check if path exists in current view
 */
int kgeofs_exists(kgeofs_volume_t *vol, const char *path);

/*============================================================================
 * Extended File Functions
 *============================================================================*/

/*
 * Append data to an existing file (reads old content, concatenates, writes new)
 */
kgeofs_error_t kgeofs_file_append(kgeofs_volume_t *vol,
                                   const char *path,
                                   const void *data,
                                   size_t size);

/*
 * Rename/move a file (creates ref at new path pointing to same content, hides old)
 */
kgeofs_error_t kgeofs_file_rename(kgeofs_volume_t *vol,
                                   const char *old_path,
                                   const char *new_path);

/*
 * Copy a file (creates ref at dest pointing to same content hash — zero-copy)
 */
kgeofs_error_t kgeofs_file_copy(kgeofs_volume_t *vol,
                                 const char *src_path,
                                 const char *dst_path);

/*
 * Recursive directory listing
 * Callback receives full path and depth for each entry
 */
typedef int (*kgeofs_tree_callback_t)(const char *full_path,
                                       const struct kgeofs_dirent *entry,
                                       int depth,
                                       void *ctx);

int kgeofs_ref_list_recursive(kgeofs_volume_t *vol,
                               const char *dir_path,
                               int max_depth,
                               kgeofs_tree_callback_t callback,
                               void *ctx);

/*
 * Search/find files by name pattern (case-insensitive substring match)
 */
typedef int (*kgeofs_find_callback_t)(const char *path,
                                       uint64_t size,
                                       int is_dir,
                                       void *ctx);

int kgeofs_file_find(kgeofs_volume_t *vol,
                      const char *start_path,
                      const char *pattern,
                      kgeofs_find_callback_t callback,
                      void *ctx);

/*
 * Set file permissions
 */
kgeofs_error_t kgeofs_file_chmod(kgeofs_volume_t *vol,
                                  const char *path,
                                  uint8_t permissions);

/*
 * Set file owner
 */
kgeofs_error_t kgeofs_file_chown(kgeofs_volume_t *vol,
                                  const char *path,
                                  uint16_t owner_id);

/*============================================================================
 * View Diff Functions
 *============================================================================*/

/*
 * Diff between two views — shows what changed
 * Callback receives each changed file
 */
typedef int (*kgeofs_diff_callback_t)(const struct kgeofs_diff_entry *entry,
                                       void *ctx);

int kgeofs_view_diff(kgeofs_volume_t *vol,
                      kgeofs_view_t view_a,
                      kgeofs_view_t view_b,
                      kgeofs_diff_callback_t callback,
                      void *ctx);

/*============================================================================
 * Branch Functions (tectonic divergence)
 *============================================================================*/

/*
 * Create a new branch from the current view
 */
kgeofs_error_t kgeofs_branch_create(kgeofs_volume_t *vol,
                                     const char *name,
                                     kgeofs_branch_t *branch_out);

/*
 * Switch to a branch (sets current_view to branch head)
 */
kgeofs_error_t kgeofs_branch_switch(kgeofs_volume_t *vol,
                                     kgeofs_branch_t branch_id);

/*
 * Switch to a branch by name
 */
kgeofs_error_t kgeofs_branch_switch_name(kgeofs_volume_t *vol,
                                          const char *name);

/*
 * Get current branch ID
 */
kgeofs_branch_t kgeofs_branch_current(kgeofs_volume_t *vol);

/*
 * List all branches
 */
typedef void (*kgeofs_branch_callback_t)(kgeofs_branch_t id,
                                          const char *name,
                                          kgeofs_view_t base_view,
                                          kgeofs_view_t head_view,
                                          kgeofs_time_t created,
                                          void *ctx);

int kgeofs_branch_list(kgeofs_volume_t *vol,
                        kgeofs_branch_callback_t callback,
                        void *ctx);

/*
 * Diff between two branches (from common ancestor)
 */
int kgeofs_branch_diff(kgeofs_volume_t *vol,
                        kgeofs_branch_t branch_a,
                        kgeofs_branch_t branch_b,
                        kgeofs_diff_callback_t callback,
                        void *ctx);

/*
 * Merge source branch into current branch
 * Non-conflicting changes are applied; conflicts are counted.
 */
kgeofs_error_t kgeofs_branch_merge(kgeofs_volume_t *vol,
                                    kgeofs_branch_t source,
                                    const char *label,
                                    int *conflict_count);

/*============================================================================
 * Access Control Functions
 *============================================================================*/

/*
 * Set the current access context (caller identity for permission checks)
 */
void kgeofs_set_context(kgeofs_volume_t *vol,
                         const struct kgeofs_access_ctx *ctx);

/*
 * Get the current access context
 */
const struct kgeofs_access_ctx *kgeofs_get_context(kgeofs_volume_t *vol);

/*============================================================================
 * Quota Functions
 *============================================================================*/

/*
 * Set quota for a branch (or volume-wide if branch_id = KGEOFS_QUOTA_VOLUME)
 */
kgeofs_error_t kgeofs_quota_set(kgeofs_volume_t *vol,
                                 kgeofs_branch_t branch_id,
                                 const struct kgeofs_quota *limits);

/*
 * Get quota for a branch
 */
kgeofs_error_t kgeofs_quota_get(kgeofs_volume_t *vol,
                                 kgeofs_branch_t branch_id,
                                 struct kgeofs_quota *limits_out);

/*
 * Get quota usage for a branch
 */
kgeofs_error_t kgeofs_quota_usage(kgeofs_volume_t *vol,
                                   kgeofs_branch_t branch_id,
                                   uint64_t *content_bytes_out,
                                   uint64_t *ref_count_out,
                                   uint64_t *view_count_out);

/*============================================================================
 * ATA Import/Export Functions
 *============================================================================*/

/*
 * Export a file to ATA disk (writes content to consecutive sectors)
 * Returns number of sectors written
 */
kgeofs_error_t kgeofs_file_export_ata(kgeofs_volume_t *vol,
                                       const char *path,
                                       uint8_t drive,
                                       uint64_t start_sector,
                                       uint64_t *sectors_written);

/*
 * Import a file from ATA disk sectors
 */
kgeofs_error_t kgeofs_file_import_ata(kgeofs_volume_t *vol,
                                       const char *path,
                                       uint8_t drive,
                                       uint64_t start_sector,
                                       uint64_t num_sectors);

/*============================================================================
 * Volume Persistence (ATA Disk Save/Restore)
 *============================================================================*/

#define KGEOFS_PERSIST_MAGIC    0x504852534F45474BULL  /* "KGEOFPHR" */
#define KGEOFS_PERSIST_VERSION  2   /* v2: branch support */

/* On-disk superblock — exactly 512 bytes (one ATA sector) */
struct kgeofs_persist_header {
    uint64_t        magic;                  /* KGEOFS_PERSIST_MAGIC */
    uint32_t        version;                /* KGEOFS_PERSIST_VERSION */
    uint32_t        flags;                  /* Reserved */

    /* Volume metadata */
    kgeofs_view_t   current_view;
    kgeofs_view_t   next_view_id;
    kgeofs_time_t   created;
    uint64_t        total_content_bytes;
    uint64_t        total_refs;
    uint64_t        total_views;
    uint64_t        dedup_hits;
    uint64_t        total_lookups;

    /* Region used bytes (actual data, not allocated capacity) */
    uint64_t        content_used;
    uint64_t        ref_used;
    uint64_t        view_used;

    /* Sector layout (offsets relative to start_sector) */
    uint64_t        content_start_sector;   /* = 1 (after superblock) */
    uint64_t        content_sector_count;
    uint64_t        ref_start_sector;
    uint64_t        ref_sector_count;
    uint64_t        view_start_sector;
    uint64_t        view_sector_count;

    /* SHA-256 checksum of all region data */
    kgeofs_hash_t   checksum;

    /* Branch state (v2) */
    kgeofs_branch_t current_branch;
    kgeofs_branch_t next_branch_id;
    uint64_t        total_branches;

    uint8_t         reserved[304];          /* Pad to exactly 512 bytes */
};

/*
 * Save entire volume to ATA disk
 * Serializes all three regions (content, refs, views) and metadata.
 *
 * @vol:           Volume to save
 * @drive:         ATA drive index (0-3)
 * @start_sector:  First sector on disk (default: 2048 = 1MB offset)
 */
kgeofs_error_t kgeofs_volume_save(kgeofs_volume_t *vol,
                                   uint8_t drive,
                                   uint64_t start_sector);

/*
 * Load volume from ATA disk
 * Deserializes regions and rebuilds in-memory indices.
 *
 * @drive:         ATA drive index (0-3)
 * @start_sector:  First sector on disk
 * @vol_out:       Output: loaded volume
 */
kgeofs_error_t kgeofs_volume_load(uint8_t drive,
                                   uint64_t start_sector,
                                   kgeofs_volume_t **vol_out);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/*
 * Compute SHA-256 hash
 */
void kgeofs_hash_compute(const void *data, size_t len, kgeofs_hash_t hash_out);

/*
 * Compare two hashes
 */
int kgeofs_hash_equal(const kgeofs_hash_t a, const kgeofs_hash_t b);

/*
 * Convert hash to hex string (needs 65 bytes for output)
 */
void kgeofs_hash_to_string(const kgeofs_hash_t hash, char *buf);

/*
 * Get error string
 */
const char *kgeofs_strerror(kgeofs_error_t err);

/*
 * Get current time (timer ticks)
 */
kgeofs_time_t kgeofs_time_now(void);

/*============================================================================
 * Debug Functions
 *============================================================================*/

void kgeofs_dump_stats(kgeofs_volume_t *vol);
void kgeofs_dump_refs(kgeofs_volume_t *vol);
void kgeofs_dump_views(kgeofs_volume_t *vol);

#endif /* PHANTOMOS_KERNEL_GEOFS_H */
