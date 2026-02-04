/*
 * ============================================================================
 *                         PHANTOM BACKUP SYSTEM
 *                     "Preservation Through Replication"
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

#include "phantom_backup.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Security: Shell Escape Function
 * ─────────────────────────────────────────────────────────────────────────────
 * Safely escapes a path for use in shell commands by wrapping in single quotes
 * and escaping any embedded single quotes. This prevents command injection.
 */
static int shell_escape_path(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size < 3) return -1;

    size_t in_len = strlen(input);
    size_t out_idx = 0;

    /* Start with opening single quote */
    output[out_idx++] = '\'';

    for (size_t i = 0; i < in_len && out_idx < output_size - 2; i++) {
        if (input[i] == '\'') {
            /* Replace ' with '\'' (end quote, escaped quote, start quote) */
            if (out_idx + 4 >= output_size - 1) return -1;
            output[out_idx++] = '\'';
            output[out_idx++] = '\\';
            output[out_idx++] = '\'';
            output[out_idx++] = '\'';
        } else {
            output[out_idx++] = input[i];
        }
    }

    if (out_idx >= output_size - 1) return -1;
    output[out_idx++] = '\'';
    output[out_idx] = '\0';

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility Functions
 * ───────────────────────────────────────────────────────────────────────────── */

const char *phantom_backup_type_name(phantom_backup_type_t type) {
    switch (type) {
        case BACKUP_TYPE_FULL:        return "Full System";
        case BACKUP_TYPE_INCREMENTAL: return "Incremental";
        case BACKUP_TYPE_SELECTIVE:   return "Selective";
        case BACKUP_TYPE_GEOFS:       return "GeoFS Volumes";
        case BACKUP_TYPE_PODS:        return "PhantomPods";
        case BACKUP_TYPE_CONFIG:      return "Configuration";
        default:                      return "Unknown";
    }
}

const char *phantom_backup_state_name(phantom_backup_state_t state) {
    switch (state) {
        case BACKUP_STATE_IDLE:        return "Idle";
        case BACKUP_STATE_PREPARING:   return "Preparing";
        case BACKUP_STATE_RUNNING:     return "Running";
        case BACKUP_STATE_COMPRESSING: return "Compressing";
        case BACKUP_STATE_VERIFYING:   return "Verifying";
        case BACKUP_STATE_COMPLETED:   return "Completed";
        case BACKUP_STATE_FAILED:      return "Failed";
        case BACKUP_STATE_CANCELLED:   return "Cancelled";
        default:                       return "Unknown";
    }
}

const char *phantom_backup_compression_name(phantom_backup_compression_t compression) {
    switch (compression) {
        case BACKUP_COMPRESSION_NONE:  return "None";
        case BACKUP_COMPRESSION_GZIP:  return "gzip";
        case BACKUP_COMPRESSION_BZIP2: return "bzip2";
        case BACKUP_COMPRESSION_XZ:    return "xz";
        default:                       return "Unknown";
    }
}

static size_t get_directory_size(const char *path) {
    size_t total = 0;
    DIR *dir = opendir(path);
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[PHANTOM_BACKUP_MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                total += get_directory_size(full_path);
            } else {
                total += st.st_size;
            }
        }
    }

    closedir(dir);
    return total;
}

size_t phantom_backup_calculate_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;

    if (S_ISDIR(st.st_mode)) {
        return get_directory_size(path);
    } else {
        return st.st_size;
    }
}

