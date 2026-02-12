/*
 * PhantomOS Kernel Shell
 * "To Create, Not To Destroy"
 *
 * Interactive command-line shell for PhantomOS kernel.
 * All commands follow the Phantom philosophy.
 */

#include "shell.h"
#include "keyboard.h"
#include "ata.h"
#include "geofs.h"
#include "pmm.h"
#include "heap.h"
#include "process.h"
#include "governor.h"
#include "timer.h"
#include "pci.h"
#include "gpu_hal.h"
#include "usb.h"
#include "usb_hid.h"
#include "virtio_net.h"
#include <stdint.h>
#include <stddef.h>

/* External declarations */
extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern size_t strlen(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);
extern char *strcpy(char *dest, const char *src);
extern char *strncpy(char *dest, const char *src, size_t n);

/*============================================================================
 * Shell State
 *============================================================================*/

static kgeofs_volume_t *shell_volume = NULL;
static char current_path[SHELL_CMD_MAX] = "/";
static int shell_running = 0;
static uint64_t shell_start_tick = 0;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/* Parse command into arguments */
static int parse_args(char *cmd, char *argv[], int max_args)
{
    int argc = 0;
    char *p = cmd;

    while (*p && argc < max_args) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* Start of argument */
        argv[argc++] = p;

        /* Find end of argument */
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }

    return argc;
}

/* Build full path from current directory and relative path */
static void build_path(const char *relative, char *full, size_t size)
{
    if (relative[0] == '/') {
        /* Absolute path */
        strncpy(full, relative, size - 1);
        full[size - 1] = '\0';
    } else {
        /* Relative path */
        size_t cur_len = strlen(current_path);
        strncpy(full, current_path, size - 1);

        if (cur_len > 0 && current_path[cur_len - 1] != '/') {
            if (cur_len < size - 2) {
                full[cur_len] = '/';
                full[cur_len + 1] = '\0';
                cur_len++;
            }
        }

        size_t remaining = size - cur_len - 1;
        if (remaining > 0) {
            strncpy(full + cur_len, relative, remaining);
            full[size - 1] = '\0';
        }
    }
}

/* Clear screen using VGA scrolling trick */
static void clear_screen(void)
{
    /* Print many newlines to scroll content off screen */
    for (int i = 0; i < 50; i++) {
        kprintf("\n");
    }
}

/*============================================================================
 * Built-in Commands
 *============================================================================*/

/* help - Show command help */
static shell_result_t cmd_help(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    shell_help();
    return SHELL_OK;
}

/* echo - Echo text */
static shell_result_t cmd_echo(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        kprintf("%s", argv[i]);
        if (i < argc - 1) kprintf(" ");
    }
    kprintf("\n");
    return SHELL_OK;
}

/* clear - Clear screen */
static shell_result_t cmd_clear(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    clear_screen();
    return SHELL_OK;
}

/* exit - Exit shell */
/* lspci - List PCI devices */
static shell_result_t cmd_lspci(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    pci_dump_devices();
    return SHELL_OK;
}

/* gpu - Show GPU info and stats */
static shell_result_t cmd_gpu(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    gpu_hal_dump_info();
    return SHELL_OK;
}

/* usb - Show USB device information */
static shell_result_t cmd_usb(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    if (!usb_is_initialized()) {
        kprintf("USB: Not initialized (no UHCI controller found)\n");
        return SHELL_OK;
    }
    usb_dump_status();
    usb_hid_dump_status();
    return SHELL_OK;
}

static shell_result_t cmd_exit(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    kprintf("Exiting shell. Returning to kernel.\n");
    return SHELL_EXIT;
}

/* pwd - Print working directory */
static shell_result_t cmd_pwd(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    kprintf("%s\n", current_path);
    return SHELL_OK;
}

/* cd - Change directory */
static shell_result_t cmd_cd(int argc, char *argv[])
{
    if (argc < 2) {
        strcpy(current_path, "/");
        return SHELL_OK;
    }

    char new_path[SHELL_CMD_MAX];
    build_path(argv[1], new_path, sizeof(new_path));

    /* Verify directory exists using GeoFS */
    if (shell_volume) {
        uint64_t size;
        int is_dir;
        kgeofs_error_t err = kgeofs_file_stat(shell_volume, new_path, &size, &is_dir);
        if (err != KGEOFS_OK) {
            kprintf("cd: %s: No such directory\n", argv[1]);
            return SHELL_ERR_NOTFOUND;
        }
        if (!is_dir) {
            kprintf("cd: %s: Not a directory\n", argv[1]);
            return SHELL_ERR_ARGS;
        }
    }

    strcpy(current_path, new_path);
    return SHELL_OK;
}

/* Callback for ls command */
static int ls_count = 0;
static int ls_callback(const struct kgeofs_dirent *entry, void *ctx)
{
    (void)ctx;
    char type_char = entry->is_directory ? 'd' :
                     (entry->file_type == KGEOFS_TYPE_LINK) ? 'l' : '-';
    kprintf("%c %8lu  %s", type_char, (unsigned long)entry->size, entry->name);
    if (entry->file_type == KGEOFS_TYPE_LINK) {
        kprintf(" -> (symlink)");
    }
    kprintf("\n");
    ls_count++;
    return 0;  /* Continue listing */
}

/* ls - List directory contents */
static shell_result_t cmd_ls(int argc, char *argv[])
{
    char path[SHELL_CMD_MAX];

    if (argc > 1) {
        build_path(argv[1], path, sizeof(path));
    } else {
        strcpy(path, current_path);
    }

    if (!shell_volume) {
        kprintf("ls: No filesystem mounted\n");
        return SHELL_ERR_IO;
    }

    ls_count = 0;
    int result = kgeofs_ref_list(shell_volume, path, ls_callback, NULL);

    if (result < 0) {
        kprintf("ls: %s: Cannot list directory\n", path);
        return SHELL_ERR_IO;
    }

    if (ls_count == 0) {
        kprintf("(empty directory)\n");
    }

    return SHELL_OK;
}

/* cat - Display file contents */
static shell_result_t cmd_cat(int argc, char *argv[])
{
    if (argc < 2) {
        kprintf("Usage: cat <file>\n");
        return SHELL_ERR_ARGS;
    }

    if (!shell_volume) {
        kprintf("cat: No filesystem mounted\n");
        return SHELL_ERR_IO;
    }

    char path[SHELL_CMD_MAX];
    build_path(argv[1], path, sizeof(path));

    /* Read file in chunks */
    char buf[512];
    size_t size_out;
    kgeofs_error_t err = kgeofs_file_read(shell_volume, path, buf, sizeof(buf) - 1, &size_out);

    if (err != KGEOFS_OK) {
        kprintf("cat: %s: Cannot read file\n", argv[1]);
        return SHELL_ERR_IO;
    }

    buf[size_out] = '\0';
    kprintf("%s", buf);
    if (size_out > 0 && buf[size_out - 1] != '\n') {
        kprintf("\n");
    }

    return SHELL_OK;
}

