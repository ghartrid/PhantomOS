/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                          PHANTOM VIRTUAL FILE SYSTEM
 *                          "To Create, Not To Destroy"
 *
 *    Implementation of the VFS layer for PhantomOS.
 *
 * ══════════════════════════════════════════════════════════════════════════════
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vfs.h"
#include "phantom.h"
#include "../geofs.h"

/* ══════════════════════════════════════════════════════════════════════════════
 * SECURITY: PATH CANONICALIZATION
 * Prevents path traversal attacks by normalizing paths and removing .. and .
 * ══════════════════════════════════════════════════════════════════════════════ */

static int vfs_canonicalize_path(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size < 2) return -1;

    /* Stack to track path components */
    char components[64][VFS_MAX_NAME + 1] = {{0}};
    int depth = 0;

    const char *p = input;

    /* Handle absolute vs relative paths */
    int is_absolute = (*p == '/');

    while (*p) {
        /* Skip slashes */
        while (*p == '/') p++;
        if (!*p) break;

        /* Find end of component */
        const char *start = p;
        while (*p && *p != '/') p++;
        size_t len = p - start;

        if (len == 0) continue;

        /* Handle . and .. */
        if (len == 1 && start[0] == '.') {
            /* Current directory - skip */
            continue;
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            /* Parent directory - pop if possible */
            if (depth > 0) {
                depth--;
            } else if (!is_absolute) {
                /* Can't go above root for absolute paths */
                /* For relative paths, keep the .. */
                if (depth < 64) {
                    strncpy(components[depth], "..", VFS_MAX_NAME);
                    components[depth][VFS_MAX_NAME] = '\0';
                    depth++;
                }
            }
            /* For absolute paths at root, just ignore the .. */
        } else {
            /* Normal component - push */
            if (depth >= 64) return -1;  /* Path too deep */
            if (len > VFS_MAX_NAME) len = VFS_MAX_NAME;
            memcpy(components[depth], start, len);
            components[depth][len] = '\0';
            depth++;
        }
    }

    /* Build output path */
    char *out = output;
    char *end = output + output_size - 1;

    if (is_absolute) {
        *out++ = '/';
    }

    for (int i = 0; i < depth && out < end; i++) {
        if (i > 0 && out < end) *out++ = '/';
        size_t clen = strlen(components[i]);
        if (out + clen >= end) return -1;  /* Output buffer too small */
        memcpy(out, components[i], clen);
        out += clen;
    }

    /* Handle empty path result */
    if (out == output) {
        if (is_absolute) {
            *out++ = '/';
        } else {
            *out++ = '.';
        }
    }

    *out = '\0';
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static phantom_time_t vfs_time_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

const char *vfs_strerror(vfs_error_t err) {
    switch (err) {
        case VFS_OK:            return "Success";
        case VFS_ERR_NOENT:     return "No such file or directory";
        case VFS_ERR_IO:        return "I/O error";
        case VFS_ERR_NOMEM:     return "Out of memory";
        case VFS_ERR_PERM:      return "Permission denied";
        case VFS_ERR_EXIST:     return "File exists";
        case VFS_ERR_NOTDIR:    return "Not a directory";
        case VFS_ERR_ISDIR:     return "Is a directory";
        case VFS_ERR_INVAL:     return "Invalid argument";
        case VFS_ERR_NFILE:     return "Too many open files";
        case VFS_ERR_BADF:      return "Bad file descriptor";
        case VFS_ERR_NOSPC:     return "No space left on device";
        case VFS_ERR_NOSYS:     return "Function not implemented";
        case VFS_ERR_NOTEMPTY:  return "Directory not empty";
        case VFS_ERR_XDEV:      return "Cross-device link";
        default:                return "Unknown error";
    }
}

