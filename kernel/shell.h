/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                           PHANTOM SHELL
 *                    "To Create, Not To Destroy"
 *
 *    An interactive command interpreter for PhantomOS.
 *    Commands create, view, and organize - but never destroy.
 *
 * ══════════════════════════════════════════════════════════════════════════════
 */

#ifndef PHANTOM_SHELL_H
#define PHANTOM_SHELL_H

#include <stdint.h>
#include <stddef.h>
#include "phantom.h"
#include "vfs.h"
#include "phantom_user.h"

/* ══════════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ══════════════════════════════════════════════════════════════════════════════ */

#define SHELL_MAX_INPUT         1024
#define SHELL_MAX_ARGS          64
#define SHELL_MAX_PATH          VFS_MAX_PATH
#define SHELL_HISTORY_SIZE      100
#define SHELL_MAX_ALIASES       32
#define SHELL_MAX_VARS          64

/* ══════════════════════════════════════════════════════════════════════════════
 * TYPES
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Command execution result */
typedef enum {
    SHELL_OK            =  0,
    SHELL_ERR_NOTFOUND  = -1,   /* Command not found */
    SHELL_ERR_ARGS      = -2,   /* Invalid arguments */
    SHELL_ERR_PERM      = -3,   /* Permission denied */
    SHELL_ERR_IO        = -4,   /* I/O error */
    SHELL_ERR_SYNTAX    = -5,   /* Syntax error */
    SHELL_ERR_NOMEM     = -6,   /* Out of memory */
    SHELL_ERR_DECLINED  = -7,   /* Governor declined */
    SHELL_EXIT          = 1,    /* Shell should exit */
} shell_result_t;

/* Command handler function type */
struct shell_context;
typedef shell_result_t (*shell_cmd_fn)(struct shell_context *ctx,
                                        int argc, char **argv);

/* ══════════════════════════════════════════════════════════════════════════════
 * STRUCTURES
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Built-in command definition
 */
struct shell_command {
    const char         *name;       /* Command name */
    const char         *help;       /* Short help text */
    const char         *usage;      /* Usage string */
    shell_cmd_fn        handler;    /* Handler function */
    int                 min_args;   /* Minimum arguments */
    int                 max_args;   /* Maximum arguments (-1 = unlimited) */
};

/*
 * Command alias
 */
struct shell_alias {
    char                name[64];
    char                value[256];
};

/*
 * Shell variable
 */
struct shell_var {
    char                name[64];
    char                value[256];
};

/*
 * History entry - stored in GeoFS for persistence
 */
struct shell_history_entry {
    char                command[SHELL_MAX_INPUT];
    phantom_time_t      executed_at;
    shell_result_t      result;
    phantom_pid_t       pid;
};

/*
 * Shell context - maintains shell state
 */
struct shell_context {
    /* Kernel references */
    struct phantom_kernel  *kernel;
    struct vfs_context     *vfs;
    phantom_pid_t           pid;        /* Shell's process ID */

    /* User authentication */
    phantom_user_system_t  *user_system;            /* User system reference */
    phantom_session_t      *session;                /* Current login session */
    uint32_t                uid;                    /* Current user ID */
    char                    username[PHANTOM_MAX_USERNAME];  /* Current username */

    /* Current state */
    char                    cwd[SHELL_MAX_PATH];    /* Current working directory */
    int                     running;                 /* Shell is active */
    int                     interactive;             /* Interactive mode */
    shell_result_t          last_result;             /* Last command result */

    /* History (append-only, of course) */
    struct shell_history_entry *history;
    size_t                  history_count;
    size_t                  history_capacity;

    /* Aliases */
    struct shell_alias      aliases[SHELL_MAX_ALIASES];
    size_t                  alias_count;

    /* Variables */
    struct shell_var        vars[SHELL_MAX_VARS];
    size_t                  var_count;

    /* Input handling */
    char                    input_buffer[SHELL_MAX_INPUT];
    int                     input_pos;

    /* Statistics */
    uint64_t                commands_executed;
    uint64_t                commands_successful;
    uint64_t                commands_failed;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * SHELL API
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Initialize a shell context
 */
shell_result_t shell_init(struct shell_context *ctx,
                          struct phantom_kernel *kernel,
                          struct vfs_context *vfs);

/*
 * Set user system for authentication
 */
void shell_set_user_system(struct shell_context *ctx, phantom_user_system_t *user_sys);

/*
 * Display login prompt and authenticate user
 * Returns 0 on success, -1 on failure/exit
 */
int shell_login(struct shell_context *ctx);

/*
 * Run the shell interactively (reads from stdin)
 * Requires successful login first
 */
shell_result_t shell_run(struct shell_context *ctx);

/*
 * Execute a single command line
 */
shell_result_t shell_execute(struct shell_context *ctx, const char *line);

/*
 * Execute a script file
 */
shell_result_t shell_run_script(struct shell_context *ctx, const char *path);

/*
 * Cleanup shell context
 */
void shell_cleanup(struct shell_context *ctx);

/* ══════════════════════════════════════════════════════════════════════════════
 * VARIABLE & ALIAS MANAGEMENT
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Set a shell variable
 */
shell_result_t shell_set_var(struct shell_context *ctx,
                              const char *name, const char *value);

/*
 * Get a shell variable (returns NULL if not set)
 */
const char *shell_get_var(struct shell_context *ctx, const char *name);

/*
 * Set an alias
 */
shell_result_t shell_set_alias(struct shell_context *ctx,
                                const char *name, const char *value);

/*
 * Expand aliases in a command line
 */
shell_result_t shell_expand_aliases(struct shell_context *ctx,
                                     char *line, size_t max_len);

/* ══════════════════════════════════════════════════════════════════════════════
 * HISTORY MANAGEMENT
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Add command to history (always - history is append-only)
 */
shell_result_t shell_history_add(struct shell_context *ctx,
                                  const char *command,
                                  shell_result_t result);

/*
 * Get history entry by index (0 = oldest)
 */
const struct shell_history_entry *shell_history_get(struct shell_context *ctx,
                                                     size_t index);

/*
 * Search history for pattern
 */
const struct shell_history_entry *shell_history_search(struct shell_context *ctx,
                                                        const char *pattern);

/* ══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Parse a command line into argc/argv
 */
int shell_parse_line(char *line, char **argv, int max_args);

/*
 * Print formatted prompt
 */
void shell_print_prompt(struct shell_context *ctx);

/*
 * Get error string
 */
const char *shell_strerror(shell_result_t err);

/* ══════════════════════════════════════════════════════════════════════════════
 * BUILT-IN COMMANDS
 * These embody the Phantom philosophy - create, view, organize, never destroy
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Note: There is no 'rm', 'rmdir', 'kill', 'unlink' - these would violate
 * the Prime Directive. Instead we have:
 *
 * - hide: Make a file invisible (but preserved in geology)
 * - suspend: Put a process to sleep (not kill)
 * - archive: Mark data as historical (not delete)
 */

/* Get the list of built-in commands */
const struct shell_command *shell_get_builtins(size_t *count);

#endif /* PHANTOM_SHELL_H */
