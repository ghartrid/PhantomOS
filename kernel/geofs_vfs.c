/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                         PHANTOM GeoFS VFS ADAPTER
 *                      "To Create, Not To Destroy"
 *
 *    Bridges the VFS layer to GeoFS persistent storage.
 *    All file operations are routed to the geology layer.
 *    Nothing is ever deleted - only new versions are created.
 *
 * ══════════════════════════════════════════════════════════════════════════════
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vfs.h"
#include "phantom.h"
#include "../geofs.h"

/* ══════════════════════════════════════════════════════════════════════════════
 * DATA STRUCTURES
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * GeoFS superblock data - holds reference to the volume
 */
struct geofs_vfs_sb_data {
    geofs_volume_t     *volume;
    char                mount_path[VFS_MAX_PATH];
    uint64_t            files_created;
    uint64_t            dirs_created;
    uint64_t            bytes_written;
};

/*
 * GeoFS inode data - maps VFS inode to GeoFS content
 */
struct geofs_vfs_inode_data {
    char                path[VFS_MAX_PATH];     /* Full path in GeoFS */
    geofs_hash_t        content_hash;           /* Hash of current content */
    uint64_t            size;                   /* Content size */
    int                 is_directory;
    geofs_volume_t     *volume;                 /* Reference to volume */
};

/*
 * GeoFS file data - per-open-file state
 */
