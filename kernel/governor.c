/*
 * PhantomOS Kernel Governor
 * "To Create, Not To Destroy"
 *
 * Implementation of the policy enforcement layer.
 */

#include "governor.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern uint64_t timer_get_ticks(void);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern size_t strlen(const char *s);
extern char *strcpy(char *dest, const char *src);
extern char *strncpy(char *dest, const char *src, size_t n);

/* From process.h - get current PID */
extern uint32_t process_getpid(void);

/*============================================================================
 * Governor State
 *============================================================================*/

/* Initialization flag */
static int gov_initialized = 0;

/* Configuration flags */
static uint32_t gov_flags = 0;

/* Statistics (append-only, never reset) */
static struct gov_stats gov_stats;

/* Audit trail (circular buffer) */
static struct gov_audit_entry audit_buffer[GOVERNOR_AUDIT_SIZE];
static int audit_head = 0;      /* Next write position */
static int audit_count = 0;     /* Total entries (max GOVERNOR_AUDIT_SIZE) */
static uint64_t audit_sequence = 0;  /* Monotonic sequence number */

/*============================================================================
 * Helper Functions
 *============================================================================*/

/* Copy string with length limit */
static void safe_strcpy(char *dest, const char *src, size_t max)
{
    if (!dest || max == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }

    size_t len = strlen(src);
    if (len >= max) len = max - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

/* Record an audit entry */
static void audit_add(gov_policy_t policy,
                      gov_verdict_t verdict,
                      uint32_t domain,
                      uint64_t arg1,
                      uint64_t arg2,
                      const char *reason)
{
    struct gov_audit_entry *entry = &audit_buffer[audit_head];

    entry->sequence = audit_sequence++;
    entry->timestamp = timer_get_ticks();
    entry->policy = policy;
    entry->verdict = verdict;
    entry->pid = process_getpid();
    entry->domain = domain;
    entry->arg1 = arg1;
    entry->arg2 = arg2;

    if (reason) {
        safe_strcpy(entry->reason, reason, GOVERNOR_MAX_REASON);
    } else {
        entry->reason[0] = '\0';
    }

    /* Advance circular buffer */
    audit_head = (audit_head + 1) % GOVERNOR_AUDIT_SIZE;
    if (audit_count < GOVERNOR_AUDIT_SIZE) {
        audit_count++;
    }
}

/*============================================================================
 * Core Governor Implementation
 *============================================================================*/

void governor_init(void)
{
    if (gov_initialized) {
        return;
    }

    /* Initialize state */
    memset(&gov_stats, 0, sizeof(gov_stats));
    memset(audit_buffer, 0, sizeof(audit_buffer));
    audit_head = 0;
    audit_count = 0;
    audit_sequence = 0;

    /* Default flags: verbose logging */
    gov_flags = GOV_FLAG_VERBOSE;

    gov_initialized = 1;

    kprintf("  Governor: Prime Directive enforcement ACTIVE\n");
}

int governor_is_initialized(void)
{
    return gov_initialized;
}

void governor_set_flags(uint32_t flags)
{
    gov_flags = flags;
}

uint32_t governor_get_flags(void)
{
    return gov_flags;
}

/*============================================================================
 * Policy Check: Memory Operations
 *============================================================================*/

gov_verdict_t governor_check_memory(gov_policy_t op,
                                     void *ptr,
                                     size_t size,
                                     gov_caps_t caps,
                                     char *reason)
{
    gov_verdict_t verdict = GOV_ALLOW;
    const char *deny_reason = NULL;

    gov_stats.total_checks++;

    switch (op) {
    case POLICY_MEM_FREE:
        /*
         * Memory free policy:
         * In pure Phantom philosophy, memory should never be freed.
         * However, kernel memory management requires it for practicality.
         *
         * Policy: Allow with GOV_CAP_MEM_FREE or GOV_CAP_KERNEL capability.
         * Log the operation for audit trail.
         */
        if (caps & (GOV_CAP_MEM_FREE | GOV_CAP_KERNEL)) {
            /* Kernel has privilege to free memory */
            verdict = GOV_ALLOW;

            /* Log if audit-all is enabled */
            if (gov_flags & GOV_FLAG_AUDIT_ALL) {
                audit_add(op, verdict, GOVERNOR_DOMAIN_MEMORY,
                          (uint64_t)(uintptr_t)ptr, size,
                          "Kernel memory free (permitted)");
            }
        } else {
            /* Non-privileged context cannot free memory */
            verdict = GOV_DENY;
            deny_reason = "Memory free denied: insufficient capability";
            gov_stats.violations_memory++;

            audit_add(op, verdict, GOVERNOR_DOMAIN_MEMORY,
                      (uint64_t)(uintptr_t)ptr, size, deny_reason);

            if (gov_flags & GOV_FLAG_VERBOSE) {
                kprintf("  [GOVERNOR] DENY: memory free at 0x%lx (%lu bytes)\n",
                        (unsigned long)(uintptr_t)ptr, (unsigned long)size);
            }
        }
        break;

    case POLICY_MEM_OVERWRITE:
        /*
         * Memory overwrite policy:
         * True Phantom would preserve all versions. Kernel allows overwrites
         * for practical reasons, but we log them.
         */
        verdict = GOV_AUDIT;

        if (gov_flags & GOV_FLAG_AUDIT_ALL) {
            audit_add(op, verdict, GOVERNOR_DOMAIN_MEMORY,
                      (uint64_t)(uintptr_t)ptr, size,
                      "Memory overwrite (audited)");
        }
        break;

    default:
        verdict = GOV_ALLOW;
        break;
    }

    /* Update statistics */
    switch (verdict) {
    case GOV_ALLOW:
    case GOV_AUDIT:
        gov_stats.total_allowed++;
        break;
    case GOV_DENY:
        gov_stats.total_denied++;
        break;
    case GOV_TRANSFORM:
        gov_stats.total_transformed++;
        break;
    }

    /* Copy reason if requested */
    if (reason && deny_reason) {
        safe_strcpy(reason, deny_reason, GOVERNOR_MAX_REASON);
    }

    return verdict;
}

/*============================================================================
 * Policy Check: Process Operations
 *============================================================================*/

gov_verdict_t governor_check_process(gov_policy_t op,
                                      uint32_t target_pid,
                                      gov_caps_t caps,
                                      char *reason)
{
    gov_verdict_t verdict = GOV_ALLOW;
    const char *deny_reason = NULL;

    gov_stats.total_checks++;

    switch (op) {
    case POLICY_PROC_KILL:
        /*
         * Process kill policy:
         * The Prime Directive says processes should not be destroyed.
         * They can exit gracefully, be suspended, or enter dormancy.
         *
         * Policy: DENY forcible process termination.
         * Recommend using process_suspend() or process_exit() instead.
         */
        if (gov_flags & GOV_FLAG_STRICT) {
            verdict = GOV_DENY;
            deny_reason = "Process kill denied: use suspension or dormancy";
            gov_stats.violations_process++;

            audit_add(op, verdict, GOVERNOR_DOMAIN_PROCESS,
                      target_pid, 0, deny_reason);

            if (gov_flags & GOV_FLAG_VERBOSE) {
                kprintf("  [GOVERNOR] DENY: kill process %u (use suspend instead)\n",
                        target_pid);
            }
        } else {
            /* Non-strict mode: Allow with kernel capability, but log */
            if (caps & GOV_CAP_KERNEL) {
                verdict = GOV_AUDIT;
                audit_add(op, verdict, GOVERNOR_DOMAIN_PROCESS,
                          target_pid, 0, "Process termination (kernel, audited)");
            } else {
                verdict = GOV_DENY;
                deny_reason = "Process kill denied: insufficient capability";
                gov_stats.violations_process++;

                audit_add(op, verdict, GOVERNOR_DOMAIN_PROCESS,
                          target_pid, 0, deny_reason);
            }
        }
        break;

    case POLICY_PROC_EXIT:
        /*
         * Process exit policy:
         * Self-termination (graceful exit) is allowed.
         * The process is choosing to end, not being destroyed.
         */
        verdict = GOV_ALLOW;

        if (gov_flags & GOV_FLAG_AUDIT_ALL) {
            audit_add(op, verdict, GOVERNOR_DOMAIN_PROCESS,
                      target_pid, 0, "Process graceful exit");
        }
        break;

    default:
        verdict = GOV_ALLOW;
        break;
    }

    /* Update statistics */
    switch (verdict) {
    case GOV_ALLOW:
    case GOV_AUDIT:
        gov_stats.total_allowed++;
        break;
    case GOV_DENY:
        gov_stats.total_denied++;
        break;
    case GOV_TRANSFORM:
        gov_stats.total_transformed++;
        break;
    }

    /* Copy reason if requested */
    if (reason && deny_reason) {
        safe_strcpy(reason, deny_reason, GOVERNOR_MAX_REASON);
    }

    return verdict;
}

/*============================================================================
 * Policy Check: Filesystem Operations
 *============================================================================*/

gov_verdict_t governor_check_filesystem(gov_policy_t op,
                                         const char *path,
                                         gov_caps_t caps,
                                         char *reason)
{
    gov_verdict_t verdict = GOV_ALLOW;
    const char *deny_reason = NULL;

    gov_stats.total_checks++;
    (void)caps;  /* Not used yet, but part of the API */

    switch (op) {
    case POLICY_FS_DELETE:
        /*
         * File delete policy:
         * The Prime Directive: "To Create, Not To Destroy"
         * Files are NEVER deleted. Delete operations are TRANSFORMED to hide.
         *
         * Policy: TRANSFORM delete -> hide
         * The file becomes invisible in current view but remains in geology.
         */
        verdict = GOV_TRANSFORM;
        gov_stats.total_transformed++;

        audit_add(op, verdict, GOVERNOR_DOMAIN_FILESYSTEM,
                  0, 0, "Delete transformed to hide (Prime Directive)");

        if (gov_flags & GOV_FLAG_VERBOSE) {
            kprintf("  [GOVERNOR] TRANSFORM: delete '%s' -> hide (preserved)\n",
                    path ? path : "(null)");
        }
        break;

    case POLICY_FS_TRUNCATE:
        /*
         * File truncate policy:
         * Truncation destroys data. This is denied.
         * Create a new version instead.
         */
        verdict = GOV_DENY;
        deny_reason = "Truncate denied: creates data loss. Create new version.";
        gov_stats.violations_fs++;

        audit_add(op, verdict, GOVERNOR_DOMAIN_FILESYSTEM,
                  0, 0, deny_reason);

        if (gov_flags & GOV_FLAG_VERBOSE) {
            kprintf("  [GOVERNOR] DENY: truncate '%s' (use versioning)\n",
                    path ? path : "(null)");
        }
        break;

    case POLICY_FS_OVERWRITE:
        /*
         * File overwrite policy:
         * In pure Phantom, overwrites create new versions.
         * GeoFS handles this automatically via content-addressing.
         *
         * Policy: ALLOW but audit (GeoFS preserves history automatically)
         */
        verdict = GOV_AUDIT;

        if (gov_flags & GOV_FLAG_AUDIT_ALL) {
            audit_add(op, verdict, GOVERNOR_DOMAIN_FILESYSTEM,
                      0, 0, "File overwrite (GeoFS preserves history)");
        }
        break;

    case POLICY_FS_HIDE:
        /*
         * File hide policy:
         * Hiding is the Phantom-approved alternative to deletion.
         * The file becomes invisible but is preserved in history.
         *
         * Policy: ALLOW (this IS the correct operation)
         */
        verdict = GOV_ALLOW;

        if (gov_flags & GOV_FLAG_AUDIT_ALL) {
            audit_add(op, verdict, GOVERNOR_DOMAIN_FILESYSTEM,
                      0, 0, "File hidden (preserved in history)");
        }
        break;

    case POLICY_FS_PERM_DENIED:
        verdict = GOV_DENY;
        deny_reason = "Permission denied";
        gov_stats.violations_fs++;
        audit_add(op, verdict, GOVERNOR_DOMAIN_FILESYSTEM,
                  0, 0, deny_reason);
        if (gov_flags & GOV_FLAG_VERBOSE) {
            kprintf("  [GOVERNOR] DENY: permission denied for '%s'\n",
                    path ? path : "(null)");
        }
        break;

    case POLICY_FS_QUOTA_EXCEEDED:
        verdict = GOV_DENY;
        deny_reason = "Quota exceeded";
        gov_stats.violations_fs++;
        audit_add(op, verdict, GOVERNOR_DOMAIN_FILESYSTEM,
                  0, 0, deny_reason);
        if (gov_flags & GOV_FLAG_VERBOSE) {
            kprintf("  [GOVERNOR] DENY: quota exceeded for '%s'\n",
                    path ? path : "(null)");
        }
        break;

    default:
        verdict = GOV_ALLOW;
        break;
    }

    /* Update statistics (if not already done above) */
    if (verdict != GOV_TRANSFORM) {  /* TRANSFORM already counted */
        switch (verdict) {
        case GOV_ALLOW:
        case GOV_AUDIT:
            gov_stats.total_allowed++;
            break;
        case GOV_DENY:
            gov_stats.total_denied++;
            break;
        default:
            break;
        }
    }

    /* Copy reason if requested */
    if (reason && deny_reason) {
        safe_strcpy(reason, deny_reason, GOVERNOR_MAX_REASON);
    }

    return verdict;
}

/*============================================================================
 * Audit API
 *============================================================================*/

void governor_get_stats(struct gov_stats *stats)
{
    if (stats) {
        *stats = gov_stats;
    }
}

int governor_audit_count(void)
{
    return audit_count;
}

int governor_audit_get(int index, struct gov_audit_entry *entry)
{
    if (!entry || index < 0 || index >= audit_count) {
        return -1;
    }

    /* Convert index to circular buffer position (0 = most recent) */
    int pos = (audit_head - 1 - index + GOVERNOR_AUDIT_SIZE) % GOVERNOR_AUDIT_SIZE;
    *entry = audit_buffer[pos];
    return 0;
}

void governor_audit_record(gov_policy_t policy,
                           gov_verdict_t verdict,
                           uint32_t domain,
                           uint64_t arg1,
                           uint64_t arg2,
                           const char *reason)
{
    if (!gov_initialized) return;
    audit_add(policy, verdict, domain, arg1, arg2, reason);
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

const char *governor_policy_name(gov_policy_t policy)
{
    switch (policy) {
    case POLICY_MEM_FREE:       return "MEM_FREE";
    case POLICY_MEM_OVERWRITE:  return "MEM_OVERWRITE";
    case POLICY_PROC_KILL:      return "PROC_KILL";
    case POLICY_PROC_EXIT:      return "PROC_EXIT";
    case POLICY_FS_DELETE:      return "FS_DELETE";
    case POLICY_FS_TRUNCATE:    return "FS_TRUNCATE";
    case POLICY_FS_OVERWRITE:   return "FS_OVERWRITE";
    case POLICY_FS_HIDE:        return "FS_HIDE";
    case POLICY_FS_PERM_DENIED: return "FS_PERM_DENIED";
    case POLICY_FS_QUOTA_EXCEEDED: return "FS_QUOTA_EXCEEDED";
    case POLICY_RES_EXHAUST:    return "RES_EXHAUST";
    default:                    return "UNKNOWN";
    }
}

const char *governor_verdict_name(gov_verdict_t verdict)
{
    switch (verdict) {
    case GOV_ALLOW:     return "ALLOW";
    case GOV_DENY:      return "DENY";
    case GOV_TRANSFORM: return "TRANSFORM";
    case GOV_AUDIT:     return "AUDIT";
    default:            return "UNKNOWN";
    }
}

const char *governor_domain_name(uint32_t domain)
{
    switch (domain) {
    case GOVERNOR_DOMAIN_MEMORY:     return "MEMORY";
    case GOVERNOR_DOMAIN_PROCESS:    return "PROCESS";
    case GOVERNOR_DOMAIN_FILESYSTEM: return "FILESYSTEM";
    case GOVERNOR_DOMAIN_RESOURCE:   return "RESOURCE";
    default:                         return "UNKNOWN";
    }
}

/*============================================================================
 * Debug Functions
 *============================================================================*/

void governor_dump_stats(void)
{
    kprintf("\nGovernor Statistics:\n");
    kprintf("  Total policy checks:  %lu\n",
            (unsigned long)gov_stats.total_checks);
    kprintf("  Operations allowed:   %lu\n",
            (unsigned long)gov_stats.total_allowed);
    kprintf("  Operations denied:    %lu\n",
            (unsigned long)gov_stats.total_denied);
    kprintf("  Operations transformed: %lu\n",
            (unsigned long)gov_stats.total_transformed);
    kprintf("  Violations blocked:\n");
    kprintf("    Memory:     %lu\n",
            (unsigned long)gov_stats.violations_memory);
    kprintf("    Process:    %lu\n",
            (unsigned long)gov_stats.violations_process);
    kprintf("    Filesystem: %lu\n",
            (unsigned long)gov_stats.violations_fs);
    kprintf("  Audit entries: %d\n", audit_count);
    kprintf("  Flags: 0x%x", gov_flags);
    if (gov_flags & GOV_FLAG_STRICT)    kprintf(" STRICT");
    if (gov_flags & GOV_FLAG_AUDIT_ALL) kprintf(" AUDIT_ALL");
    if (gov_flags & GOV_FLAG_VERBOSE)   kprintf(" VERBOSE");
    kprintf("\n");
}

void governor_dump_audit(int max_entries)
{
    int count = (max_entries > 0 && max_entries < audit_count) ?
                max_entries : audit_count;

    kprintf("\nGovernor Audit Trail (most recent %d entries):\n", count);
    kprintf("  Seq      Tick       PID    Policy          Verdict     Reason\n");
    kprintf("  ------   --------   ----   -------------   ---------   ------\n");

    for (int i = 0; i < count; i++) {
        struct gov_audit_entry entry;
        if (governor_audit_get(i, &entry) == 0) {
            kprintf("  %lu  %lu  %u  %s  %s  %s\n",
                    (unsigned long)entry.sequence,
                    (unsigned long)entry.timestamp,
                    entry.pid,
                    governor_policy_name(entry.policy),
                    governor_verdict_name(entry.verdict),
                    entry.reason[0] ? entry.reason : "-");
        }
    }
}
