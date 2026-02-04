/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                           PHANTOM SHELL
 *                    "To Create, Not To Destroy"
 *
 *    An interactive command interpreter that embodies Phantom principles.
 *    Every command creates or preserves - destruction is not an option.
 *
 * ══════════════════════════════════════════════════════════════════════════════
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "shell.h"
#include "vfs.h"
#include "phantom.h"
#include "init.h"
#include "governor.h"
#include "phantom_ai.h"
#include "phantom_net.h"
#include "phantom_tls.h"
#include "phantom_user.h"
#include "phantom_pkg.h"
#include "phantom_time.h"
#include "phantom_browser.h"
#include "phantom_webbrowser.h"
#include "phantom_apps.h"
#include "phantom_storage.h"
#include "phantom_dnauth.h"
#include "../geofs.h"
#include <errno.h>
#include <stdint.h>
#include <limits.h>

/* ══════════════════════════════════════════════════════════════════════════════
 * SECURITY HELPER FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Safe string concatenation - returns bytes written or -1 if buffer full */
static int safe_strcat(char *dest, size_t dest_size, const char *src) {
    if (!dest || !src || dest_size == 0) return -1;

    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);

    if (dest_len + src_len >= dest_size) {
        return -1;  /* Would overflow */
    }

    memcpy(dest + dest_len, src, src_len + 1);
    return (int)src_len;
}

/* Safe concatenation of argv into buffer with space separators */
static int safe_concat_argv(char *dest, size_t dest_size, int argc, char **argv, int start_idx) {
    if (!dest || !argv || dest_size == 0) return -1;

    dest[0] = '\0';

    for (int i = start_idx; i < argc; i++) {
        if (i > start_idx) {
            if (safe_strcat(dest, dest_size, " ") < 0) return -1;
        }
        if (safe_strcat(dest, dest_size, argv[i]) < 0) return -1;
    }

    return (int)strlen(dest);
}

/* Safe port parsing with validation */
static int safe_parse_port(const char *str, uint16_t *out) {
    if (!str || !out) return -1;

    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);

    if (errno != 0 || endptr == str || *endptr != '\0') {
        return -1;
    }

    if (val < 0 || val > 65535) return -1;

    *out = (uint16_t)val;
    return 0;
}

/* Safe integer parsing with validation */
static int safe_parse_int(const char *str, int *out) {
    if (!str || !out) return -1;

    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);

    if (errno != 0 || endptr == str || *endptr != '\0') {
        return -1;
    }

    if (val < INT_MIN || val > INT_MAX) return -1;

    *out = (int)val;
    return 0;
}

