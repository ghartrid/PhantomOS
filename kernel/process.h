/*
 * PhantomOS Process Management
 * "To Create, Not To Destroy"
 *
 * Process Control Block (PCB) and scheduler definitions.
 * Implements cooperative and preemptive multitasking.
 */

#ifndef PHANTOMOS_PROCESS_H
#define PHANTOMOS_PROCESS_H

#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define PROCESS_MAX             64          /* Maximum concurrent processes */
#define PROCESS_STACK_SIZE      (16 * 1024) /* 16KB stack per process */
#define PROCESS_NAME_MAX        32          /* Max process name length */

/* Process IDs */
typedef uint32_t pid_t;
#define PID_INVALID             0
#define PID_KERNEL              1           /* Kernel/idle process */

/*============================================================================
 * Process States
 *============================================================================*/

typedef enum {
    PROCESS_STATE_FREE = 0,     /* Slot is unused */
    PROCESS_STATE_CREATED,      /* Created but not yet run */
    PROCESS_STATE_READY,        /* Ready to run */
    PROCESS_STATE_RUNNING,      /* Currently running */
    PROCESS_STATE_BLOCKED,      /* Waiting for something */
    PROCESS_STATE_ZOMBIE,       /* Terminated, waiting for cleanup */
} process_state_t;

/*============================================================================
 * CPU Context (saved on context switch)
 *
 * x86-64 calling convention: RDI, RSI, RDX, RCX, R8, R9 are caller-saved
 * We save all general-purpose registers plus RFLAGS and RIP
 *============================================================================*/

struct cpu_context {
    /* General purpose registers (callee-saved first) */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;

    /* Caller-saved (saved for completeness during interrupt) */
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;

    /* Instruction pointer and flags */
    uint64_t rip;
    uint64_t rflags;

    /* Stack pointer */
    uint64_t rsp;
};

/*============================================================================
 * Process Control Block (PCB)
 *============================================================================*/

struct process {
    /* Identity */
    pid_t               pid;
    char                name[PROCESS_NAME_MAX];
    process_state_t     state;

    /* Scheduling */
    uint32_t            priority;           /* 0 = highest */
    uint64_t            time_slice;         /* Ticks remaining in quantum */
    uint64_t            total_ticks;        /* Total CPU time used */

    /* CPU state */
    struct cpu_context  context;

    /* Stack */
    void               *stack_base;         /* Bottom of stack allocation */
    void               *stack_top;          /* Top of stack (initial RSP) */

    /* Links for scheduler queues */
    struct process     *next;
    struct process     *prev;

    /* Process tree */
    pid_t               parent_pid;
    struct process     *children;           /* Linked list of children */
    struct process     *sibling;            /* Next sibling */

    /* Exit status (when ZOMBIE) */
    int                 exit_code;

    /* Statistics (append-only, Phantom style) */
    uint64_t            created_tick;
    uint64_t            context_switches;
};

/*============================================================================
 * Scheduler Statistics
 *============================================================================*/

struct scheduler_stats {
    uint64_t    total_processes_created;
    uint64_t    total_context_switches;
    uint64_t    total_ticks;
    uint64_t    idle_ticks;
    uint32_t    active_processes;
    uint32_t    peak_processes;
};

/*============================================================================
 * Process Entry Point Type
 *============================================================================*/

typedef void (*process_entry_t)(void *arg);

/*============================================================================
 * Scheduler API
 *============================================================================*/

/*
 * Initialize the scheduler
 * Creates the kernel/idle process
 */
void sched_init(void);

/*
 * Start the scheduler (begins running processes)
 * Does not return
 */
void sched_start(void);

/*
 * Yield CPU to another process (cooperative)
 */
void sched_yield(void);

/*
 * Called by timer interrupt for preemptive scheduling
 */
void scheduler_tick(void);

/*
 * Get current running process
 */
struct process *sched_current(void);

/*
 * Get scheduler statistics
 */
void sched_get_stats(struct scheduler_stats *stats);

/*
 * Dump scheduler state for debugging
 */
void sched_dump(void);

/*============================================================================
 * Process API
 *============================================================================*/

/*
 * Create a new process
 *
 * @name:  Process name (for debugging)
 * @entry: Entry point function
 * @arg:   Argument passed to entry function
 * @return: PID on success, PID_INVALID on failure
 */
pid_t process_create(const char *name, process_entry_t entry, void *arg);

/*
 * Exit current process
 *
 * @exit_code: Exit status
 */
void process_exit(int exit_code);

/*
 * Get process by PID
 *
 * @pid: Process ID
 * @return: Process pointer or NULL
 */
struct process *process_get(pid_t pid);

/*
 * Get current process PID
 */
pid_t process_getpid(void);

/*
 * Sleep current process for specified milliseconds
 */
void process_sleep_ms(uint32_t ms);

/*
 * Block current process (used internally)
 */
void process_block(void);

/*
 * Unblock a process (make it ready)
 */
void process_unblock(struct process *proc);

/*============================================================================
 * Context Switch (assembly)
 *============================================================================*/

/*
 * Switch from one process context to another
 * Defined in context_switch.S
 *
 * @old_ctx: Pointer to save current context
 * @new_ctx: Pointer to load new context
 */
extern void context_switch(struct cpu_context *old_ctx,
                           struct cpu_context *new_ctx);

/*
 * Start a new process (loads context, never returns)
 * Used for the first run of a process
 */
extern void context_start(struct cpu_context *ctx);

#endif /* PHANTOMOS_PROCESS_H */
