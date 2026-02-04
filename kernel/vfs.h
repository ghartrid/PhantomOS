/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                          PHANTOM VIRTUAL FILE SYSTEM
 *                          "To Create, Not To Destroy"
 *
 *    A unified file system abstraction layer for PhantomOS.
 *    Everything is a file - but nothing is ever deleted.
 *
 * ══════════════════════════════════════════════════════════════════════════════
 */

#ifndef PHANTOM_VFS_H
#define PHANTOM_VFS_H

#include <stdint.h>
#include <stddef.h>
#include "phantom.h"
#include "../geofs.h"

/* ══════════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ══════════════════════════════════════════════════════════════════════════════ */

#define VFS_MAX_PATH            4096
#define VFS_MAX_NAME            255
#define VFS_MAX_OPEN_FILES      1024
#define VFS_MAX_MOUNTS          64
#define VFS_MAX_FS_TYPES        16

/* ══════════════════════════════════════════════════════════════════════════════
 * TYPES
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef uint64_t vfs_ino_t;         /* Inode number */
typedef int64_t  vfs_off_t;         /* File offset */
typedef uint32_t vfs_mode_t;        /* File mode/permissions */
typedef int      vfs_fd_t;          /* File descriptor */

/* File types */
typedef enum {
    VFS_TYPE_REGULAR    = 0,        /* Regular file */
    VFS_TYPE_DIRECTORY  = 1,        /* Directory */
    VFS_TYPE_SYMLINK    = 2,        /* Symbolic link (points to path, preserved) */
    VFS_TYPE_DEVICE     = 3,        /* Device file */
    VFS_TYPE_PIPE       = 4,        /* Named pipe (FIFO) */
    VFS_TYPE_SOCKET     = 5,        /* Unix socket */
    VFS_TYPE_PROC       = 6,        /* /proc entry */
} vfs_file_type_t;

/* Open flags */
#define VFS_O_RDONLY        0x0001
#define VFS_O_WRONLY        0x0002
#define VFS_O_RDWR          0x0003
#define VFS_O_APPEND        0x0008  /* Always append (Phantom default!) */
#define VFS_O_CREATE        0x0100
#define VFS_O_EXCL          0x0200
#define VFS_O_DIRECTORY     0x1000
/* Note: No VFS_O_TRUNC - truncation doesn't exist in Phantom */

/* Seek modes */
#define VFS_SEEK_SET        0
#define VFS_SEEK_CUR        1
#define VFS_SEEK_END        2

/* Error codes */
typedef enum {
    VFS_OK              =  0,
    VFS_ERR_NOENT       = -1,       /* No such file or directory */
    VFS_ERR_IO          = -2,       /* I/O error */
    VFS_ERR_NOMEM       = -3,       /* Out of memory */
    VFS_ERR_PERM        = -4,       /* Permission denied */
    VFS_ERR_EXIST       = -5,       /* File exists */
    VFS_ERR_NOTDIR      = -6,       /* Not a directory */
    VFS_ERR_ISDIR       = -7,       /* Is a directory */
    VFS_ERR_INVAL       = -8,       /* Invalid argument */
    VFS_ERR_NFILE       = -9,       /* Too many open files */
    VFS_ERR_BADF        = -10,      /* Bad file descriptor */
    VFS_ERR_NOSPC       = -11,      /* No space left */
    VFS_ERR_NOSYS       = -12,      /* Function not implemented */
    VFS_ERR_NOTEMPTY    = -13,      /* Directory not empty (for hide) */
    VFS_ERR_XDEV        = -14,      /* Cross-device link */
    /* Note: No "deleted" error - deletion doesn't exist */
} vfs_error_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * STRUCTURES
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Forward declarations */
struct vfs_inode;
struct vfs_file;
struct vfs_dentry;
struct vfs_mount;
struct vfs_superblock;
struct vfs_operations;
struct vfs_file_operations;

/*
 * Inode - represents a file system object
 * In Phantom, inodes are never deallocated, only marked dormant
 */
struct vfs_inode {
    vfs_ino_t           ino;            /* Inode number */
    vfs_file_type_t     type;           /* File type */
    vfs_mode_t          mode;           /* Permissions */
    uint32_t            nlink;          /* Link count (only increases) */
    uint64_t            size;           /* File size */

    phantom_time_t      created;        /* Creation time */
    phantom_time_t      modified;       /* Last modification */
    phantom_time_t      accessed;       /* Last access */

    phantom_pid_t       owner_pid;      /* Creator process */
    phantom_hash_t      content_hash;   /* GeoFS content hash */

    /* File system specific data */
    void               *fs_data;
    struct vfs_superblock *sb;

    /* Operations */
    const struct vfs_inode_operations *ops;
    const struct vfs_file_operations *fops;

    /* Reference counting */
    uint32_t            ref_count;

    /* Links */
    struct vfs_inode   *next;
};

/*
 * Directory entry - maps name to inode
 */
