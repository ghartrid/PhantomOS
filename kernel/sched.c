/*
 * PhantomOS Process Scheduler
 * "To Create, Not To Destroy"
 *
 * Simple round-robin preemptive scheduler.
 */

#include "process.h"
#include "heap.h"
#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void kpanic(const char *msg);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern size_t strlen(const char *s);
extern char *strcpy(char *dest, const char *src);
extern uint64_t timer_get_ticks(void);

/* Assembly functions */
extern void context_switch(struct cpu_context *old_ctx,
                           struct cpu_context *new_ctx);
extern void context_start(struct cpu_context *ctx);
extern void process_entry_wrapper(void);

/* Interrupt control */
static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }

/*============================================================================
 * Scheduler State
 *============================================================================*/

/* Process table */
static struct process process_table[PROCESS_MAX];

/* Ready queue (doubly linked list) */
static struct process *ready_head = NULL;
static struct process *ready_tail = NULL;

/* Current running process */
static struct process *current_process = NULL;

/* Idle process (runs when nothing else can) */
static struct process *idle_process = NULL;

/* Next PID to assign */
static pid_t next_pid = PID_KERNEL;

/* Scheduler statistics */
static struct scheduler_stats sched_stats;

/* Scheduler initialized flag */
static int sched_initialized = 0;

/* Time slice (ticks per quantum) */
#define TIME_SLICE_TICKS    10  /* 100ms at 100Hz timer */

/*============================================================================
 * Ready Queue Management
 *============================================================================*/

static void ready_queue_add(struct process *proc)
{
    proc->next = NULL;
    proc->prev = ready_tail;

    if (ready_tail) {
        ready_tail->next = proc;
    } else {
        ready_head = proc;
    }
    ready_tail = proc;

    proc->state = PROCESS_STATE_READY;
}

static void ready_queue_remove(struct process *proc)
{
    if (proc->prev) {
        proc->prev->next = proc->next;
    } else {
        ready_head = proc->next;
    }

    if (proc->next) {
        proc->next->prev = proc->prev;
    } else {
        ready_tail = proc->prev;
    }

    proc->next = NULL;
    proc->prev = NULL;
}

static struct process *ready_queue_pop(void)
{
    struct process *proc = ready_head;
    if (proc) {
        ready_queue_remove(proc);
    }
    return proc;
}

/*============================================================================
 * Process Table Management
 *============================================================================*/

static struct process *alloc_process_slot(void)
{
    for (int i = 0; i < PROCESS_MAX; i++) {
        if (process_table[i].state == PROCESS_STATE_FREE) {
            return &process_table[i];
        }
    }
    return NULL;
}

static void free_process_slot(struct process *proc)
{
    /* Free stack if allocated */
    if (proc->stack_base) {
        /* In Phantom spirit, we don't actually free - but for kernel memory we must */
        kfree(proc->stack_base);
        proc->stack_base = NULL;
        proc->stack_top = NULL;
    }

    proc->state = PROCESS_STATE_FREE;
    sched_stats.active_processes--;
}

/*============================================================================
 * Idle Process
 *============================================================================*/

static void idle_task(void *arg)
{
    (void)arg;

    /* Idle loop - just halt and wait for interrupts */
    while (1) {
        sched_stats.idle_ticks++;
        __asm__ volatile("hlt");
    }
}

/*============================================================================
 * Scheduler Core
 *============================================================================*/

static void schedule(void)
{
    struct process *next;

    /* Get next process from ready queue */
    next = ready_queue_pop();

    /* If nothing ready, run idle process */
    if (!next) {
        next = idle_process;
    }

    /* If same process, just continue */
    if (next == current_process) {
        return;
    }

    /* Switch to new process */
    struct process *old = current_process;
    current_process = next;
    next->state = PROCESS_STATE_RUNNING;
    next->time_slice = TIME_SLICE_TICKS;

    sched_stats.total_context_switches++;
    next->context_switches++;

    /* If old process is still runnable, put it back in queue */
    if (old && old->state == PROCESS_STATE_RUNNING) {
        old->state = PROCESS_STATE_READY;
        ready_queue_add(old);
    }

    /* Perform context switch */
    if (old) {
        context_switch(&old->context, &next->context);
    } else {
        /* First run - just start the new context */
        context_start(&next->context);
    }
}

/*============================================================================
 * Scheduler API
 *============================================================================*/

