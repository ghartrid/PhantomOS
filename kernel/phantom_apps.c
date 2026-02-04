/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM APPLICATIONS
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Implementation of built-in applications:
 * - Notes: Versioned note-taking
 * - File Viewer: Safe read-only viewing
 * - System Monitor: Real-time statistics
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "phantom_apps.h"
#include "phantom.h"
#include "vfs.h"
#include "governor.h"
#include "phantom_net.h"
#include "../geofs.h"

/* ═══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════════ */

const char *phantom_app_result_string(phantom_app_result_t code) {
    switch (code) {
        case APP_OK:            return "OK";
        case APP_ERR_INVALID:   return "Invalid parameter";
        case APP_ERR_NOT_FOUND: return "Not found";
        case APP_ERR_NOMEM:     return "Out of memory";
        case APP_ERR_FULL:      return "Storage full";
        case APP_ERR_IO:        return "I/O error";
        case APP_ERR_PERMISSION: return "Permission denied";
        case APP_ERR_FORMAT:    return "Invalid format";
        default:                return "Unknown error";
    }
}

const char *phantom_viewer_type_string(phantom_viewer_type_t type) {
    switch (type) {
        case VIEWER_TYPE_TEXT:     return "Text";
        case VIEWER_TYPE_CODE:     return "Source Code";
        case VIEWER_TYPE_IMAGE:    return "Image";
        case VIEWER_TYPE_BINARY:   return "Binary";
        case VIEWER_TYPE_DOCUMENT: return "Document";
        case VIEWER_TYPE_UNKNOWN:  return "Unknown";
        default:                   return "Unknown";
    }
}

const char *phantom_note_state_string(phantom_note_state_t state) {
    switch (state) {
        case NOTE_STATE_ACTIVE:   return "Active";
        case NOTE_STATE_ARCHIVED: return "Archived";
        case NOTE_STATE_PINNED:   return "Pinned";
        default:                  return "Unknown";
    }
}

