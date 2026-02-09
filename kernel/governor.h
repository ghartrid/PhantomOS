/*
 * PhantomOS Kernel Governor
 * "To Create, Not To Destroy"
 *
 * The Governor is the policy enforcement layer that ensures the Phantom
 * philosophy is upheld throughout the kernel. It provides:
 *
 * - Policy enforcement: Blocks operations that violate the Prime Directive
 * - Operation transformation: Converts destructive ops to safe alternatives
 * - Audit trail: Immutable log of all significant operations
 * - Capability system: Fine-grained permission control
 *
 * The Prime Directive:
 *   "Nothing is ever truly deleted - only hidden, transformed, or preserved."
 */

#ifndef PHANTOMOS_KERNEL_GOVERNOR_H
#define PHANTOMOS_KERNEL_GOVERNOR_H

#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define GOVERNOR_VERSION        0x0001
#define GOVERNOR_MAGIC          0x564F47504854ULL    /* "PHTGOV" */

/* Maximum sizes */
#define GOVERNOR_MAX_REASON     64
#define GOVERNOR_AUDIT_SIZE     128     /* Circular audit buffer entries */

/* Policy domains */
#define GOVERNOR_DOMAIN_MEMORY      0x0001
#define GOVERNOR_DOMAIN_PROCESS     0x0002
#define GOVERNOR_DOMAIN_FILESYSTEM  0x0004
#define GOVERNOR_DOMAIN_RESOURCE    0x0008
#define GOVERNOR_DOMAIN_ALL         0xFFFF

/*============================================================================
 * Policy Types
 *============================================================================*/

/* Policy verdict - what the Governor decides */
typedef enum {
    GOV_ALLOW,              /* Operation permitted */
    GOV_DENY,               /* Operation forbidden (violates philosophy) */
    GOV_TRANSFORM,          /* Operation transformed (e.g., delete->hide) */
    GOV_AUDIT,              /* Allow but log (suspicious but permitted) */
} gov_verdict_t;

/* Policy categories */
typedef enum {
    /* Memory policies */
    POLICY_MEM_FREE,            /* Freeing memory */
    POLICY_MEM_OVERWRITE,       /* Overwriting existing data */

    /* Process policies */
    POLICY_PROC_KILL,           /* Forcibly terminating process */
    POLICY_PROC_EXIT,           /* Process self-termination (allowed) */

    /* Filesystem policies */
    POLICY_FS_DELETE,           /* Deleting a file (transformed to hide) */
    POLICY_FS_TRUNCATE,         /* Truncating a file */
    POLICY_FS_OVERWRITE,        /* Overwriting file content */
    POLICY_FS_HIDE,             /* Hiding a file (allowed!) */

    /* Permission policies */
    POLICY_FS_PERM_DENIED,      /* File permission check failed */
    POLICY_FS_QUOTA_EXCEEDED,   /* Quota exceeded */

    /* Resource policies */
    POLICY_RES_EXHAUST,         /* Resource exhaustion attempt */

    POLICY_COUNT
} gov_policy_t;

/*============================================================================
 * Capability Types
 *============================================================================*/

/* Capability flags - what an operation/context is allowed to do */
typedef uint32_t gov_caps_t;

#define GOV_CAP_NONE            0x00000000

/* Memory capabilities */
#define GOV_CAP_MEM_FREE        0x00000001   /* Can free memory */
#define GOV_CAP_MEM_KERNEL      0x00000002   /* Kernel memory operations */

/* Process capabilities */
#define GOV_CAP_PROC_SIGNAL     0x00000010   /* Can send signals */
#define GOV_CAP_PROC_ADMIN      0x00000020   /* Process admin */

/* Filesystem capabilities */
#define GOV_CAP_FS_HIDE         0x00000100   /* Can hide files */
#define GOV_CAP_FS_ADMIN        0x00000200   /* FS admin ops */

/* Special capabilities */
#define GOV_CAP_KERNEL          0x80000000   /* Kernel context (all ops) */

/* Predefined capability sets */
#define GOV_CAPS_USER           (GOV_CAP_FS_HIDE)
#define GOV_CAPS_KERNEL         (GOV_CAP_KERNEL | GOV_CAP_MEM_FREE | \
                                 GOV_CAP_MEM_KERNEL | GOV_CAP_PROC_ADMIN | \
                                 GOV_CAP_FS_HIDE | GOV_CAP_FS_ADMIN)

/*============================================================================
 * Audit Types
 *============================================================================*/

