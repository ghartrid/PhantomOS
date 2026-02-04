/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM TEMPORAL ENGINE
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * A temporal database engine that transforms the immutable geology into
 * a queryable timeline. Every change is preserved forever - this engine
 * lets you explore that history.
 *
 * Key Capabilities:
 * 1. TIME TRAVEL: Query the system state at any point in history
 * 2. DIFF: Compare system states between two timestamps
 * 3. AUDIT: Find who changed what and when
 * 4. FORENSICS: Trace the complete history of any file or process
 * 5. ROLLBACK VIEW: See what the system looked like at any moment
 *
 * Philosophy:
 *   In PhantomOS, nothing is ever destroyed. The Temporal Engine transforms
 *   this from a philosophical stance into a practical superpower - the ability
 *   to explore your system's entire history as easily as navigating files.
 */

#ifndef PHANTOM_TIME_H
#define PHANTOM_TIME_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "phantom.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────── */

#define PHANTOM_TIME_MAX_RESULTS    1024
#define PHANTOM_TIME_MAX_PATH       4096
#define PHANTOM_TIME_MAX_QUERY      2048
#define PHANTOM_TIME_INDEX_MAGIC    0x54494D455048414EULL  /* "PHANTIME" */

/* ─────────────────────────────────────────────────────────────────────────────
 * Event Types
 *
 * Every change in the system is categorized into event types.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    /* File Events */
    TIME_EVENT_FILE_CREATE      = 0x0100,
    TIME_EVENT_FILE_WRITE       = 0x0101,
    TIME_EVENT_FILE_APPEND      = 0x0102,
    TIME_EVENT_FILE_HIDE        = 0x0103,   /* Phantom 'delete' */
    TIME_EVENT_FILE_UNHIDE      = 0x0104,
    TIME_EVENT_FILE_RENAME      = 0x0105,
    TIME_EVENT_FILE_LINK        = 0x0106,

    /* Directory Events */
    TIME_EVENT_DIR_CREATE       = 0x0200,
    TIME_EVENT_DIR_HIDE         = 0x0201,

    /* Process Events */
    TIME_EVENT_PROC_CREATE      = 0x0300,
    TIME_EVENT_PROC_SUSPEND     = 0x0301,
    TIME_EVENT_PROC_RESUME      = 0x0302,
    TIME_EVENT_PROC_DORMANT     = 0x0303,
    TIME_EVENT_PROC_AWAKEN      = 0x0304,
    TIME_EVENT_PROC_STATE       = 0x0305,

    /* User Events */
    TIME_EVENT_USER_CREATE      = 0x0400,
    TIME_EVENT_USER_LOGIN       = 0x0401,
    TIME_EVENT_USER_LOGOUT      = 0x0402,
    TIME_EVENT_USER_LOCK        = 0x0403,
    TIME_EVENT_USER_UNLOCK      = 0x0404,
    TIME_EVENT_USER_DORMANT     = 0x0405,
    TIME_EVENT_USER_PERM        = 0x0406,

    /* Package Events */
    TIME_EVENT_PKG_INSTALL      = 0x0500,
    TIME_EVENT_PKG_ARCHIVE      = 0x0501,
    TIME_EVENT_PKG_RESTORE      = 0x0502,
    TIME_EVENT_PKG_SUPERSEDE    = 0x0503,

    /* Network Events */
    TIME_EVENT_NET_CONNECT      = 0x0600,
    TIME_EVENT_NET_SEND         = 0x0601,
    TIME_EVENT_NET_RECV         = 0x0602,
    TIME_EVENT_NET_SUSPEND      = 0x0603,
    TIME_EVENT_NET_DORMANT      = 0x0604,

    /* Governor Events */
    TIME_EVENT_GOV_APPROVE      = 0x0700,
    TIME_EVENT_GOV_DECLINE      = 0x0701,
    TIME_EVENT_GOV_QUERY        = 0x0702,

    /* Service Events */
    TIME_EVENT_SVC_AWAKEN       = 0x0800,
    TIME_EVENT_SVC_REST         = 0x0801,
    TIME_EVENT_SVC_REGISTER     = 0x0802,

    /* System Events */
    TIME_EVENT_SYS_BOOT         = 0x0900,
    TIME_EVENT_SYS_SHUTDOWN     = 0x0901,
    TIME_EVENT_SYS_CONFIG       = 0x0902,

} phantom_time_event_t;

