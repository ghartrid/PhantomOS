/*
 * PhantomOS Kernel Shell
 * "To Create, Not To Destroy"
 *
 * Interactive command-line shell for PhantomOS kernel.
 * All commands follow the Phantom philosophy.
 */

#ifndef PHANTOMOS_KERNEL_SHELL_H
#define PHANTOMOS_KERNEL_SHELL_H

#include <stdint.h>
#include <stddef.h>
#include "geofs.h"

/*============================================================================
 * Constants
 *============================================================================*/

#define SHELL_CMD_MAX       256     /* Max command length */
#define SHELL_ARGS_MAX      16      /* Max number of arguments */
#define SHELL_PROMPT        "phantom> "

/*============================================================================
 * Types
 *============================================================================*/

/* Command result */
typedef enum {
    SHELL_OK = 0,
    SHELL_ERR_NOTFOUND,     /* Command not found */
    SHELL_ERR_ARGS,         /* Invalid arguments */
    SHELL_ERR_IO,           /* I/O error */
    SHELL_ERR_DECLINED,     /* Governor declined */
    SHELL_EXIT,             /* Exit shell */
} shell_result_t;

/*============================================================================
 * Shell API
 *============================================================================*/

/*
 * Initialize the shell
 * @volume: GeoFS volume to use
 */
void shell_init(kgeofs_volume_t *volume);

/*
 * Run the shell (blocking main loop)
 * This function runs until 'exit' command
 */
void shell_run(void);

/*
 * Execute a single command
 * @cmd: Command string
 * Returns: shell_result_t
 */
shell_result_t shell_execute(const char *cmd);

/*
 * Print shell help
 */
void shell_help(void);

/*============================================================================
 * Built-in Commands
 *============================================================================*/

/*
 * File System Commands:
 *   ls [path]           - List directory contents
 *   cat <file>          - Display file contents
 *   write <file> <text> - Write text to file
 *   mkdir <path>        - Create directory
 *   hide <file>         - Hide file (Phantom delete)
 *   pwd                 - Print working directory
 *   cd <path>           - Change directory
 *   stat <path>         - Show file info
 *
 * Views (Geological Strata):
 *   views               - List all views
 *   view <id>           - Switch to view
 *   snapshot <name>     - Create new view
 *
 * Process Commands:
 *   ps                  - List processes
 *
 * System Commands:
 *   help                - Show help
 *   clear               - Clear screen
 *   mem                 - Show memory statistics
 *   disk                - Show disk info
 *   gov                 - Show Governor statistics
 *   uptime              - Show uptime
 *   echo <text>         - Echo text
 *   exit                - Exit shell (return to kernel)
 */

#endif /* PHANTOMOS_KERNEL_SHELL_H */
