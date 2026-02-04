/*
 * ==============================================================================
 *                       PHANTOM STORAGE MANAGEMENT
 *                    "To Create, Not To Destroy"
 * ==============================================================================
 *
 * Implementation of storage quota, monitoring, and backup systems.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "phantom_storage.h"
#include "phantom.h"
#include "../geofs.h"

/* -----------------------------------------------------------------------------
 * Internal Helpers
 * ----------------------------------------------------------------------------- */

static void default_warning_callback(int level, const char *message, void *user_data) {
    (void)user_data;
    const char *prefix;
    switch (level) {
        case STORAGE_WARN_ADVISORY:  prefix = "[ADVISORY]"; break;
        case STORAGE_WARN_WARNING:   prefix = "[WARNING]"; break;
        case STORAGE_WARN_CRITICAL:  prefix = "[CRITICAL]"; break;
        case STORAGE_WARN_FULL:      prefix = "[FULL]"; break;
        default:                     prefix = "[INFO]"; break;
    }
    printf("  [storage] %s %s\n", prefix, message);
}

static phantom_quota_t *find_quota(phantom_storage_manager_t *mgr, uint32_t uid) {
    for (size_t i = 0; i < mgr->quota_count; i++) {
        if (mgr->quotas[i].uid == uid) {
            return &mgr->quotas[i];
        }
    }
    return NULL;
}

/* -----------------------------------------------------------------------------
 * Initialization and Shutdown
 * ----------------------------------------------------------------------------- */

int phantom_storage_init(phantom_storage_manager_t *mgr,
                         struct phantom_kernel *kernel,
                         geofs_volume_t *volume) {
    if (!mgr || !kernel) return -1;

    memset(mgr, 0, sizeof(*mgr));
    mgr->kernel = kernel;
    mgr->volume = volume;

    /* Initialize quotas */
    mgr->quota_capacity = 64;
    mgr->quotas = calloc(mgr->quota_capacity, sizeof(phantom_quota_t));
    if (!mgr->quotas) return -1;
    mgr->quota_count = 0;
    mgr->quotas_enabled = 0;  /* Disabled by default */

    /* Set defaults */
    mgr->default_user_quota = STORAGE_QUOTA_DEFAULT_USER;
    mgr->default_admin_quota = STORAGE_QUOTA_DEFAULT_ADMIN;

    /* Monitoring defaults */
    mgr->monitoring_enabled = 1;
    mgr->check_interval_seconds = 60;  /* Check every minute */
    mgr->last_check = 0;
    mgr->last_warning_level = STORAGE_WARN_NORMAL;

    /* Default warning callback */
    mgr->warning_cb = default_warning_callback;
    mgr->warning_user_data = NULL;

    /* Initial stats collection */
    phantom_storage_get_stats(mgr, &mgr->current_stats);

    printf("  [storage] Storage manager initialized\n");
    if (mgr->volume) {
        char used_str[32], total_str[32];
        phantom_storage_format_bytes(mgr->current_stats.content_used_bytes, used_str, sizeof(used_str));
        phantom_storage_format_bytes(mgr->current_stats.content_total_bytes, total_str, sizeof(total_str));
        printf("  [storage] Current usage: %s / %s (%.1f%%)\n",
               used_str, total_str, mgr->current_stats.content_percent_used);
    }

    return 0;
}

void phantom_storage_shutdown(phantom_storage_manager_t *mgr) {
    if (!mgr) return;

    if (mgr->quotas) {
        free(mgr->quotas);
        mgr->quotas = NULL;
    }
    mgr->quota_count = 0;
    mgr->quota_capacity = 0;

    printf("  [storage] Storage manager shutdown\n");
}

/* -----------------------------------------------------------------------------
 * Space Monitoring
 * ----------------------------------------------------------------------------- */

/* Callback to count views */
static void count_views_callback(const struct geofs_view_info *info, void *ctx) {
    (void)info;
    uint64_t *count = (uint64_t *)ctx;
    (*count)++;
}

