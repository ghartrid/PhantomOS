/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                                 PHANTOM GeoFS
 *                        Geology FileSystem - Library API
 *
 *                          "To Create, Not To Destroy"
 *
 *    This header exposes the GeoFS library functions for use by the
 *    Phantom kernel and other system components.
 *
 * ══════════════════════════════════════════════════════════════════════════════
 */

#ifndef GEOFS_H
#define GEOFS_H

#include <stdint.h>
#include <stddef.h>

/* ══════════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ══════════════════════════════════════════════════════════════════════════════ */

#define GEOFS_VERSION           0x0001
#define GEOFS_MAGIC             0x53464F4547ULL  /* "GEOFS" */
#define GEOFS_HASH_SIZE         32
#define GEOFS_MAX_PATH          4096
#define GEOFS_MAX_NAME          255
#define GEOFS_BLOCK_SIZE        4096

/* ══════════════════════════════════════════════════════════════════════════════
 * TYPES
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef uint8_t     geofs_hash_t[GEOFS_HASH_SIZE];
typedef uint64_t    geofs_time_t;
typedef uint64_t    geofs_view_t;

typedef enum {
    GEOFS_OK            =  0,
    GEOFS_ERR_IO        = -1,
    GEOFS_ERR_NOMEM     = -2,
    GEOFS_ERR_NOTFOUND  = -3,
    GEOFS_ERR_EXISTS    = -4,
    GEOFS_ERR_INVALID   = -5,
    GEOFS_ERR_CORRUPT   = -6,
    GEOFS_ERR_FULL      = -7,
} geofs_error_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * STRUCTURES
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Opaque volume handle */
typedef struct geofs_volume geofs_volume_t;

/* Directory entry returned by listing */
struct geofs_dirent {
    char        name[GEOFS_MAX_NAME + 1];
    geofs_hash_t content_hash;
    uint64_t    size;
    geofs_time_t created;
    int         is_dir;
};

/* View information */
struct geofs_view_info {
    geofs_view_t id;
    geofs_view_t parent_id;
    geofs_time_t created;
    char        label[64];
};