/* write - Write text to file */
static shell_result_t cmd_write(int argc, char *argv[])
{
    if (argc < 3) {
        kprintf("Usage: write <file> <text...>\n");
        return SHELL_ERR_ARGS;
    }

    if (!shell_volume) {
        kprintf("write: No filesystem mounted\n");
        return SHELL_ERR_IO;
    }

    /* Note: Writing new content is allowed under Phantom philosophy
     * (content is append-only, never overwritten) */

    char path[SHELL_CMD_MAX];
    build_path(argv[1], path, sizeof(path));

    /* Build content from remaining arguments */
    char content[SHELL_CMD_MAX];
    content[0] = '\0';
    size_t pos = 0;

    for (int i = 2; i < argc && pos < sizeof(content) - 2; i++) {
        size_t len = strlen(argv[i]);
        if (pos + len + 1 < sizeof(content)) {
            strcpy(content + pos, argv[i]);
            pos += len;
            if (i < argc - 1) {
                content[pos++] = ' ';
            }
        }
    }
    content[pos++] = '\n';
    content[pos] = '\0';

    kgeofs_error_t err = kgeofs_file_write(shell_volume, path, content, pos);
    if (err != KGEOFS_OK) {
        kprintf("write: Failed to write %s\n", path);
        return SHELL_ERR_IO;
    }

    kprintf("Wrote %lu bytes to %s\n", (unsigned long)pos, path);
    return SHELL_OK;
}

/* mkdir - Create directory */
static shell_result_t cmd_mkdir(int argc, char *argv[])
{
    if (argc < 2) {
        kprintf("Usage: mkdir <path>\n");
        return SHELL_ERR_ARGS;
    }

    if (!shell_volume) {
        kprintf("mkdir: No filesystem mounted\n");
        return SHELL_ERR_IO;
    }

    char path[SHELL_CMD_MAX];
    build_path(argv[1], path, sizeof(path));

    kgeofs_error_t err = kgeofs_mkdir(shell_volume, path);
    if (err != KGEOFS_OK) {
        kprintf("mkdir: Cannot create directory %s\n", path);
        return SHELL_ERR_IO;
    }

    kprintf("Created directory: %s\n", path);
    return SHELL_OK;
}

/* hide - Hide file (Phantom delete) */
static shell_result_t cmd_hide(int argc, char *argv[])
{
    if (argc < 2) {
        kprintf("Usage: hide <file>\n");
        return SHELL_ERR_ARGS;
    }

    if (!shell_volume) {
        kprintf("hide: No filesystem mounted\n");
        return SHELL_ERR_IO;
    }

    char path[SHELL_CMD_MAX];
    build_path(argv[1], path, sizeof(path));

    /* Governor check - hide is the approved operation */
    char reason[128];
    gov_verdict_t verdict = governor_check_filesystem(POLICY_FS_HIDE, path,
                                                       GOV_CAP_KERNEL, reason);
    if (verdict != GOV_ALLOW) {
        kprintf("hide: Governor declined: %s\n", reason);
        return SHELL_ERR_DECLINED;
    }

    kgeofs_error_t err = kgeofs_view_hide(shell_volume, path);
    if (err != KGEOFS_OK) {
        kprintf("hide: Cannot hide %s\n", path);
        return SHELL_ERR_IO;
    }

    kprintf("Hidden: %s (preserved in geological history)\n", path);
    return SHELL_OK;
}

/* stat - Show file info */
static shell_result_t cmd_stat(int argc, char *argv[])
{
    if (argc < 2) {
        kprintf("Usage: stat <path>\n");
        return SHELL_ERR_ARGS;
    }

    if (!shell_volume) {
        kprintf("stat: No filesystem mounted\n");
        return SHELL_ERR_IO;
    }

    char path[SHELL_CMD_MAX];
    build_path(argv[1], path, sizeof(path));

    uint64_t size;
    int is_dir, link_count;
    uint8_t file_type, permissions;
    uint16_t owner_id;
    kgeofs_time_t created;
    kgeofs_error_t err = kgeofs_file_stat_full(shell_volume, path,
                                                &size, &is_dir, &file_type,
                                                &permissions, &owner_id,
                                                &created, &link_count);
    if (err != KGEOFS_OK) {
        kprintf("stat: Cannot stat %s\n", path);
        return SHELL_ERR_IO;
    }

    const char *type_str = is_dir ? "directory" : "file";
    if (file_type == KGEOFS_TYPE_LINK) type_str = "symlink";

    kprintf("  Path:  %s\n", path);
    kprintf("  Type:  %s\n", type_str);
    kprintf("  Size:  %lu bytes\n", (unsigned long)size);
    kprintf("  Links: %d\n", link_count);
    kprintf("  Perms: %c%c%c\n",
            (permissions & KGEOFS_PERM_READ) ? 'r' : '-',
            (permissions & KGEOFS_PERM_WRITE) ? 'w' : '-',
            (permissions & KGEOFS_PERM_EXEC) ? 'x' : '-');
    kprintf("  Owner: %u\n", (unsigned)owner_id);
    kprintf("  View:  %lu\n", (unsigned long)kgeofs_view_current(shell_volume));

    if (file_type == KGEOFS_TYPE_LINK) {
        char target[KGEOFS_MAX_PATH];
        if (kgeofs_readlink(shell_volume, path, target, sizeof(target)) == KGEOFS_OK) {
            kprintf("  Target: %s\n", target);
        }
    }

    return SHELL_OK;
}

/* views - List all views */
static shell_result_t cmd_views(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (!shell_volume) {
        kprintf("views: No filesystem mounted\n");
        return SHELL_ERR_IO;
    }

    kprintf("Geological Strata (Views):\n");
    kgeofs_dump_views(shell_volume);

    return SHELL_OK;
}