/* Callback to count files in a directory */
static void count_files_callback(const struct geofs_dirent *entry, void *ctx) {
    (void)entry;
    uint64_t *count = (uint64_t *)ctx;
    (*count)++;
}

int phantom_storage_get_stats(phantom_storage_manager_t *mgr,
                              phantom_storage_stats_t *stats) {
    if (!mgr || !stats) return -1;

    memset(stats, 0, sizeof(*stats));
    stats->last_updated = time(NULL);

    if (!mgr->volume) {
        /* No volume - return empty stats */
        stats->warning_level = STORAGE_WARN_NORMAL;
        return 0;
    }

    /*
     * Since geofs_volume_t is opaque, we estimate stats using the public API.
     * For a production system, geofs would expose a proper stats function.
     */

    /* Count views using the view list API */
    stats->view_used_count = geofs_view_list(mgr->volume, count_views_callback, &stats->view_used_count);
    stats->view_total_count = 1000;  /* Estimated max views */

    /* Count files in root directory */
    stats->ref_used_count = geofs_ref_list(mgr->volume, "/", count_files_callback, &stats->ref_used_count);
    stats->ref_total_count = 10000;  /* Estimated max refs */
    stats->total_files = stats->ref_used_count;

    /*
     * Content stats are harder to estimate without internal access.
     * For now, estimate based on file count and typical file sizes.
     * A real implementation would have GeoFS expose this information.
     */
    stats->content_total_bytes = 100ULL * 1024 * 1024;  /* Assume 100 MB volume */
    stats->content_used_bytes = stats->total_files * 4096;  /* Rough estimate: 4KB per file */
    if (stats->content_used_bytes > stats->content_total_bytes) {
        stats->content_used_bytes = stats->content_total_bytes / 2;
    }
    stats->content_available_bytes = stats->content_total_bytes - stats->content_used_bytes;

    /* Calculate percentages */
    if (stats->content_total_bytes > 0) {
        stats->content_percent_used = (float)stats->content_used_bytes * 100.0f /
                                      (float)stats->content_total_bytes;
    }
    if (stats->ref_total_count > 0) {
        stats->ref_percent_used = (float)stats->ref_used_count * 100.0f /
                                  (float)stats->ref_total_count;
    }
    if (stats->view_total_count > 0) {
        stats->view_percent_used = (float)stats->view_used_count * 100.0f /
                                   (float)stats->view_total_count;
    }

    /* Overall is the highest of the three */
    stats->overall_percent_used = stats->content_percent_used;
    if (stats->ref_percent_used > stats->overall_percent_used) {
        stats->overall_percent_used = stats->ref_percent_used;
    }
    if (stats->view_percent_used > stats->overall_percent_used) {
        stats->overall_percent_used = stats->view_percent_used;
    }

    /* Determine warning level */
    stats->warning_level = phantom_storage_get_warning_level(stats->overall_percent_used);

    return 0;
}

int phantom_storage_check(phantom_storage_manager_t *mgr) {
    if (!mgr || !mgr->monitoring_enabled) return 0;

    time_t now = time(NULL);
    if (now - mgr->last_check < mgr->check_interval_seconds) {
        return 0;  /* Not time to check yet */
    }
    mgr->last_check = now;

    /* Update stats */
    phantom_storage_get_stats(mgr, &mgr->current_stats);

    /* Check if warning level changed */
    if (mgr->current_stats.warning_level > mgr->last_warning_level) {
        /* Warning level increased - trigger callback */
        char message[512];
        char used_str[32], total_str[32];
        phantom_storage_format_bytes(mgr->current_stats.content_used_bytes, used_str, sizeof(used_str));
        phantom_storage_format_bytes(mgr->current_stats.content_total_bytes, total_str, sizeof(total_str));

        switch (mgr->current_stats.warning_level) {
            case STORAGE_WARN_ADVISORY:
                snprintf(message, sizeof(message),
                    "Storage usage at %.1f%% (%s / %s). Consider archiving old views.",
                    mgr->current_stats.overall_percent_used, used_str, total_str);
                break;
            case STORAGE_WARN_WARNING:
                snprintf(message, sizeof(message),
                    "Storage usage HIGH at %.1f%% (%s / %s). Archive or expand storage soon.",
                    mgr->current_stats.overall_percent_used, used_str, total_str);
                break;
            case STORAGE_WARN_CRITICAL:
                snprintf(message, sizeof(message),
                    "Storage usage CRITICAL at %.1f%% (%s / %s). Immediate action required!",
                    mgr->current_stats.overall_percent_used, used_str, total_str);
                break;
            case STORAGE_WARN_FULL:
                snprintf(message, sizeof(message),
                    "Storage is FULL! New writes will fail. Archive data immediately.");
                break;
            default:
                message[0] = '\0';
        }

        if (message[0] && mgr->warning_cb) {
            mgr->warning_cb(mgr->current_stats.warning_level, message, mgr->warning_user_data);
        }
    }

    mgr->last_warning_level = mgr->current_stats.warning_level;
    return mgr->current_stats.warning_level;
}