/* Simple hash for content */
static void compute_hash(const void *data, size_t len, phantom_hash_t hash) {
    const unsigned char *p = data;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    memset(hash, 0, PHANTOM_HASH_SIZE);
    memcpy(hash, &h, sizeof(h));
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * NOTES APP IMPLEMENTATION
 * ═══════════════════════════════════════════════════════════════════════════════ */

int phantom_notes_init(phantom_notes_app_t *app, struct phantom_kernel *kernel) {
    if (!app || !kernel) return APP_ERR_INVALID;

    memset(app, 0, sizeof(phantom_notes_app_t));

    app->note_capacity = 64;
    app->notes = calloc(app->note_capacity, sizeof(phantom_note_t *));
    if (!app->notes) return APP_ERR_NOMEM;

    app->kernel = kernel;
    app->geofs_volume = kernel->geofs_volume;
    app->next_note_id = 1;
    app->next_version_id = 1;
    app->initialized = 1;

    printf("  [notes] Notes app initialized\n");
    return APP_OK;
}

void phantom_notes_shutdown(phantom_notes_app_t *app) {
    if (!app || !app->initialized) return;

    /* Free all notes and their versions */
    for (uint32_t i = 0; i < app->note_count; i++) {
        if (app->notes[i]) {
            if (app->notes[i]->versions) {
                free(app->notes[i]->versions);
            }
            free(app->notes[i]);
        }
    }
    free(app->notes);
    free(app->search_results);

    app->initialized = 0;
    printf("  [notes] Notes app shutdown\n");
}

int phantom_notes_create(phantom_notes_app_t *app, const char *title,
                         const char *content, uint64_t *note_id_out) {
    if (!app || !app->initialized || !title) return APP_ERR_INVALID;

    /* Expand capacity if needed */
    if (app->note_count >= app->note_capacity) {
        uint32_t new_cap = app->note_capacity * 2;
        phantom_note_t **new_notes = realloc(app->notes, new_cap * sizeof(phantom_note_t *));
        if (!new_notes) return APP_ERR_NOMEM;
        app->notes = new_notes;
        app->note_capacity = new_cap;
    }

    /* Create new note */
    phantom_note_t *note = calloc(1, sizeof(phantom_note_t));
    if (!note) return APP_ERR_NOMEM;

    note->note_id = app->next_note_id++;
    strncpy(note->title, title, APP_MAX_TITLE - 1);
    note->state = NOTE_STATE_ACTIVE;
    note->created_at = time(NULL);
    note->modified_at = note->created_at;

    if (content) {
        size_t len = strlen(content);
        if (len >= APP_MAX_NOTE_SIZE) len = APP_MAX_NOTE_SIZE - 1;
        memcpy(note->content, content, len);
        note->content_len = len;
        app->total_characters += len;
    }

    /* Initialize version history */
    note->version_capacity = 16;
    note->versions = calloc(note->version_capacity, sizeof(phantom_note_version_t));
    if (!note->versions) {
        free(note);
        return APP_ERR_NOMEM;
    }

    /* Create initial version */
    phantom_note_version_t *v = &note->versions[0];
    v->version_id = app->next_version_id++;
    v->created_at = note->created_at;
    memcpy(v->content, note->content, note->content_len);
    v->content_len = note->content_len;
    strcpy(v->edit_summary, "Initial creation");
    note->version_count = 1;
    note->current_version = v->version_id;

    app->notes[app->note_count++] = note;
    app->total_notes_created++;

    if (note_id_out) *note_id_out = note->note_id;

    printf("  [notes] Created note %lu: \"%s\"\n", note->note_id, note->title);
    return APP_OK;
}

int phantom_notes_edit(phantom_notes_app_t *app, uint64_t note_id,
                       const char *new_content, const char *edit_summary) {
    if (!app || !app->initialized || !new_content) return APP_ERR_INVALID;

    phantom_note_t *note = phantom_notes_get(app, note_id);
    if (!note) return APP_ERR_NOT_FOUND;

    /* Expand version history if needed */
    if (note->version_count >= note->version_capacity) {
        uint32_t new_cap = note->version_capacity * 2;
        phantom_note_version_t *new_vers = realloc(note->versions,
            new_cap * sizeof(phantom_note_version_t));
        if (!new_vers) return APP_ERR_NOMEM;
        note->versions = new_vers;
        note->version_capacity = new_cap;
    }

    /* Create new version */
    phantom_note_version_t *v = &note->versions[note->version_count];
    v->version_id = app->next_version_id++;
    v->created_at = time(NULL);

    size_t len = strlen(new_content);
    if (len >= APP_MAX_NOTE_SIZE) len = APP_MAX_NOTE_SIZE - 1;
    memcpy(v->content, new_content, len);
    v->content_len = len;

    if (edit_summary) {
        strncpy(v->edit_summary, edit_summary, 255);
    } else {
        snprintf(v->edit_summary, 256, "Edit at %s", ctime(&v->created_at));
        /* Remove newline from ctime */
        char *nl = strchr(v->edit_summary, '\n');
        if (nl) *nl = '\0';
    }

    note->version_count++;
    note->current_version = v->version_id;

    /* Update current content */
    app->total_characters -= note->content_len;
    memcpy(note->content, new_content, len);
    note->content[len] = '\0';
    note->content_len = len;
    app->total_characters += len;

    note->modified_at = v->created_at;
    note->edit_count++;
    app->total_edits++;

    printf("  [notes] Edited note %lu (version %lu)\n", note_id, v->version_id);
    return APP_OK;
}

int phantom_notes_rename(phantom_notes_app_t *app, uint64_t note_id,
                         const char *new_title) {
    if (!app || !app->initialized || !new_title) return APP_ERR_INVALID;

    phantom_note_t *note = phantom_notes_get(app, note_id);
    if (!note) return APP_ERR_NOT_FOUND;

    strncpy(note->title, new_title, APP_MAX_TITLE - 1);
    note->modified_at = time(NULL);

    return APP_OK;
}

int phantom_notes_tag(phantom_notes_app_t *app, uint64_t note_id,
                      const char *tags) {
    if (!app || !app->initialized || !tags) return APP_ERR_INVALID;

    phantom_note_t *note = phantom_notes_get(app, note_id);
    if (!note) return APP_ERR_NOT_FOUND;

    strncpy(note->tags, tags, APP_MAX_TAGS - 1);
    note->modified_at = time(NULL);

    return APP_OK;
}

int phantom_notes_archive(phantom_notes_app_t *app, uint64_t note_id) {
    if (!app || !app->initialized) return APP_ERR_INVALID;

    phantom_note_t *note = phantom_notes_get(app, note_id);
    if (!note) return APP_ERR_NOT_FOUND;

    /* Notes are NEVER deleted - only archived */
    note->state = NOTE_STATE_ARCHIVED;
    note->archived_at = time(NULL);

    printf("  [notes] Archived note %lu (preserved in geology)\n", note_id);
    return APP_OK;
}

int phantom_notes_restore(phantom_notes_app_t *app, uint64_t note_id) {
    if (!app || !app->initialized) return APP_ERR_INVALID;

    phantom_note_t *note = phantom_notes_get(app, note_id);
    if (!note) return APP_ERR_NOT_FOUND;

    note->state = NOTE_STATE_ACTIVE;

    printf("  [notes] Restored note %lu\n", note_id);
    return APP_OK;
}

int phantom_notes_pin(phantom_notes_app_t *app, uint64_t note_id, int pinned) {
    if (!app || !app->initialized) return APP_ERR_INVALID;

    phantom_note_t *note = phantom_notes_get(app, note_id);
    if (!note) return APP_ERR_NOT_FOUND;

    note->state = pinned ? NOTE_STATE_PINNED : NOTE_STATE_ACTIVE;

    return APP_OK;
}

phantom_note_t *phantom_notes_get(phantom_notes_app_t *app, uint64_t note_id) {
    if (!app || !app->initialized) return NULL;

    for (uint32_t i = 0; i < app->note_count; i++) {
        if (app->notes[i] && app->notes[i]->note_id == note_id) {
            app->notes[i]->view_count++;
            return app->notes[i];
        }
    }
    return NULL;
}

int phantom_notes_get_version(phantom_notes_app_t *app, uint64_t note_id,
                              uint64_t version_id, phantom_note_version_t **version_out) {
    if (!app || !version_out) return APP_ERR_INVALID;

    phantom_note_t *note = phantom_notes_get(app, note_id);
    if (!note) return APP_ERR_NOT_FOUND;

    for (uint32_t i = 0; i < note->version_count; i++) {
        if (note->versions[i].version_id == version_id) {
            *version_out = &note->versions[i];
            return APP_OK;
        }
    }

    return APP_ERR_NOT_FOUND;
}

int phantom_notes_list(phantom_notes_app_t *app, phantom_note_t ***notes_out,
                       uint32_t *count_out, int include_archived) {
    if (!app || !notes_out || !count_out) return APP_ERR_INVALID;

    *notes_out = app->notes;

    if (include_archived) {
        *count_out = app->note_count;
    } else {
        /* Count non-archived notes */
        uint32_t count = 0;
        for (uint32_t i = 0; i < app->note_count; i++) {
            if (app->notes[i] && app->notes[i]->state != NOTE_STATE_ARCHIVED) {
                count++;
            }
        }
        *count_out = count;
    }

    return APP_OK;
}

int phantom_notes_search(phantom_notes_app_t *app, const char *query,
                         phantom_note_t ***results_out, uint32_t *count_out) {
    if (!app || !query || !results_out || !count_out) return APP_ERR_INVALID;

    strncpy(app->last_search, query, 255);

    /* Simple case-insensitive search */
    char query_lower[256];
    strncpy(query_lower, query, 255);
    for (char *p = query_lower; *p; p++) *p = tolower(*p);

    uint32_t matches = 0;
    for (uint32_t i = 0; i < app->note_count; i++) {
        phantom_note_t *note = app->notes[i];
        if (!note || note->state == NOTE_STATE_ARCHIVED) continue;

        /* Search in title */
        char title_lower[APP_MAX_TITLE];
        strncpy(title_lower, note->title, APP_MAX_TITLE - 1);
        for (char *p = title_lower; *p; p++) *p = tolower(*p);

        if (strstr(title_lower, query_lower)) {
            matches++;
            continue;
        }

        /* Search in content */
        char content_lower[1024];
        strncpy(content_lower, note->content, 1023);
        for (char *p = content_lower; *p; p++) *p = tolower(*p);

        if (strstr(content_lower, query_lower)) {
            matches++;
            continue;
        }

        /* Search in tags */
        if (strstr(note->tags, query)) {
            matches++;
        }
    }

    *results_out = app->notes;  /* Return all, let caller filter */
    *count_out = matches;

    return APP_OK;
}

void phantom_notes_print(const phantom_note_t *note) {
    if (!note) return;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  NOTE #%lu: %s\n", note->note_id, note->title);
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  State:    %s\n", phantom_note_state_string(note->state));
    printf("  Tags:     %s\n", note->tags[0] ? note->tags : "(none)");
    printf("  Created:  %s", ctime(&note->created_at));
    printf("  Modified: %s", ctime(&note->modified_at));
    printf("  Versions: %u\n", note->version_count);
    printf("  Views:    %u\n", note->view_count);
    printf("───────────────────────────────────────────────────────────────────\n");
    printf("%s\n", note->content);
    printf("═══════════════════════════════════════════════════════════════════\n");
}

void phantom_notes_print_list(phantom_notes_app_t *app) {
    if (!app) return;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("                         PHANTOM NOTES\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  Total Notes: %u  |  Total Edits: %lu  |  Characters: %lu\n",
           app->note_count, app->total_edits, app->total_characters);
    printf("───────────────────────────────────────────────────────────────────\n");
    printf("  %-4s %-7s %-30s %-20s\n", "ID", "State", "Title", "Modified");
    printf("───────────────────────────────────────────────────────────────────\n");

    for (uint32_t i = 0; i < app->note_count; i++) {
        phantom_note_t *note = app->notes[i];
        if (!note) continue;

        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", localtime(&note->modified_at));

        char state_char = ' ';
        if (note->state == NOTE_STATE_PINNED) state_char = '*';
        else if (note->state == NOTE_STATE_ARCHIVED) state_char = 'A';

        printf("  %-4lu %c%-6s %-30.30s %-20s\n",
               note->note_id,
               state_char,
               phantom_note_state_string(note->state),
               note->title,
               time_str);
    }
    printf("═══════════════════════════════════════════════════════════════════\n");
}

void phantom_notes_print_history(const phantom_note_t *note) {
    if (!note) return;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  VERSION HISTORY: %s\n", note->title);
    printf("═══════════════════════════════════════════════════════════════════\n");

    for (uint32_t i = 0; i < note->version_count; i++) {
        phantom_note_version_t *v = &note->versions[i];
        char current = (v->version_id == note->current_version) ? '*' : ' ';

        printf("  %c v%lu - %s", current, v->version_id, ctime(&v->created_at));
        printf("    %s (%zu bytes)\n", v->edit_summary, v->content_len);
    }
    printf("═══════════════════════════════════════════════════════════════════\n");
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FILE VIEWER APP IMPLEMENTATION
 * ═══════════════════════════════════════════════════════════════════════════════ */

int phantom_viewer_init(phantom_viewer_app_t *app, struct phantom_kernel *kernel,
                        struct vfs_context *vfs) {
    if (!app || !kernel || !vfs) return APP_ERR_INVALID;

    memset(app, 0, sizeof(phantom_viewer_app_t));

    app->kernel = kernel;
    app->vfs = vfs;
    app->lines_per_page = 25;
    app->show_line_numbers = 1;
    app->word_wrap = 1;

    app->history_capacity = 64;
    app->view_history = calloc(app->history_capacity, sizeof(char *));
    if (!app->view_history) return APP_ERR_NOMEM;

    app->initialized = 1;

    printf("  [viewer] File viewer initialized\n");
    return APP_OK;
}

void phantom_viewer_shutdown(phantom_viewer_app_t *app) {
    if (!app || !app->initialized) return;

    phantom_viewer_close(app);

    for (uint32_t i = 0; i < app->history_count; i++) {
        free(app->view_history[i]);
    }
    free(app->view_history);

    app->initialized = 0;
    printf("  [viewer] File viewer shutdown\n");
}

/* Detect file type from extension */
static phantom_viewer_type_t detect_file_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return VIEWER_TYPE_BINARY;
    ext++;

    /* Text files */
    if (strcasecmp(ext, "txt") == 0 || strcasecmp(ext, "md") == 0 ||
        strcasecmp(ext, "log") == 0 || strcasecmp(ext, "csv") == 0 ||
        strcasecmp(ext, "json") == 0 || strcasecmp(ext, "xml") == 0 ||
        strcasecmp(ext, "yaml") == 0 || strcasecmp(ext, "yml") == 0 ||
        strcasecmp(ext, "ini") == 0 || strcasecmp(ext, "cfg") == 0 ||
        strcasecmp(ext, "conf") == 0) {
        return VIEWER_TYPE_TEXT;
    }

    /* Source code */
    if (strcasecmp(ext, "c") == 0 || strcasecmp(ext, "h") == 0 ||
        strcasecmp(ext, "cpp") == 0 || strcasecmp(ext, "hpp") == 0 ||
        strcasecmp(ext, "py") == 0 || strcasecmp(ext, "js") == 0 ||
        strcasecmp(ext, "ts") == 0 || strcasecmp(ext, "java") == 0 ||
        strcasecmp(ext, "go") == 0 || strcasecmp(ext, "rs") == 0 ||
        strcasecmp(ext, "rb") == 0 || strcasecmp(ext, "php") == 0 ||
        strcasecmp(ext, "sh") == 0 || strcasecmp(ext, "bash") == 0 ||
        strcasecmp(ext, "html") == 0 || strcasecmp(ext, "css") == 0 ||
        strcasecmp(ext, "sql") == 0) {
        return VIEWER_TYPE_CODE;
    }

    /* Images */
    if (strcasecmp(ext, "png") == 0 || strcasecmp(ext, "jpg") == 0 ||
        strcasecmp(ext, "jpeg") == 0 || strcasecmp(ext, "gif") == 0 ||
        strcasecmp(ext, "bmp") == 0 || strcasecmp(ext, "svg") == 0 ||
        strcasecmp(ext, "ico") == 0 || strcasecmp(ext, "webp") == 0) {
        return VIEWER_TYPE_IMAGE;
    }

    /* Documents */
    if (strcasecmp(ext, "pdf") == 0 || strcasecmp(ext, "doc") == 0 ||
        strcasecmp(ext, "docx") == 0 || strcasecmp(ext, "odt") == 0) {
        return VIEWER_TYPE_DOCUMENT;
    }

    return VIEWER_TYPE_BINARY;
}

/* Get MIME type from file type */
static const char *get_mime_type(phantom_viewer_type_t type, const char *ext) {
    switch (type) {
        case VIEWER_TYPE_TEXT:
            return "text/plain";
        case VIEWER_TYPE_CODE:
            if (ext) {
                if (strcasecmp(ext, "c") == 0 || strcasecmp(ext, "h") == 0)
                    return "text/x-c";
                if (strcasecmp(ext, "py") == 0) return "text/x-python";
                if (strcasecmp(ext, "js") == 0) return "text/javascript";
                if (strcasecmp(ext, "html") == 0) return "text/html";
                if (strcasecmp(ext, "css") == 0) return "text/css";
            }
            return "text/plain";
        case VIEWER_TYPE_IMAGE:
            if (ext) {
                if (strcasecmp(ext, "png") == 0) return "image/png";
                if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0)
                    return "image/jpeg";
                if (strcasecmp(ext, "gif") == 0) return "image/gif";
            }
            return "image/unknown";
        case VIEWER_TYPE_DOCUMENT:
            if (ext && strcasecmp(ext, "pdf") == 0) return "application/pdf";
            return "application/octet-stream";
        default:
            return "application/octet-stream";
    }
}