/* view - Switch to view */
static shell_result_t cmd_view(int argc, char *argv[])
{
    if (argc < 2) {
        kprintf("Usage: view <id>\n");
        return SHELL_ERR_ARGS;
    }

    if (!shell_volume) {
        kprintf("view: No filesystem mounted\n");
        return SHELL_ERR_IO;
    }

    /* Parse view ID */
    uint64_t view_id = 0;
    for (const char *p = argv[1]; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            view_id = view_id * 10 + (*p - '0');
        } else {
            kprintf("view: Invalid view ID\n");
            return SHELL_ERR_ARGS;
        }
    }

    kgeofs_error_t err = kgeofs_view_switch(shell_volume, view_id);
    if (err != KGEOFS_OK) {
        kprintf("view: Cannot switch to view %lu\n", (unsigned long)view_id);
        return SHELL_ERR_IO;
    }

    kprintf("Switched to view %lu\n", (unsigned long)view_id);
    return SHELL_OK;
}

/* snapshot - Create new view */
static shell_result_t cmd_snapshot(int argc, char *argv[])
{
    if (argc < 2) {
        kprintf("Usage: snapshot <name>\n");
        return SHELL_ERR_ARGS;
    }

    if (!shell_volume) {
        kprintf("snapshot: No filesystem mounted\n");
        return SHELL_ERR_IO;
    }

    kgeofs_view_t new_view;
    kgeofs_error_t err = kgeofs_view_create(shell_volume, argv[1], &new_view);
    if (err != KGEOFS_OK) {
        kprintf("snapshot: Cannot create view\n");
        return SHELL_ERR_IO;
    }

    kprintf("Created view %lu: \"%s\"\n", (unsigned long)new_view, argv[1]);
    return SHELL_OK;
}

/* ps - List processes */
static shell_result_t cmd_ps(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Dump process list from scheduler */
    extern void sched_dump(void);
    sched_dump();

    return SHELL_OK;
}

/* mem - Show memory statistics */
static shell_result_t cmd_mem(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    kprintf("Memory Statistics:\n");

    /* PMM stats */
    const struct pmm_stats *pmm = pmm_get_stats();
    kprintf("  Physical Memory:\n");
    kprintf("    Total pages:  %lu\n", (unsigned long)pmm->total_pages);
    kprintf("    Free pages:   %lu\n", (unsigned long)pmm->free_pages);
    kprintf("    Used pages:   %lu\n", (unsigned long)pmm->used_pages);
    kprintf("    Page size:    4096 bytes\n");

    /* Heap stats */
    const struct heap_stats *heap = heap_get_stats();
    kprintf("  Kernel Heap:\n");
    kprintf("    Total size:   %lu bytes\n", (unsigned long)heap->total_size);
    kprintf("    Used:         %lu bytes\n", (unsigned long)heap->used_size);
    kprintf("    Free:         %lu bytes\n", (unsigned long)heap->free_size);
    kprintf("    Allocations:  %lu\n", (unsigned long)heap->total_allocations);

    return SHELL_OK;
}

/* disk - Show disk info */
static shell_result_t cmd_disk(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    kprintf("Disk Information:\n");

    const ata_drive_t *drive0 = ata_get_drive(0);
    const ata_drive_t *drive1 = ata_get_drive(1);

    if (drive0 && drive0->type != ATA_TYPE_NONE) {
        kprintf("  Drive 0 (Primary Master):\n");
        kprintf("    Model:    %s\n", drive0->model);
        kprintf("    Serial:   %s\n", drive0->serial);
        kprintf("    Sectors:  %lu\n", (unsigned long)drive0->sectors);
        kprintf("    Size:     %lu MB\n", (unsigned long)drive0->size_mb);
        kprintf("    LBA48:    %s\n", drive0->lba48 ? "yes" : "no");
    } else {
        kprintf("  Drive 0: Not present\n");
    }

    if (drive1 && drive1->type != ATA_TYPE_NONE) {
        kprintf("  Drive 1 (Primary Slave):\n");
        kprintf("    Model:    %s\n", drive1->model);
        kprintf("    Serial:   %s\n", drive1->serial);
        kprintf("    Sectors:  %lu\n", (unsigned long)drive1->sectors);
        kprintf("    Size:     %lu MB\n", (unsigned long)drive1->size_mb);
        kprintf("    LBA48:    %s\n", drive1->lba48 ? "yes" : "no");
    } else {
        kprintf("  Drive 1: Not present\n");
    }

    return SHELL_OK;
}

/* gov - Show Governor statistics */
static shell_result_t cmd_gov(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    kprintf("Governor Statistics:\n");
    governor_dump_stats();

    kprintf("\nRecent Audit Log:\n");
    governor_dump_audit(10);

    return SHELL_OK;
}

/* uptime - Show system uptime */
static shell_result_t cmd_uptime(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    uint64_t ticks = timer_get_ticks();
    uint64_t seconds = ticks / 100;  /* 100 Hz timer */
    uint64_t minutes = seconds / 60;
    uint64_t hours = minutes / 60;

    kprintf("Uptime: %lu:%02lu:%02lu (%lu ticks)\n",
            (unsigned long)hours,
            (unsigned long)(minutes % 60),
            (unsigned long)(seconds % 60),
            (unsigned long)ticks);

    return SHELL_OK;
}

/*============================================================================
 * Extended Filesystem Commands
 *============================================================================*/

/* append - Append text to file */
static shell_result_t cmd_append(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }
    if (argc < 3) {
        kprintf("Usage: append <file> <text>\n");
        return SHELL_OK;
    }

    char full[SHELL_CMD_MAX];
    build_path(argv[1], full, sizeof(full));

    /* Build text from remaining args */
    char text[1024];
    int pos = 0;
    for (int i = 2; i < argc && pos < 1020; i++) {
        if (i > 2) text[pos++] = ' ';
        const char *s = argv[i];
        while (*s && pos < 1020) text[pos++] = *s++;
    }
    text[pos++] = '\n';
    text[pos] = '\0';

    kgeofs_error_t err = kgeofs_file_append(shell_volume, full, text, (size_t)pos);
    if (err != KGEOFS_OK) {
        kprintf("append: %s\n", kgeofs_strerror(err));
    } else {
        kprintf("Appended %d bytes to %s\n", pos, full);
    }
    return SHELL_OK;
}

/* mv - Rename/move a file */
static shell_result_t cmd_mv(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }
    if (argc < 3) {
        kprintf("Usage: mv <source> <dest>\n");
        return SHELL_OK;
    }

    char src[SHELL_CMD_MAX], dst[SHELL_CMD_MAX];
    build_path(argv[1], src, sizeof(src));
    build_path(argv[2], dst, sizeof(dst));

    kgeofs_error_t err = kgeofs_file_rename(shell_volume, src, dst);
    if (err != KGEOFS_OK) {
        kprintf("mv: %s\n", kgeofs_strerror(err));
    } else {
        kprintf("Moved %s -> %s\n", src, dst);
    }
    return SHELL_OK;
}

