/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                              PHANTOM INIT SYSTEM
 *                        "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * The Phantom Init System manages services - eternal processes that serve the
 * system. In Phantom philosophy, services are never "killed" - they become
 * dormant and can be awakened. The init system monitors all services and
 * ensures system stability through creation, not destruction.
 *
 * Service definitions are stored in GeoFS at /geo/etc/init/, ensuring that
 * service history is preserved forever in the geological record.
 */

#ifndef PHANTOM_INIT_H
#define PHANTOM_INIT_H

#include "phantom.h"
#include "vfs.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Service States
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    SERVICE_EMBRYO,      /* Being created */
    SERVICE_STARTING,    /* Dependencies being resolved */
    SERVICE_RUNNING,     /* Active and healthy */
    SERVICE_DORMANT,     /* Inactive but preserved (never "stopped") */
    SERVICE_AWAKENING,   /* Transitioning from dormant to running */
    SERVICE_BLOCKED,     /* Waiting on resource */
} service_state_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Service Types
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    SERVICE_TYPE_SIMPLE,     /* Runs once, considered running while active */
    SERVICE_TYPE_DAEMON,     /* Long-running background service */
    SERVICE_TYPE_ONESHOT,    /* Runs once at startup, then dormant */
    SERVICE_TYPE_MONITOR,    /* Watchdog-style service */
} service_type_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Restart Policies (Phantom style - awakening, not restarting)
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    RESTART_NEVER,           /* Let service remain dormant */
    RESTART_ON_FAILURE,      /* Awaken only if unexpected dormancy */
    RESTART_ALWAYS,          /* Always awaken when dormant */
} restart_policy_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Service Definition
 * ───────────────────────────────────────────────────────────────────────────── */

#define SERVICE_NAME_MAX      64
#define SERVICE_DESC_MAX      256
#define SERVICE_CMD_MAX       512
#define SERVICE_DEPS_MAX      16
#define SERVICE_ENV_MAX       32

typedef struct phantom_service {
    /* Identity */
    char name[SERVICE_NAME_MAX];
    char description[SERVICE_DESC_MAX];
    uint64_t service_id;

    /* Configuration */
    service_type_t type;
    restart_policy_t restart_policy;
    char command[SERVICE_CMD_MAX];
    char working_dir[256];

    /* Dependencies (services that must be running first) */
    char dependencies[SERVICE_DEPS_MAX][SERVICE_NAME_MAX];
    int dependency_count;

    /* Environment variables */
    char env_vars[SERVICE_ENV_MAX][128];
    int env_count;

    /* Runtime state */
    service_state_t state;
    phantom_pid_t pid;               /* Associated process, if any */
    uint64_t start_count;            /* How many times awakened */
    uint64_t last_start_time;        /* Timestamp of last awakening */
    uint64_t last_dormant_time;      /* Timestamp of last dormancy */
    int exit_code;                   /* Last exit code (0 = graceful dormancy) */

    /* Monitoring */
    uint64_t cpu_time_total;         /* Total CPU time consumed */
    uint64_t memory_peak;            /* Peak memory usage */

    /* Geology tracking */
    uint8_t definition_hash[32];     /* GeoFS hash of service definition */
    uint64_t geology_version;        /* Version in geological record */

    /* Linked list */
    struct phantom_service *next;
} phantom_service_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Init System Context
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_init {
    struct phantom_kernel *kernel;
    struct vfs_context *vfs;

    /* Service registry */
    phantom_service_t *services;
    int service_count;
    uint64_t next_service_id;

    /* Configuration */
    char services_dir[256];          /* Where service definitions live */
    int auto_awaken;                 /* Auto-awaken dormant services? */
    int monitor_interval_ms;         /* How often to check services */

    /* State */
    int initialized;
    int running;
    uint64_t boot_time;

    /* Statistics */
    uint64_t total_awakenings;
    uint64_t total_dormancies;
    uint64_t uptime_seconds;

    /* Thread for monitoring */
    pthread_t monitor_thread;
    pthread_mutex_t lock;
} phantom_init_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Init System API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Lifecycle */
int init_create(phantom_init_t *init, struct phantom_kernel *kernel,
                struct vfs_context *vfs);
