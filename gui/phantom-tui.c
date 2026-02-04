/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                           PHANTOM TUI
 *                 Terminal UI for PhantomOS GeoFS
 *
 *                      "To Create, Not To Destroy"
 *
 *    A text-based interface for GeoFS volumes.
 *    Works in any terminal, no GUI dependencies.
 *
 *    Build: gcc -Wall -O2 phantom-tui.c ../geofs.c -o phantom-tui -lpthread -lncurses
 *    Usage: ./phantom-tui [volume.geo]
 *
 * ══════════════════════════════════════════════════════════════════════════════
 */

#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include "../geofs.h"

/* ══════════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ══════════════════════════════════════════════════════════════════════════════ */

#define MAX_FILES 1024
#define CONTENT_BUFFER_SIZE (64 * 1024)

/* ══════════════════════════════════════════════════════════════════════════════
 * APPLICATION STATE
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char name[GEOFS_MAX_NAME + 1];
    char hash[65];
    uint64_t size;
    geofs_time_t created;
    int is_dir;
} FileEntry;

typedef struct {
    geofs_volume_t *volume;
    char volume_path[GEOFS_MAX_PATH];
    char current_dir[GEOFS_MAX_PATH];

    FileEntry files[MAX_FILES];
    int file_count;
    int selected;
    int scroll_offset;

    char content[CONTENT_BUFFER_SIZE];
    size_t content_size;
    int content_scroll;

    char status[256];
    int show_help;
    int focus_content;  /* 0 = file list, 1 = content */
} PhantomTUI;

/* ══════════════════════════════════════════════════════════════════════════════
 * FILE LISTING
 * ══════════════════════════════════════════════════════════════════════════════ */

static void add_file_entry(const struct geofs_dirent *entry, void *ctx) {
    PhantomTUI *tui = ctx;
    if (tui->file_count >= MAX_FILES) return;

    FileEntry *fe = &tui->files[tui->file_count++];
    strncpy(fe->name, entry->name, GEOFS_MAX_NAME);
    geofs_hash_to_string(entry->content_hash, fe->hash);
    fe->size = entry->size;
    fe->created = entry->created;
    fe->is_dir = entry->is_dir;
}