void sched_init(void)
{
    if (sched_initialized) {
        return;
    }

    /* Initialize process table */
    memset(process_table, 0, sizeof(process_table));
    memset(&sched_stats, 0, sizeof(sched_stats));

    /* Create idle process (doesn't use normal creation path) */
    idle_process = &process_table[0];
    idle_process->pid = PID_KERNEL;
    idle_process->state = PROCESS_STATE_READY;
    strcpy(idle_process->name, "idle");
    idle_process->priority = 255;  /* Lowest priority */
    idle_process->created_tick = timer_get_ticks();

    /* Allocate stack for idle process */
    idle_process->stack_base = kmalloc(PROCESS_STACK_SIZE);
    if (!idle_process->stack_base) {
        kpanic("Failed to allocate idle process stack");
    }
    idle_process->stack_top = (void *)((uint64_t)idle_process->stack_base +
                                        PROCESS_STACK_SIZE);

    /* Set up idle process context */
    memset(&idle_process->context, 0, sizeof(idle_process->context));
    idle_process->context.rip = (uint64_t)process_entry_wrapper;
    idle_process->context.rsp = (uint64_t)idle_process->stack_top;
    idle_process->context.rflags = 0x202;  /* IF set (interrupts enabled) */
    idle_process->context.r12 = (uint64_t)idle_task;  /* Entry function */
    idle_process->context.rdi = 0;  /* Argument */

    next_pid = PID_KERNEL + 1;
    sched_stats.total_processes_created = 1;
    sched_stats.active_processes = 1;
    sched_stats.peak_processes = 1;

    sched_initialized = 1;
    kprintf("  Scheduler: initialized\n");
}

void sched_start(void)
{
    if (!sched_initialized) {
        kpanic("Scheduler not initialized");
    }

    /* Set current to idle initially */
    current_process = idle_process;
    idle_process->state = PROCESS_STATE_RUNNING;

    kprintf("  Scheduler: starting (idle PID=%u)\n", idle_process->pid);

    /* Enable interrupts and start running */
    sti();

    /* Run the idle task directly (schedule() will switch when others are ready) */
    idle_task(NULL);
}

void sched_yield(void)
{
    cli();
    schedule();
    sti();
}

void scheduler_tick(void)
{
    sched_stats.total_ticks++;

    if (!current_process) {
        return;
    }

    current_process->total_ticks++;

    /* Check if we should switch processes */
    int should_schedule = 0;

    /* If running idle and there are ready processes, switch */
    if (current_process == idle_process && ready_head != NULL) {
        should_schedule = 1;
    }

    /* Decrement time slice for non-idle processes */
    if (current_process != idle_process) {
        if (current_process->time_slice > 0) {
            current_process->time_slice--;
        }
        /* Time slice expired - preempt */
        if (current_process->time_slice == 0) {
            should_schedule = 1;
        }
    }

    if (should_schedule) {
        schedule();
    }
}

struct process *sched_current(void)
{
    return current_process;
}

void sched_get_stats(struct scheduler_stats *stats)
{
    if (stats) {
        *stats = sched_stats;
    }
}

void sched_dump(void)
{
    kprintf("Scheduler State:\n");
    kprintf("  Active processes: %u (peak: %u)\n",
            sched_stats.active_processes, sched_stats.peak_processes);
    kprintf("  Total created:    %lu\n",
            (unsigned long)sched_stats.total_processes_created);
    kprintf("  Context switches: %lu\n",
            (unsigned long)sched_stats.total_context_switches);
    kprintf("  Total ticks:      %lu (idle: %lu)\n",
            (unsigned long)sched_stats.total_ticks,
            (unsigned long)sched_stats.idle_ticks);

    kprintf("\nProcess Table:\n");
    kprintf("  PID   Name             State      Ticks      Switches\n");

    for (int i = 0; i < PROCESS_MAX; i++) {
        struct process *p = &process_table[i];
        if (p->state == PROCESS_STATE_FREE) continue;

        const char *state_str;
        switch (p->state) {
            case PROCESS_STATE_CREATED: state_str = "CREATED"; break;
            case PROCESS_STATE_READY:   state_str = "READY"; break;
            case PROCESS_STATE_RUNNING: state_str = "RUNNING"; break;
            case PROCESS_STATE_BLOCKED: state_str = "BLOCKED"; break;
            case PROCESS_STATE_ZOMBIE:  state_str = "ZOMBIE"; break;
            default:                    state_str = "UNKNOWN"; break;
        }

        kprintf("  %u     %s  %s  %lu  %lu%s\n",
                p->pid, p->name, state_str,
                (unsigned long)p->total_ticks,
                (unsigned long)p->context_switches,
                p == current_process ? " *" : "");
    }
}