int init_start(phantom_init_t *init);
int init_shutdown(phantom_init_t *init);  /* Graceful dormancy for all services */

/* Service management */
phantom_service_t *init_service_create(phantom_init_t *init, const char *name);
int init_service_configure(phantom_service_t *svc, const char *key,
                           const char *value);
int init_service_register(phantom_init_t *init, phantom_service_t *svc);

/* Service control */
int init_service_awaken(phantom_init_t *init, const char *name);
int init_service_rest(phantom_init_t *init, const char *name);  /* Graceful dormancy */
int init_service_reload(phantom_init_t *init, const char *name);

/* Service queries */
phantom_service_t *init_service_find(phantom_init_t *init, const char *name);
service_state_t init_service_status(phantom_init_t *init, const char *name);
int init_service_list(phantom_init_t *init,
                      void (*callback)(phantom_service_t *svc, void *ctx),
                      void *ctx);

/* Service definitions (stored in GeoFS) */
int init_load_service_file(phantom_init_t *init, const char *path);
int init_save_service_file(phantom_init_t *init, phantom_service_t *svc);
int init_scan_services_dir(phantom_init_t *init);

/* Dependency resolution */
int init_check_dependencies(phantom_init_t *init, phantom_service_t *svc);
int init_resolve_boot_order(phantom_init_t *init, phantom_service_t ***order,
                            int *count);

/* Logging (to geology) */
void init_log(phantom_init_t *init, const char *service, const char *fmt, ...);

/* ─────────────────────────────────────────────────────────────────────────────
 * Built-in Services
 * ───────────────────────────────────────────────────────────────────────────── */

/* These are core services that init creates automatically */
int init_register_builtin_services(phantom_init_t *init);

/* Built-in service names */
#define SERVICE_GEOFS     "geofs"      /* Geology filesystem */
#define SERVICE_VFS       "vfs"        /* Virtual filesystem */
#define SERVICE_PROCFS    "procfs"     /* Process filesystem */
#define SERVICE_DEVFS     "devfs"      /* Device filesystem */
#define SERVICE_GOVERNOR  "governor"   /* AI code evaluator */
#define SERVICE_SHELL     "shell"      /* Interactive shell */

/* ─────────────────────────────────────────────────────────────────────────────
 * Service Definition File Format
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * Service definitions are simple key=value files stored in /geo/etc/init/
 *
 * Example: /geo/etc/init/myservice.svc
 *
 *   name=myservice
 *   description=My example service
 *   type=daemon
 *   command=/geo/bin/myservice --daemon
 *   restart=always
 *   depends=geofs,vfs
 *   env=PATH=/geo/bin
 *   env=LOG_LEVEL=info
 *
 * ───────────────────────────────────────────────────────────────────────────── */

/* Configuration keys */
#define SVC_KEY_NAME        "name"
#define SVC_KEY_DESC        "description"
#define SVC_KEY_TYPE        "type"
#define SVC_KEY_COMMAND     "command"
#define SVC_KEY_WORKDIR     "workdir"
#define SVC_KEY_RESTART     "restart"
#define SVC_KEY_DEPENDS     "depends"
#define SVC_KEY_ENV         "env"

/* Type values */
#define SVC_TYPE_SIMPLE     "simple"
#define SVC_TYPE_DAEMON     "daemon"
#define SVC_TYPE_ONESHOT    "oneshot"
#define SVC_TYPE_MONITOR    "monitor"

/* Restart values */
#define SVC_RESTART_NEVER   "never"
#define SVC_RESTART_FAILURE "on-failure"
#define SVC_RESTART_ALWAYS  "always"

#endif /* PHANTOM_INIT_H */
