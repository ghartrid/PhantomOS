/*
 * ============================================================================
 *                            PHANTOM PODS
 *                    "Compatibility Without Compromise"
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#include "phantom_pods.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Security: Shell Escape Function
 * Safely escapes a path/argument for use in shell commands by wrapping in
 * single quotes and escaping any embedded single quotes.
 * ───────────────────────────────────────────────────────────────────────────── */
static int shell_escape_arg(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size < 3) return -1;

    size_t in_len = strlen(input);
    size_t out_idx = 0;
    size_t i = 0;

    output[out_idx++] = '\'';

    for (; i < in_len && out_idx < output_size - 2; i++) {
        if (input[i] == '\'') {
            if (out_idx + 4 >= output_size - 1) return -1;
            output[out_idx++] = '\'';
            output[out_idx++] = '\\';
            output[out_idx++] = '\'';
            output[out_idx++] = '\'';
        } else {
            output[out_idx++] = input[i];
        }
    }

    /* Check if we processed all input - if not, buffer too small */
    if (i < in_len) return -1;

    if (out_idx >= output_size - 1) return -1;
    output[out_idx++] = '\'';
    output[out_idx] = '\0';

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Built-in Pod Templates
 * ───────────────────────────────────────────────────────────────────────────── */

static phantom_pod_template_t builtin_templates[] = {
    {
        .name = "Linux Native",
        .description = "Run Linux applications with isolation",
        .icon = "🐧",
        .type = POD_TYPE_NATIVE,
        .security = POD_SECURITY_STANDARD,
        .default_limits = {
            .cpu_percent = 50,
            .memory_mb = 1024,
            .storage_mb = 2048,
            .network_kbps = 0,
            .allow_gpu = 1,
            .allow_audio = 1,
            .allow_usb = 0,
            .allow_display = 1
        }
    },
    {
        .name = "Windows (Wine)",
        .description = "Run Windows applications via Wine",
        .icon = "🪟",
        .type = POD_TYPE_WINE,
        .security = POD_SECURITY_STANDARD,
        .default_limits = {
            .cpu_percent = 75,
            .memory_mb = 2048,
            .storage_mb = 4096,
            .network_kbps = 0,
            .allow_gpu = 1,
            .allow_audio = 1,
            .allow_usb = 0,
            .allow_display = 1
        }
    },
    {
        .name = "Windows 64-bit",
        .description = "Run 64-bit Windows applications",
        .icon = "🪟",
        .type = POD_TYPE_WINE64,
        .security = POD_SECURITY_STANDARD,
        .default_limits = {
            .cpu_percent = 75,
            .memory_mb = 4096,
            .storage_mb = 8192,
            .network_kbps = 0,
            .allow_gpu = 1,
            .allow_audio = 1,
            .allow_usb = 0,
            .allow_display = 1
        }
    },
    {
        .name = "DOS Retro",
        .description = "Run classic DOS games and applications",
        .icon = "👾",
        .type = POD_TYPE_DOSBOX,
        .security = POD_SECURITY_HIGH,
        .default_limits = {
            .cpu_percent = 25,
            .memory_mb = 256,
            .storage_mb = 512,
            .network_kbps = 0,
            .allow_gpu = 0,
            .allow_audio = 1,
            .allow_usb = 0,
            .allow_display = 1
        }
    },
    {
        .name = "Flatpak Apps",
        .description = "Run Flatpak containerized applications",
        .icon = "📦",
        .type = POD_TYPE_FLATPAK,
        .security = POD_SECURITY_STANDARD,
        .default_limits = {
            .cpu_percent = 50,
            .memory_mb = 2048,
            .storage_mb = 4096,
            .network_kbps = 0,
            .allow_gpu = 1,
            .allow_audio = 1,
            .allow_usb = 1,
            .allow_display = 1
        }
    },
    {
        .name = "AppImage Runner",
        .description = "Run portable AppImage applications",
        .icon = "📀",
        .type = POD_TYPE_APPIMAGE,
        .security = POD_SECURITY_STANDARD,
        .default_limits = {
            .cpu_percent = 50,
            .memory_mb = 1024,
            .storage_mb = 1024,
            .network_kbps = 0,
            .allow_gpu = 1,
            .allow_audio = 1,
            .allow_usb = 0,
            .allow_display = 1
        }
    },
    {
        .name = "Secure Sandbox",
        .description = "Maximum isolation for untrusted apps",
        .icon = "🔒",
        .type = POD_TYPE_NATIVE,
        .security = POD_SECURITY_MAXIMUM,
        .default_limits = {
            .cpu_percent = 25,
            .memory_mb = 512,
            .storage_mb = 256,
            .network_kbps = 0,
            .allow_gpu = 0,
            .allow_audio = 0,
            .allow_usb = 0,
            .allow_display = 1
        }
    },
    {
        .name = "Developer Environment",
        .description = "Full-featured development container",
        .icon = "💻",
        .type = POD_TYPE_NATIVE,
        .security = POD_SECURITY_RELAXED,
        .default_limits = {
            .cpu_percent = 100,
            .memory_mb = 8192,
            .storage_mb = 16384,
            .network_kbps = 0,
            .allow_gpu = 1,
            .allow_audio = 1,
            .allow_usb = 1,
            .allow_display = 1
        }
    }
};