int phantom_viewer_open(phantom_viewer_app_t *app, const char *path) {
    if (!app || !app->initialized || !path) return APP_ERR_INVALID;

    /* Close previous file */
    phantom_viewer_close(app);

    /* Get file info from VFS */
    struct vfs_stat st;
    if (vfs_stat(app->vfs, path, &st) != VFS_OK) {
        return APP_ERR_NOT_FOUND;
    }

    /* Fill in file info */
    phantom_file_info_t *info = &app->current_file;
    strncpy(info->path, path, sizeof(info->path) - 1);

    /* Extract filename */
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    strncpy(info->name, name, sizeof(info->name) - 1);

    /* Extract extension */
    const char *ext = strrchr(name, '.');
    if (ext) {
        strncpy(info->extension, ext + 1, sizeof(info->extension) - 1);
    }

    info->size = st.size;
    info->type = detect_file_type(path);
    strncpy(info->mime_type, get_mime_type(info->type, info->extension),
            sizeof(info->mime_type) - 1);

    /* Read file content */
    vfs_fd_t fd = vfs_open(app->vfs, 1, path, VFS_O_RDONLY, 0);
    if (fd < 0) {
        return APP_ERR_IO;
    }

    /* Allocate buffer for content */
    size_t read_size = (info->size < 1024 * 1024) ? info->size : 1024 * 1024;  /* 1MB max */
    app->content = malloc(read_size + 1);
    if (!app->content) {
        vfs_close(app->vfs, fd);
        return APP_ERR_NOMEM;
    }

    ssize_t bytes_read = vfs_read(app->vfs, fd, app->content, read_size);
    vfs_close(app->vfs, fd);

    if (bytes_read < 0) bytes_read = 0;
    app->content[bytes_read] = '\0';
    app->content_size = (size_t)bytes_read;

    /* Compute hash */
    compute_hash(app->content, bytes_read, info->content_hash);

    /* Count lines, words, chars for text files */
    if (info->type == VIEWER_TYPE_TEXT || info->type == VIEWER_TYPE_CODE) {
        info->line_count = 1;
        info->word_count = 0;
        info->char_count = bytes_read;
        strcpy(info->encoding, "UTF-8");

        int in_word = 0;
        for (size_t i = 0; i < bytes_read; i++) {
            if (app->content[i] == '\n') info->line_count++;
            if (isspace(app->content[i])) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                info->word_count++;
            }
        }
    }

    app->file_loaded = 1;
    app->files_viewed++;
    app->bytes_viewed += bytes_read;

    /* Add to history */
    if (app->history_count < app->history_capacity) {
        app->view_history[app->history_count++] = strdup(path);
    }

    printf("  [viewer] Opened: %s (%s, %lu bytes)\n",
           info->name, phantom_viewer_type_string(info->type), info->size);

    return APP_OK;
}

