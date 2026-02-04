/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM TEMPORAL ENGINE
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Implementation of the temporal database engine.
 * Transforms immutable geology into a queryable timeline.
 */

#define _XOPEN_SOURCE 700   /* For strptime */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>

#include "phantom_time.h"
#include "phantom.h"
#include "../geofs.h"

/* Note: We use geofs.h for types but don't directly write to geology here.
 * The temporal engine maintains its own in-memory index which is fast.
 * For production, we'd persist the index to geology periodically. */

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

/* Hash function for path lookups */
static uint64_t hash_path(const char *path) {
    uint64_t hash = 5381;
    int c;
    while ((c = *path++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

/* Get current time in nanoseconds */
uint64_t phantom_time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Allocate a new index block */
static phantom_time_index_block_t *alloc_index_block(void) {
    phantom_time_index_block_t *block = calloc(1, sizeof(phantom_time_index_block_t));
    return block;
}

/* Resolve a time point to absolute nanoseconds */
static uint64_t resolve_time_point(phantom_temporal_t *temporal,
                                    phantom_time_point_t point) {
    switch (point.type) {
        case TIME_POINT_ABSOLUTE:
            return point.timestamp_ns;

        case TIME_POINT_RELATIVE:
            return phantom_time_now_ns() + point.offset_ns;

        case TIME_POINT_EVENT_ID:
            /* Search for event and return its timestamp */
            /* For now, return 0 if not found */
            return 0;

        case TIME_POINT_GEO_VIEW:
            /* Look up geological view timestamp */
            return 0;

        case TIME_POINT_BOOT:
            if (temporal->kernel) {
                return temporal->kernel->boot_time * 1000000000ULL;
            }
            return temporal->index.earliest_timestamp;

        case TIME_POINT_NOW:
        default:
            return phantom_time_now_ns();
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Lifecycle
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_time_init(phantom_temporal_t *temporal, struct phantom_kernel *kernel) {
    if (!temporal) {
        return TIME_ERR_INVALID;
    }

    memset(temporal, 0, sizeof(phantom_temporal_t));

    temporal->kernel = kernel;
    temporal->index.magic = PHANTOM_TIME_INDEX_MAGIC;
    temporal->index.version = 1;
    temporal->index.next_event_id = 1;

    /* Allocate initial timeline block */
    temporal->index.timeline_head = alloc_index_block();
    if (!temporal->index.timeline_head) {
        return TIME_ERR_NOMEM;
    }
    temporal->index.timeline_tail = temporal->index.timeline_head;

    /* Allocate initial snapshots array */
    temporal->index.snapshot_capacity = 64;
    temporal->index.snapshots = calloc(temporal->index.snapshot_capacity,
                                        sizeof(phantom_snapshot_t));
    if (!temporal->index.snapshots) {
        free(temporal->index.timeline_head);
        return TIME_ERR_NOMEM;
    }

    /* Configuration defaults */
    temporal->auto_index = 1;
    temporal->cache_enabled = 1;
    temporal->cache_size = 100;

    /* Connect to GeoFS if available */
    if (kernel && kernel->geofs_volume) {
        temporal->geofs_volume = kernel->geofs_volume;
    }

    /* Record initial timestamp */
    temporal->index.earliest_timestamp = phantom_time_now_ns();
    temporal->index.latest_timestamp = temporal->index.earliest_timestamp;

    temporal->initialized = 1;

    /* Record system init event */
    phantom_time_record_event(temporal, TIME_EVENT_SYS_BOOT,
                               0, 0, "/", 0,
                               "Temporal engine initialized");

    printf("[phantom_time] Temporal engine initialized\n");
    return TIME_OK;
}

void phantom_time_shutdown(phantom_temporal_t *temporal) {
    if (!temporal || !temporal->initialized) {
        return;
    }

    /* Record shutdown */
    phantom_time_record_event(temporal, TIME_EVENT_SYS_SHUTDOWN,
                               0, 0, "/", 0,
                               "Temporal engine shutting down");

    /* Free timeline blocks */
    phantom_time_index_block_t *block = temporal->index.timeline_head;
    while (block) {
        phantom_time_index_block_t *next = block->next;
        free(block);
        block = next;
    }

    /* Free path buckets */
    for (int i = 0; i < TIME_INDEX_BUCKET_COUNT; i++) {
        block = temporal->index.path_buckets[i];
        while (block) {
            phantom_time_index_block_t *next = block->next;
            free(block);
            block = next;
        }
    }

    /* Free user buckets */
    for (int i = 0; i < TIME_INDEX_BUCKET_COUNT; i++) {
        block = temporal->index.user_buckets[i];
        while (block) {
            phantom_time_index_block_t *next = block->next;
            free(block);
            block = next;
        }
    }

    /* Free snapshots */
    free(temporal->index.snapshots);

    temporal->initialized = 0;
    printf("[phantom_time] Temporal engine shutdown complete\n");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Event Recording
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_time_record_event(phantom_temporal_t *temporal,
                               phantom_time_event_t type,
                               uint32_t actor_uid, uint64_t actor_pid,
                               const char *subject_path, uint64_t subject_id,
                               const char *description) {
    if (!temporal || !temporal->initialized) {
        return TIME_ERR_INVALID;
    }

    /* Get current timestamp */
    uint64_t now = phantom_time_now_ns();

    /* Create index entry */
    phantom_time_index_entry_t entry = {0};
    entry.event_id = temporal->index.next_event_id++;
    entry.timestamp_ns = now;
    entry.type = type;
    entry.actor_uid = actor_uid;
    entry.subject_hash = subject_path ? hash_path(subject_path) : 0;

    /* Add to timeline */
    phantom_time_index_block_t *tail = temporal->index.timeline_tail;
    if (tail->count >= TIME_INDEX_BLOCK_SIZE) {
        /* Allocate new block */
        phantom_time_index_block_t *new_block = alloc_index_block();
        if (!new_block) {
            return TIME_ERR_NOMEM;
        }
        tail->next = new_block;
        temporal->index.timeline_tail = new_block;
        tail = new_block;
    }
    tail->entries[tail->count++] = entry;

    /* Add to path bucket */
    if (subject_path) {
        uint64_t bucket_idx = entry.subject_hash % TIME_INDEX_BUCKET_COUNT;
        phantom_time_index_block_t *bucket = temporal->index.path_buckets[bucket_idx];

        if (!bucket || bucket->count >= TIME_INDEX_BLOCK_SIZE) {
            phantom_time_index_block_t *new_block = alloc_index_block();
            if (new_block) {
                new_block->next = bucket;
                temporal->index.path_buckets[bucket_idx] = new_block;
                bucket = new_block;
            }
        }
        if (bucket && bucket->count < TIME_INDEX_BLOCK_SIZE) {
            bucket->entries[bucket->count++] = entry;
        }
    }

    /* Add to user bucket */
    {
        uint64_t bucket_idx = actor_uid % TIME_INDEX_BUCKET_COUNT;
        phantom_time_index_block_t *bucket = temporal->index.user_buckets[bucket_idx];

        if (!bucket || bucket->count >= TIME_INDEX_BLOCK_SIZE) {
            phantom_time_index_block_t *new_block = alloc_index_block();
            if (new_block) {
                new_block->next = bucket;
                temporal->index.user_buckets[bucket_idx] = new_block;
                bucket = new_block;
            }
        }
        if (bucket && bucket->count < TIME_INDEX_BLOCK_SIZE) {
            bucket->entries[bucket->count++] = entry;
        }
    }

    /* Update stats */
    temporal->index.event_count++;
    temporal->index.latest_timestamp = now;
    temporal->total_events++;

    /* Note: In production, events would be persisted to GeoFS here.
     * For now, the in-memory index is sufficient for demonstration.
     * TODO: Implement periodic index persistence to geology. */
    (void)actor_pid;
    (void)subject_id;
    (void)description;

    return TIME_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Querying
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_time_query(phantom_temporal_t *temporal,
                        const phantom_time_filter_t *filter,
                        phantom_time_result_t *result) {
    if (!temporal || !temporal->initialized || !filter || !result) {
        return TIME_ERR_INVALID;
    }

    uint64_t start_time = phantom_time_now_ns();
    memset(result, 0, sizeof(phantom_time_result_t));

    /* Resolve time range */
    uint64_t range_start = resolve_time_point(temporal, filter->time_range.start);
    uint64_t range_end = resolve_time_point(temporal, filter->time_range.end);

    if (range_start == 0) range_start = temporal->index.earliest_timestamp;
    if (range_end == 0) range_end = phantom_time_now_ns();

    /* Allocate results array */
    uint32_t max_results = filter->max_results > 0 ?
                           filter->max_results : PHANTOM_TIME_MAX_RESULTS;
    result->events = calloc(max_results, sizeof(phantom_time_event_record_t));
    if (!result->events) {
        return TIME_ERR_NOMEM;
    }

    /* Scan timeline */
    uint32_t matched = 0;
    uint32_t skipped = 0;

    phantom_time_index_block_t *block = temporal->index.timeline_head;
    while (block && result->count < max_results) {
        for (uint32_t i = 0; i < block->count && result->count < max_results; i++) {
            phantom_time_index_entry_t *entry = &block->entries[i];

            /* Check time range */
            if (entry->timestamp_ns < range_start ||
                entry->timestamp_ns > range_end) {
                continue;
            }

            /* Check event type filter */
            if (filter->event_types != 0) {
                uint32_t entry_cat = entry->type & TIME_CAT_MASK;
                if (!(filter->event_types & entry_cat)) {
                    continue;
                }
            }
            if (filter->specific_event != 0 && entry->type != filter->specific_event) {
                continue;
            }

            /* Check user filter */
            if (filter->filter_by_user && entry->actor_uid != filter->user_id) {
                continue;
            }

            /* This entry matches */
            matched++;

            /* Handle offset for pagination */
            if (skipped < filter->offset) {
                skipped++;
                continue;
            }

            /* Build result record */
            phantom_time_event_record_t *rec = &result->events[result->count];
            rec->event_id = entry->event_id;
            rec->timestamp_ns = entry->timestamp_ns;
            rec->type = entry->type;
            rec->actor_uid = entry->actor_uid;
            rec->geo_offset = entry->geo_offset;

            result->count++;
        }
        block = block->next;
    }

    result->total_matches = matched;
    result->query_time_ns = phantom_time_now_ns() - start_time;

    temporal->total_queries++;

    return TIME_OK;
}

void phantom_time_free_result(phantom_time_result_t *result) {
    if (result && result->events) {
        free(result->events);
        result->events = NULL;
        result->count = 0;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Time Travel
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_time_at(phantom_temporal_t *temporal,
                     phantom_time_point_t point,
                     phantom_snapshot_t *snapshot_out) {
    if (!temporal || !temporal->initialized || !snapshot_out) {
        return TIME_ERR_INVALID;
    }

    uint64_t target_time = resolve_time_point(temporal, point);
    memset(snapshot_out, 0, sizeof(phantom_snapshot_t));

    snapshot_out->timestamp_ns = target_time;

    /* Count events up to this point by category */
    uint64_t file_creates = 0, file_hides = 0;
    uint64_t dir_creates = 0;
    uint64_t proc_creates = 0, proc_dormant = 0;
    uint64_t user_creates = 0, user_dormant = 0;
    uint64_t total_bytes = 0;

    phantom_time_index_block_t *block = temporal->index.timeline_head;
    while (block) {
        for (uint32_t i = 0; i < block->count; i++) {
            phantom_time_index_entry_t *entry = &block->entries[i];

            if (entry->timestamp_ns > target_time) {
                goto done_counting;
            }

            switch (entry->type) {
                case TIME_EVENT_FILE_CREATE:
                    file_creates++;
                    break;
                case TIME_EVENT_FILE_HIDE:
                    file_hides++;
                    break;
                case TIME_EVENT_DIR_CREATE:
                    dir_creates++;
                    break;
                case TIME_EVENT_PROC_CREATE:
                    proc_creates++;
                    break;
                case TIME_EVENT_PROC_DORMANT:
                    proc_dormant++;
                    break;
                case TIME_EVENT_USER_CREATE:
                    user_creates++;
                    break;
                case TIME_EVENT_USER_DORMANT:
                    user_dormant++;
                    break;
                default:
                    break;
            }
        }
        block = block->next;
    }

done_counting:
    snapshot_out->file_count = file_creates - file_hides;
    snapshot_out->dir_count = dir_creates;
    snapshot_out->process_count = proc_creates - proc_dormant;
    snapshot_out->user_count = user_creates - user_dormant;
    snapshot_out->total_size = total_bytes;

    /* Generate snapshot ID */
    snapshot_out->snapshot_id = target_time / 1000000;  /* ms precision */

    return TIME_OK;
}

int phantom_time_diff(phantom_temporal_t *temporal,
                       phantom_time_point_t from,
                       phantom_time_point_t to,
                       phantom_diff_result_t *diff_out) {
    if (!temporal || !temporal->initialized || !diff_out) {
        return TIME_ERR_INVALID;
    }

    memset(diff_out, 0, sizeof(phantom_diff_result_t));

    uint64_t from_time = resolve_time_point(temporal, from);
    uint64_t to_time = resolve_time_point(temporal, to);

    /* Allocate entries */
    diff_out->entries = calloc(PHANTOM_TIME_MAX_RESULTS, sizeof(phantom_diff_entry_t));
    if (!diff_out->entries) {
        return TIME_ERR_NOMEM;
    }

    /* Scan events in range */
    phantom_time_index_block_t *block = temporal->index.timeline_head;
    while (block && diff_out->count < PHANTOM_TIME_MAX_RESULTS) {
        for (uint32_t i = 0; i < block->count && diff_out->count < PHANTOM_TIME_MAX_RESULTS; i++) {
            phantom_time_index_entry_t *entry = &block->entries[i];

            if (entry->timestamp_ns < from_time) continue;
            if (entry->timestamp_ns > to_time) goto done_diff;

            phantom_diff_entry_t *diff = &diff_out->entries[diff_out->count];

            switch (entry->type) {
                case TIME_EVENT_FILE_CREATE:
                case TIME_EVENT_DIR_CREATE:
                    diff->type = DIFF_ADDED;
                    diff_out->added_count++;
                    diff_out->count++;
                    break;

                case TIME_EVENT_FILE_WRITE:
                case TIME_EVENT_FILE_APPEND:
                    diff->type = DIFF_MODIFIED;
                    diff_out->modified_count++;
                    diff_out->count++;
                    break;

                case TIME_EVENT_FILE_HIDE:
                case TIME_EVENT_DIR_HIDE:
                    diff->type = DIFF_HIDDEN;
                    diff_out->hidden_count++;
                    diff_out->count++;
                    break;

                case TIME_EVENT_FILE_UNHIDE:
                    diff->type = DIFF_REVEALED;
                    diff_out->revealed_count++;
                    diff_out->count++;
                    break;

                case TIME_EVENT_FILE_RENAME:
                    diff->type = DIFF_MOVED;
                    diff_out->moved_count++;
                    diff_out->count++;
                    break;

                default:
                    /* Skip non-file events for diff */
                    break;
            }
        }
        block = block->next;
    }

done_diff:
    return TIME_OK;
}

void phantom_time_free_diff(phantom_diff_result_t *diff) {
    if (diff && diff->entries) {
        free(diff->entries);
        diff->entries = NULL;
        diff->count = 0;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * File History
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_time_file_history(phantom_temporal_t *temporal,
                               const char *path,
                               phantom_time_result_t *result) {
    if (!temporal || !temporal->initialized || !path || !result) {
        return TIME_ERR_INVALID;
    }

    memset(result, 0, sizeof(phantom_time_result_t));

    uint64_t path_hash = hash_path(path);
    uint64_t bucket_idx = path_hash % TIME_INDEX_BUCKET_COUNT;

    /* Allocate results */
    result->events = calloc(PHANTOM_TIME_MAX_RESULTS, sizeof(phantom_time_event_record_t));
    if (!result->events) {
        return TIME_ERR_NOMEM;
    }

    /* Search path bucket */
    phantom_time_index_block_t *block = temporal->index.path_buckets[bucket_idx];
    while (block && result->count < PHANTOM_TIME_MAX_RESULTS) {
        for (uint32_t i = 0; i < block->count && result->count < PHANTOM_TIME_MAX_RESULTS; i++) {
            phantom_time_index_entry_t *entry = &block->entries[i];

            if (entry->subject_hash == path_hash) {
                /* Check it's a file event */
                uint32_t cat = entry->type & TIME_CAT_MASK;
                if (cat == TIME_CAT_FILE || cat == TIME_CAT_DIR) {
                    phantom_time_event_record_t *rec = &result->events[result->count];
                    rec->event_id = entry->event_id;
                    rec->timestamp_ns = entry->timestamp_ns;
                    rec->type = entry->type;
                    rec->actor_uid = entry->actor_uid;
                    strncpy(rec->subject_path, path, PHANTOM_TIME_MAX_PATH - 1);
                    result->count++;
                }
            }
        }
        block = block->next;
    }

    result->total_matches = result->count;
    return TIME_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * User Activity
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_time_user_activity(phantom_temporal_t *temporal,
                                uint32_t uid,
                                phantom_time_range_t range,
                                phantom_time_result_t *result) {
    if (!temporal || !temporal->initialized || !result) {
        return TIME_ERR_INVALID;
    }

    memset(result, 0, sizeof(phantom_time_result_t));

    uint64_t range_start = resolve_time_point(temporal, range.start);
    uint64_t range_end = resolve_time_point(temporal, range.end);
    if (range_end == 0) range_end = phantom_time_now_ns();

    uint64_t bucket_idx = uid % TIME_INDEX_BUCKET_COUNT;

    /* Allocate results */
    result->events = calloc(PHANTOM_TIME_MAX_RESULTS, sizeof(phantom_time_event_record_t));
    if (!result->events) {
        return TIME_ERR_NOMEM;
    }

    /* Search user bucket */
    phantom_time_index_block_t *block = temporal->index.user_buckets[bucket_idx];
    while (block && result->count < PHANTOM_TIME_MAX_RESULTS) {
        for (uint32_t i = 0; i < block->count && result->count < PHANTOM_TIME_MAX_RESULTS; i++) {
            phantom_time_index_entry_t *entry = &block->entries[i];

            if (entry->actor_uid == uid &&
                entry->timestamp_ns >= range_start &&
                entry->timestamp_ns <= range_end) {

                phantom_time_event_record_t *rec = &result->events[result->count];
                rec->event_id = entry->event_id;
                rec->timestamp_ns = entry->timestamp_ns;
                rec->type = entry->type;
                rec->actor_uid = entry->actor_uid;
                result->count++;
            }
        }
        block = block->next;
    }

    result->total_matches = result->count;
    return TIME_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Snapshots
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_time_create_snapshot(phantom_temporal_t *temporal,
                                  const char *label,
                                  phantom_snapshot_t *snapshot_out) {
    if (!temporal || !temporal->initialized) {
        return TIME_ERR_INVALID;
    }

    /* Check capacity */
    if (temporal->index.snapshot_count >= temporal->index.snapshot_capacity) {
        uint32_t new_cap = temporal->index.snapshot_capacity * 2;
        phantom_snapshot_t *new_snaps = realloc(temporal->index.snapshots,
                                                  new_cap * sizeof(phantom_snapshot_t));
        if (!new_snaps) {
            return TIME_ERR_NOMEM;
        }
        temporal->index.snapshots = new_snaps;
        temporal->index.snapshot_capacity = new_cap;
    }

    /* Create snapshot at current time */
    phantom_time_point_t now = phantom_time_point_now();
    phantom_snapshot_t snap = {0};

    int err = phantom_time_at(temporal, now, &snap);
    if (err != TIME_OK) {
        return err;
    }

    if (label) {
        strncpy(snap.label, label, sizeof(snap.label) - 1);
    }

    /* Store snapshot */
    temporal->index.snapshots[temporal->index.snapshot_count++] = snap;

    if (snapshot_out) {
        *snapshot_out = snap;
    }

    /* Record event */
    phantom_time_record_event(temporal, TIME_EVENT_SYS_CONFIG,
                               0, 0, "/", snap.snapshot_id,
                               label ? label : "Snapshot created");

    return TIME_OK;
}

int phantom_time_list_snapshots(phantom_temporal_t *temporal,
                                 phantom_snapshot_t **list, uint32_t *count) {
    if (!temporal || !temporal->initialized || !list || !count) {
        return TIME_ERR_INVALID;
    }

    *list = temporal->index.snapshots;
    *count = temporal->index.snapshot_count;

    return TIME_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Time Point Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

phantom_time_point_t phantom_time_point_now(void) {
    phantom_time_point_t p = {0};
    p.type = TIME_POINT_NOW;
    return p;
}

phantom_time_point_t phantom_time_point_ago(int64_t seconds) {
    phantom_time_point_t p = {0};
    p.type = TIME_POINT_RELATIVE;
    p.offset_ns = -seconds * 1000000000LL;
    return p;
}

phantom_time_point_t phantom_time_point_at(time_t timestamp) {
    phantom_time_point_t p = {0};
    p.type = TIME_POINT_ABSOLUTE;
    p.timestamp_ns = (uint64_t)timestamp * 1000000000ULL;
    return p;
}

phantom_time_point_t phantom_time_point_boot(void) {
    phantom_time_point_t p = {0};
    p.type = TIME_POINT_BOOT;
    return p;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility Functions
 * ───────────────────────────────────────────────────────────────────────────── */

const char *phantom_time_event_string(phantom_time_event_t event) {
    switch (event) {
        case TIME_EVENT_FILE_CREATE:    return "FILE_CREATE";
        case TIME_EVENT_FILE_WRITE:     return "FILE_WRITE";
        case TIME_EVENT_FILE_APPEND:    return "FILE_APPEND";
        case TIME_EVENT_FILE_HIDE:      return "FILE_HIDE";
        case TIME_EVENT_FILE_UNHIDE:    return "FILE_UNHIDE";
        case TIME_EVENT_FILE_RENAME:    return "FILE_RENAME";
        case TIME_EVENT_FILE_LINK:      return "FILE_LINK";

        case TIME_EVENT_DIR_CREATE:     return "DIR_CREATE";
        case TIME_EVENT_DIR_HIDE:       return "DIR_HIDE";

        case TIME_EVENT_PROC_CREATE:    return "PROC_CREATE";
        case TIME_EVENT_PROC_SUSPEND:   return "PROC_SUSPEND";
        case TIME_EVENT_PROC_RESUME:    return "PROC_RESUME";
        case TIME_EVENT_PROC_DORMANT:   return "PROC_DORMANT";
        case TIME_EVENT_PROC_AWAKEN:    return "PROC_AWAKEN";
        case TIME_EVENT_PROC_STATE:     return "PROC_STATE";

        case TIME_EVENT_USER_CREATE:    return "USER_CREATE";
        case TIME_EVENT_USER_LOGIN:     return "USER_LOGIN";
        case TIME_EVENT_USER_LOGOUT:    return "USER_LOGOUT";
        case TIME_EVENT_USER_LOCK:      return "USER_LOCK";
        case TIME_EVENT_USER_UNLOCK:    return "USER_UNLOCK";
        case TIME_EVENT_USER_DORMANT:   return "USER_DORMANT";
        case TIME_EVENT_USER_PERM:      return "USER_PERM";

        case TIME_EVENT_PKG_INSTALL:    return "PKG_INSTALL";
        case TIME_EVENT_PKG_ARCHIVE:    return "PKG_ARCHIVE";
        case TIME_EVENT_PKG_RESTORE:    return "PKG_RESTORE";
        case TIME_EVENT_PKG_SUPERSEDE:  return "PKG_SUPERSEDE";

        case TIME_EVENT_NET_CONNECT:    return "NET_CONNECT";
        case TIME_EVENT_NET_SEND:       return "NET_SEND";
        case TIME_EVENT_NET_RECV:       return "NET_RECV";
        case TIME_EVENT_NET_SUSPEND:    return "NET_SUSPEND";
        case TIME_EVENT_NET_DORMANT:    return "NET_DORMANT";

        case TIME_EVENT_GOV_APPROVE:    return "GOV_APPROVE";
        case TIME_EVENT_GOV_DECLINE:    return "GOV_DECLINE";
        case TIME_EVENT_GOV_QUERY:      return "GOV_QUERY";

        case TIME_EVENT_SVC_AWAKEN:     return "SVC_AWAKEN";
        case TIME_EVENT_SVC_REST:       return "SVC_REST";
        case TIME_EVENT_SVC_REGISTER:   return "SVC_REGISTER";

        case TIME_EVENT_SYS_BOOT:       return "SYS_BOOT";
        case TIME_EVENT_SYS_SHUTDOWN:   return "SYS_SHUTDOWN";
        case TIME_EVENT_SYS_CONFIG:     return "SYS_CONFIG";

        default:                        return "UNKNOWN";
    }
}

const char *phantom_time_result_string(phantom_time_result_code_t code) {
    switch (code) {
        case TIME_OK:           return "OK";
        case TIME_ERR_INVALID:  return "Invalid argument";
        case TIME_ERR_NOT_FOUND: return "Not found";
        case TIME_ERR_RANGE:    return "Invalid time range";
        case TIME_ERR_NOMEM:    return "Out of memory";
        case TIME_ERR_IO:       return "I/O error";
        case TIME_ERR_INDEX:    return "Index error";
        case TIME_ERR_BUSY:     return "Busy (indexing)";
        default:                return "Unknown error";
    }
}

int phantom_time_format_timestamp(uint64_t timestamp_ns, char *buf, size_t size) {
    time_t sec = timestamp_ns / 1000000000ULL;
    uint64_t ns = timestamp_ns % 1000000000ULL;

    struct tm *tm = localtime(&sec);
    if (!tm) {
        snprintf(buf, size, "%lu", timestamp_ns);
        return -1;
    }

    int len = strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm);
    if (len > 0 && (size_t)len < size - 10) {
        snprintf(buf + len, size - len, ".%03lu", ns / 1000000);
    }

    return 0;
}

int phantom_time_parse_point(const char *str, phantom_time_point_t *point) {
    if (!str || !point) {
        return TIME_ERR_INVALID;
    }

    memset(point, 0, sizeof(phantom_time_point_t));

    /* Handle special keywords */
    if (strcmp(str, "now") == 0) {
        point->type = TIME_POINT_NOW;
        return TIME_OK;
    }

    if (strcmp(str, "boot") == 0) {
        point->type = TIME_POINT_BOOT;
        return TIME_OK;
    }

    /* Handle relative time (-Ns, -Nm, -Nh, -Nd) */
    if (str[0] == '-' && strlen(str) >= 2) {
        char unit = str[strlen(str) - 1];
        int64_t value = atoll(str + 1);

        int64_t multiplier = 1000000000LL;  /* seconds */
        switch (unit) {
            case 's': multiplier = 1000000000LL; break;
            case 'm': multiplier = 60LL * 1000000000LL; break;
            case 'h': multiplier = 3600LL * 1000000000LL; break;
            case 'd': multiplier = 86400LL * 1000000000LL; break;
            default:
                /* Assume seconds if no unit */
                if (unit >= '0' && unit <= '9') {
                    value = atoll(str + 1);
                }
                break;
        }

        point->type = TIME_POINT_RELATIVE;
        point->offset_ns = -value * multiplier;
        return TIME_OK;
    }

    /* Try to parse as timestamp */
    struct tm tm = {0};
    if (strptime(str, "%Y-%m-%d %H:%M:%S", &tm) ||
        strptime(str, "%Y-%m-%d", &tm)) {
        point->type = TIME_POINT_ABSOLUTE;
        point->timestamp_ns = (uint64_t)mktime(&tm) * 1000000000ULL;
        return TIME_OK;
    }

    return TIME_ERR_INVALID;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Print Functions
 * ───────────────────────────────────────────────────────────────────────────── */

void phantom_time_print_event(const phantom_time_event_record_t *event) {
    if (!event) return;

    char ts[64];
    phantom_time_format_timestamp(event->timestamp_ns, ts, sizeof(ts));

    printf("Event #%lu [%s]\n", event->event_id, ts);
    printf("  Type: %s\n", phantom_time_event_string(event->type));
    printf("  Actor: UID %u (PID %lu)\n", event->actor_uid, event->actor_pid);
    if (event->subject_path[0]) {
        printf("  Subject: %s\n", event->subject_path);
    }
    if (event->description[0]) {
        printf("  Description: %s\n", event->description);
    }
}

void phantom_time_print_result(const phantom_time_result_t *result) {
    if (!result) return;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                           TEMPORAL QUERY RESULTS                           ║\n");
    printf("╠═══════════╤═══════════════════════╤═════════════════╤══════════════════════╣\n");
    printf("║ Event ID  │ Timestamp             │ Type            │ Actor                ║\n");
    printf("╠═══════════╪═══════════════════════╪═════════════════╪══════════════════════╣\n");

    for (uint32_t i = 0; i < result->count; i++) {
        const phantom_time_event_record_t *e = &result->events[i];
        char ts[32];
        phantom_time_format_timestamp(e->timestamp_ns, ts, sizeof(ts));

        printf("║ %9lu │ %-21s │ %-15s │ UID %-15u ║\n",
               e->event_id, ts,
               phantom_time_event_string(e->type),
               e->actor_uid);
    }

    printf("╚═══════════╧═══════════════════════╧═════════════════╧══════════════════════╝\n");
    printf("\nShowing %u of %u matching events (query took %.3f ms)\n",
           result->count, result->total_matches,
           result->query_time_ns / 1000000.0);
}

void phantom_time_print_diff(const phantom_diff_result_t *diff) {
    if (!diff) return;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                           TIME TRAVEL DIFF                                 ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Summary:\n");
    printf("  + Added:    %u\n", diff->added_count);
    printf("  ~ Modified: %u\n", diff->modified_count);
    printf("  - Hidden:   %u\n", diff->hidden_count);
    printf("  * Revealed: %u\n", diff->revealed_count);
    printf("  > Moved:    %u\n", diff->moved_count);
    printf("\n");

    if (diff->count > 0) {
        printf("Changes:\n");
        for (uint32_t i = 0; i < diff->count && i < 20; i++) {
            const phantom_diff_entry_t *e = &diff->entries[i];
            char prefix;
            switch (e->type) {
                case DIFF_ADDED:    prefix = '+'; break;
                case DIFF_MODIFIED: prefix = '~'; break;
                case DIFF_HIDDEN:   prefix = '-'; break;
                case DIFF_REVEALED: prefix = '*'; break;
                case DIFF_MOVED:    prefix = '>'; break;
                default:            prefix = '?'; break;
            }
            printf("  %c %s\n", prefix, e->path);
        }
        if (diff->count > 20) {
            printf("  ... and %u more changes\n", diff->count - 20);
        }
    }
    printf("\n");
}

void phantom_time_print_snapshot(const phantom_snapshot_t *snapshot) {
    if (!snapshot) return;

    char ts[64];
    phantom_time_format_timestamp(snapshot->timestamp_ns, ts, sizeof(ts));

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                           SYSTEM SNAPSHOT                                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Snapshot ID: %lu\n", snapshot->snapshot_id);
    printf("  Timestamp:   %s\n", ts);
    if (snapshot->label[0]) {
        printf("  Label:       %s\n", snapshot->label);
    }
    printf("\n");
    printf("  State at this moment:\n");
    printf("    Files:       %lu\n", snapshot->file_count);
    printf("    Directories: %lu\n", snapshot->dir_count);
    printf("    Processes:   %lu\n", snapshot->process_count);
    printf("    Users:       %lu\n", snapshot->user_count);
    printf("    Connections: %lu\n", snapshot->connection_count);
    printf("    Total Size:  %lu bytes\n", snapshot->total_size);
    printf("\n");
}

void phantom_time_print_stats(phantom_temporal_t *temporal) {
    if (!temporal) return;

    char earliest[64], latest[64];
    phantom_time_format_timestamp(temporal->index.earliest_timestamp, earliest, sizeof(earliest));
    phantom_time_format_timestamp(temporal->index.latest_timestamp, latest, sizeof(latest));

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                      TEMPORAL ENGINE STATISTICS                            ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Timeline Coverage:\n");
    printf("    Earliest: %s\n", earliest);
    printf("    Latest:   %s\n", latest);
    printf("\n");
    printf("  Index:\n");
    printf("    Total Events: %lu\n", temporal->index.event_count);
    printf("    Snapshots:    %u\n", temporal->index.snapshot_count);
    printf("\n");
    printf("  Usage:\n");
    printf("    Events Recorded: %lu\n", temporal->total_events);
    printf("    Queries Run:     %lu\n", temporal->total_queries);
    if (temporal->cache_enabled) {
        uint64_t total = temporal->cache_hits + temporal->cache_misses;
        double hit_rate = total > 0 ? (100.0 * temporal->cache_hits / total) : 0;
        printf("    Cache Hit Rate:  %.1f%%\n", hit_rate);
    }
    printf("\n");
}