/* Audit entry - immutable record of an operation */
struct gov_audit_entry {
    uint64_t        sequence;       /* Monotonic sequence number */
    uint64_t        timestamp;      /* Timer ticks */
    gov_policy_t    policy;         /* Policy that was checked */
    gov_verdict_t   verdict;        /* What was decided */
    uint32_t        pid;            /* Process involved */
    uint32_t        domain;         /* Domain of operation */
    uint64_t        arg1;           /* Operation-specific argument 1 */
    uint64_t        arg2;           /* Operation-specific argument 2 */
    char            reason[GOVERNOR_MAX_REASON];
};

/* Governor statistics */
struct gov_stats {
    uint64_t    total_checks;       /* Total policy checks */
    uint64_t    total_allowed;      /* Operations allowed */
    uint64_t    total_denied;       /* Operations denied */
    uint64_t    total_transformed;  /* Operations transformed */
    uint64_t    violations_memory;  /* Memory violations blocked */
    uint64_t    violations_process; /* Process violations blocked */
    uint64_t    violations_fs;      /* Filesystem violations blocked */
};

/*============================================================================
 * Governor State
 *============================================================================*/

/* Governor configuration flags */
#define GOV_FLAG_STRICT         0x0001  /* Strict mode - no exceptions */
#define GOV_FLAG_AUDIT_ALL      0x0002  /* Audit all operations */
#define GOV_FLAG_VERBOSE        0x0004  /* Verbose logging to console */

/*============================================================================
 * Core Governor API
 *============================================================================*/

#if __STDC_HOSTED__
/*
 * Simulation-mode Governor (object-oriented, parametric)
 * Used when building the PhantomOS simulation on a hosted (Linux) system.
 */

#include "phantom.h"  /* For governor_decision_t, PHANTOM_HASH_SIZE, etc. */

/*============================================================================
 * Simulation Capability Flags
 *============================================================================*/

#define CAP_NONE                0x00000000
#define CAP_BASIC               0x00000001
#define CAP_INFO                0x00000002
#define CAP_NETWORK             0x00000004
#define CAP_NETWORK_SECURE      0x00000008
#define CAP_NETWORK_INSECURE    0x00000010
#define CAP_FILESYSTEM          0x00000020
#define CAP_PROCESS             0x00000040
#define CAP_MEMORY              0x00000080

/*============================================================================
 * Simulation Governor Threat Levels
 *============================================================================*/

#define GOVERNOR_THREAT_NONE     0
#define GOVERNOR_THREAT_LOW      1
#define GOVERNOR_THREAT_MEDIUM   2
#define GOVERNOR_THREAT_HIGH     3
#define GOVERNOR_THREAT_CRITICAL 4

/*============================================================================
 * Simulation Governor History
 *============================================================================*/

#define GOVERNOR_HISTORY_MAX    256

/*============================================================================
 * Simulation Governor Types
 *============================================================================*/

/* Evaluation request - submitted to the Governor for code approval */
typedef struct governor_eval_request {
    const void     *code_ptr;
    size_t          code_size;
    uint8_t         creator_id[32];
    char            description[1024];
    char            name[256];
    uint32_t        declared_caps;      /* Capabilities the code declares it needs */
    uint32_t        detected_caps;      /* Capabilities detected by analysis */
    int             threat_level;       /* Assessed threat level */
} governor_eval_request_t;

/* Evaluation response - the Governor's decision */
typedef struct governor_eval_response {
    governor_decision_t decision;
    char            reasoning[1024];
    char            alternatives[1024];
    uint8_t         signature[64];
    char            summary[256];
    char            decision_by[64];
    char            decline_reason[256];
    uint64_t        approved_at;        /* Timestamp of approval */
} governor_eval_response_t;

/* History entry - record of a past evaluation */
typedef struct governor_history_entry {
    uint8_t         code_hash[32];
    governor_decision_t decision;
    int             can_rollback;
    char            name[256];
    int             threat_level;
    char            decision_by[64];
    char            summary[256];
    uint64_t        timestamp;
} governor_history_entry_t;

/* The simulation Governor object */
typedef struct phantom_governor {
    struct phantom_kernel *kernel;

    /* Mode */
    int             interactive;        /* Interactive approval mode */
    int             strict_mode;        /* Strict policy enforcement */

    /* Statistics (append-only) */
    uint64_t        total_evaluations;
    uint64_t        auto_approved;
    uint64_t        user_approved;
    uint64_t        auto_declined;
    uint64_t        user_declined;

    /* Threat counters */
    uint64_t        threats_critical;
    uint64_t        threats_high;
    uint64_t        threats_medium;
    uint64_t        threats_low;
    uint64_t        threats_none;

    /* Cache */
    int             cache_enabled;
    uint64_t        cache_hits;
    uint64_t        cache_misses;

    /* History */
    governor_history_entry_t history[GOVERNOR_HISTORY_MAX];
    int             history_count;

    /* Scopes */
    int             scope_count;

    /* AI integration */
    void           *ai;
    int             ai_enabled;

    /* Initialization flag */
    int             initialized;
} phantom_governor_t;

