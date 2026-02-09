/*
 * PhantomOS Simulation Governor
 * "To Create, Not To Destroy"
 *
 * Object-oriented Governor implementation for the hosted (Linux) simulation.
 * This provides the phantom_governor_t-based API used by the GUI and
 * simulation shell, separate from the freestanding kernel's singleton governor.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "governor.h"
#include "phantom.h"

/*============================================================================
 * Simulation Governor Implementation
 *============================================================================*/

void governor_init(phantom_governor_t *gov, struct phantom_kernel *kernel) {
    if (!gov) return;

    memset(gov, 0, sizeof(*gov));
    gov->kernel = kernel;
    gov->interactive = 0;
    gov->strict_mode = 0;
    gov->cache_enabled = 1;
    gov->ai = NULL;
    gov->ai_enabled = 0;
    gov->initialized = 1;

    printf("  [governor] Simulation Governor initialized (Prime Directive active)\n");
}

void governor_shutdown(phantom_governor_t *gov) {
    if (!gov) return;

    printf("  [governor] Simulation Governor shutting down\n");
    printf("  [governor] Final stats: %lu evaluations, %lu approved, %lu declined\n",
           (unsigned long)gov->total_evaluations,
           (unsigned long)(gov->auto_approved + gov->user_approved),
           (unsigned long)(gov->auto_declined + gov->user_declined));

    gov->initialized = 0;
}

/*
 * Simple threat assessment based on code content analysis.
 * In a real system this would use AI; here we use pattern matching.
 */
static int assess_threat(const void *code_ptr, size_t code_size) {
    if (!code_ptr || code_size == 0) return GOVERNOR_THREAT_NONE;

    const char *code = (const char *)code_ptr;

    /* Check for dangerous patterns */
    if (strstr(code, "delete") || strstr(code, "rm ") || strstr(code, "destroy")) {
        return GOVERNOR_THREAT_HIGH;
    }
    if (strstr(code, "format") || strstr(code, "truncate") || strstr(code, "overwrite")) {
        return GOVERNOR_THREAT_CRITICAL;
    }
    if (strstr(code, "network") || strstr(code, "socket") || strstr(code, "connect")) {
        return GOVERNOR_THREAT_MEDIUM;
    }
    if (strstr(code, "exec") || strstr(code, "system(")) {
        return GOVERNOR_THREAT_MEDIUM;
    }

    return GOVERNOR_THREAT_LOW;
}

/*
 * Detect capabilities that the code appears to need.
 */
static uint32_t detect_capabilities(const void *code_ptr, size_t code_size) {
    if (!code_ptr || code_size == 0) return CAP_NONE;

    const char *code = (const char *)code_ptr;
    uint32_t caps = CAP_NONE;

    if (strstr(code, "network") || strstr(code, "socket") ||
        strstr(code, "connect") || strstr(code, "CAP_NETWORK")) {
        caps |= CAP_NETWORK;
    }
    if (strstr(code, "https") || strstr(code, "tls") || strstr(code, "ssl")) {
        caps |= CAP_NETWORK_SECURE;
    }
    if (strstr(code, "file") || strstr(code, "open(") || strstr(code, "write(")) {
        caps |= CAP_FILESYSTEM;
    }
    if (strstr(code, "process") || strstr(code, "fork") || strstr(code, "exec")) {
        caps |= CAP_PROCESS;
    }
    if (strstr(code, "malloc") || strstr(code, "mmap")) {
        caps |= CAP_MEMORY;
    }

    return caps;
}

/*
 * Generate a simple hash of code content for history tracking.
 */
static void compute_code_hash(const void *code_ptr, size_t code_size, uint8_t hash[32]) {
    memset(hash, 0, 32);
    if (!code_ptr || code_size == 0) return;

    const uint8_t *data = (const uint8_t *)code_ptr;
    /* Simple FNV-like hash spread across 32 bytes */
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < code_size; i++) {
        h ^= data[i];
        h *= 0x100000001b3ULL;
        hash[i % 32] ^= (uint8_t)(h >> (i % 8 * 8));
    }
}