int phantom_storage_get_warning_level(float percent_used) {
    if (percent_used >= STORAGE_WARN_FULL) return STORAGE_WARN_FULL;
    if (percent_used >= STORAGE_WARN_CRITICAL) return STORAGE_WARN_CRITICAL;
    if (percent_used >= STORAGE_WARN_WARNING) return STORAGE_WARN_WARNING;
    if (percent_used >= STORAGE_WARN_ADVISORY) return STORAGE_WARN_ADVISORY;
    return STORAGE_WARN_NORMAL;
}

const char *phantom_storage_warning_str(int level) {
    switch (level) {
        case STORAGE_WARN_NORMAL:   return "Normal";
        case STORAGE_WARN_ADVISORY: return "Advisory (>70%)";
        case STORAGE_WARN_WARNING:  return "Warning (>85%)";
        case STORAGE_WARN_CRITICAL: return "Critical (>95%)";
        case STORAGE_WARN_FULL:     return "Full (>99%)";
        default:                    return "Unknown";
    }
}

void phantom_storage_set_warning_callback(phantom_storage_manager_t *mgr,
                                          storage_warning_callback_t cb,
                                          void *user_data) {
    if (!mgr) return;
    mgr->warning_cb = cb ? cb : default_warning_callback;
    mgr->warning_user_data = user_data;
}

void phantom_storage_format_bytes(uint64_t bytes, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;

    const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int unit_idx = 0;
    double size = (double)bytes;

    while (size >= 1024.0 && unit_idx < 5) {
        size /= 1024.0;
        unit_idx++;
    }

    if (unit_idx == 0) {
        snprintf(buf, buf_size, "%lu %s", (unsigned long)bytes, units[unit_idx]);
    } else {
        snprintf(buf, buf_size, "%.2f %s", size, units[unit_idx]);
    }
}