/* cp - Copy a file */
static shell_result_t cmd_cp(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }
    if (argc < 3) {
        kprintf("Usage: cp <source> <dest>\n");
        return SHELL_OK;
    }

    char src[SHELL_CMD_MAX], dst[SHELL_CMD_MAX];
    build_path(argv[1], src, sizeof(src));
    build_path(argv[2], dst, sizeof(dst));

    kgeofs_error_t err = kgeofs_file_copy(shell_volume, src, dst);
    if (err != KGEOFS_OK) {
        kprintf("cp: %s\n", kgeofs_strerror(err));
    } else {
        kprintf("Copied %s -> %s (zero-copy dedup)\n", src, dst);
    }
    return SHELL_OK;
}

/* tree - Recursive directory listing */
static int tree_print_cb(const char *full_path, const struct kgeofs_dirent *entry,
                          int depth, void *ctx)
{
    (void)full_path; (void)ctx;
    /* Indent by depth */
    for (int i = 0; i < depth; i++) kprintf("  ");

    if (entry->is_directory) {
        kprintf("[D] %s/\n", entry->name);
    } else {
        kprintf("    %s (%lu bytes)\n", entry->name, (unsigned long)entry->size);
    }
    return 0;
}

static shell_result_t cmd_tree(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }

    char path[SHELL_CMD_MAX];
    if (argc >= 2) {
        build_path(argv[1], path, sizeof(path));
    } else {
        strcpy(path, current_path);
    }

    kprintf("Tree: %s\n", path);
    int count = kgeofs_ref_list_recursive(shell_volume, path, 8,
                                           tree_print_cb, NULL);
    kprintf("\n%d entries\n", count);
    return SHELL_OK;
}

/* find - Search files by name */
static int find_print_cb(const char *path, uint64_t size, int is_dir, void *ctx)
{
    (void)ctx;
    if (is_dir) {
        kprintf("  [D] %s\n", path);
    } else {
        kprintf("  %s (%lu bytes)\n", path, (unsigned long)size);
    }
    return 0;
}

static shell_result_t cmd_find(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }
    if (argc < 2) {
        kprintf("Usage: find <pattern> [path]\n");
        return SHELL_OK;
    }

    const char *start = current_path;
    if (argc >= 3) start = argv[2];

    kprintf("Searching for '%s' in %s:\n", argv[1], start);
    int count = kgeofs_file_find(shell_volume, start, argv[1],
                                  find_print_cb, NULL);
    kprintf("\n%d matches\n", count);
    return SHELL_OK;
}

/* grep - Search file contents */
static int grep_print_cb(const char *path, int line_num,
                          const char *line, void *ctx)
{
    (void)ctx;
    kprintf("  %s:%d: %s\n", path, line_num, line);
    return 0;
}

static shell_result_t cmd_grep(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }
    if (argc < 2) {
        kprintf("Usage: grep <pattern> [path]\n");
        return SHELL_OK;
    }

    const char *start = current_path;
    if (argc >= 3) {
        char path[SHELL_CMD_MAX];
        build_path(argv[2], path, sizeof(path));
        start = path;
    }

    kprintf("Searching for '%s' in %s:\n", argv[1], start);
    int count = kgeofs_file_grep(shell_volume, start, argv[1], 1,
                                  grep_print_cb, NULL);
    kprintf("\n%d matches\n", count);
    return SHELL_OK;
}

/* ln - Create hard or symbolic link */
static shell_result_t cmd_ln(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }

    int symlink = 0;
    int arg_start = 1;

    if (argc >= 2 && strcmp(argv[1], "-s") == 0) {
        symlink = 1;
        arg_start = 2;
    }

    if (argc < arg_start + 2) {
        kprintf("Usage: ln [-s] <target> <linkname>\n");
        return SHELL_OK;
    }

    char target[SHELL_CMD_MAX], linkname[SHELL_CMD_MAX];
    build_path(argv[arg_start], target, sizeof(target));
    build_path(argv[arg_start + 1], linkname, sizeof(linkname));

    kgeofs_error_t err;
    if (symlink) {
        err = kgeofs_file_symlink(shell_volume, target, linkname);
    } else {
        err = kgeofs_file_link(shell_volume, target, linkname);
    }

    if (err != KGEOFS_OK) {
        kprintf("ln: %s\n", kgeofs_strerror(err));
    } else {
        kprintf("Created %slink %s -> %s\n",
                symlink ? "sym" : "hard", linkname, target);
    }
    return SHELL_OK;
}

/* readlink - Read symlink target */
static shell_result_t cmd_readlink(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }
    if (argc < 2) {
        kprintf("Usage: readlink <path>\n");
        return SHELL_OK;
    }

    char path[SHELL_CMD_MAX];
    build_path(argv[1], path, sizeof(path));

    char target[KGEOFS_MAX_PATH];
    kgeofs_error_t err = kgeofs_readlink(shell_volume, path, target, sizeof(target));
    if (err != KGEOFS_OK) {
        kprintf("readlink: %s\n", kgeofs_strerror(err));
    } else {
        kprintf("%s\n", target);
    }
    return SHELL_OK;
}

/* diff - Show changes between views */
static int diff_print_cb(const struct kgeofs_diff_entry *entry, void *ctx)
{
    (void)ctx;
    const char *type = "???";
    if (entry->change_type == 0) type = "ADD";
    else if (entry->change_type == 1) type = "MOD";
    else if (entry->change_type == 2) type = "HID";

    kprintf("  [%s] %s (view %lu)\n", type, entry->path,
            (unsigned long)entry->view_id);
    return 0;
}

static shell_result_t cmd_diff(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }

    /* diff -b <branch_a> <branch_b> : branch diff */
    if (argc >= 4 && strcmp(argv[1], "-b") == 0) {
        /* Find branches by name */
        struct kgeofs_branch_entry *entry;
        kgeofs_branch_t ba = (kgeofs_branch_t)-1, bb = (kgeofs_branch_t)-1;

        for (entry = shell_volume->branch_index; entry; entry = entry->next) {
            if (strcmp(entry->name, argv[2]) == 0) ba = entry->id;
            if (strcmp(entry->name, argv[3]) == 0) bb = entry->id;
        }
        if (ba == (kgeofs_branch_t)-1) {
            kprintf("diff: branch '%s' not found\n", argv[2]);
            return SHELL_OK;
        }
        if (bb == (kgeofs_branch_t)-1) {
            kprintf("diff: branch '%s' not found\n", argv[3]);
            return SHELL_OK;
        }

        kprintf("Diff between branch '%s' and '%s':\n", argv[2], argv[3]);
        int count = kgeofs_branch_diff(shell_volume, ba, bb,
                                        diff_print_cb, NULL);
        kprintf("\n%d changes\n", count);
        return SHELL_OK;
    }

    if (argc < 3) {
        kprintf("Usage: diff <view_a> <view_b>\n");
        kprintf("       diff -b <branch_a> <branch_b>\n");
        return SHELL_OK;
    }

    /* Simple string-to-int */
    kgeofs_view_t va = 0, vb = 0;
    for (const char *p = argv[1]; *p >= '0' && *p <= '9'; p++)
        va = va * 10 + (uint64_t)(*p - '0');
    for (const char *p = argv[2]; *p >= '0' && *p <= '9'; p++)
        vb = vb * 10 + (uint64_t)(*p - '0');

    kprintf("Diff between view %lu and view %lu:\n",
            (unsigned long)va, (unsigned long)vb);
    int count = kgeofs_view_diff(shell_volume, va, vb, diff_print_cb, NULL);
    kprintf("\n%d changes\n", count);
    return SHELL_OK;
}