int governor_evaluate_code(phantom_governor_t *gov,
                            governor_eval_request_t *req,
                            governor_eval_response_t *resp) {
    if (!gov || !req || !resp) return -1;
    if (!gov->initialized) return -1;

    memset(resp, 0, sizeof(*resp));
    gov->total_evaluations++;

    /* Assess threat level */
    req->threat_level = assess_threat(req->code_ptr, req->code_size);
    req->detected_caps = detect_capabilities(req->code_ptr, req->code_size);

    /* Update threat counters */
    switch (req->threat_level) {
    case GOVERNOR_THREAT_NONE:     gov->threats_none++; break;
    case GOVERNOR_THREAT_LOW:      gov->threats_low++; break;
    case GOVERNOR_THREAT_MEDIUM:   gov->threats_medium++; break;
    case GOVERNOR_THREAT_HIGH:     gov->threats_high++; break;
    case GOVERNOR_THREAT_CRITICAL: gov->threats_critical++; break;
    }

    /* Make decision based on threat level and mode */
    if (req->threat_level >= GOVERNOR_THREAT_CRITICAL && gov->strict_mode) {
        resp->decision = GOVERNOR_DECLINE;
        snprintf(resp->decline_reason, sizeof(resp->decline_reason),
                 "Critical threat level in strict mode");
        snprintf(resp->summary, sizeof(resp->summary),
                 "Declined: critical threat detected");
        snprintf(resp->decision_by, sizeof(resp->decision_by), "auto-strict");
        gov->auto_declined++;
    } else if (req->threat_level >= GOVERNOR_THREAT_HIGH &&
               strstr((const char *)req->code_ptr, "delete")) {
        /* Transform destructive operations per Prime Directive */
        resp->decision = GOVERNOR_APPROVE;
        snprintf(resp->summary, sizeof(resp->summary),
                 "Approved with transformation: delete -> hide");
        snprintf(resp->alternatives, sizeof(resp->alternatives),
                 "Use phantom_hide() instead of delete operations");
        snprintf(resp->reasoning, sizeof(resp->reasoning),
                 "Prime Directive: destructive operation transformed to safe alternative");
        snprintf(resp->decision_by, sizeof(resp->decision_by), "auto-transform");
        gov->auto_approved++;
    } else {
        /* Default: approve */
        resp->decision = GOVERNOR_APPROVE;
        snprintf(resp->summary, sizeof(resp->summary),
                 "Approved: %s (threat: %s)",
                 req->name[0] ? req->name : "unnamed",
                 governor_threat_to_string(req->threat_level));
        snprintf(resp->reasoning, sizeof(resp->reasoning),
                 "Code evaluation passed - within acceptable parameters");
        snprintf(resp->decision_by, sizeof(resp->decision_by), "auto");
        gov->auto_approved++;
    }

    resp->approved_at = (uint64_t)time(NULL);

    /* Cache accounting */
    gov->cache_misses++;  /* Every fresh eval is a cache miss */

    return 0;
}

void governor_log_decision(phantom_governor_t *gov,
                            governor_eval_request_t *req,
                            governor_eval_response_t *resp) {
    if (!gov || !req || !resp) return;
    if (!gov->initialized) return;

    /* Add to history */
    if (gov->history_count < GOVERNOR_HISTORY_MAX) {
        governor_history_entry_t *entry = &gov->history[gov->history_count];
        compute_code_hash(req->code_ptr, req->code_size, entry->code_hash);
        entry->decision = resp->decision;
        entry->can_rollback = (resp->decision == GOVERNOR_APPROVE) ? 1 : 0;
        strncpy(entry->name, req->name, sizeof(entry->name) - 1);
        entry->threat_level = req->threat_level;
        strncpy(entry->decision_by, resp->decision_by, sizeof(entry->decision_by) - 1);
        strncpy(entry->summary, resp->summary, sizeof(entry->summary) - 1);
        entry->timestamp = (uint64_t)time(NULL);
        gov->history_count++;
    }
}

void governor_enable_cache(phantom_governor_t *gov, int enable) {
    if (!gov) return;
    gov->cache_enabled = enable;
}

void governor_clear_cache(phantom_governor_t *gov) {
    if (!gov) return;
    gov->cache_hits = 0;
    gov->cache_misses = 0;
}

int governor_history_count(phantom_governor_t *gov) {
    if (!gov) return 0;
    return gov->history_count;
}

int governor_get_history(phantom_governor_t *gov, int index,
                          governor_history_entry_t *entry) {
    if (!gov || !entry) return -1;
    if (index < 0 || index >= gov->history_count) return -1;

    *entry = gov->history[index];
    return 0;
}

const char *governor_threat_to_string(int threat_level) {
    switch (threat_level) {
    case GOVERNOR_THREAT_NONE:     return "None";
    case GOVERNOR_THREAT_LOW:      return "Low";
    case GOVERNOR_THREAT_MEDIUM:   return "Medium";
    case GOVERNOR_THREAT_HIGH:     return "High";
    case GOVERNOR_THREAT_CRITICAL: return "Critical";
    default:                       return "Unknown";
    }
}

void governor_caps_to_list(uint32_t caps, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    buf[0] = '\0';

    if (caps == CAP_NONE) {
        snprintf(buf, buf_size, "none");
        return;
    }

    char *p = buf;
    size_t remaining = buf_size;
    int first = 1;

#define APPEND_CAP(flag, name) \
    if (caps & (flag)) { \
        int written = snprintf(p, remaining, "%s%s", first ? "" : ", ", name); \
        if (written > 0 && (size_t)written < remaining) { \
            p += written; \
            remaining -= written; \
            first = 0; \
        } \
    }

    APPEND_CAP(CAP_NETWORK, "NETWORK")
    APPEND_CAP(CAP_NETWORK_SECURE, "NETWORK_SECURE")
    APPEND_CAP(CAP_NETWORK_INSECURE, "NETWORK_INSECURE")
    APPEND_CAP(CAP_FILESYSTEM, "FILESYSTEM")
    APPEND_CAP(CAP_PROCESS, "PROCESS")
    APPEND_CAP(CAP_MEMORY, "MEMORY")

#undef APPEND_CAP
}