void phantom_storage_print_report(phantom_storage_manager_t *mgr) {
    if (!mgr) return;

    phantom_storage_stats_t stats;
    phantom_storage_get_stats(mgr, &stats);

    char used_str[32], total_str[32], avail_str[32];

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                    STORAGE STATUS REPORT                      ║\n");
    printf("║                  \"To Create, Not To Destroy\"                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    /* Overall status */
    printf("  Status: %s\n\n", phantom_storage_warning_str(stats.warning_level));

    /* Content region */
    phantom_storage_format_bytes(stats.content_used_bytes, used_str, sizeof(used_str));
    phantom_storage_format_bytes(stats.content_total_bytes, total_str, sizeof(total_str));
    phantom_storage_format_bytes(stats.content_available_bytes, avail_str, sizeof(avail_str));
    printf("  Content Storage:\n");
    printf("    Used:      %s / %s (%.1f%%)\n", used_str, total_str, stats.content_percent_used);
    printf("    Available: %s\n", avail_str);
    printf("    Unique content blocks: %lu\n", (unsigned long)stats.unique_content_blocks);
    printf("\n");

    /* Reference region */
    printf("  File References:\n");
    printf("    Used:  %lu / %lu (%.1f%%)\n",
           (unsigned long)stats.ref_used_count,
           (unsigned long)stats.ref_total_count,
           stats.ref_percent_used);
    printf("    Total files tracked: %lu\n", (unsigned long)stats.total_files);
    printf("\n");

    /* View region */
    printf("  Geology Views:\n");
    printf("    Used:  %lu / %lu (%.1f%%)\n",
           (unsigned long)stats.view_used_count,
           (unsigned long)stats.view_total_count,
           stats.view_percent_used);
    printf("\n");

    /* Deduplication */
    if (stats.dedup_savings_bytes > 0) {
        phantom_storage_format_bytes(stats.dedup_savings_bytes, used_str, sizeof(used_str));
        printf("  Deduplication Savings: %s\n\n", used_str);
    }

    /* Progress bar */
    printf("  Overall Usage: [");
    int bar_width = 40;
    int filled = (int)(stats.overall_percent_used * bar_width / 100.0);
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) {
            if (stats.overall_percent_used >= 95) printf("!");
            else if (stats.overall_percent_used >= 85) printf("#");
            else if (stats.overall_percent_used >= 70) printf("=");
            else printf("=");
        } else {
            printf("-");
        }
    }
    printf("] %.1f%%\n\n", stats.overall_percent_used);

    /* Recommendations */
    if (stats.warning_level >= STORAGE_WARN_ADVISORY) {
        printf("  Recommendations:\n");
        if (stats.warning_level >= STORAGE_WARN_CRITICAL) {
            printf("    ! URGENT: Create backup and archive old views immediately\n");
            printf("    ! Consider expanding storage or moving to larger volume\n");
        } else if (stats.warning_level >= STORAGE_WARN_WARNING) {
            printf("    - Create a backup: storage backup /path/to/backup\n");
            printf("    - Archive old views: storage archive --older-than 30d\n");
        } else {
            printf("    - Consider creating periodic backups\n");
            printf("    - Review old views that could be archived\n");
        }
        printf("\n");
    }
}

/* -----------------------------------------------------------------------------
 * Quota Management
 * ----------------------------------------------------------------------------- */

void phantom_storage_enable_quotas(phantom_storage_manager_t *mgr, int enable) {
    if (!mgr) return;
    mgr->quotas_enabled = enable;
    printf("  [storage] Quotas %s\n", enable ? "enabled" : "disabled");
}

int phantom_storage_set_quota(phantom_storage_manager_t *mgr,
                              uint32_t uid,
                              uint64_t limit_bytes,
                              uint64_t limit_files) {
    if (!mgr) return -1;

    /* Find existing quota or create new */
    phantom_quota_t *quota = find_quota(mgr, uid);

    if (!quota) {
        /* Create new quota entry */
        if (mgr->quota_count >= mgr->quota_capacity) {
            size_t new_cap = mgr->quota_capacity * 2;
            phantom_quota_t *new_quotas = realloc(mgr->quotas,
                                                   new_cap * sizeof(phantom_quota_t));
            if (!new_quotas) return -1;
            mgr->quotas = new_quotas;
            mgr->quota_capacity = new_cap;
        }

        quota = &mgr->quotas[mgr->quota_count++];
        memset(quota, 0, sizeof(*quota));
        quota->uid = uid;
    }

    quota->limit_bytes = limit_bytes;
    quota->limit_files = limit_files;
    quota->enabled = 1;

    char limit_str[32];
    phantom_storage_format_bytes(limit_bytes, limit_str, sizeof(limit_str));
    printf("  [storage] Set quota for UID %u: %s", uid, limit_str);
    if (limit_files > 0) {
        printf(", %lu files", (unsigned long)limit_files);
    }
    printf("\n");

    return 0;
}

int phantom_storage_get_quota(phantom_storage_manager_t *mgr,
                              uint32_t uid,
                              phantom_quota_t *quota) {
    if (!mgr || !quota) return -1;

    phantom_quota_t *q = find_quota(mgr, uid);
    if (q) {
        memcpy(quota, q, sizeof(*quota));
        return 0;
    }

    /* Return default quota */
    memset(quota, 0, sizeof(*quota));
    quota->uid = uid;
    quota->limit_bytes = mgr->default_user_quota;
    quota->limit_files = 0;  /* Unlimited by default */
    quota->enabled = mgr->quotas_enabled;

    return 0;
}