struct vfs_dentry {
    char                name[VFS_MAX_NAME + 1];
    struct vfs_inode   *inode;
    struct vfs_dentry  *parent;
    struct vfs_dentry  *children;       /* First child */
    struct vfs_dentry  *sibling;        /* Next sibling */
    struct vfs_mount   *mount;          /* Owning mount point */
    int                 is_hidden;      /* Hidden but preserved */
    phantom_time_t      hidden_at;      /* When hidden */
};

/*
 * Open file descriptor
 */
struct vfs_file {
    struct vfs_inode   *inode;
    struct vfs_dentry  *dentry;
    vfs_off_t           pos;            /* Current position */
    uint32_t            flags;          /* Open flags */
    phantom_pid_t       owner_pid;      /* Process that opened */
    phantom_time_t      opened_at;
    uint32_t            ref_count;

    /* File system specific */
    void               *private_data;
};

/*
 * File system type - describes a file system implementation
 */
struct vfs_fs_type {
    const char         *name;           /* e.g., "geofs", "procfs", "devfs" */
    uint32_t            flags;

    /* Mount a file system */
    vfs_error_t (*mount)(struct vfs_fs_type *fs_type,
                         const char *device,
                         struct vfs_superblock **sb_out);

    /* Unmount - in Phantom, this just syncs and marks dormant */
    void (*unmount)(struct vfs_superblock *sb);

    struct vfs_fs_type *next;
};

/*
 * Superblock - represents a mounted file system
 */
struct vfs_superblock {
    const struct vfs_fs_type *fs_type;
    struct vfs_inode   *root;           /* Root inode */
    void               *fs_data;        /* FS-specific data */
    uint64_t            block_size;
    uint64_t            total_blocks;
    uint64_t            free_blocks;    /* In Phantom, this only decreases */
    uint32_t            flags;

    /* Statistics (append-only) */
    uint64_t            total_inodes;
    uint64_t            total_files_created;
    uint64_t            total_bytes_written;

    struct vfs_superblock *next;
};

/*
 * Mount point
 */
struct vfs_mount {
    char                mount_path[VFS_MAX_PATH];
    struct vfs_superblock *sb;
    struct vfs_dentry  *mount_point;    /* Where mounted */
    struct vfs_dentry  *root;           /* Root of mounted FS */
    uint32_t            flags;
    phantom_time_t      mounted_at;

    struct vfs_mount   *next;
};

/*
 * Inode operations
 */
struct vfs_inode_operations {
    /* Lookup name in directory */
    struct vfs_dentry *(*lookup)(struct vfs_inode *dir, const char *name);

    /* Create file in directory */
    vfs_error_t (*create)(struct vfs_inode *dir, const char *name,
                          vfs_mode_t mode, struct vfs_inode **inode_out);

    /* Create directory */
    vfs_error_t (*mkdir)(struct vfs_inode *dir, const char *name,
                         vfs_mode_t mode, struct vfs_inode **inode_out);

    /* Create symbolic link */
    vfs_error_t (*symlink)(struct vfs_inode *dir, const char *name,
                           const char *target);

    /* Read symbolic link */
    vfs_error_t (*readlink)(struct vfs_inode *inode, char *buf, size_t size);

    /* Hide entry (not delete!) */
    vfs_error_t (*hide)(struct vfs_inode *dir, const char *name);

    /* Get attributes */
    vfs_error_t (*getattr)(struct vfs_inode *inode, struct vfs_stat *stat);
};

/*
 * File operations
 */
struct vfs_file_operations {
    /* Open file */
    vfs_error_t (*open)(struct vfs_inode *inode, struct vfs_file *file);

    /* Close file */
    vfs_error_t (*close)(struct vfs_file *file);

    /* Read from file */
    ssize_t (*read)(struct vfs_file *file, void *buf, size_t count);

    /* Write to file (always append in Phantom) */
    ssize_t (*write)(struct vfs_file *file, const void *buf, size_t count);

    /* Seek */
    vfs_off_t (*seek)(struct vfs_file *file, vfs_off_t offset, int whence);

    /* Read directory entries */
    vfs_error_t (*readdir)(struct vfs_file *file,
                           void (*callback)(const char *name,
                                           vfs_ino_t ino,
                                           vfs_file_type_t type,
                                           void *ctx),
                           void *ctx);

    /* Sync to storage */
    vfs_error_t (*sync)(struct vfs_file *file);

    /* I/O control */
    vfs_error_t (*ioctl)(struct vfs_file *file, uint32_t cmd, void *arg);
};

/*
 * File status
 */
struct vfs_stat {
    vfs_ino_t           ino;
    vfs_file_type_t     type;
    vfs_mode_t          mode;
    uint32_t            nlink;
    uint64_t            size;
    uint64_t            blocks;
    phantom_time_t      created;
    phantom_time_t      modified;
    phantom_time_t      accessed;
    phantom_pid_t       owner_pid;
};

/*
 * Directory entry (for readdir)
 */
struct vfs_dirent {
    vfs_ino_t           ino;
    vfs_file_type_t     type;
    char                name[VFS_MAX_NAME + 1];
};

/*
 * VFS context - per-kernel VFS state
 */
