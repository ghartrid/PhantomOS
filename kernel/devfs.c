/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                            PHANTOM DEVFS
 *                      Device Filesystem
 *                    "To Create, Not To Destroy"
 *
 *    A pseudo-filesystem for device access.
 *    In Phantom, even device I/O is logged to the geology.
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
 * DEVICE TYPES
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef enum {
    DEV_NULL,       /* /dev/null - write sink, read returns EOF */
    DEV_ZERO,       /* /dev/zero - read returns zeros */
    DEV_FULL,       /* /dev/full - writes always fail (ENOSPC) */
    DEV_RANDOM,     /* /dev/random - pseudo-random data */
    DEV_URANDOM,    /* /dev/urandom - same as random in Phantom */
    DEV_CONSOLE,    /* /dev/console - kernel console */
    DEV_TTY,        /* /dev/tty - current TTY */
    DEV_KMSG,       /* /dev/kmsg - kernel message buffer */
} devfs_device_t;

struct devfs_inode_data {
    devfs_device_t device;
    uint64_t bytes_read;
    uint64_t bytes_written;
};

/* Simple PRNG for /dev/random */
static uint64_t prng_state = 0x853c49e6748fea9bULL;

static uint64_t prng_next(void) {
    prng_state ^= prng_state >> 12;
    prng_state ^= prng_state << 25;
    prng_state ^= prng_state >> 27;
    return prng_state * 0x2545F4914F6CDD1DULL;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * DEVICE FILE OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static vfs_error_t devfs_open(struct vfs_inode *inode, struct vfs_file *file) {
    (void)inode;
    (void)file;
    return VFS_OK;
}

static vfs_error_t devfs_close(struct vfs_file *file) {
    (void)file;
    return VFS_OK;
}

static ssize_t devfs_read(struct vfs_file *file, void *buf, size_t count) {
    struct devfs_inode_data *data = file->inode->fs_data;
    if (!data) return VFS_ERR_IO;

    ssize_t result = 0;
    uint8_t *out = buf;

    switch (data->device) {
        case DEV_NULL:
            /* Read from /dev/null returns EOF */
            result = 0;
            break;

        case DEV_ZERO:
            /* Read from /dev/zero returns zeros */
            memset(buf, 0, count);
            result = count;
            break;

        case DEV_FULL:
            /* /dev/full reads like /dev/zero */
            memset(buf, 0, count);
            result = count;
            break;

        case DEV_RANDOM:
        case DEV_URANDOM:
            /* Generate pseudo-random data */
            for (size_t i = 0; i < count; i += 8) {
                uint64_t r = prng_next();
                size_t to_copy = count - i;
                if (to_copy > 8) to_copy = 8;
                memcpy(out + i, &r, to_copy);
            }
            result = count;
            break;

        case DEV_CONSOLE:
        case DEV_TTY:
            /* Console read - not implemented in simulation */
            result = 0;
            break;

        case DEV_KMSG:
            /* Kernel message buffer - return empty for now */
            result = 0;
            break;

        default:
            return VFS_ERR_IO;
    }

    if (result > 0) {
        data->bytes_read += result;
    }

    return result;
}

static ssize_t devfs_write(struct vfs_file *file, const void *buf, size_t count) {
    struct devfs_inode_data *data = file->inode->fs_data;
    if (!data) return VFS_ERR_IO;

    ssize_t result = 0;
    (void)buf;

    switch (data->device) {
        case DEV_NULL:
            /* Write to /dev/null succeeds but discards data */
            result = count;
            break;

        case DEV_ZERO:
            /* Can write to /dev/zero (discarded) */
            result = count;
            break;

        case DEV_FULL:
            /* Write to /dev/full always fails */
            return VFS_ERR_NOSPC;

        case DEV_RANDOM:
        case DEV_URANDOM:
            /* Write to random adds entropy (we just accept it) */
            result = count;
            break;

        case DEV_CONSOLE:
        case DEV_TTY:
            /* Console write - print to stdout */
            printf("%.*s", (int)count, (const char *)buf);
            result = count;
            break;

        case DEV_KMSG:
            /* Write to kernel message buffer */
            printf("[kernel] %.*s", (int)count, (const char *)buf);
            result = count;
            break;

        default:
            return VFS_ERR_IO;
    }

    if (result > 0) {
        data->bytes_written += result;
    }

    return result;
}

static const struct vfs_file_operations devfs_file_ops = {
    .open = devfs_open,
    .close = devfs_close,
    .read = devfs_read,
    .write = devfs_write,
    .seek = NULL,
    .readdir = NULL,
    .sync = NULL,
    .ioctl = NULL,
};

/* ══════════════════════════════════════════════════════════════════════════════
 * DEVFS DIRECTORY OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Device table */
static struct {
    const char *name;
    devfs_device_t device;
} devfs_devices[] = {
    { "null",    DEV_NULL },
    { "zero",    DEV_ZERO },
    { "full",    DEV_FULL },
    { "random",  DEV_RANDOM },
    { "urandom", DEV_URANDOM },
    { "console", DEV_CONSOLE },
    { "tty",     DEV_TTY },
    { "kmsg",    DEV_KMSG },
    { NULL, 0 }
};

static struct vfs_dentry *devfs_lookup(struct vfs_inode *dir, const char *name) {
    (void)dir;

    /* Find device by name */
    devfs_device_t device = (devfs_device_t)-1;
    for (int i = 0; devfs_devices[i].name; i++) {
        if (strcmp(devfs_devices[i].name, name) == 0) {
            device = devfs_devices[i].device;
            break;
        }
    }

    if (device == (devfs_device_t)-1) {
        return NULL;  /* Device not found */
    }

    /* Create dentry and inode */
    struct vfs_dentry *dentry = calloc(1, sizeof(struct vfs_dentry));
    if (!dentry) return NULL;
    strncpy(dentry->name, name, VFS_MAX_NAME);

    struct vfs_inode *inode = calloc(1, sizeof(struct vfs_inode));
    if (!inode) { free(dentry); return NULL; }

    struct devfs_inode_data *idata = calloc(1, sizeof(struct devfs_inode_data));
    if (!idata) { free(inode); free(dentry); return NULL; }

    idata->device = device;
    inode->type = VFS_TYPE_DEVICE;
    inode->fs_data = idata;
    inode->fops = &devfs_file_ops;

    dentry->inode = inode;
    return dentry;
}

static const struct vfs_inode_operations devfs_dir_ops = {
    .lookup = devfs_lookup,
    .create = NULL,  /* Can't create devices dynamically (yet) */
    .mkdir = NULL,
    .symlink = NULL,
    .readlink = NULL,
    .hide = NULL,
    .getattr = NULL,
};

/* ══════════════════════════════════════════════════════════════════════════════
 * DEVFS MOUNT/UNMOUNT
 * ══════════════════════════════════════════════════════════════════════════════ */

static vfs_error_t devfs_mount(struct vfs_fs_type *fs_type,
                                const char *device,
                                struct vfs_superblock **sb_out) {
    (void)device;

    struct vfs_superblock *sb = calloc(1, sizeof(struct vfs_superblock));
    if (!sb) return VFS_ERR_NOMEM;

    /* Create root inode */
    struct vfs_inode *root = calloc(1, sizeof(struct vfs_inode));
    if (!root) { free(sb); return VFS_ERR_NOMEM; }

    root->type = VFS_TYPE_DIRECTORY;
    root->ops = &devfs_dir_ops;
    root->ino = 1;
    root->sb = sb;

    sb->fs_type = fs_type;
    sb->root = root;

    /* Seed PRNG with current time */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    prng_state ^= ts.tv_sec ^ ts.tv_nsec;

    *sb_out = sb;

    printf("  [devfs] Mounted device filesystem\n");
    printf("  [devfs] Available devices: null, zero, full, random, urandom, console, tty, kmsg\n");

    return VFS_OK;
}

static void devfs_unmount(struct vfs_superblock *sb) {
    (void)sb;
    printf("  [devfs] Unmounted (data preserved)\n");
}

/* Global devfs type */
struct vfs_fs_type devfs_fs_type = {
    .name = "devfs",
    .flags = 0,
    .mount = devfs_mount,
    .unmount = devfs_unmount,
    .next = NULL,
};