/* Event categories for filtering */
#define TIME_CAT_FILE       0x0100
#define TIME_CAT_DIR        0x0200
#define TIME_CAT_PROC       0x0300
#define TIME_CAT_USER       0x0400
#define TIME_CAT_PKG        0x0500
#define TIME_CAT_NET        0x0600
#define TIME_CAT_GOV        0x0700
#define TIME_CAT_SVC        0x0800
#define TIME_CAT_SYS        0x0900
#define TIME_CAT_MASK       0xFF00

/* ─────────────────────────────────────────────────────────────────────────────
 * Temporal Event Record
 *
 * Every event in the system creates one of these records.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_time_event_record {
    uint64_t event_id;              /* Unique monotonic event ID */
    uint64_t timestamp_ns;          /* Nanosecond-precision timestamp */
    phantom_time_event_t type;      /* Event type */

    /* Actor (who performed this action) */
    uint32_t actor_uid;             /* User ID */
    uint64_t actor_pid;             /* Process ID */
    char actor_name[64];            /* Username */

    /* Subject (what was affected) */
    char subject_path[PHANTOM_TIME_MAX_PATH];   /* File/dir path */
    uint64_t subject_id;            /* Process/socket/etc ID */
    char subject_name[256];         /* Name */

    /* Change details */
    uint64_t old_version;           /* Previous geological version */
    uint64_t new_version;           /* New geological version */
    uint64_t bytes_affected;        /* Size of change */
    phantom_hash_t content_hash;    /* Hash of content after change */

    /* Context */
    char description[512];          /* Human-readable description */
    char metadata[256];             /* JSON metadata for event-specific data */

    /* Geological reference */
    uint64_t geo_view_id;           /* GeoFS view this belongs to */
    uint64_t geo_offset;            /* Offset in geology file */

} phantom_time_event_record_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Time Point & Range
 *
 * For specifying points and ranges in history.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    TIME_POINT_ABSOLUTE,        /* Specific timestamp */
    TIME_POINT_RELATIVE,        /* Relative to now (e.g., -1 hour) */
    TIME_POINT_EVENT_ID,        /* Specific event ID */
    TIME_POINT_GEO_VIEW,        /* Specific geological view */
    TIME_POINT_BOOT,            /* System boot time */
    TIME_POINT_NOW,             /* Current moment */
} phantom_time_point_type_t;

typedef struct phantom_time_point {
    phantom_time_point_type_t type;
    union {
        uint64_t timestamp_ns;      /* For ABSOLUTE */
        int64_t offset_ns;          /* For RELATIVE (negative = past) */
        uint64_t event_id;          /* For EVENT_ID */
        uint64_t geo_view;          /* For GEO_VIEW */
    };
} phantom_time_point_t;

typedef struct phantom_time_range {
    phantom_time_point_t start;
    phantom_time_point_t end;
} phantom_time_range_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Query Structures
 * ───────────────────────────────────────────────────────────────────────────── */

/* Query filter options */
typedef struct phantom_time_filter {
    /* Time range */
    phantom_time_range_t time_range;

    /* Event type filter (0 = all) */
    uint32_t event_types;           /* Bitmask of categories */
    phantom_time_event_t specific_event;  /* Or specific type */

    /* Actor filter */
    int filter_by_user;
    uint32_t user_id;
    char username[64];

    /* Subject filter */
    int filter_by_path;
    char path_pattern[PHANTOM_TIME_MAX_PATH];  /* Glob pattern */

    int filter_by_process;
    uint64_t process_id;

    /* Result limits */
    uint32_t max_results;
    uint32_t offset;                /* For pagination */

    /* Sort order */
    int ascending;                  /* 0 = newest first */

} phantom_time_filter_t;

/* Query result */
typedef struct phantom_time_result {
    phantom_time_event_record_t *events;
    uint32_t count;
    uint32_t total_matches;         /* Total matching (may be > count if limited) */
    uint64_t query_time_ns;         /* How long query took */
} phantom_time_result_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Diff Structures
 *
 * For comparing system states at different times.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    DIFF_ADDED,                 /* New file/entity created */
    DIFF_MODIFIED,              /* Content changed */
    DIFF_HIDDEN,                /* Made dormant/hidden */
    DIFF_REVEALED,              /* Restored from dormant */
    DIFF_MOVED,                 /* Renamed/moved */
} phantom_diff_type_t;

