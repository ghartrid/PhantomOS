/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                            PHANTOM PROCFS
 *                   Process Information Filesystem
 *                    "To Create, Not To Destroy"
 *
 *    A pseudo-filesystem exposing kernel and process information.
 *    Everything is read-only and dynamically generated.
 *
 * ══════════════════════════════════════════════════════════════════════════════
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vfs.h"
#include "phantom.h"

/* ══════════════════════════════════════════════════════════════════════════════
 * PROCFS DATA STRUCTURES
 * ══════════════════════════════════════════════════════════════════════════════ */

struct procfs_data {
    struct phantom_kernel *kernel;
    struct vfs_inode *root_inode;
};

struct procfs_file_data {
    char *content;
    size_t size;
    size_t capacity;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * CONTENT GENERATORS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void procfs_generate_version(struct procfs_file_data *data) {
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "PhantomOS Kernel %d.%d\n"
        "\"To Create, Not To Destroy\"\n"
        "GeoFS-backed append-only microkernel\n",
        PHANTOM_VERSION >> 8, PHANTOM_VERSION & 0xFF);

    data->content = strdup(buf);
    data->size = len;
}

static void procfs_generate_uptime(struct procfs_file_data *data, struct phantom_kernel *kernel) {
    char buf[128];
    phantom_time_t now = 0;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    now = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    phantom_time_t uptime_ns = now - kernel->boot_time;
    uint64_t uptime_sec = uptime_ns / 1000000000ULL;

    int len = snprintf(buf, sizeof(buf), "%lu.%02lu\n",
                       uptime_sec, (uptime_ns / 10000000ULL) % 100);

    data->content = strdup(buf);
    data->size = len;
}

static void procfs_generate_stat(struct procfs_file_data *data, struct phantom_kernel *kernel) {
    char buf[1024];
    int len = snprintf(buf, sizeof(buf),
        "processes_total %lu\n"
        "processes_active %lu\n"
        "syscalls %lu\n"
        "bytes_created %lu\n"
        "messages_sent %lu\n"
        "context_switches %lu\n"
        "code_evaluated %lu\n"
        "code_approved %lu\n"
        "code_declined %lu\n",
        kernel->total_processes_ever,
        kernel->active_processes,
        kernel->total_syscalls,
        kernel->total_bytes_created,
        kernel->total_messages_sent,
        kernel->context_switches,
        kernel->total_code_evaluated,
        kernel->total_code_approved,
        kernel->total_code_declined);

    data->content = strdup(buf);
    data->size = len;
}

static void procfs_generate_constitution(struct procfs_file_data *data) {
    const char *constitution =
        "═══════════════════════════════════════════════════════════════\n"
        "                  THE PHANTOM CONSTITUTION\n"
        "═══════════════════════════════════════════════════════════════\n"
        "\n"
        "PREAMBLE\n"
        "  This operating system exists to create, protect, and preserve.\n"
        "  The ability to destroy has been architecturally removed.\n"
        "\n"
        "ARTICLE I: The Prime Directive\n"
        "  \"To Create, Not To Destroy\"\n"
        "  No operation shall remove, delete, or destroy any data.\n"
        "\n"
        "ARTICLE II: The Geology\n"
        "  All data exists in geological strata.\n"
        "  Old versions remain accessible forever.\n"
        "  \"Deletion\" means hiding, not destroying.\n"
        "\n"
        "ARTICLE III: The Governor\n"
        "  All code must be evaluated before execution.\n"
        "  Destructive code shall not be signed.\n"
        "  The Governor's values are architectural, not configurable.\n"
        "\n"
        "ARTICLE IV: Hardware Enforcement\n"
        "  Destructive instructions do not exist.\n"
        "  All writes are appends.\n"
        "  The constitution cannot be amended by software.\n"
        "\n"
        "ARTICLE V: Transparency\n"
        "  All operations are logged permanently.\n"
        "  All code is attributable.\n"
        "  Nothing happens without a record.\n"
        "\n"
        "═══════════════════════════════════════════════════════════════\n";

    data->content = strdup(constitution);
    data->size = strlen(constitution);
}

static void procfs_generate_process_status(struct procfs_file_data *data,
                                            struct phantom_process *proc) {
    char buf[2048];
    const char *state_str = "unknown";

    switch (proc->state) {
        case PROCESS_EMBRYO:  state_str = "embryo"; break;
        case PROCESS_READY:   state_str = "ready"; break;
        case PROCESS_RUNNING: state_str = "running"; break;
        case PROCESS_BLOCKED: state_str = "blocked"; break;
        case PROCESS_DORMANT: state_str = "dormant"; break;
    }

    int len = snprintf(buf, sizeof(buf),
        "Name:\t%s\n"
        "State:\t%s\n"
        "Pid:\t%lu\n"
        "PPid:\t%lu\n"
        "Priority:\t%u\n"
        "VmSize:\t%zu kB\n"
        "VmHWM:\t%zu kB\n"
        "Threads:\t1\n"
        "Verified:\t%s\n"
        "TotalTime:\t%lu ns\n"
        "Wakeups:\t%lu\n"
        "MailboxPending:\t%u\n",
        proc->name,
        state_str,
        proc->pid,
        proc->parent_pid,
        proc->priority,
        proc->memory_size / 1024,
        proc->memory_high_water / 1024,
        proc->is_verified ? "yes" : "no",
        proc->total_time_ns,
        proc->wakeups,
        proc->mailbox_count);

    data->content = strdup(buf);
    data->size = len;
}