/* chmod - Set file permissions */
static shell_result_t cmd_chmod(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }
    if (argc < 3) {
        kprintf("Usage: chmod <perms> <file>\n");
        kprintf("  perms: r=read, w=write, x=exec (e.g. rwx, rw, rx)\n");
        return SHELL_OK;
    }

    uint8_t perms = 0;
    for (const char *p = argv[1]; *p; p++) {
        if (*p == 'r') perms |= KGEOFS_PERM_READ;
        else if (*p == 'w') perms |= KGEOFS_PERM_WRITE;
        else if (*p == 'x') perms |= KGEOFS_PERM_EXEC;
    }

    char full[SHELL_CMD_MAX];
    build_path(argv[2], full, sizeof(full));

    kgeofs_error_t err = kgeofs_file_chmod(shell_volume, full, perms);
    if (err != KGEOFS_OK) {
        kprintf("chmod: %s\n", kgeofs_strerror(err));
    } else {
        kprintf("Permissions set: %c%c%c on %s\n",
                (perms & KGEOFS_PERM_READ) ? 'r' : '-',
                (perms & KGEOFS_PERM_WRITE) ? 'w' : '-',
                (perms & KGEOFS_PERM_EXEC) ? 'x' : '-',
                full);
    }
    return SHELL_OK;
}

/* chown - Set file owner */
static shell_result_t cmd_chown(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }
    if (argc < 3) {
        kprintf("Usage: chown <uid> <file>\n");
        return SHELL_OK;
    }

    uint16_t uid = 0;
    for (const char *p = argv[1]; *p >= '0' && *p <= '9'; p++)
        uid = uid * 10 + (uint16_t)(*p - '0');

    char full[SHELL_CMD_MAX];
    build_path(argv[2], full, sizeof(full));

    kgeofs_error_t err = kgeofs_file_chown(shell_volume, full, uid);
    if (err != KGEOFS_OK) {
        kprintf("chown: %s\n", kgeofs_strerror(err));
    } else {
        kprintf("Owner set to uid %u on %s\n", (unsigned)uid, full);
    }
    return SHELL_OK;
}

/* export - Export file to ATA disk */
static shell_result_t cmd_export(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }
    if (argc < 3) {
        kprintf("Usage: export <file> <sector>\n");
        kprintf("  Writes file to ATA drive 0 starting at sector\n");
        return SHELL_OK;
    }

    char full[SHELL_CMD_MAX];
    build_path(argv[1], full, sizeof(full));

    uint64_t sector = 0;
    for (const char *p = argv[2]; *p >= '0' && *p <= '9'; p++)
        sector = sector * 10 + (uint64_t)(*p - '0');

    uint64_t written = 0;
    kgeofs_error_t err = kgeofs_file_export_ata(shell_volume, full, 0,
                                                 sector, &written);
    if (err != KGEOFS_OK) {
        kprintf("export: %s\n", kgeofs_strerror(err));
    } else {
        kprintf("Exported %s -> ATA sector %lu (%lu sectors)\n",
                full, (unsigned long)sector, (unsigned long)written);
    }
    return SHELL_OK;
}

/* import - Import file from ATA disk */
static shell_result_t cmd_import(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }
    if (argc < 4) {
        kprintf("Usage: import <file> <sector> <count>\n");
        kprintf("  Reads <count> sectors from ATA drive 0\n");
        return SHELL_OK;
    }

    char full[SHELL_CMD_MAX];
    build_path(argv[1], full, sizeof(full));

    uint64_t sector = 0, count = 0;
    for (const char *p = argv[2]; *p >= '0' && *p <= '9'; p++)
        sector = sector * 10 + (uint64_t)(*p - '0');
    for (const char *p = argv[3]; *p >= '0' && *p <= '9'; p++)
        count = count * 10 + (uint64_t)(*p - '0');

    kgeofs_error_t err = kgeofs_file_import_ata(shell_volume, full, 0,
                                                 sector, count);
    if (err != KGEOFS_OK) {
        kprintf("import: %s\n", kgeofs_strerror(err));
    } else {
        kprintf("Imported ATA sector %lu (%lu sectors) -> %s\n",
                (unsigned long)sector, (unsigned long)count, full);
    }
    return SHELL_OK;
}

/*============================================================================
 * Volume Persistence Commands
 *============================================================================*/

static shell_result_t cmd_save(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }

    /* Default: drive 0, sector 2048 (1MB offset) */
    uint64_t sector = 2048;
    if (argc >= 2) {
        sector = 0;
        for (const char *p = argv[1]; *p >= '0' && *p <= '9'; p++)
            sector = sector * 10 + (uint64_t)(*p - '0');
    }

    kgeofs_error_t err = kgeofs_volume_save(shell_volume, 0, sector);
    if (err != KGEOFS_OK) {
        kprintf("save: %s\n", kgeofs_strerror(err));
    } else {
        kprintf("Volume saved to drive 0 sector %lu\n", (unsigned long)sector);
    }
    return SHELL_OK;
}

static shell_result_t cmd_load(int argc, char *argv[])
{
    /* Default: drive 0, sector 2048 */
    uint64_t sector = 2048;
    if (argc >= 2) {
        sector = 0;
        for (const char *p = argv[1]; *p >= '0' && *p <= '9'; p++)
            sector = sector * 10 + (uint64_t)(*p - '0');
    }

    kgeofs_volume_t *new_vol = NULL;
    kgeofs_error_t err = kgeofs_volume_load(0, sector, &new_vol);
    if (err != KGEOFS_OK) {
        kprintf("load: %s\n", kgeofs_strerror(err));
        return SHELL_OK;
    }

    /* Replace current volume (old stays in memory — Phantom philosophy) */
    shell_volume = new_vol;
    kprintf("Volume loaded from drive 0 sector %lu\n", (unsigned long)sector);
    return SHELL_OK;
}