typedef struct phantom_diff_entry {
    phantom_diff_type_t type;
    char path[PHANTOM_TIME_MAX_PATH];
    char old_path[PHANTOM_TIME_MAX_PATH];   /* For MOVED */

    /* Version info */
    uint64_t old_version;
    uint64_t new_version;
    uint64_t old_size;
    uint64_t new_size;

    /* Content hashes */
    phantom_hash_t old_hash;
    phantom_hash_t new_hash;

    /* Timestamps */
    uint64_t old_timestamp;
    uint64_t new_timestamp;

    /* Who made this change */
    uint32_t modified_by_uid;
    char modified_by_name[64];

} phantom_diff_entry_t;

typedef struct phantom_diff_result {
    phantom_diff_entry_t *entries;
    uint32_t count;

    /* Summary stats */
    uint32_t added_count;
    uint32_t modified_count;
    uint32_t hidden_count;
    uint32_t revealed_count;
    uint32_t moved_count;

    int64_t total_size_change;

} phantom_diff_result_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Snapshot Structure
 *
 * Represents the system state at a specific point in time.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_snapshot {
    uint64_t snapshot_id;
    uint64_t timestamp_ns;
    uint64_t geo_view_id;

    /* State counts */
    uint64_t file_count;
    uint64_t dir_count;
    uint64_t process_count;
    uint64_t user_count;
    uint64_t connection_count;

    /* Size totals */
    uint64_t total_size;
    uint64_t geology_size;

    /* Hash of system state */
    phantom_hash_t state_hash;

    /* Optional label */
    char label[256];

} phantom_snapshot_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Timeline Index
 *
 * In-memory index for fast temporal queries.
 * ───────────────────────────────────────────────────────────────────────────── */

#define TIME_INDEX_BUCKET_COUNT     4096
#define TIME_INDEX_BLOCK_SIZE       1024

typedef struct phantom_time_index_entry {
    uint64_t event_id;
    uint64_t timestamp_ns;
    uint64_t geo_offset;            /* Where in geology file */
    phantom_time_event_t type;
    uint32_t actor_uid;
    uint64_t subject_hash;          /* Hash of subject path for fast lookup */
} phantom_time_index_entry_t;

typedef struct phantom_time_index_block {
    phantom_time_index_entry_t entries[TIME_INDEX_BLOCK_SIZE];
    uint32_t count;
    struct phantom_time_index_block *next;
} phantom_time_index_block_t;

typedef struct phantom_time_index {
    uint64_t magic;
    uint64_t version;

    /* Main timeline (sorted by timestamp) */
    phantom_time_index_block_t *timeline_head;
    phantom_time_index_block_t *timeline_tail;
    uint64_t event_count;
    uint64_t next_event_id;

    /* Hash buckets for fast path lookup */
    phantom_time_index_block_t *path_buckets[TIME_INDEX_BUCKET_COUNT];

    /* Hash buckets for fast user lookup */
    phantom_time_index_block_t *user_buckets[TIME_INDEX_BUCKET_COUNT];

    /* Named snapshots */
    phantom_snapshot_t *snapshots;
    uint32_t snapshot_count;
    uint32_t snapshot_capacity;

    /* Time bounds */
    uint64_t earliest_timestamp;
    uint64_t latest_timestamp;

} phantom_time_index_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Temporal Engine Context
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_temporal {
    /* Index */
    phantom_time_index_t index;

    /* Configuration */
    int auto_index;                 /* Automatically index new events */
    int cache_enabled;              /* Enable query caching */
    uint32_t cache_size;            /* Max cached queries */

    /* Statistics */
    uint64_t total_events;
    uint64_t total_queries;
    uint64_t cache_hits;
    uint64_t cache_misses;

    /* References */
    struct phantom_kernel *kernel;
    void *geofs_volume;             /* GeoFS volume */

    /* State */
    int initialized;
    int indexing;                   /* Currently building index */

} phantom_temporal_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Result Codes
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    TIME_OK = 0,
    TIME_ERR_INVALID = -1,
    TIME_ERR_NOT_FOUND = -2,
    TIME_ERR_RANGE = -3,            /* Invalid time range */
    TIME_ERR_NOMEM = -4,
    TIME_ERR_IO = -5,
    TIME_ERR_INDEX = -6,            /* Index corruption */
    TIME_ERR_BUSY = -7,             /* Indexing in progress */
} phantom_time_result_code_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Temporal Engine API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Lifecycle */
int phantom_time_init(phantom_temporal_t *temporal, struct phantom_kernel *kernel);
void phantom_time_shutdown(phantom_temporal_t *temporal);

