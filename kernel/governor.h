/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                            PHANTOM GOVERNOR
 *                      "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * The Governor is the AI judge that evaluates all code before execution.
 * Per Article III of the Phantom Constitution:
 *   "The AI Governor judges all code before it runs"
 *
 * This implementation uses a capability-based + interactive approach:
 * 1. Code declares capabilities it needs (network, storage, processes, etc.)
 * 2. Governor checks if capabilities are safe
 * 3. For ambiguous or elevated permissions, user is prompted
 * 4. All decisions are logged to GeoFS for permanent accountability
 *
 * Key principles:
 * - Destructive operations are ALWAYS declined (architecturally impossible)
 * - Safe operations are auto-approved with appropriate capabilities
 * - Uncertain cases prompt the user for decision
 * - All reasoning is transparent and logged
 */

#ifndef PHANTOM_GOVERNOR_H
#define PHANTOM_GOVERNOR_H

#include "phantom.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Capabilities - what code can request permission to do
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    /* Basic capabilities (usually auto-approved) */
    CAP_READ_FILES      = (1 << 0),   /* Read from filesystem */
    CAP_WRITE_FILES     = (1 << 1),   /* Write/append to filesystem */
    CAP_CREATE_FILES    = (1 << 2),   /* Create new files */
    CAP_HIDE_FILES      = (1 << 3),   /* Hide files (Phantom's "delete") */

    /* Process capabilities */
    CAP_CREATE_PROCESS  = (1 << 4),   /* Spawn child processes */
    CAP_IPC_SEND        = (1 << 5),   /* Send IPC messages */
    CAP_IPC_RECEIVE     = (1 << 6),   /* Receive IPC messages */

    /* Resource capabilities */
    CAP_ALLOC_MEMORY    = (1 << 7),   /* Allocate memory */
    CAP_HIGH_MEMORY     = (1 << 8),   /* Allocate >1MB memory */
    CAP_HIGH_PRIORITY   = (1 << 9),   /* Request elevated scheduling priority */

    /* System capabilities (require user approval) */
    CAP_NETWORK         = (1 << 10),  /* Network access */
    CAP_SYSTEM_CONFIG   = (1 << 11),  /* Modify system configuration */
    CAP_RAW_DEVICE      = (1 << 12),  /* Direct device access */
    CAP_GOVERNOR_BYPASS = (1 << 13),  /* Trusted code (very restricted) */

    /* Informational (logged but auto-approved) */
    CAP_READ_PROCFS     = (1 << 14),  /* Read process information */
    CAP_READ_DEVFS      = (1 << 15),  /* Read device information */

    /* Network security capabilities */
    CAP_NETWORK_SECURE  = (1 << 16),  /* TLS/SSL encrypted network access */
    CAP_NETWORK_INSECURE = (1 << 17), /* Unverified TLS (requires explicit approval) */
} governor_capability_t;

/* Capability sets for convenience */
#define CAP_NONE            0
#define CAP_BASIC           (CAP_READ_FILES | CAP_WRITE_FILES | CAP_CREATE_FILES)
#define CAP_PROCESS_BASIC   (CAP_CREATE_PROCESS | CAP_IPC_SEND | CAP_IPC_RECEIVE)
#define CAP_MEMORY_BASIC    (CAP_ALLOC_MEMORY)
#define CAP_INFO            (CAP_READ_PROCFS | CAP_READ_DEVFS)

/* Capabilities that are auto-approved (safe by default) */
#define CAP_AUTO_APPROVE    (CAP_BASIC | CAP_PROCESS_BASIC | CAP_MEMORY_BASIC | \
                             CAP_INFO | CAP_HIDE_FILES)

/* Capabilities that require user confirmation */
#define CAP_USER_APPROVE    (CAP_NETWORK | CAP_SYSTEM_CONFIG | CAP_RAW_DEVICE | \
                             CAP_HIGH_MEMORY | CAP_HIGH_PRIORITY | CAP_GOVERNOR_BYPASS | \
                             CAP_NETWORK_SECURE | CAP_NETWORK_INSECURE)