/*============================================================================
 * Simulation Governor API
 *============================================================================*/

/*
 * Initialize the simulation Governor with a kernel reference.
 */
void governor_init(phantom_governor_t *gov, struct phantom_kernel *kernel);

/*
 * Shut down the simulation Governor.
 */
void governor_shutdown(phantom_governor_t *gov);

/*
 * Evaluate code for execution approval.
 * Returns 0 on success, -1 on error.
 */
int governor_evaluate_code(phantom_governor_t *gov,
                            governor_eval_request_t *req,
                            governor_eval_response_t *resp);

/*
 * Log a Governor decision to the audit trail.
 */
void governor_log_decision(phantom_governor_t *gov,
                            governor_eval_request_t *req,
                            governor_eval_response_t *resp);

/*
 * Enable or disable the evaluation cache.
 */
void governor_enable_cache(phantom_governor_t *gov, int enable);

/*
 * Clear the evaluation cache.
 */
void governor_clear_cache(phantom_governor_t *gov);

/*
 * Get the number of history entries.
 */
int governor_history_count(phantom_governor_t *gov);

/*
 * Get a history entry by index.
 * Returns 0 on success, -1 on error.
 */
int governor_get_history(phantom_governor_t *gov, int index,
                          governor_history_entry_t *entry);

/*
 * Convert a threat level to a human-readable string.
 */
const char *governor_threat_to_string(int threat_level);

/*
 * Convert capability flags to a comma-separated list string.
 */
void governor_caps_to_list(uint32_t caps, char *buf, size_t buf_size);

/*
 * Set interactive mode (requires user approval for each evaluation).
 */
void governor_set_interactive(phantom_governor_t *gov, int interactive);

/*
 * Set strict mode (rejects all high/critical threat code).
 */
void governor_set_strict(phantom_governor_t *gov, int strict);

/*
 * Verify code by hash and signature.
 * Returns 1 if valid, 0 if invalid.
 */
int governor_verify_code(phantom_governor_t *gov,
                          const uint8_t *code_hash,
                          const uint8_t *signature);

/*
 * Add a capability scope (grant a capability for a specific resource pattern).
 * Returns 0 on success, -1 on error.
 */
int governor_add_scope(phantom_governor_t *gov, uint32_t capability,
                        const char *pattern, size_t max_bytes,
                        int duration_seconds);

/*
 * Set the AI subsystem reference for the Governor.
 */
void governor_set_ai(phantom_governor_t *gov, void *ai);

/*
 * Enable or disable AI-assisted evaluation.
 */
void governor_enable_ai(phantom_governor_t *gov, int enable);

/*============================================================================
 * Behavioral Analysis
 *============================================================================*/

/* Behavior flags for code analysis */
#define BEHAVIOR_NONE              0x0000
#define BEHAVIOR_INFINITE_LOOP     0x0001
#define BEHAVIOR_MEMORY_BOMB       0x0002
#define BEHAVIOR_FORK_BOMB         0x0004
#define BEHAVIOR_OBFUSCATION       0x0008
#define BEHAVIOR_ENCODED_PAYLOAD   0x0010
#define BEHAVIOR_SHELL_INJECTION   0x0020
#define BEHAVIOR_PATH_TRAVERSAL    0x0040
#define BEHAVIOR_RESOURCE_EXHAUST  0x0080
#define BEHAVIOR_LOOP_DESTRUCTION  0x0100

#define GOVERNOR_BEHAVIOR_MAX_DESCRIPTIONS 16
#define GOVERNOR_BEHAVIOR_DESC_LEN         256

/* Result of a behavioral analysis */
typedef struct governor_behavior_result {
    uint32_t    flags;              /* Combination of BEHAVIOR_* flags */
    int         suspicious_score;   /* 0-100 suspiciousness score */
    int         description_count;  /* Number of descriptions filled */
    char        descriptions[GOVERNOR_BEHAVIOR_MAX_DESCRIPTIONS][GOVERNOR_BEHAVIOR_DESC_LEN];
} governor_behavior_result_t;

/*
 * Analyze code for suspicious behavioral patterns.
 * Returns 0 on success, -1 on error.
 */
int governor_analyze_behavior(const char *code, size_t code_size,
                               governor_behavior_result_t *result);

#else /* Freestanding kernel mode */

/*
 * Initialize the Governor system (freestanding singleton)
 * Must be called early in kernel initialization, after heap is ready
 */
void governor_init(void);

#endif /* __STDC_HOSTED__ */

/*
 * Check if the Governor is initialized
 */
int governor_is_initialized(void);

/*
 * Set Governor flags
 */