/* Index Management */
int phantom_time_build_index(phantom_temporal_t *temporal);
int phantom_time_update_index(phantom_temporal_t *temporal);
int phantom_time_save_index(phantom_temporal_t *temporal, const char *path);
int phantom_time_load_index(phantom_temporal_t *temporal, const char *path);

/* Event Recording */
int phantom_time_record_event(phantom_temporal_t *temporal,
                               phantom_time_event_t type,
                               uint32_t actor_uid, uint64_t actor_pid,
                               const char *subject_path, uint64_t subject_id,
                               const char *description);

/* Querying */
int phantom_time_query(phantom_temporal_t *temporal,
                        const phantom_time_filter_t *filter,
                        phantom_time_result_t *result);
void phantom_time_free_result(phantom_time_result_t *result);

/* Time Travel */
int phantom_time_at(phantom_temporal_t *temporal,
                     phantom_time_point_t point,
                     phantom_snapshot_t *snapshot_out);
int phantom_time_diff(phantom_temporal_t *temporal,
                       phantom_time_point_t from,
                       phantom_time_point_t to,
                       phantom_diff_result_t *diff_out);
void phantom_time_free_diff(phantom_diff_result_t *diff);

/* File History */
int phantom_time_file_history(phantom_temporal_t *temporal,
                               const char *path,
                               phantom_time_result_t *result);
int phantom_time_file_at(phantom_temporal_t *temporal,
                          const char *path,
                          phantom_time_point_t point,
                          void **content_out, size_t *size_out);

/* Process History */
int phantom_time_process_history(phantom_temporal_t *temporal,
                                  uint64_t pid,
                                  phantom_time_result_t *result);

/* User Activity */
int phantom_time_user_activity(phantom_temporal_t *temporal,
                                uint32_t uid,
                                phantom_time_range_t range,
                                phantom_time_result_t *result);

/* Snapshots */
int phantom_time_create_snapshot(phantom_temporal_t *temporal,
                                  const char *label,
                                  phantom_snapshot_t *snapshot_out);
int phantom_time_list_snapshots(phantom_temporal_t *temporal,
                                 phantom_snapshot_t **list, uint32_t *count);
int phantom_time_restore_view(phantom_temporal_t *temporal,
                               uint64_t snapshot_id);

/* Forensics */
int phantom_time_trace_changes(phantom_temporal_t *temporal,
                                const char *path,
                                phantom_time_range_t range,
                                phantom_time_result_t *result);
int phantom_time_find_author(phantom_temporal_t *temporal,
                              const char *path,
                              uint64_t version,
                              uint32_t *uid_out, char *username_out);

/* Utility */
const char *phantom_time_event_string(phantom_time_event_t event);
const char *phantom_time_result_string(phantom_time_result_code_t code);
int phantom_time_parse_point(const char *str, phantom_time_point_t *point);
int phantom_time_format_timestamp(uint64_t timestamp_ns, char *buf, size_t size);
uint64_t phantom_time_now_ns(void);

/* Helper: Create common time points */
phantom_time_point_t phantom_time_point_now(void);
phantom_time_point_t phantom_time_point_ago(int64_t seconds);
phantom_time_point_t phantom_time_point_at(time_t timestamp);
phantom_time_point_t phantom_time_point_boot(void);

/* Print functions */
void phantom_time_print_event(const phantom_time_event_record_t *event);
void phantom_time_print_result(const phantom_time_result_t *result);
void phantom_time_print_diff(const phantom_diff_result_t *diff);
void phantom_time_print_snapshot(const phantom_snapshot_t *snapshot);
void phantom_time_print_stats(phantom_temporal_t *temporal);

#endif /* PHANTOM_TIME_H */