/*============================================================================
 * Branch Commands
 *============================================================================*/

static void branch_list_cb(kgeofs_branch_t id, const char *name,
                            kgeofs_view_t base_view, kgeofs_view_t head_view,
                            kgeofs_time_t created, void *ctx)
{
    (void)created;
    kgeofs_volume_t *vol = (kgeofs_volume_t *)ctx;
    const char *marker = (id == kgeofs_branch_current(vol)) ? " *" : "";
    kprintf("  [%lu] %s (base=%lu head=%lu)%s\n",
            (unsigned long)id, name,
            (unsigned long)base_view, (unsigned long)head_view, marker);
}

static shell_result_t cmd_branch(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }

    /* branch -s <name> : switch to branch */
    if (argc >= 3 && strcmp(argv[1], "-s") == 0) {
        kgeofs_error_t err = kgeofs_branch_switch_name(shell_volume, argv[2]);
        if (err != KGEOFS_OK) {
            kprintf("branch: %s\n", kgeofs_strerror(err));
        } else {
            kprintf("Switched to branch '%s'\n", argv[2]);
        }
        return SHELL_OK;
    }

    /* branch <name> : create new branch */
    if (argc >= 2) {
        kgeofs_branch_t new_id;
        kgeofs_error_t err = kgeofs_branch_create(shell_volume, argv[1], &new_id);
        if (err != KGEOFS_OK) {
            kprintf("branch: %s\n", kgeofs_strerror(err));
        } else {
            kprintf("Created branch '%s' (id=%lu)\n",
                    argv[1], (unsigned long)new_id);
        }
        return SHELL_OK;
    }

    /* branch : list all branches */
    kprintf("Branches (* = current):\n");
    int count = kgeofs_branch_list(shell_volume, branch_list_cb, shell_volume);
    kprintf("\n%d branch%s\n", count, count == 1 ? "" : "es");
    return SHELL_OK;
}

/* merge - Merge branch into current */
static shell_result_t cmd_merge(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }
    if (argc < 2) {
        kprintf("Usage: merge <branch_name>\n");
        return SHELL_OK;
    }

    /* Find source branch by name */
    struct kgeofs_branch_entry *entry = shell_volume->branch_index;
    kgeofs_branch_t source = (kgeofs_branch_t)-1;
    while (entry) {
        if (strcmp(entry->name, argv[1]) == 0) {
            source = entry->id;
            break;
        }
        entry = entry->next;
    }
    if (source == (kgeofs_branch_t)-1) {
        kprintf("merge: branch '%s' not found\n", argv[1]);
        return SHELL_OK;
    }

    /* Build merge label */
    char label[128];
    strncpy(label, "Merge: ", sizeof(label) - 1);
    strncpy(label + 7, argv[1], sizeof(label) - 8);
    label[sizeof(label) - 1] = '\0';

    int conflicts = 0;
    kgeofs_error_t err = kgeofs_branch_merge(shell_volume, source, label,
                                              &conflicts);
    if (err == KGEOFS_ERR_CONFLICT) {
        kprintf("Merged with %d conflict%s (skipped)\n",
                conflicts, conflicts == 1 ? "" : "s");
    } else if (err != KGEOFS_OK) {
        kprintf("merge: %s\n", kgeofs_strerror(err));
    } else {
        kprintf("Merged '%s' into current branch\n", argv[1]);
    }
    return SHELL_OK;
}

/*============================================================================
 * Access Control Commands
 *============================================================================*/

/* su - Switch user context */
static shell_result_t cmd_su(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }

    struct kgeofs_access_ctx ctx;

    if (argc < 2) {
        /* su with no args: back to root */
        ctx.uid = 0;
        ctx.gid = 0;
        ctx.caps = GOV_CAPS_KERNEL;
        kgeofs_set_context(shell_volume, &ctx);
        kprintf("Switched to root (uid=0)\n");
        return SHELL_OK;
    }

    /* Parse uid */
    uint16_t uid = 0;
    for (const char *p = argv[1]; *p >= '0' && *p <= '9'; p++)
        uid = uid * 10 + (uint16_t)(*p - '0');

    ctx.uid = uid;
    ctx.gid = uid;  /* Simple: gid = uid */
    ctx.caps = 0;   /* No special caps for non-root */
    kgeofs_set_context(shell_volume, &ctx);
    kprintf("Switched to uid=%u\n", (unsigned)uid);
    return SHELL_OK;
}

/* whoami - Show current identity */
static shell_result_t cmd_whoami(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }

    const struct kgeofs_access_ctx *ctx = kgeofs_get_context(shell_volume);
    kprintf("uid=%u gid=%u caps=0x%x", (unsigned)ctx->uid,
            (unsigned)ctx->gid, (unsigned)ctx->caps);
    if (ctx->caps & GOV_CAP_KERNEL)
        kprintf(" [KERNEL]");
    if (ctx->caps & GOV_CAP_FS_ADMIN)
        kprintf(" [FS_ADMIN]");
    kprintf("\n");
    return SHELL_OK;
}

/*============================================================================
 * Quota Commands
 *============================================================================*/