/* File history entry (for geology viewer) */
struct geofs_history_entry {
    char        path[GEOFS_MAX_PATH];
    geofs_hash_t content_hash;
    geofs_view_t view_id;
    geofs_time_t created;
    uint64_t    size;
    int         is_hidden;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * CALLBACK TYPES
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef void (*geofs_dir_callback)(const struct geofs_dirent *entry, void *ctx);
typedef void (*geofs_view_callback)(const struct geofs_view_info *info, void *ctx);
typedef void (*geofs_history_callback)(const struct geofs_history_entry *entry, void *ctx);

/* ══════════════════════════════════════════════════════════════════════════════
 * VOLUME OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Create a new GeoFS volume.
 *
 * @param path      Path to the volume file (will be created)
 * @param size_mb   Size of the volume in megabytes
 * @param vol_out   Output pointer for the opened volume
 * @return          GEOFS_OK on success, error code otherwise
 */
geofs_error_t geofs_volume_create(const char *path, uint64_t size_mb,
                                   geofs_volume_t **vol_out);

/*
 * Open an existing GeoFS volume.
 *
 * @param path      Path to the volume file
 * @param vol_out   Output pointer for the opened volume
 * @return          GEOFS_OK on success, error code otherwise
 */
geofs_error_t geofs_volume_open(const char *path, geofs_volume_t **vol_out);

/*
 * Close a GeoFS volume.
 * All changes are flushed to disk.
 *
 * @param vol   The volume to close
 */
void geofs_volume_close(geofs_volume_t *vol);

/* ══════════════════════════════════════════════════════════════════════════════
 * CONTENT OPERATIONS (append-only, content-addressed)
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Store content in the volume.
 * Content is deduplicated by hash - storing the same data twice is a no-op.
 *
 * @param vol       The volume
 * @param data      Pointer to the data to store
 * @param size      Size of the data
 * @param hash_out  Output: the content hash (32 bytes)
 * @return          GEOFS_OK on success
 */
geofs_error_t geofs_content_store(geofs_volume_t *vol, const void *data,
                                   size_t size, geofs_hash_t hash_out);

/*
 * Read content from the volume by hash.
 *
 * @param vol       The volume
 * @param hash      The content hash
 * @param buf       Buffer to read into
 * @param buf_size  Size of the buffer
 * @param size_out  Output: actual bytes read
 * @return          GEOFS_OK on success, GEOFS_ERR_NOTFOUND if hash not found
 */
geofs_error_t geofs_content_read(geofs_volume_t *vol, const geofs_hash_t hash,
                                  void *buf, size_t buf_size, size_t *size_out);

/*
 * Get the size of content by hash.
 *
 * @param vol       The volume
 * @param hash      The content hash
 * @param size_out  Output: the content size
 * @return          GEOFS_OK on success
 */
geofs_error_t geofs_content_size(geofs_volume_t *vol, const geofs_hash_t hash,
                                  uint64_t *size_out);

/* ══════════════════════════════════════════════════════════════════════════════
 * REFERENCE OPERATIONS (path -> content mapping)
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Create a reference (path -> content hash mapping).
 * References are versioned by view - old versions remain accessible.
 *
 * @param vol           The volume
 * @param path          The file path
 * @param content_hash  The content hash this path should point to
 * @return              GEOFS_OK on success
 */
geofs_error_t geofs_ref_create(geofs_volume_t *vol, const char *path,
                                const geofs_hash_t content_hash);

/*
 * Resolve a path to its content hash in the current view.
 *
 * @param vol       The volume
 * @param path      The file path
 * @param hash_out  Output: the content hash
 * @return          GEOFS_OK on success, GEOFS_ERR_NOTFOUND if not found
 */
geofs_error_t geofs_ref_resolve(geofs_volume_t *vol, const char *path,
                                 geofs_hash_t hash_out);

/*
 * List files in a directory.
 *
 * @param vol       The volume
 * @param dir_path  The directory path (e.g., "/" or "/subdir")
 * @param callback  Called for each entry
 * @param ctx       User context passed to callback
 * @return          Number of entries found
 */
int geofs_ref_list(geofs_volume_t *vol, const char *dir_path,
                   geofs_dir_callback callback, void *ctx);

/* ══════════════════════════════════════════════════════════════════════════════
 * VIEW OPERATIONS (geological strata)
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Create a new view (geological stratum).
 * Views provide a way to see different "versions" of the filesystem.
 *
 * @param vol       The volume
 * @param label     Optional label for the view
 * @param view_out  Output: the new view ID
 * @return          GEOFS_OK on success
 */
geofs_error_t geofs_view_create(geofs_volume_t *vol, const char *label,
                                 geofs_view_t *view_out);

/*
 * Switch to a different view.
 * This changes what files are visible - older views show older state.
 *
 * @param vol       The volume
 * @param view_id   The view to switch to
 * @return          GEOFS_OK on success
 */
geofs_error_t geofs_view_switch(geofs_volume_t *vol, geofs_view_t view_id);

/*
 * Get the current view ID.
 *
 * @param vol   The volume
 * @return      The current view ID
 */
geofs_view_t geofs_view_current(geofs_volume_t *vol);

/*
 * List all views.
 *
 * @param vol       The volume
 * @param callback  Called for each view
 * @param ctx       User context passed to callback
 * @return          Number of views
 */
int geofs_view_list(geofs_volume_t *vol, geofs_view_callback callback, void *ctx);

/*
 * Hide a file from the current view.
 * This does NOT delete the file - it creates a new view where the file
 * is not visible. The file can still be accessed from earlier views.
 *
 * @param vol   The volume
 * @param path  The file path to hide
 * @return      GEOFS_OK on success
 */
geofs_error_t geofs_view_hide(geofs_volume_t *vol, const char *path);

/* ══════════════════════════════════════════════════════════════════════════════
 * HISTORY OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * List all file entries in the geology (file history).
 * This includes all versions of all files, including hidden entries.
 *
 * @param vol       The volume
 * @param callback  Called for each history entry
 * @param ctx       User context passed to callback
 * @return          Number of entries
 */
int geofs_ref_history(geofs_volume_t *vol, geofs_history_callback callback, void *ctx);

/* ══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Get a human-readable error message.
 *
 * @param err   The error code
 * @return      Error message string
 */
const char *geofs_strerror(geofs_error_t err);

/*
 * Format a timestamp for display.
 *
 * @param t     The timestamp
 * @param buf   Buffer to write to
 * @param len   Buffer length
 */
void geofs_time_format(geofs_time_t t, char *buf, size_t len);

/*
 * Convert a hash to a hex string.
 *
 * @param hash  The hash (32 bytes)
 * @param buf   Buffer for hex string (must be at least 65 bytes)
 */
void geofs_hash_to_string(const geofs_hash_t hash, char *buf);

#endif /* GEOFS_H */