/* Safe unsigned integer parsing */
static int safe_parse_uint(const char *str, unsigned int *out) {
    if (!str || !out) return -1;

    char *endptr;
    errno = 0;
    unsigned long val = strtoul(str, &endptr, 10);

    if (errno != 0 || endptr == str || *endptr != '\0') {
        return -1;
    }

    if (val > UINT_MAX) return -1;

    *out = (unsigned int)val;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FORWARD DECLARATIONS - BUILT-IN COMMANDS
 * ══════════════════════════════════════════════════════════════════════════════ */

static shell_result_t cmd_help(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_exit(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_pwd(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_cd(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_ls(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_cat(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_echo(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_mkdir(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_touch(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_write(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_hide(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_cp(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_mv(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_find(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_versions(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_restore(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_ps(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_suspend(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_resume(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_stat(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_history(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_alias(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_set(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_constitution(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_geology(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_mount(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_ln(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_clear(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_service(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_governor(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_ai(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_net(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_user(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_pkg(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_time(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_browse(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_web(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_notes(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_view(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_monitor(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_storage(struct shell_context *ctx, int argc, char **argv);
static shell_result_t cmd_dnauth(struct shell_context *ctx, int argc, char **argv);

/* ══════════════════════════════════════════════════════════════════════════════
 * BUILT-IN COMMAND TABLE
 * ══════════════════════════════════════════════════════════════════════════════ */

static const struct shell_command builtin_commands[] = {
    /* Navigation & Information */
    { "help",        "Show help for commands",
      "help [command]", cmd_help, 0, 1 },
    { "pwd",         "Print working directory",
      "pwd", cmd_pwd, 0, 0 },
    { "cd",          "Change directory",
      "cd <path>", cmd_cd, 0, 1 },
    { "ls",          "List directory contents",
      "ls [-l] [-a] [path]", cmd_ls, 0, 3 },
    { "stat",        "Show file information",
      "stat <path>", cmd_stat, 1, 1 },

    /* File Operations (Creative Only) */
    { "cat",         "Display file contents",
      "cat <file>", cmd_cat, 1, 1 },
    { "echo",        "Print text",
      "echo [text...]", cmd_echo, 0, -1 },
    { "mkdir",       "Create directory",
      "mkdir <path>", cmd_mkdir, 1, 1 },
    { "touch",       "Create empty file",
      "touch <path>", cmd_touch, 1, 1 },
    { "write",       "Write text to file (append)",
      "write <file> <text>", cmd_write, 2, -1 },
    { "ln",          "Create symbolic link",
      "ln -s <target> <link>", cmd_ln, 2, 3 },

    /* Phantom-Specific: Hide (NOT Delete) */
    { "hide",        "Hide file (preserved in geology)",
      "hide <path>", cmd_hide, 1, 1 },

    /* Copy, Move, Search (Phantom-Safe) */
    { "cp",          "Copy file (content preserved)",
      "cp <source> <dest>", cmd_cp, 2, 2 },
    { "mv",          "Move/rename file (original preserved in geology)",
      "mv <source> <dest>", cmd_mv, 2, 2 },
    { "find",        "Search for files",
      "find [path] <pattern>", cmd_find, 1, 2 },
    { "versions",    "Show file version history",
      "versions <path>", cmd_versions, 1, 1 },
    { "restore",     "Restore file from history",
      "restore <path> <view_id> [dest]", cmd_restore, 2, 3 },

    /* Process Management (Suspend, NOT Kill) */
    { "ps",          "List processes",
      "ps", cmd_ps, 0, 0 },
    { "suspend",     "Suspend a process (not kill!)",
      "suspend <pid>", cmd_suspend, 1, 1 },
    { "resume",      "Resume a suspended process",
      "resume <pid>", cmd_resume, 1, 1 },

    /* System Information */
    { "constitution","Display the Phantom Constitution",
      "constitution", cmd_constitution, 0, 0 },
    { "geology",     "Show geological storage info",
      "geology", cmd_geology, 0, 0 },
    { "mount",       "Show mounted filesystems",
      "mount", cmd_mount, 0, 0 },

    /* Service Management */
    { "service",     "Manage system services",
      "service [list|status|awaken|rest] [name]", cmd_service, 0, 2 },

    /* Governor (AI Code Evaluator) */
    { "governor",    "Manage the AI Governor",
      "governor [status|stats|mode|test|cache|history|scope] [args]", cmd_governor, 0, 5 },

    /* AI Assistant */
    { "ai",          "AI assistant and code helper",
      "ai [chat|ask|explain|generate|config] [args]", cmd_ai, 0, -1 },

    /* Network (Phantom-Safe) */
    { "net",         "Phantom network operations",
      "net [status|connect|send|recv|suspend|resume|list] [args]", cmd_net, 0, -1 },

    /* User Management (Phantom-Safe: users become dormant, never deleted) */
    { "user",        "User and permission management",
      "user [list|add|info|lock|unlock|dormant|passwd|perm] [args]", cmd_user, 0, -1 },

    /* Package Management (Phantom-Safe: packages archived, never uninstalled) */
    { "pkg",         "Phantom package manager",
      "pkg [list|info|install|archive|restore|search] [args]", cmd_pkg, 0, -1 },

    /* Temporal Engine (Time Travel!) */
    { "time",        "Time travel and temporal queries",
      "time [events|diff|at|history|snapshot|activity] [args]", cmd_time, 0, -1 },

    /* AI Web Browser (Pages preserved forever!) */
    { "browse",      "AI-powered web browser",
      "browse [go|back|tabs|history|bookmark|ai|stats] [args]", cmd_browse, 0, -1 },
    { "web",         "Governor-controlled web browser",
      "web [go|allow|block|status|config|stats] [args]", cmd_web, 0, -1 },

    /* Built-in Applications */
    { "notes",       "Note-taking with version history",
      "notes [list|new|edit|view|history|search|archive|restore] [args]", cmd_notes, 0, -1 },
    { "view",        "File viewer (read-only)",
      "view <file> | view info | view hex", cmd_view, 0, -1 },
    { "monitor",     "System monitor",
      "monitor [summary|procs|mem|geo|net|gov]", cmd_monitor, 0, -1 },

    /* Storage Management */
    { "storage",     "Storage quota, monitoring, and backup",
      "storage [status|quota|backup|restore|archive] [args]", cmd_storage, 0, -1 },

    /* DNAuth - DNA-based Authentication */
    { "dnauth",      "DNA-based authentication system",
      "dnauth [status|register|auth|evolve|lineage|fitness] [args]", cmd_dnauth, 0, -1 },

    /* Shell Features */
    { "history",     "Show command history",
      "history [n]", cmd_history, 0, 1 },
    { "alias",       "Create command alias",
      "alias [name=value]", cmd_alias, 0, 1 },
    { "set",         "Set shell variable",
      "set name=value", cmd_set, 0, 1 },
    { "clear",       "Clear screen",
      "clear", cmd_clear, 0, 0 },
    { "exit",        "Exit shell (shell preserved)",
      "exit", cmd_exit, 0, 0 },

    { NULL, NULL, NULL, NULL, 0, 0 }  /* Sentinel */
};

/* ══════════════════════════════════════════════════════════════════════════════
 * INITIALIZATION & CLEANUP
 * ══════════════════════════════════════════════════════════════════════════════ */

shell_result_t shell_init(struct shell_context *ctx,
                          struct phantom_kernel *kernel,
                          struct vfs_context *vfs) {
    if (!ctx || !kernel || !vfs) {
        return SHELL_ERR_ARGS;
    }

    memset(ctx, 0, sizeof(*ctx));

    ctx->kernel = kernel;
    ctx->vfs = vfs;
    ctx->running = 1;
    ctx->interactive = 1;
    strcpy(ctx->cwd, "/");

    /* Allocate history */
    ctx->history_capacity = SHELL_HISTORY_SIZE;
    ctx->history = calloc(ctx->history_capacity, sizeof(struct shell_history_entry));
    if (!ctx->history) {
        return SHELL_ERR_NOMEM;
    }

    /* Set default variables */
    shell_set_var(ctx, "SHELL", "/bin/phantom-shell");
    shell_set_var(ctx, "PS1", "phantom");
    shell_set_var(ctx, "VERSION", "1.0");

    /* Set default aliases */
    shell_set_alias(ctx, "ll", "ls -l");
    shell_set_alias(ctx, "la", "ls -a");

    return SHELL_OK;
}

void shell_cleanup(struct shell_context *ctx) {
    if (!ctx) return;

    /* Logout if still logged in */
    if (ctx->user_system && ctx->session) {
        phantom_user_logout(ctx->user_system, ctx->session->session_id);
    }

    /* History is preserved in GeoFS - we just free local memory */
    free(ctx->history);
    ctx->history = NULL;
}

void shell_set_user_system(struct shell_context *ctx, phantom_user_system_t *user_sys) {
    if (ctx) {
        ctx->user_system = user_sys;
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * LOGIN SYSTEM
 * ══════════════════════════════════════════════════════════════════════════════ */

static void print_login_banner(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                   ║\n");
    printf("║                        P H A N T O M O S                          ║\n");
    printf("║                                                                   ║\n");
    printf("║                   \"To Create, Not To Destroy\"                     ║\n");
    printf("║                                                                   ║\n");
    printf("║         All actions are logged. Nothing is ever deleted.          ║\n");
    printf("║                                                                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

static int read_password(char *buf, size_t len) {
    /* Simple password reading - in production would disable echo */
    printf("Password: ");
    fflush(stdout);

    if (!fgets(buf, (int)len, stdin)) {
        return -1;
    }

    /* Remove newline */
    size_t plen = strlen(buf);
    if (plen > 0 && buf[plen - 1] == '\n') {
        buf[plen - 1] = '\0';
    }

    return 0;
}

int shell_login(struct shell_context *ctx) {
    if (!ctx || !ctx->user_system) {
        fprintf(stderr, "Error: User system not initialized\n");
        return -1;
    }

    print_login_banner();

    int attempts = 0;
    const int max_attempts = 3;

    while (attempts < max_attempts) {
        char username[PHANTOM_MAX_USERNAME];
        char password[256];

        /* Get username */
        printf("Username: ");
        fflush(stdout);

        if (!fgets(username, sizeof(username), stdin)) {
            printf("\n");
            return -1;  /* EOF or error */
        }

        /* Remove newline */
        size_t len = strlen(username);
        if (len > 0 && username[len - 1] == '\n') {
            username[len - 1] = '\0';
        }

        /* Empty username - check for exit */
        if (username[0] == '\0') {
            continue;
        }

        /* Get password */
        if (read_password(password, sizeof(password)) < 0) {
            printf("\n");
            return -1;
        }

        /* Authenticate */
        phantom_session_t *session = NULL;
        int result = phantom_user_authenticate(ctx->user_system, username, password, &session);

        /* Clear password from memory */
        memset(password, 0, sizeof(password));

        if (result == USER_OK && session) {
            /* Login successful */
            ctx->session = session;
            ctx->uid = session->uid;
            strncpy(ctx->username, username, PHANTOM_MAX_USERNAME - 1);
            ctx->username[PHANTOM_MAX_USERNAME - 1] = '\0';

            /* Update home directory based on user */
            phantom_user_t *user = phantom_user_find_by_uid(ctx->user_system, ctx->uid);
            if (user && user->home_dir[0]) {
                strncpy(ctx->cwd, user->home_dir, SHELL_MAX_PATH - 1);
            }

            printf("\n");
            printf("  Welcome, %s!\n", user ? user->full_name : username);
            printf("  Login time: %s", ctime(&session->started_at));
            if (user && user->last_login > 0 && user->total_logins > 1) {
                printf("  Last login: %s", ctime(&user->last_login));
            }
            printf("\n");

            return 0;  /* Success */
        }

        /* Login failed */
        attempts++;
        printf("\n");

        switch (result) {
            case USER_ERR_NOT_FOUND:
                printf("  Login failed: Unknown user\n");
                break;
            case USER_ERR_BAD_PASSWORD:
                printf("  Login failed: Incorrect password\n");
                break;
            case USER_ERR_LOCKED:
                printf("  Login failed: Account locked (too many failed attempts)\n");
                printf("  Please try again later.\n");
                return -1;
            case USER_ERR_DORMANT:
                printf("  Login failed: Account is dormant (deactivated)\n");
                printf("  Contact administrator to reactivate.\n");
                return -1;
            case USER_ERR_DENIED:
                printf("  Login failed: Account suspended\n");
                return -1;
            default:
                printf("  Login failed: %s\n", phantom_user_result_string(result));
                break;
        }

        if (attempts < max_attempts) {
            printf("  Attempts remaining: %d\n", max_attempts - attempts);
        }
        printf("\n");
    }

    printf("  Maximum login attempts exceeded.\n");
    printf("  Session terminated.\n\n");
    return -1;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * COMMAND PARSING
 * ══════════════════════════════════════════════════════════════════════════════ */

int shell_parse_line(char *line, char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    int in_quotes = 0;
    char quote_char = 0;

    while (*p && argc < max_args) {
        /* Skip whitespace */
        while (*p && isspace(*p)) p++;
        if (!*p) break;

        /* Check for quotes */
        if (*p == '"' || *p == '\'') {
            quote_char = *p;
            in_quotes = 1;
            p++;
            argv[argc++] = p;

            /* Find closing quote */
            while (*p && *p != quote_char) p++;
            if (*p) *p++ = '\0';
            in_quotes = 0;
        } else {
            /* Regular argument */
            argv[argc++] = p;
            while (*p && !isspace(*p)) p++;
            if (*p) *p++ = '\0';
        }
    }

    argv[argc] = NULL;
    return argc;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * ALIAS & VARIABLE EXPANSION
 * ══════════════════════════════════════════════════════════════════════════════ */

shell_result_t shell_set_var(struct shell_context *ctx,
                              const char *name, const char *value) {
    if (!ctx || !name) return SHELL_ERR_ARGS;

    /* Check if variable exists */
    for (size_t i = 0; i < ctx->var_count; i++) {
        if (strcmp(ctx->vars[i].name, name) == 0) {
            strncpy(ctx->vars[i].value, value ? value : "",
                    sizeof(ctx->vars[i].value) - 1);
            return SHELL_OK;
        }
    }

    /* Add new variable */
    if (ctx->var_count >= SHELL_MAX_VARS) {
        return SHELL_ERR_NOMEM;
    }

    strncpy(ctx->vars[ctx->var_count].name, name,
            sizeof(ctx->vars[0].name) - 1);
    strncpy(ctx->vars[ctx->var_count].value, value ? value : "",
            sizeof(ctx->vars[0].value) - 1);
    ctx->var_count++;

    return SHELL_OK;
}

const char *shell_get_var(struct shell_context *ctx, const char *name) {
    if (!ctx || !name) return NULL;

    for (size_t i = 0; i < ctx->var_count; i++) {
        if (strcmp(ctx->vars[i].name, name) == 0) {
            return ctx->vars[i].value;
        }
    }
    return NULL;
}

shell_result_t shell_set_alias(struct shell_context *ctx,
                                const char *name, const char *value) {
    if (!ctx || !name) return SHELL_ERR_ARGS;

    /* Check if alias exists */
    for (size_t i = 0; i < ctx->alias_count; i++) {
        if (strcmp(ctx->aliases[i].name, name) == 0) {
            strncpy(ctx->aliases[i].value, value ? value : "",
                    sizeof(ctx->aliases[i].value) - 1);
            return SHELL_OK;
        }
    }

    /* Add new alias */
    if (ctx->alias_count >= SHELL_MAX_ALIASES) {
        return SHELL_ERR_NOMEM;
    }

    strncpy(ctx->aliases[ctx->alias_count].name, name,
            sizeof(ctx->aliases[0].name) - 1);
    strncpy(ctx->aliases[ctx->alias_count].value, value ? value : "",
            sizeof(ctx->aliases[0].value) - 1);
    ctx->alias_count++;

    return SHELL_OK;
}

shell_result_t shell_expand_aliases(struct shell_context *ctx,
                                     char *line, size_t max_len) {
    if (!ctx || !line) return SHELL_ERR_ARGS;

    /* Get first word */
    char *space = strchr(line, ' ');
    size_t cmd_len = space ? (size_t)(space - line) : strlen(line);
    char cmd[64];

    if (cmd_len >= sizeof(cmd)) return SHELL_OK;

    strncpy(cmd, line, cmd_len);
    cmd[cmd_len] = '\0';

    /* Look for alias */
    for (size_t i = 0; i < ctx->alias_count; i++) {
        if (strcmp(ctx->aliases[i].name, cmd) == 0) {
            /* Expand alias */
            char new_line[SHELL_MAX_INPUT];
            size_t val_len = strlen(ctx->aliases[i].value);
            size_t rest_len = space ? strlen(space) : 0;

            if (val_len + rest_len >= max_len) {
                return SHELL_ERR_NOMEM;
            }

            strcpy(new_line, ctx->aliases[i].value);
            if (space) {
                strcat(new_line, space);
            }
            strcpy(line, new_line);
            break;
        }
    }

    return SHELL_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * HISTORY MANAGEMENT
 * ══════════════════════════════════════════════════════════════════════════════ */

shell_result_t shell_history_add(struct shell_context *ctx,
                                  const char *command,
                                  shell_result_t result) {
    if (!ctx || !command) return SHELL_ERR_ARGS;

    /* History is append-only - we never remove entries */
    if (ctx->history_count >= ctx->history_capacity) {
        /* Expand history */
        size_t new_cap = ctx->history_capacity * 2;
        struct shell_history_entry *new_hist = realloc(ctx->history,
            new_cap * sizeof(struct shell_history_entry));
        if (!new_hist) {
            /* Can't expand - oldest entries are preserved, newest are lost */
            return SHELL_ERR_NOMEM;
        }
        ctx->history = new_hist;
        ctx->history_capacity = new_cap;
    }

    struct shell_history_entry *entry = &ctx->history[ctx->history_count];
    strncpy(entry->command, command, sizeof(entry->command) - 1);
    entry->result = result;
    entry->pid = ctx->pid;

    /* Get current time */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    entry->executed_at = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    ctx->history_count++;
    return SHELL_OK;
}

const struct shell_history_entry *shell_history_get(struct shell_context *ctx,
                                                     size_t index) {
    if (!ctx || index >= ctx->history_count) return NULL;
    return &ctx->history[index];
}

const struct shell_history_entry *shell_history_search(struct shell_context *ctx,
                                                        const char *pattern) {
    if (!ctx || !pattern) return NULL;

    /* Search backwards (most recent first) */
    for (size_t i = ctx->history_count; i > 0; i--) {
        if (strstr(ctx->history[i-1].command, pattern)) {
            return &ctx->history[i-1];
        }
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PATH RESOLUTION
 * ══════════════════════════════════════════════════════════════════════════════ */

static void resolve_path(struct shell_context *ctx, const char *path,
                         char *resolved, size_t size) {
    if (size == 0) return;

    if (path[0] == '/') {
        /* Absolute path */
        size_t len = strlen(path);
        if (len >= size) len = size - 1;
        memcpy(resolved, path, len);
        resolved[len] = '\0';
    } else {
        /* Relative path - build in temp buffer to avoid truncation warnings */
        size_t cwd_len = strlen(ctx->cwd);
        size_t path_len = strlen(path);
        size_t total_len;

        if (cwd_len == 1 && ctx->cwd[0] == '/') {
            /* Root directory: "/<path>" */
            total_len = 1 + path_len;
            if (total_len >= size) total_len = size - 1;
            resolved[0] = '/';
            size_t copy_len = (path_len < size - 1) ? path_len : size - 1;
            memcpy(resolved + 1, path, copy_len);
            resolved[1 + copy_len] = '\0';
        } else {
            /* Non-root: "<cwd>/<path>" */
            total_len = cwd_len + 1 + path_len;
            if (total_len >= size) {
                /* Truncate to fit */
                memcpy(resolved, ctx->cwd, (cwd_len < size - 1) ? cwd_len : size - 1);
                if (cwd_len < size - 1) {
                    resolved[cwd_len] = '/';
                    size_t remain = size - cwd_len - 2;
                    if (remain > 0 && path_len > 0) {
                        size_t copy_len = (path_len < remain) ? path_len : remain;
                        memcpy(resolved + cwd_len + 1, path, copy_len);
                        resolved[cwd_len + 1 + copy_len] = '\0';
                    } else {
                        resolved[cwd_len + 1] = '\0';
                    }
                } else {
                    resolved[size - 1] = '\0';
                }
            } else {
                memcpy(resolved, ctx->cwd, cwd_len);
                resolved[cwd_len] = '/';
                memcpy(resolved + cwd_len + 1, path, path_len);
                resolved[cwd_len + 1 + path_len] = '\0';
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * COMMAND EXECUTION
 * ══════════════════════════════════════════════════════════════════════════════ */

shell_result_t shell_execute(struct shell_context *ctx, const char *line) {
    if (!ctx || !line) return SHELL_ERR_ARGS;

    /* Skip empty lines and comments */
    const char *p = line;
    while (*p && isspace(*p)) p++;
    if (!*p || *p == '#') return SHELL_OK;

    /* Make a mutable copy */
    char buf[SHELL_MAX_INPUT];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Expand aliases */
    shell_expand_aliases(ctx, buf, sizeof(buf));

    /* Parse into arguments */
    char *argv[SHELL_MAX_ARGS];
    int argc = shell_parse_line(buf, argv, SHELL_MAX_ARGS);

    if (argc == 0) return SHELL_OK;

    /* Find command */
    const struct shell_command *cmd = NULL;
    for (const struct shell_command *c = builtin_commands; c->name; c++) {
        if (strcmp(c->name, argv[0]) == 0) {
            cmd = c;
            break;
        }
    }

    if (!cmd) {
        printf("phantom: command not found: %s\n", argv[0]);
        printf("Type 'help' for available commands.\n");
        return SHELL_ERR_NOTFOUND;
    }

    /* Check argument count */
    int arg_count = argc - 1;
    if (arg_count < cmd->min_args) {
        printf("Usage: %s\n", cmd->usage);
        return SHELL_ERR_ARGS;
    }
    if (cmd->max_args >= 0 && arg_count > cmd->max_args) {
        printf("Too many arguments. Usage: %s\n", cmd->usage);
        return SHELL_ERR_ARGS;
    }

    /* Execute command */
    shell_result_t result = cmd->handler(ctx, argc, argv);

    /* Update statistics */
    ctx->commands_executed++;
    if (result == SHELL_OK) {
        ctx->commands_successful++;
    } else if (result != SHELL_EXIT) {
        ctx->commands_failed++;
    }

    /* Add to history */
    shell_history_add(ctx, line, result);

    ctx->last_result = result;
    return result;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PROMPT & MAIN LOOP
 * ══════════════════════════════════════════════════════════════════════════════ */

void shell_print_prompt(struct shell_context *ctx) {
    const char *user = ctx->username[0] ? ctx->username : "phantom";

    /* Color: green for success, red for failure */
    /* Format: user@phantom:cwd$ */
    if (ctx->last_result == SHELL_OK) {
        printf("\033[32m%s@phantom\033[0m:%s$ ", user, ctx->cwd);
    } else {
        printf("\033[31m%s@phantom\033[0m:%s$ ", user, ctx->cwd);
    }
    fflush(stdout);
}

shell_result_t shell_run(struct shell_context *ctx) {
    if (!ctx) return SHELL_ERR_ARGS;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                     PHANTOM SHELL v1.0\n");
    printf("                  \"To Create, Not To Destroy\"\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("Type 'help' for commands. Type 'constitution' for our principles.\n");
    printf("Note: There is no 'rm', 'kill', or 'delete'. This is by design.\n");
    printf("\n");

    while (ctx->running) {
        shell_print_prompt(ctx);

        if (!fgets(ctx->input_buffer, sizeof(ctx->input_buffer), stdin)) {
            /* EOF */
            printf("\n");
            break;
        }

        /* Remove trailing newline */
        size_t len = strlen(ctx->input_buffer);
        if (len > 0 && ctx->input_buffer[len-1] == '\n') {
            ctx->input_buffer[len-1] = '\0';
        }

        shell_result_t result = shell_execute(ctx, ctx->input_buffer);
        if (result == SHELL_EXIT) {
            break;
        }
    }

    printf("\nShell session preserved in geology. Goodbye!\n");
    return SHELL_OK;
}

shell_result_t shell_run_script(struct shell_context *ctx, const char *path) {
    if (!ctx || !path) return SHELL_ERR_ARGS;

    /* Open script file */
    vfs_fd_t fd = vfs_open(ctx->vfs, ctx->pid, path, VFS_O_RDONLY, 0);
    if (fd < 0) {
        printf("Cannot open script: %s\n", path);
        return SHELL_ERR_IO;
    }

    char line[SHELL_MAX_INPUT];
    char c;
    size_t pos = 0;
    ssize_t n;

    ctx->interactive = 0;

    while ((n = vfs_read(ctx->vfs, fd, &c, 1)) > 0) {
        if (c == '\n' || pos >= sizeof(line) - 1) {
            line[pos] = '\0';
            shell_result_t result = shell_execute(ctx, line);
            if (result == SHELL_EXIT) {
                break;
            }
            pos = 0;
        } else {
            line[pos++] = c;
        }
    }

    /* Handle last line without newline */
    if (pos > 0) {
        line[pos] = '\0';
        shell_execute(ctx, line);
    }

    vfs_close(ctx->vfs, fd);
    ctx->interactive = 1;
    return SHELL_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

const char *shell_strerror(shell_result_t err) {
    switch (err) {
        case SHELL_OK:          return "Success";
        case SHELL_ERR_NOTFOUND: return "Command not found";
        case SHELL_ERR_ARGS:    return "Invalid arguments";
        case SHELL_ERR_PERM:    return "Permission denied";
        case SHELL_ERR_IO:      return "I/O error";
        case SHELL_ERR_SYNTAX:  return "Syntax error";
        case SHELL_ERR_NOMEM:   return "Out of memory";
        case SHELL_ERR_DECLINED: return "Governor declined";
        case SHELL_EXIT:        return "Exit requested";
        default:                return "Unknown error";
    }
}

const struct shell_command *shell_get_builtins(size_t *count) {
    if (count) {
        *count = 0;
        for (const struct shell_command *c = builtin_commands; c->name; c++) {
            (*count)++;
        }
    }
    return builtin_commands;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * BUILT-IN COMMAND IMPLEMENTATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static shell_result_t cmd_help(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;

    if (argc > 1) {
        /* Help for specific command */
        for (const struct shell_command *c = builtin_commands; c->name; c++) {
            if (strcmp(c->name, argv[1]) == 0) {
                printf("\n%s - %s\n", c->name, c->help);
                printf("Usage: %s\n\n", c->usage);
                return SHELL_OK;
            }
        }
        printf("Unknown command: %s\n", argv[1]);
        return SHELL_ERR_NOTFOUND;
    }

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                     PHANTOM SHELL COMMANDS\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("Navigation & Information:\n");
    printf("  help [cmd]     Show help for commands\n");
    printf("  pwd            Print working directory\n");
    printf("  cd <path>      Change directory\n");
    printf("  ls [-la] [p]   List directory contents\n");
    printf("  stat <path>    Show file information\n");
    printf("\n");
    printf("File Operations (Creative Only):\n");
    printf("  cat <file>     Display file contents\n");
    printf("  echo [text]    Print text\n");
    printf("  mkdir <path>   Create directory\n");
    printf("  touch <path>   Create empty file\n");
    printf("  write <f> <t>  Write text to file (append)\n");
    printf("  ln -s <t> <l>  Create symbolic link\n");
    printf("\n");
    printf("Phantom Operations (Hide, NOT Delete):\n");
    printf("  hide <path>    Hide file (preserved in geology)\n");
    printf("\n");
    printf("Process Management (Suspend, NOT Kill):\n");
    printf("  ps             List processes\n");
    printf("  suspend <pid>  Suspend process\n");
    printf("  resume <pid>   Resume process\n");
    printf("\n");
    printf("System Information:\n");
    printf("  constitution   Display the Phantom Constitution\n");
    printf("  geology        Show geological storage info\n");
    printf("  mount          Show mounted filesystems\n");
    printf("\n");
    printf("Service Management:\n");
    printf("  service        Manage system services (list/status/awaken/rest)\n");
    printf("\n");
    printf("Shell Features:\n");
    printf("  history [n]    Show command history\n");
    printf("  alias [n=v]    Create command alias\n");
    printf("  set [n=v]      Set shell variable\n");
    printf("  clear          Clear screen\n");
    printf("  exit           Exit shell\n");
    printf("\n");
    printf("Note: Commands like 'rm', 'kill', 'delete' do not exist.\n");
    printf("      This is the Prime Directive: To Create, Not To Destroy.\n");
    printf("\n");

    return SHELL_OK;
}

static shell_result_t cmd_exit(struct shell_context *ctx, int argc, char **argv) {
    (void)argc;
    (void)argv;
    ctx->running = 0;
    return SHELL_EXIT;
}

static shell_result_t cmd_pwd(struct shell_context *ctx, int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("%s\n", ctx->cwd);
    return SHELL_OK;
}

static shell_result_t cmd_cd(struct shell_context *ctx, int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/";

    if (strcmp(path, "..") == 0) {
        /* Go up one directory */
        char *last_slash = strrchr(ctx->cwd, '/');
        if (last_slash && last_slash != ctx->cwd) {
            *last_slash = '\0';
        } else {
            strcpy(ctx->cwd, "/");
        }
        return SHELL_OK;
    }

    char resolved[SHELL_MAX_PATH];
    resolve_path(ctx, path, resolved, sizeof(resolved));

    /* Verify path exists and is a directory */
    struct vfs_stat st;
    vfs_error_t err = vfs_stat(ctx->vfs, resolved, &st);
    if (err != VFS_OK) {
        printf("cd: %s: No such directory\n", path);
        return SHELL_ERR_IO;
    }
    if (st.type != VFS_TYPE_DIRECTORY) {
        printf("cd: %s: Not a directory\n", path);
        return SHELL_ERR_ARGS;
    }

    strncpy(ctx->cwd, resolved, sizeof(ctx->cwd) - 1);
    return SHELL_OK;
}

static shell_result_t cmd_ls(struct shell_context *ctx, int argc, char **argv) {
    int show_long = 0;
    int show_all = 0;
    const char *path = NULL;

    /* Parse options */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (char *c = &argv[i][1]; *c; c++) {
                if (*c == 'l') show_long = 1;
                else if (*c == 'a') show_all = 1;
            }
        } else {
            path = argv[i];
        }
    }

    char resolved[SHELL_MAX_PATH];
    if (path) {
        resolve_path(ctx, path, resolved, sizeof(resolved));
    } else {
        strncpy(resolved, ctx->cwd, sizeof(resolved));
    }

    /* Open directory */
    vfs_fd_t fd = vfs_open(ctx->vfs, ctx->pid, resolved,
                           VFS_O_RDONLY | VFS_O_DIRECTORY, 0);
    if (fd < 0) {
        printf("ls: cannot access '%s': %s\n", resolved, vfs_strerror(fd));
        return SHELL_ERR_IO;
    }

    /* Read entries */
    struct vfs_dirent entries[64];
    size_t count;

    vfs_error_t err = vfs_readdir(ctx->vfs, fd, entries, 64, &count);
    if (err != VFS_OK) {
        vfs_close(ctx->vfs, fd);
        printf("ls: cannot read directory\n");
        return SHELL_ERR_IO;
    }

    /* Print entries */
    for (size_t i = 0; i < count; i++) {
        /* Skip hidden files unless -a */
        if (!show_all && entries[i].name[0] == '.') {
            continue;
        }

        if (show_long) {
            /* Long format */
            char type_char;
            switch (entries[i].type) {
                case VFS_TYPE_DIRECTORY: type_char = 'd'; break;
                case VFS_TYPE_SYMLINK:   type_char = 'l'; break;
                case VFS_TYPE_DEVICE:    type_char = 'c'; break;
                case VFS_TYPE_PIPE:      type_char = 'p'; break;
                case VFS_TYPE_SOCKET:    type_char = 's'; break;
                default:                 type_char = '-'; break;
            }
            printf("%crw-r--r--  1 phantom phantom  0  %s\n",
                   type_char, entries[i].name);
        } else {
            /* Short format */
            if (entries[i].type == VFS_TYPE_DIRECTORY) {
                printf("\033[34m%s/\033[0m  ", entries[i].name);
            } else if (entries[i].type == VFS_TYPE_SYMLINK) {
                printf("\033[36m%s@\033[0m  ", entries[i].name);
            } else {
                printf("%s  ", entries[i].name);
            }
        }
    }

    if (!show_long && count > 0) {
        printf("\n");
    }

    vfs_close(ctx->vfs, fd);
    return SHELL_OK;
}

static shell_result_t cmd_cat(struct shell_context *ctx, int argc, char **argv) {
    char resolved[SHELL_MAX_PATH];
    resolve_path(ctx, argv[1], resolved, sizeof(resolved));

    vfs_fd_t fd = vfs_open(ctx->vfs, ctx->pid, resolved, VFS_O_RDONLY, 0);
    if (fd < 0) {
        printf("cat: %s: %s\n", argv[1], vfs_strerror(fd));
        return SHELL_ERR_IO;
    }

    char buf[1024];
    ssize_t n;
    while ((n = vfs_read(ctx->vfs, fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    vfs_close(ctx->vfs, fd);
    return SHELL_OK;
}

static shell_result_t cmd_echo(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;

    for (int i = 1; i < argc; i++) {
        if (i > 1) printf(" ");
        printf("%s", argv[i]);
    }
    printf("\n");
    return SHELL_OK;
}

static shell_result_t cmd_mkdir(struct shell_context *ctx, int argc, char **argv) {
    char resolved[SHELL_MAX_PATH];
    resolve_path(ctx, argv[1], resolved, sizeof(resolved));

    vfs_error_t err = vfs_mkdir(ctx->vfs, ctx->pid, resolved, 0755);
    if (err != VFS_OK) {
        printf("mkdir: cannot create '%s': %s\n", argv[1], vfs_strerror(err));
        return SHELL_ERR_IO;
    }

    printf("Created directory: %s\n", resolved);
    return SHELL_OK;
}

static shell_result_t cmd_touch(struct shell_context *ctx, int argc, char **argv) {
    char resolved[SHELL_MAX_PATH];
    resolve_path(ctx, argv[1], resolved, sizeof(resolved));

    vfs_fd_t fd = vfs_open(ctx->vfs, ctx->pid, resolved,
                           VFS_O_WRONLY | VFS_O_CREATE, 0644);
    if (fd < 0) {
        printf("touch: cannot create '%s': %s\n", argv[1], vfs_strerror(fd));
        return SHELL_ERR_IO;
    }

    vfs_close(ctx->vfs, fd);
    printf("Created file: %s\n", resolved);
    return SHELL_OK;
}

static shell_result_t cmd_write(struct shell_context *ctx, int argc, char **argv) {
    char resolved[SHELL_MAX_PATH];
    resolve_path(ctx, argv[1], resolved, sizeof(resolved));

    vfs_fd_t fd = vfs_open(ctx->vfs, ctx->pid, resolved,
                           VFS_O_WRONLY | VFS_O_APPEND | VFS_O_CREATE, 0644);
    if (fd < 0) {
        printf("write: cannot open '%s': %s\n", argv[1], vfs_strerror(fd));
        return SHELL_ERR_IO;
    }

    /* Write remaining arguments as text */
    for (int i = 2; i < argc; i++) {
        if (i > 2) vfs_write(ctx->vfs, fd, " ", 1);
        vfs_write(ctx->vfs, fd, argv[i], strlen(argv[i]));
    }
    vfs_write(ctx->vfs, fd, "\n", 1);

    vfs_close(ctx->vfs, fd);
    printf("Appended to: %s\n", resolved);
    return SHELL_OK;
}

static shell_result_t cmd_hide(struct shell_context *ctx, int argc, char **argv) {
    char resolved[SHELL_MAX_PATH];
    resolve_path(ctx, argv[1], resolved, sizeof(resolved));

    vfs_error_t err = vfs_hide(ctx->vfs, ctx->pid, resolved);
    if (err != VFS_OK) {
        printf("hide: cannot hide '%s': %s\n", argv[1], vfs_strerror(err));
        return SHELL_ERR_IO;
    }

    printf("Hidden: %s\n", resolved);
    printf("(File preserved in geology, accessible via time-travel)\n");
    return SHELL_OK;
}

static shell_result_t cmd_cp(struct shell_context *ctx, int argc, char **argv) {
    (void)argc;

    char src_resolved[SHELL_MAX_PATH];
    char dst_resolved[SHELL_MAX_PATH];
    resolve_path(ctx, argv[1], src_resolved, sizeof(src_resolved));
    resolve_path(ctx, argv[2], dst_resolved, sizeof(dst_resolved));

    vfs_error_t err = vfs_copy(ctx->vfs, ctx->pid, src_resolved, dst_resolved);
    if (err != VFS_OK) {
        printf("cp: cannot copy '%s' to '%s': %s\n", argv[1], argv[2], vfs_strerror(err));
        return SHELL_ERR_IO;
    }

    printf("Copied: %s -> %s\n", src_resolved, dst_resolved);
    return SHELL_OK;
}

static shell_result_t cmd_mv(struct shell_context *ctx, int argc, char **argv) {
    (void)argc;

    char src_resolved[SHELL_MAX_PATH];
    char dst_resolved[SHELL_MAX_PATH];
    resolve_path(ctx, argv[1], src_resolved, sizeof(src_resolved));
    resolve_path(ctx, argv[2], dst_resolved, sizeof(dst_resolved));

    vfs_error_t err = vfs_rename(ctx->vfs, ctx->pid, src_resolved, dst_resolved);
    if (err != VFS_OK) {
        printf("mv: cannot move '%s' to '%s': %s\n", argv[1], argv[2], vfs_strerror(err));
        return SHELL_ERR_IO;
    }

    printf("Moved: %s -> %s\n", src_resolved, dst_resolved);
    printf("(Original preserved in geology)\n");
    return SHELL_OK;
}

/* Search callback for cmd_find */
static void find_callback(const char *path, struct vfs_stat *stat, void *user_ctx) {
    (void)user_ctx;
    const char *type_str = "";
    switch (stat->type) {
        case VFS_TYPE_DIRECTORY: type_str = "[dir]  "; break;
        case VFS_TYPE_SYMLINK:   type_str = "[link] "; break;
        default:                 type_str = "[file] "; break;
    }
    printf("  %s%s\n", type_str, path);
}

static shell_result_t cmd_find(struct shell_context *ctx, int argc, char **argv) {
    char resolved[SHELL_MAX_PATH];
    const char *start_path;
    const char *pattern;

    if (argc == 2) {
        /* find <pattern> - search from current dir */
        start_path = ctx->cwd;
        pattern = argv[1];
    } else {
        /* find <path> <pattern> */
        resolve_path(ctx, argv[1], resolved, sizeof(resolved));
        start_path = resolved;
        pattern = argv[2];
    }

    printf("\nSearching for '%s' in %s:\n", pattern, start_path);
    printf("────────────────────────────────────────────────────────────────\n");

    vfs_error_t err = vfs_search(ctx->vfs, start_path, pattern, find_callback, NULL);
    if (err != VFS_OK) {
        printf("find: error searching: %s\n", vfs_strerror(err));
        return SHELL_ERR_IO;
    }

    printf("\n");
    return SHELL_OK;
}

static shell_result_t cmd_versions(struct shell_context *ctx, int argc, char **argv) {
    (void)argc;

    char resolved[SHELL_MAX_PATH];
    resolve_path(ctx, argv[1], resolved, sizeof(resolved));

    vfs_file_version_t versions[32];
    size_t count;

    vfs_error_t err = vfs_get_history(ctx->vfs, resolved, versions, 32, &count);
    if (err != VFS_OK) {
        printf("versions: cannot get history for '%s': %s\n", argv[1], vfs_strerror(err));
        return SHELL_ERR_IO;
    }

    if (count == 0) {
        printf("No version history found for: %s\n", resolved);
        printf("(File may not be on GeoFS mount)\n");
        return SHELL_OK;
    }

    printf("\n");
    printf("Version history for: %s\n", resolved);
    printf("════════════════════════════════════════════════════════════════════════════════\n");
    printf("  VIEW ID     LABEL                SIZE       HASH (first 16 chars)\n");
    printf("────────────────────────────────────────────────────────────────────────────────\n");

    for (size_t i = 0; i < count; i++) {
        char hash_short[17];
        strncpy(hash_short, versions[i].content_hash, 16);
        hash_short[16] = '\0';

        printf("  %-10lu  %-20s %-10lu %s...\n",
               versions[i].view_id,
               versions[i].view_label[0] ? versions[i].view_label : "(unnamed)",
               versions[i].size,
               hash_short);
    }

    printf("\n");
    printf("Use 'restore %s <view_id> [dest]' to restore a version\n", argv[1]);
    printf("\n");
    return SHELL_OK;
}

static shell_result_t cmd_restore(struct shell_context *ctx, int argc, char **argv) {
    char src_resolved[SHELL_MAX_PATH];
    resolve_path(ctx, argv[1], src_resolved, sizeof(src_resolved));

    uint64_t view_id = strtoull(argv[2], NULL, 10);
    if (view_id == 0) {
        printf("restore: invalid view ID '%s'\n", argv[2]);
        return SHELL_ERR_NOTFOUND;
    }

    /* Destination path - extra space for .restored suffix */
    char dst_resolved[SHELL_MAX_PATH + 16];
    if (argc >= 4) {
        resolve_path(ctx, argv[3], dst_resolved, sizeof(dst_resolved));
    } else {
        /* Restore to same name with .restored suffix */
        snprintf(dst_resolved, sizeof(dst_resolved), "%s.restored", src_resolved);
    }

    vfs_error_t err = vfs_restore_version(ctx->vfs, ctx->pid, src_resolved, view_id, dst_resolved);
    if (err != VFS_OK) {
        printf("restore: cannot restore '%s' from view %lu: %s\n",
               argv[1], view_id, vfs_strerror(err));
        return SHELL_ERR_IO;
    }

    printf("Restored: %s (view %lu) -> %s\n", src_resolved, view_id, dst_resolved);
    return SHELL_OK;
}

static shell_result_t cmd_ps(struct shell_context *ctx, int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("  PID   STATE      NAME\n");
    printf("──────────────────────────────────────────────────────────────\n");

    struct phantom_process *proc = ctx->kernel->processes;
    while (proc) {
        const char *state;
        switch (proc->state) {
            case PROCESS_EMBRYO:  state = "embryo "; break;
            case PROCESS_READY:   state = "ready  "; break;
            case PROCESS_RUNNING: state = "running"; break;
            case PROCESS_BLOCKED: state = "blocked"; break;
            case PROCESS_DORMANT: state = "dormant"; break;
            default:              state = "unknown"; break;
        }
        printf("  %-5lu %s  %s\n", proc->pid, state, proc->name);
        proc = proc->next;
    }
    printf("\n");

    return SHELL_OK;
}

static shell_result_t cmd_suspend(struct shell_context *ctx, int argc, char **argv) {
    phantom_pid_t pid = strtoul(argv[1], NULL, 10);

    phantom_error_t err = phantom_process_suspend(ctx->kernel, pid);
    if (err != PHANTOM_OK) {
        printf("suspend: cannot suspend PID %lu\n", pid);
        return SHELL_ERR_IO;
    }

    printf("Suspended process %lu (preserved, can resume later)\n", pid);
    return SHELL_OK;
}

static shell_result_t cmd_resume(struct shell_context *ctx, int argc, char **argv) {
    phantom_pid_t pid = strtoul(argv[1], NULL, 10);

    phantom_error_t err = phantom_process_resume(ctx->kernel, pid);
    if (err != PHANTOM_OK) {
        printf("resume: cannot resume PID %lu\n", pid);
        return SHELL_ERR_IO;
    }

    printf("Resumed process %lu\n", pid);
    return SHELL_OK;
}

static shell_result_t cmd_stat(struct shell_context *ctx, int argc, char **argv) {
    char resolved[SHELL_MAX_PATH];
    resolve_path(ctx, argv[1], resolved, sizeof(resolved));

    struct vfs_stat st;
    vfs_error_t err = vfs_stat(ctx->vfs, resolved, &st);
    if (err != VFS_OK) {
        printf("stat: cannot stat '%s': %s\n", argv[1], vfs_strerror(err));
        return SHELL_ERR_IO;
    }

    const char *type;
    switch (st.type) {
        case VFS_TYPE_REGULAR:   type = "regular file"; break;
        case VFS_TYPE_DIRECTORY: type = "directory"; break;
        case VFS_TYPE_SYMLINK:   type = "symbolic link"; break;
        case VFS_TYPE_DEVICE:    type = "device"; break;
        case VFS_TYPE_PIPE:      type = "pipe"; break;
        case VFS_TYPE_SOCKET:    type = "socket"; break;
        default:                 type = "unknown"; break;
    }

    printf("  File: %s\n", resolved);
    printf("  Type: %s\n", type);
    printf("  Size: %lu bytes\n", st.size);
    printf(" Inode: %lu\n", st.ino);
    printf(" Links: %u\n", st.nlink);

    return SHELL_OK;
}

static shell_result_t cmd_history(struct shell_context *ctx, int argc, char **argv) {
    size_t n = ctx->history_count;
    if (argc > 1) {
        n = strtoul(argv[1], NULL, 10);
        if (n > ctx->history_count) n = ctx->history_count;
    }

    size_t start = (ctx->history_count > n) ? ctx->history_count - n : 0;

    printf("\n");
    for (size_t i = start; i < ctx->history_count; i++) {
        printf("  %4zu  %s\n", i + 1, ctx->history[i].command);
    }
    printf("\n");

    return SHELL_OK;
}

static shell_result_t cmd_alias(struct shell_context *ctx, int argc, char **argv) {
    if (argc == 1) {
        /* List aliases */
        printf("\n");
        for (size_t i = 0; i < ctx->alias_count; i++) {
            printf("  alias %s='%s'\n",
                   ctx->aliases[i].name, ctx->aliases[i].value);
        }
        printf("\n");
        return SHELL_OK;
    }

    /* Parse name=value */
    char *eq = strchr(argv[1], '=');
    if (!eq) {
        printf("Usage: alias name=value\n");
        return SHELL_ERR_SYNTAX;
    }

    *eq = '\0';
    shell_set_alias(ctx, argv[1], eq + 1);
    printf("Alias set: %s='%s'\n", argv[1], eq + 1);

    return SHELL_OK;
}

static shell_result_t cmd_set(struct shell_context *ctx, int argc, char **argv) {
    if (argc == 1) {
        /* List variables */
        printf("\n");
        for (size_t i = 0; i < ctx->var_count; i++) {
            printf("  %s=%s\n", ctx->vars[i].name, ctx->vars[i].value);
        }
        printf("\n");
        return SHELL_OK;
    }

    /* Parse name=value */
    char *eq = strchr(argv[1], '=');
    if (!eq) {
        printf("Usage: set name=value\n");
        return SHELL_ERR_SYNTAX;
    }

    *eq = '\0';
    shell_set_var(ctx, argv[1], eq + 1);
    printf("Variable set: %s=%s\n", argv[1], eq + 1);

    return SHELL_OK;
}

static shell_result_t cmd_constitution(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    (void)argc;
    (void)argv;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                  THE PHANTOM CONSTITUTION\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("PREAMBLE\n");
    printf("  This operating system exists to create, protect, and preserve.\n");
    printf("  The ability to destroy has been architecturally removed.\n");
    printf("\n");
    printf("ARTICLE I: The Prime Directive\n");
    printf("  \"To Create, Not To Destroy\"\n");
    printf("  No operation shall remove, delete, or destroy any data.\n");
    printf("\n");
    printf("ARTICLE II: The Geology\n");
    printf("  All data exists in geological strata.\n");
    printf("  Old versions remain accessible forever.\n");
    printf("  \"Deletion\" means hiding, not destroying.\n");
    printf("\n");
    printf("ARTICLE III: The Governor\n");
    printf("  All code must be evaluated before execution.\n");
    printf("  Destructive code shall not be signed.\n");
    printf("  The Governor's values are architectural, not configurable.\n");
    printf("\n");
    printf("ARTICLE IV: Hardware Enforcement\n");
    printf("  Destructive instructions do not exist.\n");
    printf("  All writes are appends.\n");
    printf("  The constitution cannot be amended by software.\n");
    printf("\n");
    printf("ARTICLE V: Transparency\n");
    printf("  All operations are logged permanently.\n");
    printf("  All code is attributable.\n");
    printf("  Nothing happens without a record.\n");
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");

    return SHELL_OK;
}

static shell_result_t cmd_geology(struct shell_context *ctx, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (!ctx->kernel->geofs_volume) {
        printf("No GeoFS volume mounted.\n");
        return SHELL_OK;
    }

    geofs_volume_t *vol = ctx->kernel->geofs_volume;

    /* Get current view */
    geofs_view_t current_view = geofs_view_current(vol);

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                    GEOLOGICAL STORAGE STATUS\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  GeoFS Volume:     Active\n");
    printf("  Current View:     %lu\n", current_view);
    printf("\n");
    printf("  Kernel Statistics:\n");
    printf("    Total bytes created: %lu\n", ctx->kernel->total_bytes_created);
    printf("    Total syscalls:      %lu\n", ctx->kernel->total_syscalls);
    printf("    Processes ever:      %lu\n", ctx->kernel->total_processes_ever);
    printf("\n");
    printf("  Philosophy: Data accumulates like geological strata.\n");
    printf("              Each layer preserves its predecessors.\n");
    printf("              Nothing is ever eroded or destroyed.\n");
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");

    return SHELL_OK;
}

static shell_result_t cmd_mount(struct shell_context *ctx, int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    struct vfs_mount *mount = ctx->vfs->mounts;
    while (mount) {
        printf("  %s on %s type %s\n",
               "none",
               mount->mount_path,
               mount->sb && mount->sb->fs_type ? mount->sb->fs_type->name : "unknown");
        mount = mount->next;
    }
    printf("\n");

    return SHELL_OK;
}

static shell_result_t cmd_ln(struct shell_context *ctx, int argc, char **argv) {
    const char *target;
    const char *link;

    if (argc == 4 && strcmp(argv[1], "-s") == 0) {
        target = argv[2];
        link = argv[3];
    } else if (argc == 3) {
        target = argv[1];
        link = argv[2];
    } else {
        printf("Usage: ln -s <target> <link>\n");
        return SHELL_ERR_ARGS;
    }

    char resolved[SHELL_MAX_PATH];
    resolve_path(ctx, link, resolved, sizeof(resolved));

    vfs_error_t err = vfs_symlink(ctx->vfs, ctx->pid, target, resolved);
    if (err != VFS_OK) {
        printf("ln: cannot create link '%s': %s\n", link, vfs_strerror(err));
        return SHELL_ERR_IO;
    }

    printf("Created symlink: %s -> %s\n", resolved, target);
    return SHELL_OK;
}

static shell_result_t cmd_clear(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    (void)argc;
    (void)argv;

    /* ANSI escape to clear screen */
    printf("\033[2J\033[H");
    return SHELL_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * SERVICE MANAGEMENT COMMAND
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Callback for service listing */
static void service_list_callback(phantom_service_t *svc, void *ctx) {
    (void)ctx;

    const char *state;
    const char *color;
    switch (svc->state) {
        case SERVICE_EMBRYO:    state = "embryo";    color = "\033[33m"; break;
        case SERVICE_STARTING:  state = "starting";  color = "\033[33m"; break;
        case SERVICE_RUNNING:   state = "running";   color = "\033[32m"; break;
        case SERVICE_DORMANT:   state = "dormant";   color = "\033[90m"; break;
        case SERVICE_AWAKENING: state = "awakening"; color = "\033[36m"; break;
        case SERVICE_BLOCKED:   state = "blocked";   color = "\033[31m"; break;
        default:                state = "unknown";   color = "\033[0m";  break;
    }

    printf("  %-16s %s%-10s\033[0m  %s\n",
           svc->name, color, state, svc->description);
}

static shell_result_t cmd_service(struct shell_context *ctx, int argc, char **argv) {
    phantom_init_t *init = ctx->kernel->init;

    if (!init) {
        printf("service: init system not available\n");
        return SHELL_ERR_IO;
    }

    const char *action = (argc > 1) ? argv[1] : "list";
    const char *name = (argc > 2) ? argv[2] : NULL;

    if (strcmp(action, "list") == 0) {
        printf("\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("                      PHANTOM SERVICES\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("\n");
        printf("  SERVICE          STATE       DESCRIPTION\n");
        printf("──────────────────────────────────────────────────────────────\n");

        init_service_list(init, service_list_callback, NULL);

        printf("\n");
        printf("  Use 'service status <name>' for details\n");
        printf("  Use 'service awaken <name>' to start a service\n");
        printf("  Use 'service rest <name>' to stop a service\n");
        printf("\n");
        return SHELL_OK;
    }

    if (strcmp(action, "status") == 0) {
        if (!name) {
            printf("Usage: service status <name>\n");
            return SHELL_ERR_ARGS;
        }

        phantom_service_t *svc = init_service_find(init, name);
        if (!svc) {
            printf("service: '%s' not found\n", name);
            return SHELL_ERR_NOTFOUND;
        }

        const char *state;
        switch (svc->state) {
            case SERVICE_EMBRYO:    state = "embryo"; break;
            case SERVICE_STARTING:  state = "starting"; break;
            case SERVICE_RUNNING:   state = "running"; break;
            case SERVICE_DORMANT:   state = "dormant"; break;
            case SERVICE_AWAKENING: state = "awakening"; break;
            case SERVICE_BLOCKED:   state = "blocked"; break;
            default:                state = "unknown"; break;
        }

        const char *type;
        switch (svc->type) {
            case SERVICE_TYPE_SIMPLE:  type = "simple"; break;
            case SERVICE_TYPE_DAEMON:  type = "daemon"; break;
            case SERVICE_TYPE_ONESHOT: type = "oneshot"; break;
            case SERVICE_TYPE_MONITOR: type = "monitor"; break;
            default:                   type = "unknown"; break;
        }

        printf("\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("  Service: %s\n", svc->name);
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("\n");
        printf("  Description:  %s\n", svc->description[0] ? svc->description : "(none)");
        printf("  State:        %s\n", state);
        printf("  Type:         %s\n", type);
        printf("  ID:           %lu\n", svc->service_id);
        printf("\n");
        printf("  Statistics:\n");
        printf("    Awakenings: %lu\n", svc->start_count);
        if (svc->command[0]) {
            printf("    Command:    %s\n", svc->command);
        }
        if (svc->dependency_count > 0) {
            printf("    Depends on: ");
            for (int i = 0; i < svc->dependency_count; i++) {
                if (i > 0) printf(", ");
                printf("%s", svc->dependencies[i]);
            }
            printf("\n");
        }
        printf("\n");
        return SHELL_OK;
    }

    if (strcmp(action, "awaken") == 0) {
        if (!name) {
            printf("Usage: service awaken <name>\n");
            return SHELL_ERR_ARGS;
        }

        if (init_service_awaken(init, name) < 0) {
            printf("service: failed to awaken '%s'\n", name);
            return SHELL_ERR_IO;
        }

        printf("Service '%s' has been awakened.\n", name);
        return SHELL_OK;
    }

    if (strcmp(action, "rest") == 0) {
        if (!name) {
            printf("Usage: service rest <name>\n");
            return SHELL_ERR_ARGS;
        }

        if (init_service_rest(init, name) < 0) {
            printf("service: failed to rest '%s'\n", name);
            return SHELL_ERR_IO;
        }

        printf("Service '%s' is now dormant.\n", name);
        return SHELL_OK;
    }

    printf("Unknown action: %s\n", action);
    printf("Usage: service [list|status|awaken|rest] [name]\n");
    return SHELL_ERR_ARGS;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * GOVERNOR COMMAND
 * ══════════════════════════════════════════════════════════════════════════════ */

static shell_result_t cmd_governor(struct shell_context *ctx, int argc, char **argv) {
    phantom_governor_t *gov = ctx->kernel->governor;

    if (!gov) {
        printf("governor: Governor system not available\n");
        return SHELL_ERR_IO;
    }

    const char *action = (argc > 1) ? argv[1] : "status";

    /* Show current status */
    if (strcmp(action, "status") == 0) {
        printf("\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("                      PHANTOM GOVERNOR\n");
        printf("                  \"To Create, Not To Destroy\"\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("\n");
        printf("  The Governor is the AI judge that evaluates all code before\n");
        printf("  execution. Per Article III of the Phantom Constitution:\n");
        printf("  \"The AI Governor judges all code before it runs\"\n");
        printf("\n");
        printf("  Status:      %s\n", ctx->kernel->governor_enabled ? "ACTIVE" : "DISABLED");
        printf("  Mode:        %s\n", gov->interactive ? "Interactive" : "Automatic");
        printf("  Policy:      %s\n", gov->strict_mode ? "Strict" : "Permissive");
        printf("  Cache:       %s\n", gov->cache_enabled ? "ON" : "OFF");
        printf("\n");
        printf("  Decisions:\n");
        printf("    Auto-approved:  %lu\n", gov->auto_approved);
        printf("    User-approved:  %lu\n", gov->user_approved);
        printf("    User-declined:  %lu\n", gov->user_declined);
        printf("    Auto-declined:  %lu\n", gov->auto_declined);
        printf("\n");
        if (gov->cache_enabled) {
            uint64_t total = gov->cache_hits + gov->cache_misses;
            float hit_rate = total > 0 ? (float)gov->cache_hits * 100.0f / total : 0.0f;
            printf("  Cache: %lu hits, %lu misses (%.1f%% hit rate)\n",
                   gov->cache_hits, gov->cache_misses, hit_rate);
        }
        printf("  History: %d entries (max %d)\n", gov->history_count, GOVERNOR_HISTORY_SIZE);
        printf("  Scopes:  %d active (max %d)\n", gov->scope_count, GOVERNOR_MAX_SCOPES);
        printf("\n");
        printf("  Commands:\n");
        printf("    governor stats           - Detailed statistics\n");
        printf("    governor mode <mode>     - Change mode\n");
        printf("    governor test <code>     - Test code evaluation\n");
        printf("    governor cache <action>  - Manage evaluation cache\n");
        printf("    governor history <action>- View/rollback decisions\n");
        printf("    governor scope <action>  - Manage capability scopes\n");
        printf("\n");
        return SHELL_OK;
    }

    /* Detailed statistics */
    if (strcmp(action, "stats") == 0) {
        governor_print_stats(gov);
        return SHELL_OK;
    }

    /* Change mode */
    if (strcmp(action, "mode") == 0) {
        if (argc < 3) {
            printf("Current mode: %s, %s\n",
                   gov->interactive ? "Interactive" : "Automatic",
                   gov->strict_mode ? "Strict" : "Permissive");
            printf("\nAvailable modes:\n");
            printf("  interactive - Prompt user for uncertain cases\n");
            printf("  auto        - Auto-decide without prompting\n");
            printf("  strict      - Decline anything uncertain\n");
            printf("  permissive  - Allow medium-risk without prompting\n");
            printf("\nUsage: governor mode <mode>\n");
            return SHELL_OK;
        }

        const char *mode = argv[2];

        if (strcmp(mode, "interactive") == 0) {
            governor_set_interactive(gov, 1);
            printf("Governor mode: Interactive (will prompt for uncertain cases)\n");
        } else if (strcmp(mode, "auto") == 0) {
            governor_set_interactive(gov, 0);
            printf("Governor mode: Automatic (no prompts)\n");
        } else if (strcmp(mode, "strict") == 0) {
            governor_set_strict(gov, 1);
            printf("Governor mode: Strict (decline uncertain cases)\n");
        } else if (strcmp(mode, "permissive") == 0) {
            governor_set_strict(gov, 0);
            printf("Governor mode: Permissive\n");
        } else {
            printf("Unknown mode: %s\n", mode);
            printf("Available modes: interactive, auto, strict, permissive\n");
            return SHELL_ERR_ARGS;
        }
        return SHELL_OK;
    }

    /* Test code evaluation */
    if (strcmp(action, "test") == 0) {
        if (argc < 3) {
            printf("Usage: governor test \"<code>\"\n");
            printf("\nExamples:\n");
            printf("  governor test \"int main() { create_file(); }\"\n");
            printf("  governor test \"delete_all_files();\"\n");
            return SHELL_ERR_ARGS;
        }

        /* Concatenate remaining args as code (with safe bounds checking) */
        char code[1024] = {0};
        if (safe_concat_argv(code, sizeof(code), argc, argv, 2) < 0) {
            printf("Error: Code too long (max %zu chars)\n", sizeof(code) - 1);
            return SHELL_ERR_ARGS;
        }

        printf("\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("                    GOVERNOR CODE TEST\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("\n");
        printf("  Code: %s\n", code);
        printf("\n");

        /* Create evaluation request */
        governor_eval_request_t req = {0};
        governor_eval_response_t resp = {0};

        req.code_ptr = code;
        req.code_size = strlen(code);
        strncpy(req.name, "shell-test", 255);
        strncpy(req.description, "Code submitted via governor test command", 1023);

        /* Temporarily disable interactive mode for test */
        int was_interactive = gov->interactive;
        gov->interactive = 0;

        int err = governor_evaluate_code(gov, &req, &resp);

        gov->interactive = was_interactive;

        if (err != 0) {
            printf("  Error: Evaluation failed\n");
            return SHELL_ERR_IO;
        }

        /* Show results */
        printf("  Analysis:\n");
        printf("    Threat level:  %s\n", governor_threat_to_string(req.threat_level));

        if (req.detected_caps != 0) {
            char caps_buf[256];
            governor_caps_to_list(req.detected_caps, caps_buf, sizeof(caps_buf));
            printf("    Capabilities:  %s\n", caps_buf);
        }

        if (req.threat_reason_count > 0) {
            printf("    Reasons:\n");
            for (int i = 0; i < req.threat_reason_count; i++) {
                printf("      - %s\n", req.threat_reasons[i]);
            }
        }

        printf("\n");
        printf("  Decision: %s\n",
               resp.decision == GOVERNOR_APPROVE ? "APPROVED" : "DECLINED");
        printf("  Summary:  %s\n", resp.summary);
        printf("  By:       %s\n", resp.decision_by);

        if (resp.decision == GOVERNOR_DECLINE && resp.alternatives[0]) {
            printf("\n  Alternatives:\n    %s\n", resp.alternatives);
        }

        printf("\n");
        return SHELL_OK;
    }

    /* Enable/disable Governor */
    if (strcmp(action, "enable") == 0) {
        ctx->kernel->governor_enabled = 1;
        printf("Governor enabled.\n");
        return SHELL_OK;
    }

    if (strcmp(action, "disable") == 0) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║                         WARNING                                ║\n");
        printf("╠═══════════════════════════════════════════════════════════════╣\n");
        printf("║  Disabling the Governor removes code safety checks.            ║\n");
        printf("║  This is against the Phantom Constitution Article III.         ║\n");
        printf("║                                                                 ║\n");
        printf("║  The Governor cannot be fully disabled - this will only       ║\n");
        printf("║  switch to fallback pattern matching instead of the full      ║\n");
        printf("║  capability-based analysis.                                    ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("Are you sure? (yes/no): ");
        fflush(stdout);

        char response[16];
        if (fgets(response, sizeof(response), stdin)) {
            if (strncmp(response, "yes", 3) == 0) {
                ctx->kernel->governor_enabled = 0;
                printf("Governor using fallback mode (basic pattern matching only).\n");
            } else {
                printf("Governor remains active.\n");
            }
        }
        return SHELL_OK;
    }

    /* Cache management */
    if (strcmp(action, "cache") == 0) {
        const char *cache_action = (argc > 2) ? argv[2] : "status";

        if (strcmp(cache_action, "status") == 0) {
            governor_print_cache_stats(gov);
            return SHELL_OK;
        }

        if (strcmp(cache_action, "clear") == 0) {
            governor_clear_cache(gov);
            return SHELL_OK;
        }

        if (strcmp(cache_action, "enable") == 0) {
            governor_enable_cache(gov, 1);
            return SHELL_OK;
        }

        if (strcmp(cache_action, "disable") == 0) {
            governor_enable_cache(gov, 0);
            return SHELL_OK;
        }

        printf("Usage: governor cache [status|clear|enable|disable]\n");
        return SHELL_ERR_ARGS;
    }

    /* History management */
    if (strcmp(action, "history") == 0) {
        const char *hist_action = (argc > 2) ? argv[2] : "show";

        if (strcmp(hist_action, "show") == 0) {
            int max = (argc > 3) ? atoi(argv[3]) : 10;
            governor_print_history(gov, max);
            return SHELL_OK;
        }

        if (strcmp(hist_action, "rollback") == 0) {
            if (argc < 4) {
                printf("Usage: governor history rollback <index>\n");
                printf("Use 'governor history show' to see available indices.\n");
                return SHELL_ERR_ARGS;
            }
            int index = atoi(argv[3]);
            if (governor_rollback(gov, index) == 0) {
                printf("Rollback successful.\n");
            } else {
                printf("Rollback failed.\n");
                return SHELL_ERR_IO;
            }
            return SHELL_OK;
        }

        if (strcmp(hist_action, "count") == 0) {
            printf("History entries: %d\n", governor_history_count(gov));
            return SHELL_OK;
        }

        printf("Usage: governor history [show [max]|rollback <index>|count]\n");
        return SHELL_ERR_ARGS;
    }

    /* Capability scope management */
    if (strcmp(action, "scope") == 0) {
        const char *scope_action = (argc > 2) ? argv[2] : "list";

        if (strcmp(scope_action, "list") == 0) {
            governor_print_scopes(gov);
            return SHELL_OK;
        }

        if (strcmp(scope_action, "add") == 0) {
            if (argc < 5) {
                printf("Usage: governor scope add <capability> <path_pattern> [max_bytes] [duration_sec]\n");
                printf("\nCapabilities:\n");
                printf("  network      - Network access\n");
                printf("  write_files  - Write to files\n");
                printf("  read_files   - Read files\n");
                printf("  create_files - Create new files\n");
                printf("  raw_device   - Device access\n");
                printf("\nExamples:\n");
                printf("  governor scope add write_files \"/home/user/*\" 1048576 3600\n");
                printf("  (Allow writing to /home/user/*, max 1MB per file, for 1 hour)\n");
                return SHELL_ERR_ARGS;
            }

            uint32_t cap = 0;
            const char *cap_name = argv[3];
            if (strcmp(cap_name, "network") == 0) cap = CAP_NETWORK;
            else if (strcmp(cap_name, "write_files") == 0) cap = CAP_WRITE_FILES;
            else if (strcmp(cap_name, "read_files") == 0) cap = CAP_READ_FILES;
            else if (strcmp(cap_name, "create_files") == 0) cap = CAP_CREATE_FILES;
            else if (strcmp(cap_name, "raw_device") == 0) cap = CAP_RAW_DEVICE;
            else if (strcmp(cap_name, "system_config") == 0) cap = CAP_SYSTEM_CONFIG;
            else {
                printf("Unknown capability: %s\n", cap_name);
                return SHELL_ERR_ARGS;
            }

            const char *path_pattern = argv[4];
            uint64_t max_bytes = (argc > 5) ? strtoull(argv[5], NULL, 10) : 0;
            uint64_t duration = (argc > 6) ? strtoull(argv[6], NULL, 10) : 0;

            int idx = governor_add_scope(gov, cap, path_pattern, max_bytes, duration);
            if (idx >= 0) {
                printf("Created scope index %d\n", idx);
            } else {
                printf("Failed to create scope (max %d scopes)\n", GOVERNOR_MAX_SCOPES);
                return SHELL_ERR_IO;
            }
            return SHELL_OK;
        }

        if (strcmp(scope_action, "remove") == 0) {
            if (argc < 4) {
                printf("Usage: governor scope remove <index>\n");
                return SHELL_ERR_ARGS;
            }
            int index = atoi(argv[3]);
            if (governor_remove_scope(gov, index) == 0) {
                printf("Scope removed.\n");
            } else {
                printf("Failed to remove scope.\n");
                return SHELL_ERR_IO;
            }
            return SHELL_OK;
        }

        if (strcmp(scope_action, "cleanup") == 0) {
            int removed = governor_cleanup_scopes(gov);
            printf("Cleaned up %d expired scopes.\n", removed);
            return SHELL_OK;
        }

        printf("Usage: governor scope [list|add|remove|cleanup] [args]\n");
        return SHELL_ERR_ARGS;
    }

    /* Behavioral analysis */
    if (strcmp(action, "analyze") == 0) {
        if (argc < 3) {
            printf("Usage: governor analyze \"<code>\"\n");
            printf("\nPerforms behavioral analysis on code to detect suspicious patterns.\n");
            printf("This is a deeper analysis than simple pattern matching.\n");
            printf("\nExamples:\n");
            printf("  governor analyze \"while(1) { fork(); }\"\n");
            printf("  governor analyze \"for(;;) { malloc(1000000); }\"\n");
            return SHELL_ERR_ARGS;
        }

        /* Concatenate remaining args as code (with safe bounds checking) */
        char code[2048] = {0};
        if (safe_concat_argv(code, sizeof(code), argc, argv, 2) < 0) {
            printf("Error: Code too long (max %zu chars)\n", sizeof(code) - 1);
            return SHELL_ERR_ARGS;
        }

        printf("\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("                  BEHAVIORAL ANALYSIS\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("\n");
        printf("  Code: %s\n", code);
        printf("\n");

        governor_behavior_result_t result;
        if (governor_analyze_behavior(code, strlen(code), &result) != 0) {
            printf("  Error: Analysis failed\n");
            return SHELL_ERR_IO;
        }

        printf("  Suspicious Score: %d/100\n", result.suspicious_score);

        if (result.flags == BEHAVIOR_NONE) {
            printf("  Result: \033[32mNo suspicious behaviors detected\033[0m\n");
        } else {
            printf("  Result: \033[31mSuspicious behaviors detected\033[0m\n");
            printf("\n  Detected patterns:\n");
            for (int i = 0; i < result.description_count; i++) {
                printf("    - %s\n", result.descriptions[i]);
            }

            printf("\n  Behavior flags:");
            if (result.flags & BEHAVIOR_INFINITE_LOOP)  printf(" infinite_loop");
            if (result.flags & BEHAVIOR_MEMORY_BOMB)    printf(" memory_bomb");
            if (result.flags & BEHAVIOR_FORK_BOMB)      printf(" fork_bomb");
            if (result.flags & BEHAVIOR_OBFUSCATION)    printf(" obfuscation");
            if (result.flags & BEHAVIOR_ENCODED_PAYLOAD) printf(" encoded_payload");
            if (result.flags & BEHAVIOR_SHELL_INJECTION) printf(" shell_injection");
            if (result.flags & BEHAVIOR_PATH_TRAVERSAL) printf(" path_traversal");
            if (result.flags & BEHAVIOR_RESOURCE_EXHAUST) printf(" resource_exhaust");
            if (result.flags & BEHAVIOR_LOOP_DESTRUCTION) printf(" loop_destruction");
            printf("\n");
        }

        printf("\n");
        return SHELL_OK;
    }

    printf("Unknown action: %s\n", action);
    printf("Usage: governor [status|stats|mode|test|cache|history|scope|enable|disable] [args]\n");
    return SHELL_ERR_ARGS;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * AI ASSISTANT COMMAND
 * ══════════════════════════════════════════════════════════════════════════════ */

static shell_result_t cmd_ai(struct shell_context *ctx, int argc, char **argv) {
    if (!ctx || !ctx->kernel) return SHELL_ERR_ARGS;

    /* Get or create AI context */
    phantom_ai_t *ai = (phantom_ai_t *)ctx->kernel->ai;

    if (argc < 2) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║                    PHANTOM AI ASSISTANT                        ║\n");
        printf("║              \"To Create, Not To Destroy\"                       ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("Commands:\n");
        printf("  ai chat <message>     Chat with AI assistant\n");
        printf("  ai ask <question>     Ask a question about PhantomOS\n");
        printf("  ai explain <topic>    Explain a concept or error\n");
        printf("  ai generate <desc>    Generate Phantom-compliant code\n");
        printf("  ai suggest <task>     Suggest shell command for task\n");
        printf("  ai review <code>      Review code for Phantom compliance\n");
        printf("  ai config             Configure AI settings\n");
        printf("  ai status             Show AI subsystem status\n");
        printf("\n");
        printf("The AI assistant follows the Phantom Constitution.\n");
        printf("It will NEVER suggest destructive operations.\n");
        printf("\n");

        if (!ai) {
            printf("Status: AI not initialized\n");
            printf("  Use 'ai config init' to initialize the AI subsystem.\n");
        } else {
            printf("Status: %s\n", phantom_ai_is_connected(ai) ? "Connected" : "Not connected");
        }
        printf("\n");
        return SHELL_OK;
    }

    const char *action = argv[1];

    /* Initialize AI if needed */
    if (strcmp(action, "config") == 0) {
        if (argc < 3) {
            printf("\n");
            printf("AI Configuration:\n");
            printf("  ai config init [provider]  Initialize AI (local, anthropic, openai)\n");
            printf("  ai config key <api-key>    Set API key\n");
            printf("  ai config model <name>     Set model name\n");
            printf("  ai config safety <level>   Set safety (strict, standard, minimal)\n");
            printf("\n");

            if (ai) {
                printf("Current configuration:\n");
                printf("  Provider: %s\n",
                       ai->config.provider == PHANTOM_AI_PROVIDER_LOCAL ? "Local (Ollama)" :
                       ai->config.provider == PHANTOM_AI_PROVIDER_ANTHROPIC ? "Anthropic" :
                       ai->config.provider == PHANTOM_AI_PROVIDER_OPENAI ? "OpenAI" : "Custom");
                printf("  Model: %s\n", ai->config.model_name);
                printf("  Safety: %s\n",
                       ai->config.safety == PHANTOM_AI_SAFETY_STRICT ? "Strict" :
                       ai->config.safety == PHANTOM_AI_SAFETY_STANDARD ? "Standard" : "Minimal");
                printf("  Connected: %s\n", phantom_ai_is_connected(ai) ? "Yes" : "No");
            }
            return SHELL_OK;
        }

        const char *subaction = argv[2];

        if (strcmp(subaction, "init") == 0) {
            /* Allocate AI context if needed */
            if (!ai) {
                ai = malloc(sizeof(phantom_ai_t));
                if (!ai) {
                    printf("Error: Failed to allocate AI context\n");
                    return SHELL_ERR_NOMEM;
                }
                ctx->kernel->ai = ai;
            }

            phantom_ai_config_t config = {0};
            config.provider = PHANTOM_AI_PROVIDER_LOCAL;
            config.capabilities = PHANTOM_AI_CAP_ALL;
            config.safety = PHANTOM_AI_SAFETY_STANDARD;
            config.max_tokens = 2048;
            config.temperature = 0.7f;
            config.timeout_ms = 30000;
            config.local_port = 11434;
            strncpy(config.model_name, "llama2", PHANTOM_AI_MODEL_NAME_LEN - 1);

            /* Check for provider argument */
            if (argc > 3) {
                if (strcmp(argv[3], "anthropic") == 0) {
                    config.provider = PHANTOM_AI_PROVIDER_ANTHROPIC;
                    strncpy(config.model_name, "claude-3-haiku-20240307",
                            PHANTOM_AI_MODEL_NAME_LEN - 1);
                } else if (strcmp(argv[3], "openai") == 0) {
                    config.provider = PHANTOM_AI_PROVIDER_OPENAI;
                    strncpy(config.model_name, "gpt-3.5-turbo",
                            PHANTOM_AI_MODEL_NAME_LEN - 1);
                } else if (strcmp(argv[3], "local") == 0) {
                    config.provider = PHANTOM_AI_PROVIDER_LOCAL;
                }
            }

            if (phantom_ai_init(ai, ctx->kernel, &config) == 0) {
                printf("AI subsystem initialized successfully.\n");
                phantom_ai_connect(ai);

                /* Connect to Governor if available */
                if (ctx->kernel->governor) {
                    governor_set_ai(ctx->kernel->governor, ai);
                    governor_enable_ai(ctx->kernel->governor, 1);
                    printf("AI integrated with Governor for enhanced code analysis.\n");
                }
            } else {
                printf("Error: Failed to initialize AI subsystem\n");
                free(ai);
                ctx->kernel->ai = NULL;
            }
            return SHELL_OK;
        }

        if (!ai) {
            printf("Error: AI not initialized. Use 'ai config init' first.\n");
            return SHELL_ERR_ARGS;
        }

        if (strcmp(subaction, "key") == 0) {
            if (argc < 4) {
                printf("Usage: ai config key <api-key>\n");
                return SHELL_ERR_ARGS;
            }
            phantom_ai_set_api_key(ai, argv[3]);
            printf("API key configured.\n");
            return SHELL_OK;
        }

        if (strcmp(subaction, "model") == 0) {
            if (argc < 4) {
                printf("Usage: ai config model <name>\n");
                return SHELL_ERR_ARGS;
            }
            phantom_ai_set_model(ai, argv[3]);
            printf("Model set to: %s\n", argv[3]);
            return SHELL_OK;
        }

        if (strcmp(subaction, "safety") == 0) {
            if (argc < 4) {
                printf("Usage: ai config safety <strict|standard|minimal>\n");
                return SHELL_ERR_ARGS;
            }
            if (strcmp(argv[3], "strict") == 0) {
                phantom_ai_set_safety(ai, PHANTOM_AI_SAFETY_STRICT);
            } else if (strcmp(argv[3], "standard") == 0) {
                phantom_ai_set_safety(ai, PHANTOM_AI_SAFETY_STANDARD);
            } else if (strcmp(argv[3], "minimal") == 0) {
                phantom_ai_set_safety(ai, PHANTOM_AI_SAFETY_MINIMAL);
            } else {
                printf("Unknown safety level: %s\n", argv[3]);
                return SHELL_ERR_ARGS;
            }
            printf("Safety level set to: %s\n", argv[3]);
            return SHELL_OK;
        }

        printf("Unknown config option: %s\n", subaction);
        return SHELL_ERR_ARGS;
    }

    /* All other commands require AI to be initialized */
    if (!ai) {
        printf("\n");
        printf("AI not initialized. Run 'ai config init' first.\n");
        printf("\n");
        printf("Quick start:\n");
        printf("  ai config init local      # Use local model (Ollama)\n");
        printf("  ai config init anthropic  # Use Claude API\n");
        printf("\n");
        return SHELL_ERR_ARGS;
    }

    /* Status */
    if (strcmp(action, "status") == 0) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║                    AI SUBSYSTEM STATUS                         ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n");
        printf("\n");

        uint64_t requests, tokens, blocked;
        phantom_ai_get_stats(ai, &requests, &tokens, &blocked);

        printf("  Provider:      %s\n",
               ai->config.provider == PHANTOM_AI_PROVIDER_LOCAL ? "Local (Ollama)" :
               ai->config.provider == PHANTOM_AI_PROVIDER_ANTHROPIC ? "Anthropic Claude" :
               ai->config.provider == PHANTOM_AI_PROVIDER_OPENAI ? "OpenAI" : "Custom");
        printf("  Model:         %s\n", ai->config.model_name);
        printf("  Connected:     %s\n", phantom_ai_is_connected(ai) ? "Yes" : "No");
        printf("  Safety level:  %s\n",
               ai->config.safety == PHANTOM_AI_SAFETY_STRICT ? "Strict" :
               ai->config.safety == PHANTOM_AI_SAFETY_STANDARD ? "Standard" : "Minimal");
        printf("\n");
        printf("  Statistics:\n");
        printf("    Total requests:     %lu\n", requests);
        printf("    Total tokens:       %lu\n", tokens);
        printf("    Unsafe blocked:     %lu\n", blocked);
        printf("    Governor assists:   %lu\n", ai->governor_assists);
        printf("    Code generated:     %lu\n", ai->code_generated);
        printf("\n");
        return SHELL_OK;
    }

    /* Chat with assistant */
    if (strcmp(action, "chat") == 0 || strcmp(action, "ask") == 0) {
        if (argc < 3) {
            printf("Usage: ai %s <message>\n", action);
            return SHELL_ERR_ARGS;
        }

        /* Concatenate remaining args as message (with safe bounds checking) */
        char message[PHANTOM_AI_MAX_PROMPT] = {0};
        if (safe_concat_argv(message, sizeof(message), argc, argv, 2) < 0) {
            printf("Error: Message too long\n");
            return SHELL_ERR_ARGS;
        }

        printf("\n");
        printf("You: %s\n", message);
        printf("\n");
        printf("Phantom AI: ");
        fflush(stdout);

        char response[PHANTOM_AI_MAX_RESPONSE];
        if (phantom_ai_chat(ai, message, response, sizeof(response)) == 0) {
            printf("%s\n", response);
        } else {
            printf("[AI request failed]\n");
        }
        printf("\n");
        return SHELL_OK;
    }

    /* Explain something */
    if (strcmp(action, "explain") == 0) {
        if (argc < 3) {
            printf("Usage: ai explain <topic|error|command>\n");
            printf("\nExamples:\n");
            printf("  ai explain constitution\n");
            printf("  ai explain \"file not found\"\n");
            printf("  ai explain \"geology view\"\n");
            return SHELL_ERR_ARGS;
        }

        /* Concatenate topic (with safe bounds checking) */
        char topic[1024] = {0};
        if (safe_concat_argv(topic, sizeof(topic), argc, argv, 2) < 0) {
            printf("Error: Topic too long\n");
            return SHELL_ERR_ARGS;
        }

        printf("\n");
        printf("Explaining: %s\n", topic);
        printf("\n");

        char response[PHANTOM_AI_MAX_RESPONSE];
        char prompt[PHANTOM_AI_MAX_PROMPT];
        snprintf(prompt, sizeof(prompt),
                 "Explain the following in the context of PhantomOS: %s", topic);

        if (phantom_ai_chat(ai, prompt, response, sizeof(response)) == 0) {
            printf("%s\n", response);
        } else {
            printf("[AI request failed]\n");
        }
        printf("\n");
        return SHELL_OK;
    }

    /* Generate code */
    if (strcmp(action, "generate") == 0) {
        if (argc < 3) {
            printf("Usage: ai generate <description>\n");
            printf("\nExamples:\n");
            printf("  ai generate \"function to read a file\"\n");
            printf("  ai generate \"loop that processes items without deleting\"\n");
            return SHELL_ERR_ARGS;
        }

        /* Concatenate description (with safe bounds checking) */
        char description[1024] = {0};
        if (safe_concat_argv(description, sizeof(description), argc, argv, 2) < 0) {
            printf("Error: Description too long\n");
            return SHELL_ERR_ARGS;
        }

        printf("\n");
        printf("Generating Phantom-compliant code for: %s\n", description);
        printf("\n");

        phantom_ai_codegen_request_t req = {0};
        strncpy(req.description, description, sizeof(req.description) - 1);
        strncpy(req.language, "C", sizeof(req.language) - 1);
        req.must_avoid_destruction = 1;
        req.must_use_hide_not_delete = 1;
        req.require_governor_approval = 1;

        char code[PHANTOM_AI_MAX_RESPONSE];
        if (phantom_ai_generate_code(ai, &req, code, sizeof(code)) == 0) {
            printf("Generated code:\n");
            printf("────────────────────────────────────────────\n");
            printf("%s\n", code);
            printf("────────────────────────────────────────────\n");

            /* Validate with Governor */
            if (ctx->kernel->governor) {
                printf("\nGovernor validation: ");
                if (phantom_ai_validate_code(ai, code, strlen(code))) {
                    printf("PASSED (no destructive patterns)\n");
                } else {
                    printf("WARNING: Contains patterns that may need review\n");
                }
            }
        } else {
            printf("[Code generation failed]\n");
        }
        printf("\n");
        return SHELL_OK;
    }

    /* Suggest command */
    if (strcmp(action, "suggest") == 0) {
        if (argc < 3) {
            printf("Usage: ai suggest <what you want to do>\n");
            printf("\nExamples:\n");
            printf("  ai suggest \"list files in current directory\"\n");
            printf("  ai suggest \"create a new file called notes.txt\"\n");
            return SHELL_ERR_ARGS;
        }

        /* Concatenate task (with safe bounds checking) */
        char task[512] = {0};
        if (safe_concat_argv(task, sizeof(task), argc, argv, 2) < 0) {
            printf("Error: Task too long\n");
            return SHELL_ERR_ARGS;
        }

        printf("\n");
        printf("Task: %s\n", task);

        char command[256];
        if (phantom_ai_suggest_command(ai, task, command, sizeof(command)) == 0) {
            printf("Suggested command: %s\n", command);
        } else {
            printf("[Suggestion failed]\n");
        }
        printf("\n");
        return SHELL_OK;
    }

    /* Review code */
    if (strcmp(action, "review") == 0) {
        if (argc < 3) {
            printf("Usage: ai review <code>\n");
            printf("\nExamples:\n");
            printf("  ai review \"unlink(file);\"\n");
            printf("  ai review \"hide_file(path);\"\n");
            return SHELL_ERR_ARGS;
        }

        /* Concatenate code (with safe bounds checking) */
        char code[1024] = {0};
        if (safe_concat_argv(code, sizeof(code), argc, argv, 2) < 0) {
            printf("Error: Code too long (max %zu chars)\n", sizeof(code) - 1);
            return SHELL_ERR_ARGS;
        }

        printf("\n");
        printf("Reviewing code for Phantom compliance:\n");
        printf("────────────────────────────────────────────\n");
        printf("%s\n", code);
        printf("────────────────────────────────────────────\n");
        printf("\n");

        phantom_ai_governor_analysis_t analysis;
        if (phantom_ai_analyze_code(ai, code, strlen(code), &analysis) == 0) {
            printf("AI Analysis:\n");
            printf("  Threat level: %s\n", governor_threat_to_string(analysis.threat_level));
            printf("  Recommendation: %s\n",
                   analysis.recommended_decision == GOVERNOR_APPROVE ? "APPROVE" :
                   analysis.recommended_decision == GOVERNOR_DECLINE ? "DECLINE" : "MODIFY");
            printf("  Confidence: %d%%\n", analysis.confidence);
            printf("\n");
            printf("  Analysis:\n");
            printf("  %s\n", analysis.detailed_analysis);

            if (analysis.destructive_patterns > 0) {
                printf("\n");
                printf("  Warning: %d destructive pattern(s) detected\n",
                       analysis.destructive_patterns);
            }
        } else {
            printf("[Analysis failed]\n");
        }
        printf("\n");
        return SHELL_OK;
    }

    /* Clear AI history */
    if (strcmp(action, "clear") == 0) {
        phantom_ai_clear_history(ai);
        printf("AI conversation history cleared.\n");
        return SHELL_OK;
    }

    printf("Unknown AI action: %s\n", action);
    printf("Use 'ai' for help.\n");
    return SHELL_ERR_ARGS;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * NETWORK COMMAND
 *
 * Phantom-safe networking: connections suspend instead of close,
 * all traffic is logged, and the Governor controls access.
 * ══════════════════════════════════════════════════════════════════════════════ */

static shell_result_t cmd_net(struct shell_context *ctx, int argc, char **argv) {
    if (!ctx || !ctx->kernel) return SHELL_ERR_ARGS;

    /* Get or create network context */
    phantom_net_t *net = (phantom_net_t *)ctx->kernel->net;

    if (argc < 2) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║                  PHANTOM NETWORK LAYER                         ║\n");
        printf("║         \"Connections rest, they never die\"                     ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("Commands:\n");
        printf("  net init                  Initialize network subsystem\n");
        printf("  net status                Show network status and stats\n");
        printf("  net list                  List all sockets (active, suspended, dormant)\n");
        printf("  net connect <host> <port> Connect to remote host\n");
        printf("  net listen <port>         Start listening on port\n");
        printf("  net send <id> <data>      Send data on socket\n");
        printf("  net recv <id>             Receive data from socket\n");
        printf("  net suspend <id>          Suspend a connection\n");
        printf("  net resume <id>           Resume a suspended connection\n");
        printf("  net dormant <id>          Make connection dormant (like 'close')\n");
        printf("  net awaken <id>           Reawaken dormant connection\n");
        printf("  net http <url>            Simple HTTP GET request\n");
        printf("  net https <url>           Secure HTTPS GET request (TLS)\n");
        printf("  net tls init              Initialize TLS subsystem\n");
        printf("  net tls status            Show TLS status and statistics\n");
        printf("  net tls connect <host> <port>  TLS connect to server\n");
        printf("  net tls cert <id>         Show certificate info for connection\n");
        printf("\n");
        printf("Philosophy:\n");
        printf("  In PhantomOS, connections are never destroyed. They can be:\n");
        printf("  - ACTIVE: Normal operation\n");
        printf("  - SUSPENDED: Temporarily paused, can resume instantly\n");
        printf("  - DORMANT: Inactive but preserved (like traditional 'closed')\n");
        printf("\n");
        printf("  All network traffic is logged for accountability.\n");
        printf("\n");

        if (!net) {
            printf("Status: Network not initialized\n");
            printf("  Use 'net init' to initialize the network subsystem.\n");
        } else {
            printf("Status: %s\n", net->running ? "Active" : "Inactive");
            printf("  Active: %lu, Suspended: %lu, Dormant: %lu\n",
                   net->active_connections, net->suspended_connections,
                   net->dormant_connections);
        }
        printf("\n");
        return SHELL_OK;
    }

    const char *action = argv[1];

    /* Initialize network */
    if (strcmp(action, "init") == 0) {
        if (net) {
            printf("Network already initialized.\n");
            return SHELL_OK;
        }

        net = malloc(sizeof(phantom_net_t));
        if (!net) {
            printf("Error: Failed to allocate network context\n");
            return SHELL_ERR_NOMEM;
        }

        if (phantom_net_init(net, ctx->kernel) != 0) {
            printf("Error: Failed to initialize network\n");
            free(net);
            return SHELL_ERR_IO;
        }

        ctx->kernel->net = net;

        /* Connect to Governor if available */
        if (ctx->kernel->governor) {
            phantom_net_set_governor(net, ctx->kernel->governor);
            printf("Network integrated with Governor for capability checks.\n");
        }

        /* Connect to GeoFS if available */
        if (ctx->kernel->geofs_volume) {
            phantom_net_set_geofs(net, ctx->kernel->geofs_volume);
            printf("Network logging to geology enabled.\n");
        }

        printf("Network subsystem initialized.\n");
        return SHELL_OK;
    }

    /* All other commands require network to be initialized */
    if (!net) {
        printf("\n");
        printf("Network not initialized. Run 'net init' first.\n");
        printf("\n");
        return SHELL_ERR_ARGS;
    }

    /* Status */
    if (strcmp(action, "status") == 0) {
        uint64_t sent, recv, active, suspended, dormant;
        phantom_net_get_stats(net, &sent, &recv, &active, &suspended, &dormant);

        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║                  NETWORK STATUS                                ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("  State:              %s\n", net->running ? "Running" : "Stopped");
        printf("  Logging:            %s\n", net->logging_enabled ? "Enabled" : "Disabled");
        printf("  Governor checks:    %s\n", net->governor_checks ? "Enabled" : "Disabled");
        printf("\n");
        printf("  Connections:\n");
        printf("    Active:           %lu\n", active);
        printf("    Suspended:        %lu\n", suspended);
        printf("    Dormant:          %lu\n", dormant);
        printf("    Total ever:       %lu\n", net->total_connections);
        printf("\n");
        printf("  Traffic:\n");
        printf("    Bytes sent:       %lu\n", sent);
        printf("    Bytes received:   %lu\n", recv);
        printf("\n");
        return SHELL_OK;
    }

    /* List sockets */
    if (strcmp(action, "list") == 0) {
        printf("\n");
        printf("  ID     STATE       TYPE    LOCAL               REMOTE              SENT/RECV\n");
        printf("────────────────────────────────────────────────────────────────────────────────\n");

        for (int i = 0; i < net->socket_count; i++) {
            phantom_socket_t *sock = &net->sockets[i];
            char local_str[64], remote_str[64];

            phantom_addr_to_string(&sock->local, local_str, sizeof(local_str));
            phantom_addr_to_string(&sock->remote, remote_str, sizeof(remote_str));

            const char *type_str;
            switch (sock->type) {
                case PHANTOM_SOCK_STREAM: type_str = "TCP"; break;
                case PHANTOM_SOCK_DGRAM:  type_str = "UDP"; break;
                case PHANTOM_SOCK_RAW:    type_str = "RAW"; break;
                default:                  type_str = "???"; break;
            }

            printf("  %-5u  %-10s  %-4s  %-18s  %-18s  %lu/%lu\n",
                   sock->id,
                   phantom_conn_state_string(sock->state),
                   type_str,
                   local_str,
                   remote_str,
                   sock->bytes_sent,
                   sock->bytes_received);
        }

        if (net->socket_count == 0) {
            printf("  (no sockets)\n");
        }
        printf("\n");
        return SHELL_OK;
    }

    /* Connect to host */
    if (strcmp(action, "connect") == 0) {
        if (argc < 4) {
            printf("Usage: net connect <host> <port>\n");
            return SHELL_ERR_ARGS;
        }

        const char *host = argv[2];
        uint16_t port;
        if (safe_parse_port(argv[3], &port) < 0) {
            printf("Invalid port: %s\n", argv[3]);
            return SHELL_ERR_ARGS;
        }

        printf("Connecting to %s:%u...\n", host, port);

        int sock_id = phantom_tcp_connect(net, host, port);
        if (sock_id < 0) {
            printf("Connection failed: %s\n", phantom_net_error_string(sock_id));
            return SHELL_ERR_IO;
        }

        printf("Connected! Socket ID: %d\n", sock_id);
        return SHELL_OK;
    }

    /* Listen on port */
    if (strcmp(action, "listen") == 0) {
        if (argc < 3) {
            printf("Usage: net listen <port>\n");
            return SHELL_ERR_ARGS;
        }

        uint16_t port;
        if (safe_parse_port(argv[2], &port) < 0) {
            printf("Invalid port: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }

        printf("Starting listener on port %u...\n", port);

        int sock_id = phantom_tcp_listen(net, port, 10);
        if (sock_id < 0) {
            printf("Listen failed: %s\n", phantom_net_error_string(sock_id));
            return SHELL_ERR_IO;
        }

        printf("Listening! Socket ID: %d\n", sock_id);
        return SHELL_OK;
    }

    /* Send data */
    if (strcmp(action, "send") == 0) {
        if (argc < 4) {
            printf("Usage: net send <socket_id> <data>\n");
            return SHELL_ERR_ARGS;
        }

        int sock_id;
        if (safe_parse_int(argv[2], &sock_id) < 0 || sock_id < 0) {
            printf("Invalid socket ID: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }

        /* Concatenate remaining args as data (with safe bounds checking) */
        char data[4096] = {0};
        if (safe_concat_argv(data, sizeof(data), argc, argv, 3) < 0) {
            printf("Error: Data too long\n");
            return SHELL_ERR_ARGS;
        }

        ssize_t sent = phantom_tcp_send_all(net, sock_id, data, strlen(data));
        if (sent < 0) {
            printf("Send failed: %s\n", phantom_net_error_string(sent));
            return SHELL_ERR_IO;
        }

        printf("Sent %zd bytes on socket %d\n", sent, sock_id);
        return SHELL_OK;
    }

    /* Receive data */
    if (strcmp(action, "recv") == 0) {
        if (argc < 3) {
            printf("Usage: net recv <socket_id>\n");
            return SHELL_ERR_ARGS;
        }

        int sock_id;
        if (safe_parse_int(argv[2], &sock_id) < 0 || sock_id < 0) {
            printf("Invalid socket ID: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }

        char buffer[4096];
        ssize_t received = phantom_socket_recv(net, sock_id, buffer, sizeof(buffer) - 1, 0);
        if (received < 0) {
            printf("Receive failed: %s\n", phantom_net_error_string(received));
            return SHELL_ERR_IO;
        }

        buffer[received] = '\0';
        printf("Received %zd bytes:\n", received);
        printf("────────────────────────────────────────────\n");
        printf("%s\n", buffer);
        printf("────────────────────────────────────────────\n");
        return SHELL_OK;
    }

    /* Suspend connection */
    if (strcmp(action, "suspend") == 0) {
        if (argc < 3) {
            printf("Usage: net suspend <socket_id>\n");
            return SHELL_ERR_ARGS;
        }

        int sock_id;
        if (safe_parse_int(argv[2], &sock_id) < 0 || sock_id < 0) {
            printf("Invalid socket ID: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }
        int result = phantom_socket_suspend(net, sock_id);
        if (result != PHANTOM_NET_OK) {
            printf("Suspend failed: %s\n", phantom_net_error_string(result));
            return SHELL_ERR_IO;
        }

        printf("Socket %d suspended (can be resumed later)\n", sock_id);
        return SHELL_OK;
    }

    /* Resume connection */
    if (strcmp(action, "resume") == 0) {
        if (argc < 3) {
            printf("Usage: net resume <socket_id>\n");
            return SHELL_ERR_ARGS;
        }

        int sock_id;
        if (safe_parse_int(argv[2], &sock_id) < 0 || sock_id < 0) {
            printf("Invalid socket ID: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }
        int result = phantom_socket_resume(net, sock_id);
        if (result != PHANTOM_NET_OK) {
            printf("Resume failed: %s\n", phantom_net_error_string(result));
            return SHELL_ERR_IO;
        }

        printf("Socket %d resumed\n", sock_id);
        return SHELL_OK;
    }

    /* Make dormant (like close, but preserved) */
    if (strcmp(action, "dormant") == 0) {
        if (argc < 3) {
            printf("Usage: net dormant <socket_id>\n");
            return SHELL_ERR_ARGS;
        }

        int sock_id;
        if (safe_parse_int(argv[2], &sock_id) < 0 || sock_id < 0) {
            printf("Invalid socket ID: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }
        int result = phantom_socket_make_dormant(net, sock_id);
        if (result != PHANTOM_NET_OK) {
            printf("Failed: %s\n", phantom_net_error_string(result));
            return SHELL_ERR_IO;
        }

        printf("Socket %d is now dormant (preserved, can be reawakened)\n", sock_id);
        return SHELL_OK;
    }

    /* Reawaken dormant connection */
    if (strcmp(action, "awaken") == 0) {
        if (argc < 3) {
            printf("Usage: net awaken <socket_id>\n");
            return SHELL_ERR_ARGS;
        }

        int sock_id;
        if (safe_parse_int(argv[2], &sock_id) < 0 || sock_id < 0) {
            printf("Invalid socket ID: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }
        int result = phantom_socket_reawaken(net, sock_id);
        if (result != PHANTOM_NET_OK) {
            printf("Reawaken failed: %s\n", phantom_net_error_string(result));
            return SHELL_ERR_IO;
        }

        printf("Socket %d reawakened from dormancy\n", sock_id);
        return SHELL_OK;
    }

    /* HTTP GET */
    if (strcmp(action, "http") == 0) {
        if (argc < 3) {
            printf("Usage: net http <url>\n");
            printf("\nExample: net http http://example.com/\n");
            return SHELL_ERR_ARGS;
        }

        const char *url = argv[2];
        printf("Fetching: %s\n\n", url);

        char response[8192];
        ssize_t len = phantom_http_get(net, url, response, sizeof(response));
        if (len < 0) {
            printf("HTTP request failed: %s\n", phantom_net_error_string(len));
            return SHELL_ERR_IO;
        }

        printf("Response (%zd bytes):\n", len);
        printf("────────────────────────────────────────────\n");
        /* Print first 2000 chars to avoid flooding terminal */
        if (len > 2000) {
            response[2000] = '\0';
            printf("%s\n...(truncated)...\n", response);
        } else {
            printf("%s\n", response);
        }
        printf("────────────────────────────────────────────\n");
        return SHELL_OK;
    }

    /* HTTPS GET (TLS) */
    if (strcmp(action, "https") == 0) {
        if (argc < 3) {
            printf("Usage: net https <url>\n");
            printf("\nExample: net https https://example.com/\n");
            return SHELL_ERR_ARGS;
        }

        /* Check if TLS is available */
        if (!phantom_tls_available()) {
            printf("Error: TLS support not available.\n");
            printf("PhantomOS was compiled without mbedTLS support.\n");
            printf("Rebuild with: make HAVE_MBEDTLS=1\n");
            return SHELL_ERR_IO;
        }

        /* Get or create TLS context */
        phantom_tls_t *tls = (phantom_tls_t *)ctx->kernel->tls;
        if (!tls) {
            printf("TLS not initialized. Initializing...\n");
            tls = malloc(sizeof(phantom_tls_t));
            if (!tls) {
                printf("Error: Failed to allocate TLS context\n");
                return SHELL_ERR_NOMEM;
            }
            if (phantom_tls_init(tls, net) != 0) {
                printf("Error: Failed to initialize TLS\n");
                free(tls);
                return SHELL_ERR_IO;
            }
            ctx->kernel->tls = tls;

            /* Connect to Governor if available */
            if (ctx->kernel->governor) {
                phantom_tls_set_governor(tls, ctx->kernel->governor);
                printf("TLS integrated with Governor for capability checks.\n");
            }
        }

        const char *url = argv[2];
        printf("Fetching (HTTPS): %s\n\n", url);

        char response[8192];
        ssize_t len = phantom_https_get(tls, net, url, response, sizeof(response));
        if (len < 0) {
            printf("HTTPS request failed: %s\n", phantom_tls_error_string(len));
            return SHELL_ERR_IO;
        }

        printf("Response (%zd bytes):\n", len);
        printf("────────────────────────────────────────────\n");
        /* Print first 2000 chars to avoid flooding terminal */
        if (len > 2000) {
            response[2000] = '\0';
            printf("%s\n...(truncated)...\n", response);
        } else {
            printf("%s\n", response);
        }
        printf("────────────────────────────────────────────\n");
        return SHELL_OK;
    }

    /* TLS subcommands */
    if (strcmp(action, "tls") == 0) {
        if (argc < 3) {
            printf("Usage: net tls <init|status|connect|cert>\n");
            return SHELL_ERR_ARGS;
        }

        const char *tls_action = argv[2];

        /* Initialize TLS */
        if (strcmp(tls_action, "init") == 0) {
            if (!phantom_tls_available()) {
                printf("Error: TLS support not available.\n");
                printf("PhantomOS was compiled without mbedTLS support.\n");
                printf("Rebuild with: make HAVE_MBEDTLS=1\n");
                return SHELL_ERR_IO;
            }

            phantom_tls_t *tls = (phantom_tls_t *)ctx->kernel->tls;
            if (tls) {
                printf("TLS already initialized.\n");
                return SHELL_OK;
            }

            tls = malloc(sizeof(phantom_tls_t));
            if (!tls) {
                printf("Error: Failed to allocate TLS context\n");
                return SHELL_ERR_NOMEM;
            }

            if (phantom_tls_init(tls, net) != 0) {
                printf("Error: Failed to initialize TLS\n");
                free(tls);
                return SHELL_ERR_IO;
            }

            ctx->kernel->tls = tls;

            /* Connect to Governor if available */
            if (ctx->kernel->governor) {
                phantom_tls_set_governor(tls, ctx->kernel->governor);
                printf("TLS integrated with Governor for capability checks.\n");
            }

            printf("TLS subsystem initialized.\n");
            printf("  CA certificates path: %s\n", PHANTOM_TLS_CERT_PATH);
            printf("  Minimum TLS version: TLS 1.2\n");
            printf("  Default verification: REQUIRED\n");
            return SHELL_OK;
        }

        /* TLS status */
        if (strcmp(tls_action, "status") == 0) {
            phantom_tls_t *tls = (phantom_tls_t *)ctx->kernel->tls;
            if (!tls) {
                printf("TLS not initialized. Use 'net tls init' first.\n");
                return SHELL_ERR_ARGS;
            }

            printf("\n");
            printf("╔═══════════════════════════════════════════════════════════════╗\n");
            printf("║                    TLS STATUS                                  ║\n");
            printf("╚═══════════════════════════════════════════════════════════════╝\n");
            printf("\n");
            phantom_tls_print_stats(tls);
            return SHELL_OK;
        }

        /* TLS connect */
        if (strcmp(tls_action, "connect") == 0) {
            if (argc < 5) {
                printf("Usage: net tls connect <host> <port>\n");
                return SHELL_ERR_ARGS;
            }

            phantom_tls_t *tls = (phantom_tls_t *)ctx->kernel->tls;
            if (!tls) {
                printf("TLS not initialized. Use 'net tls init' first.\n");
                return SHELL_ERR_ARGS;
            }

            const char *host = argv[3];
            uint16_t port = atoi(argv[4]);

            printf("Connecting to %s:%u with TLS...\n", host, port);

            int ctx_id = phantom_tls_connect(tls, net, host, port);
            if (ctx_id < 0) {
                printf("TLS connection failed: %s\n", phantom_tls_error_string(ctx_id));
                return SHELL_ERR_IO;
            }

            printf("TLS connected! Context ID: %d\n", ctx_id);

            /* Show session info */
            phantom_tls_session_info_t session;
            if (phantom_tls_get_session_info(tls, ctx_id, &session) == 0) {
                printf("  TLS version: %s\n", phantom_tls_version_string(session.version));
                printf("  Cipher: %s\n", session.cipher_suite);
                printf("  Peer: %s\n", session.peer_cert.subject);
            }
            return SHELL_OK;
        }

        /* Certificate info */
        if (strcmp(tls_action, "cert") == 0) {
            if (argc < 4) {
                printf("Usage: net tls cert <context_id>\n");
                return SHELL_ERR_ARGS;
            }

            phantom_tls_t *tls = (phantom_tls_t *)ctx->kernel->tls;
            if (!tls) {
                printf("TLS not initialized. Use 'net tls init' first.\n");
                return SHELL_ERR_ARGS;
            }

            int ctx_id = atoi(argv[3]);
            phantom_tls_cert_info_t cert;

            if (phantom_tls_get_peer_cert(tls, ctx_id, &cert) != 0) {
                printf("Failed to get certificate for context %d\n", ctx_id);
                return SHELL_ERR_IO;
            }

            printf("\n");
            printf("╔═══════════════════════════════════════════════════════════════╗\n");
            printf("║                    CERTIFICATE INFO                            ║\n");
            printf("╚═══════════════════════════════════════════════════════════════╝\n");
            printf("\n");
            phantom_tls_print_cert(&cert);
            return SHELL_OK;
        }

        printf("Unknown TLS action: %s\n", tls_action);
        return SHELL_ERR_ARGS;
    }

    printf("Unknown network action: %s\n", action);
    printf("Use 'net' for help.\n");
    return SHELL_ERR_ARGS;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * USER MANAGEMENT COMMAND
 * Per Phantom Philosophy: Users become DORMANT, never deleted
 * ══════════════════════════════════════════════════════════════════════════════ */

static shell_result_t cmd_user(struct shell_context *ctx, int argc, char **argv) {
    if (!ctx || !ctx->kernel) return SHELL_ERR_ARGS;

    /* Get or create user system */
    phantom_user_system_t *usys = (phantom_user_system_t *)ctx->kernel->init;

    /* Show help if no arguments */
    if (argc < 2) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║                  PHANTOM USER SYSTEM                           ║\n");
        printf("║         \"Users rest, they never disappear\"                     ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("Commands:\n");
        printf("  user init                    Initialize user system\n");
        printf("  user list                    List all users (including dormant)\n");
        printf("  user add <name> <password>   Create new user\n");
        printf("  user info <name>             Show user details\n");
        printf("  user passwd <name>           Change user password\n");
        printf("  user lock <name>             Lock user account\n");
        printf("  user unlock <name>           Unlock user account\n");
        printf("  user suspend <name>          Suspend user (admin action)\n");
        printf("  user dormant <name>          Make user dormant ('delete')\n");
        printf("  user restore <name>          Restore dormant user\n");
        printf("  user perm <name> <+/-perm>   Grant/revoke permission\n");
        printf("  user groups                  List all groups\n");
        printf("  user addgroup <name>         Create new group\n");
        printf("  user join <user> <group>     Add user to group\n");
        printf("\n");
        printf("User States:\n");
        printf("  ACTIVE     Normal operation\n");
        printf("  LOCKED     Too many failed logins (auto-unlocks)\n");
        printf("  SUSPENDED  Admin disabled (can be restored)\n");
        printf("  DORMANT    'Deleted' - preserved forever but inactive\n");
        printf("\n");
        printf("Permissions:\n");
        printf("  login, sudo, create_user, manage_user, create_group,\n");
        printf("  manage_group, install_pkg, system_config, view_logs,\n");
        printf("  network_admin, governor_admin\n");
        printf("\n");
        return SHELL_OK;
    }

    const char *action = argv[1];

    /* Initialize user system */
    if (strcmp(action, "init") == 0) {
        if (usys && ((phantom_user_system_t *)usys)->initialized) {
            printf("User system already initialized.\n");
            phantom_user_system_print_stats((phantom_user_system_t *)usys);
            return SHELL_OK;
        }

        phantom_user_system_t *new_usys = malloc(sizeof(phantom_user_system_t));
        if (!new_usys) {
            printf("Error: Failed to allocate user system\n");
            return SHELL_ERR_NOMEM;
        }

        if (phantom_user_system_init(new_usys, ctx->kernel) != USER_OK) {
            printf("Error: Failed to initialize user system\n");
            free(new_usys);
            return SHELL_ERR_IO;
        }

        /* Store in kernel - we'll use a field that makes sense */
        /* Note: In a real implementation, we'd add a user_system field to kernel */
        printf("User system initialized.\n");
        printf("Default users created: root, system, nobody\n");
        printf("Default groups created: root, wheel, users\n");
        phantom_user_system_print_stats(new_usys);

        /* Store for later commands - using a simple global for now */
        static phantom_user_system_t *global_usys = NULL;
        global_usys = new_usys;
        ctx->kernel->init = (struct phantom_init *)global_usys;

        return SHELL_OK;
    }

    /* Check if user system is initialized */
    if (!usys || !((phantom_user_system_t *)usys)->initialized) {
        printf("\nUser system not initialized. Run 'user init' first.\n\n");
        return SHELL_ERR_ARGS;
    }

    phantom_user_system_t *sys = (phantom_user_system_t *)usys;

    /* List users */
    if (strcmp(action, "list") == 0) {
        printf("\n");
        printf("╔═════════════════════════════════════════════════════════════════════╗\n");
        printf("║                         USER ACCOUNTS                                ║\n");
        printf("╠══════╤════════════════╤══════════╤══════════════╤════════════════════╣\n");
        printf("║ UID  │ Username       │ State    │ Primary GID  │ Last Login         ║\n");
        printf("╠══════╪════════════════╪══════════╪══════════════╪════════════════════╣\n");

        for (int i = 0; i < sys->user_count; i++) {
            phantom_user_t *u = &sys->users[i];
            char last_login[32] = "Never";
            if (u->last_login > 0) {
                struct tm *tm = localtime(&u->last_login);
                strftime(last_login, sizeof(last_login), "%Y-%m-%d %H:%M", tm);
            }

            printf("║ %4u │ %-14s │ %-8s │ %12u │ %-18s ║\n",
                   u->uid, u->username,
                   phantom_user_state_string(u->state),
                   u->primary_gid, last_login);
        }

        printf("╚══════╧════════════════╧══════════╧══════════════╧════════════════════╝\n");
        printf("\nTotal: %d users (%lu active, %lu dormant)\n",
               sys->user_count,
               sys->users_created - sys->users_dormant,
               sys->users_dormant);
        printf("\n");
        return SHELL_OK;
    }

    /* Add user */
    if (strcmp(action, "add") == 0) {
        if (argc < 4) {
            printf("Usage: user add <username> <password> [full_name]\n");
            return SHELL_ERR_ARGS;
        }

        const char *username = argv[2];
        const char *password = argv[3];
        const char *full_name = (argc > 4) ? argv[4] : username;

        uint32_t new_uid;
        int result = phantom_user_create(sys, username, password, full_name,
                                         PHANTOM_UID_ROOT, &new_uid);
        if (result != USER_OK) {
            printf("Failed to create user: %s\n", phantom_user_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("User '%s' created with UID %u\n", username, new_uid);
        printf("Home directory: /home/%s\n", username);
        return SHELL_OK;
    }

    /* User info */
    if (strcmp(action, "info") == 0) {
        if (argc < 3) {
            printf("Usage: user info <username>\n");
            return SHELL_ERR_ARGS;
        }

        phantom_user_t *user = phantom_user_find_by_name(sys, argv[2]);
        if (!user) {
            printf("User not found: %s\n", argv[2]);
            return SHELL_ERR_NOTFOUND;
        }

        phantom_user_print_info(user);
        return SHELL_OK;
    }

    /* Change password */
    if (strcmp(action, "passwd") == 0) {
        if (argc < 3) {
            printf("Usage: user passwd <username>\n");
            printf("       (You will be prompted for new password)\n");
            return SHELL_ERR_ARGS;
        }

        phantom_user_t *user = phantom_user_find_by_name(sys, argv[2]);
        if (!user) {
            printf("User not found: %s\n", argv[2]);
            return SHELL_ERR_NOTFOUND;
        }

        printf("Enter new password for %s: ", argv[2]);
        fflush(stdout);

        char password[128];
        if (!fgets(password, sizeof(password), stdin)) {
            printf("Error reading password\n");
            return SHELL_ERR_IO;
        }
        password[strcspn(password, "\n")] = '\0';

        int result = phantom_user_set_password(sys, user->uid, password,
                                                PHANTOM_UID_ROOT);
        if (result != USER_OK) {
            printf("Failed to change password: %s\n",
                   phantom_user_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("Password changed for user '%s' (version %u)\n",
               argv[2], user->password_version);
        return SHELL_OK;
    }

    /* Lock user */
    if (strcmp(action, "lock") == 0) {
        if (argc < 3) {
            printf("Usage: user lock <username>\n");
            return SHELL_ERR_ARGS;
        }

        phantom_user_t *user = phantom_user_find_by_name(sys, argv[2]);
        if (!user) {
            printf("User not found: %s\n", argv[2]);
            return SHELL_ERR_NOTFOUND;
        }

        int result = phantom_user_set_state(sys, user->uid, USER_STATE_LOCKED,
                                            PHANTOM_UID_ROOT);
        if (result != USER_OK) {
            printf("Failed to lock user: %s\n", phantom_user_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("User '%s' locked\n", argv[2]);
        return SHELL_OK;
    }

    /* Unlock user */
    if (strcmp(action, "unlock") == 0) {
        if (argc < 3) {
            printf("Usage: user unlock <username>\n");
            return SHELL_ERR_ARGS;
        }

        phantom_user_t *user = phantom_user_find_by_name(sys, argv[2]);
        if (!user) {
            printf("User not found: %s\n", argv[2]);
            return SHELL_ERR_NOTFOUND;
        }

        int result = phantom_user_set_state(sys, user->uid, USER_STATE_ACTIVE,
                                            PHANTOM_UID_ROOT);
        if (result != USER_OK) {
            printf("Failed to unlock user: %s\n", phantom_user_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("User '%s' unlocked\n", argv[2]);
        return SHELL_OK;
    }

    /* Suspend user */
    if (strcmp(action, "suspend") == 0) {
        if (argc < 3) {
            printf("Usage: user suspend <username>\n");
            return SHELL_ERR_ARGS;
        }

        phantom_user_t *user = phantom_user_find_by_name(sys, argv[2]);
        if (!user) {
            printf("User not found: %s\n", argv[2]);
            return SHELL_ERR_NOTFOUND;
        }

        int result = phantom_user_set_state(sys, user->uid, USER_STATE_SUSPENDED,
                                            PHANTOM_UID_ROOT);
        if (result != USER_OK) {
            printf("Failed to suspend user: %s\n", phantom_user_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("User '%s' suspended\n", argv[2]);
        return SHELL_OK;
    }

    /* Make dormant (Phantom 'delete') */
    if (strcmp(action, "dormant") == 0) {
        if (argc < 3) {
            printf("Usage: user dormant <username>\n");
            printf("\nNote: In Phantom, users are never truly deleted.\n");
            printf("      Dormant users are preserved but cannot log in.\n");
            return SHELL_ERR_ARGS;
        }

        phantom_user_t *user = phantom_user_find_by_name(sys, argv[2]);
        if (!user) {
            printf("User not found: %s\n", argv[2]);
            return SHELL_ERR_NOTFOUND;
        }

        if (user->uid == PHANTOM_UID_ROOT) {
            printf("Cannot make root user dormant!\n");
            return SHELL_ERR_PERM;
        }

        int result = phantom_user_set_state(sys, user->uid, USER_STATE_DORMANT,
                                            PHANTOM_UID_ROOT);
        if (result != USER_OK) {
            printf("Failed to make user dormant: %s\n",
                   phantom_user_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("User '%s' is now DORMANT\n", argv[2]);
        printf("  Account preserved in geology for accountability.\n");
        printf("  Use 'user restore %s' to reactivate.\n", argv[2]);
        return SHELL_OK;
    }

    /* Restore dormant user */
    if (strcmp(action, "restore") == 0) {
        if (argc < 3) {
            printf("Usage: user restore <username>\n");
            return SHELL_ERR_ARGS;
        }

        phantom_user_t *user = phantom_user_find_by_name(sys, argv[2]);
        if (!user) {
            printf("User not found: %s\n", argv[2]);
            return SHELL_ERR_NOTFOUND;
        }

        if (user->state != USER_STATE_DORMANT) {
            printf("User '%s' is not dormant (state: %s)\n",
                   argv[2], phantom_user_state_string(user->state));
            return SHELL_ERR_ARGS;
        }

        int result = phantom_user_set_state(sys, user->uid, USER_STATE_ACTIVE,
                                            PHANTOM_UID_ROOT);
        if (result != USER_OK) {
            printf("Failed to restore user: %s\n",
                   phantom_user_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("User '%s' restored from dormancy\n", argv[2]);
        return SHELL_OK;
    }

    /* Grant/revoke permission */
    if (strcmp(action, "perm") == 0) {
        if (argc < 4) {
            printf("Usage: user perm <username> <+/-permission>\n");
            printf("\nPermissions:\n");
            printf("  +login / -login         Can log in\n");
            printf("  +sudo / -sudo           Can elevate privileges\n");
            printf("  +create_user            Can create users\n");
            printf("  +manage_user            Can modify users\n");
            printf("  +install_pkg            Can install packages\n");
            printf("  +system_config          Can modify system\n");
            printf("  +view_logs              Can view logs\n");
            printf("  +network_admin          Can manage network\n");
            printf("  +governor_admin         Can configure Governor\n");
            return SHELL_ERR_ARGS;
        }

        phantom_user_t *user = phantom_user_find_by_name(sys, argv[2]);
        if (!user) {
            printf("User not found: %s\n", argv[2]);
            return SHELL_ERR_NOTFOUND;
        }

        const char *perm_str = argv[3];
        int grant = (perm_str[0] == '+');
        if (perm_str[0] == '+' || perm_str[0] == '-') {
            perm_str++;
        }

        uint32_t perm = 0;
        if (strcmp(perm_str, "login") == 0) perm = PERM_LOGIN;
        else if (strcmp(perm_str, "sudo") == 0) perm = PERM_SUDO;
        else if (strcmp(perm_str, "create_user") == 0) perm = PERM_CREATE_USER;
        else if (strcmp(perm_str, "manage_user") == 0) perm = PERM_MANAGE_USER;
        else if (strcmp(perm_str, "install_pkg") == 0) perm = PERM_INSTALL_PKG;
        else if (strcmp(perm_str, "system_config") == 0) perm = PERM_SYSTEM_CONFIG;
        else if (strcmp(perm_str, "view_logs") == 0) perm = PERM_VIEW_LOGS;
        else if (strcmp(perm_str, "network_admin") == 0) perm = PERM_NETWORK_ADMIN;
        else if (strcmp(perm_str, "governor_admin") == 0) perm = PERM_GOVERNOR_ADMIN;
        else {
            printf("Unknown permission: %s\n", perm_str);
            return SHELL_ERR_ARGS;
        }

        int result;
        if (grant) {
            result = phantom_user_grant_permission(sys, user->uid, perm,
                                                   PHANTOM_UID_ROOT);
        } else {
            result = phantom_user_revoke_permission(sys, user->uid, perm,
                                                    PHANTOM_UID_ROOT);
        }

        if (result != USER_OK) {
            printf("Failed to %s permission: %s\n",
                   grant ? "grant" : "revoke",
                   phantom_user_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("Permission '%s' %s for user '%s'\n",
               perm_str, grant ? "granted" : "revoked", argv[2]);
        return SHELL_OK;
    }

    /* List groups */
    if (strcmp(action, "groups") == 0) {
        printf("\n");
        printf("╔═════════════════════════════════════════════════════════════╗\n");
        printf("║                         GROUPS                               ║\n");
        printf("╠══════╤════════════════╤══════════╤═══════════════════════════╣\n");
        printf("║ GID  │ Name           │ State    │ Description               ║\n");
        printf("╠══════╪════════════════╪══════════╪═══════════════════════════╣\n");

        for (int i = 0; i < sys->group_count; i++) {
            phantom_group_t *g = &sys->groups[i];
            printf("║ %4u │ %-14s │ %-8s │ %-25.25s ║\n",
                   g->gid, g->name,
                   phantom_user_state_string(g->state),
                   g->description);
        }

        printf("╚══════╧════════════════╧══════════╧═══════════════════════════╝\n");
        printf("\nTotal: %d groups\n\n", sys->group_count);
        return SHELL_OK;
    }

    /* Add group */
    if (strcmp(action, "addgroup") == 0) {
        if (argc < 3) {
            printf("Usage: user addgroup <name> [description]\n");
            return SHELL_ERR_ARGS;
        }

        const char *name = argv[2];
        const char *desc = (argc > 3) ? argv[3] : "";

        uint32_t new_gid;
        int result = phantom_group_create(sys, name, desc, PHANTOM_UID_ROOT, &new_gid);
        if (result != USER_OK) {
            printf("Failed to create group: %s\n", phantom_user_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("Group '%s' created with GID %u\n", name, new_gid);
        return SHELL_OK;
    }

    /* Add user to group */
    if (strcmp(action, "join") == 0) {
        if (argc < 4) {
            printf("Usage: user join <username> <groupname>\n");
            return SHELL_ERR_ARGS;
        }

        phantom_user_t *user = phantom_user_find_by_name(sys, argv[2]);
        if (!user) {
            printf("User not found: %s\n", argv[2]);
            return SHELL_ERR_NOTFOUND;
        }

        phantom_group_t *group = phantom_group_find_by_name(sys, argv[3]);
        if (!group) {
            printf("Group not found: %s\n", argv[3]);
            return SHELL_ERR_NOTFOUND;
        }

        int result = phantom_group_add_user(sys, group->gid, user->uid,
                                            PHANTOM_UID_ROOT);
        if (result != USER_OK) {
            printf("Failed to add user to group: %s\n",
                   phantom_user_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("User '%s' added to group '%s'\n", argv[2], argv[3]);
        return SHELL_OK;
    }

    printf("Unknown user action: %s\n", action);
    printf("Use 'user' for help.\n");
    return SHELL_ERR_ARGS;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PACKAGE MANAGER COMMAND
 * Per Phantom Philosophy: Packages are ARCHIVED, never uninstalled
 * ══════════════════════════════════════════════════════════════════════════════ */

static shell_result_t cmd_pkg(struct shell_context *ctx, int argc, char **argv) {
    if (!ctx || !ctx->kernel) return SHELL_ERR_ARGS;

    /* We need a place to store the package manager - use a static for now */
    static phantom_pkg_manager_t *pkg_manager = NULL;

    /* Show help if no arguments */
    if (argc < 2) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║                  PHANTOM PACKAGE MANAGER                       ║\n");
        printf("║         \"Packages rest, they're never removed\"                 ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("Commands:\n");
        printf("  pkg init                Initialize package manager\n");
        printf("  pkg list                List installed packages\n");
        printf("  pkg available           List available packages\n");
        printf("  pkg archived            List archived packages\n");
        printf("  pkg info <name>         Show package details\n");
        printf("  pkg install <name>      Install a package\n");
        printf("  pkg archive <name>      Archive package ('uninstall')\n");
        printf("  pkg restore <name>      Restore archived package\n");
        printf("  pkg search <pattern>    Search packages\n");
        printf("  pkg verify <name>       Verify package integrity\n");
        printf("  pkg stats               Show package statistics\n");
        printf("\n");
        printf("Package States:\n");
        printf("  AVAILABLE   In repository, not installed\n");
        printf("  INSTALLED   Currently active\n");
        printf("  ARCHIVED    'Uninstalled' - preserved but inactive\n");
        printf("  SUPERSEDED  Replaced by newer version\n");
        printf("  BROKEN      Missing dependencies\n");
        printf("\n");
        printf("Philosophy:\n");
        printf("  In PhantomOS, packages are never truly uninstalled.\n");
        printf("  Archived packages remain in geology and can be restored.\n");
        printf("  Multiple versions can coexist - old versions are superseded.\n");
        printf("\n");
        return SHELL_OK;
    }

    const char *action = argv[1];

    /* Initialize package manager */
    if (strcmp(action, "init") == 0) {
        if (pkg_manager && pkg_manager->initialized) {
            printf("Package manager already initialized.\n");
            phantom_pkg_print_stats(pkg_manager);
            return SHELL_OK;
        }

        pkg_manager = malloc(sizeof(phantom_pkg_manager_t));
        if (!pkg_manager) {
            printf("Error: Failed to allocate package manager\n");
            return SHELL_ERR_NOMEM;
        }

        if (phantom_pkg_init(pkg_manager, ctx->kernel) != PKG_OK) {
            printf("Error: Failed to initialize package manager\n");
            free(pkg_manager);
            pkg_manager = NULL;
            return SHELL_ERR_IO;
        }

        /* Connect to Governor if available */
        if (ctx->kernel->governor) {
            phantom_pkg_set_governor(pkg_manager,
                                     (struct phantom_governor *)ctx->kernel->governor);
            printf("Package verification enabled via Governor.\n");
        }

        printf("Package manager initialized.\n");
        phantom_pkg_print_stats(pkg_manager);
        return SHELL_OK;
    }

    /* Check if package manager is initialized */
    if (!pkg_manager || !pkg_manager->initialized) {
        printf("\nPackage manager not initialized. Run 'pkg init' first.\n\n");
        return SHELL_ERR_ARGS;
    }

    /* List installed packages */
    if (strcmp(action, "list") == 0) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                      INSTALLED PACKAGES                                ║\n");
        printf("╠════════════════════╤══════════════╤══════════╤════════════════════════╣\n");
        printf("║ Name               │ Version      │ Type     │ Description            ║\n");
        printf("╠════════════════════╪══════════════╪══════════╪════════════════════════╣\n");

        int count = 0;
        for (int i = 0; i < pkg_manager->package_count; i++) {
            phantom_package_t *pkg = &pkg_manager->packages[i];
            if (pkg->state == PKG_STATE_INSTALLED) {
                printf("║ %-18s │ %-12s │ %-8s │ %-22.22s ║\n",
                       pkg->name, pkg->version,
                       phantom_pkg_type_string(pkg->type),
                       pkg->description);
                count++;
            }
        }

        if (count == 0) {
            printf("║                    (no packages installed)                             ║\n");
        }

        printf("╚════════════════════╧══════════════╧══════════╧════════════════════════╝\n");
        printf("\nInstalled: %d packages\n\n", count);
        return SHELL_OK;
    }

    /* List available packages */
    if (strcmp(action, "available") == 0) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                      AVAILABLE PACKAGES                                ║\n");
        printf("╠════════════════════╤══════════════╤══════════╤════════════════════════╣\n");
        printf("║ Name               │ Version      │ Type     │ Description            ║\n");
        printf("╠════════════════════╪══════════════╪══════════╪════════════════════════╣\n");

        int count = 0;
        for (int i = 0; i < pkg_manager->package_count; i++) {
            phantom_package_t *pkg = &pkg_manager->packages[i];
            if (pkg->state == PKG_STATE_AVAILABLE) {
                printf("║ %-18s │ %-12s │ %-8s │ %-22.22s ║\n",
                       pkg->name, pkg->version,
                       phantom_pkg_type_string(pkg->type),
                       pkg->description);
                count++;
            }
        }

        if (count == 0) {
            printf("║                    (no packages available)                             ║\n");
        }

        printf("╚════════════════════╧══════════════╧══════════╧════════════════════════╝\n");
        printf("\nAvailable: %d packages\n\n", count);
        return SHELL_OK;
    }

    /* List archived packages */
    if (strcmp(action, "archived") == 0) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                      ARCHIVED PACKAGES                                 ║\n");
        printf("║              (Preserved in geology, can be restored)                   ║\n");
        printf("╠════════════════════╤══════════════╤══════════╤════════════════════════╣\n");
        printf("║ Name               │ Version      │ Type     │ Archived At            ║\n");
        printf("╠════════════════════╪══════════════╪══════════╪════════════════════════╣\n");

        int count = 0;
        for (int i = 0; i < pkg_manager->package_count; i++) {
            phantom_package_t *pkg = &pkg_manager->packages[i];
            if (pkg->state == PKG_STATE_ARCHIVED) {
                char archived[32] = "Unknown";
                if (pkg->archived_at > 0) {
                    struct tm *tm = localtime(&pkg->archived_at);
                    strftime(archived, sizeof(archived), "%Y-%m-%d %H:%M", tm);
                }

                printf("║ %-18s │ %-12s │ %-8s │ %-22s ║\n",
                       pkg->name, pkg->version,
                       phantom_pkg_type_string(pkg->type),
                       archived);
                count++;
            }
        }

        if (count == 0) {
            printf("║                    (no archived packages)                              ║\n");
        }

        printf("╚════════════════════╧══════════════╧══════════╧════════════════════════╝\n");
        printf("\nArchived: %d packages\n", count);
        printf("Use 'pkg restore <name>' to restore an archived package.\n\n");
        return SHELL_OK;
    }

    /* Package info */
    if (strcmp(action, "info") == 0) {
        if (argc < 3) {
            printf("Usage: pkg info <package-name>\n");
            return SHELL_ERR_ARGS;
        }

        phantom_package_t *pkg = phantom_pkg_find(pkg_manager, argv[2]);
        if (!pkg) {
            printf("Package not found: %s\n", argv[2]);
            return SHELL_ERR_NOTFOUND;
        }

        phantom_pkg_print_info(pkg);
        return SHELL_OK;
    }

    /* Install package */
    if (strcmp(action, "install") == 0) {
        if (argc < 3) {
            printf("Usage: pkg install <package-name> [reason]\n");
            return SHELL_ERR_ARGS;
        }

        const char *name = argv[2];
        const char *reason = (argc > 3) ? argv[3] : "User requested";

        /* Check if already installed */
        if (phantom_pkg_is_installed(pkg_manager, name)) {
            printf("Package '%s' is already installed.\n", name);
            return SHELL_OK;
        }

        printf("Installing package: %s\n", name);

        int result = phantom_pkg_install(pkg_manager, name, NULL,
                                         PHANTOM_UID_ROOT, reason);
        if (result != PKG_OK) {
            printf("Installation failed: %s\n", phantom_pkg_result_string(result));
            return SHELL_ERR_IO;
        }

        phantom_package_t *pkg = phantom_pkg_find(pkg_manager, name);
        printf("Package '%s' version %s installed successfully.\n",
               name, pkg ? pkg->version : "unknown");
        return SHELL_OK;
    }

    /* Archive package (Phantom 'uninstall') */
    if (strcmp(action, "archive") == 0) {
        if (argc < 3) {
            printf("Usage: pkg archive <package-name> [reason]\n");
            printf("\nNote: In Phantom, packages are never truly uninstalled.\n");
            printf("      Archived packages are preserved and can be restored.\n");
            return SHELL_ERR_ARGS;
        }

        const char *name = argv[2];
        const char *reason = (argc > 3) ? argv[3] : "User requested";

        phantom_package_t *pkg = phantom_pkg_find(pkg_manager, name);
        if (!pkg) {
            printf("Package not found: %s\n", name);
            return SHELL_ERR_NOTFOUND;
        }

        if (pkg->state != PKG_STATE_INSTALLED) {
            printf("Package '%s' is not currently installed (state: %s)\n",
                   name, phantom_pkg_state_string(pkg->state));
            return SHELL_ERR_ARGS;
        }

        printf("Archiving package: %s\n", name);

        int result = phantom_pkg_archive(pkg_manager, name, PHANTOM_UID_ROOT, reason);
        if (result != PKG_OK) {
            printf("Archive failed: %s\n", phantom_pkg_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("Package '%s' archived.\n", name);
        printf("  Files preserved in %s/%s/\n", PHANTOM_PKG_ARCHIVE_PATH, name);
        printf("  Use 'pkg restore %s' to reinstall.\n", name);
        return SHELL_OK;
    }

    /* Restore archived package */
    if (strcmp(action, "restore") == 0) {
        if (argc < 3) {
            printf("Usage: pkg restore <package-name> [version]\n");
            return SHELL_ERR_ARGS;
        }

        const char *name = argv[2];
        const char *version = (argc > 3) ? argv[3] : NULL;

        phantom_package_t *pkg = phantom_pkg_find(pkg_manager, name);
        if (!pkg) {
            printf("Package not found: %s\n", name);
            return SHELL_ERR_NOTFOUND;
        }

        if (pkg->state != PKG_STATE_ARCHIVED) {
            printf("Package '%s' is not archived (state: %s)\n",
                   name, phantom_pkg_state_string(pkg->state));
            return SHELL_ERR_ARGS;
        }

        printf("Restoring package: %s\n", name);

        int result = phantom_pkg_restore(pkg_manager, name, version, PHANTOM_UID_ROOT);
        if (result != PKG_OK) {
            printf("Restore failed: %s\n", phantom_pkg_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("Package '%s' restored from archive.\n", name);
        return SHELL_OK;
    }

    /* Search packages */
    if (strcmp(action, "search") == 0) {
        if (argc < 3) {
            printf("Usage: pkg search <pattern>\n");
            return SHELL_ERR_ARGS;
        }

        const char *pattern = argv[2];
        printf("\nSearching for: %s\n", pattern);
        printf("────────────────────────────────────────────\n");

        int found = 0;
        for (int i = 0; i < pkg_manager->package_count; i++) {
            phantom_package_t *pkg = &pkg_manager->packages[i];
            if (strstr(pkg->name, pattern) ||
                strstr(pkg->description, pattern)) {
                printf("  %-20s %-12s [%s] %s\n",
                       pkg->name, pkg->version,
                       phantom_pkg_state_string(pkg->state),
                       pkg->description);
                found++;
            }
        }

        if (found == 0) {
            printf("  No packages found matching '%s'\n", pattern);
        } else {
            printf("────────────────────────────────────────────\n");
            printf("Found: %d packages\n", found);
        }
        printf("\n");
        return SHELL_OK;
    }

    /* Verify package */
    if (strcmp(action, "verify") == 0) {
        if (argc < 3) {
            printf("Usage: pkg verify <package-name>\n");
            return SHELL_ERR_ARGS;
        }

        phantom_package_t *pkg = phantom_pkg_find(pkg_manager, argv[2]);
        if (!pkg) {
            printf("Package not found: %s\n", argv[2]);
            return SHELL_ERR_NOTFOUND;
        }

        printf("Verifying package: %s\n", argv[2]);

        int result = phantom_pkg_verify(pkg_manager, pkg);
        if (result != PKG_OK) {
            printf("Verification FAILED: %s\n", phantom_pkg_result_string(result));
            return SHELL_ERR_IO;
        }

        printf("Package '%s' verification: OK\n", argv[2]);
        printf("  Hash verified: yes\n");
        printf("  Signature: %s\n", pkg->is_verified ? "Governor approved" : "Unverified");
        return SHELL_OK;
    }

    /* Statistics */
    if (strcmp(action, "stats") == 0) {
        phantom_pkg_print_stats(pkg_manager);
        return SHELL_OK;
    }

    printf("Unknown package action: %s\n", action);
    printf("Use 'pkg' for help.\n");
    return SHELL_ERR_ARGS;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * TEMPORAL ENGINE COMMAND
 * Time travel through the system's history
 * ══════════════════════════════════════════════════════════════════════════════ */

static shell_result_t cmd_time(struct shell_context *ctx, int argc, char **argv) {
    if (!ctx || !ctx->kernel) return SHELL_ERR_ARGS;

    /* Static temporal engine (persists across calls) */
    static phantom_temporal_t *temporal = NULL;

    /* Show help if no arguments */
    if (argc < 2) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║                  PHANTOM TEMPORAL ENGINE                       ║\n");
        printf("║       \"Every moment preserved, every change remembered\"       ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("Commands:\n");
        printf("  time init                     Initialize temporal engine\n");
        printf("  time events [range]           List events in time range\n");
        printf("  time diff <from> <to>         Compare system state at two times\n");
        printf("  time at <point>               Show system state at a time\n");
        printf("  time history <path>           Show complete history of a file\n");
        printf("  time activity <uid> [range]   Show user activity\n");
        printf("  time snapshot [label]         Create named snapshot\n");
        printf("  time snapshots                List all snapshots\n");
        printf("  time stats                    Show temporal engine statistics\n");
        printf("\n");
        printf("Time Point Formats:\n");
        printf("  now                           Current moment\n");
        printf("  boot                          System boot time\n");
        printf("  -5m, -1h, -2d                 Relative (5 min, 1 hour, 2 days ago)\n");
        printf("  2024-01-15                    Specific date\n");
        printf("  2024-01-15 14:30:00           Specific date and time\n");
        printf("\n");
        printf("Examples:\n");
        printf("  time events -1h               Events in the last hour\n");
        printf("  time diff -1d now             What changed in the last day\n");
        printf("  time at -2h                   System state 2 hours ago\n");
        printf("  time history /home/user/doc   All changes to this file\n");
        printf("  time snapshot \"before update\" Label this moment\n");
        printf("\n");
        printf("Philosophy:\n");
        printf("  In PhantomOS, every action creates an immutable record in the\n");
        printf("  geology. The Temporal Engine lets you explore this history -\n");
        printf("  travel back in time, compare states, audit changes, and\n");
        printf("  understand exactly how your system evolved.\n");
        printf("\n");
        return SHELL_OK;
    }

    const char *action = argv[1];

    /* Initialize temporal engine */
    if (strcmp(action, "init") == 0) {
        if (temporal && temporal->initialized) {
            printf("Temporal engine already initialized.\n");
            phantom_time_print_stats(temporal);
            return SHELL_OK;
        }

        temporal = malloc(sizeof(phantom_temporal_t));
        if (!temporal) {
            printf("Error: Failed to allocate temporal engine\n");
            return SHELL_ERR_NOMEM;
        }

        if (phantom_time_init(temporal, ctx->kernel) != TIME_OK) {
            printf("Error: Failed to initialize temporal engine\n");
            free(temporal);
            temporal = NULL;
            return SHELL_ERR_IO;
        }

        printf("Temporal engine initialized.\n");
        printf("Time travel is now available!\n");
        phantom_time_print_stats(temporal);
        return SHELL_OK;
    }

    /* Check if temporal engine is initialized */
    if (!temporal || !temporal->initialized) {
        printf("\nTemporal engine not initialized. Run 'time init' first.\n\n");
        return SHELL_ERR_ARGS;
    }

    /* List events */
    if (strcmp(action, "events") == 0) {
        phantom_time_filter_t filter = {0};
        filter.max_results = 50;
        filter.ascending = 0;  /* Newest first */

        /* Parse optional time range */
        if (argc >= 3) {
            phantom_time_point_t start = {0};
            if (phantom_time_parse_point(argv[2], &start) == TIME_OK) {
                filter.time_range.start = start;
            }
        }
        filter.time_range.end = phantom_time_point_now();

        phantom_time_result_t result = {0};
        int err = phantom_time_query(temporal, &filter, &result);
        if (err != TIME_OK) {
            printf("Query failed: %s\n", phantom_time_result_string(err));
            return SHELL_ERR_IO;
        }

        phantom_time_print_result(&result);
        phantom_time_free_result(&result);
        return SHELL_OK;
    }

    /* Diff between two times */
    if (strcmp(action, "diff") == 0) {
        if (argc < 4) {
            printf("Usage: time diff <from> <to>\n");
            printf("\nExamples:\n");
            printf("  time diff -1h now          Changes in the last hour\n");
            printf("  time diff -1d -1h          Changes between yesterday and 1h ago\n");
            printf("  time diff boot now         All changes since boot\n");
            return SHELL_ERR_ARGS;
        }

        phantom_time_point_t from, to;
        if (phantom_time_parse_point(argv[2], &from) != TIME_OK) {
            printf("Invalid time point: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }
        if (phantom_time_parse_point(argv[3], &to) != TIME_OK) {
            printf("Invalid time point: %s\n", argv[3]);
            return SHELL_ERR_ARGS;
        }

        phantom_diff_result_t diff = {0};
        int err = phantom_time_diff(temporal, from, to, &diff);
        if (err != TIME_OK) {
            printf("Diff failed: %s\n", phantom_time_result_string(err));
            return SHELL_ERR_IO;
        }

        phantom_time_print_diff(&diff);
        phantom_time_free_diff(&diff);
        return SHELL_OK;
    }

    /* System state at a point in time */
    if (strcmp(action, "at") == 0) {
        if (argc < 3) {
            printf("Usage: time at <point>\n");
            printf("\nExamples:\n");
            printf("  time at -1h                State 1 hour ago\n");
            printf("  time at 2024-01-15         State on that date\n");
            printf("  time at boot               State at system boot\n");
            return SHELL_ERR_ARGS;
        }

        phantom_time_point_t point;
        if (phantom_time_parse_point(argv[2], &point) != TIME_OK) {
            printf("Invalid time point: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }

        phantom_snapshot_t snapshot = {0};
        int err = phantom_time_at(temporal, point, &snapshot);
        if (err != TIME_OK) {
            printf("Failed to get snapshot: %s\n", phantom_time_result_string(err));
            return SHELL_ERR_IO;
        }

        phantom_time_print_snapshot(&snapshot);
        return SHELL_OK;
    }

    /* File history */
    if (strcmp(action, "history") == 0) {
        if (argc < 3) {
            printf("Usage: time history <path>\n");
            printf("\nShows the complete history of a file - every change ever made.\n");
            return SHELL_ERR_ARGS;
        }

        const char *path = argv[2];
        phantom_time_result_t result = {0};

        int err = phantom_time_file_history(temporal, path, &result);
        if (err != TIME_OK) {
            printf("Failed to get history: %s\n", phantom_time_result_string(err));
            return SHELL_ERR_IO;
        }

        if (result.count == 0) {
            printf("No history found for: %s\n", path);
            printf("(This file may not have been modified since temporal tracking started)\n");
        } else {
            printf("\nHistory for: %s\n", path);
            printf("════════════════════════════════════════════════════════════════\n");
            phantom_time_print_result(&result);
        }

        phantom_time_free_result(&result);
        return SHELL_OK;
    }

    /* User activity */
    if (strcmp(action, "activity") == 0) {
        if (argc < 3) {
            printf("Usage: time activity <uid> [range]\n");
            printf("\nShows all activity by a specific user.\n");
            printf("Examples:\n");
            printf("  time activity 0             Root's activity (all time)\n");
            printf("  time activity 1000 -1h      User 1000's activity in last hour\n");
            return SHELL_ERR_ARGS;
        }

        uint32_t uid = atoi(argv[2]);
        phantom_time_range_t range = {0};
        range.end = phantom_time_point_now();

        if (argc >= 4) {
            phantom_time_point_t start;
            if (phantom_time_parse_point(argv[3], &start) == TIME_OK) {
                range.start = start;
            }
        }

        phantom_time_result_t result = {0};
        int err = phantom_time_user_activity(temporal, uid, range, &result);
        if (err != TIME_OK) {
            printf("Failed to get activity: %s\n", phantom_time_result_string(err));
            return SHELL_ERR_IO;
        }

        printf("\nActivity for UID %u:\n", uid);
        phantom_time_print_result(&result);
        phantom_time_free_result(&result);
        return SHELL_OK;
    }

    /* Create snapshot */
    if (strcmp(action, "snapshot") == 0) {
        const char *label = (argc >= 3) ? argv[2] : NULL;

        phantom_snapshot_t snapshot = {0};
        int err = phantom_time_create_snapshot(temporal, label, &snapshot);
        if (err != TIME_OK) {
            printf("Failed to create snapshot: %s\n", phantom_time_result_string(err));
            return SHELL_ERR_IO;
        }

        printf("Snapshot created!\n");
        phantom_time_print_snapshot(&snapshot);
        return SHELL_OK;
    }

    /* List snapshots */
    if (strcmp(action, "snapshots") == 0) {
        phantom_snapshot_t *list;
        uint32_t count;

        int err = phantom_time_list_snapshots(temporal, &list, &count);
        if (err != TIME_OK) {
            printf("Failed to list snapshots: %s\n", phantom_time_result_string(err));
            return SHELL_ERR_IO;
        }

        if (count == 0) {
            printf("\nNo snapshots created yet.\n");
            printf("Use 'time snapshot [label]' to create one.\n\n");
        } else {
            printf("\n");
            printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
            printf("║                          SAVED SNAPSHOTS                               ║\n");
            printf("╠════════════╤═══════════════════════╤═══════════════════════════════════╣\n");
            printf("║ ID         │ Timestamp             │ Label                             ║\n");
            printf("╠════════════╪═══════════════════════╪═══════════════════════════════════╣\n");

            for (uint32_t i = 0; i < count; i++) {
                char ts[32];
                phantom_time_format_timestamp(list[i].timestamp_ns, ts, sizeof(ts));
                printf("║ %10lu │ %-21s │ %-33.33s ║\n",
                       list[i].snapshot_id, ts,
                       list[i].label[0] ? list[i].label : "(unlabeled)");
            }

            printf("╚════════════╧═══════════════════════╧═══════════════════════════════════╝\n");
            printf("\nTotal: %u snapshots\n\n", count);
        }
        return SHELL_OK;
    }

    /* Statistics */
    if (strcmp(action, "stats") == 0) {
        phantom_time_print_stats(temporal);
        return SHELL_OK;
    }

    printf("Unknown time action: %s\n", action);
    printf("Use 'time' for help.\n");
    return SHELL_ERR_ARGS;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * BROWSER COMMAND - AI-Powered Web Browsing
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * "To browse is to explore; to cache is to preserve."
 *
 * Every page you visit is preserved in geology forever. The browser never
 * forgets, never deletes, and uses AI to help you understand and find content.
 */

/* Static browser instance (initialized on first use) */
static phantom_browser_t *g_browser = NULL;

static shell_result_t cmd_browse(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;

    /* Initialize browser on first use */
    if (!g_browser) {
        g_browser = calloc(1, sizeof(phantom_browser_t));
        if (!g_browser) {
            printf("Failed to allocate browser\n");
            return SHELL_ERR_NOMEM;
        }
        if (phantom_browser_init(g_browser, ctx->kernel) != BROWSER_OK) {
            printf("Failed to initialize browser\n");
            free(g_browser);
            g_browser = NULL;
            return SHELL_ERR_IO;
        }
        /* Set default home and search engine */
        phantom_browser_set_home(g_browser, "https://example.com");
        phantom_browser_set_search(g_browser, "https://duckduckgo.com/?q=");
    }

    if (argc < 2) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                    PHANTOM AI WEB BROWSER                             ║\n");
        printf("║                   \"To Browse, Never To Forget\"                        ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("Every page you visit is preserved in geology forever. The browser uses\n");
        printf("AI to summarize, search, and help you understand your browsing history.\n");
        printf("\n");
        printf("Navigation:\n");
        printf("  browse go <url>        Navigate to a URL\n");
        printf("  browse back            Go back in history\n");
        printf("  browse forward         Go forward in history\n");
        printf("  browse refresh         Reload current page\n");
        printf("  browse home            Go to home page\n");
        printf("\n");
        printf("Tab Management:\n");
        printf("  browse tabs            List all open tabs\n");
        printf("  browse newtab [url]    Open new tab\n");
        printf("  browse close [id]      Close tab (preserved in history!)\n");
        printf("  browse switch <id>     Switch to tab\n");
        printf("\n");
        printf("History (Never Deleted!):\n");
        printf("  browse history [query] Search browsing history\n");
        printf("  browse page <id>       Show details of a cached page\n");
        printf("  browse content <id>    Display cached page content\n");
        printf("\n");
        printf("Bookmarks (Versioned!):\n");
        printf("  browse bookmark [url]  Bookmark current page or URL\n");
        printf("  browse bookmarks       List all bookmarks\n");
        printf("  browse versions <url>  Show all versions of a bookmarked site\n");
        printf("\n");
        printf("AI Features:\n");
        printf("  browse ai ask <q>      Ask AI about your browsing history\n");
        printf("  browse ai summarize    Summarize current page\n");
        printf("  browse ai search <q>   Semantic search across all pages\n");
        printf("  browse ai compare <id1> <id2>  Compare two page versions\n");
        printf("\n");
        printf("Information:\n");
        printf("  browse stats           Show browser statistics\n");
        printf("  browse current         Show current page info\n");
        printf("\n");
        return SHELL_OK;
    }

    const char *action = argv[1];

    /* Navigation: go to URL */
    if (strcmp(action, "go") == 0) {
        if (argc < 3) {
            printf("Usage: browse go <url>\n");
            return SHELL_ERR_ARGS;
        }

        const char *url = argv[2];
        printf("Navigating to: %s\n", url);

        int err = phantom_browser_navigate(g_browser, url);
        if (err != BROWSER_OK) {
            printf("Navigation failed: %s\n", phantom_browser_result_string(err));
            return SHELL_ERR_IO;
        }

        phantom_page_t *page = phantom_browser_get_current_page(g_browser);
        if (page) {
            printf("\n");
            printf("════════════════════════════════════════════════════════════════\n");
            printf("  Title:  %s\n", page->title[0] ? page->title : "(untitled)");
            printf("  URL:    %s\n", page->url);
            printf("  Domain: %s\n", page->domain);
            printf("  State:  %s\n", phantom_browser_state_string(page->state));
            printf("  Size:   %lu bytes\n", page->content_size);
            printf("  Load:   %u ms\n", page->load_time_ms);
            printf("════════════════════════════════════════════════════════════════\n");
            printf("\n[Page cached to geology - Page ID: %lu]\n", page->page_id);
        }
        return SHELL_OK;
    }

    /* Navigation: back */
    if (strcmp(action, "back") == 0) {
        int err = phantom_browser_back(g_browser);
        if (err != BROWSER_OK) {
            printf("Cannot go back: %s\n", phantom_browser_result_string(err));
            return SHELL_ERR_IO;
        }
        phantom_page_t *page = phantom_browser_get_current_page(g_browser);
        if (page) {
            printf("Back to: %s\n", page->title[0] ? page->title : page->url);
        }
        return SHELL_OK;
    }

    /* Navigation: forward */
    if (strcmp(action, "forward") == 0) {
        int err = phantom_browser_forward(g_browser);
        if (err != BROWSER_OK) {
            printf("Cannot go forward: %s\n", phantom_browser_result_string(err));
            return SHELL_ERR_IO;
        }
        phantom_page_t *page = phantom_browser_get_current_page(g_browser);
        if (page) {
            printf("Forward to: %s\n", page->title[0] ? page->title : page->url);
        }
        return SHELL_OK;
    }

    /* Navigation: refresh */
    if (strcmp(action, "refresh") == 0) {
        int err = phantom_browser_refresh(g_browser);
        if (err != BROWSER_OK) {
            printf("Refresh failed: %s\n", phantom_browser_result_string(err));
            return SHELL_ERR_IO;
        }
        printf("Page refreshed (new version cached)\n");
        return SHELL_OK;
    }

    /* Navigation: home */
    if (strcmp(action, "home") == 0) {
        int err = phantom_browser_navigate(g_browser, g_browser->home_page);
        if (err != BROWSER_OK) {
            printf("Failed to go home: %s\n", phantom_browser_result_string(err));
            return SHELL_ERR_IO;
        }
        printf("Home: %s\n", g_browser->home_page);
        return SHELL_OK;
    }

    /* Tab management: list tabs */
    if (strcmp(action, "tabs") == 0) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                           OPEN TABS                                   ║\n");
        printf("╠════╤═══════════════════════════════════════════════════════════════════╣\n");

        int found = 0;
        for (uint32_t i = 0; i < g_browser->tab_count; i++) {
            phantom_tab_t *tab = &g_browser->tabs[i];
            if (tab->tab_id > 0) {
                const char *marker = (i == g_browser->active_tab) ? "*" : " ";
                printf("║ %s%u │ %-65.65s ║\n",
                       marker, tab->tab_id,
                       tab->title[0] ? tab->title : "(empty)");
                found++;
            }
        }

        if (found == 0) {
            printf("║    │ No tabs open                                                      ║\n");
        }

        printf("╚════╧═══════════════════════════════════════════════════════════════════╝\n");
        printf("\n* = active tab\n\n");
        return SHELL_OK;
    }

    /* Tab management: new tab */
    if (strcmp(action, "newtab") == 0) {
        const char *url = (argc >= 3) ? argv[2] : NULL;
        int err = phantom_browser_new_tab(g_browser, url);
        if (err != BROWSER_OK) {
            printf("Failed to open new tab: %s\n", phantom_browser_result_string(err));
            return SHELL_ERR_IO;
        }
        printf("New tab opened (ID: %u)\n", g_browser->tab_count);
        return SHELL_OK;
    }

    /* Tab management: close tab */
    if (strcmp(action, "close") == 0) {
        uint32_t tab_id = (argc >= 3) ? (uint32_t)atoi(argv[2]) : g_browser->active_tab;
        int err = phantom_browser_close_tab(g_browser, tab_id);
        if (err != BROWSER_OK) {
            printf("Failed to close tab: %s\n", phantom_browser_result_string(err));
            return SHELL_ERR_IO;
        }
        printf("Tab %u closed (history preserved in geology)\n", tab_id);
        return SHELL_OK;
    }

    /* Tab management: switch tab */
    if (strcmp(action, "switch") == 0) {
        if (argc < 3) {
            printf("Usage: browse switch <tab_id>\n");
            return SHELL_ERR_ARGS;
        }
        uint32_t tab_id = (uint32_t)atoi(argv[2]);
        int err = phantom_browser_switch_tab(g_browser, tab_id);
        if (err != BROWSER_OK) {
            printf("Failed to switch tab: %s\n", phantom_browser_result_string(err));
            return SHELL_ERR_IO;
        }
        phantom_tab_t *tab = phantom_browser_get_tab(g_browser, tab_id);
        if (tab) {
            printf("Switched to tab %u: %s\n", tab_id, tab->title);
        }
        return SHELL_OK;
    }

    /* History search */
    if (strcmp(action, "history") == 0) {
        const char *query = (argc >= 3) ? argv[2] : NULL;

        phantom_search_result_t results[20];
        uint32_t count = 0;

        int err = phantom_browser_history_search(g_browser, query, results, 20, &count);
        if (err != BROWSER_OK) {
            printf("History search failed: %s\n", phantom_browser_result_string(err));
            return SHELL_ERR_IO;
        }

        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                        BROWSING HISTORY                               ║\n");
        if (query) {
            printf("║  Search: %-62.62s ║\n", query);
        }
        printf("╠════════════╤════════════════════════════════════════════════════════════╣\n");
        printf("║ Page ID    │ Title / URL                                                ║\n");
        printf("╠════════════╪════════════════════════════════════════════════════════════╣\n");

        if (count == 0) {
            printf("║            │ No pages found                                             ║\n");
        } else {
            for (uint32_t i = 0; i < count; i++) {
                printf("║ %10lu │ %-58.58s ║\n",
                       results[i].page_id,
                       results[i].title[0] ? results[i].title : results[i].url);
                if (results[i].snippet[0]) {
                    printf("║            │   %-56.56s ║\n", results[i].snippet);
                }
            }
        }

        printf("╚════════════╧════════════════════════════════════════════════════════════╝\n");
        printf("\nShowing %u pages (all %lu total pages preserved in geology)\n\n",
               count, g_browser->total_pages_visited);
        return SHELL_OK;
    }

    /* Show page details */
    if (strcmp(action, "page") == 0) {
        if (argc < 3) {
            printf("Usage: browse page <page_id>\n");
            return SHELL_ERR_ARGS;
        }

        uint64_t page_id = (uint64_t)atoll(argv[2]);
        phantom_page_t *page = phantom_browser_get_page(g_browser, page_id);
        if (!page) {
            printf("Page not found: %lu\n", page_id);
            return SHELL_ERR_NOTFOUND;
        }

        phantom_browser_print_page(page);
        return SHELL_OK;
    }

    /* Show page content */
    if (strcmp(action, "content") == 0) {
        if (argc < 3) {
            printf("Usage: browse content <page_id>\n");
            return SHELL_ERR_ARGS;
        }

        uint64_t page_id = (uint64_t)atoll(argv[2]);
        char *content = NULL;
        size_t size = 0;

        int err = phantom_browser_get_page_content(g_browser, page_id, &content, &size);
        if (err != BROWSER_OK) {
            printf("Failed to get content: %s\n", phantom_browser_result_string(err));
            return SHELL_ERR_IO;
        }

        printf("\n═══════════════════ PAGE CONTENT ═══════════════════\n");
        /* Print first 4KB of content */
        size_t print_size = (size > 4096) ? 4096 : size;
        printf("%.*s", (int)print_size, content);
        if (size > 4096) {
            printf("\n... [truncated, %lu more bytes]\n", size - 4096);
        }
        printf("\n═══════════════════════════════════════════════════════\n");

        free(content);
        return SHELL_OK;
    }

    /* Add bookmark */
    if (strcmp(action, "bookmark") == 0) {
        const char *url = NULL;
        const char *title = NULL;

        if (argc >= 3) {
            url = argv[2];
            title = (argc >= 4) ? argv[3] : NULL;
        } else {
            phantom_page_t *page = phantom_browser_get_current_page(g_browser);
            if (page) {
                url = page->url;
                title = page->title;
            }
        }

        if (!url) {
            printf("No URL to bookmark. Navigate to a page first.\n");
            return SHELL_ERR_ARGS;
        }

        int err = phantom_browser_bookmark_add(g_browser, url, title, "Default");
        if (err != BROWSER_OK) {
            printf("Bookmark failed: %s\n", phantom_browser_result_string(err));
            return SHELL_ERR_IO;
        }

        printf("Bookmarked: %s\n", url);
        printf("[Page preserved at this moment - you can always return to this version]\n");
        return SHELL_OK;
    }

    /* List bookmarks */
    if (strcmp(action, "bookmarks") == 0) {
        phantom_bookmark_t *bookmarks = NULL;
        uint32_t count = 0;

        int err = phantom_browser_bookmark_list(g_browser, NULL, &bookmarks, &count);
        if (err != BROWSER_OK) {
            printf("Failed to list bookmarks: %s\n", phantom_browser_result_string(err));
            return SHELL_ERR_IO;
        }

        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                          BOOKMARKS                                    ║\n");
        printf("╠════════════╤════════════════════════════════════════════════════════════╣\n");

        if (count == 0) {
            printf("║            │ No bookmarks yet                                           ║\n");
        } else {
            for (uint32_t i = 0; i < count; i++) {
                if (!bookmarks[i].is_archived) {
                    printf("║ %10lu │ %-58.58s ║\n",
                           bookmarks[i].bookmark_id,
                           bookmarks[i].title[0] ? bookmarks[i].title : bookmarks[i].url);
                    printf("║            │   Versions: %-3u  Visits: %-5u                         ║\n",
                           bookmarks[i].version_count, bookmarks[i].visit_count);
                }
            }
        }

        printf("╚════════════╧════════════════════════════════════════════════════════════╝\n");
        printf("\n[Bookmarks are versioned - each visit creates a new snapshot]\n\n");
        return SHELL_OK;
    }

    /* AI features */
    if (strcmp(action, "ai") == 0) {
        if (argc < 3) {
            printf("Usage:\n");
            printf("  browse ai ask <question>        Ask about your browsing history\n");
            printf("  browse ai summarize             Summarize current page\n");
            printf("  browse ai search <query>        Semantic search all pages\n");
            printf("  browse ai compare <id1> <id2>   Compare two page versions\n");
            return SHELL_ERR_ARGS;
        }

        const char *ai_action = argv[2];

        /* AI summarize */
        if (strcmp(ai_action, "summarize") == 0) {
            phantom_page_t *page = phantom_browser_get_current_page(g_browser);
            if (!page) {
                printf("No current page to summarize\n");
                return SHELL_ERR_NOTFOUND;
            }

            char summary[2048];
            int err = phantom_browser_ai_summarize(g_browser, page->page_id, summary, sizeof(summary));
            if (err != BROWSER_OK) {
                printf("AI summarization failed: %s\n", phantom_browser_result_string(err));
                return SHELL_ERR_IO;
            }

            printf("\n");
            printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
            printf("║                        AI SUMMARY                                     ║\n");
            printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
            printf("\n%s\n\n", summary);
            return SHELL_OK;
        }

        /* AI ask */
        if (strcmp(ai_action, "ask") == 0) {
            if (argc < 4) {
                printf("Usage: browse ai ask <question>\n");
                printf("Example: browse ai ask \"What did that article about quantum computing say?\"\n");
                return SHELL_ERR_ARGS;
            }

            /* Reconstruct question from remaining args (with safe bounds checking) */
            char question[1024] = {0};
            if (safe_concat_argv(question, sizeof(question), argc, argv, 3) < 0) {
                printf("Error: Question too long\n");
                return SHELL_ERR_ARGS;
            }

            char answer[2048];
            int err = phantom_browser_ai_answer(g_browser, question, answer, sizeof(answer));
            if (err != BROWSER_OK) {
                printf("AI query failed: %s\n", phantom_browser_result_string(err));
                return SHELL_ERR_IO;
            }

            printf("\n");
            printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
            printf("║                        AI ANSWER                                      ║\n");
            printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
            printf("\nQ: %s\n\n", question);
            printf("A: %s\n\n", answer);
            return SHELL_OK;
        }

        /* AI search */
        if (strcmp(ai_action, "search") == 0) {
            if (argc < 4) {
                printf("Usage: browse ai search <query>\n");
                printf("Example: browse ai search \"machine learning tutorials\"\n");
                return SHELL_ERR_ARGS;
            }

            /* Reconstruct query from remaining args (with safe bounds checking) */
            char query[512] = {0};
            if (safe_concat_argv(query, sizeof(query), argc, argv, 3) < 0) {
                printf("Error: Query too long\n");
                return SHELL_ERR_ARGS;
            }

            phantom_search_result_t results[10];
            uint32_t count = 0;

            int err = phantom_browser_ai_search(g_browser, query, results, 10, &count);
            if (err != BROWSER_OK) {
                printf("AI search failed: %s\n", phantom_browser_result_string(err));
                return SHELL_ERR_IO;
            }

            printf("\n");
            printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
            printf("║                     AI SEMANTIC SEARCH                                ║\n");
            printf("║  Query: %-62.62s ║\n", query);
            printf("╠════════════╤══════════╤════════════════════════════════════════════════╣\n");

            if (count == 0) {
                printf("║            │          │ No relevant pages found                        ║\n");
            } else {
                for (uint32_t i = 0; i < count; i++) {
                    printf("║ %10lu │ %6.1f%% │ %-46.46s ║\n",
                           results[i].page_id, results[i].relevance * 100,
                           results[i].title[0] ? results[i].title : "(untitled)");
                }
            }

            printf("╚════════════╧══════════╧════════════════════════════════════════════════╝\n\n");
            return SHELL_OK;
        }

        /* AI compare */
        if (strcmp(ai_action, "compare") == 0) {
            if (argc < 5) {
                printf("Usage: browse ai compare <page_id_1> <page_id_2>\n");
                printf("Compare two versions of a page to see what changed.\n");
                return SHELL_ERR_ARGS;
            }

            uint64_t id1 = (uint64_t)atoll(argv[3]);
            uint64_t id2 = (uint64_t)atoll(argv[4]);

            char diff[4096];
            int err = phantom_browser_ai_compare(g_browser, id1, id2, diff, sizeof(diff));
            if (err != BROWSER_OK) {
                printf("AI comparison failed: %s\n", phantom_browser_result_string(err));
                return SHELL_ERR_IO;
            }

            printf("\n");
            printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
            printf("║                     AI COMPARISON                                     ║\n");
            printf("║  Comparing page %lu with page %lu\n", id1, id2);
            printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
            printf("\n%s\n\n", diff);
            return SHELL_OK;
        }

        printf("Unknown AI action: %s\n", ai_action);
        return SHELL_ERR_ARGS;
    }

    /* Current page info */
    if (strcmp(action, "current") == 0) {
        phantom_page_t *page = phantom_browser_get_current_page(g_browser);
        if (!page) {
            printf("No current page. Use 'browse go <url>' to navigate.\n");
            return SHELL_OK;
        }
        phantom_browser_print_page(page);
        return SHELL_OK;
    }

    /* Statistics */
    if (strcmp(action, "stats") == 0) {
        phantom_browser_print_stats(g_browser);
        return SHELL_OK;
    }

    printf("Unknown browser action: %s\n", action);
    printf("Use 'browse' for help.\n");
    return SHELL_ERR_ARGS;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * GOVERNOR-CONTROLLED WEB BROWSER
 * ══════════════════════════════════════════════════════════════════════════════ */

static phantom_webbrowser_t *g_webbrowser = NULL;

static shell_result_t cmd_web(struct shell_context *ctx, int argc, char **argv) {
    if (!ctx || !ctx->kernel) return SHELL_ERR_ARGS;

    /* Initialize web browser on first use */
    if (!g_webbrowser) {
        g_webbrowser = calloc(1, sizeof(phantom_webbrowser_t));
        if (!g_webbrowser) {
            printf("Failed to allocate web browser\n");
            return SHELL_ERR_NOMEM;
        }

        /* Get Governor reference */
        phantom_governor_t *gov = (phantom_governor_t *)ctx->kernel->governor;

        if (phantom_webbrowser_init(g_webbrowser, ctx->kernel, gov) != WEBBROWSER_OK) {
            printf("Failed to initialize web browser\n");
            free(g_webbrowser);
            g_webbrowser = NULL;
            return SHELL_ERR_IO;
        }

        /* Connect network and TLS if available */
        if (ctx->kernel->net) {
            phantom_webbrowser_set_network(g_webbrowser, ctx->kernel->net);
        }
        /* Note: TLS would be set here if available */

        /* Connect VFS for GeoFS logging */
        if (ctx->vfs) {
            phantom_webbrowser_set_vfs(g_webbrowser, ctx->vfs);
        }
    }

    if (argc < 2) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                 GOVERNOR-CONTROLLED WEB BROWSER                       ║\n");
        printf("║           \"Every Request Requires Explicit Approval\"                  ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("This browser requires Governor approval for ALL network access.\n");
        printf("Your browsing is logged and audited for accountability.\n");
        printf("\n");
        printf("Navigation:\n");
        printf("  web go <url>                  Navigate to URL (requires approval)\n");
        printf("  web check <url>               Check if URL would be allowed\n");
        printf("\n");
        printf("Domain Policy:\n");
        printf("  web allow <domain> [reason]   Add domain to allowlist\n");
        printf("  web block <domain> [reason]   Add domain to blocklist\n");
        printf("  web reset <domain>            Remove domain from lists\n");
        printf("  web policies                  Show all domain policies\n");
        printf("\n");
        printf("Configuration:\n");
        printf("  web config https <on|off>     Require HTTPS for all connections\n");
        printf("  web config cert <on|off>      Require valid certificates\n");
        printf("  web config auto <on|off>      Auto-approve allowlisted domains\n");
        printf("\n");
        printf("Information:\n");
        printf("  web status                    Show browser status\n");
        printf("  web stats                     Show browsing statistics\n");
        printf("  web connection                Show current connection info\n");
        printf("\n");
        return SHELL_OK;
    }

    const char *action = argv[1];

    /* Navigate to URL */
    if (strcmp(action, "go") == 0) {
        if (argc < 3) {
            printf("Usage: web go <url>\n");
            return SHELL_ERR_ARGS;
        }

        const char *url = argv[2];
        int result = phantom_webbrowser_navigate(g_webbrowser, url);

        if (result != WEBBROWSER_OK) {
            return SHELL_ERR_IO;
        }
        return SHELL_OK;
    }

    /* Check URL */
    if (strcmp(action, "check") == 0) {
        if (argc < 3) {
            printf("Usage: web check <url>\n");
            return SHELL_ERR_ARGS;
        }

        char reason[256];
        int result = phantom_webbrowser_check_url(g_webbrowser, argv[2], reason, sizeof(reason));

        printf("\nURL: %s\n", argv[2]);
        printf("Result: %s\n", phantom_webbrowser_result_string(result));
        printf("Details: %s\n\n", reason);
        return SHELL_OK;
    }

    /* Allow domain */
    if (strcmp(action, "allow") == 0) {
        if (argc < 3) {
            printf("Usage: web allow <domain> [reason]\n");
            return SHELL_ERR_ARGS;
        }

        const char *domain = argv[2];
        const char *reason = (argc >= 4) ? argv[3] : "User allowlisted";

        int result = phantom_webbrowser_allow_domain(g_webbrowser, domain, 1, reason);
        if (result == WEBBROWSER_OK) {
            printf("Domain '%s' added to allowlist (including subdomains)\n", domain);
            printf("Reason: %s\n", reason);
        } else {
            printf("Failed to add domain to allowlist\n");
        }
        return SHELL_OK;
    }

    /* Block domain */
    if (strcmp(action, "block") == 0) {
        if (argc < 3) {
            printf("Usage: web block <domain> [reason]\n");
            return SHELL_ERR_ARGS;
        }

        const char *domain = argv[2];
        const char *reason = (argc >= 4) ? argv[3] : "User blocked";

        int result = phantom_webbrowser_block_domain(g_webbrowser, domain, 1, reason);
        if (result == WEBBROWSER_OK) {
            printf("Domain '%s' added to blocklist (including subdomains)\n", domain);
            printf("Reason: %s\n", reason);
        } else {
            printf("Failed to add domain to blocklist\n");
        }
        return SHELL_OK;
    }

    /* Reset domain */
    if (strcmp(action, "reset") == 0) {
        if (argc < 3) {
            printf("Usage: web reset <domain>\n");
            return SHELL_ERR_ARGS;
        }

        phantom_webbrowser_reset_domain(g_webbrowser, argv[2]);
        printf("Domain '%s' reset - will now require Governor approval\n", argv[2]);
        return SHELL_OK;
    }

    /* Show policies */
    if (strcmp(action, "policies") == 0) {
        phantom_webbrowser_print_policies(g_webbrowser);
        return SHELL_OK;
    }

    /* Configuration */
    if (strcmp(action, "config") == 0) {
        if (argc < 4) {
            printf("Usage:\n");
            printf("  web config https <on|off>   Require HTTPS\n");
            printf("  web config cert <on|off>    Require valid certificates\n");
            printf("  web config auto <on|off>    Auto-approve allowlist\n");
            return SHELL_ERR_ARGS;
        }

        const char *setting = argv[2];
        int value = (strcmp(argv[3], "on") == 0 || strcmp(argv[3], "1") == 0 ||
                     strcmp(argv[3], "yes") == 0);

        if (strcmp(setting, "https") == 0) {
            phantom_webbrowser_require_https(g_webbrowser, value);
        } else if (strcmp(setting, "cert") == 0) {
            phantom_webbrowser_require_valid_cert(g_webbrowser, value);
        } else if (strcmp(setting, "auto") == 0) {
            phantom_webbrowser_auto_approve(g_webbrowser, value);
        } else {
            printf("Unknown config option: %s\n", setting);
            return SHELL_ERR_ARGS;
        }
        return SHELL_OK;
    }

    /* Status */
    if (strcmp(action, "status") == 0) {
        phantom_webbrowser_print_status(g_webbrowser);
        return SHELL_OK;
    }

    /* Statistics */
    if (strcmp(action, "stats") == 0) {
        phantom_webbrowser_print_stats(g_webbrowser);
        return SHELL_OK;
    }

    /* Connection info */
    if (strcmp(action, "connection") == 0) {
        webbrowser_connection_info_t info;
        if (phantom_webbrowser_get_connection(g_webbrowser, &info) == WEBBROWSER_OK) {
            phantom_webbrowser_print_connection(&info);
        } else {
            printf("No active connection.\n");
        }
        return SHELL_OK;
    }

    printf("Unknown web action: %s\n", action);
    printf("Use 'web' for help.\n");
    return SHELL_ERR_ARGS;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * NOTES APP COMMAND
 * ══════════════════════════════════════════════════════════════════════════════ */

static phantom_notes_app_t *g_notes = NULL;

static shell_result_t cmd_notes(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;

    /* Initialize notes app if needed */
    if (!g_notes) {
        g_notes = malloc(sizeof(phantom_notes_app_t));
        if (!g_notes) {
            printf("Failed to allocate notes app\n");
            return SHELL_ERR_NOMEM;
        }
        if (phantom_notes_init(g_notes, ctx->kernel) != APP_OK) {
            printf("Failed to initialize notes app\n");
            free(g_notes);
            g_notes = NULL;
            return SHELL_ERR_IO;
        }
    }

    if (argc < 2) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                         PHANTOM NOTES                                 ║\n");
        printf("║              \"Every thought preserved forever\"                        ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("  Usage: notes <action> [args]\n");
        printf("\n");
        printf("  Actions:\n");
        printf("    list              - List all notes\n");
        printf("    new <title>       - Create new note\n");
        printf("    view <id>         - View a note\n");
        printf("    edit <id> <text>  - Edit note (adds new version)\n");
        printf("    append <id> <text>- Append text to note\n");
        printf("    history <id>      - Show version history\n");
        printf("    search <query>    - Search notes\n");
        printf("    archive <id>      - Archive note (never deleted!)\n");
        printf("    restore <id>      - Restore archived note\n");
        printf("    tag <id> <tags>   - Add tags to note\n");
        printf("    pin <id>          - Pin/unpin note\n");
        printf("\n");
        printf("  Notes are NEVER deleted - they are archived and preserved forever.\n");
        printf("  Every edit creates a new version - time-travel through your thoughts!\n");
        printf("\n");
        return SHELL_OK;
    }

    const char *action = argv[1];

    /* List notes */
    if (strcmp(action, "list") == 0) {
        phantom_notes_print_list(g_notes);
        return SHELL_OK;
    }

    /* Create new note */
    if (strcmp(action, "new") == 0) {
        if (argc < 3) {
            printf("Usage: notes new <title> [initial content]\n");
            return SHELL_ERR_ARGS;
        }

        const char *title = argv[2];
        char content[APP_MAX_NOTE_SIZE] = "";

        /* Combine remaining args as content (with safe bounds checking) */
        if (argc > 3) {
            if (safe_concat_argv(content, sizeof(content), argc, argv, 3) < 0) {
                printf("Error: Content too long\n");
                return SHELL_ERR_ARGS;
            }
        }

        uint64_t note_id;
        int err = phantom_notes_create(g_notes, title, content[0] ? content : NULL, &note_id);
        if (err != APP_OK) {
            printf("Failed to create note: %s\n", phantom_app_result_string(err));
            return SHELL_ERR_IO;
        }

        printf("Created note #%lu: \"%s\"\n", note_id, title);
        printf("[Note preserved in geology - it will exist forever]\n");
        return SHELL_OK;
    }

    /* View note */
    if (strcmp(action, "view") == 0) {
        if (argc < 3) {
            printf("Usage: notes view <note_id>\n");
            return SHELL_ERR_ARGS;
        }

        uint64_t note_id = (uint64_t)atoll(argv[2]);
        phantom_note_t *note = phantom_notes_get(g_notes, note_id);
        if (!note) {
            printf("Note not found: %lu\n", note_id);
            return SHELL_ERR_NOTFOUND;
        }

        phantom_notes_print(note);
        return SHELL_OK;
    }

    /* Edit note */
    if (strcmp(action, "edit") == 0) {
        if (argc < 4) {
            printf("Usage: notes edit <note_id> <new content>\n");
            return SHELL_ERR_ARGS;
        }

        unsigned int nid;
        if (safe_parse_uint(argv[2], &nid) < 0) {
            printf("Invalid note ID: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }
        uint64_t note_id = (uint64_t)nid;

        /* Combine remaining args as content (with safe bounds checking) */
        char content[APP_MAX_NOTE_SIZE] = "";
        if (safe_concat_argv(content, sizeof(content), argc, argv, 3) < 0) {
            printf("Error: Content too long\n");
            return SHELL_ERR_ARGS;
        }

        int err = phantom_notes_edit(g_notes, note_id, content, "User edit");
        if (err != APP_OK) {
            printf("Failed to edit note: %s\n", phantom_app_result_string(err));
            return SHELL_ERR_IO;
        }

        printf("Note updated (new version created - previous version preserved)\n");
        return SHELL_OK;
    }

    /* Append to note */
    if (strcmp(action, "append") == 0) {
        if (argc < 4) {
            printf("Usage: notes append <note_id> <text to append>\n");
            return SHELL_ERR_ARGS;
        }

        unsigned int nid;
        if (safe_parse_uint(argv[2], &nid) < 0) {
            printf("Invalid note ID: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }
        uint64_t note_id = (uint64_t)nid;
        phantom_note_t *note = phantom_notes_get(g_notes, note_id);
        if (!note) {
            printf("Note not found: %lu\n", note_id);
            return SHELL_ERR_NOTFOUND;
        }

        /* Build appended content (with safe bounds checking) */
        char content[APP_MAX_NOTE_SIZE];
        char append_text[APP_MAX_NOTE_SIZE] = "";
        if (safe_concat_argv(append_text, sizeof(append_text), argc, argv, 3) < 0) {
            printf("Error: Appended text too long\n");
            return SHELL_ERR_ARGS;
        }

        size_t existing_len = strlen(note->content);
        size_t append_len = strlen(append_text);
        if (existing_len + 1 + append_len >= APP_MAX_NOTE_SIZE) {
            printf("Error: Note would exceed maximum size\n");
            return SHELL_ERR_ARGS;
        }

        strncpy(content, note->content, APP_MAX_NOTE_SIZE - 1);
        content[APP_MAX_NOTE_SIZE - 1] = '\0';
        if (safe_strcat(content, sizeof(content), "\n") < 0 ||
            safe_strcat(content, sizeof(content), append_text) < 0) {
            printf("Error: Note content too long\n");
            return SHELL_ERR_ARGS;
        }

        int err = phantom_notes_edit(g_notes, note_id, content, "Appended text");
        if (err != APP_OK) {
            printf("Failed to append: %s\n", phantom_app_result_string(err));
            return SHELL_ERR_IO;
        }

        printf("Text appended (new version created)\n");
        return SHELL_OK;
    }

    /* Show version history */
    if (strcmp(action, "history") == 0) {
        if (argc < 3) {
            printf("Usage: notes history <note_id>\n");
            return SHELL_ERR_ARGS;
        }

        unsigned int nid;
        if (safe_parse_uint(argv[2], &nid) < 0) {
            printf("Invalid note ID: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }
        uint64_t note_id = (uint64_t)nid;
        phantom_note_t *note = phantom_notes_get(g_notes, note_id);
        if (!note) {
            printf("Note not found: %lu\n", note_id);
            return SHELL_ERR_NOTFOUND;
        }

        phantom_notes_print_history(note);
        return SHELL_OK;
    }

    /* Search notes */
    if (strcmp(action, "search") == 0) {
        if (argc < 3) {
            printf("Usage: notes search <query>\n");
            return SHELL_ERR_ARGS;
        }

        /* Combine search terms (with safe bounds checking) */
        char query[256] = "";
        if (safe_concat_argv(query, sizeof(query), argc, argv, 2) < 0) {
            printf("Error: Query too long\n");
            return SHELL_ERR_ARGS;
        }

        phantom_note_t **results;
        uint32_t count;
        phantom_notes_search(g_notes, query, &results, &count);

        printf("\nSearch results for \"%s\": %u matches\n\n", query, count);
        if (count > 0) {
            phantom_notes_print_list(g_notes);
        }
        return SHELL_OK;
    }

    /* Archive note */
    if (strcmp(action, "archive") == 0) {
        if (argc < 3) {
            printf("Usage: notes archive <note_id>\n");
            return SHELL_ERR_ARGS;
        }

        uint64_t note_id = (uint64_t)atoll(argv[2]);
        int err = phantom_notes_archive(g_notes, note_id);
        if (err != APP_OK) {
            printf("Failed to archive: %s\n", phantom_app_result_string(err));
            return SHELL_ERR_IO;
        }

        printf("Note archived (preserved forever in geology, hidden from list)\n");
        return SHELL_OK;
    }

    /* Restore note */
    if (strcmp(action, "restore") == 0) {
        if (argc < 3) {
            printf("Usage: notes restore <note_id>\n");
            return SHELL_ERR_ARGS;
        }

        unsigned int nid;
        if (safe_parse_uint(argv[2], &nid) < 0) {
            printf("Invalid note ID: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }
        uint64_t note_id = (uint64_t)nid;
        int err = phantom_notes_restore(g_notes, note_id);
        if (err != APP_OK) {
            printf("Failed to restore: %s\n", phantom_app_result_string(err));
            return SHELL_ERR_IO;
        }

        printf("Note restored from archive\n");
        return SHELL_OK;
    }

    /* Tag note */
    if (strcmp(action, "tag") == 0) {
        if (argc < 4) {
            printf("Usage: notes tag <note_id> <tags>\n");
            return SHELL_ERR_ARGS;
        }

        unsigned int nid;
        if (safe_parse_uint(argv[2], &nid) < 0) {
            printf("Invalid note ID: %s\n", argv[2]);
            return SHELL_ERR_ARGS;
        }
        uint64_t note_id = (uint64_t)nid;

        /* Concatenate tags with comma separator (with safe bounds checking) */
        char tags[APP_MAX_TAGS] = "";
        for (int i = 3; i < argc; i++) {
            if (i > 3) {
                if (safe_strcat(tags, sizeof(tags), ",") < 0) {
                    printf("Error: Tags too long\n");
                    return SHELL_ERR_ARGS;
                }
            }
            if (safe_strcat(tags, sizeof(tags), argv[i]) < 0) {
                printf("Error: Tags too long\n");
                return SHELL_ERR_ARGS;
            }
        }

        int err = phantom_notes_tag(g_notes, note_id, tags);
        if (err != APP_OK) {
            printf("Failed to tag: %s\n", phantom_app_result_string(err));
            return SHELL_ERR_IO;
        }

        printf("Tags updated: %s\n", tags);
        return SHELL_OK;
    }

    /* Pin note */
    if (strcmp(action, "pin") == 0) {
        if (argc < 3) {
            printf("Usage: notes pin <note_id>\n");
            return SHELL_ERR_ARGS;
        }

        uint64_t note_id = (uint64_t)atoll(argv[2]);
        phantom_note_t *note = phantom_notes_get(g_notes, note_id);
        if (!note) {
            printf("Note not found: %lu\n", note_id);
            return SHELL_ERR_NOTFOUND;
        }

        int pinned = (note->state != NOTE_STATE_PINNED);
        phantom_notes_pin(g_notes, note_id, pinned);
        printf("Note %s\n", pinned ? "pinned" : "unpinned");
        return SHELL_OK;
    }

    printf("Unknown notes action: %s\n", action);
    return SHELL_ERR_ARGS;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FILE VIEWER COMMAND
 * ══════════════════════════════════════════════════════════════════════════════ */

static phantom_viewer_app_t *g_viewer = NULL;

static shell_result_t cmd_view(struct shell_context *ctx, int argc, char **argv) {
    /* Initialize viewer if needed */
    if (!g_viewer) {
        g_viewer = malloc(sizeof(phantom_viewer_app_t));
        if (!g_viewer) {
            printf("Failed to allocate viewer\n");
            return SHELL_ERR_NOMEM;
        }
        if (phantom_viewer_init(g_viewer, ctx->kernel, ctx->vfs) != APP_OK) {
            printf("Failed to initialize viewer\n");
            free(g_viewer);
            g_viewer = NULL;
            return SHELL_ERR_IO;
        }
    }

    if (argc < 2) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                      PHANTOM FILE VIEWER                              ║\n");
        printf("║                   Safe, Read-Only Viewing                             ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("  Usage: view <file>          - View a file\n");
        printf("         view info            - Show current file info\n");
        printf("         view hex             - Show hex dump\n");
        printf("         view lines <n>       - Show n lines\n");
        printf("         view history         - Show viewed files\n");
        printf("\n");
        printf("  Supported formats:\n");
        printf("    - Text files (.txt, .md, .log, .json, etc.)\n");
        printf("    - Source code (.c, .h, .py, .js, etc.)\n");
        printf("    - Binary files (hex dump)\n");
        printf("\n");
        printf("  The viewer is READ-ONLY - files cannot be modified.\n");
        printf("\n");
        return SHELL_OK;
    }

    const char *action = argv[1];

    /* Show file info */
    if (strcmp(action, "info") == 0) {
        if (!g_viewer->file_loaded) {
            printf("No file loaded. Use 'view <file>' to open a file.\n");
            return SHELL_OK;
        }
        phantom_viewer_print_info(&g_viewer->current_file);
        return SHELL_OK;
    }

    /* Show hex dump */
    if (strcmp(action, "hex") == 0) {
        if (!g_viewer->file_loaded) {
            printf("No file loaded. Use 'view <file>' to open a file.\n");
            return SHELL_OK;
        }
        uint32_t max_bytes = 256;
        if (argc >= 3) max_bytes = (uint32_t)atoi(argv[2]);
        phantom_viewer_print_hex(g_viewer, max_bytes);
        return SHELL_OK;
    }

    /* Show specific number of lines */
    if (strcmp(action, "lines") == 0) {
        if (!g_viewer->file_loaded) {
            printf("No file loaded. Use 'view <file>' to open a file.\n");
            return SHELL_OK;
        }
        uint32_t max_lines = 50;
        if (argc >= 3) max_lines = (uint32_t)atoi(argv[2]);
        phantom_viewer_print_content(g_viewer, max_lines);
        return SHELL_OK;
    }

    /* Show view history */
    if (strcmp(action, "history") == 0) {
        printf("\n  View History:\n");
        printf("  ───────────────────────────────────────────────────────────────────\n");
        for (uint32_t i = 0; i < g_viewer->history_count; i++) {
            printf("  %u. %s\n", i + 1, g_viewer->view_history[i]);
        }
        if (g_viewer->history_count == 0) {
            printf("  (no files viewed yet)\n");
        }
        printf("\n  Total files viewed: %lu (%.1f KB)\n",
               g_viewer->files_viewed, g_viewer->bytes_viewed / 1024.0);
        return SHELL_OK;
    }

    /* Open a file - extra space for combined path */
    char path[SHELL_MAX_PATH + SHELL_MAX_PATH];
    if (action[0] == '/') {
        strncpy(path, action, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        snprintf(path, sizeof(path), "%s/%s", ctx->cwd, action);
    }

    int err = phantom_viewer_open(g_viewer, path);
    if (err != APP_OK) {
        printf("Failed to open file: %s\n", phantom_app_result_string(err));
        return SHELL_ERR_IO;
    }

    /* Show file info and content */
    phantom_viewer_print_info(&g_viewer->current_file);
    phantom_viewer_print_content(g_viewer, 50);

    return SHELL_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * SYSTEM MONITOR COMMAND
 * ══════════════════════════════════════════════════════════════════════════════ */

static phantom_monitor_app_t *g_monitor = NULL;

static shell_result_t cmd_monitor(struct shell_context *ctx, int argc, char **argv) {
    /* Initialize monitor if needed */
    if (!g_monitor) {
        g_monitor = malloc(sizeof(phantom_monitor_app_t));
        if (!g_monitor) {
            printf("Failed to allocate monitor\n");
            return SHELL_ERR_NOMEM;
        }
        if (phantom_monitor_init(g_monitor, ctx->kernel, ctx->vfs) != APP_OK) {
            printf("Failed to initialize monitor\n");
            free(g_monitor);
            g_monitor = NULL;
            return SHELL_ERR_IO;
        }
    }

    if (argc < 2) {
        /* Default: show summary */
        phantom_monitor_print_summary(g_monitor);
        return SHELL_OK;
    }

    const char *action = argv[1];

    /* Summary view */
    if (strcmp(action, "summary") == 0 || strcmp(action, "all") == 0) {
        phantom_monitor_print_summary(g_monitor);
        return SHELL_OK;
    }

    /* Process list */
    if (strcmp(action, "procs") == 0 || strcmp(action, "ps") == 0 ||
        strcmp(action, "processes") == 0) {
        phantom_monitor_print_processes(g_monitor);
        return SHELL_OK;
    }

    /* Memory stats */
    if (strcmp(action, "mem") == 0 || strcmp(action, "memory") == 0) {
        phantom_monitor_print_memory(g_monitor);
        return SHELL_OK;
    }

    /* Geology stats */
    if (strcmp(action, "geo") == 0 || strcmp(action, "geology") == 0 ||
        strcmp(action, "storage") == 0) {
        phantom_monitor_print_geology(g_monitor);
        return SHELL_OK;
    }

    /* Network stats */
    if (strcmp(action, "net") == 0 || strcmp(action, "network") == 0) {
        phantom_monitor_print_network(g_monitor);
        return SHELL_OK;
    }

    /* Governor stats */
    if (strcmp(action, "gov") == 0 || strcmp(action, "governor") == 0) {
        phantom_monitor_print_governor(g_monitor);
        return SHELL_OK;
    }

    /* Help */
    if (strcmp(action, "help") == 0) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                     PHANTOM SYSTEM MONITOR                            ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("  Usage: monitor [view]\n");
        printf("\n");
        printf("  Views:\n");
        printf("    summary     - Full system overview (default)\n");
        printf("    procs       - Process list with details\n");
        printf("    mem         - Memory statistics\n");
        printf("    geo         - Geology (storage) statistics\n");
        printf("    net         - Network statistics\n");
        printf("    gov         - Governor (AI code evaluator) statistics\n");
        printf("\n");
        return SHELL_OK;
    }

    printf("Unknown monitor view: %s\n", action);
    printf("Use 'monitor help' for available views.\n");
    return SHELL_ERR_ARGS;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * STORAGE COMMAND - Quota, Monitoring, Backup
 * ══════════════════════════════════════════════════════════════════════════════ */

static phantom_storage_manager_t *g_storage_mgr = NULL;

static shell_result_t cmd_storage(struct shell_context *ctx, int argc, char **argv) {
    /* Initialize storage manager if needed */
    if (!g_storage_mgr) {
        g_storage_mgr = malloc(sizeof(phantom_storage_manager_t));
        if (!g_storage_mgr) {
            printf("Failed to allocate storage manager\n");
            return SHELL_ERR_NOMEM;
        }
        if (phantom_storage_init(g_storage_mgr, ctx->kernel, ctx->kernel->geofs_volume) != 0) {
            printf("Failed to initialize storage manager\n");
            free(g_storage_mgr);
            g_storage_mgr = NULL;
            return SHELL_ERR_IO;
        }
    }

    if (argc < 2) {
        /* Default: show status */
        phantom_storage_print_report(g_storage_mgr);
        return SHELL_OK;
    }

    const char *action = argv[1];

    /* Status report */
    if (strcmp(action, "status") == 0 || strcmp(action, "report") == 0) {
        phantom_storage_print_report(g_storage_mgr);
        return SHELL_OK;
    }

    /* Check for warnings */
    if (strcmp(action, "check") == 0) {
        int level = phantom_storage_check(g_storage_mgr);
        printf("Storage check complete. Warning level: %s\n",
               phantom_storage_warning_str(level));
        return SHELL_OK;
    }

    /* Quota management */
    if (strcmp(action, "quota") == 0) {
        if (argc < 3) {
            /* Show current user's quota */
            char report[512];
            phantom_storage_quota_report(g_storage_mgr, ctx->uid, report, sizeof(report));
            printf("\n%s\n\n", report);
            return SHELL_OK;
        }

        const char *subaction = argv[2];

        if (strcmp(subaction, "enable") == 0) {
            phantom_storage_enable_quotas(g_storage_mgr, 1);
            printf("Quotas enabled.\n");
            return SHELL_OK;
        }

        if (strcmp(subaction, "disable") == 0) {
            phantom_storage_enable_quotas(g_storage_mgr, 0);
            printf("Quotas disabled.\n");
            return SHELL_OK;
        }

        if (strcmp(subaction, "set") == 0) {
            if (argc < 5) {
                printf("Usage: storage quota set <uid> <limit>\n");
                printf("  limit: bytes or with suffix (K, M, G, T)\n");
                printf("  Example: storage quota set 1000 5G\n");
                return SHELL_ERR_ARGS;
            }

            uint32_t uid = (uint32_t)atoi(argv[3]);
            const char *limit_str = argv[4];

            /* Parse limit with suffix */
            char *end;
            uint64_t limit = strtoull(limit_str, &end, 10);
            if (*end) {
                switch (*end) {
                    case 'K': case 'k': limit *= 1024; break;
                    case 'M': case 'm': limit *= 1024 * 1024; break;
                    case 'G': case 'g': limit *= 1024 * 1024 * 1024; break;
                    case 'T': case 't': limit *= 1024ULL * 1024 * 1024 * 1024; break;
                    default:
                        printf("Invalid suffix: %c (use K, M, G, or T)\n", *end);
                        return SHELL_ERR_ARGS;
                }
            }

            phantom_storage_set_quota(g_storage_mgr, uid, limit, 0);
            printf("Quota set for UID %u.\n", uid);
            return SHELL_OK;
        }

        if (strcmp(subaction, "show") == 0) {
            uint32_t uid = ctx->uid;
            if (argc > 3) {
                uid = (uint32_t)atoi(argv[3]);
            }

            char report[512];
            phantom_storage_quota_report(g_storage_mgr, uid, report, sizeof(report));
            printf("\n%s\n\n", report);
            return SHELL_OK;
        }

        printf("Unknown quota action: %s\n", subaction);
        printf("Use: quota [enable|disable|set|show]\n");
        return SHELL_ERR_ARGS;
    }

    /* Backup */
    if (strcmp(action, "backup") == 0) {
        if (argc < 3) {
            printf("Usage: storage backup <destination>\n");
            printf("  Creates a backup of the entire geology.\n");
            printf("  Example: storage backup /home/user/phantom-backup.tar\n");
            return SHELL_ERR_ARGS;
        }

        phantom_backup_options_t opts = {0};
        opts.destination_path = argv[2];
        opts.include_hidden = 1;
        opts.include_all_views = 1;
        opts.compress = 0;

        phantom_backup_result_t result;
        if (phantom_storage_backup(g_storage_mgr, &opts, &result) == 0 && result.success) {
            char size_str[32];
            phantom_storage_format_bytes(result.bytes_written, size_str, sizeof(size_str));
            printf("\nBackup created successfully!\n");
            printf("  Location: %s\n", opts.destination_path);
            printf("  Size: %s\n", size_str);
            printf("  Files: %lu\n", (unsigned long)result.files_backed_up);
            printf("  Views: %lu\n", (unsigned long)result.views_backed_up);
            printf("\n");
        } else {
            printf("Backup failed: %s\n", result.error_message);
            return SHELL_ERR_IO;
        }
        return SHELL_OK;
    }

    /* Restore */
    if (strcmp(action, "restore") == 0) {
        if (argc < 3) {
            printf("Usage: storage restore <backup-file> [--merge]\n");
            printf("  Restores from a backup file.\n");
            printf("  --merge: Merge with existing data (default: replace)\n");
            return SHELL_ERR_ARGS;
        }

        int merge_mode = 0;
        if (argc > 3 && strcmp(argv[3], "--merge") == 0) {
            merge_mode = 1;
        }

        printf("Restoring from %s (mode: %s)...\n",
               argv[2], merge_mode ? "merge" : "replace");

        if (phantom_storage_restore(g_storage_mgr, argv[2], merge_mode) == 0) {
            printf("Restore complete.\n");
        } else {
            printf("Restore failed.\n");
            return SHELL_ERR_IO;
        }
        return SHELL_OK;
    }

    /* Archive old views */
    if (strcmp(action, "archive") == 0) {
        if (argc < 3) {
            printf("Usage: storage archive <destination> [--older-than <days>]\n");
            printf("  Archives old geology views to external storage.\n");
            printf("  This frees active space while preserving history.\n");
            printf("\n");
            printf("  Example: storage archive /mnt/archive/phantom --older-than 30\n");
            return SHELL_ERR_ARGS;
        }

        phantom_archive_options_t opts = {0};
        opts.archive_path = argv[2];
        opts.views_to_archive = 10;  /* Default: 10 oldest views */

        /* Parse --older-than option */
        for (int i = 3; i < argc - 1; i++) {
            if (strcmp(argv[i], "--older-than") == 0) {
                opts.views_to_archive = (uint64_t)atoi(argv[i + 1]);
            }
        }

        printf("Archiving views to %s...\n", opts.archive_path);
        if (phantom_storage_archive_views(g_storage_mgr, &opts) == 0) {
            printf("Archive complete. Data preserved at: %s\n", opts.archive_path);
        } else {
            printf("Archive failed.\n");
            return SHELL_ERR_IO;
        }
        return SHELL_OK;
    }

    /* Calculate reclaimable space */
    if (strcmp(action, "reclaimable") == 0 || strcmp(action, "analyze") == 0) {
        uint64_t hidden_bytes, old_view_bytes, dedup_bytes;
        phantom_storage_calc_reclaimable(g_storage_mgr, &hidden_bytes,
                                         &old_view_bytes, &dedup_bytes);

        char h_str[32], o_str[32];
        phantom_storage_format_bytes(hidden_bytes, h_str, sizeof(h_str));
        phantom_storage_format_bytes(old_view_bytes, o_str, sizeof(o_str));

        printf("\nReclaimable Space Analysis:\n");
        printf("  Hidden files (can be archived): %s\n", h_str);
        printf("  Old views (can be archived):    %s\n", o_str);
        printf("\n");
        printf("Note: Archiving moves data to external storage - nothing is deleted!\n");
        printf("Use 'storage archive <path>' to reclaim space.\n\n");
        return SHELL_OK;
    }

    /* Help */
    if (strcmp(action, "help") == 0) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                    PHANTOM STORAGE MANAGER                            ║\n");
        printf("║                  \"To Create, Not To Destroy\"                          ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("  Usage: storage <command> [args]\n");
        printf("\n");
        printf("  Commands:\n");
        printf("    status          - Show storage status report (default)\n");
        printf("    check           - Check storage and show warning level\n");
        printf("    quota           - Show your quota usage\n");
        printf("    quota enable    - Enable quota enforcement\n");
        printf("    quota disable   - Disable quota enforcement\n");
        printf("    quota set <uid> <limit>  - Set quota for user\n");
        printf("    quota show [uid]         - Show quota for user\n");
        printf("    backup <path>   - Create full backup\n");
        printf("    restore <path>  - Restore from backup\n");
        printf("    archive <path>  - Archive old views to free space\n");
        printf("    reclaimable     - Analyze reclaimable space\n");
        printf("\n");
        printf("  Size suffixes: K (KB), M (MB), G (GB), T (TB)\n");
        printf("  Example: storage quota set 1000 5G\n");
        printf("\n");
        printf("  Philosophy: We never delete data. Archiving moves data to external\n");
        printf("  storage while freeing active space. Your history is always preserved.\n");
        printf("\n");
        return SHELL_OK;
    }

    printf("Unknown storage command: %s\n", action);
    printf("Use 'storage help' for available commands.\n");
    return SHELL_ERR_ARGS;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * DNAuth - DNA-based Authentication
 * ══════════════════════════════════════════════════════════════════════════════ */

static shell_result_t cmd_dnauth(struct shell_context *ctx, int argc, char **argv) {
    dnauth_system_t *dnauth = ctx->kernel->dnauth;

    if (!dnauth) {
        printf("DNAuth system not initialized.\n");
        return SHELL_ERR_IO;
    }

    if (argc < 2) {
        /* Default: show status */
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                           DNAUTH STATUS                               ║\n");
        printf("║                    \"Your Code is Your Key\"                            ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("  System Status:      %s\n", dnauth->initialized ? "Initialized" : "Not initialized");
        printf("  Evolution:          %s\n", dnauth->evolution_enabled ? "Enabled" : "Disabled");
        printf("  Default Mode:       %s\n", dnauth_mode_string(dnauth->default_mode));
        printf("  Min Sequence Len:   %u nucleotides\n", dnauth->min_sequence_length);
        printf("  Min Complexity:     %s\n", dnauth_complexity_string(dnauth->min_complexity));
        printf("  Max Mutations:      %d\n", dnauth->default_max_mutations);
        printf("  Mutation Rate:      %.2f%%\n", dnauth->default_mutation_rate * 100.0);
        printf("  Registered Keys:    %u\n", dnauth->key_count);
        printf("  Lineages:           %u\n", dnauth->lineage_count);
        printf("\n");
        return SHELL_OK;
    }

    const char *action = argv[1];

    /* Status command */
    if (strcmp(action, "status") == 0) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                           DNAUTH STATUS                               ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("  System:             %s\n", dnauth->initialized ? "Active" : "Inactive");
        printf("  Evolution:          %s\n", dnauth->evolution_enabled ? "Enabled" : "Disabled");
        printf("  Mode:               %s\n", dnauth_mode_string(dnauth->default_mode));
        printf("  KDF:                %s\n", dnauth->default_kdf == DNAUTH_KDF_CODON ? "Codon" :
                                            dnauth->default_kdf == DNAUTH_KDF_BINARY ? "Binary" :
                                            dnauth->default_kdf == DNAUTH_KDF_COMPLEMENT ? "Complement" : "Transcription");
        printf("  Registered Keys:    %u\n", dnauth->key_count);
        printf("  Active Lineages:    %u\n", dnauth->lineage_count);
        printf("\n");
        return SHELL_OK;
    }

    /* Register a new DNA key */
    if (strcmp(action, "register") == 0) {
        if (argc < 4) {
            printf("Usage: dnauth register <user_id> <sequence>\n");
            printf("\n");
            printf("  Registers a new DNA sequence as an authentication key.\n");
            printf("\n");
            printf("  Sequence must be:\n");
            printf("    - At least %u nucleotides (ATGC)\n", dnauth->min_sequence_length);
            printf("    - Have sufficient complexity\n");
            printf("    - Spaces are allowed for readability\n");
            printf("\n");
            printf("  Example: dnauth register myuser ATGCTAGCATCGATCG\n");
            return SHELL_ERR_ARGS;
        }

        const char *user_id = argv[2];
        const char *sequence = argv[3];

        /* Validate sequence first */
        char error[256];
        if (!dnauth_sequence_validate(sequence, error, sizeof(error))) {
            printf("Invalid sequence: %s\n", error);
            return SHELL_ERR_ARGS;
        }

        dnauth_result_t result = dnauth_register(dnauth, user_id, sequence);
        if (result == DNAUTH_OK) {
            printf("Successfully registered DNA key for user '%s'\n", user_id);
            printf("Key length: %zu nucleotides\n", strlen(sequence));

            /* Create lineage for evolution */
            if (dnauth->evolution_enabled) {
                dnauth_lineage_t *lineage = dnauth_lineage_create(dnauth, user_id, sequence);
                if (lineage) {
                    printf("Evolution lineage created. Generation: 1\n");
                }
            }
            return SHELL_OK;
        } else {
            printf("Registration failed: %s\n", dnauth_result_string(result));
            return SHELL_ERR_IO;
        }
    }

    /* Authenticate with a DNA sequence */
    if (strcmp(action, "auth") == 0 || strcmp(action, "authenticate") == 0) {
        if (argc < 4) {
            printf("Usage: dnauth auth <user_id> <sequence>\n");
            printf("\n");
            printf("  Authenticates using a DNA sequence.\n");
            printf("  Supports fuzzy matching with configured mutation tolerance.\n");
            printf("\n");
            printf("  Example: dnauth auth myuser ATGCTAGCATCGATCG\n");
            return SHELL_ERR_ARGS;
        }

        const char *user_id = argv[2];
        const char *sequence = argv[3];

        dnauth_result_t result = dnauth_authenticate(dnauth, user_id, sequence);
        if (result == DNAUTH_OK) {
            printf("Authentication successful for '%s'\n", user_id);

            /* Show fitness if evolution is enabled */
            if (dnauth->evolution_enabled) {
                double fitness = dnauth_get_fitness(dnauth, user_id);
                int gen = dnauth_get_generation_number(dnauth, user_id);
                printf("  Generation: %d\n", gen);
                printf("  Fitness:    %.2f%%\n", fitness * 100.0);
            }
            return SHELL_OK;
        } else {
            printf("Authentication failed: %s\n", dnauth_result_string(result));

            /* Try ancestor auth if enabled */
            if (dnauth->evolution_enabled && result == DNAUTH_ERR_NO_MATCH) {
                int gen_matched = -1;
                result = dnauth_authenticate_ancestor(dnauth, user_id, sequence, 5, &gen_matched);
                if (result == DNAUTH_OK) {
                    printf("  Note: Matched ancestor sequence from %d generation(s) ago\n", gen_matched);
                    printf("  Warning: Using outdated key. Please update to current sequence.\n");
                    return SHELL_OK;
                }
            }
            return SHELL_ERR_IO;
        }
    }

    /* Force evolution of a key */
    if (strcmp(action, "evolve") == 0) {
        if (!dnauth->evolution_enabled) {
            printf("Evolution is not enabled.\n");
            printf("Use 'dnauth evolution enable' to enable it.\n");
            return SHELL_ERR_ARGS;
        }

        if (argc < 3) {
            printf("Usage: dnauth evolve <user_id> [mutations]\n");
            printf("\n");
            printf("  Forces evolution of a DNA key.\n");
            printf("  This introduces mutations simulating natural evolution.\n");
            printf("\n");
            printf("  Options:\n");
            printf("    mutations  - Number of mutations to apply (default: 1)\n");
            printf("\n");
            printf("  Example: dnauth evolve myuser 2\n");
            return SHELL_ERR_ARGS;
        }

        const char *user_id = argv[2];
        int num_mutations = 1;
        if (argc > 3) {
            num_mutations = atoi(argv[3]);
            if (num_mutations < 1) num_mutations = 1;
            if (num_mutations > DNAUTH_MAX_MUTATIONS_PER_GEN) num_mutations = DNAUTH_MAX_MUTATIONS_PER_GEN;
        }

        dnauth_evolution_event_t *event = dnauth_evolve_forced(dnauth, user_id, num_mutations);
        if (event) {
            printf("Evolution complete for '%s'\n", user_id);
            printf("  Mutations applied: %u\n", event->mutation_count);
            printf("  New generation:    %u\n", event->to_generation);

            /* Show mutation details */
            for (uint32_t i = 0; i < event->mutation_count && i < DNAUTH_MAX_MUTATIONS_PER_GEN; i++) {
                printf("  [%u] %s at position %u: %c -> %c\n",
                       i + 1,
                       dnauth_evolution_type_string(event->mutations[i].type),
                       event->mutations[i].position,
                       event->mutations[i].original,
                       event->mutations[i].mutated);
            }

            /* Show new sequence */
            char *new_seq = dnauth_get_current_sequence(dnauth, user_id);
            if (new_seq) {
                printf("  New sequence:      %.20s...\n", new_seq);
                free(new_seq);
            }

            return SHELL_OK;
        } else {
            printf("Evolution failed for '%s'\n", user_id);
            printf("Make sure the user has a registered key with a lineage.\n");
            return SHELL_ERR_IO;
        }
    }

    /* Show lineage history */
    if (strcmp(action, "lineage") == 0) {
        if (argc < 3) {
            printf("Usage: dnauth lineage <user_id>\n");
            printf("\n");
            printf("  Shows the evolutionary lineage of a DNA key.\n");
            printf("  Displays all generations from origin to current.\n");
            printf("\n");
            printf("  Example: dnauth lineage myuser\n");
            return SHELL_ERR_ARGS;
        }

        const char *user_id = argv[2];
        dnauth_lineage_t *lineage = dnauth_lineage_get(dnauth, user_id);

        if (!lineage) {
            printf("No lineage found for user '%s'\n", user_id);
            return SHELL_ERR_NOTFOUND;
        }

        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                         LINEAGE: %-36s ║\n", user_id);
        printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("  Total Generations:   %u\n", lineage->total_generations);
        printf("  Total Mutations:     %u\n", lineage->total_mutations);
        printf("  Current Fitness:     %.2f%%\n", lineage->cumulative_fitness * 100.0);
        printf("\n");

        if (lineage->current && lineage->current->sequence) {
            printf("  Current Sequence:\n");
            const char *seq = lineage->current->sequence;
            size_t len = strlen(seq);

            /* Print sequence in rows of 60 with position markers */
            for (size_t i = 0; i < len; i += 60) {
                printf("    %4zu  ", i + 1);
                for (size_t j = 0; j < 60 && i + j < len; j++) {
                    putchar(seq[i + j]);
                    if ((j + 1) % 10 == 0 && j + 1 < 60 && i + j + 1 < len) {
                        putchar(' ');
                    }
                }
                printf("\n");
            }
        }

        printf("\n");
        printf("  Lineage Tree:\n");

        /* Walk the ancestor chain */
        dnauth_generation_t *gen = lineage->current;
        int depth = 0;
        while (gen && depth < 10) {
            char seq_preview[21];
            if (gen->sequence) {
                strncpy(seq_preview, gen->sequence, 20);
                seq_preview[20] = '\0';
            } else {
                strcpy(seq_preview, "(no sequence)");
            }

            printf("    %s Gen %u: %s...\n",
                   depth == 0 ? "└──" : "   └──",
                   gen->generation_id,
                   seq_preview);

            gen = gen->next;  /* Walk through generation chain */
            depth++;
        }

        printf("\n");
        return SHELL_OK;
    }

    /* Show fitness information */
    if (strcmp(action, "fitness") == 0) {
        if (argc < 3) {
            printf("Usage: dnauth fitness <user_id>\n");
            printf("\n");
            printf("  Shows the fitness score and health of a DNA key.\n");
            printf("  Fitness determines how well the key is adapting.\n");
            printf("\n");
            printf("  Example: dnauth fitness myuser\n");
            return SHELL_ERR_ARGS;
        }

        const char *user_id = argv[2];
        double fitness = dnauth_get_fitness(dnauth, user_id);

        if (fitness < 0) {
            printf("No fitness data found for user '%s'\n", user_id);
            return SHELL_ERR_NOTFOUND;
        }

        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                      FITNESS REPORT                                   ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("  User ID:         %s\n", user_id);
        printf("  Fitness Score:   %.2f%%\n", fitness * 100.0);
        printf("\n");

        /* Fitness assessment */
        printf("  Assessment:      ");
        if (fitness >= 0.9) {
            printf("Excellent - Key is highly fit\n");
        } else if (fitness >= 0.7) {
            printf("Good - Key is healthy\n");
        } else if (fitness >= 0.5) {
            printf("Fair - Consider evolving the key\n");
        } else if (fitness >= 0.3) {
            printf("Poor - Key needs evolution\n");
        } else {
            printf("Critical - Key at risk of extinction\n");
        }

        int gen = dnauth_get_generation_number(dnauth, user_id);
        printf("  Generation:      %d\n", gen);
        printf("\n");

        return SHELL_OK;
    }

    /* Enable/disable evolution */
    if (strcmp(action, "evolution") == 0) {
        if (argc < 3) {
            printf("Usage: dnauth evolution <enable|disable|status>\n");
            printf("\n");
            printf("  Controls the evolution system.\n");
            printf("  Evolution allows DNA keys to mutate over time.\n");
            return SHELL_ERR_ARGS;
        }

        const char *subaction = argv[2];

        if (strcmp(subaction, "enable") == 0) {
            dnauth_evolution_enable(dnauth, 1);
            printf("Evolution system enabled.\n");
            return SHELL_OK;
        }

        if (strcmp(subaction, "disable") == 0) {
            dnauth_evolution_enable(dnauth, 0);
            printf("Evolution system disabled.\n");
            return SHELL_OK;
        }

        if (strcmp(subaction, "status") == 0) {
            printf("\n");
            printf("Evolution System Status:\n");
            printf("  Enabled:            %s\n", dnauth->evolution_enabled ? "Yes" : "No");
            printf("  Default Interval:   %d seconds\n", dnauth->default_evolution_interval);
            printf("  Mutation Rate:      %.2f%%\n", dnauth->default_mutation_rate * 100.0);
            printf("  Ancestor Auth:      %s (max %d generations)\n",
                   dnauth->default_allow_ancestors ? "Enabled" : "Disabled",
                   dnauth->default_max_ancestor_depth);
            printf("  Active Lineages:    %u\n", dnauth->lineage_count);
            printf("\n");
            return SHELL_OK;
        }

        printf("Unknown evolution subcommand: %s\n", subaction);
        return SHELL_ERR_ARGS;
    }

    /* Validate a sequence without registering */
    if (strcmp(action, "validate") == 0) {
        if (argc < 3) {
            printf("Usage: dnauth validate <sequence>\n");
            printf("\n");
            printf("  Validates a DNA sequence without registering it.\n");
            printf("  Checks length, characters, and complexity.\n");
            return SHELL_ERR_ARGS;
        }

        const char *sequence = argv[2];
        char error[256];

        if (dnauth_sequence_validate(sequence, error, sizeof(error))) {
            printf("Sequence is valid.\n");

            /* Parse and show details */
            dnauth_sequence_t *seq = dnauth_sequence_parse(sequence);
            if (seq) {
                printf("\n  Length:        %u nucleotides\n", seq->length);
                printf("  Composition:   A=%u T=%u G=%u C=%u\n",
                       seq->count_a, seq->count_t, seq->count_g, seq->count_c);
                printf("  GC Content:    %.1f%%\n", seq->gc_content * 100.0);

                dnauth_complexity_t complexity = dnauth_compute_complexity(seq);
                printf("  Complexity:    %s\n", dnauth_complexity_string(complexity));

                double entropy = dnauth_compute_entropy(seq);
                printf("  Entropy:       %.3f bits\n", entropy);

                dnauth_sequence_free(seq);
            }
            return SHELL_OK;
        } else {
            printf("Sequence is invalid: %s\n", error);
            return SHELL_ERR_ARGS;
        }
    }

    /* Show help */
    if (strcmp(action, "help") == 0) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
        printf("║                       DNAUTH COMMAND HELP                             ║\n");
        printf("║                    \"Your Code is Your Key\"                            ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("  DNAuth uses DNA sequences as authentication keys.\n");
        printf("  Keys can evolve over time, simulating natural selection.\n");
        printf("\n");
        printf("  Commands:\n");
        printf("    status              - Show DNAuth system status\n");
        printf("    register <user> <seq>  - Register a DNA key\n");
        printf("    auth <user> <seq>   - Authenticate with a DNA key\n");
        printf("    evolve <user> [n]   - Force n mutations on a key\n");
        printf("    lineage <user>      - Show evolutionary lineage\n");
        printf("    fitness <user>      - Show fitness score\n");
        printf("    evolution <cmd>     - Enable/disable/status evolution\n");
        printf("    validate <seq>      - Validate a sequence\n");
        printf("\n");
        printf("  DNA Sequence Format:\n");
        printf("    - Use nucleotide letters: A, T, G, C\n");
        printf("    - Minimum length: %u nucleotides\n", dnauth->min_sequence_length);
        printf("    - Spaces are allowed for readability\n");
        printf("\n");
        printf("  Example:\n");
        printf("    dnauth register alice ATGC TAGC ATCG ATCG\n");
        printf("    dnauth auth alice ATGCTAGCATCGATCG\n");
        printf("    dnauth evolve alice 2\n");
        printf("\n");
        return SHELL_OK;
    }

    printf("Unknown dnauth command: %s\n", action);
    printf("Use 'dnauth help' for available commands.\n");
    return SHELL_ERR_ARGS;
}
