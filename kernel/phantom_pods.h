/*
 * ============================================================================
 *                            PHANTOM PODS
 *                    "Compatibility Without Compromise"
 * ============================================================================
 *
 * PhantomPods is the compatibility layer system for PhantomOS. It provides
 * isolated containers for running external applications from various
 * environments (Linux, Windows via Wine, legacy systems, etc.)
 *
 * Key Principles:
 * - Pods are never destroyed, only made dormant (Phantom philosophy)
 * - Each pod has its own GeoFS layer for persistent state
 * - Governor integration for security and capability control
 * - Seamless integration with PhantomOS services
 *
 * Pod Types:
 * - Native: Run Linux binaries directly with isolation
 * - Wine: Run Windows applications via Wine compatibility
 * - Legacy: Emulation layer for older systems
 * - Custom: User-defined compatibility environments
 */

#ifndef PHANTOM_PODS_H
#define PHANTOM_PODS_H

#include <stdint.h>
#include <time.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod Constants
 * ───────────────────────────────────────────────────────────────────────────── */

#define PHANTOM_POD_MAX_NAME        64
#define PHANTOM_POD_MAX_DESC        256
#define PHANTOM_POD_MAX_PATH        512
#define PHANTOM_POD_MAX_APPS        32
#define PHANTOM_POD_MAX_PODS        64
#define PHANTOM_POD_MAX_ENV_VARS    64
#define PHANTOM_POD_MAX_MOUNTS      16

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod Types - Compatibility Modes
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    POD_TYPE_NATIVE,        /* Native Linux binaries with isolation */
    POD_TYPE_WINE,          /* Windows apps via Wine */
    POD_TYPE_WINE64,        /* 64-bit Windows apps via Wine */
    POD_TYPE_DOSBOX,        /* DOS applications via DOSBox */
    POD_TYPE_QEMU,          /* Full system emulation */
    POD_TYPE_FLATPAK,       /* Flatpak container integration */
    POD_TYPE_APPIMAGE,      /* AppImage support */
    POD_TYPE_CUSTOM         /* User-defined environment */
} phantom_pod_type_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod States (Following Phantom Philosophy)
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    POD_STATE_MANIFESTING,  /* Pod being created/configured */
    POD_STATE_READY,        /* Configured but not running */
    POD_STATE_ACTIVE,       /* Currently running */
    POD_STATE_DORMANT,      /* Suspended (can resume instantly) */
    POD_STATE_ARCHIVED,     /* Preserved in geology, inactive */
    POD_STATE_MIGRATING     /* Being transferred or updated */
} phantom_pod_state_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod Security Level
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    POD_SECURITY_MAXIMUM,   /* No network, no host filesystem access */
    POD_SECURITY_HIGH,      /* Limited network, read-only host access */
    POD_SECURITY_STANDARD,  /* Controlled access to resources */
    POD_SECURITY_RELAXED,   /* Broader access for trusted apps */
    POD_SECURITY_CUSTOM     /* User-defined security policy */
} phantom_pod_security_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod Resource Limits
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t cpu_percent;       /* Max CPU usage (1-100) */
    uint64_t memory_mb;         /* Max memory in MB */
    uint64_t storage_mb;        /* Max storage in MB */
    uint32_t network_kbps;      /* Network bandwidth limit (0 = none) */
    int      allow_gpu;         /* GPU access allowed */
    int      allow_audio;       /* Audio access allowed */
    int      allow_usb;         /* USB device access */
    int      allow_display;     /* Display/GUI access */
} phantom_pod_limits_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod Mount Point
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    char host_path[PHANTOM_POD_MAX_PATH];   /* Path on host/GeoFS */
    char pod_path[PHANTOM_POD_MAX_PATH];    /* Path inside pod */
    int  read_only;                          /* Mount as read-only */
    int  geology_backed;                     /* Use GeoFS for versioning */
} phantom_pod_mount_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod Application Entry
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    char name[PHANTOM_POD_MAX_NAME];        /* Display name */
    char executable[PHANTOM_POD_MAX_PATH];  /* Path to executable */
    char arguments[PHANTOM_POD_MAX_PATH];   /* Command line arguments */
    char icon[64];                          /* Icon (emoji or path) */
    char working_dir[PHANTOM_POD_MAX_PATH]; /* Working directory */
    int  installed;                         /* Successfully installed */
    time_t last_run;                        /* Last execution time */
    uint64_t run_count;                     /* Total run count */
} phantom_pod_app_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod Environment Variable
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    char name[128];
    char value[256];
} phantom_pod_env_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * PhantomPod Structure
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_pod {
    /* Identity */
    uint32_t id;                            /* Unique pod ID */
    char name[PHANTOM_POD_MAX_NAME];        /* Pod name */
    char description[PHANTOM_POD_MAX_DESC]; /* Pod description */
    char icon[64];                          /* Pod icon (emoji) */

    /* Type and State */
    phantom_pod_type_t type;                /* Compatibility mode */
    phantom_pod_state_t state;              /* Current state */
    phantom_pod_security_t security;        /* Security level */

    /* Configuration */
    phantom_pod_limits_t limits;            /* Resource limits */
    phantom_pod_mount_t mounts[PHANTOM_POD_MAX_MOUNTS];
    int mount_count;
    phantom_pod_env_t env_vars[PHANTOM_POD_MAX_ENV_VARS];
    int env_count;

    /* Applications */
    phantom_pod_app_t apps[PHANTOM_POD_MAX_APPS];
    int app_count;

    /* Runtime */
    pid_t pid;                              /* Main process PID (0 if dormant) */
    time_t created;                         /* Creation timestamp */
    time_t last_active;                     /* Last activity timestamp */
    uint64_t total_runtime_secs;            /* Total runtime in seconds */

    /* GeoFS Integration */
    char geology_layer[PHANTOM_POD_MAX_PATH]; /* Pod's geology layer path */
    uint64_t geology_size;                  /* Current geology size */

    /* Governor Integration */
    uint32_t governor_policy_id;            /* Associated governor policy */
    int governor_approved;                  /* Approved by governor */

} phantom_pod_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod System Context
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_pod_system {
    phantom_pod_t pods[PHANTOM_POD_MAX_PODS];
    int pod_count;
    uint32_t next_pod_id;

    /* System paths */
    char pods_root[PHANTOM_POD_MAX_PATH];   /* Root directory for pods */
    char templates_path[PHANTOM_POD_MAX_PATH]; /* Pod templates */

    /* Compatibility layer status */
    int wine_available;
    int wine64_available;
    int dosbox_available;
    int flatpak_available;

    /* Statistics */
    uint64_t total_pods_created;
    uint64_t total_apps_run;
    uint64_t total_runtime_secs;

} phantom_pod_system_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod Templates (Pre-configured environments)
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    const char *description;
    const char *icon;
    phantom_pod_type_t type;
    phantom_pod_security_t security;
    phantom_pod_limits_t default_limits;
} phantom_pod_template_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod System API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Initialization */
int phantom_pods_init(phantom_pod_system_t *system, const char *pods_root);
void phantom_pods_shutdown(phantom_pod_system_t *system);