void phantom_viewer_close(phantom_viewer_app_t *app) {
    if (!app) return;

    if (app->content) {
        free(app->content);
        app->content = NULL;
    }
    app->content_size = 0;
    app->file_loaded = 0;
    app->scroll_offset = 0;
}

int phantom_viewer_get_info(phantom_viewer_app_t *app, phantom_file_info_t *info_out) {
    if (!app || !info_out || !app->file_loaded) return APP_ERR_INVALID;

    memcpy(info_out, &app->current_file, sizeof(phantom_file_info_t));
    return APP_OK;
}

int phantom_viewer_get_content(phantom_viewer_app_t *app, char **content_out,
                               size_t *size_out) {
    if (!app || !content_out || !size_out || !app->file_loaded) return APP_ERR_INVALID;

    *content_out = app->content;
    *size_out = app->content_size;
    return APP_OK;
}

void phantom_viewer_print_info(const phantom_file_info_t *info) {
    if (!info) return;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("                        FILE INFORMATION\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  Name:      %s\n", info->name);
    printf("  Path:      %s\n", info->path);
    printf("  Type:      %s\n", phantom_viewer_type_string(info->type));
    printf("  MIME:      %s\n", info->mime_type);
    printf("  Size:      %lu bytes\n", info->size);

    if (info->type == VIEWER_TYPE_TEXT || info->type == VIEWER_TYPE_CODE) {
        printf("  Lines:     %u\n", info->line_count);
        printf("  Words:     %u\n", info->word_count);
        printf("  Encoding:  %s\n", info->encoding);
    }

    if (info->type == VIEWER_TYPE_IMAGE) {
        printf("  Dimensions: %ux%u\n", info->width, info->height);
    }

    printf("═══════════════════════════════════════════════════════════════════\n");
}

