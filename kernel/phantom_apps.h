/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM APPLICATIONS
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Built-in applications for PhantomOS:
 * - Notes: Versioned note-taking with full history
 * - File Viewer: Safe read-only file viewing
 * - System Monitor: Real-time system statistics
 *
 * All apps follow Phantom principles:
 * - Nothing is ever deleted, only archived
 * - Full audit trail of all actions
 * - AI assistance where helpful
 */

#ifndef PHANTOM_APPS_H
#define PHANTOM_APPS_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "phantom.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────── */

#define APP_MAX_NOTES           1024
#define APP_MAX_NOTE_SIZE       65536
#define APP_MAX_TITLE           256
#define APP_MAX_TAGS            512
#define APP_NOTE_PATH           "/home/.apps/notes"
#define APP_VIEWER_CACHE        "/var/cache/viewer"

/* ─────────────────────────────────────────────────────────────────────────────
 * App Result Codes
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    APP_OK = 0,
    APP_ERR_INVALID = -1,
    APP_ERR_NOT_FOUND = -2,
    APP_ERR_NOMEM = -3,
    APP_ERR_FULL = -4,
    APP_ERR_IO = -5,
    APP_ERR_PERMISSION = -6,
    APP_ERR_FORMAT = -7,
} phantom_app_result_t;

/* ═══════════════════════════════════════════════════════════════════════════════
 * NOTES APP
 *
 * A note-taking application where every edit is preserved in geology.
 * Features:
 * - Create, edit, view notes
 * - Full version history (time-travel through edits)
 * - Tags and search
 * - AI summarization
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Note state - notes are never deleted */
typedef enum {
    NOTE_STATE_ACTIVE = 0,      /* Normal, visible note */
    NOTE_STATE_ARCHIVED,        /* User "deleted" - hidden but preserved */
    NOTE_STATE_PINNED,          /* Pinned to top */
} phantom_note_state_t;

/* A single version of a note */
typedef struct phantom_note_version {
    uint64_t version_id;
    time_t created_at;
    char content[APP_MAX_NOTE_SIZE];
    size_t content_len;
    char edit_summary[256];     /* What changed */
} phantom_note_version_t;

/* A note with its history */
typedef struct phantom_note {
    uint64_t note_id;
    char title[APP_MAX_TITLE];
    char tags[APP_MAX_TAGS];    /* Comma-separated */
    phantom_note_state_t state;

    /* Current content */
    char content[APP_MAX_NOTE_SIZE];
    size_t content_len;

    /* Timestamps */
    time_t created_at;
    time_t modified_at;
    time_t archived_at;         /* If archived */

    /* Version history */
    phantom_note_version_t *versions;
    uint32_t version_count;
    uint32_t version_capacity;
    uint64_t current_version;

    /* AI analysis */
    char summary[1024];         /* AI-generated summary */
    char keywords[256];         /* Extracted keywords */
    int ai_analyzed;

    /* Stats */
    uint32_t view_count;
    uint32_t edit_count;

} phantom_note_t;

/* Notes application context */
typedef struct phantom_notes_app {
    phantom_note_t **notes;
    uint32_t note_count;
    uint32_t note_capacity;
    uint64_t next_note_id;
    uint64_t next_version_id;

    /* Search index */
    char last_search[256];
    uint64_t *search_results;
    uint32_t search_count;

    /* Statistics */
    uint64_t total_notes_created;
    uint64_t total_edits;
    uint64_t total_characters;

    /* References */
    struct phantom_kernel *kernel;
    void *geofs_volume;

    int initialized;
} phantom_notes_app_t;

/* Notes API */
int phantom_notes_init(phantom_notes_app_t *app, struct phantom_kernel *kernel);
void phantom_notes_shutdown(phantom_notes_app_t *app);

int phantom_notes_create(phantom_notes_app_t *app, const char *title,
                         const char *content, uint64_t *note_id_out);
int phantom_notes_edit(phantom_notes_app_t *app, uint64_t note_id,
                       const char *new_content, const char *edit_summary);
int phantom_notes_rename(phantom_notes_app_t *app, uint64_t note_id,
                         const char *new_title);
int phantom_notes_tag(phantom_notes_app_t *app, uint64_t note_id,
                      const char *tags);
int phantom_notes_archive(phantom_notes_app_t *app, uint64_t note_id);
int phantom_notes_restore(phantom_notes_app_t *app, uint64_t note_id);
int phantom_notes_pin(phantom_notes_app_t *app, uint64_t note_id, int pinned);