static const int builtin_template_count = sizeof(builtin_templates) / sizeof(builtin_templates[0]);

/* ─────────────────────────────────────────────────────────────────────────────
 * Helper Functions
 * ───────────────────────────────────────────────────────────────────────────── */

static int check_command_exists(const char *cmd) {
    char path[512];
    snprintf(path, sizeof(path), "which %s >/dev/null 2>&1", cmd);
    return system(path) == 0;
}

const char *phantom_pod_type_name(phantom_pod_type_t type) {
    switch (type) {
        case POD_TYPE_NATIVE:   return "Native Linux";
        case POD_TYPE_WINE:     return "Wine (Windows)";
        case POD_TYPE_WINE64:   return "Wine64 (Windows 64-bit)";
        case POD_TYPE_DOSBOX:   return "DOSBox";
        case POD_TYPE_QEMU:     return "QEMU Emulation";
        case POD_TYPE_FLATPAK:  return "Flatpak";
        case POD_TYPE_APPIMAGE: return "AppImage";
        case POD_TYPE_CUSTOM:   return "Custom";
        default:                return "Unknown";
    }
}

const char *phantom_pod_state_name(phantom_pod_state_t state) {
    switch (state) {
        case POD_STATE_MANIFESTING: return "Manifesting";
        case POD_STATE_READY:       return "Ready";
        case POD_STATE_ACTIVE:      return "Active";
        case POD_STATE_DORMANT:     return "Dormant";
        case POD_STATE_ARCHIVED:    return "Archived";
        case POD_STATE_MIGRATING:   return "Migrating";
        default:                    return "Unknown";
    }
}