void phantom_viewer_print_content(phantom_viewer_app_t *app, uint32_t max_lines) {
    if (!app || !app->file_loaded || !app->content) return;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  %s\n", app->current_file.name);
    printf("═══════════════════════════════════════════════════════════════════\n");

    if (app->current_file.type == VIEWER_TYPE_IMAGE) {
        printf("  [Image file - cannot display in terminal]\n");
        printf("  Format: %s\n", app->current_file.mime_type);
        return;
    }

    if (app->current_file.type == VIEWER_TYPE_DOCUMENT) {
        printf("  [Document file - cannot display in terminal]\n");
        printf("  Format: %s\n", app->current_file.mime_type);
        return;
    }

    if (app->current_file.type == VIEWER_TYPE_BINARY) {
        phantom_viewer_print_hex(app, 256);
        return;
    }

    /* Print text content with line numbers */
    uint32_t line = 1;
    const char *p = app->content;
    const char *line_start = p;

    while (*p && line <= max_lines) {
        if (*p == '\n' || *(p + 1) == '\0') {
            int len = (int)(p - line_start);
            if (*p != '\n') len++;

            if (app->show_line_numbers) {
                printf("%4u │ %.*s\n", line, len, line_start);
            } else {
                printf("%.*s\n", len, line_start);
            }

            line++;
            line_start = p + 1;
        }
        p++;
    }

    if (line > max_lines && *p) {
        printf("  ... (%u more lines)\n", app->current_file.line_count - max_lines);
    }

    printf("═══════════════════════════════════════════════════════════════════\n");
}