phantom_note_t *phantom_notes_get(phantom_notes_app_t *app, uint64_t note_id);
int phantom_notes_get_version(phantom_notes_app_t *app, uint64_t note_id,
                              uint64_t version_id, phantom_note_version_t **version_out);
int phantom_notes_list(phantom_notes_app_t *app, phantom_note_t ***notes_out,
                       uint32_t *count_out, int include_archived);
int phantom_notes_search(phantom_notes_app_t *app, const char *query,
                         phantom_note_t ***results_out, uint32_t *count_out);

void phantom_notes_print(const phantom_note_t *note);
void phantom_notes_print_list(phantom_notes_app_t *app);
void phantom_notes_print_history(const phantom_note_t *note);

/* ═══════════════════════════════════════════════════════════════════════════════
 * FILE VIEWER APP
 *
 * A safe, read-only file viewer supporting multiple formats.
 * Features:
 * - Text files with syntax highlighting hints
 * - Image metadata (dimensions, format)
 * - Binary file hex dump
 * - File information and statistics
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Supported file types */
typedef enum {
    VIEWER_TYPE_TEXT = 0,
    VIEWER_TYPE_CODE,           /* Source code */
    VIEWER_TYPE_IMAGE,
    VIEWER_TYPE_BINARY,
    VIEWER_TYPE_DOCUMENT,       /* PDF, etc - metadata only */
    VIEWER_TYPE_UNKNOWN,
} phantom_viewer_type_t;

/* File information */
typedef struct phantom_file_info {
    char path[4096];
    char name[256];
    char extension[32];
    phantom_viewer_type_t type;

    /* Size and dates */
    uint64_t size;
    time_t created_at;
    time_t modified_at;
    time_t accessed_at;

    /* Content info */
    uint32_t line_count;        /* For text files */
    uint32_t word_count;
    uint32_t char_count;
    char encoding[32];          /* UTF-8, ASCII, etc */
    char mime_type[64];

    /* For images */
    uint32_t width;
    uint32_t height;
    uint32_t depth;             /* Bits per pixel */

    /* Hash for integrity */
    phantom_hash_t content_hash;

} phantom_file_info_t;

/* Viewer context */
typedef struct phantom_viewer_app {
    /* Current file */
    phantom_file_info_t current_file;
    char *content;              /* File content (text) or hex dump (binary) */
    size_t content_size;
    int file_loaded;

    /* View state */
    uint32_t scroll_offset;     /* Line offset for text */
    uint32_t lines_per_page;
    int show_line_numbers;
    int word_wrap;

    /* History of viewed files */
    char **view_history;
    uint32_t history_count;
    uint32_t history_capacity;

    /* Statistics */
    uint64_t files_viewed;
    uint64_t bytes_viewed;

    /* References */
    struct phantom_kernel *kernel;
    struct vfs_context *vfs;

    int initialized;
} phantom_viewer_app_t;

/* Viewer API */
int phantom_viewer_init(phantom_viewer_app_t *app, struct phantom_kernel *kernel,
                        struct vfs_context *vfs);
void phantom_viewer_shutdown(phantom_viewer_app_t *app);

int phantom_viewer_open(phantom_viewer_app_t *app, const char *path);
void phantom_viewer_close(phantom_viewer_app_t *app);

int phantom_viewer_get_info(phantom_viewer_app_t *app, phantom_file_info_t *info_out);
int phantom_viewer_get_content(phantom_viewer_app_t *app, char **content_out,
                               size_t *size_out);
int phantom_viewer_get_lines(phantom_viewer_app_t *app, uint32_t start_line,
                             uint32_t count, char **lines_out);
int phantom_viewer_get_hex(phantom_viewer_app_t *app, uint64_t offset,
                           uint32_t bytes, char **hex_out);

void phantom_viewer_print_info(const phantom_file_info_t *info);
void phantom_viewer_print_content(phantom_viewer_app_t *app, uint32_t max_lines);
void phantom_viewer_print_hex(phantom_viewer_app_t *app, uint32_t max_bytes);