static shell_result_t cmd_quota(int argc, char *argv[])
{
    if (!shell_volume) {
        kprintf("No filesystem volume\n");
        return SHELL_OK;
    }

    /* quota set [-b] <bytes> */
    if (argc >= 3 && strcmp(argv[1], "set") == 0) {
        kgeofs_branch_t target;
        const char *bytes_str;

        if (argc >= 4 && strcmp(argv[2], "-b") == 0) {
            /* Branch-specific quota */
            target = shell_volume->current_branch;
            bytes_str = argv[3];
        } else {
            /* Volume-wide quota */
            target = KGEOFS_QUOTA_VOLUME;
            bytes_str = argv[2];
        }

        uint64_t max_bytes = 0;
        for (const char *p = bytes_str; *p >= '0' && *p <= '9'; p++)
            max_bytes = max_bytes * 10 + (uint64_t)(*p - '0');

        struct kgeofs_quota limits;
        limits.max_content_bytes = max_bytes;
        limits.max_ref_count = 0;     /* unlimited */
        limits.max_view_count = 0;    /* unlimited */

        kgeofs_error_t err = kgeofs_quota_set(shell_volume, target, &limits);
        if (err != KGEOFS_OK) {
            kprintf("quota: %s\n", kgeofs_strerror(err));
        } else {
            if (target == KGEOFS_QUOTA_VOLUME)
                kprintf("Volume quota set: %lu bytes\n",
                        (unsigned long)max_bytes);
            else
                kprintf("Branch %lu quota set: %lu bytes\n",
                        (unsigned long)target, (unsigned long)max_bytes);
        }
        return SHELL_OK;
    }

    /* quota (no args): show usage */
    kgeofs_branch_t cur = shell_volume->current_branch;
    uint64_t bytes = 0, refs = 0, views = 0;
    kgeofs_quota_usage(shell_volume, cur, &bytes, &refs, &views);

    kprintf("Branch %lu usage:\n", (unsigned long)cur);
    kprintf("  Content: %lu bytes\n", (unsigned long)bytes);
    kprintf("  Refs:    %lu\n", (unsigned long)refs);
    kprintf("  Views:   %lu\n", (unsigned long)views);

    /* Show limits if set */
    struct kgeofs_quota limits;
    if (kgeofs_quota_get(shell_volume, cur, &limits) == KGEOFS_OK) {
        if (limits.max_content_bytes > 0)
            kprintf("  Limit:   %lu bytes\n",
                    (unsigned long)limits.max_content_bytes);
    }
    if (kgeofs_quota_get(shell_volume, KGEOFS_QUOTA_VOLUME, &limits) == KGEOFS_OK) {
        if (limits.max_content_bytes > 0)
            kprintf("  Volume:  %lu bytes\n",
                    (unsigned long)limits.max_content_bytes);
    }

    return SHELL_OK;
}

/*============================================================================
 * Command Table
 *============================================================================*/

typedef shell_result_t (*cmd_handler_t)(int argc, char *argv[]);

typedef struct {
    const char *name;
    cmd_handler_t handler;
    const char *description;
} shell_cmd_t;

/*============================================================================
 * Network Commands
 *============================================================================*/

static shell_result_t cmd_net(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    virtio_net_dump_info();
    return SHELL_OK;
}

static uint32_t parse_ip(const char *s)
{
    uint32_t a = 0, b = 0, c = 0, d = 0;
    int field = 0, val = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            val = val * 10 + (s[i] - '0');
        } else if (s[i] == '.') {
            if (field == 0) a = (uint32_t)val;
            else if (field == 1) b = (uint32_t)val;
            else if (field == 2) c = (uint32_t)val;
            val = 0;
            field++;
        }
    }
    d = (uint32_t)val;
    return (a << 24) | (b << 16) | (c << 8) | d;
}

static shell_result_t cmd_ping(int argc, char *argv[])
{
    if (!virtio_net_available()) {
        kprintf("Network not available\n");
        return SHELL_ERR_IO;
    }

    uint32_t target_ip = 0x0A000202;  /* 10.0.2.2 */
    const char *target_str = "10.0.2.2";

    if (argc >= 2) {
        target_ip = parse_ip(argv[1]);
        target_str = argv[1];
    }

    kprintf("PING %s:\n", target_str);

    int success = 0;
    for (int i = 0; i < 4; i++) {
        if (virtio_net_ping(target_ip, (uint16_t)(i + 1)) != 0) {
            kprintf("  seq=%d: send failed\n", i + 1);
            continue;
        }

        int rtt = -1;
        for (int t = 0; t < 200; t++) {
            timer_sleep_ms(10);
            rtt = virtio_net_ping_check();
            if (rtt >= 0) break;
        }

        if (rtt >= 0) {
            kprintf("  Reply from %s: seq=%d time=%dms\n",
                    target_str, i + 1, rtt);
            success++;
        } else {
            kprintf("  seq=%d: Request timed out\n", i + 1);
        }
    }

    kprintf("--- %d/4 packets received ---\n", success);
    return SHELL_OK;
}

static const shell_cmd_t commands[] = {
    /* Filesystem commands */
    { "ls",       cmd_ls,       "List directory contents" },
    { "cat",      cmd_cat,      "Display file contents" },
    { "write",    cmd_write,    "Write text to file" },
    { "append",   cmd_append,   "Append text to file" },
    { "mkdir",    cmd_mkdir,    "Create directory" },
    { "hide",     cmd_hide,     "Hide file (Phantom delete)" },
    { "pwd",      cmd_pwd,      "Print working directory" },
    { "cd",       cmd_cd,       "Change directory" },
    { "stat",     cmd_stat,     "Show file info" },
    { "mv",       cmd_mv,       "Move/rename file" },
    { "cp",       cmd_cp,       "Copy file (zero-copy)" },
    { "tree",     cmd_tree,     "Recursive directory listing" },
    { "find",     cmd_find,     "Search files by name" },
    { "grep",     cmd_grep,     "Search file contents" },
    { "ln",       cmd_ln,       "Create hard/symlink (ln [-s])" },
    { "readlink", cmd_readlink, "Read symlink target" },
    { "chmod",    cmd_chmod,    "Set file permissions" },
    { "chown",    cmd_chown,    "Set file owner" },

    /* View commands */
    { "views",    cmd_views,    "List all geological views" },
    { "view",     cmd_view,     "Switch to view" },
    { "snapshot", cmd_snapshot, "Create new view (snapshot)" },
    { "diff",     cmd_diff,     "Diff between views or branches" },

    /* Branch commands */
    { "branch",   cmd_branch,   "List/create/switch branches" },
    { "merge",    cmd_merge,    "Merge branch into current" },

    /* Import/Export */
    { "export",   cmd_export,   "Export file to ATA disk" },
    { "import",   cmd_import,   "Import file from ATA disk" },

    /* Volume Persistence */
    { "save",     cmd_save,     "Save volume to ATA disk" },
    { "load",     cmd_load,     "Load volume from ATA disk" },

    /* Access Control */
    { "su",       cmd_su,       "Switch user (su [uid])" },
    { "whoami",   cmd_whoami,   "Show current identity" },
    { "quota",    cmd_quota,    "Show/set quotas" },

    /* Process commands */
    { "ps",       cmd_ps,       "List processes" },

    /* System commands */
    { "help",     cmd_help,     "Show this help" },
    { "clear",    cmd_clear,    "Clear screen" },
    { "mem",      cmd_mem,      "Show memory statistics" },
    { "disk",     cmd_disk,     "Show disk information" },
    { "gov",      cmd_gov,      "Show Governor statistics" },
    { "uptime",   cmd_uptime,   "Show system uptime" },
    { "echo",     cmd_echo,     "Echo text" },
    { "exit",     cmd_exit,     "Exit shell" },

    /* Hardware */
    { "lspci",    cmd_lspci,    "List PCI devices" },
    { "gpu",      cmd_gpu,      "Show GPU info and stats" },
    { "usb",      cmd_usb,      "Show USB device info" },

    /* Network */
    { "net",      cmd_net,      "Show network info" },
    { "ping",     cmd_ping,     "Ping gateway (or IP)" },

    { NULL, NULL, NULL }  /* Sentinel */
};

