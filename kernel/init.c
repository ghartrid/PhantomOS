/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                              PHANTOM INIT SYSTEM
 *                        "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Implementation of the Phantom Init System - a service manager that embodies
 * the Phantom philosophy. Services don't die, they become dormant. They don't
 * restart, they awaken. All service history is preserved in the geological
 * record forever.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "init.h"
#include "vfs.h"
#include "../geofs.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Helper Functions
 * ───────────────────────────────────────────────────────────────────────────── */

static uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const char *state_to_string(service_state_t state) {
    switch (state) {
        case SERVICE_EMBRYO:    return "embryo";
        case SERVICE_STARTING:  return "starting";
        case SERVICE_RUNNING:   return "running";
        case SERVICE_DORMANT:   return "dormant";
        case SERVICE_AWAKENING: return "awakening";
        case SERVICE_BLOCKED:   return "blocked";
        default:                return "unknown";
    }
}

static const char *type_to_string(service_type_t type) {
    switch (type) {
        case SERVICE_TYPE_SIMPLE:  return SVC_TYPE_SIMPLE;
        case SERVICE_TYPE_DAEMON:  return SVC_TYPE_DAEMON;
        case SERVICE_TYPE_ONESHOT: return SVC_TYPE_ONESHOT;
        case SERVICE_TYPE_MONITOR: return SVC_TYPE_MONITOR;
        default:                   return "unknown";
    }
}

static service_type_t string_to_type(const char *str) {
    if (strcmp(str, SVC_TYPE_SIMPLE) == 0)  return SERVICE_TYPE_SIMPLE;
    if (strcmp(str, SVC_TYPE_DAEMON) == 0)  return SERVICE_TYPE_DAEMON;
    if (strcmp(str, SVC_TYPE_ONESHOT) == 0) return SERVICE_TYPE_ONESHOT;
    if (strcmp(str, SVC_TYPE_MONITOR) == 0) return SERVICE_TYPE_MONITOR;
    return SERVICE_TYPE_SIMPLE;
}

static restart_policy_t string_to_restart(const char *str) {
    if (strcmp(str, SVC_RESTART_NEVER) == 0)   return RESTART_NEVER;
    if (strcmp(str, SVC_RESTART_FAILURE) == 0) return RESTART_ON_FAILURE;
    if (strcmp(str, SVC_RESTART_ALWAYS) == 0)  return RESTART_ALWAYS;
    return RESTART_NEVER;
}

