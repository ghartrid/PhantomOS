/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM PACKAGE MANAGER
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * A package manager that embodies the Phantom philosophy:
 * - Packages are NEVER uninstalled, only archived
 * - All package installations are logged to geology
 * - Multiple versions can coexist (no overwrites)
 * - Governor verifies package signatures before installation
 *
 * Key Principles:
 * 1. PRESERVATION: Old versions are never deleted, only superseded
 * 2. ACCOUNTABILITY: All installations tracked with who/when/why
 * 3. REVERSIBILITY: Can always revert to previous versions
 * 4. VERIFICATION: Governor checks all packages before installation
 */

#ifndef PHANTOM_PKG_H
#define PHANTOM_PKG_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "phantom.h"
#include "governor.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────── */

#define PHANTOM_MAX_PACKAGES        512
#define PHANTOM_MAX_PKG_NAME        64
#define PHANTOM_MAX_PKG_VERSION     32
#define PHANTOM_MAX_PKG_DESC        256
#define PHANTOM_MAX_PKG_DEPS        32
#define PHANTOM_PKG_PATH            "/pkg"
#define PHANTOM_PKG_ARCHIVE_PATH    "/pkg/.archive"

/* ─────────────────────────────────────────────────────────────────────────────
 * Package States
 *
 * Packages are never truly uninstalled - they become archived.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PKG_STATE_AVAILABLE = 0,    /* In repository, not installed */
    PKG_STATE_INSTALLED,        /* Currently active */
    PKG_STATE_ARCHIVED,         /* "Uninstalled" - still preserved */
    PKG_STATE_SUPERSEDED,       /* Replaced by newer version */
    PKG_STATE_BROKEN,           /* Dependencies missing */
} phantom_pkg_state_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Package Types
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PKG_TYPE_BINARY = 0,        /* Executable program */
    PKG_TYPE_LIBRARY,           /* Shared library */
    PKG_TYPE_SERVICE,           /* System service */
    PKG_TYPE_DATA,              /* Data files */
    PKG_TYPE_CONFIG,            /* Configuration */
    PKG_TYPE_DOCS,              /* Documentation */
    PKG_TYPE_META,              /* Meta-package (just dependencies) */
} phantom_pkg_type_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Package Structure
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_package {
    char name[PHANTOM_MAX_PKG_NAME];
    char version[PHANTOM_MAX_PKG_VERSION];
    char description[PHANTOM_MAX_PKG_DESC];
    char author[128];
    char license[64];

    phantom_pkg_type_t type;
    phantom_pkg_state_t state;

    /* Version info */
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    uint64_t version_number;            /* For comparison */

    /* Dependencies */
    char dependencies[PHANTOM_MAX_PKG_DEPS][PHANTOM_MAX_PKG_NAME];
    int dependency_count;

    /* Capabilities required */
    uint32_t required_caps;

    /* Installation info */
    time_t installed_at;
    time_t archived_at;
    uint32_t installed_by_uid;
    char install_reason[128];

    /* Storage */
    char install_path[256];             /* Where files are installed */
    uint64_t installed_size;            /* Total size in bytes */
    uint32_t file_count;                /* Number of files */

    /* Verification */
    phantom_hash_t package_hash;        /* Hash of package contents */
    phantom_signature_t signature;      /* Governor approval signature */
    int is_verified;

    /* History tracking */
    uint32_t install_count;             /* Times this exact version installed */
    char previous_version[PHANTOM_MAX_PKG_VERSION];  /* Version it replaced */

} phantom_package_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Package Manager Context
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_pkg_manager {
    /* Package database */
    phantom_package_t packages[PHANTOM_MAX_PACKAGES];
    int package_count;

    /* Statistics */
    uint64_t total_installed;
    uint64_t total_archived;
    uint64_t total_bytes_installed;

    /* Configuration */
    char pkg_root[256];                 /* Package installation root */
    char archive_root[256];             /* Archive location */
    int verify_signatures;              /* Require Governor verification */
    int auto_archive;                   /* Auto-archive old versions */

    /* References */
    struct phantom_kernel *kernel;
    struct phantom_governor *governor;

    /* State */
    int initialized;

} phantom_pkg_manager_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Result Codes
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PKG_OK = 0,
    PKG_ERR_INVALID = -1,
    PKG_ERR_NOT_FOUND = -2,
    PKG_ERR_EXISTS = -3,
    PKG_ERR_DENIED = -4,
    PKG_ERR_DEPS = -5,              /* Missing dependencies */
    PKG_ERR_VERIFY = -6,            /* Verification failed */
    PKG_ERR_IO = -7,
    PKG_ERR_FULL = -8,
    PKG_ERR_ARCHIVED = -9,
} phantom_pkg_result_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Package Manager API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Lifecycle */
int phantom_pkg_init(phantom_pkg_manager_t *pm, struct phantom_kernel *kernel);
void phantom_pkg_shutdown(phantom_pkg_manager_t *pm);

/* Configuration */
void phantom_pkg_set_governor(phantom_pkg_manager_t *pm, struct phantom_governor *gov);
void phantom_pkg_set_root(phantom_pkg_manager_t *pm, const char *root);
void phantom_pkg_set_verify(phantom_pkg_manager_t *pm, int enabled);

/* Package Operations */
int phantom_pkg_install(phantom_pkg_manager_t *pm, const char *name,
                        const char *version, uint32_t uid, const char *reason);
int phantom_pkg_archive(phantom_pkg_manager_t *pm, const char *name,
                        uint32_t uid, const char *reason);
int phantom_pkg_restore(phantom_pkg_manager_t *pm, const char *name,
                        const char *version, uint32_t uid);

/* Package Query */
phantom_package_t *phantom_pkg_find(phantom_pkg_manager_t *pm, const char *name);
phantom_package_t *phantom_pkg_find_version(phantom_pkg_manager_t *pm,
                                             const char *name, const char *version);
int phantom_pkg_is_installed(phantom_pkg_manager_t *pm, const char *name);
int phantom_pkg_list_installed(phantom_pkg_manager_t *pm,
                               phantom_package_t **list, int max_count);
int phantom_pkg_list_archived(phantom_pkg_manager_t *pm,
                              phantom_package_t **list, int max_count);

/* Dependency Management */
int phantom_pkg_check_deps(phantom_pkg_manager_t *pm, phantom_package_t *pkg);
int phantom_pkg_get_dependents(phantom_pkg_manager_t *pm, const char *name,
                               phantom_package_t **list, int max_count);

/* Verification */
int phantom_pkg_verify(phantom_pkg_manager_t *pm, phantom_package_t *pkg);

/* Information */
void phantom_pkg_print_info(phantom_package_t *pkg);
void phantom_pkg_print_stats(phantom_pkg_manager_t *pm);

/* Utility */
const char *phantom_pkg_state_string(phantom_pkg_state_t state);
const char *phantom_pkg_type_string(phantom_pkg_type_t type);
const char *phantom_pkg_result_string(phantom_pkg_result_t result);
int phantom_pkg_parse_version(const char *version_str,
                               uint32_t *major, uint32_t *minor, uint32_t *patch);
int phantom_pkg_compare_versions(const char *v1, const char *v2);

/* Built-in Packages */
int phantom_pkg_register_builtin(phantom_pkg_manager_t *pm);

#endif /* PHANTOM_PKG_H */