/*============================================================================
 * Shell API Implementation
 *============================================================================*/

void shell_init(kgeofs_volume_t *volume)
{
    shell_volume = volume;
    strcpy(current_path, "/");
    shell_running = 0;
    shell_start_tick = timer_get_ticks();

    kprintf("[SHELL] Initialized\n");
}

void shell_help(void)
{
    kprintf("\nPhantomOS Shell Commands\n");
    kprintf("\"To Create, Not To Destroy\"\n");
    kprintf("========================\n\n");

    kprintf("Filesystem:\n");
    for (const shell_cmd_t *cmd = commands; cmd->name; cmd++) {
        if (strcmp(cmd->name, "ls") == 0 || strcmp(cmd->name, "cat") == 0 ||
            strcmp(cmd->name, "write") == 0 || strcmp(cmd->name, "append") == 0 ||
            strcmp(cmd->name, "mkdir") == 0 || strcmp(cmd->name, "hide") == 0 ||
            strcmp(cmd->name, "pwd") == 0 || strcmp(cmd->name, "cd") == 0 ||
            strcmp(cmd->name, "stat") == 0 || strcmp(cmd->name, "mv") == 0 ||
            strcmp(cmd->name, "cp") == 0 || strcmp(cmd->name, "tree") == 0 ||
            strcmp(cmd->name, "find") == 0 || strcmp(cmd->name, "grep") == 0 ||
            strcmp(cmd->name, "ln") == 0 || strcmp(cmd->name, "readlink") == 0 ||
            strcmp(cmd->name, "chmod") == 0 || strcmp(cmd->name, "chown") == 0) {
            kprintf("  %-10s %s\n", cmd->name, cmd->description);
        }
    }

    kprintf("\nGeological Views:\n");
    for (const shell_cmd_t *cmd = commands; cmd->name; cmd++) {
        if (strcmp(cmd->name, "views") == 0 || strcmp(cmd->name, "view") == 0 ||
            strcmp(cmd->name, "snapshot") == 0 || strcmp(cmd->name, "diff") == 0) {
            kprintf("  %-10s %s\n", cmd->name, cmd->description);
        }
    }

    kprintf("\nBranches:\n");
    for (const shell_cmd_t *cmd = commands; cmd->name; cmd++) {
        if (strcmp(cmd->name, "branch") == 0 || strcmp(cmd->name, "merge") == 0) {
            kprintf("  %-10s %s\n", cmd->name, cmd->description);
        }
    }

    kprintf("\nImport/Export:\n");
    for (const shell_cmd_t *cmd = commands; cmd->name; cmd++) {
        if (strcmp(cmd->name, "export") == 0 || strcmp(cmd->name, "import") == 0) {
            kprintf("  %-10s %s\n", cmd->name, cmd->description);
        }
    }

    kprintf("\nAccess Control:\n");
    for (const shell_cmd_t *cmd = commands; cmd->name; cmd++) {
        if (strcmp(cmd->name, "su") == 0 || strcmp(cmd->name, "whoami") == 0 ||
            strcmp(cmd->name, "quota") == 0) {
            kprintf("  %-10s %s\n", cmd->name, cmd->description);
        }
    }

    kprintf("\nProcess:\n");
    kprintf("  %-10s %s\n", "ps", "List processes");

    kprintf("\nHardware:\n");
    for (const shell_cmd_t *cmd = commands; cmd->name; cmd++) {
        if (strcmp(cmd->name, "lspci") == 0 || strcmp(cmd->name, "gpu") == 0 ||
            strcmp(cmd->name, "usb") == 0) {
            kprintf("  %-10s %s\n", cmd->name, cmd->description);
        }
    }

    kprintf("\nNetwork:\n");
    for (const shell_cmd_t *cmd = commands; cmd->name; cmd++) {
        if (strcmp(cmd->name, "net") == 0 || strcmp(cmd->name, "ping") == 0) {
            kprintf("  %-10s %s\n", cmd->name, cmd->description);
        }
    }

    kprintf("\nSystem:\n");
    for (const shell_cmd_t *cmd = commands; cmd->name; cmd++) {
        if (strcmp(cmd->name, "help") == 0 || strcmp(cmd->name, "clear") == 0 ||
            strcmp(cmd->name, "mem") == 0 || strcmp(cmd->name, "disk") == 0 ||
            strcmp(cmd->name, "gov") == 0 || strcmp(cmd->name, "uptime") == 0 ||
            strcmp(cmd->name, "echo") == 0 || strcmp(cmd->name, "exit") == 0) {
            kprintf("  %-10s %s\n", cmd->name, cmd->description);
        }
    }

    kprintf("\n");
}

shell_result_t shell_execute(const char *cmd)
{
    /* Copy command for parsing (we modify it) */
    char cmd_buf[SHELL_CMD_MAX];
    strncpy(cmd_buf, cmd, sizeof(cmd_buf) - 1);
    cmd_buf[sizeof(cmd_buf) - 1] = '\0';

    /* Parse into arguments */
    char *argv[SHELL_ARGS_MAX];
    int argc = parse_args(cmd_buf, argv, SHELL_ARGS_MAX);

    if (argc == 0) {
        return SHELL_OK;  /* Empty command */
    }

    /* Look up command */
    for (const shell_cmd_t *c = commands; c->name; c++) {
        if (strcmp(argv[0], c->name) == 0) {
            return c->handler(argc, argv);
        }
    }

    kprintf("%s: command not found\n", argv[0]);
    return SHELL_ERR_NOTFOUND;
}

void shell_run(void)
{
    char cmd_buf[SHELL_CMD_MAX];

    shell_running = 1;

    kprintf("\n");
    kprintf("====================================\n");
    kprintf("  PhantomOS Shell\n");
    kprintf("  \"To Create, Not To Destroy\"\n");
    kprintf("====================================\n");
    kprintf("Type 'help' for available commands.\n\n");

    while (shell_running) {
        /* Print prompt */
        kprintf("%s", SHELL_PROMPT);

        /* Read command */
        int len = keyboard_readline(cmd_buf, sizeof(cmd_buf));
        if (len < 0) {
            kprintf("\nRead error\n");
            continue;
        }

        /* Execute command */
        shell_result_t result = shell_execute(cmd_buf);

        if (result == SHELL_EXIT) {
            shell_running = 0;
        }
    }

    kprintf("Shell terminated.\n");
}