/*============================================================================
 * Process API
 *============================================================================*/

pid_t process_create(const char *name, process_entry_t entry, void *arg)
{
    cli();

    /* Find free slot */
    struct process *proc = alloc_process_slot();
    if (!proc) {
        sti();
        kprintf("process_create: no free slots\n");
        return PID_INVALID;
    }

    /* Initialize process */
    memset(proc, 0, sizeof(*proc));
    proc->pid = next_pid++;
    proc->state = PROCESS_STATE_CREATED;
    proc->priority = 10;  /* Default priority */
    proc->parent_pid = current_process ? current_process->pid : PID_KERNEL;
    proc->created_tick = timer_get_ticks();

    /* Copy name */
    if (name) {
        size_t len = strlen(name);
        if (len >= PROCESS_NAME_MAX) len = PROCESS_NAME_MAX - 1;
        memcpy(proc->name, name, len);
        proc->name[len] = '\0';
    } else {
        strcpy(proc->name, "unnamed");
    }

    /* Allocate stack */
    proc->stack_base = kmalloc(PROCESS_STACK_SIZE);
    if (!proc->stack_base) {
        proc->state = PROCESS_STATE_FREE;
        sti();
        kprintf("process_create: failed to allocate stack\n");
        return PID_INVALID;
    }
    proc->stack_top = (void *)((uint64_t)proc->stack_base + PROCESS_STACK_SIZE);

    /* Set up initial context */
    memset(&proc->context, 0, sizeof(proc->context));
    proc->context.rip = (uint64_t)process_entry_wrapper;
    proc->context.rsp = (uint64_t)proc->stack_top;
    proc->context.rflags = 0x202;  /* IF set (interrupts enabled) */
    proc->context.r12 = (uint64_t)entry;  /* Entry function in R12 */
    proc->context.rdi = (uint64_t)arg;    /* Argument in RDI */

    /* Update statistics */
    sched_stats.total_processes_created++;
    sched_stats.active_processes++;
    if (sched_stats.active_processes > sched_stats.peak_processes) {
        sched_stats.peak_processes = sched_stats.active_processes;
    }

    /* Add to ready queue */
    ready_queue_add(proc);

    sti();
    return proc->pid;
}

void process_exit(int exit_code)
{
    cli();

    if (!current_process || current_process == idle_process) {
        kpanic("Cannot exit idle process");
    }

    current_process->state = PROCESS_STATE_ZOMBIE;
    current_process->exit_code = exit_code;

    kprintf("Process %u (%s) exited with code %d\n",
            current_process->pid, current_process->name, exit_code);

    /* Clean up (in a full OS, we'd wait for parent to collect) */
    free_process_slot(current_process);

    /* Schedule next process */
    current_process = NULL;
    schedule();

    /* Should never reach here */
    kpanic("process_exit returned");
}

struct process *process_get(pid_t pid)
{
    for (int i = 0; i < PROCESS_MAX; i++) {
        if (process_table[i].pid == pid &&
            process_table[i].state != PROCESS_STATE_FREE) {
            return &process_table[i];
        }
    }
    return NULL;
}

pid_t process_getpid(void)
{
    return current_process ? current_process->pid : PID_INVALID;
}

void process_sleep_ms(uint32_t ms)
{
    /* Simple busy-wait sleep for now */
    uint64_t start = timer_get_ticks();
    uint64_t ticks = (ms + 9) / 10;  /* Convert to ticks (100Hz = 10ms per tick) */

    while (timer_get_ticks() - start < ticks) {
        sched_yield();
    }
}

void process_block(void)
{
    cli();

    if (current_process && current_process != idle_process) {
        current_process->state = PROCESS_STATE_BLOCKED;
        schedule();
    }

    sti();
}

void process_unblock(struct process *proc)
{
    cli();

    if (proc && proc->state == PROCESS_STATE_BLOCKED) {
        ready_queue_add(proc);
    }

    sti();
}