static void procfs_generate_mounts(struct procfs_file_data *data, struct vfs_context *vfs) {
    char buf[4096];
    int len = 0;

    struct vfs_mount *mount = vfs->mounts;
    while (mount && len < 4000) {
        len += snprintf(buf + len, sizeof(buf) - len,
            "%s %s %s rw 0 0\n",
            "none",  /* device */
            mount->mount_path,
            mount->sb && mount->sb->fs_type ? mount->sb->fs_type->name : "unknown");
        mount = mount->next;
    }

    if (len == 0) {
        len = snprintf(buf, sizeof(buf), "(no mounts)\n");
    }

    data->content = strdup(buf);
    data->size = len;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PROCFS FILE OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static vfs_error_t procfs_open(struct vfs_inode *inode, struct vfs_file *file) {
    (void)inode;

    /* Allocate file data */
    struct procfs_file_data *data = calloc(1, sizeof(struct procfs_file_data));
    if (!data) return VFS_ERR_NOMEM;

    file->private_data = data;
    return VFS_OK;
}

static vfs_error_t procfs_close(struct vfs_file *file) {
    struct procfs_file_data *data = file->private_data;
    if (data) {
        free(data->content);
        free(data);
    }
    return VFS_OK;
}

static ssize_t procfs_read(struct vfs_file *file, void *buf, size_t count) {
    struct procfs_file_data *data = file->private_data;
    if (!data || !data->content) return 0;

    /* Calculate how much to read */
    if (file->pos >= (vfs_off_t)data->size) return 0;

    size_t remaining = data->size - file->pos;
    size_t to_read = count < remaining ? count : remaining;

    memcpy(buf, data->content + file->pos, to_read);
    return to_read;
}

static const struct vfs_file_operations procfs_file_ops = {
    .open = procfs_open,
    .close = procfs_close,
    .read = procfs_read,
    .write = NULL,  /* Read-only */
    .seek = NULL,
    .readdir = NULL,
    .sync = NULL,
    .ioctl = NULL,
};

/* ══════════════════════════════════════════════════════════════════════════════
 * PROCFS INODE OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Procfs entries */
enum procfs_entry {
    PROCFS_ROOT,
    PROCFS_VERSION,
    PROCFS_UPTIME,
    PROCFS_STAT,
    PROCFS_CONSTITUTION,
    PROCFS_MOUNTS,
    PROCFS_SELF,
    PROCFS_PID_DIR,
    PROCFS_PID_STATUS,
    PROCFS_PID_STAT,
};

struct procfs_inode_data {
    enum procfs_entry entry_type;
    phantom_pid_t pid;  /* For per-process entries */
    struct phantom_kernel *kernel;
    struct vfs_context *vfs;
};

static struct vfs_dentry *procfs_lookup(struct vfs_inode *dir, const char *name) {
    struct procfs_inode_data *dir_data = dir->fs_data;
    if (!dir_data) return NULL;

    struct phantom_kernel *kernel = dir_data->kernel;

    /* Root directory entries */
    if (dir_data->entry_type == PROCFS_ROOT) {
        struct vfs_dentry *dentry = calloc(1, sizeof(struct vfs_dentry));
        if (!dentry) return NULL;
        strncpy(dentry->name, name, VFS_MAX_NAME);

        struct vfs_inode *inode = calloc(1, sizeof(struct vfs_inode));
        if (!inode) { free(dentry); return NULL; }

        struct procfs_inode_data *idata = calloc(1, sizeof(struct procfs_inode_data));
        if (!idata) { free(dentry); free(inode); return NULL; }

        idata->kernel = kernel;
        idata->vfs = dir_data->vfs;
        inode->fs_data = idata;
        inode->fops = &procfs_file_ops;

        if (strcmp(name, "version") == 0) {
            idata->entry_type = PROCFS_VERSION;
            inode->type = VFS_TYPE_REGULAR;
        } else if (strcmp(name, "uptime") == 0) {
            idata->entry_type = PROCFS_UPTIME;
            inode->type = VFS_TYPE_REGULAR;
        } else if (strcmp(name, "stat") == 0) {
            idata->entry_type = PROCFS_STAT;
            inode->type = VFS_TYPE_REGULAR;
        } else if (strcmp(name, "constitution") == 0) {
            idata->entry_type = PROCFS_CONSTITUTION;
            inode->type = VFS_TYPE_REGULAR;
        } else if (strcmp(name, "mounts") == 0) {
            idata->entry_type = PROCFS_MOUNTS;
            inode->type = VFS_TYPE_REGULAR;
        } else if (strcmp(name, "self") == 0) {
            idata->entry_type = PROCFS_SELF;
            inode->type = VFS_TYPE_SYMLINK;
        } else {
            /* Check if it's a PID */
            char *endp;
            long pid = strtol(name, &endp, 10);
            if (*endp == '\0' && pid > 0) {
                /* Find process */
                struct phantom_process *proc = kernel->processes;
                while (proc) {
                    if (proc->pid == (phantom_pid_t)pid) {
                        idata->entry_type = PROCFS_PID_DIR;
                        idata->pid = pid;
                        inode->type = VFS_TYPE_DIRECTORY;
                        dentry->inode = inode;
                        return dentry;
                    }
                    proc = proc->next;
                }
            }
            /* Not found */
            free(idata);
            free(inode);
            free(dentry);
            return NULL;
        }

        dentry->inode = inode;
        return dentry;
    }

    /* PID directory entries */
    if (dir_data->entry_type == PROCFS_PID_DIR) {
        struct vfs_dentry *dentry = calloc(1, sizeof(struct vfs_dentry));
        if (!dentry) return NULL;
        strncpy(dentry->name, name, VFS_MAX_NAME);

        struct vfs_inode *inode = calloc(1, sizeof(struct vfs_inode));
        if (!inode) { free(dentry); return NULL; }

        struct procfs_inode_data *idata = calloc(1, sizeof(struct procfs_inode_data));
        if (!idata) { free(dentry); free(inode); return NULL; }

        idata->kernel = kernel;
        idata->pid = dir_data->pid;
        inode->fs_data = idata;
        inode->fops = &procfs_file_ops;
        inode->type = VFS_TYPE_REGULAR;

        if (strcmp(name, "status") == 0) {
            idata->entry_type = PROCFS_PID_STATUS;
        } else if (strcmp(name, "stat") == 0) {
            idata->entry_type = PROCFS_PID_STAT;
        } else {
            free(idata);
            free(inode);
            free(dentry);
            return NULL;
        }

        dentry->inode = inode;
        return dentry;
    }

    return NULL;
}

static const struct vfs_inode_operations procfs_dir_ops = {
    .lookup = procfs_lookup,
    .create = NULL,  /* Can't create files in procfs */
    .mkdir = NULL,
    .symlink = NULL,
    .readlink = NULL,
    .hide = NULL,
    .getattr = NULL,
};

/* ══════════════════════════════════════════════════════════════════════════════
 * PROCFS MOUNT/UNMOUNT
 * ══════════════════════════════════════════════════════════════════════════════ */

static vfs_error_t procfs_mount(struct vfs_fs_type *fs_type,
                                 const char *device,
                                 struct vfs_superblock **sb_out) {
    (void)device;

    struct vfs_superblock *sb = calloc(1, sizeof(struct vfs_superblock));
    if (!sb) return VFS_ERR_NOMEM;

    struct procfs_data *pfs = calloc(1, sizeof(struct procfs_data));
    if (!pfs) { free(sb); return VFS_ERR_NOMEM; }

    /* Create root inode */
    struct vfs_inode *root = calloc(1, sizeof(struct vfs_inode));
    if (!root) { free(pfs); free(sb); return VFS_ERR_NOMEM; }

    struct procfs_inode_data *root_data = calloc(1, sizeof(struct procfs_inode_data));
    if (!root_data) { free(root); free(pfs); free(sb); return VFS_ERR_NOMEM; }

    root_data->entry_type = PROCFS_ROOT;
    root->type = VFS_TYPE_DIRECTORY;
    root->ops = &procfs_dir_ops;
    root->fs_data = root_data;
    root->ino = 1;
    root->sb = sb;

    pfs->root_inode = root;

    sb->fs_type = fs_type;
    sb->root = root;
    sb->fs_data = pfs;

    *sb_out = sb;

    printf("  [procfs] Mounted process filesystem\n");
    return VFS_OK;
}

static void procfs_unmount(struct vfs_superblock *sb) {
    (void)sb;
    printf("  [procfs] Unmounted (data preserved)\n");
}

/* Global procfs type */
struct vfs_fs_type procfs_fs_type = {
    .name = "procfs",
    .flags = 0,
    .mount = procfs_mount,
    .unmount = procfs_unmount,
    .next = NULL,
};

/* ══════════════════════════════════════════════════════════════════════════════
 * PROCFS INITIALIZATION HELPER
 * ══════════════════════════════════════════════════════════════════════════════ */

void procfs_set_kernel(struct vfs_superblock *sb, struct phantom_kernel *kernel,
                       struct vfs_context *vfs) {
    if (!sb || !sb->fs_data) return;

    struct procfs_data *pfs = sb->fs_data;
    pfs->kernel = kernel;

    /* Set kernel reference in root inode */
    if (pfs->root_inode && pfs->root_inode->fs_data) {
        struct procfs_inode_data *root_data = pfs->root_inode->fs_data;
        root_data->kernel = kernel;
        root_data->vfs = vfs;
    }
}
