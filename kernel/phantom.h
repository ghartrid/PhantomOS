/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                            PHANTOM KERNEL
 *                     "To Create, Not To Destroy"
 *
 *    A microkernel implementing the Phantom Constitution principles.
 *    Built on Linux, enforcing append-only semantics and governed execution.
 *
 * ══════════════════════════════════════════════════════════════════════════════
 */

#ifndef PHANTOM_H
#define PHANTOM_H

#include <stdint.h>
#include <stddef.h>

/* ══════════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ══════════════════════════════════════════════════════════════════════════════ */

#define PHANTOM_VERSION         0x0001
#define PHANTOM_MAGIC           0x4D4F544E414850ULL  /* "PHANTOM" */

#define PHANTOM_MAX_PROCESSES   1024
#define PHANTOM_MAX_PATH        4096
#define PHANTOM_HASH_SIZE       32
#define PHANTOM_SIGNATURE_SIZE  64

/* ══════════════════════════════════════════════════════════════════════════════
 * TYPES
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef uint64_t phantom_pid_t;
typedef uint64_t phantom_time_t;
typedef uint8_t  phantom_hash_t[PHANTOM_HASH_SIZE];
typedef uint8_t  phantom_signature_t[PHANTOM_SIGNATURE_SIZE];

/* Error codes - note: no "success" for destructive operations */
typedef enum {
    PHANTOM_OK              =  0,
    PHANTOM_ERR_NOMEM       = -1,
    PHANTOM_ERR_NOTFOUND    = -2,
    PHANTOM_ERR_INVALID     = -3,
    PHANTOM_ERR_DENIED      = -4,   /* Governor declined */
    PHANTOM_ERR_UNSIGNED    = -5,   /* Code not signed */
    PHANTOM_ERR_CORRUPT     = -6,
    PHANTOM_ERR_FULL        = -7,
    PHANTOM_ERR_IO          = -8,
    /* Note: No PHANTOM_ERR_DELETED - concept doesn't exist */
} phantom_error_t;

/* Governor decision */
typedef enum {
    GOVERNOR_APPROVE        = 0,
    GOVERNOR_DECLINE        = 1,
    GOVERNOR_QUERY          = 2,    /* Need more information */
} governor_decision_t;

/* Process state */
typedef enum {
    PROCESS_EMBRYO          = 0,    /* Being created */
    PROCESS_READY           = 1,    /* Ready to run */
    PROCESS_RUNNING         = 2,    /* Currently executing */
    PROCESS_BLOCKED         = 3,    /* Waiting for I/O */
    PROCESS_DORMANT         = 4,    /* Suspended (not deleted!) */
} process_state_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * STRUCTURES
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Code signature - all executable code must be Governor-signed
 * Per Article IV: "Hardware checks signature before any code runs"
 */
struct phantom_code_signature {
    phantom_hash_t      code_hash;      /* SHA-256 of code */
    phantom_signature_t governor_sig;   /* Governor's approval signature */
    phantom_time_t      signed_at;      /* When approved */
    uint32_t            flags;          /* Approval flags */
    char                reason[256];    /* Governor's reasoning */
};

/* Forward declaration for scheduling info */
struct phantom_sched_info;
struct phantom_memory_region;
struct phantom_init;       /* Init system */
struct phantom_governor;   /* Governor system */
struct phantom_ai;         /* AI subsystem */
struct phantom_net;        /* Network subsystem */
struct phantom_tls;        /* TLS/SSL subsystem */
struct dnauth_system;      /* DNA-based authentication */
struct qrnet_system;       /* QR code distributed file network */

/*
 * Process Control Block
 * Note: Processes are never truly killed - they become dormant
 * Their history remains in the geology forever
 */
struct phantom_process {
    phantom_pid_t       pid;
    phantom_pid_t       parent_pid;
    process_state_t     state;
    phantom_time_t      created;
    phantom_time_t      state_changed;

    /* Code verification */
    struct phantom_code_signature signature;
    phantom_hash_t      code_hash;
    int                 is_verified;

    /* Identity (permanent, for accountability) */
    phantom_hash_t      creator_id;
    char                name[256];