static const char *restart_to_string(restart_policy_t policy) {
    switch (policy) {
        case RESTART_NEVER:      return SVC_RESTART_NEVER;
        case RESTART_ON_FAILURE: return SVC_RESTART_FAILURE;
        case RESTART_ALWAYS:     return SVC_RESTART_ALWAYS;
        default:                 return "unknown";
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Logging (to geology)
 * ───────────────────────────────────────────────────────────────────────────── */

void init_log(phantom_init_t *init, const char *service, const char *fmt, ...) {
    char message[1024];
    char full_log[1280];
    va_list args;

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    /* Format: [timestamp] [service] message */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    snprintf(full_log, sizeof(full_log), "[%s] [%s] %s\n",
             timestamp, service ? service : "init", message);

    /* Print to console */
    printf("  [init] %s", full_log);

    /* If we have GeoFS, also log to geology */
    if (init && init->kernel && init->kernel->geofs_volume) {
        char log_path[256];
        snprintf(log_path, sizeof(log_path), "/geo/var/log/init.log");

        /* Append to log file via VFS (which goes to GeoFS) */
        if (init->vfs) {
            vfs_fd_t fd = vfs_open(init->vfs, 1, log_path,
                                    VFS_O_WRONLY | VFS_O_APPEND | VFS_O_CREATE, 0644);
            if (fd >= 0) {
                vfs_write(init->vfs, fd, full_log, strlen(full_log));
                vfs_close(init->vfs, fd);
            }
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Service Creation and Configuration
 * ───────────────────────────────────────────────────────────────────────────── */

phantom_service_t *init_service_create(phantom_init_t *init, const char *name) {
    phantom_service_t *svc = calloc(1, sizeof(phantom_service_t));
    if (!svc) return NULL;

    strncpy(svc->name, name, SERVICE_NAME_MAX - 1);
    svc->state = SERVICE_EMBRYO;
    svc->type = SERVICE_TYPE_SIMPLE;
    svc->restart_policy = RESTART_NEVER;
    svc->pid = 0;

    if (init) {
        pthread_mutex_lock(&init->lock);
        svc->service_id = init->next_service_id++;
        pthread_mutex_unlock(&init->lock);
    }

    return svc;
}

int init_service_configure(phantom_service_t *svc, const char *key,
                           const char *value) {
    if (!svc || !key || !value) return -1;

    if (strcmp(key, SVC_KEY_NAME) == 0) {
        strncpy(svc->name, value, SERVICE_NAME_MAX - 1);
    } else if (strcmp(key, SVC_KEY_DESC) == 0) {
        strncpy(svc->description, value, SERVICE_DESC_MAX - 1);
    } else if (strcmp(key, SVC_KEY_TYPE) == 0) {
        svc->type = string_to_type(value);
    } else if (strcmp(key, SVC_KEY_COMMAND) == 0) {
        strncpy(svc->command, value, SERVICE_CMD_MAX - 1);
    } else if (strcmp(key, SVC_KEY_WORKDIR) == 0) {
        strncpy(svc->working_dir, value, sizeof(svc->working_dir) - 1);
    } else if (strcmp(key, SVC_KEY_RESTART) == 0) {
        svc->restart_policy = string_to_restart(value);
    } else if (strcmp(key, SVC_KEY_DEPENDS) == 0) {
        /* Parse comma-separated dependencies */
        char deps[512];
        strncpy(deps, value, sizeof(deps) - 1);
        deps[sizeof(deps) - 1] = '\0';

        char *saveptr;
        char *dep = strtok_r(deps, ",", &saveptr);
        while (dep && svc->dependency_count < SERVICE_DEPS_MAX) {
            /* Trim whitespace */
            while (*dep == ' ') dep++;
            char *end = dep + strlen(dep) - 1;
            while (end > dep && *end == ' ') *end-- = '\0';

            if (*dep) {
                strncpy(svc->dependencies[svc->dependency_count],
                        dep, SERVICE_NAME_MAX - 1);
                svc->dependency_count++;
            }
            dep = strtok_r(NULL, ",", &saveptr);
        }
    } else if (strcmp(key, SVC_KEY_ENV) == 0) {
        if (svc->env_count < SERVICE_ENV_MAX) {
            strncpy(svc->env_vars[svc->env_count], value, 127);
            svc->env_count++;
        }
    } else {
        return -1;  /* Unknown key */
    }

    return 0;
}

int init_service_register(phantom_init_t *init, phantom_service_t *svc) {
    if (!init || !svc) {
        if (svc) free(svc);  /* Free on invalid init */
        return -1;
    }

    pthread_mutex_lock(&init->lock);

    /* Check for duplicate */
    phantom_service_t *existing = init->services;
    while (existing) {
        if (strcmp(existing->name, svc->name) == 0) {
            pthread_mutex_unlock(&init->lock);
            free(svc);  /* Free duplicate service to prevent leak */
            return -1;  /* Already registered */
        }
        existing = existing->next;
    }

    /* Add to list */
    svc->next = init->services;
    init->services = svc;
    init->service_count++;

    pthread_mutex_unlock(&init->lock);

    init_log(init, svc->name, "Service registered (type=%s, restart=%s)",
             type_to_string(svc->type), restart_to_string(svc->restart_policy));

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Service Queries
 * ───────────────────────────────────────────────────────────────────────────── */

/* Internal version - caller must hold lock */
static phantom_service_t *init_service_find_unlocked(phantom_init_t *init, const char *name) {
    if (!init || !name) return NULL;

    phantom_service_t *svc = init->services;
    while (svc) {
        if (strcmp(svc->name, name) == 0) {
            return svc;
        }
        svc = svc->next;
    }

    return NULL;
}

phantom_service_t *init_service_find(phantom_init_t *init, const char *name) {
    if (!init || !name) return NULL;

    pthread_mutex_lock(&init->lock);
    phantom_service_t *svc = init_service_find_unlocked(init, name);
    pthread_mutex_unlock(&init->lock);

    return svc;
}

service_state_t init_service_status(phantom_init_t *init, const char *name) {
    phantom_service_t *svc = init_service_find(init, name);
    if (!svc) return SERVICE_DORMANT;
    return svc->state;
}

int init_service_list(phantom_init_t *init,
                      void (*callback)(phantom_service_t *svc, void *ctx),
                      void *ctx) {
    if (!init || !callback) return -1;

    pthread_mutex_lock(&init->lock);

    phantom_service_t *svc = init->services;
    int count = 0;
    while (svc) {
        callback(svc, ctx);
        count++;
        svc = svc->next;
    }

    pthread_mutex_unlock(&init->lock);
    return count;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Dependency Resolution
 * ───────────────────────────────────────────────────────────────────────────── */

int init_check_dependencies(phantom_init_t *init, phantom_service_t *svc) {
    if (!init || !svc) return -1;

    for (int i = 0; i < svc->dependency_count; i++) {
        phantom_service_t *dep = init_service_find(init, svc->dependencies[i]);
        if (!dep) {
            init_log(init, svc->name, "Dependency not found: %s",
                     svc->dependencies[i]);
            return -1;
        }
        if (dep->state != SERVICE_RUNNING) {
            init_log(init, svc->name, "Dependency not running: %s (state=%s)",
                     svc->dependencies[i], state_to_string(dep->state));
            return -1;
        }
    }

    return 0;
}

/* Topological sort for boot order */
static int visit_service(phantom_service_t *svc, phantom_service_t **order,
                         int *index, int *visited, phantom_init_t *init) {
    int svc_idx = 0;
    phantom_service_t *s = init->services;
    while (s && s != svc) {
        svc_idx++;
        s = s->next;
    }

    if (visited[svc_idx] == 1) {
        /* Circular dependency detected */
        return -1;
    }
    if (visited[svc_idx] == 2) {
        /* Already processed */
        return 0;
    }

    visited[svc_idx] = 1;  /* Mark as being visited */

    /* Visit dependencies first */
    for (int i = 0; i < svc->dependency_count; i++) {
        phantom_service_t *dep = init_service_find_unlocked(init, svc->dependencies[i]);
        if (dep) {
            if (visit_service(dep, order, index, visited, init) < 0) {
                return -1;
            }
        }
    }

    visited[svc_idx] = 2;  /* Mark as fully processed */
    order[(*index)++] = svc;

    return 0;
}

int init_resolve_boot_order(phantom_init_t *init, phantom_service_t ***order,
                            int *count) {
    if (!init || !order || !count) return -1;

    pthread_mutex_lock(&init->lock);

    int n = init->service_count;
    if (n == 0) {
        pthread_mutex_unlock(&init->lock);
        *order = NULL;
        *count = 0;
        return 0;
    }

    *order = calloc(n, sizeof(phantom_service_t *));
    int *visited = calloc(n, sizeof(int));
    if (!*order || !visited) {
        free(*order);
        free(visited);
        pthread_mutex_unlock(&init->lock);
        return -1;
    }

    int index = 0;
    phantom_service_t *svc = init->services;
    while (svc) {
        if (visit_service(svc, *order, &index, visited, init) < 0) {
            free(*order);
            free(visited);
            pthread_mutex_unlock(&init->lock);
            init_log(init, NULL, "Circular dependency detected!");
            return -1;
        }
        svc = svc->next;
    }

    free(visited);
    *count = index;

    pthread_mutex_unlock(&init->lock);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Service Control
 * ───────────────────────────────────────────────────────────────────────────── */

int init_service_awaken(phantom_init_t *init, const char *name) {
    phantom_service_t *svc = init_service_find(init, name);
    if (!svc) {
        init_log(init, name, "Cannot awaken: service not found");
        return -1;
    }

    if (svc->state == SERVICE_RUNNING) {
        init_log(init, name, "Service already running");
        return 0;
    }

    /* Check dependencies */
    if (init_check_dependencies(init, svc) < 0) {
        init_log(init, name, "Cannot awaken: dependencies not satisfied");
        return -1;
    }

    svc->state = SERVICE_AWAKENING;
    init_log(init, name, "Awakening service...");

    /* For now, we simulate service startup */
    /* In a real system, this would fork/exec the command */
    if (svc->command[0]) {
        init_log(init, name, "Would execute: %s", svc->command);
    }

    /* Mark as running */
    svc->state = SERVICE_RUNNING;
    svc->start_count++;
    svc->last_start_time = get_timestamp_ms();

    pthread_mutex_lock(&init->lock);
    init->total_awakenings++;
    pthread_mutex_unlock(&init->lock);

    init_log(init, name, "Service awakened (awakening #%lu)", svc->start_count);

    return 0;
}

int init_service_rest(phantom_init_t *init, const char *name) {
    phantom_service_t *svc = init_service_find(init, name);
    if (!svc) {
        init_log(init, name, "Cannot rest: service not found");
        return -1;
    }

    if (svc->state == SERVICE_DORMANT) {
        init_log(init, name, "Service already dormant");
        return 0;
    }

    init_log(init, name, "Service entering dormancy...");

    /* In a real system, this would send SIGTERM then SIGKILL */
    /* For Phantom, we gracefully transition to dormancy */

    svc->state = SERVICE_DORMANT;
    svc->last_dormant_time = get_timestamp_ms();
    svc->exit_code = 0;  /* Graceful dormancy */

    pthread_mutex_lock(&init->lock);
    init->total_dormancies++;
    pthread_mutex_unlock(&init->lock);

    init_log(init, name, "Service is now dormant");

    return 0;
}

int init_service_reload(phantom_init_t *init, const char *name) {
    phantom_service_t *svc = init_service_find(init, name);
    if (!svc) {
        init_log(init, name, "Cannot reload: service not found");
        return -1;
    }

    init_log(init, name, "Reloading service configuration...");

    /* In a real system, this would send SIGHUP */
    /* For now, we just log the action */

    init_log(init, name, "Service reloaded");
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Service Definition File I/O
 * ───────────────────────────────────────────────────────────────────────────── */

int init_load_service_file(phantom_init_t *init, const char *path) {
    if (!init || !path) return -1;

    vfs_fd_t fd = vfs_open(init->vfs, 1, path, VFS_O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    char buffer[4096];
    ssize_t bytes_read = vfs_read(init->vfs, fd, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        vfs_close(init->vfs, fd);
        return -1;
    }
    buffer[bytes_read] = '\0';
    vfs_close(init->vfs, fd);

    /* Parse the service file */
    phantom_service_t *svc = init_service_create(init, "");

    char *line = strtok(buffer, "\n");
    while (line) {
        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0') {
            line = strtok(NULL, "\n");
            continue;
        }

        /* Parse key=value */
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *key = line;
            char *value = eq + 1;

            /* Trim whitespace */
            while (*key == ' ') key++;
            char *end = key + strlen(key) - 1;
            while (end > key && *end == ' ') *end-- = '\0';

            while (*value == ' ') value++;
            end = value + strlen(value) - 1;
            while (end > value && *end == ' ') *end-- = '\0';

            init_service_configure(svc, key, value);
        }

        line = strtok(NULL, "\n");
    }

    if (svc->name[0] == '\0') {
        free(svc);
        return -1;
    }

    init_service_register(init, svc);
    init_log(init, svc->name, "Loaded service definition from %s", path);

    return 0;
}

int init_save_service_file(phantom_init_t *init, phantom_service_t *svc) {
    if (!init || !svc) return -1;

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.svc", init->services_dir, svc->name);

    /* Build the service file content */
    char content[4096];
    int len = 0;

    len += snprintf(content + len, sizeof(content) - len,
                    "# Phantom Service Definition\n");
    len += snprintf(content + len, sizeof(content) - len,
                    "# Generated by init system\n\n");
    len += snprintf(content + len, sizeof(content) - len,
                    "name=%s\n", svc->name);
    if (svc->description[0]) {
        len += snprintf(content + len, sizeof(content) - len,
                        "description=%s\n", svc->description);
    }
    len += snprintf(content + len, sizeof(content) - len,
                    "type=%s\n", type_to_string(svc->type));
    if (svc->command[0]) {
        len += snprintf(content + len, sizeof(content) - len,
                        "command=%s\n", svc->command);
    }
    if (svc->working_dir[0]) {
        len += snprintf(content + len, sizeof(content) - len,
                        "workdir=%s\n", svc->working_dir);
    }
    len += snprintf(content + len, sizeof(content) - len,
                    "restart=%s\n", restart_to_string(svc->restart_policy));

    if (svc->dependency_count > 0) {
        len += snprintf(content + len, sizeof(content) - len, "depends=");
        for (int i = 0; i < svc->dependency_count; i++) {
            if (i > 0) len += snprintf(content + len, sizeof(content) - len, ",");
            len += snprintf(content + len, sizeof(content) - len, "%s",
                            svc->dependencies[i]);
        }
        len += snprintf(content + len, sizeof(content) - len, "\n");
    }

    for (int i = 0; i < svc->env_count; i++) {
        len += snprintf(content + len, sizeof(content) - len,
                        "env=%s\n", svc->env_vars[i]);
    }

    /* Write to VFS (GeoFS) */
    vfs_fd_t fd = vfs_open(init->vfs, 1, path,
                            VFS_O_WRONLY | VFS_O_CREATE, 0644);
    if (fd < 0) {
        init_log(init, svc->name, "Failed to save service file to %s", path);
        return -1;
    }

    vfs_write(init->vfs, fd, content, strlen(content));
    vfs_close(init->vfs, fd);

    init_log(init, svc->name, "Saved service definition to %s", path);
    return 0;
}

int init_scan_services_dir(phantom_init_t *init) {
    if (!init) return -1;

    init_log(init, NULL, "Scanning services directory: %s", init->services_dir);

    /* For now, we register built-in services */
    /* In a full implementation, we would read the directory and load .svc files */

    return init_register_builtin_services(init);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Built-in Services
 * ───────────────────────────────────────────────────────────────────────────── */

int init_register_builtin_services(phantom_init_t *init) {
    phantom_service_t *svc;

    /* GeoFS - the foundation */
    svc = init_service_create(init, SERVICE_GEOFS);
    init_service_configure(svc, SVC_KEY_DESC, "Geology FileSystem - append-only storage");
    init_service_configure(svc, SVC_KEY_TYPE, SVC_TYPE_DAEMON);
    init_service_configure(svc, SVC_KEY_RESTART, SVC_RESTART_ALWAYS);
    svc->state = SERVICE_RUNNING;  /* Already running by kernel init */
    init_service_register(init, svc);

    /* VFS - virtual filesystem */
    svc = init_service_create(init, SERVICE_VFS);
    init_service_configure(svc, SVC_KEY_DESC, "Virtual FileSystem layer");
    init_service_configure(svc, SVC_KEY_TYPE, SVC_TYPE_DAEMON);
    init_service_configure(svc, SVC_KEY_RESTART, SVC_RESTART_ALWAYS);
    init_service_configure(svc, SVC_KEY_DEPENDS, SERVICE_GEOFS);
    svc->state = SERVICE_RUNNING;
    init_service_register(init, svc);

    /* ProcFS - process filesystem */
    svc = init_service_create(init, SERVICE_PROCFS);
    init_service_configure(svc, SVC_KEY_DESC, "Process information filesystem");
    init_service_configure(svc, SVC_KEY_TYPE, SVC_TYPE_DAEMON);
    init_service_configure(svc, SVC_KEY_RESTART, SVC_RESTART_ALWAYS);
    init_service_configure(svc, SVC_KEY_DEPENDS, SERVICE_VFS);
    svc->state = SERVICE_RUNNING;
    init_service_register(init, svc);

    /* DevFS - device filesystem */
    svc = init_service_create(init, SERVICE_DEVFS);
    init_service_configure(svc, SVC_KEY_DESC, "Device filesystem");
    init_service_configure(svc, SVC_KEY_TYPE, SVC_TYPE_DAEMON);
    init_service_configure(svc, SVC_KEY_RESTART, SVC_RESTART_ALWAYS);
    init_service_configure(svc, SVC_KEY_DEPENDS, SERVICE_VFS);
    svc->state = SERVICE_RUNNING;
    init_service_register(init, svc);

    /* Governor - AI code evaluator */
    svc = init_service_create(init, SERVICE_GOVERNOR);
    init_service_configure(svc, SVC_KEY_DESC, "AI code evaluator - protector of creation");
    init_service_configure(svc, SVC_KEY_TYPE, SVC_TYPE_DAEMON);
    init_service_configure(svc, SVC_KEY_RESTART, SVC_RESTART_ALWAYS);
    init_service_configure(svc, SVC_KEY_DEPENDS, "geofs,vfs");
    svc->state = SERVICE_RUNNING;
    init_service_register(init, svc);

    /* Shell - interactive interface */
    svc = init_service_create(init, SERVICE_SHELL);
    init_service_configure(svc, SVC_KEY_DESC, "Interactive Phantom shell");
    init_service_configure(svc, SVC_KEY_TYPE, SVC_TYPE_SIMPLE);
    init_service_configure(svc, SVC_KEY_RESTART, SVC_RESTART_NEVER);
    init_service_configure(svc, SVC_KEY_DEPENDS, "vfs,procfs,devfs");
    svc->state = SERVICE_DORMANT;  /* Started on demand */
    init_service_register(init, svc);

    init_log(init, NULL, "Registered %d built-in services", init->service_count);

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Service Monitor Thread
 * ───────────────────────────────────────────────────────────────────────────── */

static void *monitor_thread_func(void *arg) {
    phantom_init_t *init = (phantom_init_t *)arg;

    while (init->running) {
        usleep(init->monitor_interval_ms * 1000);

        pthread_mutex_lock(&init->lock);

        /* Check each service */
        phantom_service_t *svc = init->services;
        while (svc) {
            /* For daemon services with restart=always, check if they need awakening */
            if (svc->type == SERVICE_TYPE_DAEMON &&
                svc->restart_policy == RESTART_ALWAYS &&
                svc->state == SERVICE_DORMANT) {

                pthread_mutex_unlock(&init->lock);
                init_log(init, svc->name, "Auto-awakening dormant daemon");
                init_service_awaken(init, svc->name);
                pthread_mutex_lock(&init->lock);
            }

            svc = svc->next;
        }

        init->uptime_seconds = (get_timestamp_ms() - init->boot_time) / 1000;

        pthread_mutex_unlock(&init->lock);
    }

    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Init System Lifecycle
 * ───────────────────────────────────────────────────────────────────────────── */

int init_create(phantom_init_t *init, struct phantom_kernel *kernel,
                struct vfs_context *vfs) {
    if (!init || !kernel) return -1;

    memset(init, 0, sizeof(phantom_init_t));

    init->kernel = kernel;
    init->vfs = vfs;
    init->services = NULL;
    init->service_count = 0;
    init->next_service_id = 1;

    strncpy(init->services_dir, "/geo/etc/init", sizeof(init->services_dir) - 1);
    init->auto_awaken = 1;
    init->monitor_interval_ms = 5000;  /* Check every 5 seconds */

    init->initialized = 0;
    init->running = 0;
    init->boot_time = get_timestamp_ms();

    pthread_mutex_init(&init->lock, NULL);

    return 0;
}

int init_start(phantom_init_t *init) {
    if (!init) return -1;

    init_log(init, NULL, "═══════════════════════════════════════════════════");
    init_log(init, NULL, "         PHANTOM INIT SYSTEM STARTING");
    init_log(init, NULL, "           \"To Create, Not To Destroy\"");
    init_log(init, NULL, "═══════════════════════════════════════════════════");

    /* Create the services directory structure in GeoFS */
    if (init->vfs) {
        vfs_mkdir(init->vfs, 1, "/geo/etc", 0755);
        vfs_mkdir(init->vfs, 1, "/geo/etc/init", 0755);
        vfs_mkdir(init->vfs, 1, "/geo/var", 0755);
        vfs_mkdir(init->vfs, 1, "/geo/var/log", 0755);
    }

    /* Scan and load services */
    init_scan_services_dir(init);

    /* Resolve boot order */
    phantom_service_t **boot_order = NULL;
    int boot_count = 0;
    if (init_resolve_boot_order(init, &boot_order, &boot_count) == 0) {
        init_log(init, NULL, "Boot order resolved: %d services", boot_count);

        /* Start services in order */
        for (int i = 0; i < boot_count; i++) {
            if (boot_order[i]->state == SERVICE_DORMANT &&
                boot_order[i]->type != SERVICE_TYPE_SIMPLE) {
                init_service_awaken(init, boot_order[i]->name);
            }
        }

        free(boot_order);
    }

    /* Start monitor thread */
    init->running = 1;
    init->initialized = 1;
    pthread_create(&init->monitor_thread, NULL, monitor_thread_func, init);

    init_log(init, NULL, "Init system started successfully");
    init_log(init, NULL, "═══════════════════════════════════════════════════");

    return 0;
}

int init_shutdown(phantom_init_t *init) {
    if (!init) return -1;

    init_log(init, NULL, "═══════════════════════════════════════════════════");
    init_log(init, NULL, "         PHANTOM INIT SYSTEM SHUTDOWN");
    init_log(init, NULL, "       (All services entering dormancy)");
    init_log(init, NULL, "═══════════════════════════════════════════════════");

    /* Stop monitor thread */
    init->running = 0;
    pthread_join(init->monitor_thread, NULL);

    /* Put all services to rest (in reverse order ideally) */
    pthread_mutex_lock(&init->lock);
    phantom_service_t *svc = init->services;
    while (svc) {
        if (svc->state == SERVICE_RUNNING) {
            pthread_mutex_unlock(&init->lock);
            init_service_rest(init, svc->name);
            pthread_mutex_lock(&init->lock);
        }
        svc = svc->next;
    }
    pthread_mutex_unlock(&init->lock);

    init_log(init, NULL, "All services dormant. Init shutdown complete.");
    init_log(init, NULL, "Total awakenings: %lu", init->total_awakenings);
    init_log(init, NULL, "Total dormancies: %lu", init->total_dormancies);
    init_log(init, NULL, "Uptime: %lu seconds", init->uptime_seconds);

    pthread_mutex_destroy(&init->lock);

    return 0;
}