int phantom_backup_space_available(const char *path, size_t *available_bytes) {
    /* Simple implementation - check with statvfs in production */
    if (available_bytes) {
        *available_bytes = 10ULL * 1024 * 1024 * 1024;  /* Assume 10GB available */
    }
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * System Initialization
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_backup_init(phantom_backup_system_t *system, const char *backup_root) {
    if (!system) return -1;

    memset(system, 0, sizeof(phantom_backup_system_t));

    /* Set paths */
    if (backup_root) {
        strncpy(system->backup_root, backup_root, PHANTOM_BACKUP_MAX_PATH - 1);
    } else {
        strncpy(system->backup_root, "/var/phantom/backups", PHANTOM_BACKUP_MAX_PATH - 1);
    }

    strncpy(system->geofs_path, "/var/phantom/geofs", PHANTOM_BACKUP_MAX_PATH - 1);
    strncpy(system->pods_path, "/var/phantom/pods", PHANTOM_BACKUP_MAX_PATH - 1);
    strncpy(system->config_path, "/etc/phantom", PHANTOM_BACKUP_MAX_PATH - 1);

    system->next_backup_id = 1;
    system->default_compression = BACKUP_COMPRESSION_GZIP;
    system->auto_verify = 1;

    /* Create backup directory if it doesn't exist */
    mkdir(system->backup_root, 0755);

    printf("[PhantomBackup] Initialized at %s\n", system->backup_root);
    return 0;
}

void phantom_backup_shutdown(phantom_backup_system_t *system) {
    if (!system) return;

    /* Cancel any running job */
    if (system->current_job) {
        phantom_backup_cancel(system->current_job);
        free(system->current_job);
        system->current_job = NULL;
    }

    printf("[PhantomBackup] Shutdown complete. %d backups preserved.\n", system->backup_count);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Default Backup Items
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_backup_get_default_items(phantom_backup_type_t type,
                                     phantom_backup_item_t *items,
                                     int max_items) {
    if (!items || max_items < 1) return 0;

    int count = 0;

    switch (type) {
        case BACKUP_TYPE_FULL:
            if (count < max_items) {
                strncpy(items[count].name, "GeoFS Volumes", PHANTOM_BACKUP_MAX_NAME - 1);
                strncpy(items[count].path, "/var/phantom/geofs", PHANTOM_BACKUP_MAX_PATH - 1);
                items[count].enabled = 1;
                items[count].is_directory = 1;
                count++;
            }
            if (count < max_items) {
                strncpy(items[count].name, "PhantomPods", PHANTOM_BACKUP_MAX_NAME - 1);
                strncpy(items[count].path, "/var/phantom/pods", PHANTOM_BACKUP_MAX_PATH - 1);
                items[count].enabled = 1;
                items[count].is_directory = 1;
                count++;
            }
            if (count < max_items) {
                strncpy(items[count].name, "Configuration", PHANTOM_BACKUP_MAX_NAME - 1);
                strncpy(items[count].path, "/etc/phantom", PHANTOM_BACKUP_MAX_PATH - 1);
                items[count].enabled = 1;
                items[count].is_directory = 1;
                count++;
            }
            if (count < max_items) {
                strncpy(items[count].name, "User Data", PHANTOM_BACKUP_MAX_NAME - 1);
                strncpy(items[count].path, "/home", PHANTOM_BACKUP_MAX_PATH - 1);
                items[count].enabled = 1;
                items[count].is_directory = 1;
                count++;
            }
            break;

        case BACKUP_TYPE_GEOFS:
            if (count < max_items) {
                strncpy(items[count].name, "GeoFS Volumes", PHANTOM_BACKUP_MAX_NAME - 1);
                strncpy(items[count].path, "/var/phantom/geofs", PHANTOM_BACKUP_MAX_PATH - 1);
                items[count].enabled = 1;
                items[count].is_directory = 1;
                count++;
            }
            break;

        case BACKUP_TYPE_PODS:
            if (count < max_items) {
                strncpy(items[count].name, "PhantomPods", PHANTOM_BACKUP_MAX_NAME - 1);
                strncpy(items[count].path, "/var/phantom/pods", PHANTOM_BACKUP_MAX_PATH - 1);
                items[count].enabled = 1;
                items[count].is_directory = 1;
                count++;
            }
            break;

        case BACKUP_TYPE_CONFIG:
            if (count < max_items) {
                strncpy(items[count].name, "System Configuration", PHANTOM_BACKUP_MAX_NAME - 1);
                strncpy(items[count].path, "/etc/phantom", PHANTOM_BACKUP_MAX_PATH - 1);
                items[count].enabled = 1;
                items[count].is_directory = 1;
                count++;
            }
            break;

        default:
            break;
    }

    /* Calculate sizes */
    for (int i = 0; i < count; i++) {
        items[i].size_bytes = phantom_backup_calculate_size(items[i].path);
    }

    return count;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Backup Job Creation
 * ───────────────────────────────────────────────────────────────────────────── */

phantom_backup_job_t *phantom_backup_create_job(phantom_backup_system_t *system,
                                                 const char *name,
                                                 phantom_backup_type_t type,
                                                 const char *destination) {
    if (!system || !name || !destination) return NULL;

    phantom_backup_job_t *job = calloc(1, sizeof(phantom_backup_job_t));
    if (!job) return NULL;

    /* Initialize record */
    job->record.id = system->next_backup_id++;
    strncpy(job->record.name, name, PHANTOM_BACKUP_MAX_NAME - 1);
    strncpy(job->record.destination, destination, PHANTOM_BACKUP_MAX_PATH - 1);
    job->record.type = type;
    job->record.compression = system->default_compression;
    job->record.created = time(NULL);
    job->record.state = BACKUP_STATE_IDLE;

    /* Load default items based on type */
    job->item_count = phantom_backup_get_default_items(type, job->items, PHANTOM_BACKUP_MAX_ITEMS);

    strncpy(job->status_message, "Backup job created", sizeof(job->status_message) - 1);

    printf("[PhantomBackup] Created job '%s' (Type: %s)\n", name, phantom_backup_type_name(type));

    return job;
}

int phantom_backup_add_item(phantom_backup_job_t *job, const char *name, const char *path) {
    if (!job || !name || !path) return -1;
    if (job->item_count >= PHANTOM_BACKUP_MAX_ITEMS) return -1;

    phantom_backup_item_t *item = &job->items[job->item_count];
    strncpy(item->name, name, PHANTOM_BACKUP_MAX_NAME - 1);
    strncpy(item->path, path, PHANTOM_BACKUP_MAX_PATH - 1);
    item->enabled = 1;

    struct stat st;
    if (stat(path, &st) == 0) {
        item->is_directory = S_ISDIR(st.st_mode);
        item->size_bytes = phantom_backup_calculate_size(path);
    }

    job->item_count++;
    return 0;
}

int phantom_backup_set_compression(phantom_backup_job_t *job, phantom_backup_compression_t compression) {
    if (!job) return -1;
    job->record.compression = compression;
    return 0;
}

int phantom_backup_set_encryption(phantom_backup_job_t *job, const char *password) {
    if (!job) return -1;
    job->record.encrypted = (password != NULL && strlen(password) > 0);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Backup Execution
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_backup_start(phantom_backup_system_t *sys, phantom_backup_job_t *job) {
    if (!sys || !job) return -1;
    if (sys->current_job) return -1;  /* Job already running */

    job->record.state = BACKUP_STATE_PREPARING;
    job->start_time = time(NULL);
    sys->current_job = job;

    /* Calculate total size */
    job->record.total_bytes = 0;
    for (int i = 0; i < job->item_count; i++) {
        if (job->items[i].enabled) {
            job->record.total_bytes += job->items[i].size_bytes;
        }
    }

    /* Generate archive filename */
    char timestamp[32];
    struct tm *tm_info = localtime(&job->record.created);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);

    const char *ext = "";
    switch (job->record.compression) {
        case BACKUP_COMPRESSION_GZIP: ext = ".tar.gz"; break;
        case BACKUP_COMPRESSION_BZIP2: ext = ".tar.bz2"; break;
        case BACKUP_COMPRESSION_XZ: ext = ".tar.xz"; break;
        default: ext = ".tar"; break;
    }

    snprintf(job->record.archive_path, PHANTOM_BACKUP_MAX_PATH,
             "%.300s/%.100s_%.60s%s",
             job->record.destination, job->record.name, timestamp, ext);

    job->record.state = BACKUP_STATE_RUNNING;
    strncpy(job->status_message, "Creating backup archive...", sizeof(job->status_message) - 1);

    /* Build tar command with shell-safe path escaping */
    char command[8192];
    char escaped_archive[1024];
    const char *compress_flag = "";
    switch (job->record.compression) {
        case BACKUP_COMPRESSION_GZIP: compress_flag = "z"; break;
        case BACKUP_COMPRESSION_BZIP2: compress_flag = "j"; break;
        case BACKUP_COMPRESSION_XZ: compress_flag = "J"; break;
        default: compress_flag = ""; break;
    }

    if (shell_escape_path(job->record.archive_path, escaped_archive, sizeof(escaped_archive)) != 0) {
        job->record.state = BACKUP_STATE_FAILED;
        strncpy(job->status_message, "Archive path too long or invalid", sizeof(job->status_message) - 1);
        sys->current_job = NULL;
        return -1;
    }

    snprintf(command, sizeof(command), "tar -c%sf %s",
             compress_flag, escaped_archive);

    for (int i = 0; i < job->item_count; i++) {
        if (job->items[i].enabled && access(job->items[i].path, R_OK) == 0) {
            char escaped_item[1024];
            if (shell_escape_path(job->items[i].path, escaped_item, sizeof(escaped_item)) == 0) {
                strncat(command, " ", sizeof(command) - strlen(command) - 1);
                strncat(command, escaped_item, sizeof(command) - strlen(command) - 1);
            }
        }
    }

    strncat(command, " 2>/dev/null", sizeof(command) - strlen(command) - 1);

    printf("[PhantomBackup] Executing: %s\n", command);

    /* Execute backup */
    int result = system(command);

    if (result == 0) {
        job->record.state = BACKUP_STATE_COMPLETED;
        job->record.completed = time(NULL);
        strncpy(job->status_message, "Backup completed successfully", sizeof(job->status_message) - 1);

        /* Get compressed size */
        struct stat st;
        if (stat(job->record.archive_path, &st) == 0) {
            job->record.compressed_bytes = st.st_size;
        }

        /* Add to sys backup history */
        if (sys->backup_count < PHANTOM_BACKUP_MAX_BACKUPS) {
            memcpy(&sys->backups[sys->backup_count], &job->record, sizeof(phantom_backup_record_t));
            sys->backup_count++;
            sys->total_backups_created++;
            sys->total_backup_size += job->record.compressed_bytes;
        }

        printf("[PhantomBackup] Backup '%s' completed: %zu bytes compressed to %zu bytes\n",
               job->record.name, job->record.total_bytes, job->record.compressed_bytes);
    } else {
        job->record.state = BACKUP_STATE_FAILED;
        strncpy(job->status_message, "Backup failed", sizeof(job->status_message) - 1);
        printf("[PhantomBackup] Backup '%s' failed\n", job->record.name);
    }

    sys->current_job = NULL;
    return (result == 0) ? 0 : -1;
}

int phantom_backup_cancel(phantom_backup_job_t *job) {
    if (!job) return -1;

    job->cancel_requested = 1;
    if (job->worker_pid > 0) {
        kill(job->worker_pid, SIGTERM);
    }

    job->record.state = BACKUP_STATE_CANCELLED;
    strncpy(job->status_message, "Backup cancelled by user", sizeof(job->status_message) - 1);

    return 0;
}

int phantom_backup_verify(phantom_backup_system_t *sys, phantom_backup_record_t *backup) {
    if (!sys || !backup) return -1;

    /* Check if archive exists and is readable */
    if (access(backup->archive_path, R_OK) != 0) {
        return -1;
    }

    /* Verify tar archive integrity with shell-safe escaping */
    char command[PHANTOM_BACKUP_MAX_PATH + 256];
    char escaped_archive[1024];
    const char *compress_flag = "";
    switch (backup->compression) {
        case BACKUP_COMPRESSION_GZIP: compress_flag = "z"; break;
        case BACKUP_COMPRESSION_BZIP2: compress_flag = "j"; break;
        case BACKUP_COMPRESSION_XZ: compress_flag = "J"; break;
        default: compress_flag = ""; break;
    }

    if (shell_escape_path(backup->archive_path, escaped_archive, sizeof(escaped_archive)) != 0) {
        return -1;
    }

    snprintf(command, sizeof(command), "tar -t%sf %s >/dev/null 2>&1",
             compress_flag, escaped_archive);

    int result = system(command);
    backup->verified = (result == 0);

    return (result == 0) ? 0 : -1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Restore Operations
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_backup_restore(phantom_backup_system_t *sys,
                           phantom_backup_record_t *backup,
                           const char *restore_path) {
    if (!sys || !backup || !restore_path) return -1;

    /* Verify backup first */
    if (phantom_backup_verify(sys, backup) != 0) {
        printf("[PhantomBackup] Backup verification failed\n");
        return -1;
    }

    /* Build tar extract command with shell-safe escaping */
    char command[4096];
    char escaped_archive[1024];
    char escaped_restore[1024];
    const char *compress_flag = "";
    switch (backup->compression) {
        case BACKUP_COMPRESSION_GZIP: compress_flag = "z"; break;
        case BACKUP_COMPRESSION_BZIP2: compress_flag = "j"; break;
        case BACKUP_COMPRESSION_XZ: compress_flag = "J"; break;
        default: compress_flag = ""; break;
    }

    if (shell_escape_path(backup->archive_path, escaped_archive, sizeof(escaped_archive)) != 0 ||
        shell_escape_path(restore_path, escaped_restore, sizeof(escaped_restore)) != 0) {
        printf("[PhantomBackup] Path escaping failed\n");
        return -1;
    }

    snprintf(command, sizeof(command), "tar -x%sf %s -C %s 2>/dev/null",
             compress_flag, escaped_archive, escaped_restore);

    printf("[PhantomBackup] Restoring: %s\n", command);

    int result = system(command);

    if (result == 0) {
        sys->total_restores_performed++;
        printf("[PhantomBackup] Restore completed successfully\n");
    } else {
        printf("[PhantomBackup] Restore failed\n");
    }

    return (result == 0) ? 0 : -1;
}

int phantom_backup_restore_item(phantom_backup_system_t *sys,
                                phantom_backup_record_t *backup,
                                const char *item_path,
                                const char *restore_path) {
    if (!sys || !backup || !item_path || !restore_path) return -1;

    /* Build tar extract command with shell-safe escaping */
    char command[4096];
    char escaped_archive[1024];
    char escaped_restore[1024];
    char escaped_item[1024];
    const char *compress_flag = "";
    switch (backup->compression) {
        case BACKUP_COMPRESSION_GZIP: compress_flag = "z"; break;
        case BACKUP_COMPRESSION_BZIP2: compress_flag = "j"; break;
        case BACKUP_COMPRESSION_XZ: compress_flag = "J"; break;
        default: compress_flag = ""; break;
    }

    if (shell_escape_path(backup->archive_path, escaped_archive, sizeof(escaped_archive)) != 0 ||
        shell_escape_path(restore_path, escaped_restore, sizeof(escaped_restore)) != 0 ||
        shell_escape_path(item_path, escaped_item, sizeof(escaped_item)) != 0) {
        printf("[PhantomBackup] Path escaping failed\n");
        return -1;
    }

    snprintf(command, sizeof(command), "tar -x%sf %s -C %s %s 2>/dev/null",
             compress_flag, escaped_archive, escaped_restore, escaped_item);

    printf("[PhantomBackup] Restoring item: %s\n", item_path);

    int result = system(command);
    return (result == 0) ? 0 : -1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Quick Backup Functions
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_backup_quick_full(phantom_backup_system_t *sys, const char *destination) {
    if (!sys || !destination) return -1;

    phantom_backup_job_t *job = phantom_backup_create_job(sys, "QuickFull",
                                                          BACKUP_TYPE_FULL, destination);
    if (!job) return -1;

    int result = phantom_backup_start(sys, job);
    free(job);
    return result;
}

int phantom_backup_quick_geofs(phantom_backup_system_t *sys, const char *destination) {
    if (!sys || !destination) return -1;

    phantom_backup_job_t *job = phantom_backup_create_job(sys, "QuickGeoFS",
                                                          BACKUP_TYPE_GEOFS, destination);
    if (!job) return -1;

    int result = phantom_backup_start(sys, job);
    free(job);
    return result;
}

int phantom_backup_quick_pods(phantom_backup_system_t *sys, const char *destination) {
    if (!sys || !destination) return -1;

    phantom_backup_job_t *job = phantom_backup_create_job(sys, "QuickPods",
                                                          BACKUP_TYPE_PODS, destination);
    if (!job) return -1;

    int result = phantom_backup_start(sys, job);
    free(job);
    return result;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Query Functions
 * ───────────────────────────────────────────────────────────────────────────── */

phantom_backup_record_t *phantom_backup_find_by_id(phantom_backup_system_t *sys, uint32_t id) {
    if (!sys) return NULL;
    for (int i = 0; i < sys->backup_count; i++) {
        if (sys->backups[i].id == id) {
            return &sys->backups[i];
        }
    }
    return NULL;
}

phantom_backup_record_t *phantom_backup_find_by_name(phantom_backup_system_t *sys, const char *name) {
    if (!sys || !name) return NULL;
    for (int i = 0; i < sys->backup_count; i++) {
        if (strcmp(sys->backups[i].name, name) == 0) {
            return &sys->backups[i];
        }
    }
    return NULL;
}

phantom_backup_record_t *phantom_backup_get_latest(phantom_backup_system_t *sys) {
    if (!sys || sys->backup_count == 0) return NULL;
    return &sys->backups[sys->backup_count - 1];
}

int phantom_backup_get_history(phantom_backup_system_t *sys,
                               phantom_backup_record_t **backups,
                               int max_count) {
    if (!sys || !backups) return 0;

    int count = (sys->backup_count < max_count) ? sys->backup_count : max_count;
    *backups = sys->backups;
    return count;
}