/* Pod Lifecycle */
phantom_pod_t *phantom_pod_create(phantom_pod_system_t *system,
                                   const char *name,
                                   phantom_pod_type_t type);
phantom_pod_t *phantom_pod_create_from_template(phantom_pod_system_t *system,
                                                 const char *name,
                                                 const phantom_pod_template_t *tmpl);
int phantom_pod_activate(phantom_pod_system_t *system, phantom_pod_t *pod);
int phantom_pod_make_dormant(phantom_pod_system_t *system, phantom_pod_t *pod);
int phantom_pod_archive(phantom_pod_system_t *system, phantom_pod_t *pod);
int phantom_pod_restore(phantom_pod_system_t *system, phantom_pod_t *pod);

/* Pod Configuration */
int phantom_pod_set_limits(phantom_pod_t *pod, const phantom_pod_limits_t *limits);
int phantom_pod_add_mount(phantom_pod_t *pod, const char *host_path,
                          const char *pod_path, int read_only);
int phantom_pod_add_env(phantom_pod_t *pod, const char *name, const char *value);
int phantom_pod_set_security(phantom_pod_t *pod, phantom_pod_security_t level);

/* Application Management */
int phantom_pod_install_app(phantom_pod_t *pod, const char *name,
                            const char *executable, const char *icon);
int phantom_pod_run_app(phantom_pod_system_t *system, phantom_pod_t *pod,
                        phantom_pod_app_t *app);
int phantom_pod_import_executable(phantom_pod_t *pod, const char *host_path);

/* Query */
phantom_pod_t *phantom_pod_find_by_id(phantom_pod_system_t *system, uint32_t id);
phantom_pod_t *phantom_pod_find_by_name(phantom_pod_system_t *system, const char *name);
int phantom_pod_get_active_count(phantom_pod_system_t *system);

/* Compatibility Detection */
int phantom_pods_detect_compatibility(phantom_pod_system_t *system);
const char *phantom_pod_type_name(phantom_pod_type_t type);
const char *phantom_pod_state_name(phantom_pod_state_t state);
const char *phantom_pod_security_name(phantom_pod_security_t security);

/* Templates */
const phantom_pod_template_t *phantom_pod_get_templates(int *count);

#endif /* PHANTOM_PODS_H */