/* Parse path into components */
static int path_split(const char *path, char components[][VFS_MAX_NAME + 1], int max_components) {
    if (!path || !components) return 0;

    int count = 0;
    const char *p = path;

    /* Skip leading slashes */
    while (*p == '/') p++;

    while (*p && count < max_components) {
        const char *start = p;
        while (*p && *p != '/') p++;

        size_t len = p - start;
        if (len > 0 && len <= VFS_MAX_NAME) {
            memcpy(components[count], start, len);
            components[count][len] = '\0';
            count++;
        }

        while (*p == '/') p++;
    }

    return count;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * DENTRY OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static struct vfs_dentry *dentry_alloc(const char *name) {
    struct vfs_dentry *dentry = calloc(1, sizeof(struct vfs_dentry));
    if (!dentry) return NULL;

    if (name) {
        strncpy(dentry->name, name, VFS_MAX_NAME);
    }
    return dentry;
}

static struct vfs_dentry *dentry_lookup_child(struct vfs_dentry *parent, const char *name) {
    if (!parent || !name) return NULL;

    struct vfs_dentry *child = parent->children;
    while (child) {
        if (!child->is_hidden && strcmp(child->name, name) == 0) {
            return child;
        }
        child = child->sibling;
    }
    return NULL;
}

static void dentry_add_child(struct vfs_dentry *parent, struct vfs_dentry *child) {
    if (!parent || !child) return;

    child->parent = parent;
    child->sibling = parent->children;
    parent->children = child;
}

static void dentry_free_recursive(struct vfs_dentry *dentry) {
    if (!dentry) return;

    /* Free all children first */
    struct vfs_dentry *child = dentry->children;
    while (child) {
        struct vfs_dentry *next = child->sibling;
        dentry_free_recursive(child);
        child = next;
    }

    /* Free the inode if present */
    if (dentry->inode) {
        /* Free inode's fs_data (devfs/procfs specific data) */
        if (dentry->inode->fs_data) {
            free(dentry->inode->fs_data);
        }
        free(dentry->inode);
    }

    /* Free the dentry itself */
    free(dentry);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * INODE OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static vfs_ino_t next_ino = 1;

static struct vfs_inode *inode_alloc(struct vfs_superblock *sb, vfs_file_type_t type) {
    struct vfs_inode *inode = calloc(1, sizeof(struct vfs_inode));
    if (!inode) return NULL;

    inode->ino = next_ino++;
    inode->type = type;
    inode->created = vfs_time_now();
    inode->modified = inode->created;
    inode->accessed = inode->created;
    inode->ref_count = 1;
    inode->sb = sb;

    if (sb) {
        sb->total_inodes++;
    }

    return inode;
}

static void inode_ref(struct vfs_inode *inode) {
    if (inode) inode->ref_count++;
}

static void inode_unref(struct vfs_inode *inode) {
    if (inode && inode->ref_count > 0) {
        inode->ref_count--;
        /* Note: We never free inodes in Phantom - they're preserved */
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FILE DESCRIPTOR TABLE
 * ══════════════════════════════════════════════════════════════════════════════ */

static vfs_fd_t fd_alloc(struct vfs_context *ctx) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (ctx->open_files[i] == NULL) {
            return i;
        }
    }
    return -1;
}

static struct vfs_file *fd_get(struct vfs_context *ctx, vfs_fd_t fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES) return NULL;
    return ctx->open_files[fd];
}

/* ══════════════════════════════════════════════════════════════════════════════
 * VFS INITIALIZATION
 * ══════════════════════════════════════════════════════════════════════════════ */

vfs_error_t vfs_init(struct vfs_context *ctx) {
    if (!ctx) return VFS_ERR_INVAL;

    memset(ctx, 0, sizeof(struct vfs_context));

    /* Create root dentry */
    ctx->root = dentry_alloc("");
    if (!ctx->root) return VFS_ERR_NOMEM;

    /* Create root inode */
    struct vfs_inode *root_inode = inode_alloc(NULL, VFS_TYPE_DIRECTORY);
    if (!root_inode) return VFS_ERR_NOMEM;
    root_inode->mode = 0755;
    ctx->root->inode = root_inode;

    printf("  [vfs] Virtual File System initialized\n");
    printf("  [vfs] Root dentry created\n");

    return VFS_OK;
}

void vfs_shutdown(struct vfs_context *ctx) {
    if (!ctx) return;

    /* Close all open files (but preserve them in geology) */
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (ctx->open_files[i]) {
            vfs_close(ctx, i);
        }
    }

    /* Free the root dentry tree */
    if (ctx->root) {
        dentry_free_recursive(ctx->root);
        ctx->root = NULL;
    }

    /* Free all mount structures */
    struct vfs_mount *mount = ctx->mounts;
    while (mount) {
        struct vfs_mount *next = mount->next;
        /* Free mount root dentry tree (including children like /dev/console) */
        if (mount->root && mount->root != ctx->root) {
            /* Clear inode pointer before recursive free - it's owned by superblock */
            mount->root->inode = NULL;
            dentry_free_recursive(mount->root);
        }
        if (mount->sb) {
            /* Free the superblock's root inode if present */
            if (mount->sb->root) {
                /* Free inode's fs_data (procfs/devfs specific data) */
                if (mount->sb->root->fs_data) {
                    free(mount->sb->root->fs_data);
                }
                free(mount->sb->root);
            }
            /* Free superblock's fs_data (procfs_data/devfs_data) */
            if (mount->sb->fs_data) {
                free(mount->sb->fs_data);
            }
            free(mount->sb);
        }
        free(mount);
        mount = next;
    }
    ctx->mounts = NULL;

    printf("  [vfs] Shutdown complete\n");
    printf("  [vfs] Statistics:\n");
    printf("    Total opens:         %lu\n", ctx->total_opens);
    printf("    Total reads:         %lu\n", ctx->total_reads);
    printf("    Total writes:        %lu\n", ctx->total_writes);
    printf("    Total bytes read:    %lu\n", ctx->total_bytes_read);
    printf("    Total bytes written: %lu\n", ctx->total_bytes_written);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FILE SYSTEM REGISTRATION
 * ══════════════════════════════════════════════════════════════════════════════ */

vfs_error_t vfs_register_fs(struct vfs_context *ctx, struct vfs_fs_type *fs_type) {
    if (!ctx || !fs_type) return VFS_ERR_INVAL;

    /* Check if already registered */
    struct vfs_fs_type *fs = ctx->fs_types;
    while (fs) {
        if (strcmp(fs->name, fs_type->name) == 0) {
            return VFS_ERR_EXIST;
        }
        fs = fs->next;
    }

    /* Add to list */
    fs_type->next = ctx->fs_types;
    ctx->fs_types = fs_type;

    printf("  [vfs] Registered filesystem: %s\n", fs_type->name);
    return VFS_OK;
}

static struct vfs_fs_type *find_fs_type(struct vfs_context *ctx, const char *name) {
    struct vfs_fs_type *fs = ctx->fs_types;
    while (fs) {
        if (strcmp(fs->name, name) == 0) return fs;
        fs = fs->next;
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * MOUNT OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

vfs_error_t vfs_mount(struct vfs_context *ctx,
                      const char *fs_type_name,
                      const char *device,
                      const char *mount_path,
                      uint32_t flags) {
    if (!ctx || !fs_type_name || !mount_path) return VFS_ERR_INVAL;

    /* Find filesystem type */
    struct vfs_fs_type *fs_type = find_fs_type(ctx, fs_type_name);
    if (!fs_type) {
        printf("  [vfs] Unknown filesystem type: %s\n", fs_type_name);
        return VFS_ERR_NOENT;
    }

    /* Create mount structure */
    struct vfs_mount *mount = calloc(1, sizeof(struct vfs_mount));
    if (!mount) return VFS_ERR_NOMEM;

    strncpy(mount->mount_path, mount_path, VFS_MAX_PATH - 1);
    mount->flags = flags;
    mount->mounted_at = vfs_time_now();

    /* Mount the filesystem */
    vfs_error_t err = fs_type->mount(fs_type, device, &mount->sb);
    if (err != VFS_OK) {
        free(mount);
        return err;
    }

    /* Create mount point dentry */
    if (strcmp(mount_path, "/") == 0) {
        /* Root mount */
        mount->root = ctx->root;
        ctx->root->inode = mount->sb->root;
        ctx->root->mount = mount;
    } else {
        /* Find or create mount point */
        char components[32][VFS_MAX_NAME + 1];
        int count = path_split(mount_path, components, 32);

        struct vfs_dentry *parent = ctx->root;
        for (int i = 0; i < count; i++) {
            struct vfs_dentry *child = dentry_lookup_child(parent, components[i]);
            if (!child) {
                /* Create intermediate directory */
                child = dentry_alloc(components[i]);
                if (!child) {
                    free(mount);
                    return VFS_ERR_NOMEM;
                }
                child->inode = inode_alloc(NULL, VFS_TYPE_DIRECTORY);
                dentry_add_child(parent, child);
            }
            parent = child;
        }

        mount->mount_point = parent;
        mount->root = dentry_alloc("");
        mount->root->inode = mount->sb->root;
        mount->root->mount = mount;
        parent->mount = mount;
    }

    /* Add to mount list */
    mount->next = ctx->mounts;
    ctx->mounts = mount;

    printf("  [vfs] Mounted %s on %s (type: %s)\n",
           device ? device : "none", mount_path, fs_type_name);

    return VFS_OK;
}

vfs_error_t vfs_sync_unmount(struct vfs_context *ctx, const char *mount_path) {
    if (!ctx || !mount_path) return VFS_ERR_INVAL;

    /* Find mount */
    struct vfs_mount *mount = ctx->mounts;

    while (mount) {
        if (strcmp(mount->mount_path, mount_path) == 0) {
            break;
        }
        mount = mount->next;
    }

    if (!mount) return VFS_ERR_NOENT;

    /* Sync and unmount (but preserve in geology) */
    if (mount->sb && mount->sb->fs_type && mount->sb->fs_type->unmount) {
        mount->sb->fs_type->unmount(mount->sb);
    }

    /* Note: We don't actually remove the mount - it stays in the list
     * marked as unmounted. Nothing is ever truly removed in Phantom. */
    printf("  [vfs] Synced and unmounted %s (preserved in geology)\n", mount_path);

    return VFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PATH RESOLUTION
 * ══════════════════════════════════════════════════════════════════════════════ */

vfs_error_t vfs_resolve_path(struct vfs_context *ctx,
                              const char *path,
                              struct vfs_dentry **dentry_out) {
    if (!ctx || !path || !dentry_out) return VFS_ERR_INVAL;

    /* SECURITY: Canonicalize path to prevent traversal attacks */
    char canonical_path[VFS_MAX_PATH];
    if (vfs_canonicalize_path(path, canonical_path, sizeof(canonical_path)) != 0) {
        return VFS_ERR_INVAL;
    }

    /* Handle empty path or just "/" */
    if (canonical_path[0] == '\0' || (canonical_path[0] == '/' && canonical_path[1] == '\0')) {
        *dentry_out = ctx->root;
        return VFS_OK;
    }

    /* Split path */
    char components[64][VFS_MAX_NAME + 1];
    int count = path_split(canonical_path, components, 64);

    struct vfs_dentry *current = ctx->root;

    for (int i = 0; i < count; i++) {
        if (!current) return VFS_ERR_NOENT;

        /* Handle "." and ".." */
        if (strcmp(components[i], ".") == 0) {
            continue;
        }
        if (strcmp(components[i], "..") == 0) {
            if (current->parent) {
                current = current->parent;
            }
            continue;
        }

        /* Check if this is a mount point */
        if (current->mount && current->mount->root) {
            current = current->mount->root;
        }

        /* First check in-memory dentry tree (for updated inodes) */
        struct vfs_dentry *child = dentry_lookup_child(current, components[i]);

        if (!child) {
            /* Not in dentry cache - use filesystem lookup if available */
            if (current->inode && current->inode->ops && current->inode->ops->lookup) {
                child = current->inode->ops->lookup(current->inode, components[i]);
                if (child) {
                    /* Add to dentry tree for future lookups */
                    dentry_add_child(current, child);
                }
            }
        }

        if (!child) {
            return VFS_ERR_NOENT;
        }
        current = child;
    }

    /* If we ended on a mount point, transition to the mount's root */
    if (current->mount && current->mount->root) {
        current = current->mount->root;
    }

    *dentry_out = current;
    return VFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FILE OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

vfs_fd_t vfs_open(struct vfs_context *ctx, phantom_pid_t pid,
                  const char *path, uint32_t flags, vfs_mode_t mode) {
    if (!ctx || !path) return VFS_ERR_INVAL;

    /* Resolve path */
    struct vfs_dentry *dentry = NULL;
    vfs_error_t err = vfs_resolve_path(ctx, path, &dentry);

    if (err == VFS_ERR_NOENT && (flags & VFS_O_CREATE)) {
        /* Create new file */
        char dir_path[VFS_MAX_PATH];
        char filename[VFS_MAX_NAME + 1];

        /* Split into directory and filename */
        const char *last_slash = strrchr(path, '/');
        if (last_slash) {
            size_t dir_len = last_slash - path;
            if (dir_len == 0) dir_len = 1;  /* Root directory */
            memcpy(dir_path, path, dir_len);
            dir_path[dir_len] = '\0';
            strncpy(filename, last_slash + 1, VFS_MAX_NAME);
        } else {
            strcpy(dir_path, "/");
            strncpy(filename, path, VFS_MAX_NAME);
        }

        /* Resolve parent directory */
        struct vfs_dentry *parent = NULL;
        err = vfs_resolve_path(ctx, dir_path, &parent);
        if (err != VFS_OK) return err;

        if (!parent->inode || parent->inode->type != VFS_TYPE_DIRECTORY) {
            return VFS_ERR_NOTDIR;
        }

        /* Create new inode */
        struct vfs_inode *new_inode = NULL;
        if (parent->inode->ops && parent->inode->ops->create) {
            err = parent->inode->ops->create(parent->inode, filename, mode, &new_inode);
            if (err != VFS_OK) return err;
        } else {
            /* Fallback: create in dentry tree */
            new_inode = inode_alloc(parent->inode->sb, VFS_TYPE_REGULAR);
            if (!new_inode) return VFS_ERR_NOMEM;
            new_inode->mode = mode;
        }

        /* Create dentry */
        dentry = dentry_alloc(filename);
        if (!dentry) {
            free(new_inode);  /* Free inode if dentry allocation fails */
            return VFS_ERR_NOMEM;
        }
        dentry->inode = new_inode;
        dentry_add_child(parent, dentry);

        if (parent->inode->sb) {
            parent->inode->sb->total_files_created++;
        }

    } else if (err != VFS_OK) {
        return err;
    }

    if (!dentry || !dentry->inode) return VFS_ERR_NOENT;

    /* Check if opening directory without O_DIRECTORY */
    if (dentry->inode->type == VFS_TYPE_DIRECTORY && !(flags & VFS_O_DIRECTORY)) {
        return VFS_ERR_ISDIR;
    }

    /* Allocate file descriptor */
    vfs_fd_t fd = fd_alloc(ctx);
    if (fd < 0) return VFS_ERR_NFILE;

    /* Create file structure */
    struct vfs_file *file = calloc(1, sizeof(struct vfs_file));
    if (!file) return VFS_ERR_NOMEM;

    file->inode = dentry->inode;
    file->dentry = dentry;
    file->pos = (flags & VFS_O_APPEND) ? dentry->inode->size : 0;
    file->flags = flags;
    file->owner_pid = pid;
    file->opened_at = vfs_time_now();
    file->ref_count = 1;

    inode_ref(dentry->inode);

    /* Call filesystem open if available */
    if (file->inode->fops && file->inode->fops->open) {
        err = file->inode->fops->open(file->inode, file);
        if (err != VFS_OK) {
            free(file);
            return err;
        }
    }

    ctx->open_files[fd] = file;
    ctx->total_opens++;

    return fd;
}

vfs_error_t vfs_close(struct vfs_context *ctx, vfs_fd_t fd) {
    if (!ctx) return VFS_ERR_INVAL;

    struct vfs_file *file = fd_get(ctx, fd);
    if (!file) return VFS_ERR_BADF;

    /* Call filesystem close if available */
    if (file->inode->fops && file->inode->fops->close) {
        file->inode->fops->close(file);
    }

    inode_unref(file->inode);

    /* Free the file structure to prevent memory leaks */
    free(file);
    ctx->open_files[fd] = NULL;

    return VFS_OK;
}

ssize_t vfs_read(struct vfs_context *ctx, vfs_fd_t fd, void *buf, size_t count) {
    if (!ctx || !buf) return VFS_ERR_INVAL;

    struct vfs_file *file = fd_get(ctx, fd);
    if (!file) return VFS_ERR_BADF;

    if (!(file->flags & VFS_O_RDONLY) && !(file->flags & VFS_O_RDWR)) {
        return VFS_ERR_PERM;
    }

    ssize_t bytes_read = 0;

    if (file->inode->fops && file->inode->fops->read) {
        bytes_read = file->inode->fops->read(file, buf, count);
    } else {
        /* No read implementation */
        return VFS_ERR_NOSYS;
    }

    if (bytes_read > 0) {
        file->pos += bytes_read;
        file->inode->accessed = vfs_time_now();
        ctx->total_reads++;
        ctx->total_bytes_read += bytes_read;
    }

    return bytes_read;
}

ssize_t vfs_write(struct vfs_context *ctx, vfs_fd_t fd, const void *buf, size_t count) {
    if (!ctx || !buf) return VFS_ERR_INVAL;

    struct vfs_file *file = fd_get(ctx, fd);
    if (!file) return VFS_ERR_BADF;

    if (!(file->flags & VFS_O_WRONLY) && !(file->flags & VFS_O_RDWR)) {
        return VFS_ERR_PERM;
    }

    /* In Phantom, all writes are effectively appends */
    ssize_t bytes_written = 0;

    if (file->inode->fops && file->inode->fops->write) {
        bytes_written = file->inode->fops->write(file, buf, count);
    } else {
        return VFS_ERR_NOSYS;
    }

    if (bytes_written > 0) {
        file->pos += bytes_written;
        file->inode->modified = vfs_time_now();
        file->inode->size += bytes_written;  /* Append-only: size only grows */

        if (file->inode->sb) {
            file->inode->sb->total_bytes_written += bytes_written;
        }

        ctx->total_writes++;
        ctx->total_bytes_written += bytes_written;
    }

    return bytes_written;
}

vfs_off_t vfs_seek(struct vfs_context *ctx, vfs_fd_t fd, vfs_off_t offset, int whence) {
    if (!ctx) return VFS_ERR_INVAL;

    struct vfs_file *file = fd_get(ctx, fd);
    if (!file) return VFS_ERR_BADF;

    vfs_off_t new_pos;

    switch (whence) {
        case VFS_SEEK_SET:
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:
            new_pos = file->pos + offset;
            break;
        case VFS_SEEK_END:
            new_pos = file->inode->size + offset;
            break;
        default:
            return VFS_ERR_INVAL;
    }

    if (new_pos < 0) return VFS_ERR_INVAL;

    /* Call filesystem seek if available */
    if (file->inode->fops && file->inode->fops->seek) {
        new_pos = file->inode->fops->seek(file, offset, whence);
        if (new_pos < 0) return new_pos;
    }

    file->pos = new_pos;
    return new_pos;
}

vfs_error_t vfs_sync(struct vfs_context *ctx, vfs_fd_t fd) {
    if (!ctx) return VFS_ERR_INVAL;

    struct vfs_file *file = fd_get(ctx, fd);
    if (!file) return VFS_ERR_BADF;

    if (file->inode->fops && file->inode->fops->sync) {
        return file->inode->fops->sync(file);
    }

    return VFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * DIRECTORY OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

vfs_error_t vfs_mkdir(struct vfs_context *ctx, phantom_pid_t pid,
                      const char *path, vfs_mode_t mode) {
    if (!ctx || !path) return VFS_ERR_INVAL;

    /* Split path */
    char dir_path[VFS_MAX_PATH];
    char dirname[VFS_MAX_NAME + 1];

    const char *last_slash = strrchr(path, '/');
    if (last_slash) {
        size_t dir_len = last_slash - path;
        if (dir_len == 0) dir_len = 1;
        memcpy(dir_path, path, dir_len);
        dir_path[dir_len] = '\0';
        strncpy(dirname, last_slash + 1, VFS_MAX_NAME);
    } else {
        strcpy(dir_path, "/");
        strncpy(dirname, path, VFS_MAX_NAME);
    }

    /* Resolve parent */
    struct vfs_dentry *parent = NULL;
    vfs_error_t err = vfs_resolve_path(ctx, dir_path, &parent);
    if (err != VFS_OK) return err;

    if (!parent->inode || parent->inode->type != VFS_TYPE_DIRECTORY) {
        return VFS_ERR_NOTDIR;
    }

    /* Check if already exists */
    if (dentry_lookup_child(parent, dirname)) {
        return VFS_ERR_EXIST;
    }

    /* Create directory inode */
    struct vfs_inode *new_inode = NULL;
    if (parent->inode->ops && parent->inode->ops->mkdir) {
        err = parent->inode->ops->mkdir(parent->inode, dirname, mode, &new_inode);
        if (err != VFS_OK) return err;
    } else {
        new_inode = inode_alloc(parent->inode->sb, VFS_TYPE_DIRECTORY);
        if (!new_inode) return VFS_ERR_NOMEM;
        new_inode->mode = mode;
        new_inode->owner_pid = pid;
    }

    /* Create dentry */
    struct vfs_dentry *dentry = dentry_alloc(dirname);
    if (!dentry) {
        free(new_inode);  /* Free inode if dentry allocation fails */
        return VFS_ERR_NOMEM;
    }

    dentry->inode = new_inode;
    dentry_add_child(parent, dentry);

    printf("  [vfs] Created directory: %s\n", path);
    return VFS_OK;
}

/* Callback context for readdir */
typedef struct {
    struct vfs_dirent *entries;
    size_t max_entries;
    size_t *count;
} readdir_ctx_t;

/* Callback for filesystem readdir */
static void vfs_readdir_callback(const char *name, vfs_ino_t ino,
                                  vfs_file_type_t type, void *ctx) {
    readdir_ctx_t *rctx = (readdir_ctx_t *)ctx;
    if (*rctx->count >= rctx->max_entries) return;

    rctx->entries[*rctx->count].ino = ino;
    rctx->entries[*rctx->count].type = type;
    strncpy(rctx->entries[*rctx->count].name, name, VFS_MAX_NAME);
    rctx->entries[*rctx->count].name[VFS_MAX_NAME] = '\0';
    (*rctx->count)++;
}

vfs_error_t vfs_readdir(struct vfs_context *ctx, vfs_fd_t fd,
                        struct vfs_dirent *entries, size_t max_entries,
                        size_t *count_out) {
    if (!ctx || !entries || !count_out) return VFS_ERR_INVAL;

    struct vfs_file *file = fd_get(ctx, fd);
    if (!file) return VFS_ERR_BADF;

    if (file->inode->type != VFS_TYPE_DIRECTORY) {
        return VFS_ERR_NOTDIR;
    }

    size_t count = 0;

    /* Use filesystem readdir if available */
    if (file->inode->fops && file->inode->fops->readdir) {
        readdir_ctx_t rctx = {
            .entries = entries,
            .max_entries = max_entries,
            .count = &count
        };
        file->inode->fops->readdir(file, vfs_readdir_callback, &rctx);
    } else {
        /* Fall back to in-memory dentry tree */
        struct vfs_dentry *child = file->dentry->children;

        while (child && count < max_entries) {
            if (!child->is_hidden) {
                entries[count].ino = child->inode ? child->inode->ino : 0;
                entries[count].type = child->inode ? child->inode->type : VFS_TYPE_REGULAR;
                strncpy(entries[count].name, child->name, VFS_MAX_NAME);
                count++;
            }
            child = child->sibling;
        }
    }

    *count_out = count;
    return VFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FILE INFORMATION
 * ══════════════════════════════════════════════════════════════════════════════ */

vfs_error_t vfs_stat(struct vfs_context *ctx, const char *path,
                     struct vfs_stat *stat_out) {
    if (!ctx || !path || !stat_out) return VFS_ERR_INVAL;

    struct vfs_dentry *dentry = NULL;
    vfs_error_t err = vfs_resolve_path(ctx, path, &dentry);
    if (err != VFS_OK) return err;

    if (!dentry->inode) return VFS_ERR_NOENT;

    struct vfs_inode *inode = dentry->inode;
    stat_out->ino = inode->ino;
    stat_out->type = inode->type;
    stat_out->mode = inode->mode;
    stat_out->nlink = inode->nlink;
    stat_out->size = inode->size;
    stat_out->blocks = (inode->size + 4095) / 4096;
    stat_out->created = inode->created;
    stat_out->modified = inode->modified;
    stat_out->accessed = inode->accessed;
    stat_out->owner_pid = inode->owner_pid;

    return VFS_OK;
}

vfs_error_t vfs_fstat(struct vfs_context *ctx, vfs_fd_t fd,
                      struct vfs_stat *stat_out) {
    if (!ctx || !stat_out) return VFS_ERR_INVAL;

    struct vfs_file *file = fd_get(ctx, fd);
    if (!file) return VFS_ERR_BADF;

    struct vfs_inode *inode = file->inode;
    stat_out->ino = inode->ino;
    stat_out->type = inode->type;
    stat_out->mode = inode->mode;
    stat_out->nlink = inode->nlink;
    stat_out->size = inode->size;
    stat_out->blocks = (inode->size + 4095) / 4096;
    stat_out->created = inode->created;
    stat_out->modified = inode->modified;
    stat_out->accessed = inode->accessed;
    stat_out->owner_pid = inode->owner_pid;

    return VFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PHANTOM-SPECIFIC: HIDE (NOT DELETE)
 * ══════════════════════════════════════════════════════════════════════════════ */

vfs_error_t vfs_hide(struct vfs_context *ctx, phantom_pid_t pid,
                     const char *path) {
    if (!ctx || !path) return VFS_ERR_INVAL;
    (void)pid;  /* For audit logging */

    struct vfs_dentry *dentry = NULL;
    vfs_error_t err = vfs_resolve_path(ctx, path, &dentry);
    if (err != VFS_OK) return err;

    if (!dentry || dentry == ctx->root) {
        return VFS_ERR_PERM;  /* Can't hide root */
    }

    /* If directory, check if empty */
    if (dentry->inode && dentry->inode->type == VFS_TYPE_DIRECTORY) {
        if (dentry->children) {
            return VFS_ERR_NOTEMPTY;
        }
    }

    /* Hide the entry (but preserve it!) */
    dentry->is_hidden = 1;
    dentry->hidden_at = vfs_time_now();

    /* Call filesystem hide if available */
    if (dentry->parent && dentry->parent->inode &&
        dentry->parent->inode->ops && dentry->parent->inode->ops->hide) {
        dentry->parent->inode->ops->hide(dentry->parent->inode, dentry->name);
    }

    printf("  [vfs] Hidden: %s (preserved in geology)\n", path);
    return VFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * SYMBOLIC LINKS
 * ══════════════════════════════════════════════════════════════════════════════ */

vfs_error_t vfs_symlink(struct vfs_context *ctx, phantom_pid_t pid,
                        const char *target, const char *link_path) {
    if (!ctx || !target || !link_path) return VFS_ERR_INVAL;

    /* Split link path */
    char dir_path[VFS_MAX_PATH];
    char linkname[VFS_MAX_NAME + 1];

    const char *last_slash = strrchr(link_path, '/');
    if (last_slash) {
        size_t dir_len = last_slash - link_path;
        if (dir_len == 0) dir_len = 1;
        memcpy(dir_path, link_path, dir_len);
        dir_path[dir_len] = '\0';
        strncpy(linkname, last_slash + 1, VFS_MAX_NAME);
    } else {
        strcpy(dir_path, "/");
        strncpy(linkname, link_path, VFS_MAX_NAME);
    }

    /* Resolve parent */
    struct vfs_dentry *parent = NULL;
    vfs_error_t err = vfs_resolve_path(ctx, dir_path, &parent);
    if (err != VFS_OK) return err;

    /* Create symlink inode */
    struct vfs_inode *new_inode = inode_alloc(parent->inode->sb, VFS_TYPE_SYMLINK);
    if (!new_inode) return VFS_ERR_NOMEM;

    new_inode->owner_pid = pid;
    new_inode->size = strlen(target);
    /* Store target in fs_data (simplified) */
    new_inode->fs_data = strdup(target);

    /* Create dentry */
    struct vfs_dentry *dentry = dentry_alloc(linkname);
    if (!dentry) {
        free(new_inode->fs_data);  /* Free strdup'd target */
        free(new_inode);           /* Free inode */
        return VFS_ERR_NOMEM;
    }

    dentry->inode = new_inode;
    dentry_add_child(parent, dentry);

    printf("  [vfs] Created symlink: %s -> %s\n", link_path, target);
    return VFS_OK;
}

vfs_error_t vfs_readlink(struct vfs_context *ctx, const char *path,
                         char *buf, size_t size) {
    if (!ctx || !path || !buf) return VFS_ERR_INVAL;

    struct vfs_dentry *dentry = NULL;
    vfs_error_t err = vfs_resolve_path(ctx, path, &dentry);
    if (err != VFS_OK) return err;

    if (!dentry->inode || dentry->inode->type != VFS_TYPE_SYMLINK) {
        return VFS_ERR_INVAL;
    }

    /* Read target from fs_data */
    if (dentry->inode->fs_data) {
        strncpy(buf, (char *)dentry->inode->fs_data, size - 1);
        buf[size - 1] = '\0';
    }

    return VFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * COPY AND RENAME OPERATIONS
 * In Phantom, rename creates a new reference while preserving the old in geology.
 * Copy creates a new file with the same content (zero-copy if same filesystem).
 * ══════════════════════════════════════════════════════════════════════════════ */

vfs_error_t vfs_copy(struct vfs_context *ctx, phantom_pid_t pid,
                     const char *src_path, const char *dst_path) {
    if (!ctx || !src_path || !dst_path) return VFS_ERR_INVAL;

    /* Check source exists */
    struct vfs_stat src_stat;
    vfs_error_t err = vfs_stat(ctx, src_path, &src_stat);
    if (err != VFS_OK) return err;

    /* Can't copy directories (yet) */
    if (src_stat.type == VFS_TYPE_DIRECTORY) {
        return VFS_ERR_ISDIR;
    }

    /* Open source for reading */
    vfs_fd_t src_fd = vfs_open(ctx, pid, src_path, VFS_O_RDONLY, 0);
    if (src_fd < 0) return (vfs_error_t)src_fd;

    /* Create destination file */
    vfs_fd_t dst_fd = vfs_open(ctx, pid, dst_path, VFS_O_WRONLY | VFS_O_CREATE, 0644);
    if (dst_fd < 0) {
        vfs_close(ctx, src_fd);
        return (vfs_error_t)dst_fd;
    }

    /* Copy content in chunks */
    char buffer[8192];
    ssize_t bytes_read;
    size_t total_copied = 0;

    while ((bytes_read = vfs_read(ctx, src_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t bytes_written = vfs_write(ctx, dst_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            vfs_close(ctx, src_fd);
            vfs_close(ctx, dst_fd);
            return VFS_ERR_IO;
        }
        total_copied += bytes_written;
    }

    vfs_close(ctx, src_fd);
    vfs_close(ctx, dst_fd);

    printf("  [vfs] Copied %s -> %s (%zu bytes)\n", src_path, dst_path, total_copied);
    return VFS_OK;
}

vfs_error_t vfs_rename(struct vfs_context *ctx, phantom_pid_t pid,
                       const char *old_path, const char *new_path) {
    if (!ctx || !old_path || !new_path) return VFS_ERR_INVAL;

    /* In Phantom, rename = copy + hide old
     * This preserves the old file in geology history while creating new reference */

    /* Check source exists */
    struct vfs_stat src_stat;
    vfs_error_t err = vfs_stat(ctx, old_path, &src_stat);
    if (err != VFS_OK) return err;

    /* Check destination doesn't exist */
    struct vfs_stat dst_stat;
    if (vfs_stat(ctx, new_path, &dst_stat) == VFS_OK) {
        return VFS_ERR_EXIST;  /* Destination already exists */
    }

    /* For directories, create new dir and move contents */
    if (src_stat.type == VFS_TYPE_DIRECTORY) {
        /* Create new directory */
        err = vfs_mkdir(ctx, pid, new_path, src_stat.mode);
        if (err != VFS_OK && err != VFS_ERR_EXIST) return err;

        /* Move directory contents would require recursion - for now just rename empty dirs */
        /* Hide old directory */
        err = vfs_hide(ctx, pid, old_path);
        if (err != VFS_OK) {
            printf("  [vfs] Warning: Could not hide old directory (may not be empty)\n");
        }

        printf("  [vfs] Renamed directory %s -> %s\n", old_path, new_path);
        return VFS_OK;
    }

    /* For files, copy content then hide old */
    err = vfs_copy(ctx, pid, old_path, new_path);
    if (err != VFS_OK) return err;

    /* Hide original (preserved in geology) */
    err = vfs_hide(ctx, pid, old_path);
    if (err != VFS_OK) {
        printf("  [vfs] Warning: Copied but could not hide original\n");
    }

    printf("  [vfs] Renamed %s -> %s (original preserved in geology)\n", old_path, new_path);
    return VFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FILE SEARCH
 * Recursively search for files matching a pattern.
 * Pattern supports * (any chars) and ? (single char).
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Simple pattern matching */
static int pattern_match(const char *pattern, const char *str) {
    if (!pattern || !str) return 0;

    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return 1;  /* Trailing * matches everything */

            /* Try matching rest from each position */
            while (*str) {
                if (pattern_match(pattern, str)) return 1;
                str++;
            }
            return pattern_match(pattern, str);
        } else if (*pattern == '?' || *pattern == *str) {
            pattern++;
            str++;
        } else {
            return 0;
        }
    }

    /* Handle trailing *s */
    while (*pattern == '*') pattern++;
    return (*pattern == '\0' && *str == '\0');
}

/* Recursive search helper */
static void vfs_search_recursive(struct vfs_context *ctx, const char *dir_path,
                                 const char *pattern, vfs_search_callback_t callback,
                                 void *user_ctx, int depth) {
    if (depth > 32) return;  /* Prevent infinite recursion */

    /* Open directory */
    vfs_fd_t fd = vfs_open(ctx, 1, dir_path, VFS_O_RDONLY | VFS_O_DIRECTORY, 0);
    if (fd < 0) return;

    /* Read entries */
    struct vfs_dirent entries[64];
    size_t count;

    if (vfs_readdir(ctx, fd, entries, 64, &count) == VFS_OK) {
        for (size_t i = 0; i < count; i++) {
            /* Skip . and .. */
            if (strcmp(entries[i].name, ".") == 0 ||
                strcmp(entries[i].name, "..") == 0) {
                continue;
            }

            /* Build full path */
            char full_path[VFS_MAX_PATH];
            if (strcmp(dir_path, "/") == 0) {
                snprintf(full_path, sizeof(full_path), "/%s", entries[i].name);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entries[i].name);
            }

            /* Check if name matches pattern */
            if (pattern_match(pattern, entries[i].name)) {
                struct vfs_stat stat;
                if (vfs_stat(ctx, full_path, &stat) == VFS_OK) {
                    callback(full_path, &stat, user_ctx);
                }
            }

            /* Recurse into directories */
            if (entries[i].type == VFS_TYPE_DIRECTORY) {
                vfs_search_recursive(ctx, full_path, pattern, callback, user_ctx, depth + 1);
            }
        }
    }

    vfs_close(ctx, fd);
}

vfs_error_t vfs_search(struct vfs_context *ctx, const char *start_path,
                       const char *pattern, vfs_search_callback_t callback,
                       void *user_ctx) {
    if (!ctx || !start_path || !pattern || !callback) return VFS_ERR_INVAL;

    /* SECURITY: Canonicalize start path to prevent traversal attacks */
    char canonical_path[VFS_MAX_PATH];
    if (vfs_canonicalize_path(start_path, canonical_path, sizeof(canonical_path)) != 0) {
        return VFS_ERR_INVAL;
    }

    vfs_search_recursive(ctx, canonical_path, pattern, callback, user_ctx, 0);
    return VFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FILE HISTORY (GEOLOGY INTEGRATION)
 * Get version history of a file from the geology layer.
 * Note: Simplified implementation - full history requires walking through views.
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Callback context for collecting view info */
typedef struct {
    vfs_file_version_t *versions;
    size_t max_versions;
    size_t count;
    geofs_volume_t *vol;
    const char *rel_path;
    geofs_view_t original_view;
} history_ctx_t;

static void history_view_callback(const struct geofs_view_info *info, void *ctx) {
    history_ctx_t *hctx = (history_ctx_t *)ctx;
    if (hctx->count >= hctx->max_versions) return;

    /* Switch to this view to check if file exists */
    geofs_view_switch(hctx->vol, info->id);

    /* Try to resolve file in this view */
    geofs_hash_t hash;
    if (geofs_ref_resolve(hctx->vol, hctx->rel_path, hash) == GEOFS_OK) {
        /* Found file in this view */
        vfs_file_version_t *ver = &hctx->versions[hctx->count];
        ver->view_id = info->id;
        strncpy(ver->view_label, info->label, sizeof(ver->view_label) - 1);
        ver->view_label[sizeof(ver->view_label) - 1] = '\0';
        ver->timestamp = info->created;
        ver->size = 0;  /* Size not easily available without reading content */

        /* Convert hash to hex string */
        for (int j = 0; j < 32; j++) {
            snprintf(ver->content_hash + j * 2, 3, "%02x", hash[j]);
        }

        hctx->count++;
    }
}

vfs_error_t vfs_get_history(struct vfs_context *ctx, const char *path,
                            vfs_file_version_t *versions, size_t max_versions,
                            size_t *count_out) {
    if (!ctx || !path || !versions || !count_out) return VFS_ERR_INVAL;

    *count_out = 0;

    /* SECURITY: Canonicalize path to prevent traversal attacks */
    char canonical_path[VFS_MAX_PATH];
    if (vfs_canonicalize_path(path, canonical_path, sizeof(canonical_path)) != 0) {
        return VFS_ERR_INVAL;
    }

    /* Find mount point for this path */
    struct vfs_mount *mount = ctx->mounts;
    struct vfs_mount *best_mount = NULL;
    size_t best_len = 0;

    while (mount) {
        size_t len = strlen(mount->mount_path);
        if (strncmp(canonical_path, mount->mount_path, len) == 0 && len > best_len) {
            best_mount = mount;
            best_len = len;
        }
        mount = mount->next;
    }

    if (!best_mount || !best_mount->sb || !best_mount->sb->fs_data) {
        return VFS_ERR_NOENT;
    }

    /* Check if this is a GeoFS mount */
    if (strcmp(best_mount->sb->fs_type->name, "geofs") != 0) {
        /* Non-GeoFS filesystems don't have history */
        return VFS_OK;
    }

    /* Get the GeoFS volume from superblock */
    geofs_volume_t *vol = (geofs_volume_t *)best_mount->sb->fs_data;
    if (!vol) return VFS_ERR_NOENT;

    /* Build relative path within mount */
    const char *rel_path = canonical_path + best_len;
    if (*rel_path == '\0') rel_path = "/";

    /* Save current view */
    geofs_view_t current_view = geofs_view_current(vol);

    /* Set up callback context */
    history_ctx_t hctx = {
        .versions = versions,
        .max_versions = max_versions,
        .count = 0,
        .vol = vol,
        .rel_path = rel_path,
        .original_view = current_view
    };

    /* List all views and check each for this file */
    geofs_view_list(vol, history_view_callback, &hctx);

    /* Restore original view */
    geofs_view_switch(vol, current_view);

    *count_out = hctx.count;
    return VFS_OK;
}

vfs_error_t vfs_restore_version(struct vfs_context *ctx, phantom_pid_t pid,
                                const char *path, uint64_t view_id,
                                const char *restore_path) {
    if (!ctx || !path || !restore_path) return VFS_ERR_INVAL;

    /* SECURITY: Canonicalize paths to prevent traversal attacks */
    char canonical_path[VFS_MAX_PATH];
    char canonical_restore[VFS_MAX_PATH];
    if (vfs_canonicalize_path(path, canonical_path, sizeof(canonical_path)) != 0) {
        return VFS_ERR_INVAL;
    }
    if (vfs_canonicalize_path(restore_path, canonical_restore, sizeof(canonical_restore)) != 0) {
        return VFS_ERR_INVAL;
    }

    /* Find mount point for source path */
    struct vfs_mount *mount = ctx->mounts;
    struct vfs_mount *best_mount = NULL;
    size_t best_len = 0;

    while (mount) {
        size_t len = strlen(mount->mount_path);
        if (strncmp(canonical_path, mount->mount_path, len) == 0 && len > best_len) {
            best_mount = mount;
            best_len = len;
        }
        mount = mount->next;
    }

    if (!best_mount || !best_mount->sb || !best_mount->sb->fs_data) {
        return VFS_ERR_NOENT;
    }

    /* Check if this is a GeoFS mount */
    if (strcmp(best_mount->sb->fs_type->name, "geofs") != 0) {
        return VFS_ERR_NOSYS;  /* Can't restore from non-GeoFS */
    }

    geofs_volume_t *vol = (geofs_volume_t *)best_mount->sb->fs_data;
    if (!vol) return VFS_ERR_NOENT;

    /* Build relative path within mount */
    const char *rel_path = canonical_path + best_len;
    if (*rel_path == '\0') rel_path = "/";

    /* Save current view */
    geofs_view_t current_view = geofs_view_current(vol);

    /* Switch to historical view */
    if (geofs_view_switch(vol, view_id) != GEOFS_OK) {
        return VFS_ERR_NOENT;
    }

    /* Resolve file in historical view */
    geofs_hash_t hash;
    if (geofs_ref_resolve(vol, rel_path, hash) != GEOFS_OK) {
        geofs_view_switch(vol, current_view);
        return VFS_ERR_NOENT;
    }

    /* Read historical content - use a reasonable buffer size */
    size_t buffer_size = 1024 * 1024;  /* 1MB max for now */
    uint8_t *content = malloc(buffer_size);
    if (!content) {
        geofs_view_switch(vol, current_view);
        return VFS_ERR_NOMEM;
    }

    size_t read_size;
    if (geofs_content_read(vol, hash, content, buffer_size, &read_size) != GEOFS_OK) {
        free(content);
        geofs_view_switch(vol, current_view);
        return VFS_ERR_IO;
    }

    /* Restore to current view */
    geofs_view_switch(vol, current_view);

    /* Write content to restore path */
    vfs_fd_t fd = vfs_open(ctx, pid, canonical_restore, VFS_O_WRONLY | VFS_O_CREATE, 0644);
    if (fd < 0) {
        free(content);
        return (vfs_error_t)fd;
    }

    ssize_t written = vfs_write(ctx, fd, content, read_size);
    vfs_close(ctx, fd);
    free(content);

    if (written != (ssize_t)read_size) {
        return VFS_ERR_IO;
    }

    printf("  [vfs] Restored %s from view %lu to %s (%zu bytes)\n",
           canonical_path, (unsigned long)view_id, canonical_restore, read_size);
    return VFS_OK;
}