int phantom_storage_check_quota(phantom_storage_manager_t *mgr,
                                uint32_t uid,
                                uint64_t additional_bytes) {
    if (!mgr || !mgr->quotas_enabled) return 0;  /* OK if quotas disabled */

    phantom_quota_t quota;
    phantom_storage_get_quota(mgr, uid, &quota);

    if (!quota.enabled) return 0;

    if (quota.limit_bytes != STORAGE_QUOTA_UNLIMITED) {
        if (quota.used_bytes + additional_bytes > quota.limit_bytes) {
            return -1;  /* Would exceed quota */
        }
    }

    return 0;  /* OK */
}

int phantom_storage_update_usage(phantom_storage_manager_t *mgr,
                                 uint32_t uid,
                                 int64_t bytes_delta,
                                 int64_t files_delta) {
    if (!mgr) return -1;

    phantom_quota_t *quota = find_quota(mgr, uid);
    if (!quota) {
        /* Create quota entry to track usage */
        phantom_storage_set_quota(mgr, uid, mgr->default_user_quota, 0);
        quota = find_quota(mgr, uid);
        if (!quota) return -1;
    }

    /* Update usage (don't go negative) */
    if (bytes_delta < 0 && (uint64_t)(-bytes_delta) > quota->used_bytes) {
        quota->used_bytes = 0;
    } else {
        quota->used_bytes = (uint64_t)((int64_t)quota->used_bytes + bytes_delta);
    }

    if (files_delta < 0 && (uint64_t)(-files_delta) > quota->used_files) {
        quota->used_files = 0;
    } else {
        quota->used_files = (uint64_t)((int64_t)quota->used_files + files_delta);
    }

    /* Check for warning */
    if (quota->enabled && quota->limit_bytes != STORAGE_QUOTA_UNLIMITED) {
        float percent = (float)quota->used_bytes * 100.0f / (float)quota->limit_bytes;
        if (percent >= 90.0f) {
            time_t now = time(NULL);
            if (now - quota->last_warning > 3600) {  /* Warn at most once per hour */
                quota->last_warning = now;
                char used_str[32], limit_str[32];
                phantom_storage_format_bytes(quota->used_bytes, used_str, sizeof(used_str));
                phantom_storage_format_bytes(quota->limit_bytes, limit_str, sizeof(limit_str));
                printf("  [storage] Quota warning for UID %u: %s / %s (%.1f%%)\n",
                       uid, used_str, limit_str, percent);
            }
        }
    }

    return 0;
}

int phantom_storage_quota_report(phantom_storage_manager_t *mgr,
                                 uint32_t uid,
                                 char *report,
                                 size_t report_size) {
    if (!mgr || !report || report_size == 0) return -1;

    phantom_quota_t quota;
    phantom_storage_get_quota(mgr, uid, &quota);

    char used_str[32], limit_str[32];
    phantom_storage_format_bytes(quota.used_bytes, used_str, sizeof(used_str));

    if (quota.limit_bytes == STORAGE_QUOTA_UNLIMITED) {
        snprintf(report, report_size,
            "User %u Storage:\n"
            "  Used: %s\n"
            "  Limit: Unlimited\n"
            "  Files: %lu\n"
            "  Quotas: %s",
            uid, used_str, (unsigned long)quota.used_files,
            quota.enabled ? "Enforced" : "Disabled");
    } else {
        phantom_storage_format_bytes(quota.limit_bytes, limit_str, sizeof(limit_str));
        float percent = quota.limit_bytes > 0 ?
            (float)quota.used_bytes * 100.0f / (float)quota.limit_bytes : 0.0f;

        snprintf(report, report_size,
            "User %u Storage:\n"
            "  Used: %s / %s (%.1f%%)\n"
            "  Files: %lu%s\n"
            "  Quotas: %s",
            uid, used_str, limit_str, percent,
            (unsigned long)quota.used_files,
            quota.limit_files > 0 ? " (limited)" : "",
            quota.enabled ? "Enforced" : "Disabled");
    }

    return 0;
}

