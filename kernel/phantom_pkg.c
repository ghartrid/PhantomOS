/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM PACKAGE MANAGER
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Implementation of the Phantom package manager.
 * Packages are never uninstalled - they become archived.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "phantom_pkg.h"
#include "phantom.h"
#include "governor.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

static uint64_t version_to_number(uint32_t major, uint32_t minor, uint32_t patch) {
    return ((uint64_t)major << 32) | ((uint64_t)minor << 16) | patch;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Version Parsing
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_pkg_parse_version(const char *version_str,
                               uint32_t *major, uint32_t *minor, uint32_t *patch) {
    if (!version_str) return -1;

    *major = *minor = *patch = 0;

    int parsed = sscanf(version_str, "%u.%u.%u", major, minor, patch);
    return (parsed >= 1) ? 0 : -1;
}

int phantom_pkg_compare_versions(const char *v1, const char *v2) {
    uint32_t maj1 = 0, min1 = 0, pat1 = 0, maj2 = 0, min2 = 0, pat2 = 0;

    phantom_pkg_parse_version(v1, &maj1, &min1, &pat1);
    phantom_pkg_parse_version(v2, &maj2, &min2, &pat2);

    uint64_t n1 = version_to_number(maj1, min1, pat1);
    uint64_t n2 = version_to_number(maj2, min2, pat2);

    if (n1 < n2) return -1;
    if (n1 > n2) return 1;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Lifecycle
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_pkg_init(phantom_pkg_manager_t *pm, struct phantom_kernel *kernel) {
    if (!pm) return -1;

    memset(pm, 0, sizeof(phantom_pkg_manager_t));

    pm->kernel = kernel;
    strncpy(pm->pkg_root, PHANTOM_PKG_PATH, sizeof(pm->pkg_root) - 1);
    strncpy(pm->archive_root, PHANTOM_PKG_ARCHIVE_PATH, sizeof(pm->archive_root) - 1);
    pm->verify_signatures = 1;
    pm->auto_archive = 1;

    pm->initialized = 1;

    /* Register built-in packages */
    phantom_pkg_register_builtin(pm);

    printf("[phantom_pkg] Package manager initialized\n");
    printf("              Packages are never uninstalled, only archived\n");
    printf("              Root: %s, Archive: %s\n", pm->pkg_root, pm->archive_root);

    return 0;
}

void phantom_pkg_shutdown(phantom_pkg_manager_t *pm) {
    if (!pm || !pm->initialized) return;

    printf("[phantom_pkg] Package manager shutting down...\n");
    printf("              %d packages, %lu installed, %lu archived\n",
           pm->package_count, pm->total_installed, pm->total_archived);

    pm->initialized = 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Configuration
 * ───────────────────────────────────────────────────────────────────────────── */

void phantom_pkg_set_governor(phantom_pkg_manager_t *pm, struct phantom_governor *gov) {
    if (pm) pm->governor = gov;
}

void phantom_pkg_set_root(phantom_pkg_manager_t *pm, const char *root) {
    if (pm && root) {
        strncpy(pm->pkg_root, root, sizeof(pm->pkg_root) - 1);
    }
}

void phantom_pkg_set_verify(phantom_pkg_manager_t *pm, int enabled) {
    if (pm) pm->verify_signatures = enabled;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Package Operations
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_pkg_install(phantom_pkg_manager_t *pm, const char *name,
                        const char *version, uint32_t uid, const char *reason) {
    if (!pm || !name) return PKG_ERR_INVALID;

    /* Check if already installed */
    phantom_package_t *existing = phantom_pkg_find(pm, name);
    if (existing && existing->state == PKG_STATE_INSTALLED) {
        /* Check version */
        if (!version || strcmp(existing->version, version) == 0) {
            printf("[phantom_pkg] Package '%s' already installed\n", name);
            return PKG_ERR_EXISTS;
        }

        /* Archive existing version if auto-archive enabled */
        if (pm->auto_archive) {
            printf("[phantom_pkg] Archiving existing version %s\n", existing->version);
            strncpy(existing->previous_version, existing->version,
                    PHANTOM_MAX_PKG_VERSION - 1);
            existing->state = PKG_STATE_SUPERSEDED;
            existing->archived_at = time(NULL);
            pm->total_archived++;
        }
    }

    /* Find available slot */
    if (pm->package_count >= PHANTOM_MAX_PACKAGES) {
        return PKG_ERR_FULL;
    }

    phantom_package_t *pkg = &pm->packages[pm->package_count];
    memset(pkg, 0, sizeof(phantom_package_t));

    strncpy(pkg->name, name, PHANTOM_MAX_PKG_NAME - 1);
    strncpy(pkg->version, version ? version : "1.0.0", PHANTOM_MAX_PKG_VERSION - 1);

    phantom_pkg_parse_version(pkg->version,
                               &pkg->version_major, &pkg->version_minor, &pkg->version_patch);
    pkg->version_number = version_to_number(pkg->version_major,
                                             pkg->version_minor, pkg->version_patch);

    pkg->state = PKG_STATE_INSTALLED;
    pkg->installed_at = time(NULL);
    pkg->installed_by_uid = uid;
    if (reason) {
        strncpy(pkg->install_reason, reason, sizeof(pkg->install_reason) - 1);
    }

    snprintf(pkg->install_path, sizeof(pkg->install_path),
             "%.100s/%.100s/%.50s", pm->pkg_root, name, pkg->version);

    pkg->install_count = 1;

    pm->package_count++;
    pm->total_installed++;

    printf("[phantom_pkg] Installed '%s' version %s by uid=%u\n",
           name, pkg->version, uid);
    if (reason) {
        printf("              Reason: %s\n", reason);
    }

    return PKG_OK;
}

int phantom_pkg_archive(phantom_pkg_manager_t *pm, const char *name,
                        uint32_t uid, const char *reason) {
    if (!pm || !name) return PKG_ERR_INVALID;

    phantom_package_t *pkg = phantom_pkg_find(pm, name);
    if (!pkg) return PKG_ERR_NOT_FOUND;

    if (pkg->state != PKG_STATE_INSTALLED) {
        printf("[phantom_pkg] Package '%s' is not currently installed\n", name);
        return PKG_ERR_ARCHIVED;
    }

    /* Check for dependents */
    phantom_package_t *dependents[32];
    int dep_count = phantom_pkg_get_dependents(pm, name, dependents, 32);
    if (dep_count > 0) {
        printf("[phantom_pkg] Cannot archive '%s': %d packages depend on it\n",
               name, dep_count);
        for (int i = 0; i < dep_count && i < 5; i++) {
            printf("              - %s\n", dependents[i]->name);
        }
        return PKG_ERR_DEPS;
    }

    pkg->state = PKG_STATE_ARCHIVED;
    pkg->archived_at = time(NULL);
    pm->total_archived++;

    printf("[phantom_pkg] Archived '%s' version %s by uid=%u\n",
           name, pkg->version, uid);
    printf("              Package preserved at: %s\n", pkg->install_path);
    if (reason) {
        printf("              Reason: %s\n", reason);
    }

    return PKG_OK;
}

int phantom_pkg_restore(phantom_pkg_manager_t *pm, const char *name,
                        const char *version, uint32_t uid) {
    if (!pm || !name) return PKG_ERR_INVALID;

    phantom_package_t *pkg = NULL;

    if (version) {
        pkg = phantom_pkg_find_version(pm, name, version);
    } else {
        /* Find most recent archived version */
        for (int i = 0; i < pm->package_count; i++) {
            if (strcmp(pm->packages[i].name, name) == 0 &&
                pm->packages[i].state == PKG_STATE_ARCHIVED) {
                if (!pkg || pm->packages[i].archived_at > pkg->archived_at) {
                    pkg = &pm->packages[i];
                }
            }
        }
    }

    if (!pkg) return PKG_ERR_NOT_FOUND;

    if (pkg->state != PKG_STATE_ARCHIVED && pkg->state != PKG_STATE_SUPERSEDED) {
        printf("[phantom_pkg] Package '%s' is not archived\n", name);
        return PKG_ERR_INVALID;
    }

    /* Archive current installed version if exists */
    phantom_package_t *current = phantom_pkg_find(pm, name);
    if (current && current->state == PKG_STATE_INSTALLED && current != pkg) {
        current->state = PKG_STATE_SUPERSEDED;
        current->archived_at = time(NULL);
    }

    pkg->state = PKG_STATE_INSTALLED;
    pkg->install_count++;

    printf("[phantom_pkg] Restored '%s' version %s by uid=%u\n",
           name, pkg->version, uid);

    return PKG_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Package Query
 * ───────────────────────────────────────────────────────────────────────────── */

phantom_package_t *phantom_pkg_find(phantom_pkg_manager_t *pm, const char *name) {
    if (!pm || !name) return NULL;

    /* Find installed version first */
    for (int i = 0; i < pm->package_count; i++) {
        if (strcmp(pm->packages[i].name, name) == 0 &&
            pm->packages[i].state == PKG_STATE_INSTALLED) {
            return &pm->packages[i];
        }
    }

    /* Return any version if not found */
    for (int i = 0; i < pm->package_count; i++) {
        if (strcmp(pm->packages[i].name, name) == 0) {
            return &pm->packages[i];
        }
    }

    return NULL;
}

phantom_package_t *phantom_pkg_find_version(phantom_pkg_manager_t *pm,
                                             const char *name, const char *version) {
    if (!pm || !name || !version) return NULL;

    for (int i = 0; i < pm->package_count; i++) {
        if (strcmp(pm->packages[i].name, name) == 0 &&
            strcmp(pm->packages[i].version, version) == 0) {
            return &pm->packages[i];
        }
    }

    return NULL;
}

int phantom_pkg_is_installed(phantom_pkg_manager_t *pm, const char *name) {
    phantom_package_t *pkg = phantom_pkg_find(pm, name);
    return (pkg && pkg->state == PKG_STATE_INSTALLED) ? 1 : 0;
}

int phantom_pkg_list_installed(phantom_pkg_manager_t *pm,
                               phantom_package_t **list, int max_count) {
    if (!pm || !list || max_count <= 0) return 0;

    int count = 0;
    for (int i = 0; i < pm->package_count && count < max_count; i++) {
        if (pm->packages[i].state == PKG_STATE_INSTALLED) {
            list[count++] = &pm->packages[i];
        }
    }

    return count;
}

int phantom_pkg_list_archived(phantom_pkg_manager_t *pm,
                              phantom_package_t **list, int max_count) {
    if (!pm || !list || max_count <= 0) return 0;

    int count = 0;
    for (int i = 0; i < pm->package_count && count < max_count; i++) {
        if (pm->packages[i].state == PKG_STATE_ARCHIVED ||
            pm->packages[i].state == PKG_STATE_SUPERSEDED) {
            list[count++] = &pm->packages[i];
        }
    }

    return count;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Dependency Management
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_pkg_check_deps(phantom_pkg_manager_t *pm, phantom_package_t *pkg) {
    if (!pm || !pkg) return -1;

    int missing = 0;
    for (int i = 0; i < pkg->dependency_count; i++) {
        if (!phantom_pkg_is_installed(pm, pkg->dependencies[i])) {
            printf("[phantom_pkg] Missing dependency: %s\n", pkg->dependencies[i]);
            missing++;
        }
    }

    return missing;
}

int phantom_pkg_get_dependents(phantom_pkg_manager_t *pm, const char *name,
                               phantom_package_t **list, int max_count) {
    if (!pm || !name || !list || max_count <= 0) return 0;

    int count = 0;
    for (int i = 0; i < pm->package_count && count < max_count; i++) {
        phantom_package_t *pkg = &pm->packages[i];
        if (pkg->state != PKG_STATE_INSTALLED) continue;

        for (int j = 0; j < pkg->dependency_count; j++) {
            if (strcmp(pkg->dependencies[j], name) == 0) {
                list[count++] = pkg;
                break;
            }
        }
    }

    return count;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Verification
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_pkg_verify(phantom_pkg_manager_t *pm, phantom_package_t *pkg) {
    if (!pm || !pkg) return -1;

    /* In a real implementation, verify package hash and signature */
    pkg->is_verified = 1;

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Information
 * ───────────────────────────────────────────────────────────────────────────── */

void phantom_pkg_print_info(phantom_package_t *pkg) {
    if (!pkg) return;

    printf("Package: %s\n", pkg->name);
    printf("  Version:     %s\n", pkg->version);
    printf("  State:       %s\n", phantom_pkg_state_string(pkg->state));
    printf("  Type:        %s\n", phantom_pkg_type_string(pkg->type));
    if (pkg->description[0]) {
        printf("  Description: %s\n", pkg->description);
    }
    if (pkg->author[0]) {
        printf("  Author:      %s\n", pkg->author);
    }
    printf("  Path:        %s\n", pkg->install_path);

    if (pkg->installed_at > 0) {
        char time_buf[64];
        struct tm *tm = localtime(&pkg->installed_at);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);
        printf("  Installed:   %s (by uid %u)\n", time_buf, pkg->installed_by_uid);
    }

    if (pkg->archived_at > 0) {
        char time_buf[64];
        struct tm *tm = localtime(&pkg->archived_at);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);
        printf("  Archived:    %s\n", time_buf);
    }

    if (pkg->dependency_count > 0) {
        printf("  Dependencies: ");
        for (int i = 0; i < pkg->dependency_count; i++) {
            printf("%s%s", pkg->dependencies[i],
                   i < pkg->dependency_count - 1 ? ", " : "\n");
        }
    }
}

void phantom_pkg_print_stats(phantom_pkg_manager_t *pm) {
    if (!pm) return;

    int installed = 0, archived = 0, superseded = 0;
    for (int i = 0; i < pm->package_count; i++) {
        switch (pm->packages[i].state) {
            case PKG_STATE_INSTALLED: installed++; break;
            case PKG_STATE_ARCHIVED: archived++; break;
            case PKG_STATE_SUPERSEDED: superseded++; break;
            default: break;
        }
    }

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                  PACKAGE MANAGER STATISTICS                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Total packages:     %d\n", pm->package_count);
    printf("  Installed:          %d\n", installed);
    printf("  Archived:           %d\n", archived);
    printf("  Superseded:         %d\n", superseded);
    printf("\n");
    printf("  Lifetime installs:  %lu\n", pm->total_installed);
    printf("  Lifetime archives:  %lu\n", pm->total_archived);
    printf("\n");
    printf("  Package root:       %s\n", pm->pkg_root);
    printf("  Archive root:       %s\n", pm->archive_root);
    printf("  Signature verify:   %s\n", pm->verify_signatures ? "enabled" : "disabled");
    printf("\n");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility
 * ───────────────────────────────────────────────────────────────────────────── */

const char *phantom_pkg_state_string(phantom_pkg_state_t state) {
    switch (state) {
        case PKG_STATE_AVAILABLE:   return "available";
        case PKG_STATE_INSTALLED:   return "installed";
        case PKG_STATE_ARCHIVED:    return "archived";
        case PKG_STATE_SUPERSEDED:  return "superseded";
        case PKG_STATE_BROKEN:      return "broken";
        default:                    return "unknown";
    }
}

const char *phantom_pkg_type_string(phantom_pkg_type_t type) {
    switch (type) {
        case PKG_TYPE_BINARY:   return "binary";
        case PKG_TYPE_LIBRARY:  return "library";
        case PKG_TYPE_SERVICE:  return "service";
        case PKG_TYPE_DATA:     return "data";
        case PKG_TYPE_CONFIG:   return "config";
        case PKG_TYPE_DOCS:     return "docs";
        case PKG_TYPE_META:     return "meta";
        default:                return "unknown";
    }
}

const char *phantom_pkg_result_string(phantom_pkg_result_t result) {
    switch (result) {
        case PKG_OK:            return "success";
        case PKG_ERR_INVALID:   return "invalid parameter";
        case PKG_ERR_NOT_FOUND: return "package not found";
        case PKG_ERR_EXISTS:    return "already installed";
        case PKG_ERR_DENIED:    return "permission denied";
        case PKG_ERR_DEPS:      return "dependency error";
        case PKG_ERR_VERIFY:    return "verification failed";
        case PKG_ERR_IO:        return "I/O error";
        case PKG_ERR_FULL:      return "package limit reached";
        case PKG_ERR_ARCHIVED:  return "package is archived";
        default:                return "unknown error";
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Built-in Packages
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_pkg_register_builtin(phantom_pkg_manager_t *pm) {
    if (!pm) return -1;

    /* Register PhantomOS core components as packages */
    struct {
        const char *name;
        const char *version;
        const char *desc;
        phantom_pkg_type_t type;
    } builtins[] = {
        {"phantom-kernel", "1.0.0", "PhantomOS Kernel", PKG_TYPE_BINARY},
        {"phantom-shell", "1.0.0", "Phantom Shell", PKG_TYPE_BINARY},
        {"phantom-vfs", "1.0.0", "Virtual File System", PKG_TYPE_LIBRARY},
        {"phantom-geofs", "1.0.0", "Geology File System", PKG_TYPE_LIBRARY},
        {"phantom-governor", "1.0.0", "AI Governor", PKG_TYPE_SERVICE},
        {"phantom-init", "1.0.0", "Init System", PKG_TYPE_SERVICE},
        {"phantom-ai", "1.0.0", "AI Assistant", PKG_TYPE_SERVICE},
        {"phantom-net", "1.0.0", "Network Layer", PKG_TYPE_LIBRARY},
        {"phantom-gui", "1.0.0", "Graphical Interface", PKG_TYPE_BINARY},
        {NULL, NULL, NULL, 0}
    };

    for (int i = 0; builtins[i].name != NULL; i++) {
        phantom_package_t *pkg = &pm->packages[pm->package_count];
        memset(pkg, 0, sizeof(phantom_package_t));

        strncpy(pkg->name, builtins[i].name, PHANTOM_MAX_PKG_NAME - 1);
        strncpy(pkg->version, builtins[i].version, PHANTOM_MAX_PKG_VERSION - 1);
        strncpy(pkg->description, builtins[i].desc, PHANTOM_MAX_PKG_DESC - 1);
        strncpy(pkg->author, "PhantomOS Team", 127);
        strncpy(pkg->license, "Phantom License", 63);

        pkg->type = builtins[i].type;
        pkg->state = PKG_STATE_INSTALLED;
        pkg->is_verified = 1;

        phantom_pkg_parse_version(pkg->version,
                                   &pkg->version_major, &pkg->version_minor, &pkg->version_patch);
        pkg->version_number = version_to_number(pkg->version_major,
                                                 pkg->version_minor, pkg->version_patch);

        pkg->installed_at = time(NULL);
        pkg->installed_by_uid = 0;  /* System */
        strncpy(pkg->install_reason, "Built-in system component", 127);

        snprintf(pkg->install_path, sizeof(pkg->install_path),
                 "/phantom/lib/%s", builtins[i].name);

        pm->package_count++;
    }

    printf("[phantom_pkg] Registered %d built-in packages\n", pm->package_count);

    return 0;
}