struct geofs_vfs_file_data {
    char               *content;                /* Buffered content */
    size_t              content_size;           /* Size of content */
    size_t              content_capacity;       /* Allocated capacity */
    int                 dirty;                  /* Modified since open */
    char                path[VFS_MAX_PATH];     /* Path for writes */
    geofs_volume_t     *volume;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static phantom_time_t geofs_vfs_time_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Build full GeoFS path from mount point and relative path */
static void build_geofs_path(const char *mount_path, const char *rel_path,
                              char *out, size_t out_size) {
    if (rel_path[0] == '/') {
        /* Absolute path - use as-is */
        strncpy(out, rel_path, out_size - 1);
    } else {
        /* Relative to mount point */
        snprintf(out, out_size, "%s/%s", mount_path, rel_path);
    }
    out[out_size - 1] = '\0';
}

/* Build child path from parent directory path and child name */
static void build_child_path(const char *dir_path, const char *name,
                             char *out, size_t out_size) {
    if (out_size == 0) return;
    size_t dir_len = strlen(dir_path);
    size_t name_len = strlen(name);

    if (dir_len == 1 && dir_path[0] == '/') {
        /* Root directory */
        if (1 + name_len < out_size) {
            out[0] = '/';
            memcpy(out + 1, name, name_len);
            out[1 + name_len] = '\0';
        } else {
            out[0] = '/';
            memcpy(out + 1, name, out_size - 2);
            out[out_size - 1] = '\0';
        }
    } else {
        /* Non-root directory */
        size_t total = dir_len + 1 + name_len;
        if (total < out_size) {
            memcpy(out, dir_path, dir_len);
            out[dir_len] = '/';
            memcpy(out + dir_len + 1, name, name_len);
            out[dir_len + 1 + name_len] = '\0';
        } else {
            /* Truncate */
            memcpy(out, dir_path, (dir_len < out_size - 1) ? dir_len : out_size - 1);
            if (dir_len < out_size - 1) {
                out[dir_len] = '/';
                size_t remain = out_size - dir_len - 2;
                if (remain > 0 && name_len > 0) {
                    memcpy(out + dir_len + 1, name, (name_len < remain) ? name_len : remain);
                }
            }
            out[out_size - 1] = '\0';
        }
    }
    out[out_size - 1] = '\0';
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FILE OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static vfs_error_t geofs_vfs_open(struct vfs_inode *inode, struct vfs_file *file) {
    struct geofs_vfs_inode_data *idata = inode->fs_data;
    if (!idata) return VFS_ERR_IO;

    /* Allocate file data */
    struct geofs_vfs_file_data *fdata = calloc(1, sizeof(struct geofs_vfs_file_data));
    if (!fdata) return VFS_ERR_NOMEM;

    strncpy(fdata->path, idata->path, VFS_MAX_PATH - 1);
    fdata->volume = idata->volume;

    /* Load existing content if file exists */
    if (idata->size > 0) {
        fdata->content = malloc(idata->size + 1);
        if (!fdata->content) {
            free(fdata);
            return VFS_ERR_NOMEM;
        }

        size_t got = 0;
        geofs_error_t err = geofs_content_read(idata->volume, idata->content_hash,
                                                fdata->content, idata->size, &got);
        if (err == GEOFS_OK) {
            fdata->content_size = got;
            fdata->content_capacity = idata->size + 1;
            fdata->content[got] = '\0';
        } else {
            /* File doesn't exist yet or read failed - start empty */
            fdata->content_size = 0;
            fdata->content_capacity = idata->size + 1;
        }
    }

    file->private_data = fdata;
    return VFS_OK;
}

static vfs_error_t geofs_vfs_close(struct vfs_file *file) {
    struct geofs_vfs_file_data *fdata = file->private_data;
    if (!fdata) return VFS_OK;

    /* If dirty, write content to GeoFS */
    if (fdata->dirty && fdata->content && fdata->content_size > 0) {
        geofs_hash_t hash;
        geofs_error_t err = geofs_content_store(fdata->volume, fdata->content,
                                                 fdata->content_size, hash);
        if (err == GEOFS_OK) {
            /* Update reference */
            geofs_ref_create(fdata->volume, fdata->path, hash);

            /* Update inode */
            struct geofs_vfs_inode_data *idata = file->inode->fs_data;
            if (idata) {
                memcpy(idata->content_hash, hash, GEOFS_HASH_SIZE);
                idata->size = fdata->content_size;
            }
            file->inode->size = fdata->content_size;
        }
    }

    free(fdata->content);
    free(fdata);
    file->private_data = NULL;

    return VFS_OK;
}

static ssize_t geofs_vfs_read(struct vfs_file *file, void *buf, size_t count) {
    struct geofs_vfs_file_data *fdata = file->private_data;
    if (!fdata) return VFS_ERR_IO;

    /* Check bounds */
    if (file->pos >= (vfs_off_t)fdata->content_size) {
        return 0;  /* EOF */
    }

    size_t remaining = fdata->content_size - file->pos;
    size_t to_read = count < remaining ? count : remaining;

    if (fdata->content && to_read > 0) {
        memcpy(buf, fdata->content + file->pos, to_read);
    }

    return to_read;
}

static ssize_t geofs_vfs_write(struct vfs_file *file, const void *buf, size_t count) {
    struct geofs_vfs_file_data *fdata = file->private_data;
    if (!fdata) return VFS_ERR_IO;

    /* In Phantom, all writes are appends */
    size_t new_size = fdata->content_size + count;

    /* Grow buffer if needed */
    if (new_size > fdata->content_capacity) {
        size_t new_cap = new_size * 2;
        if (new_cap < 1024) new_cap = 1024;

        char *new_content = realloc(fdata->content, new_cap);
        if (!new_content) return VFS_ERR_NOMEM;

        fdata->content = new_content;
        fdata->content_capacity = new_cap;
    }

    /* Append data */
    memcpy(fdata->content + fdata->content_size, buf, count);
    fdata->content_size = new_size;
    fdata->dirty = 1;

    return count;
}

static vfs_off_t geofs_vfs_seek(struct vfs_file *file, vfs_off_t offset, int whence) {
    struct geofs_vfs_file_data *fdata = file->private_data;
    if (!fdata) return VFS_ERR_IO;

    vfs_off_t new_pos;

    switch (whence) {
        case VFS_SEEK_SET:
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:
            new_pos = file->pos + offset;
            break;
        case VFS_SEEK_END:
            new_pos = fdata->content_size + offset;
            break;
        default:
            return VFS_ERR_INVAL;
    }

    if (new_pos < 0) return VFS_ERR_INVAL;

    file->pos = new_pos;
    return new_pos;
}

static vfs_error_t geofs_vfs_sync(struct vfs_file *file) {
    struct geofs_vfs_file_data *fdata = file->private_data;
    if (!fdata || !fdata->dirty) return VFS_OK;

    /* Write content to GeoFS */
    if (fdata->content && fdata->content_size > 0) {
        geofs_hash_t hash;
        geofs_error_t err = geofs_content_store(fdata->volume, fdata->content,
                                                 fdata->content_size, hash);
        if (err == GEOFS_OK) {
            geofs_ref_create(fdata->volume, fdata->path, hash);
            fdata->dirty = 0;

            /* Update inode */
            struct geofs_vfs_inode_data *idata = file->inode->fs_data;
            if (idata) {
                memcpy(idata->content_hash, hash, GEOFS_HASH_SIZE);
                idata->size = fdata->content_size;
            }
        } else {
            return VFS_ERR_IO;
        }
    }

    return VFS_OK;
}

static const struct vfs_file_operations geofs_vfs_file_ops = {
    .open = geofs_vfs_open,
    .close = geofs_vfs_close,
    .read = geofs_vfs_read,
    .write = geofs_vfs_write,
    .seek = geofs_vfs_seek,
    .readdir = NULL,  /* Directories use different ops */
    .sync = geofs_vfs_sync,
    .ioctl = NULL,
};

/* ══════════════════════════════════════════════════════════════════════════════
 * DIRECTORY OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Directory entry format stored in GeoFS */
struct geofs_dir_entry {
    char        name[VFS_MAX_NAME + 1];
    int         is_directory;
    uint64_t    size;
    geofs_hash_t content_hash;
};

struct geofs_dir_data {
    struct geofs_dir_entry *entries;
    size_t      count;
    size_t      capacity;
};

static vfs_error_t geofs_vfs_dir_open(struct vfs_inode *inode, struct vfs_file *file) {
    (void)inode;
    file->private_data = NULL;  /* Directories don't need file data */
    return VFS_OK;
}

static vfs_error_t geofs_vfs_dir_close(struct vfs_file *file) {
    (void)file;
    return VFS_OK;
}

/* Callback for directory listing */
struct geofs_readdir_ctx {
    void (*vfs_callback)(const char *name, vfs_ino_t ino,
                         vfs_file_type_t type, void *ctx);
    void *vfs_ctx;
    const char *dir_path;
    size_t dir_path_len;
};

static void geofs_readdir_callback(const struct geofs_dirent *entry, void *user_ctx) {
    struct geofs_readdir_ctx *ctx = user_ctx;
    if (!ctx || !ctx->vfs_callback) return;

    /* Determine file type */
    vfs_file_type_t type = entry->is_dir ? VFS_TYPE_DIRECTORY : VFS_TYPE_REGULAR;

    /* Call VFS callback */
    ctx->vfs_callback(entry->name, 0, type, ctx->vfs_ctx);
}

static vfs_error_t geofs_vfs_readdir(struct vfs_file *file,
                                      void (*callback)(const char *name,
                                                      vfs_ino_t ino,
                                                      vfs_file_type_t type,
                                                      void *ctx),
                                      void *ctx) {
    struct geofs_vfs_inode_data *idata = file->inode->fs_data;
    if (!idata || !idata->volume) return VFS_ERR_IO;

    /* Set up callback context */
    struct geofs_readdir_ctx list_ctx = {
        .vfs_callback = callback,
        .vfs_ctx = ctx,
        .dir_path = idata->path,
        .dir_path_len = strlen(idata->path)
    };

    /* Use geofs_ref_list to enumerate directory */
    geofs_ref_list(idata->volume, idata->path, geofs_readdir_callback, &list_ctx);

    return VFS_OK;
}

static const struct vfs_file_operations geofs_vfs_dir_ops = {
    .open = geofs_vfs_dir_open,
    .close = geofs_vfs_dir_close,
    .read = NULL,
    .write = NULL,
    .seek = NULL,
    .readdir = geofs_vfs_readdir,
    .sync = NULL,
    .ioctl = NULL,
};

/* ══════════════════════════════════════════════════════════════════════════════
 * INODE OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Forward declaration */
static const struct vfs_inode_operations geofs_vfs_inode_ops;

static struct vfs_dentry *geofs_vfs_lookup(struct vfs_inode *dir, const char *name) {
    struct geofs_vfs_inode_data *dir_data = dir->fs_data;
    if (!dir_data || !dir_data->volume) return NULL;

    /* Build path - extra space for dir + "/" + name */
    char path[VFS_MAX_PATH + VFS_MAX_NAME + 2];
    build_child_path(dir_data->path, name, path, sizeof(path));

    /* Check if path exists in GeoFS */
    geofs_hash_t hash;
    geofs_error_t err = geofs_ref_resolve(dir_data->volume, path, hash);

    if (err != GEOFS_OK) {
        return NULL;  /* Not found */
    }

    /* Create dentry and inode */
    struct vfs_dentry *dentry = calloc(1, sizeof(struct vfs_dentry));
    if (!dentry) return NULL;
    strncpy(dentry->name, name, VFS_MAX_NAME);

    struct vfs_inode *inode = calloc(1, sizeof(struct vfs_inode));
    if (!inode) { free(dentry); return NULL; }

    struct geofs_vfs_inode_data *idata = calloc(1, sizeof(struct geofs_vfs_inode_data));
    if (!idata) { free(inode); free(dentry); return NULL; }

    strncpy(idata->path, path, VFS_MAX_PATH - 1);
    memcpy(idata->content_hash, hash, GEOFS_HASH_SIZE);
    idata->volume = dir_data->volume;

    /* Get content size from GeoFS */
    uint64_t content_size = 0;
    geofs_content_size(dir_data->volume, hash, &content_size);
    idata->size = content_size;

    /* Read content to determine if file or directory */
    /* Directories have "__PHANTOM_DIR__" as content marker */
    /* Symlinks have "__PHANTOM_SYMLINK__" prefix */
    char probe[32];
    size_t got = 0;
    idata->is_directory = 0;

    if (geofs_content_read(dir_data->volume, hash, probe, sizeof(probe) - 1, &got) == GEOFS_OK) {
        probe[got] = '\0';
        if (strncmp(probe, "__PHANTOM_DIR__", 15) == 0) {
            idata->is_directory = 1;
        } else if (strncmp(probe, "__PHANTOM_SYMLINK__", 19) == 0) {
            /* It's a symlink - for now treat as file */
            idata->is_directory = 0;
        }
    }

    inode->type = idata->is_directory ? VFS_TYPE_DIRECTORY : VFS_TYPE_REGULAR;
    inode->fs_data = idata;
    inode->fops = idata->is_directory ? &geofs_vfs_dir_ops : &geofs_vfs_file_ops;
    inode->ops = idata->is_directory ? &geofs_vfs_inode_ops : NULL;
    inode->size = idata->size;
    inode->created = geofs_vfs_time_now();
    inode->modified = inode->created;
    inode->accessed = inode->created;

    dentry->inode = inode;
    return dentry;
}

static vfs_error_t geofs_vfs_create(struct vfs_inode *dir, const char *name,
                                     vfs_mode_t mode, struct vfs_inode **inode_out) {
    struct geofs_vfs_inode_data *dir_data = dir->fs_data;
    if (!dir_data || !dir_data->volume) return VFS_ERR_IO;

    /* Build path - extra space for dir + "/" + name */
    char path[VFS_MAX_PATH + VFS_MAX_NAME + 2];
    build_child_path(dir_data->path, name, path, sizeof(path));

    /* Create empty file in GeoFS */
    geofs_hash_t hash;
    geofs_error_t err = geofs_content_store(dir_data->volume, "", 0, hash);
    if (err != GEOFS_OK) return VFS_ERR_IO;

    err = geofs_ref_create(dir_data->volume, path, hash);
    if (err != GEOFS_OK) return VFS_ERR_IO;

    /* Create inode */
    struct vfs_inode *inode = calloc(1, sizeof(struct vfs_inode));
    if (!inode) return VFS_ERR_NOMEM;

    struct geofs_vfs_inode_data *idata = calloc(1, sizeof(struct geofs_vfs_inode_data));
    if (!idata) { free(inode); return VFS_ERR_NOMEM; }

    strncpy(idata->path, path, VFS_MAX_PATH - 1);
    memcpy(idata->content_hash, hash, GEOFS_HASH_SIZE);
    idata->volume = dir_data->volume;
    idata->is_directory = 0;
    idata->size = 0;

    inode->type = VFS_TYPE_REGULAR;
    inode->mode = mode;
    inode->fs_data = idata;
    inode->fops = &geofs_vfs_file_ops;
    inode->size = 0;
    inode->created = geofs_vfs_time_now();
    inode->modified = inode->created;
    inode->accessed = inode->created;
    inode->sb = dir->sb;

    if (dir->sb) {
        struct geofs_vfs_sb_data *sb_data = dir->sb->fs_data;
        if (sb_data) sb_data->files_created++;
    }

    *inode_out = inode;
    return VFS_OK;
}

static vfs_error_t geofs_vfs_mkdir(struct vfs_inode *dir, const char *name,
                                    vfs_mode_t mode, struct vfs_inode **inode_out) {
    struct geofs_vfs_inode_data *dir_data = dir->fs_data;
    if (!dir_data || !dir_data->volume) return VFS_ERR_IO;

    /* Build path - extra space for dir + "/" + name */
    char path[VFS_MAX_PATH + VFS_MAX_NAME + 2];
    build_child_path(dir_data->path, name, path, sizeof(path));

    /* Create directory marker in GeoFS */
    /* We store a special marker content to indicate it's a directory */
    const char *dir_marker = "__PHANTOM_DIR__";
    geofs_hash_t hash;
    geofs_error_t err = geofs_content_store(dir_data->volume, dir_marker,
                                             strlen(dir_marker), hash);
    if (err != GEOFS_OK) return VFS_ERR_IO;

    err = geofs_ref_create(dir_data->volume, path, hash);
    if (err != GEOFS_OK) return VFS_ERR_IO;

    /* Create inode */
    struct vfs_inode *inode = calloc(1, sizeof(struct vfs_inode));
    if (!inode) return VFS_ERR_NOMEM;

    struct geofs_vfs_inode_data *idata = calloc(1, sizeof(struct geofs_vfs_inode_data));
    if (!idata) { free(inode); return VFS_ERR_NOMEM; }

    strncpy(idata->path, path, VFS_MAX_PATH - 1);
    memcpy(idata->content_hash, hash, GEOFS_HASH_SIZE);
    idata->volume = dir_data->volume;
    idata->is_directory = 1;
    idata->size = 0;

    inode->type = VFS_TYPE_DIRECTORY;
    inode->mode = mode;
    inode->fs_data = idata;
    inode->fops = &geofs_vfs_dir_ops;
    inode->ops = dir->ops;  /* Inherit directory operations */
    inode->size = 0;
    inode->created = geofs_vfs_time_now();
    inode->modified = inode->created;
    inode->accessed = inode->created;
    inode->sb = dir->sb;

    if (dir->sb) {
        struct geofs_vfs_sb_data *sb_data = dir->sb->fs_data;
        if (sb_data) sb_data->dirs_created++;
    }

    *inode_out = inode;
    return VFS_OK;
}

static vfs_error_t geofs_vfs_symlink(struct vfs_inode *dir, const char *name,
                                      const char *target) {
    struct geofs_vfs_inode_data *dir_data = dir->fs_data;
    if (!dir_data || !dir_data->volume) return VFS_ERR_IO;

    /* Build path - extra space for dir + "/" + name */
    char path[VFS_MAX_PATH + VFS_MAX_NAME + 2];
    build_child_path(dir_data->path, name, path, sizeof(path));

    /* Store symlink target as content with marker */
    char symlink_content[VFS_MAX_PATH + 32];
    snprintf(symlink_content, sizeof(symlink_content), "__PHANTOM_SYMLINK__%s", target);

    geofs_hash_t hash;
    geofs_error_t err = geofs_content_store(dir_data->volume, symlink_content,
                                             strlen(symlink_content), hash);
    if (err != GEOFS_OK) return VFS_ERR_IO;

    err = geofs_ref_create(dir_data->volume, path, hash);
    if (err != GEOFS_OK) return VFS_ERR_IO;

    return VFS_OK;
}

static vfs_error_t geofs_vfs_hide(struct vfs_inode *dir, const char *name) {
    struct geofs_vfs_inode_data *dir_data = dir->fs_data;
    if (!dir_data || !dir_data->volume) return VFS_ERR_IO;

    /* Build path - extra space for dir + "/" + name */
    char path[VFS_MAX_PATH + VFS_MAX_NAME + 2];
    build_child_path(dir_data->path, name, path, sizeof(path));

    /* Hide via GeoFS view system - content is preserved */
    geofs_error_t err = geofs_view_hide(dir_data->volume, path);
    if (err != GEOFS_OK) return VFS_ERR_IO;

    printf("  [geofs_vfs] Hidden: %s (preserved in geology, view %lu)\n",
           path, geofs_view_current(dir_data->volume));

    return VFS_OK;
}

static const struct vfs_inode_operations geofs_vfs_inode_ops = {
    .lookup = geofs_vfs_lookup,
    .create = geofs_vfs_create,
    .mkdir = geofs_vfs_mkdir,
    .symlink = geofs_vfs_symlink,
    .readlink = NULL,  /* TODO: implement */
    .hide = geofs_vfs_hide,
    .getattr = NULL,
};

/* ══════════════════════════════════════════════════════════════════════════════
 * MOUNT/UNMOUNT
 * ══════════════════════════════════════════════════════════════════════════════ */

static vfs_error_t geofs_vfs_mount(struct vfs_fs_type *fs_type,
                                    const char *device,
                                    struct vfs_superblock **sb_out) {
    /* Device is the GeoFS volume pointer (passed as string hex address) */
    geofs_volume_t *volume = NULL;

    if (device) {
        /* Parse volume pointer from device string */
        sscanf(device, "%p", (void **)&volume);
    }

    if (!volume) {
        fprintf(stderr, "  [geofs_vfs] Error: No GeoFS volume specified\n");
        return VFS_ERR_INVAL;
    }

    /* Create superblock */
    struct vfs_superblock *sb = calloc(1, sizeof(struct vfs_superblock));
    if (!sb) return VFS_ERR_NOMEM;

    struct geofs_vfs_sb_data *sb_data = calloc(1, sizeof(struct geofs_vfs_sb_data));
    if (!sb_data) { free(sb); return VFS_ERR_NOMEM; }

    sb_data->volume = volume;

    /* Create root inode */
    struct vfs_inode *root = calloc(1, sizeof(struct vfs_inode));
    if (!root) { free(sb_data); free(sb); return VFS_ERR_NOMEM; }

    struct geofs_vfs_inode_data *root_data = calloc(1, sizeof(struct geofs_vfs_inode_data));
    if (!root_data) { free(root); free(sb_data); free(sb); return VFS_ERR_NOMEM; }

    strcpy(root_data->path, "/");
    root_data->volume = volume;
    root_data->is_directory = 1;

    root->type = VFS_TYPE_DIRECTORY;
    root->mode = 0755;
    root->ops = &geofs_vfs_inode_ops;
    root->fops = &geofs_vfs_dir_ops;
    root->fs_data = root_data;
    root->ino = 1;
    root->sb = sb;
    root->created = geofs_vfs_time_now();
    root->modified = root->created;
    root->accessed = root->created;

    sb->fs_type = fs_type;
    sb->root = root;
    sb->fs_data = sb_data;
    sb->block_size = 4096;

    *sb_out = sb;

    printf("  [geofs_vfs] Mounted GeoFS filesystem\n");
    printf("  [geofs_vfs] All files will persist to geology\n");

    return VFS_OK;
}

static void geofs_vfs_unmount(struct vfs_superblock *sb) {
    if (!sb) return;

    struct geofs_vfs_sb_data *sb_data = sb->fs_data;
    if (sb_data) {
        printf("  [geofs_vfs] Unmounted (data preserved)\n");
        printf("  [geofs_vfs] Statistics:\n");
        printf("    Files created: %lu\n", sb_data->files_created);
        printf("    Dirs created:  %lu\n", sb_data->dirs_created);
        printf("    Bytes written: %lu\n", sb_data->bytes_written);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * GLOBAL FILESYSTEM TYPE
 * ══════════════════════════════════════════════════════════════════════════════ */

struct vfs_fs_type geofs_vfs_type = {
    .name = "geofs",
    .flags = 0,
    .mount = geofs_vfs_mount,
    .unmount = geofs_vfs_unmount,
    .next = NULL,
};

/* ══════════════════════════════════════════════════════════════════════════════
 * HELPER FUNCTION FOR MOUNTING
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Mount GeoFS at a path with a volume reference.
 * This is a convenience wrapper for the shell/kernel.
 */
vfs_error_t geofs_vfs_mount_volume(struct vfs_context *ctx,
                                    geofs_volume_t *volume,
                                    const char *mount_path) {
    char device[32];
    snprintf(device, sizeof(device), "%p", (void *)volume);

    return vfs_mount(ctx, "geofs", device, mount_path, 0);
}