void governor_set_flags(uint32_t flags);

/*
 * Get Governor flags
 */
uint32_t governor_get_flags(void);

/*============================================================================
 * Policy Check API
 *============================================================================*/

/*
 * Check a memory operation
 * Returns verdict and optionally logs to audit trail
 *
 * @op:     Policy operation (POLICY_MEM_*)
 * @ptr:    Memory address involved
 * @size:   Size of operation
 * @caps:   Capabilities of the requester
 * @reason: Output buffer for denial reason (can be NULL)
 */
gov_verdict_t governor_check_memory(gov_policy_t op,
                                     void *ptr,
                                     size_t size,
                                     gov_caps_t caps,
                                     char *reason);

/*
 * Check a process operation
 *
 * @op:         Policy operation (POLICY_PROC_*)
 * @target_pid: Target process ID (if applicable)
 * @caps:       Capabilities of the requester
 * @reason:     Output buffer for denial reason (can be NULL)
 */
gov_verdict_t governor_check_process(gov_policy_t op,
                                      uint32_t target_pid,
                                      gov_caps_t caps,
                                      char *reason);

/*
 * Check a filesystem operation
 *
 * @op:     Policy operation (POLICY_FS_*)
 * @path:   Path involved (can be NULL)
 * @caps:   Capabilities of the requester
 * @reason: Output buffer for denial reason (can be NULL)
 */
gov_verdict_t governor_check_filesystem(gov_policy_t op,
                                         const char *path,
                                         gov_caps_t caps,
                                         char *reason);

/*============================================================================
 * Convenience Macros
 *============================================================================*/

/*
 * Quick check macros for common operations
 */

/* Check before freeing kernel memory */
#define GOVERNOR_CHECK_FREE(ptr, size) \
    governor_check_memory(POLICY_MEM_FREE, (ptr), (size), GOV_CAP_KERNEL, NULL)

/* Check before killing a process */
#define GOVERNOR_CHECK_KILL(pid) \
    governor_check_process(POLICY_PROC_KILL, (pid), GOV_CAP_KERNEL, NULL)

/* Check before deleting a file (will return TRANSFORM) */
#define GOVERNOR_CHECK_DELETE(path) \
    governor_check_filesystem(POLICY_FS_DELETE, (path), GOV_CAP_KERNEL, NULL)

/*============================================================================
 * Audit API
 *============================================================================*/

/*
 * Get Governor statistics
 */
void governor_get_stats(struct gov_stats *stats);

/*
 * Get the number of audit entries
 */
int governor_audit_count(void);

/*
 * Get an audit entry by index (0 = most recent)
 * Returns 0 on success, -1 on error
 */
int governor_audit_get(int index, struct gov_audit_entry *entry);

/*
 * Manually add an audit entry (for external subsystems)
 */
void governor_audit_record(gov_policy_t policy,
                           gov_verdict_t verdict,
                           uint32_t domain,
                           uint64_t arg1,
                           uint64_t arg2,
                           const char *reason);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/*
 * Get policy name string
 */
const char *governor_policy_name(gov_policy_t policy);

/*
 * Get verdict name string
 */
const char *governor_verdict_name(gov_verdict_t verdict);

/*
 * Get domain name string
 */
const char *governor_domain_name(uint32_t domain);

/*============================================================================
 * Debug Functions
 *============================================================================*/

/*
 * Dump Governor state to console
 */
void governor_dump_stats(void);
void governor_dump_audit(int max_entries);

/*============================================================================
 * Integration Macros for Subsystems
 *============================================================================*/

/*
 * Use in kfree() to enforce Governor policy:
 *
 * void kfree(void *ptr) {
 *     GOVERNOR_ENFORCE_FREE(ptr, size);
 *     // ... actual free logic
 * }
 */
#define GOVERNOR_ENFORCE_FREE(ptr, size) \
    do { \
        if (governor_is_initialized()) { \
            gov_verdict_t _v = GOVERNOR_CHECK_FREE(ptr, size); \
            if (_v == GOV_DENY) { \
                return; \
            } \
        } \
    } while(0)

/*
 * Transform delete operations to hide:
 *
 * if (user_wants_delete(path)) {
 *     GOVERNOR_TRANSFORM_DELETE(path, hide_function);
 * }
 */
#define GOVERNOR_TRANSFORM_DELETE(path, hide_func) \
    do { \
        if (governor_is_initialized()) { \
            gov_verdict_t _v = GOVERNOR_CHECK_DELETE(path); \
            if (_v == GOV_DENY) { \
                return -1; \
            } else if (_v == GOV_TRANSFORM) { \
                return hide_func(path); \
            } \
        } \
    } while(0)

#endif /* PHANTOMOS_KERNEL_GOVERNOR_H */