    /* Execution context */
    void               *context;        /* Architecture-specific */
    uint64_t            instruction_count;
    uint64_t            program_counter;    /* Saved PC for resume */

    /* Memory (append-only model) */
    void               *memory_base;
    size_t              memory_size;
    size_t              memory_high_water;  /* Peak usage, for geology */
    uint32_t            num_regions;
    struct phantom_memory_region *regions;  /* Memory regions */

    /* IPC */
    uint32_t            mailbox_head;       /* First unread message index */
    uint32_t            mailbox_count;      /* Number of pending messages */

    /* Scheduling */
    uint8_t             priority;
    uint64_t            time_slice_ns;
    uint64_t            time_used_ns;
    uint64_t            total_time_ns;
    uint64_t            wait_time_ns;
    uint64_t            wakeups;
    phantom_time_t      last_scheduled;

    /* Links */
    struct phantom_process *next;
};

/*
 * Governor evaluation request
 * Per Article III: "AI judges all code before execution"
 */
struct governor_request {
    phantom_hash_t      code_hash;
    void               *code_ptr;
    size_t              code_size;
    phantom_hash_t      creator_id;
    char                description[1024];

    /* For evaluation */
    int                 accesses_network;
    int                 accesses_storage;
    int                 creates_processes;
    char                requested_permissions[1024];
};

struct governor_response {
    governor_decision_t decision;
    char                reasoning[1024];
    char                alternatives[1024];  /* If declined */
    phantom_signature_t signature;           /* If approved */
};

/* Scheduler types (defined here for kernel struct, detailed later) */
typedef enum {
    PHANTOM_SCHED_ROUND_ROBIN   = 0,
    PHANTOM_SCHED_PRIORITY      = 1,
    PHANTOM_SCHED_FAIR          = 2,
} phantom_sched_type_t;

/*
 * Kernel state
 */
struct phantom_kernel {
    uint64_t            magic;
    uint16_t            version;
    phantom_time_t      boot_time;

    /* Process table (never shrinks - dormant processes remain) */
    struct phantom_process *processes;
    phantom_pid_t       next_pid;
    uint64_t            total_processes_ever;  /* Historical count */
    uint64_t            active_processes;

    /* Scheduler state */
    phantom_sched_type_t sched_type;
    struct phantom_process *current_process;   /* Currently running */
    struct phantom_process *run_queue;         /* Ready to run */
    uint64_t            context_switches;
    uint64_t            total_idle_ns;

    /* Governor */
    int                 governor_enabled;
    phantom_hash_t      governor_public_key;
    struct phantom_governor *governor;   /* Enhanced Governor */

    /* GeoFS volume for all storage */
    void               *geofs_volume;

    /* Init system for service management */
    struct phantom_init *init;

    /* AI subsystem */
    struct phantom_ai *ai;

    /* Network subsystem */
    struct phantom_net *net;

    /* TLS/SSL subsystem */
    struct phantom_tls *tls;

    /* DNAuth - DNA-based authentication */
    struct dnauth_system *dnauth;

    /* QRNet - QR code distributed file network */
    struct qrnet_system *qrnet;

