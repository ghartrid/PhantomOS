/*
 * ==============================================================================
 *                       PHANTOM STORAGE MANAGEMENT
 *                    "To Create, Not To Destroy"
 * ==============================================================================
 *
 * Storage quota management, space monitoring, and backup/archival system.
 * Because even eternal preservation needs thoughtful stewardship.
 *
 * Features:
 * - User and system quotas
 * - Space usage monitoring with warnings
 * - Automatic alerts at configurable thresholds
 * - Backup/export of geology layers to external storage
 * - Archive old views to free active space
 */

#ifndef PHANTOM_STORAGE_H
#define PHANTOM_STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* Forward declarations */
struct phantom_kernel;
struct vfs_context;
typedef struct geofs_volume geofs_volume_t;

/* -----------------------------------------------------------------------------
 * Storage Thresholds and Limits
 * ----------------------------------------------------------------------------- */

/* Warning levels (percentage of capacity used) */
#define STORAGE_WARN_NORMAL     0    /* Below 70% - all good */
#define STORAGE_WARN_ADVISORY   70   /* 70-85% - advisory warning */
#define STORAGE_WARN_WARNING    85   /* 85-95% - warning, consider cleanup */
#define STORAGE_WARN_CRITICAL   95   /* 95-99% - critical, action required */
#define STORAGE_WARN_FULL       99   /* 99%+ - effectively full */

/* Default quota values (in bytes) */
#define STORAGE_QUOTA_UNLIMITED     UINT64_MAX
#define STORAGE_QUOTA_DEFAULT_USER  (1ULL * 1024 * 1024 * 1024)  /* 1 GB */
#define STORAGE_QUOTA_DEFAULT_ADMIN (10ULL * 1024 * 1024 * 1024) /* 10 GB */

/* Backup chunk size */
#define STORAGE_BACKUP_CHUNK_SIZE   (64 * 1024)  /* 64 KB chunks */

/* -----------------------------------------------------------------------------
 * Data Structures
 * ----------------------------------------------------------------------------- */

/* Storage usage statistics */
typedef struct phantom_storage_stats {
    /* Content region */
    uint64_t content_total_bytes;      /* Total content region size */
    uint64_t content_used_bytes;       /* Used content bytes */
    uint64_t content_available_bytes;  /* Available content bytes */

    /* Reference region */
    uint64_t ref_total_count;          /* Total ref slots */
    uint64_t ref_used_count;           /* Used ref slots */

    /* View region */
    uint64_t view_total_count;         /* Total view slots */
    uint64_t view_used_count;          /* Used view slots */

    /* Derived statistics */
    uint64_t total_files;              /* Total file references */
    uint64_t hidden_files;             /* Hidden (soft-deleted) files */
    uint64_t unique_content_blocks;    /* Deduplicated content blocks */
    uint64_t dedup_savings_bytes;      /* Bytes saved by deduplication */

    /* Percentages */
    float content_percent_used;
    float ref_percent_used;
    float view_percent_used;
    float overall_percent_used;        /* Highest of the three */

    /* Warning level */
    int warning_level;

    /* Timestamp */
    time_t last_updated;
} phantom_storage_stats_t;

/* User quota */
typedef struct phantom_quota {
    uint32_t uid;                      /* User ID */
    uint64_t limit_bytes;              /* Maximum bytes allowed */
    uint64_t used_bytes;               /* Currently used bytes */
    uint64_t limit_files;              /* Maximum files allowed (0 = unlimited) */
    uint64_t used_files;               /* Currently used files */
    int      enabled;                  /* Quota enforcement enabled */
    time_t   last_warning;             /* Last time user was warned */
} phantom_quota_t;

/* Storage warning callback */
typedef void (*storage_warning_callback_t)(int level, const char *message, void *user_data);

/* Backup progress callback */
typedef void (*backup_progress_callback_t)(uint64_t bytes_written, uint64_t total_bytes,
                                           const char *current_item, void *user_data);

/* Backup options */
typedef struct phantom_backup_options {
    const char *destination_path;      /* Where to write backup */
    int include_hidden;                /* Include hidden files */
    int include_all_views;             /* Include all views or just current */
    uint64_t max_view_age_days;        /* Archive views older than this (0 = all) */
    int compress;                      /* Compress output */
    backup_progress_callback_t progress_cb;
    void *progress_user_data;
} phantom_backup_options_t;

/* Backup result */
typedef struct phantom_backup_result {
    int success;
    uint64_t bytes_written;
    uint64_t files_backed_up;
    uint64_t views_backed_up;
    char error_message[256];
    time_t completed_at;
} phantom_backup_result_t;

/* Archive operation for freeing space */
typedef struct phantom_archive_options {
    uint64_t views_to_archive;         /* Number of oldest views to archive */
    const char *archive_path;          /* Where to store archive */
    int remove_archived_content;       /* Remove content only in archived views */
} phantom_archive_options_t;

