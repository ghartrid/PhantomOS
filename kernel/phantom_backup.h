/*
 * ============================================================================
 *                         PHANTOM BACKUP SYSTEM
 *                     "Preservation Through Replication"
 * ============================================================================
 *
 * Manual backup utility for PhantomOS data preservation
 */

#ifndef PHANTOM_BACKUP_H
#define PHANTOM_BACKUP_H

#include <stdint.h>
#include <time.h>

#define PHANTOM_BACKUP_MAX_NAME 128
#define PHANTOM_BACKUP_MAX_PATH 512
#define PHANTOM_BACKUP_MAX_BACKUPS 256
#define PHANTOM_BACKUP_MAX_ITEMS 64

/* ─────────────────────────────────────────────────────────────────────────────
 * Backup Types
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    BACKUP_TYPE_FULL,           /* Complete system backup */
    BACKUP_TYPE_INCREMENTAL,    /* Only changes since last backup */
    BACKUP_TYPE_SELECTIVE,      /* User-selected items */
    BACKUP_TYPE_GEOFS,          /* GeoFS volumes only */
    BACKUP_TYPE_PODS,           /* PhantomPods only */
    BACKUP_TYPE_CONFIG          /* Configuration files only */
} phantom_backup_type_t;

typedef enum {
    BACKUP_STATE_IDLE,
    BACKUP_STATE_PREPARING,
    BACKUP_STATE_RUNNING,
    BACKUP_STATE_COMPRESSING,
    BACKUP_STATE_VERIFYING,
    BACKUP_STATE_COMPLETED,
    BACKUP_STATE_FAILED,
    BACKUP_STATE_CANCELLED
} phantom_backup_state_t;

typedef enum {
    BACKUP_COMPRESSION_NONE,
    BACKUP_COMPRESSION_GZIP,
    BACKUP_COMPRESSION_BZIP2,
    BACKUP_COMPRESSION_XZ
} phantom_backup_compression_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Backup Items
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    char name[PHANTOM_BACKUP_MAX_NAME];
    char path[PHANTOM_BACKUP_MAX_PATH];
    int enabled;
    int is_directory;
    size_t size_bytes;
} phantom_backup_item_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Backup Record
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t id;
    char name[PHANTOM_BACKUP_MAX_NAME];
    char destination[PHANTOM_BACKUP_MAX_PATH];
    phantom_backup_type_t type;
    phantom_backup_compression_t compression;

    time_t created;
    time_t completed;

    phantom_backup_state_t state;

    size_t total_bytes;
    size_t compressed_bytes;
    int item_count;

    int encrypted;
    int verified;

    char archive_path[PHANTOM_BACKUP_MAX_PATH];
    char checksum[65];  /* SHA-256 */
} phantom_backup_record_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Backup Job (Current Operation)
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    phantom_backup_record_t record;

    phantom_backup_item_t items[PHANTOM_BACKUP_MAX_ITEMS];
    int item_count;

    /* Progress tracking */
    int current_item;
    size_t bytes_processed;
    float progress_percent;

    char current_file[PHANTOM_BACKUP_MAX_PATH];
    char status_message[256];

    /* Timestamps */
    time_t start_time;
    time_t estimated_completion;

    /* Control */
    int cancel_requested;
    pid_t worker_pid;
} phantom_backup_job_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Backup System
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    char backup_root[PHANTOM_BACKUP_MAX_PATH];
    char geofs_path[PHANTOM_BACKUP_MAX_PATH];
    char pods_path[PHANTOM_BACKUP_MAX_PATH];
    char config_path[PHANTOM_BACKUP_MAX_PATH];

    phantom_backup_record_t backups[PHANTOM_BACKUP_MAX_BACKUPS];
    int backup_count;

    phantom_backup_job_t *current_job;

    uint32_t next_backup_id;

    /* Statistics */
    size_t total_backup_size;
    int total_backups_created;
    int total_restores_performed;

    /* Default settings */
    phantom_backup_compression_t default_compression;
    int auto_verify;
} phantom_backup_system_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * API Functions
 * ───────────────────────────────────────────────────────────────────────────── */

/* System initialization */
int phantom_backup_init(phantom_backup_system_t *system, const char *backup_root);
void phantom_backup_shutdown(phantom_backup_system_t *system);

/* Backup operations */
phantom_backup_job_t *phantom_backup_create_job(phantom_backup_system_t *system,
                                                 const char *name,
                                                 phantom_backup_type_t type,
                                                 const char *destination);

int phantom_backup_add_item(phantom_backup_job_t *job, const char *name, const char *path);
int phantom_backup_set_compression(phantom_backup_job_t *job, phantom_backup_compression_t compression);
int phantom_backup_set_encryption(phantom_backup_job_t *job, const char *password);

int phantom_backup_start(phantom_backup_system_t *sys, phantom_backup_job_t *job);
int phantom_backup_cancel(phantom_backup_job_t *job);
int phantom_backup_verify(phantom_backup_system_t *sys, phantom_backup_record_t *backup);

/* Restore operations */
int phantom_backup_restore(phantom_backup_system_t *sys,
                           phantom_backup_record_t *backup,
                           const char *restore_path);

int phantom_backup_restore_item(phantom_backup_system_t *sys,
                                phantom_backup_record_t *backup,
                                const char *item_path,
                                const char *restore_path);

/* Quick backup templates */
int phantom_backup_quick_full(phantom_backup_system_t *sys, const char *destination);
int phantom_backup_quick_geofs(phantom_backup_system_t *sys, const char *destination);
int phantom_backup_quick_pods(phantom_backup_system_t *sys, const char *destination);

/* Query functions */
phantom_backup_record_t *phantom_backup_find_by_id(phantom_backup_system_t *sys, uint32_t id);
phantom_backup_record_t *phantom_backup_find_by_name(phantom_backup_system_t *sys, const char *name);
phantom_backup_record_t *phantom_backup_get_latest(phantom_backup_system_t *sys);

int phantom_backup_get_history(phantom_backup_system_t *sys,
                               phantom_backup_record_t **backups,
                               int max_count);

/* Utility functions */
const char *phantom_backup_type_name(phantom_backup_type_t type);
const char *phantom_backup_state_name(phantom_backup_state_t state);
const char *phantom_backup_compression_name(phantom_backup_compression_t compression);

size_t phantom_backup_calculate_size(const char *path);
int phantom_backup_space_available(const char *path, size_t *available_bytes);

/* Default backup items */
int phantom_backup_get_default_items(phantom_backup_type_t type,
                                     phantom_backup_item_t *items,
                                     int max_items);

#endif /* PHANTOM_BACKUP_H */