    /* Statistics (append-only - never reset) */
    uint64_t            total_syscalls;
    uint64_t            total_bytes_created;
    uint64_t            total_messages_sent;
    uint64_t            total_code_evaluated;
    uint64_t            total_code_approved;
    uint64_t            total_code_declined;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * KERNEL API
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Initialization */
phantom_error_t phantom_init(struct phantom_kernel *kernel, const char *geofs_path);
void phantom_shutdown(struct phantom_kernel *kernel);

/* Process management (no kill - only dormant) */
phantom_error_t phantom_process_create(struct phantom_kernel *kernel,
                                        const void *code, size_t code_size,
                                        const char *name,
                                        phantom_pid_t *pid_out);
phantom_error_t phantom_process_suspend(struct phantom_kernel *kernel, phantom_pid_t pid);
phantom_error_t phantom_process_resume(struct phantom_kernel *kernel, phantom_pid_t pid);
struct phantom_process *phantom_process_find(struct phantom_kernel *kernel, phantom_pid_t pid);

/* Governor interface */
phantom_error_t governor_evaluate(struct phantom_kernel *kernel,
                                   struct governor_request *request,
                                   struct governor_response *response);
int governor_verify_signature(struct phantom_kernel *kernel,
                               const phantom_hash_t code_hash,
                               const phantom_signature_t signature);

/* Syscall interface (routed through GeoFS) */
phantom_error_t phantom_syscall_write(struct phantom_kernel *kernel,
                                       phantom_pid_t pid,
                                       const char *path,
                                       const void *data, size_t size);
phantom_error_t phantom_syscall_read(struct phantom_kernel *kernel,
                                      phantom_pid_t pid,
                                      const char *path,
                                      void *buf, size_t size, size_t *read_out);
/* Note: No syscall_delete - concept doesn't exist */
phantom_error_t phantom_syscall_hide(struct phantom_kernel *kernel,
                                      phantom_pid_t pid,
                                      const char *path);

/* ══════════════════════════════════════════════════════════════════════════════
 * PROCESS PERSISTENCE
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Save a process to GeoFS.
 * The process state is serialized and stored, allowing it to survive kernel restarts.
 * Per Constitution: processes are never destroyed, only preserved.
 */
phantom_error_t phantom_process_save(struct phantom_kernel *kernel, phantom_pid_t pid);

/*
 * Save all processes to GeoFS.
 */
phantom_error_t phantom_process_save_all(struct phantom_kernel *kernel);

/*
 * Restore all processes from GeoFS on boot.
 */
phantom_error_t phantom_process_restore_all(struct phantom_kernel *kernel);

/* ══════════════════════════════════════════════════════════════════════════════
 * INTER-PROCESS COMMUNICATION (IPC)
 * ══════════════════════════════════════════════════════════════════════════════ */

#define PHANTOM_MSG_MAX_SIZE    4096
#define PHANTOM_MAILBOX_MAX     64

/* Message structure - stored in GeoFS for persistence */
struct phantom_message {
    phantom_pid_t   sender;
    phantom_pid_t   receiver;
    phantom_time_t  sent_at;
    uint32_t        msg_type;
    uint32_t        flags;
    size_t          data_size;
    uint8_t         data[PHANTOM_MSG_MAX_SIZE];
    phantom_hash_t  msg_hash;      /* For verification */
};

/* Message types */
#define PHANTOM_MSG_DATA        0   /* Generic data message */
#define PHANTOM_MSG_SIGNAL      1   /* Signal to process */
#define PHANTOM_MSG_REQUEST     2   /* Request for service */
#define PHANTOM_MSG_RESPONSE    3   /* Response to request */

/*
 * Send a message to another process.
 * Messages are stored in GeoFS and persist across kernel restarts.
 */
phantom_error_t phantom_ipc_send(struct phantom_kernel *kernel,
                                  phantom_pid_t sender,
                                  phantom_pid_t receiver,
                                  uint32_t msg_type,
                                  const void *data, size_t size);

/*
 * Receive a message (blocks if mailbox empty, unless PHANTOM_IPC_NOWAIT).
 */
phantom_error_t phantom_ipc_receive(struct phantom_kernel *kernel,
                                     phantom_pid_t pid,
                                     struct phantom_message *msg_out,
                                     int flags);

/*
 * Check how many messages are pending for a process.
 */
int phantom_ipc_pending(struct phantom_kernel *kernel, phantom_pid_t pid);

/* IPC flags */
#define PHANTOM_IPC_NOWAIT      0x01    /* Don't block */
#define PHANTOM_IPC_PEEK        0x02    /* Don't remove from queue */

/* ══════════════════════════════════════════════════════════════════════════════
 * MEMORY MANAGEMENT
 * ══════════════════════════════════════════════════════════════════════════════ */

#define PHANTOM_PAGE_SIZE       4096
#define PHANTOM_MAX_REGIONS     64

/* Memory region - append-only, copy-on-write */
struct phantom_memory_region {
    uint64_t        base_addr;      /* Virtual base address */
    size_t          size;           /* Region size */
    uint32_t        flags;          /* Protection flags */
    phantom_hash_t  content_hash;   /* Hash of region contents in GeoFS */
    phantom_time_t  created;
    phantom_time_t  last_snapshot;  /* When last saved to GeoFS */
    int             dirty;          /* Modified since last snapshot */
    void           *data;           /* Actual memory pointer for cleanup */
};

/* Memory flags */
#define PHANTOM_MEM_READ        0x01
#define PHANTOM_MEM_WRITE       0x02
#define PHANTOM_MEM_EXEC        0x04
#define PHANTOM_MEM_COW         0x08    /* Copy-on-write */
#define PHANTOM_MEM_SHARED      0x10    /* Shared between processes */

/*
 * Allocate a memory region for a process.
 * Memory is always append-only - modifications create new versions.
 */
phantom_error_t phantom_mem_alloc(struct phantom_kernel *kernel,
                                   phantom_pid_t pid,
                                   size_t size,
                                   uint32_t flags,
                                   uint64_t *addr_out);

/*
 * Snapshot a process's memory to GeoFS.
 * This preserves the current memory state in the geology.
 */
phantom_error_t phantom_mem_snapshot(struct phantom_kernel *kernel,
                                      phantom_pid_t pid);

/*
 * Restore a process's memory from GeoFS.
 */
phantom_error_t phantom_mem_restore(struct phantom_kernel *kernel,
                                     phantom_pid_t pid,
                                     phantom_hash_t snapshot_hash);

/* ══════════════════════════════════════════════════════════════════════════════
 * SCHEDULER
 * ══════════════════════════════════════════════════════════════════════════════ */

#define PHANTOM_PRIORITY_MIN    0
#define PHANTOM_PRIORITY_MAX    31
#define PHANTOM_PRIORITY_DEFAULT 16
#define PHANTOM_TIME_SLICE_NS   10000000    /* 10ms default time slice */

/* Per-process scheduling info */
struct phantom_sched_info {
    uint8_t         priority;           /* 0-31, higher = more CPU */
    uint64_t        time_slice_ns;      /* Time slice in nanoseconds */
    uint64_t        time_used_ns;       /* CPU time used this slice */
    uint64_t        total_time_ns;      /* Total CPU time ever */
    uint64_t        wait_time_ns;       /* Total time waiting */
    uint64_t        wakeups;            /* Number of times scheduled */
    phantom_time_t  last_scheduled;     /* When last ran */
};

/*
 * Initialize the scheduler.
 */
phantom_error_t phantom_sched_init(struct phantom_kernel *kernel,
                                    phantom_sched_type_t type);

/*
 * Run the scheduler - select and run the next process.
 * Returns when a time slice expires or process blocks.
 */
phantom_error_t phantom_sched_run(struct phantom_kernel *kernel);

/*
 * Yield the current process's time slice.
 */
phantom_error_t phantom_sched_yield(struct phantom_kernel *kernel, phantom_pid_t pid);

/*
 * Set a process's priority.
 */
phantom_error_t phantom_sched_set_priority(struct phantom_kernel *kernel,
                                            phantom_pid_t pid,
                                            uint8_t priority);

/*
 * Get scheduling statistics for a process.
 */
phantom_error_t phantom_sched_stats(struct phantom_kernel *kernel,
                                     phantom_pid_t pid,
                                     struct phantom_sched_info *info_out);

/* ══════════════════════════════════════════════════════════════════════════════
 * CONSTITUTION ENFORCEMENT
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * These functions enforce Article IV: Hardware Enforcement
 * In software, we simulate the "absent instructions" by refusing
 * to implement them at all.
 */

/* These functions DO NOT EXIST - they are listed here as documentation */
/* phantom_error_t phantom_delete(...);     -- ABSENT */
/* phantom_error_t phantom_overwrite(...);  -- ABSENT */
/* phantom_error_t phantom_truncate(...);   -- ABSENT */
/* phantom_error_t phantom_format(...);     -- ABSENT */
/* phantom_error_t phantom_kill(...);       -- ABSENT (use suspend) */

/*
 * The Prime Directive
 * This is checked before every operation
 */
static inline int phantom_is_creative(int operation_type) {
    /* Only creative operations are permitted */
    /* Destructive operations return 0 and are refused */
    (void)operation_type;
    return 1;  /* Placeholder - real implementation checks operation */
}

#endif /* PHANTOM_H */