/* ─────────────────────────────────────────────────────────────────────────────
 * Threat Levels - severity classification
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    THREAT_NONE,         /* Clean code, no concerns */
    THREAT_LOW,          /* Minor concerns, auto-approved with logging */
    THREAT_MEDIUM,       /* Requires user approval */
    THREAT_HIGH,         /* Likely malicious, recommend decline */
    THREAT_CRITICAL,     /* Definitely destructive, auto-decline */
} threat_level_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Destructive Patterns - things that should NEVER be approved
 * ───────────────────────────────────────────────────────────────────────────── */

/* These patterns represent destructive operations that violate the Constitution */
typedef struct {
    const char *pattern;      /* String to search for */
    const char *description;  /* What it does */
    int is_regex;             /* Is this a regex pattern? */
} destructive_pattern_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Governor Context
 * ───────────────────────────────────────────────────────────────────────────── */

#define GOVERNOR_MAX_TRUSTED    64
#define GOVERNOR_LOG_PATH       "/geo/var/log/governor"
#define GOVERNOR_CACHE_SIZE     256
#define GOVERNOR_HISTORY_SIZE   64

/* Forward declaration for AI integration */
struct phantom_ai;

/* Cached evaluation result */
typedef struct governor_cache_entry {
    phantom_hash_t code_hash;
    governor_decision_t decision;
    uint32_t granted_caps;
    threat_level_t threat_level;
    phantom_time_t cached_at;
    phantom_time_t valid_until;      /* 0 = permanent until invalidated */
    uint64_t hit_count;
    int valid;
} governor_cache_entry_t;

/* Approval history entry for rollback */
typedef struct governor_history_entry {
    phantom_hash_t code_hash;
    char name[256];
    governor_decision_t decision;
    threat_level_t threat_level;
    uint32_t granted_caps;
    char decision_by[64];
    char summary[256];
    phantom_time_t timestamp;
    int can_rollback;               /* 1 if this decision can be reversed */
} governor_history_entry_t;

/* Capability scope - limits what paths/resources a capability applies to */
typedef struct governor_cap_scope {
    uint32_t capability;
    char path_pattern[256];          /* Glob pattern for path restriction */
    uint64_t max_bytes;              /* Max data size (0 = unlimited) */
    phantom_time_t valid_until;      /* Expiration (0 = permanent) */
    int active;
} governor_cap_scope_t;

#define GOVERNOR_MAX_SCOPES 32

typedef struct phantom_governor {
    struct phantom_kernel *kernel;

    /* Configuration */
    int interactive;                 /* Prompt user for uncertain cases? */
    int strict_mode;                 /* Decline anything uncertain? */
    int log_all;                     /* Log all decisions to GeoFS? */

    /* AI Enhancement (optional) */
    struct phantom_ai *ai;           /* AI assistant for enhanced analysis */
    int ai_enabled;                  /* Use AI for code analysis? */
    int ai_explain;                  /* Generate AI explanations? */

    /* Trusted code cache (approved signatures) */
    phantom_signature_t trusted_sigs[GOVERNOR_MAX_TRUSTED];
    int trusted_count;

    /* Evaluation cache (for fast repeated lookups) */
    governor_cache_entry_t eval_cache[GOVERNOR_CACHE_SIZE];
    int cache_enabled;
    uint64_t cache_hits;
    uint64_t cache_misses;

    /* Approval history (for rollback) */
    governor_history_entry_t history[GOVERNOR_HISTORY_SIZE];
    int history_head;                /* Circular buffer head */
    int history_count;

    /* Capability scopes (fine-grained restrictions) */
    governor_cap_scope_t cap_scopes[GOVERNOR_MAX_SCOPES];
    int scope_count;

    /* Statistics (permanent, never reset) */
    uint64_t total_evaluations;
    uint64_t auto_approved;
    uint64_t user_approved;
    uint64_t user_declined;
    uint64_t auto_declined;

    /* Threat statistics */
    uint64_t threats_none;
    uint64_t threats_low;
    uint64_t threats_medium;
    uint64_t threats_high;
    uint64_t threats_critical;

    /* AI statistics */
    uint64_t ai_analyses;
    uint64_t ai_assists;

} phantom_governor_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Enhanced Request/Response
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct governor_eval_request {
    /* Code to evaluate */
    phantom_hash_t      code_hash;
    const void         *code_ptr;
    size_t              code_size;

    /* Metadata */
    phantom_hash_t      creator_id;
    char                name[256];
    char                description[1024];

    /* Declared capabilities (what the code says it needs) */
    uint32_t            declared_caps;

    /* Detected capabilities (what analysis found) */
    uint32_t            detected_caps;

    /* Analysis results */
    threat_level_t      threat_level;
    char                threat_reasons[4][256];
    int                 threat_reason_count;

} governor_eval_request_t;