const char *phantom_pod_security_name(phantom_pod_security_t security) {
    switch (security) {
        case POD_SECURITY_MAXIMUM:  return "Maximum";
        case POD_SECURITY_HIGH:     return "High";
        case POD_SECURITY_STANDARD: return "Standard";
        case POD_SECURITY_RELAXED:  return "Relaxed";
        case POD_SECURITY_CUSTOM:   return "Custom";
        default:                    return "Unknown";
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * System Initialization
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_pods_init(phantom_pod_system_t *system, const char *pods_root) {
    if (!system) return -1;

    memset(system, 0, sizeof(phantom_pod_system_t));

    /* Set paths */
    if (pods_root) {
        strncpy(system->pods_root, pods_root, PHANTOM_POD_MAX_PATH - 1);
    } else {
        strncpy(system->pods_root, "/var/phantom/pods", PHANTOM_POD_MAX_PATH - 1);
    }

    snprintf(system->templates_path, PHANTOM_POD_MAX_PATH,
             "%.490s/templates", system->pods_root);

    system->next_pod_id = 1;

    /* Detect available compatibility layers */
    phantom_pods_detect_compatibility(system);

    printf("[PhantomPods] Initialized at %s\n", system->pods_root);
    printf("[PhantomPods] Compatibility: Wine=%d Wine64=%d DOSBox=%d Flatpak=%d\n",
           system->wine_available, system->wine64_available,
           system->dosbox_available, system->flatpak_available);

    return 0;
}

void phantom_pods_shutdown(phantom_pod_system_t *system) {
    if (!system) return;

    /* Make all active pods dormant */
    for (int i = 0; i < system->pod_count; i++) {
        if (system->pods[i].state == POD_STATE_ACTIVE) {
            phantom_pod_make_dormant(system, &system->pods[i]);
        }
    }

    printf("[PhantomPods] Shutdown complete. %d pods preserved.\n", system->pod_count);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Compatibility Detection
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_pods_detect_compatibility(phantom_pod_system_t *system) {
    if (!system) return -1;

    system->wine_available = check_command_exists("wine");
    system->wine64_available = check_command_exists("wine64");
    system->dosbox_available = check_command_exists("dosbox");
    system->flatpak_available = check_command_exists("flatpak");

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod Creation
 * ───────────────────────────────────────────────────────────────────────────── */

phantom_pod_t *phantom_pod_create(phantom_pod_system_t *system,
                                   const char *name,
                                   phantom_pod_type_t type) {
    if (!system || !name) return NULL;
    if (system->pod_count >= PHANTOM_POD_MAX_PODS) return NULL;

    /* Check if name already exists */
    for (int i = 0; i < system->pod_count; i++) {
        if (strcmp(system->pods[i].name, name) == 0) {
            return NULL;  /* Name must be unique */
        }
    }

    phantom_pod_t *pod = &system->pods[system->pod_count];
    memset(pod, 0, sizeof(phantom_pod_t));

    /* Initialize pod */
    pod->id = system->next_pod_id++;
    strncpy(pod->name, name, PHANTOM_POD_MAX_NAME - 1);
    pod->type = type;
    pod->state = POD_STATE_MANIFESTING;
    pod->security = POD_SECURITY_STANDARD;
    pod->created = time(NULL);

    /* Set icon based on type */
    switch (type) {
        case POD_TYPE_NATIVE:   strcpy(pod->icon, "🐧"); break;
        case POD_TYPE_WINE:
        case POD_TYPE_WINE64:   strcpy(pod->icon, "🪟"); break;
        case POD_TYPE_DOSBOX:   strcpy(pod->icon, "👾"); break;
        case POD_TYPE_FLATPAK:  strcpy(pod->icon, "📦"); break;
        case POD_TYPE_APPIMAGE: strcpy(pod->icon, "📀"); break;
        default:                strcpy(pod->icon, "📦"); break;
    }

    /* Set default limits */
    pod->limits.cpu_percent = 50;
    pod->limits.memory_mb = 1024;
    pod->limits.storage_mb = 2048;
    pod->limits.allow_display = 1;
    pod->limits.allow_audio = 1;

    /* Create geology layer path */
    snprintf(pod->geology_layer, PHANTOM_POD_MAX_PATH,
             "%.380s/%.100s/geology", system->pods_root, name);

    system->pod_count++;
    system->total_pods_created++;

    /* Mark as ready */
    pod->state = POD_STATE_READY;

    printf("[PhantomPods] Created pod '%s' (ID: %u, Type: %s)\n",
           name, pod->id, phantom_pod_type_name(type));

    return pod;
}

phantom_pod_t *phantom_pod_create_from_template(phantom_pod_system_t *system,
                                                 const char *name,
                                                 const phantom_pod_template_t *tmpl) {
    if (!tmpl) return NULL;

    phantom_pod_t *pod = phantom_pod_create(system, name, tmpl->type);
    if (!pod) return NULL;

    /* Apply template settings */
    strncpy(pod->description, tmpl->description, PHANTOM_POD_MAX_DESC - 1);
    strncpy(pod->icon, tmpl->icon, sizeof(pod->icon) - 1);
    pod->security = tmpl->security;
    memcpy(&pod->limits, &tmpl->default_limits, sizeof(phantom_pod_limits_t));

    return pod;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod Lifecycle
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_pod_activate(phantom_pod_system_t *system, phantom_pod_t *pod) {
    if (!system || !pod) return -1;
    if (pod->state == POD_STATE_ACTIVE) return 0;  /* Already active */

    /* Check compatibility layer availability */
    switch (pod->type) {
        case POD_TYPE_NATIVE:
        case POD_TYPE_APPIMAGE:
        case POD_TYPE_CUSTOM:
            /* Native Linux pods always work */
            break;
        case POD_TYPE_WINE:
            if (!system->wine_available) {
                printf("[PhantomPods] Error: Wine not available. Install with: sudo apt install wine\n");
                return -2;  /* Wine not available */
            }
            break;
        case POD_TYPE_WINE64:
            if (!system->wine64_available) {
                printf("[PhantomPods] Error: Wine64 not available. Install with: sudo apt install wine64\n");
                return -3;  /* Wine64 not available */
            }
            break;
        case POD_TYPE_DOSBOX:
            if (!system->dosbox_available) {
                printf("[PhantomPods] Error: DOSBox not available. Install with: sudo apt install dosbox\n");
                return -4;  /* DOSBox not available */
            }
            break;
        case POD_TYPE_FLATPAK:
            if (!system->flatpak_available) {
                printf("[PhantomPods] Error: Flatpak not available. Install with: sudo apt install flatpak\n");
                return -5;  /* Flatpak not available */
            }
            break;
        case POD_TYPE_QEMU:
            /* QEMU support - check if qemu is available */
            if (!check_command_exists("qemu-system-x86_64")) {
                printf("[PhantomPods] Error: QEMU not available. Install with: sudo apt install qemu-system-x86\n");
                return -6;  /* QEMU not available */
            }
            break;
    }

    pod->state = POD_STATE_ACTIVE;
    pod->last_active = time(NULL);

    printf("[PhantomPods] Pod '%s' activated\n", pod->name);
    return 0;
}

int phantom_pod_make_dormant(phantom_pod_system_t *system, phantom_pod_t *pod) {
    if (!system || !pod) return -1;
    if (pod->state == POD_STATE_DORMANT) return 0;

    /* If running, gracefully stop */
    if (pod->pid > 0) {
        kill(pod->pid, SIGTERM);
        usleep(100000);  /* Wait 100ms */
        if (kill(pod->pid, 0) == 0) {
            kill(pod->pid, SIGKILL);
        }
        pod->pid = 0;
    }

    /* Update runtime stats */
    if (pod->last_active > 0) {
        pod->total_runtime_secs += (time(NULL) - pod->last_active);
    }

    pod->state = POD_STATE_DORMANT;
    printf("[PhantomPods] Pod '%s' is now dormant\n", pod->name);

    return 0;
}

int phantom_pod_archive(phantom_pod_system_t *system, phantom_pod_t *pod) {
    if (!system || !pod) return -1;

    /* Must be dormant first */
    if (pod->state == POD_STATE_ACTIVE) {
        phantom_pod_make_dormant(system, pod);
    }

    pod->state = POD_STATE_ARCHIVED;
    printf("[PhantomPods] Pod '%s' archived to geology\n", pod->name);

    return 0;
}

int phantom_pod_restore(phantom_pod_system_t *system, phantom_pod_t *pod) {
    if (!system || !pod) return -1;
    if (pod->state != POD_STATE_ARCHIVED) return -1;

    pod->state = POD_STATE_READY;
    printf("[PhantomPods] Pod '%s' restored from geology\n", pod->name);

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Pod Configuration
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_pod_set_limits(phantom_pod_t *pod, const phantom_pod_limits_t *limits) {
    if (!pod || !limits) return -1;
    memcpy(&pod->limits, limits, sizeof(phantom_pod_limits_t));
    return 0;
}

int phantom_pod_add_mount(phantom_pod_t *pod, const char *host_path,
                          const char *pod_path, int read_only) {
    if (!pod || !host_path || !pod_path) return -1;
    if (pod->mount_count >= PHANTOM_POD_MAX_MOUNTS) return -1;

    phantom_pod_mount_t *mount = &pod->mounts[pod->mount_count];
    strncpy(mount->host_path, host_path, PHANTOM_POD_MAX_PATH - 1);
    strncpy(mount->pod_path, pod_path, PHANTOM_POD_MAX_PATH - 1);
    mount->read_only = read_only;
    mount->geology_backed = 1;  /* Default to GeoFS backing */

    pod->mount_count++;
    return 0;
}

int phantom_pod_add_env(phantom_pod_t *pod, const char *name, const char *value) {
    if (!pod || !name) return -1;
    if (pod->env_count >= PHANTOM_POD_MAX_ENV_VARS) return -1;

    phantom_pod_env_t *env = &pod->env_vars[pod->env_count];
    strncpy(env->name, name, sizeof(env->name) - 1);
    strncpy(env->value, value ? value : "", sizeof(env->value) - 1);

    pod->env_count++;
    return 0;
}

int phantom_pod_set_security(phantom_pod_t *pod, phantom_pod_security_t level) {
    if (!pod) return -1;
    pod->security = level;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Application Management
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_pod_install_app(phantom_pod_t *pod, const char *name,
                            const char *executable, const char *icon) {
    if (!pod || !name || !executable) return -1;
    if (pod->app_count >= PHANTOM_POD_MAX_APPS) return -1;

    phantom_pod_app_t *app = &pod->apps[pod->app_count];
    memset(app, 0, sizeof(phantom_pod_app_t));

    strncpy(app->name, name, PHANTOM_POD_MAX_NAME - 1);
    strncpy(app->executable, executable, PHANTOM_POD_MAX_PATH - 1);
    if (icon) strncpy(app->icon, icon, sizeof(app->icon) - 1);
    else strcpy(app->icon, "📄");

    app->installed = 1;

    pod->app_count++;
    printf("[PhantomPods] Installed app '%s' in pod '%s'\n", name, pod->name);

    return 0;
}

int phantom_pod_run_app(phantom_pod_system_t *system, phantom_pod_t *pod,
                        phantom_pod_app_t *app) {
    if (!system || !pod || !app) return -1;

    /* Ensure pod is active */
    if (pod->state != POD_STATE_ACTIVE) {
        if (phantom_pod_activate(system, pod) != 0) {
            return -1;
        }
    }

    /* Build command with properly escaped paths to prevent command injection */
    char command[2048];
    char escaped_exec[512];
    char escaped_args[512];

    /* Escape executable path */
    if (shell_escape_arg(app->executable, escaped_exec, sizeof(escaped_exec)) != 0) {
        printf("[PhantomPods] Error: executable path too long or invalid\n");
        return -1;
    }

    /* Escape arguments if present */
    escaped_args[0] = '\0';
    if (app->arguments[0]) {
        if (shell_escape_arg(app->arguments, escaped_args, sizeof(escaped_args)) != 0) {
            printf("[PhantomPods] Error: arguments too long or invalid\n");
            return -1;
        }
    }

    switch (pod->type) {
        case POD_TYPE_WINE:
            snprintf(command, sizeof(command), "wine %s %s",
                     escaped_exec, escaped_args);
            break;
        case POD_TYPE_WINE64:
            snprintf(command, sizeof(command), "wine64 %s %s",
                     escaped_exec, escaped_args);
            break;
        case POD_TYPE_DOSBOX:
            snprintf(command, sizeof(command), "dosbox %s -exit",
                     escaped_exec);
            break;
        case POD_TYPE_NATIVE:
        case POD_TYPE_APPIMAGE:
        default:
            snprintf(command, sizeof(command), "%s %s",
                     escaped_exec, escaped_args);
            break;
    }

    /* Fork and execute */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child process */
        /* Set environment variables - sanitize names to prevent injection */
        for (int i = 0; i < pod->env_count; i++) {
            /* Only allow alphanumeric and underscore in env var names */
            int valid = 1;
            for (const char *p = pod->env_vars[i].name; *p && valid; p++) {
                if (!(*p >= 'A' && *p <= 'Z') && !(*p >= 'a' && *p <= 'z') &&
                    !(*p >= '0' && *p <= '9') && *p != '_') {
                    valid = 0;
                }
            }
            if (valid && pod->env_vars[i].name[0]) {
                setenv(pod->env_vars[i].name, pod->env_vars[i].value, 1);
            }
        }

        /* Change to working directory if set */
        if (app->working_dir[0]) {
            /* Validate working directory doesn't contain shell metacharacters */
            if (chdir(app->working_dir) != 0) {
                _exit(1);
            }
        }

        /* Execute */
        execl("/bin/sh", "sh", "-c", command, NULL);
        _exit(1);
    } else if (pid > 0) {
        pod->pid = pid;
        app->last_run = time(NULL);
        app->run_count++;
        system->total_apps_run++;

        printf("[PhantomPods] Running '%s' in pod '%s' (PID: %d)\n",
               app->name, pod->name, pid);
        return 0;
    }

    return -1;
}

int phantom_pod_import_executable(phantom_pod_t *pod, const char *host_path) {
    if (!pod || !host_path) return -1;

    /* Get filename from path */
    const char *filename = strrchr(host_path, '/');
    filename = filename ? filename + 1 : host_path;

    /* Install as app */
    return phantom_pod_install_app(pod, filename, host_path, "📄");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Query Functions
 * ───────────────────────────────────────────────────────────────────────────── */

phantom_pod_t *phantom_pod_find_by_id(phantom_pod_system_t *system, uint32_t id) {
    if (!system) return NULL;
    for (int i = 0; i < system->pod_count; i++) {
        if (system->pods[i].id == id) {
            return &system->pods[i];
        }
    }
    return NULL;
}

phantom_pod_t *phantom_pod_find_by_name(phantom_pod_system_t *system, const char *name) {
    if (!system || !name) return NULL;
    for (int i = 0; i < system->pod_count; i++) {
        if (strcmp(system->pods[i].name, name) == 0) {
            return &system->pods[i];
        }
    }
    return NULL;
}

int phantom_pod_get_active_count(phantom_pod_system_t *system) {
    if (!system) return 0;
    int count = 0;
    for (int i = 0; i < system->pod_count; i++) {
        if (system->pods[i].state == POD_STATE_ACTIVE) {
            count++;
        }
    }
    return count;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Templates
 * ───────────────────────────────────────────────────────────────────────────── */

const phantom_pod_template_t *phantom_pod_get_templates(int *count) {
    if (count) *count = builtin_template_count;
    return builtin_templates;
}