struct vfs_context {
    struct vfs_fs_type *fs_types;       /* Registered file systems */
    struct vfs_mount   *mounts;         /* Mount table */
    struct vfs_dentry  *root;           /* Root dentry */
    struct vfs_file    *open_files[VFS_MAX_OPEN_FILES];

    /* Statistics */
    uint64_t            total_opens;
    uint64_t            total_reads;
    uint64_t            total_writes;
    uint64_t            total_bytes_read;
    uint64_t            total_bytes_written;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * VFS API
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Initialization */
vfs_error_t vfs_init(struct vfs_context *ctx);
void vfs_shutdown(struct vfs_context *ctx);

/* File system registration */
vfs_error_t vfs_register_fs(struct vfs_context *ctx, struct vfs_fs_type *fs_type);

/* Mounting */
vfs_error_t vfs_mount(struct vfs_context *ctx,
                      const char *fs_type,
                      const char *device,
                      const char *mount_path,
                      uint32_t flags);

/* Note: No vfs_unmount that destroys - only vfs_sync_unmount that preserves */
vfs_error_t vfs_sync_unmount(struct vfs_context *ctx, const char *mount_path);

/* Path resolution */
vfs_error_t vfs_resolve_path(struct vfs_context *ctx,
                              const char *path,
                              struct vfs_dentry **dentry_out);

/* File operations */
vfs_fd_t vfs_open(struct vfs_context *ctx, phantom_pid_t pid,
                  const char *path, uint32_t flags, vfs_mode_t mode);

vfs_error_t vfs_close(struct vfs_context *ctx, vfs_fd_t fd);

ssize_t vfs_read(struct vfs_context *ctx, vfs_fd_t fd,
                 void *buf, size_t count);

ssize_t vfs_write(struct vfs_context *ctx, vfs_fd_t fd,
                  const void *buf, size_t count);

vfs_off_t vfs_seek(struct vfs_context *ctx, vfs_fd_t fd,
                   vfs_off_t offset, int whence);

vfs_error_t vfs_sync(struct vfs_context *ctx, vfs_fd_t fd);

/* Directory operations */
vfs_error_t vfs_mkdir(struct vfs_context *ctx, phantom_pid_t pid,
                      const char *path, vfs_mode_t mode);

vfs_error_t vfs_readdir(struct vfs_context *ctx, vfs_fd_t fd,
                        struct vfs_dirent *entries, size_t max_entries,
                        size_t *count_out);

/* File information */
vfs_error_t vfs_stat(struct vfs_context *ctx, const char *path,
                     struct vfs_stat *stat_out);

vfs_error_t vfs_fstat(struct vfs_context *ctx, vfs_fd_t fd,
                      struct vfs_stat *stat_out);

/* Phantom-specific: hide (not delete!) */
vfs_error_t vfs_hide(struct vfs_context *ctx, phantom_pid_t pid,
                     const char *path);

/* Link operations (only creation, never removal) */
vfs_error_t vfs_symlink(struct vfs_context *ctx, phantom_pid_t pid,
                        const char *target, const char *link_path);

vfs_error_t vfs_readlink(struct vfs_context *ctx, const char *path,
                         char *buf, size_t size);

/* Copy and rename operations (create new, preserve old in geology) */
vfs_error_t vfs_copy(struct vfs_context *ctx, phantom_pid_t pid,
                     const char *src_path, const char *dst_path);

vfs_error_t vfs_rename(struct vfs_context *ctx, phantom_pid_t pid,
                       const char *old_path, const char *new_path);

/* File search - returns paths matching pattern */
typedef void (*vfs_search_callback_t)(const char *path, struct vfs_stat *stat, void *ctx);
vfs_error_t vfs_search(struct vfs_context *ctx, const char *start_path,
                       const char *pattern, vfs_search_callback_t callback, void *user_ctx);

/* Get file history from geology (returns views where file changed) */
typedef struct {
    uint64_t    view_id;
    char        view_label[64];
    uint64_t    timestamp;
    uint64_t    size;
    char        content_hash[65];  /* hex SHA-256 */
} vfs_file_version_t;

vfs_error_t vfs_get_history(struct vfs_context *ctx, const char *path,
                            vfs_file_version_t *versions, size_t max_versions,
                            size_t *count_out);

/* Restore file from a specific view (creates copy with old content) */
vfs_error_t vfs_restore_version(struct vfs_context *ctx, phantom_pid_t pid,
                                const char *path, uint64_t view_id,
                                const char *restore_path);

/* Utility */
const char *vfs_strerror(vfs_error_t err);

/* ══════════════════════════════════════════════════════════════════════════════
 * BUILT-IN FILE SYSTEMS
 * ══════════════════════════════════════════════════════════════════════════════ */

/* GeoFS adapter - wraps GeoFS for VFS */
extern struct vfs_fs_type geofs_fs_type;

/* procfs - process information */
extern struct vfs_fs_type procfs_fs_type;

/* devfs - device files */
extern struct vfs_fs_type devfs_fs_type;

/* tmpfs - temporary storage (still append-only, cleared on reboot) */
extern struct vfs_fs_type tmpfs_fs_type;

#endif /* PHANTOM_VFS_H */