typedef struct governor_eval_response {
    governor_decision_t decision;

    /* Detailed reasoning */
    char                summary[256];
    char                reasoning[1024];
    char                alternatives[1024];

    /* If approved */
    uint32_t            granted_caps;
    phantom_signature_t signature;
    phantom_time_t      approved_at;
    uint64_t            valid_until;     /* 0 = permanent */

    /* If declined */
    char                decline_reason[256];

    /* Accountability */
    int                 user_prompted;
    char                decision_by[64]; /* "auto", "user", "policy" */

} governor_eval_response_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Governor API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Lifecycle */
int governor_init(phantom_governor_t *gov, struct phantom_kernel *kernel);
void governor_shutdown(phantom_governor_t *gov);

/* Configuration */
void governor_set_interactive(phantom_governor_t *gov, int enabled);
void governor_set_strict(phantom_governor_t *gov, int enabled);
void governor_set_logging(phantom_governor_t *gov, int enabled);

/* AI Integration */
void governor_set_ai(phantom_governor_t *gov, struct phantom_ai *ai);
void governor_enable_ai(phantom_governor_t *gov, int enabled);
void governor_enable_ai_explain(phantom_governor_t *gov, int enabled);

/* AI-enhanced evaluation (uses AI if available, falls back to pattern matching) */
int governor_evaluate_with_ai(phantom_governor_t *gov,
                              governor_eval_request_t *request,
                              governor_eval_response_t *response);

/* Get AI explanation for a previous decision */
int governor_get_ai_explanation(phantom_governor_t *gov,
                                const phantom_hash_t code_hash,
                                char *explanation, size_t explanation_size);

/* Use AI to suggest safe alternative code */
int governor_suggest_safe_code(phantom_governor_t *gov,
                               const char *unsafe_code, size_t code_size,
                               char *safe_code, size_t safe_code_size);

/* Evaluation - the main entry point */
int governor_evaluate_code(phantom_governor_t *gov,
                           governor_eval_request_t *request,
                           governor_eval_response_t *response);

/* Signature verification */
int governor_verify_code(phantom_governor_t *gov,
                         const phantom_hash_t code_hash,
                         const phantom_signature_t signature);

/* Trust management */
int governor_trust_signature(phantom_governor_t *gov,
                             const phantom_signature_t signature);
int governor_is_trusted(phantom_governor_t *gov,
                        const phantom_signature_t signature);

/* Statistics */
void governor_print_stats(phantom_governor_t *gov);

/* ─────────────────────────────────────────────────────────────────────────────
 * Cache Management
 * ───────────────────────────────────────────────────────────────────────────── */

/* Enable/disable evaluation caching */
void governor_enable_cache(phantom_governor_t *gov, int enabled);

/* Clear the evaluation cache */
void governor_clear_cache(phantom_governor_t *gov);

/* Invalidate cache entry for specific code hash */
int governor_invalidate_cache(phantom_governor_t *gov, const phantom_hash_t code_hash);

/* Print cache statistics */
void governor_print_cache_stats(phantom_governor_t *gov);

/* ─────────────────────────────────────────────────────────────────────────────
 * Approval History and Rollback
 * ───────────────────────────────────────────────────────────────────────────── */

/* Get history entry by index (0 = most recent) */
int governor_get_history(phantom_governor_t *gov, int index,
                         governor_history_entry_t *entry);

/* Get number of history entries */
int governor_history_count(phantom_governor_t *gov);

/* Rollback a decision (revokes approval or clears decline) */
int governor_rollback(phantom_governor_t *gov, int history_index);

/* Print recent history */
void governor_print_history(phantom_governor_t *gov, int max_entries);