static void refresh_file_list(PhantomTUI *tui) {
    tui->file_count = 0;
    tui->selected = 0;
    tui->scroll_offset = 0;

    if (!tui->volume) return;

    geofs_ref_list(tui->volume, tui->current_dir, add_file_entry, tui);

    snprintf(tui->status, sizeof(tui->status),
             "View %lu | %s | %d items",
             geofs_view_current(tui->volume),
             tui->current_dir,
             tui->file_count);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CONTENT LOADING
 * ══════════════════════════════════════════════════════════════════════════════ */

static void load_content(PhantomTUI *tui, const char *path) {
    tui->content[0] = '\0';
    tui->content_size = 0;
    tui->content_scroll = 0;

    if (!tui->volume) return;

    geofs_hash_t hash;
    if (geofs_ref_resolve(tui->volume, path, hash) != GEOFS_OK) {
        snprintf(tui->content, sizeof(tui->content), "[Failed to resolve: %s]", path);
        tui->content_size = strlen(tui->content);
        return;
    }

    uint64_t size;
    if (geofs_content_size(tui->volume, hash, &size) != GEOFS_OK) {
        snprintf(tui->content, sizeof(tui->content), "[Failed to get size]");
        tui->content_size = strlen(tui->content);
        return;
    }

    if (size > CONTENT_BUFFER_SIZE - 1) {
        snprintf(tui->content, sizeof(tui->content),
                 "[File too large: %lu bytes (max %d)]", size, CONTENT_BUFFER_SIZE - 1);
        tui->content_size = strlen(tui->content);
        return;
    }

    size_t got;
    if (geofs_content_read(tui->volume, hash, tui->content, size, &got) != GEOFS_OK) {
        snprintf(tui->content, sizeof(tui->content), "[Failed to read content]");
        tui->content_size = strlen(tui->content);
        return;
    }

    tui->content[got] = '\0';
    tui->content_size = got;

    snprintf(tui->status, sizeof(tui->status),
             "Loaded: %s (%zu bytes)", path, got);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * DRAWING
 * ══════════════════════════════════════════════════════════════════════════════ */

static void draw_header(PhantomTUI *tui) {
    attron(A_REVERSE | A_BOLD);
    mvhline(0, 0, ' ', COLS);
    mvprintw(0, 2, " PHANTOM EXPLORER ");
    if (tui->volume_path[0]) {
        mvprintw(0, 22, "| %s ", tui->volume_path);
    }
    attroff(A_REVERSE | A_BOLD);
}

static void draw_file_list(PhantomTUI *tui, int start_y, int height, int width) {
    int list_width = width / 2 - 1;

    /* Border */
    attron(tui->focus_content ? A_DIM : A_BOLD);
    mvvline(start_y, list_width, ACS_VLINE, height);
    mvprintw(start_y - 1, 1, "Files");
    attroff(tui->focus_content ? A_DIM : A_BOLD);

    /* Adjust scroll */
    int visible = height - 1;
    if (tui->selected < tui->scroll_offset) {
        tui->scroll_offset = tui->selected;
    } else if (tui->selected >= tui->scroll_offset + visible) {
        tui->scroll_offset = tui->selected - visible + 1;
    }

    /* Draw files */
    for (int i = 0; i < visible && i + tui->scroll_offset < tui->file_count; i++) {
        int idx = i + tui->scroll_offset;
        FileEntry *fe = &tui->files[idx];

        int y = start_y + i;

        if (idx == tui->selected) {
            attron(A_REVERSE);
        }

        /* Clear line */
        mvhline(y, 1, ' ', list_width - 1);

        /* Icon */
        mvprintw(y, 1, "%s", fe->is_dir ? "[D]" : "   ");

        /* Name (truncated) */
        int name_width = list_width - 20;
        if ((int)strlen(fe->name) > name_width) {
            mvprintw(y, 5, "%.*s...", name_width - 3, fe->name);
        } else {
            mvprintw(y, 5, "%s", fe->name);
        }

        /* Size */
        char size_str[16];
        if (fe->size < 1024) {
            snprintf(size_str, sizeof(size_str), "%luB", fe->size);
        } else if (fe->size < 1024 * 1024) {
            snprintf(size_str, sizeof(size_str), "%.1fK", fe->size / 1024.0);
        } else {
            snprintf(size_str, sizeof(size_str), "%.1fM", fe->size / (1024.0 * 1024.0));
        }
        mvprintw(y, list_width - 12, "%8s", size_str);

        if (idx == tui->selected) {
            attroff(A_REVERSE);
        }
    }

    /* Scroll indicator */
    if (tui->file_count > visible) {
        int pos = start_y + (tui->scroll_offset * visible / tui->file_count);
        mvaddch(pos, list_width, ACS_DIAMOND);
    }
}

static void draw_content(PhantomTUI *tui, int start_y, int height, int width) {
    int content_start = width / 2 + 1;
    int content_width = width - content_start - 1;

    /* Header */
    attron(tui->focus_content ? A_BOLD : A_DIM);
    mvprintw(start_y - 1, content_start, "Content");
    attroff(tui->focus_content ? A_BOLD : A_DIM);

    /* Count lines in content */
    int lines = 1;
    for (size_t i = 0; i < tui->content_size; i++) {
        if (tui->content[i] == '\n') lines++;
    }

    /* Draw content */
    int visible = height - 1;
    int line = 0;
    int col = 0;
    int y = start_y;

    for (size_t i = 0; i < tui->content_size && y < start_y + visible; i++) {
        char c = tui->content[i];

        if (c == '\n') {
            line++;
            col = 0;
            if (line > tui->content_scroll) {
                y++;
            }
            continue;
        }

        if (line >= tui->content_scroll && col < content_width) {
            if (c >= 32 && c < 127) {
                mvaddch(y, content_start + col, c);
            } else {
                mvaddch(y, content_start + col, '.');
            }
            col++;
        } else if (col < content_width) {
            col++;
        }
    }

    /* Scroll indicator */
    if (lines > visible) {
        int pos = start_y + (tui->content_scroll * visible / lines);
        mvaddch(pos, width - 1, ACS_DIAMOND);
    }
}

static void draw_status(PhantomTUI *tui) {
    attron(A_REVERSE);
    mvhline(LINES - 2, 0, ' ', COLS);
    mvprintw(LINES - 2, 2, " %s ", tui->status);
    attroff(A_REVERSE);
}

static void draw_help(void) {
    mvprintw(LINES - 1, 2,
             "q:Quit  o:Open  u:Up  Enter:Select  Tab:Switch  v:View  h:Help  "
             "j/k:Navigate  g/G:Top/Bottom");
}

static void draw_help_overlay(void) {
    int w = 50, h = 16;
    int x = (COLS - w) / 2;
    int y = (LINES - h) / 2;

    /* Draw box */
    attron(A_REVERSE);
    for (int i = 0; i < h; i++) {
        mvhline(y + i, x, ' ', w);
    }

    mvprintw(y + 1, x + 2, "PHANTOM EXPLORER - Help");
    mvprintw(y + 2, x + 2, "\"To Create, Not To Destroy\"");
    attroff(A_REVERSE);

    mvprintw(y + 4, x + 4, "Navigation:");
    mvprintw(y + 5, x + 6, "j/Down   - Move down");
    mvprintw(y + 6, x + 6, "k/Up     - Move up");
    mvprintw(y + 7, x + 6, "Enter    - Open file/directory");
    mvprintw(y + 8, x + 6, "u        - Go up one directory");
    mvprintw(y + 9, x + 6, "Tab      - Switch focus");

    mvprintw(y + 11, x + 4, "Volume:");
    mvprintw(y + 12, x + 6, "o        - Open volume");
    mvprintw(y + 13, x + 6, "v        - Switch view (stratum)");

    mvprintw(y + 15, x + 4, "Press any key to close...");
}

static void draw_screen(PhantomTUI *tui) {
    clear();

    draw_header(tui);

    int content_start = 2;
    int content_height = LINES - 4;

    draw_file_list(tui, content_start, content_height, COLS);
    draw_content(tui, content_start, content_height, COLS);
    draw_status(tui);
    draw_help();

    if (tui->show_help) {
        draw_help_overlay();
    }

    refresh();
}

/* ══════════════════════════════════════════════════════════════════════════════
 * INPUT HANDLING
 * ══════════════════════════════════════════════════════════════════════════════ */

static void open_volume_dialog(PhantomTUI *tui) {
    char path[GEOFS_MAX_PATH];

    /* Simple path input */
    echo();
    curs_set(1);

    attron(A_REVERSE);
    mvhline(LINES / 2, 10, ' ', COLS - 20);
    mvprintw(LINES / 2, 12, "Open volume: ");
    attroff(A_REVERSE);

    getnstr(path, sizeof(path) - 1);

    noecho();
    curs_set(0);

    if (path[0] == '\0') {
        snprintf(tui->status, sizeof(tui->status), "Cancelled");
        return;
    }

    /* Close existing */
    if (tui->volume) {
        geofs_volume_close(tui->volume);
        tui->volume = NULL;
    }

    /* Open new */
    geofs_error_t err = geofs_volume_open(path, &tui->volume);
    if (err != GEOFS_OK) {
        snprintf(tui->status, sizeof(tui->status),
                 "Failed to open: %s", geofs_strerror(err));
        return;
    }

    strncpy(tui->volume_path, path, GEOFS_MAX_PATH - 1);
    strcpy(tui->current_dir, "/");
    refresh_file_list(tui);
}

static void switch_view_dialog(PhantomTUI *tui) {
    if (!tui->volume) {
        snprintf(tui->status, sizeof(tui->status), "No volume open");
        return;
    }

    char input[16];

    echo();
    curs_set(1);

    attron(A_REVERSE);
    mvhline(LINES / 2, 10, ' ', COLS - 20);
    mvprintw(LINES / 2, 12, "Switch to view: ");
    attroff(A_REVERSE);

    getnstr(input, sizeof(input) - 1);

    noecho();
    curs_set(0);

    if (input[0] == '\0') {
        snprintf(tui->status, sizeof(tui->status), "Cancelled");
        return;
    }

    geofs_view_t view = strtoull(input, NULL, 10);
    geofs_error_t err = geofs_view_switch(tui->volume, view);
    if (err != GEOFS_OK) {
        snprintf(tui->status, sizeof(tui->status),
                 "Failed to switch view: %s", geofs_strerror(err));
        return;
    }

    refresh_file_list(tui);
}

static void navigate_up(PhantomTUI *tui) {
    if (strcmp(tui->current_dir, "/") == 0) return;

    char *last = strrchr(tui->current_dir, '/');
    if (last && last != tui->current_dir) {
        *last = '\0';
    } else {
        strcpy(tui->current_dir, "/");
    }

    refresh_file_list(tui);
    tui->content[0] = '\0';
    tui->content_size = 0;
}

static void select_current(PhantomTUI *tui) {
    if (tui->file_count == 0) return;

    FileEntry *fe = &tui->files[tui->selected];

    /* Build full path */
    char path[GEOFS_MAX_PATH];
    if (strcmp(tui->current_dir, "/") == 0) {
        snprintf(path, sizeof(path), "/%s", fe->name);
    } else {
        snprintf(path, sizeof(path), "%s/%s", tui->current_dir, fe->name);
    }

    if (fe->is_dir) {
        strncpy(tui->current_dir, path, GEOFS_MAX_PATH - 1);
        refresh_file_list(tui);
    } else {
        load_content(tui, path);
    }
}

static int handle_input(PhantomTUI *tui) {
    int ch = getch();

    if (tui->show_help) {
        tui->show_help = 0;
        return 1;
    }

    switch (ch) {
        case 'q':
        case 'Q':
            return 0;

        case 'h':
        case '?':
            tui->show_help = 1;
            break;

        case 'o':
        case 'O':
            open_volume_dialog(tui);
            break;

        case 'v':
        case 'V':
            switch_view_dialog(tui);
            break;

        case 'u':
        case KEY_BACKSPACE:
            navigate_up(tui);
            break;

        case '\t':
            tui->focus_content = !tui->focus_content;
            break;

        case 'j':
        case KEY_DOWN:
            if (tui->focus_content) {
                tui->content_scroll++;
            } else if (tui->selected < tui->file_count - 1) {
                tui->selected++;
            }
            break;

        case 'k':
        case KEY_UP:
            if (tui->focus_content) {
                if (tui->content_scroll > 0) tui->content_scroll--;
            } else if (tui->selected > 0) {
                tui->selected--;
            }
            break;

        case 'g':
            if (tui->focus_content) {
                tui->content_scroll = 0;
            } else {
                tui->selected = 0;
            }
            break;

        case 'G':
            if (!tui->focus_content && tui->file_count > 0) {
                tui->selected = tui->file_count - 1;
            }
            break;

        case '\n':
        case KEY_ENTER:
            if (!tui->focus_content) {
                select_current(tui);
            }
            break;

        case KEY_RESIZE:
            /* Terminal resized */
            break;
    }

    return 1;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    PhantomTUI tui = {0};
    strcpy(tui.current_dir, "/");
    strcpy(tui.status, "Press 'o' to open a volume, 'h' for help");

    /* Initialize ncurses */
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    /* Colors */
    if (has_colors()) {
        start_color();
        use_default_colors();
    }

    /* Open volume from command line */
    if (argc > 1) {
        geofs_error_t err = geofs_volume_open(argv[1], &tui.volume);
        if (err == GEOFS_OK) {
            strncpy(tui.volume_path, argv[1], GEOFS_MAX_PATH - 1);
            refresh_file_list(&tui);
        } else {
            snprintf(tui.status, sizeof(tui.status),
                     "Failed to open %s: %s", argv[1], geofs_strerror(err));
        }
    }

    /* Main loop */
    while (1) {
        draw_screen(&tui);
        if (!handle_input(&tui)) break;
    }

    /* Cleanup */
    endwin();

    if (tui.volume) {
        geofs_volume_close(tui.volume);
    }

    printf("\n\"To Create, Not To Destroy\"\n\n");

    return 0;
}