void phantom_viewer_print_hex(phantom_viewer_app_t *app, uint32_t max_bytes) {
    if (!app || !app->file_loaded || !app->content) return;

    printf("\n  HEX DUMP:\n");
    printf("  ────────────────────────────────────────────────────────────────\n");

    uint32_t bytes = (app->content_size < max_bytes) ? app->content_size : max_bytes;
    const unsigned char *data = (const unsigned char *)app->content;

    for (uint32_t i = 0; i < bytes; i += 16) {
        printf("  %08X  ", i);

        /* Hex bytes */
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < bytes) {
                printf("%02X ", data[i + j]);
            } else {
                printf("   ");
            }
            if (j == 7) printf(" ");
        }

        printf(" │");

        /* ASCII */
        for (uint32_t j = 0; j < 16 && i + j < bytes; j++) {
            unsigned char c = data[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }

        printf("│\n");
    }

    if (bytes < app->content_size) {
        printf("  ... (%lu more bytes)\n", app->content_size - bytes);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * SYSTEM MONITOR APP IMPLEMENTATION
 * ═══════════════════════════════════════════════════════════════════════════════ */

int phantom_monitor_init(phantom_monitor_app_t *app, struct phantom_kernel *kernel,
                         struct vfs_context *vfs) {
    if (!app || !kernel || !vfs) return APP_ERR_INVALID;

    memset(app, 0, sizeof(phantom_monitor_app_t));

    app->kernel = kernel;
    app->vfs = vfs;
    app->boot_time = time(NULL);
    app->refresh_interval_ms = 1000;

    /* Process list */
    app->process_capacity = 64;
    app->processes = calloc(app->process_capacity, sizeof(phantom_proc_info_t));
    if (!app->processes) return APP_ERR_NOMEM;

    /* History for graphs */
    app->history_size = 60;  /* 60 samples */
    app->cpu_history = calloc(app->history_size, sizeof(float));
    app->mem_history = calloc(app->history_size, sizeof(float));
    if (!app->cpu_history || !app->mem_history) {
        free(app->processes);
        free(app->cpu_history);
        free(app->mem_history);
        return APP_ERR_NOMEM;
    }

    strcpy(app->hostname, "phantom");
    strcpy(app->version, "1.0.0");

    app->initialized = 1;

    printf("  [monitor] System monitor initialized\n");
    return APP_OK;
}

void phantom_monitor_shutdown(phantom_monitor_app_t *app) {
    if (!app || !app->initialized) return;

    free(app->processes);
    free(app->cpu_history);
    free(app->mem_history);

    app->initialized = 0;
    printf("  [monitor] System monitor shutdown\n");
}

int phantom_monitor_refresh(phantom_monitor_app_t *app) {
    if (!app || !app->initialized) return APP_ERR_INVALID;

    struct phantom_kernel *kernel = app->kernel;

    /* Update uptime */
    app->uptime_seconds = time(NULL) - app->boot_time;

    /* Update process list - iterate linked list */
    app->process_count = 0;
    struct phantom_process *proc = kernel->processes;
    while (proc && app->process_count < app->process_capacity) {
        phantom_proc_info_t *info = &app->processes[app->process_count++];
        info->pid = proc->pid;
        strncpy(info->name, proc->name, 255);
        info->name[255] = '\0';
        info->state = proc->state;
        info->start_time = proc->created;
        info->cpu_time_ms = (uint64_t)(proc->total_time_ns / 1000000ULL);
        info->memory_bytes = proc->memory_size;
        info->cpu_percent = 0.0f;  /* Would need time delta calculation */
        info->mem_percent = 0.0f;
        proc = proc->next;
    }

    /* Update memory stats (simulated) */
    app->mem_stats.total_bytes = 1024 * 1024 * 1024;  /* 1GB simulated */
    app->mem_stats.used_bytes = 256 * 1024 * 1024;
    app->mem_stats.free_bytes = app->mem_stats.total_bytes - app->mem_stats.used_bytes;
    app->mem_stats.cached_bytes = 128 * 1024 * 1024;
    app->mem_stats.usage_percent = 100.0f * app->mem_stats.used_bytes / app->mem_stats.total_bytes;

    /* Update geology stats */
    /* Note: geofs_volume_t is opaque, so we use simulated values */
    /* A proper implementation would add a geofs_volume_stats() function */
    if (kernel->geofs_volume) {
        app->geo_stats.total_bytes = 100 * 1024 * 1024;  /* 100MB simulated */
        app->geo_stats.used_bytes = 10 * 1024 * 1024;    /* 10MB used */
        app->geo_stats.free_bytes = 90 * 1024 * 1024;
        app->geo_stats.usage_percent = 10.0f;
        app->geo_stats.total_operations = 0;  /* Would need tracking in kernel */
        app->geo_stats.total_views = 1;  /* At least one view exists */
        app->geo_stats.active_view_id = 0;
    }

    /* Update network stats */
    if (kernel->net) {
        phantom_net_t *net = kernel->net;
        app->net_stats.network_enabled = net->initialized;
        app->net_stats.active_connections = (uint32_t)net->active_connections;
        app->net_stats.bytes_sent = net->total_bytes_sent;
        app->net_stats.bytes_received = net->total_bytes_received;
    }

    /* Update governor stats */
    if (kernel->governor) {
        phantom_governor_t *gov = kernel->governor;
        app->gov_stats.total_evaluations = gov->total_evaluations;
        app->gov_stats.approvals = gov->auto_approved + gov->user_approved;
        app->gov_stats.denials = gov->auto_declined + gov->user_declined;
        if (gov->total_evaluations > 0) {
            app->gov_stats.approval_rate = 100.0f * app->gov_stats.approvals / gov->total_evaluations;
        }
        /* Calculate threat level from threat statistics */
        if (gov->threats_critical > 0 || gov->threats_high > 0) {
            app->gov_stats.threat_level = 2;  /* High */
        } else if (gov->threats_medium > 0) {
            app->gov_stats.threat_level = 1;  /* Medium */
        } else {
            app->gov_stats.threat_level = 0;  /* Low/None */
        }
    }

    /* Update history */
    app->cpu_history[app->history_index] = 10.0f + (rand() % 20);  /* Simulated */
    app->mem_history[app->history_index] = app->mem_stats.usage_percent;
    app->history_index = (app->history_index + 1) % app->history_size;

    app->last_refresh = time(NULL);

    return APP_OK;
}

int phantom_monitor_get_processes(phantom_monitor_app_t *app,
                                  phantom_proc_info_t **procs_out,
                                  uint32_t *count_out) {
    if (!app || !procs_out || !count_out) return APP_ERR_INVALID;

    *procs_out = app->processes;
    *count_out = app->process_count;
    return APP_OK;
}

int phantom_monitor_get_memory(phantom_monitor_app_t *app, phantom_mem_stats_t *stats_out) {
    if (!app || !stats_out) return APP_ERR_INVALID;
    memcpy(stats_out, &app->mem_stats, sizeof(phantom_mem_stats_t));
    return APP_OK;
}

int phantom_monitor_get_geology(phantom_monitor_app_t *app, phantom_geo_stats_t *stats_out) {
    if (!app || !stats_out) return APP_ERR_INVALID;
    memcpy(stats_out, &app->geo_stats, sizeof(phantom_geo_stats_t));
    return APP_OK;
}

int phantom_monitor_get_network(phantom_monitor_app_t *app, phantom_net_stats_t *stats_out) {
    if (!app || !stats_out) return APP_ERR_INVALID;
    memcpy(stats_out, &app->net_stats, sizeof(phantom_net_stats_t));
    return APP_OK;
}

int phantom_monitor_get_governor(phantom_monitor_app_t *app, phantom_gov_stats_t *stats_out) {
    if (!app || !stats_out) return APP_ERR_INVALID;
    memcpy(stats_out, &app->gov_stats, sizeof(phantom_gov_stats_t));
    return APP_OK;
}

void phantom_monitor_print_summary(phantom_monitor_app_t *app) {
    if (!app) return;

    phantom_monitor_refresh(app);

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                    PHANTOM SYSTEM MONITOR                         ║\n");
    printf("║                  \"To Create, Not To Destroy\"                      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* System Info */
    printf("  SYSTEM\n");
    printf("  ──────────────────────────────────────────────────────────────────\n");
    printf("  Hostname:    %s\n", app->hostname);
    printf("  Version:     PhantomOS %s\n", app->version);
    printf("  Uptime:      %lu hours %lu minutes\n",
           app->uptime_seconds / 3600, (app->uptime_seconds % 3600) / 60);
    printf("  Processes:   %u active\n", app->process_count);
    printf("\n");

    /* Memory */
    printf("  MEMORY\n");
    printf("  ──────────────────────────────────────────────────────────────────\n");
    printf("  Total:       %.1f MB\n", app->mem_stats.total_bytes / (1024.0 * 1024.0));
    printf("  Used:        %.1f MB (%.1f%%)\n",
           app->mem_stats.used_bytes / (1024.0 * 1024.0),
           app->mem_stats.usage_percent);
    printf("  Free:        %.1f MB\n", app->mem_stats.free_bytes / (1024.0 * 1024.0));

    /* Visual bar */
    printf("  [");
    int bar_width = 50;
    int filled = (int)(app->mem_stats.usage_percent * bar_width / 100);
    for (int i = 0; i < bar_width; i++) {
        printf("%s", i < filled ? "#" : "-");
    }
    printf("]\n\n");

    /* Geology */
    printf("  GEOLOGY (Storage)\n");
    printf("  ──────────────────────────────────────────────────────────────────\n");
    printf("  Total:       %.1f MB\n", app->geo_stats.total_bytes / (1024.0 * 1024.0));
    printf("  Used:        %.1f MB (%.1f%%)\n",
           app->geo_stats.used_bytes / (1024.0 * 1024.0),
           app->geo_stats.usage_percent);
    printf("  Operations:  %lu\n", app->geo_stats.total_operations);
    printf("  Views:       %lu (active: %lu)\n",
           app->geo_stats.total_views, app->geo_stats.active_view_id);

    /* Visual bar */
    printf("  [");
    filled = (int)(app->geo_stats.usage_percent * bar_width / 100);
    for (int i = 0; i < bar_width; i++) {
        printf("%s", i < filled ? "#" : "-");
    }
    printf("]\n\n");

    /* Network */
    printf("  NETWORK\n");
    printf("  ──────────────────────────────────────────────────────────────────\n");
    printf("  Status:      %s\n", app->net_stats.network_enabled ? "Enabled" : "Disabled");
    printf("  Connections: %u active\n", app->net_stats.active_connections);
    printf("  Sent:        %.1f KB\n", app->net_stats.bytes_sent / 1024.0);
    printf("  Received:    %.1f KB\n", app->net_stats.bytes_received / 1024.0);
    printf("\n");

    /* Governor */
    printf("  GOVERNOR\n");
    printf("  ──────────────────────────────────────────────────────────────────\n");
    printf("  Evaluations: %lu\n", app->gov_stats.total_evaluations);
    printf("  Approved:    %lu (%.1f%%)\n",
           app->gov_stats.approvals, app->gov_stats.approval_rate);
    printf("  Denied:      %lu\n", app->gov_stats.denials);
    printf("  Threat:      Level %u\n", app->gov_stats.threat_level);
    printf("\n");
}

void phantom_monitor_print_processes(phantom_monitor_app_t *app) {
    if (!app) return;

    phantom_monitor_refresh(app);

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("                         PROCESS LIST\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  %-6s %-20s %-12s %-10s %-10s\n",
           "PID", "Name", "State", "Memory", "CPU");
    printf("───────────────────────────────────────────────────────────────────\n");

    for (uint32_t i = 0; i < app->process_count; i++) {
        phantom_proc_info_t *p = &app->processes[i];

        const char *state_str;
        switch (p->state) {
            case PROCESS_RUNNING: state_str = "Running"; break;
            case PROCESS_DORMANT: state_str = "Dormant"; break;
            case PROCESS_BLOCKED: state_str = "Blocked"; break;
            case PROCESS_READY: state_str = "Ready"; break;
            case PROCESS_EMBRYO: state_str = "Embryo"; break;
            default: state_str = "Unknown"; break;
        }

        printf("  %-6lu %-20.20s %-12s %-10lu %-10.1f%%\n",
               (unsigned long)p->pid, p->name, state_str, p->memory_bytes, p->cpu_percent);
    }

    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  Total: %u processes\n", app->process_count);
}

void phantom_monitor_print_memory(phantom_monitor_app_t *app) {
    if (!app) return;

    phantom_monitor_refresh(app);

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("                       MEMORY STATISTICS\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  Total Memory:    %lu bytes (%.1f MB)\n",
           app->mem_stats.total_bytes, app->mem_stats.total_bytes / (1024.0 * 1024.0));
    printf("  Used Memory:     %lu bytes (%.1f MB)\n",
           app->mem_stats.used_bytes, app->mem_stats.used_bytes / (1024.0 * 1024.0));
    printf("  Free Memory:     %lu bytes (%.1f MB)\n",
           app->mem_stats.free_bytes, app->mem_stats.free_bytes / (1024.0 * 1024.0));
    printf("  Cached:          %lu bytes (%.1f MB)\n",
           app->mem_stats.cached_bytes, app->mem_stats.cached_bytes / (1024.0 * 1024.0));
    printf("  Usage:           %.1f%%\n", app->mem_stats.usage_percent);
    printf("═══════════════════════════════════════════════════════════════════\n");
}

void phantom_monitor_print_geology(phantom_monitor_app_t *app) {
    if (!app) return;

    phantom_monitor_refresh(app);

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("                      GEOLOGY STATISTICS\n");
    printf("                  (Immutable Storage Layer)\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  Total Capacity:   %lu bytes (%.1f MB)\n",
           app->geo_stats.total_bytes, app->geo_stats.total_bytes / (1024.0 * 1024.0));
    printf("  Used:             %lu bytes (%.1f MB)\n",
           app->geo_stats.used_bytes, app->geo_stats.used_bytes / (1024.0 * 1024.0));
    printf("  Free:             %lu bytes (%.1f MB)\n",
           app->geo_stats.free_bytes, app->geo_stats.free_bytes / (1024.0 * 1024.0));
    printf("  Usage:            %.1f%%\n", app->geo_stats.usage_percent);
    printf("───────────────────────────────────────────────────────────────────\n");
    printf("  Total Operations: %lu\n", app->geo_stats.total_operations);
    printf("  Total Views:      %lu\n", app->geo_stats.total_views);
    printf("  Active View:      %lu\n", app->geo_stats.active_view_id);
    printf("═══════════════════════════════════════════════════════════════════\n");
}

void phantom_monitor_print_network(phantom_monitor_app_t *app) {
    if (!app) return;

    phantom_monitor_refresh(app);

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("                      NETWORK STATISTICS\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  Network Status:   %s\n", app->net_stats.network_enabled ? "Enabled" : "Disabled");
    printf("  Active Conns:     %u\n", app->net_stats.active_connections);
    printf("  Total Conns:      %u\n", app->net_stats.total_connections);
    printf("───────────────────────────────────────────────────────────────────\n");
    printf("  Bytes Sent:       %lu (%.1f KB)\n",
           app->net_stats.bytes_sent, app->net_stats.bytes_sent / 1024.0);
    printf("  Bytes Received:   %lu (%.1f KB)\n",
           app->net_stats.bytes_received, app->net_stats.bytes_received / 1024.0);
    printf("  Packets Sent:     %lu\n", app->net_stats.packets_sent);
    printf("  Packets Received: %lu\n", app->net_stats.packets_received);
    printf("═══════════════════════════════════════════════════════════════════\n");
}

void phantom_monitor_print_governor(phantom_monitor_app_t *app) {
    if (!app) return;

    phantom_monitor_refresh(app);

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("                     GOVERNOR STATISTICS\n");
    printf("                   (Code Safety Evaluator)\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  Total Evaluations: %lu\n", app->gov_stats.total_evaluations);
    printf("  Approvals:         %lu\n", app->gov_stats.approvals);
    printf("  Denials:           %lu\n", app->gov_stats.denials);
    printf("  Approval Rate:     %.1f%%\n", app->gov_stats.approval_rate);
    printf("───────────────────────────────────────────────────────────────────\n");
    printf("  Current Threat:    Level %u\n", app->gov_stats.threat_level);
    printf("═══════════════════════════════════════════════════════════════════\n");
}