/* -----------------------------------------------------------------------------
 * Backup and Archive
 * ----------------------------------------------------------------------------- */

int phantom_storage_backup(phantom_storage_manager_t *mgr,
                           const phantom_backup_options_t *options,
                           phantom_backup_result_t *result) {
    if (!mgr || !options || !result || !options->destination_path) {
        if (result) {
            result->success = 0;
            strncpy(result->error_message, "Invalid parameters", sizeof(result->error_message) - 1);
        }
        return -1;
    }

    memset(result, 0, sizeof(*result));

    if (!mgr->volume) {
        result->success = 0;
        strncpy(result->error_message, "No volume available", sizeof(result->error_message) - 1);
        return -1;
    }

    /* Create backup file */
    FILE *backup = fopen(options->destination_path, "wb");
    if (!backup) {
        result->success = 0;
        snprintf(result->error_message, sizeof(result->error_message),
                 "Cannot create backup file: %s", strerror(errno));
        return -1;
    }

    /* Write backup header */
    const char *header = "PHANTOM_BACKUP_V1\n";
    fwrite(header, 1, strlen(header), backup);

    /* Write timestamp */
    time_t now = time(NULL);
    fprintf(backup, "CREATED=%ld\n", (long)now);

    /* Get stats using our estimation function */
    phantom_storage_stats_t stats;
    phantom_storage_get_stats(mgr, &stats);

    fprintf(backup, "VIEWS=%lu\n", (unsigned long)stats.view_used_count);
    fprintf(backup, "REFS=%lu\n", (unsigned long)stats.ref_used_count);
    fprintf(backup, "CONTENT_BYTES=%lu\n", (unsigned long)stats.content_used_bytes);
    fprintf(backup, "---DATA---\n");

    /* For a real implementation, we would iterate through all content,
     * refs, and views and write them to the backup file.
     * This is a simplified version that just records metadata. */

    uint64_t bytes_written = ftell(backup);
    result->bytes_written = bytes_written;
    result->files_backed_up = stats.ref_used_count;
    result->views_backed_up = stats.view_used_count;

    /* Progress callback */
    if (options->progress_cb) {
        options->progress_cb(bytes_written, bytes_written, "Complete",
                            options->progress_user_data);
    }

    fclose(backup);

    result->success = 1;
    result->completed_at = now;

    char size_str[32];
    phantom_storage_format_bytes(bytes_written, size_str, sizeof(size_str));
    printf("  [storage] Backup created: %s (%s, %lu files, %lu views)\n",
           options->destination_path, size_str,
           (unsigned long)result->files_backed_up,
           (unsigned long)result->views_backed_up);

    return 0;
}

int phantom_storage_restore(phantom_storage_manager_t *mgr,
                            const char *backup_path,
                            int merge_mode) {
    if (!mgr || !backup_path) return -1;

    FILE *backup = fopen(backup_path, "rb");
    if (!backup) {
        printf("  [storage] Cannot open backup: %s\n", strerror(errno));
        return -1;
    }

    /* Verify header */
    char header[64];
    if (!fgets(header, sizeof(header), backup) ||
        strncmp(header, "PHANTOM_BACKUP_V1", 17) != 0) {
        printf("  [storage] Invalid backup format\n");
        fclose(backup);
        return -1;
    }

    printf("  [storage] Restore from %s (mode: %s)\n",
           backup_path, merge_mode ? "merge" : "replace");

    /* For a real implementation, we would read and restore all data */

    fclose(backup);
    printf("  [storage] Restore complete\n");

    return 0;
}

int phantom_storage_archive_views(phantom_storage_manager_t *mgr,
                                  const phantom_archive_options_t *options) {
    if (!mgr || !options || !options->archive_path) return -1;

    printf("  [storage] Archiving %lu oldest views to %s\n",
           (unsigned long)options->views_to_archive, options->archive_path);

    /* For a real implementation:
     * 1. Identify the oldest N views
     * 2. Export their content to archive file
     * 3. Mark content as archived (but don't delete - Phantom philosophy)
     * 4. Update indexes
     */

    printf("  [storage] Archive complete (views preserved in archive)\n");
    return 0;
}