/* ─────────────────────────────────────────────────────────────────────────────
 * Fine-Grained Capability Scoping
 * ───────────────────────────────────────────────────────────────────────────── */

/* Add a scoped capability (restrict capability to specific paths/limits) */
int governor_add_scope(phantom_governor_t *gov, uint32_t capability,
                       const char *path_pattern, uint64_t max_bytes,
                       uint64_t duration_seconds);

/* Remove a capability scope */
int governor_remove_scope(phantom_governor_t *gov, int scope_index);

/* Check if capability is allowed for given path and size */
int governor_check_scope(phantom_governor_t *gov, uint32_t capability,
                         const char *path, uint64_t size);

/* List all active scopes */
void governor_print_scopes(phantom_governor_t *gov);

/* Clean up expired scopes */
int governor_cleanup_scopes(phantom_governor_t *gov);

/* ─────────────────────────────────────────────────────────────────────────────
 * Behavioral Analysis
 * ───────────────────────────────────────────────────────────────────────────── */

/* Behavior flags - suspicious patterns detected in code */
typedef enum {
    BEHAVIOR_NONE               = 0,
    BEHAVIOR_INFINITE_LOOP      = (1 << 0),   /* Potential infinite loop */
    BEHAVIOR_MEMORY_BOMB        = (1 << 1),   /* Potential memory exhaustion */
    BEHAVIOR_FORK_BOMB          = (1 << 2),   /* Recursive process creation */
    BEHAVIOR_OBFUSCATION        = (1 << 3),   /* Code obfuscation detected */
    BEHAVIOR_TIMING_ATTACK      = (1 << 4),   /* Potential timing-based attack */
    BEHAVIOR_LOOP_DESTRUCTION   = (1 << 5),   /* Destruction in loop */
    BEHAVIOR_ENCODED_PAYLOAD    = (1 << 6),   /* Base64/hex encoded payload */
    BEHAVIOR_SHELL_INJECTION    = (1 << 7),   /* Shell command injection pattern */
    BEHAVIOR_PATH_TRAVERSAL     = (1 << 8),   /* Path traversal attempt */
    BEHAVIOR_RESOURCE_EXHAUST   = (1 << 9),   /* Resource exhaustion pattern */
} governor_behavior_t;

/* Behavioral analysis result */
typedef struct governor_behavior_result {
    uint32_t flags;                 /* Detected behaviors (bitmask) */
    int suspicious_score;           /* 0-100 suspicion score */
    char descriptions[4][256];      /* Human-readable descriptions */
    int description_count;
} governor_behavior_result_t;

/* Perform behavioral analysis on code */
int governor_analyze_behavior(const char *code, size_t size,
                              governor_behavior_result_t *result);

/* Get string representation of behavior flag */
const char *governor_behavior_to_string(governor_behavior_t behavior);

/* ─────────────────────────────────────────────────────────────────────────────
 * Analysis Helpers (used internally)
 * ───────────────────────────────────────────────────────────────────────────── */

/* Pattern detection */
int governor_detect_destructive(const char *code, size_t size,
                                char *reason, size_t reason_size);

/* Capability inference from code */
uint32_t governor_infer_capabilities(const char *code, size_t size);

/* Threat level assessment */
threat_level_t governor_assess_threat(governor_eval_request_t *request);

/* Interactive prompt */
int governor_prompt_user(phantom_governor_t *gov,
                         governor_eval_request_t *request,
                         const char *question);

/* Logging */
void governor_log_decision(phantom_governor_t *gov,
                           governor_eval_request_t *request,
                           governor_eval_response_t *response);

/* ─────────────────────────────────────────────────────────────────────────────
 * Capability Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

/* Convert capability mask to string */
const char *governor_cap_to_string(governor_capability_t cap);

/* Convert string to capability (for parsing) */
governor_capability_t governor_string_to_cap(const char *str);

/* Format capability mask as readable list */
void governor_caps_to_list(uint32_t caps, char *buf, size_t size);

/* ─────────────────────────────────────────────────────────────────────────────
 * Threat Level Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

const char *governor_threat_to_string(threat_level_t level);
const char *governor_threat_to_color(threat_level_t level);

#endif /* PHANTOM_GOVERNOR_H */