/* Storage manager context */
typedef struct phantom_storage_manager {
    struct phantom_kernel *kernel;
    geofs_volume_t *volume;

    /* Quotas */
    phantom_quota_t *quotas;
    size_t quota_count;
    size_t quota_capacity;
    int quotas_enabled;

    /* Monitoring */
    phantom_storage_stats_t current_stats;
    int monitoring_enabled;
    int check_interval_seconds;
    time_t last_check;

    /* Warning callbacks */
    storage_warning_callback_t warning_cb;
    void *warning_user_data;
    int last_warning_level;

    /* Configuration */
    uint64_t default_user_quota;
    uint64_t default_admin_quota;
} phantom_storage_manager_t;

/* -----------------------------------------------------------------------------
 * Initialization and Shutdown
 * ----------------------------------------------------------------------------- */

/* Initialize storage manager */
int phantom_storage_init(phantom_storage_manager_t *mgr,
                         struct phantom_kernel *kernel,
                         geofs_volume_t *volume);

/* Shutdown storage manager */
void phantom_storage_shutdown(phantom_storage_manager_t *mgr);

/* -----------------------------------------------------------------------------
 * Space Monitoring
 * ----------------------------------------------------------------------------- */

/* Get current storage statistics */
int phantom_storage_get_stats(phantom_storage_manager_t *mgr,
                              phantom_storage_stats_t *stats);

/* Check storage and trigger warnings if needed */
int phantom_storage_check(phantom_storage_manager_t *mgr);

/* Get warning level from percentage */
int phantom_storage_get_warning_level(float percent_used);

/* Get warning level description */
const char *phantom_storage_warning_str(int level);

/* Set warning callback */
void phantom_storage_set_warning_callback(phantom_storage_manager_t *mgr,
                                          storage_warning_callback_t cb,
                                          void *user_data);

/* Format bytes as human-readable string */
void phantom_storage_format_bytes(uint64_t bytes, char *buf, size_t buf_size);

/* Print storage report to stdout */
void phantom_storage_print_report(phantom_storage_manager_t *mgr);

/* -----------------------------------------------------------------------------
 * Quota Management
 * ----------------------------------------------------------------------------- */

/* Enable/disable quota enforcement */
void phantom_storage_enable_quotas(phantom_storage_manager_t *mgr, int enable);

/* Set quota for a user */
int phantom_storage_set_quota(phantom_storage_manager_t *mgr,
                              uint32_t uid,
                              uint64_t limit_bytes,
                              uint64_t limit_files);

/* Get quota for a user */
int phantom_storage_get_quota(phantom_storage_manager_t *mgr,
                              uint32_t uid,
                              phantom_quota_t *quota);

/* Check if operation would exceed quota */
int phantom_storage_check_quota(phantom_storage_manager_t *mgr,
                                uint32_t uid,
                                uint64_t additional_bytes);

/* Update usage for a user */
int phantom_storage_update_usage(phantom_storage_manager_t *mgr,
                                 uint32_t uid,
                                 int64_t bytes_delta,
                                 int64_t files_delta);

/* Get quota usage report for user */
int phantom_storage_quota_report(phantom_storage_manager_t *mgr,
                                 uint32_t uid,
                                 char *report,
                                 size_t report_size);

/* -----------------------------------------------------------------------------
 * Backup and Archive
 * ----------------------------------------------------------------------------- */

/* Create a full backup of the geology */
int phantom_storage_backup(phantom_storage_manager_t *mgr,
                           const phantom_backup_options_t *options,
                           phantom_backup_result_t *result);

/* Restore from a backup */
int phantom_storage_restore(phantom_storage_manager_t *mgr,
                            const char *backup_path,
                            int merge_mode);  /* 0 = replace, 1 = merge */

/* Archive old views to external storage */
int phantom_storage_archive_views(phantom_storage_manager_t *mgr,
                                  const phantom_archive_options_t *options);

/* List available backups in a directory */
int phantom_storage_list_backups(const char *directory,
                                 char **backup_names,
                                 size_t max_backups,
                                 size_t *count);

/* Get backup information */
int phantom_storage_backup_info(const char *backup_path,
                                uint64_t *size,
                                time_t *created,
                                uint64_t *file_count,
                                uint64_t *view_count);

/* -----------------------------------------------------------------------------
 * Space Reclamation (Phantom-safe)
 * ----------------------------------------------------------------------------- */

/*
 * Note: These operations don't truly delete data - they move it to archives
 * or external storage, maintaining the "To Create, Not To Destroy" philosophy.
 */

/* Calculate reclaimable space (hidden files, old views) */
int phantom_storage_calc_reclaimable(phantom_storage_manager_t *mgr,
                                     uint64_t *hidden_bytes,
                                     uint64_t *old_view_bytes,
                                     uint64_t *dedup_candidates);

/* Export and archive hidden files (moves to archive, frees active space) */
int phantom_storage_archive_hidden(phantom_storage_manager_t *mgr,
                                   const char *archive_path);

/* Compact by exporting old views (preserves data, frees active space) */
int phantom_storage_compact_views(phantom_storage_manager_t *mgr,
                                  uint64_t keep_recent_days,
                                  const char *archive_path);

#endif /* PHANTOM_STORAGE_H */