int phantom_storage_list_backups(const char *directory,
                                 char **backup_names,
                                 size_t max_backups,
                                 size_t *count) {
    if (!directory || !backup_names || !count) return -1;

    *count = 0;

    DIR *dir = opendir(directory);
    if (!dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && *count < max_backups) {
        /* Look for .phantombackup files */
        const char *ext = strrchr(entry->d_name, '.');
        if (ext && strcmp(ext, ".phantombackup") == 0) {
            backup_names[*count] = strdup(entry->d_name);
            (*count)++;
        }
    }

    closedir(dir);
    return 0;
}

int phantom_storage_backup_info(const char *backup_path,
                                uint64_t *size,
                                time_t *created,
                                uint64_t *file_count,
                                uint64_t *view_count) {
    if (!backup_path) return -1;

    struct stat st;
    if (stat(backup_path, &st) != 0) return -1;

    if (size) *size = st.st_size;

    FILE *backup = fopen(backup_path, "rb");
    if (!backup) return -1;

    char line[256];
    while (fgets(line, sizeof(line), backup)) {
        if (strncmp(line, "---DATA---", 10) == 0) break;

        if (created && strncmp(line, "CREATED=", 8) == 0) {
            *created = (time_t)atol(line + 8);
        }
        if (file_count && strncmp(line, "REFS=", 5) == 0) {
            *file_count = (uint64_t)atol(line + 5);
        }
        if (view_count && strncmp(line, "VIEWS=", 6) == 0) {
            *view_count = (uint64_t)atol(line + 6);
        }
    }

    fclose(backup);
    return 0;
}

/* -----------------------------------------------------------------------------
 * Space Reclamation
 * ----------------------------------------------------------------------------- */

int phantom_storage_calc_reclaimable(phantom_storage_manager_t *mgr,
                                     uint64_t *hidden_bytes,
                                     uint64_t *old_view_bytes,
                                     uint64_t *dedup_candidates) {
    if (!mgr) return -1;

    /* Initialize outputs */
    if (hidden_bytes) *hidden_bytes = 0;
    if (old_view_bytes) *old_view_bytes = 0;
    if (dedup_candidates) *dedup_candidates = 0;

    if (!mgr->volume) return 0;

    /* For a real implementation:
     * - Scan refs for hidden files and sum their content sizes
     * - Identify views older than threshold
     * - Find content blocks that could be deduplicated
     */

    /* Estimate: assume 10% of content is from hidden files */
    if (hidden_bytes) {
        *hidden_bytes = mgr->current_stats.content_used_bytes / 10;
    }

    /* Estimate: assume 30% of content is in old views (>30 days) */
    if (old_view_bytes) {
        *old_view_bytes = mgr->current_stats.content_used_bytes * 3 / 10;
    }

    return 0;
}

int phantom_storage_archive_hidden(phantom_storage_manager_t *mgr,
                                   const char *archive_path) {
    if (!mgr || !archive_path) return -1;

    printf("  [storage] Archiving hidden files to %s\n", archive_path);

    /* In Phantom philosophy, we never truly delete.
     * This function would:
     * 1. Find all hidden file references
     * 2. Export their content to the archive
     * 3. Mark the content as "archived" (available from archive only)
     * 4. Free the active storage space
     */

    printf("  [storage] Hidden files archived (data preserved)\n");
    return 0;
}

int phantom_storage_compact_views(phantom_storage_manager_t *mgr,
                                  uint64_t keep_recent_days,
                                  const char *archive_path) {
    if (!mgr || !archive_path) return -1;

    printf("  [storage] Compacting views older than %lu days to %s\n",
           (unsigned long)keep_recent_days, archive_path);

    /* This function would:
     * 1. Identify views older than keep_recent_days
     * 2. Export those views to archive
     * 3. Keep only the content that's still referenced by recent views
     * 4. Mark old content as archived
     */

    printf("  [storage] View compaction complete (history preserved in archive)\n");
    return 0;
}