/* ═══════════════════════════════════════════════════════════════════════════════
 * SYSTEM MONITOR APP
 *
 * Real-time system statistics and monitoring.
 * Features:
 * - Process list with CPU/memory usage
 * - Memory statistics
 * - Geology (storage) statistics
 * - Network statistics
 * - Governor activity log
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Process info for monitor */
typedef struct phantom_proc_info {
    phantom_pid_t pid;
    char name[256];
    process_state_t state;
    time_t start_time;
    uint64_t cpu_time_ms;       /* Total CPU time used */
    uint64_t memory_bytes;      /* Memory allocated */
    float cpu_percent;          /* Current CPU usage */
    float mem_percent;          /* Memory percentage */
} phantom_proc_info_t;

/* Memory statistics */
typedef struct phantom_mem_stats {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t cached_bytes;
    float usage_percent;

    /* Per-subsystem breakdown */
    uint64_t kernel_bytes;
    uint64_t process_bytes;
    uint64_t vfs_bytes;
    uint64_t geology_bytes;
} phantom_mem_stats_t;

/* Geology (storage) statistics */
typedef struct phantom_geo_stats {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    float usage_percent;

    uint64_t total_operations;
    uint64_t total_views;
    uint64_t active_view_id;

    /* Recent activity */
    uint64_t ops_per_minute;
    uint64_t bytes_written_recent;
} phantom_geo_stats_t;

/* Network statistics */
typedef struct phantom_net_stats {
    int network_enabled;
    uint32_t active_connections;
    uint32_t total_connections;

    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t packets_sent;
    uint64_t packets_received;

    /* Rate (per second) */
    uint64_t send_rate;
    uint64_t recv_rate;
} phantom_net_stats_t;

/* Governor statistics */
typedef struct phantom_gov_stats {
    uint64_t total_evaluations;
    uint64_t approvals;
    uint64_t denials;
    float approval_rate;

    uint32_t threat_level;
    char last_action[256];
    time_t last_evaluation;
} phantom_gov_stats_t;

/* System monitor context */
typedef struct phantom_monitor_app {
    /* Current statistics */
    phantom_mem_stats_t mem_stats;
    phantom_geo_stats_t geo_stats;
    phantom_net_stats_t net_stats;
    phantom_gov_stats_t gov_stats;

    /* Process list */
    phantom_proc_info_t *processes;
    uint32_t process_count;
    uint32_t process_capacity;

    /* System info */
    time_t boot_time;
    uint64_t uptime_seconds;
    char hostname[64];
    char version[64];

    /* Refresh settings */
    uint32_t refresh_interval_ms;
    time_t last_refresh;

    /* Historical data (for graphs) */
    float *cpu_history;         /* Last N samples */
    float *mem_history;
    uint32_t history_size;
    uint32_t history_index;

    /* References */
    struct phantom_kernel *kernel;
    struct vfs_context *vfs;

    int initialized;
} phantom_monitor_app_t;

/* Monitor API */
int phantom_monitor_init(phantom_monitor_app_t *app, struct phantom_kernel *kernel,
                         struct vfs_context *vfs);
void phantom_monitor_shutdown(phantom_monitor_app_t *app);

int phantom_monitor_refresh(phantom_monitor_app_t *app);
int phantom_monitor_get_processes(phantom_monitor_app_t *app,
                                  phantom_proc_info_t **procs_out,
                                  uint32_t *count_out);
int phantom_monitor_get_memory(phantom_monitor_app_t *app, phantom_mem_stats_t *stats_out);
int phantom_monitor_get_geology(phantom_monitor_app_t *app, phantom_geo_stats_t *stats_out);
int phantom_monitor_get_network(phantom_monitor_app_t *app, phantom_net_stats_t *stats_out);
int phantom_monitor_get_governor(phantom_monitor_app_t *app, phantom_gov_stats_t *stats_out);

void phantom_monitor_print_summary(phantom_monitor_app_t *app);
void phantom_monitor_print_processes(phantom_monitor_app_t *app);
void phantom_monitor_print_memory(phantom_monitor_app_t *app);
void phantom_monitor_print_geology(phantom_monitor_app_t *app);
void phantom_monitor_print_network(phantom_monitor_app_t *app);
void phantom_monitor_print_governor(phantom_monitor_app_t *app);

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility Functions
 * ───────────────────────────────────────────────────────────────────────────── */

const char *phantom_app_result_string(phantom_app_result_t code);
const char *phantom_viewer_type_string(phantom_viewer_type_t type);
const char *phantom_note_state_string(phantom_note_state_t state);

#endif /* PHANTOM_APPS_H */