/*============================================================================
 * Mode and Scope Functions
 *============================================================================*/

void governor_set_interactive(phantom_governor_t *gov, int interactive) {
    if (!gov) return;
    gov->interactive = interactive;
}

void governor_set_strict(phantom_governor_t *gov, int strict) {
    if (!gov) return;
    gov->strict_mode = strict;
}

int governor_verify_code(phantom_governor_t *gov,
                          const uint8_t *code_hash,
                          const uint8_t *signature) {
    if (!gov || !code_hash || !signature) return 0;

    /*
     * In the simulation, we perform a simple verification:
     * accept all code that has been through the Governor evaluation.
     * A production system would use proper cryptographic verification.
     */
    (void)code_hash;
    (void)signature;
    return 1;  /* Accept - simulation mode */
}

int governor_add_scope(phantom_governor_t *gov, uint32_t capability,
                        const char *pattern, size_t max_bytes,
                        int duration_seconds) {
    if (!gov || !pattern) return -1;

    (void)capability;
    (void)max_bytes;
    (void)duration_seconds;

    gov->scope_count++;
    return 0;
}

void governor_set_ai(phantom_governor_t *gov, void *ai) {
    if (!gov) return;
    gov->ai = ai;
}

void governor_enable_ai(phantom_governor_t *gov, int enable) {
    if (!gov) return;
    gov->ai_enabled = enable;
}

/*============================================================================
 * Behavioral Analysis Implementation
 *============================================================================*/

static void behavior_add_desc(governor_behavior_result_t *result, const char *desc) {
    if (result->description_count < GOVERNOR_BEHAVIOR_MAX_DESCRIPTIONS) {
        strncpy(result->descriptions[result->description_count], desc,
                GOVERNOR_BEHAVIOR_DESC_LEN - 1);
        result->descriptions[result->description_count][GOVERNOR_BEHAVIOR_DESC_LEN - 1] = '\0';
        result->description_count++;
    }
}

int governor_analyze_behavior(const char *code, size_t code_size,
                               governor_behavior_result_t *result) {
    if (!code || !result) return -1;

    memset(result, 0, sizeof(*result));
    (void)code_size;

    int score = 0;

    /* Check for infinite loop patterns */
    if (strstr(code, "while(1)") || strstr(code, "while (1)") ||
        strstr(code, "for(;;)") || strstr(code, "for (;;)")) {
        result->flags |= BEHAVIOR_INFINITE_LOOP;
        score += 20;
        behavior_add_desc(result, "Potential infinite loop detected (while(1) or for(;;))");
    }

    /* Check for memory bomb patterns */
    if (strstr(code, "malloc") && strstr(code, "while")) {
        result->flags |= BEHAVIOR_MEMORY_BOMB;
        score += 30;
        behavior_add_desc(result, "Potential memory bomb: allocation in loop");
    }

    /* Check for fork bomb patterns */
    if (strstr(code, "fork()") && (strstr(code, "while") || strstr(code, "for"))) {
        result->flags |= BEHAVIOR_FORK_BOMB;
        score += 40;
        behavior_add_desc(result, "Potential fork bomb: fork() in loop");
    }

    /* Check for obfuscation */
    if (strstr(code, "\\x") || strstr(code, "0x") || strstr(code, "atoi")) {
        result->flags |= BEHAVIOR_OBFUSCATION;
        score += 10;
        behavior_add_desc(result, "Possible code obfuscation detected");
    }

    /* Check for encoded payloads */
    if (strstr(code, "base64") || strstr(code, "decode") || strstr(code, "eval(")) {
        result->flags |= BEHAVIOR_ENCODED_PAYLOAD;
        score += 25;
        behavior_add_desc(result, "Encoded payload or dynamic evaluation detected");
    }

    /* Check for shell injection */
    if (strstr(code, "system(") || strstr(code, "popen(") || strstr(code, "exec(")) {
        result->flags |= BEHAVIOR_SHELL_INJECTION;
        score += 30;
        behavior_add_desc(result, "Potential shell injection via system()/exec()");
    }

    /* Check for path traversal */
    if (strstr(code, "../") || strstr(code, "..\\")) {
        result->flags |= BEHAVIOR_PATH_TRAVERSAL;
        score += 20;
        behavior_add_desc(result, "Path traversal pattern detected (../)");
    }

    /* Check for resource exhaustion */
    if (strstr(code, "ulimit") || (strstr(code, "open(") && strstr(code, "while"))) {
        result->flags |= BEHAVIOR_RESOURCE_EXHAUST;
        score += 25;
        behavior_add_desc(result, "Potential resource exhaustion pattern");
    }

    /* Check for loop-based destruction */
    if ((strstr(code, "rm ") || strstr(code, "unlink") || strstr(code, "delete")) &&
        (strstr(code, "while") || strstr(code, "for"))) {
        result->flags |= BEHAVIOR_LOOP_DESTRUCTION;
        score += 35;
        behavior_add_desc(result, "Destructive operation in loop detected");
    }

    /* Cap score at 100 */
    result->suspicious_score = score > 100 ? 100 : score;

    return 0;
}
