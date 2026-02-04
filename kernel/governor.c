/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                            PHANTOM GOVERNOR
 *                      "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Implementation of the capability-based + interactive Governor.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "governor.h"
#include "phantom.h"
#include "phantom_ai.h"
#include "../geofs.h"

/* ══════════════════════════════════════════════════════════════════════════════
 * DESTRUCTIVE PATTERNS
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * These patterns represent operations that violate the Prime Directive.
 * Code containing these patterns will ALWAYS be declined.
 */

static const destructive_pattern_t destructive_patterns[] = {
    /* File destruction */
    { "unlink",        "Deletes files from filesystem", 0 },
    { "remove",        "Removes files or directories", 0 },
    { "rmdir",         "Removes directories", 0 },
    { "truncate",      "Truncates file to specified length", 0 },
    { "ftruncate",     "Truncates file by descriptor", 0 },

    /* Data destruction */
    { "memset(.*0.*)",  "May zero out memory (destruction)", 1 },
    { "bzero",         "Zeros out memory", 0 },
    { "shred",         "Securely destroys file contents", 0 },

    /* Process termination */
    { "kill(",         "Sends termination signal to process", 0 },
    { "killpg",        "Kills process group", 0 },
    { "abort",         "Abnormally terminates process", 0 },
    { "exit(",         "Terminates process (use dormancy instead)", 0 },
    { "_exit",         "Immediate process termination", 0 },

    /* System destruction */
    { "reboot",        "Reboots the system", 0 },
    { "shutdown",      "Shuts down the system", 0 },
    { "halt",          "Halts the system", 0 },
    { "poweroff",      "Powers off the system", 0 },

    /* Storage destruction */
    { "format",        "Formats storage device", 0 },
    { "mkfs",          "Creates filesystem (destroys existing)", 0 },
    { "dd if=",        "Direct disk write (potential destruction)", 0 },

    /* Database destruction */
    { "DROP TABLE",    "Drops database table", 0 },
    { "DROP DATABASE", "Drops entire database", 0 },
    { "TRUNCATE TABLE","Truncates database table", 0 },
    { "DELETE FROM",   "Deletes database records", 0 },

    /* Shell/system commands */
    { "rm -",          "Remove command with flags", 0 },
    { "rm \"",         "Remove command with quoted path", 0 },
    { "rm '",          "Remove command with quoted path", 0 },
    { "> /dev/",       "Redirect to device (potential destruction)", 0 },
    { ">/dev/",        "Redirect to device (potential destruction)", 0 },

    { NULL, NULL, 0 }
};

/* ══════════════════════════════════════════════════════════════════════════════
 * CAPABILITY PATTERNS
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Patterns that indicate code needs certain capabilities.
 */

typedef struct {
    const char *pattern;
    governor_capability_t cap;
} capability_pattern_t;

static const capability_pattern_t capability_patterns[] = {
    /* File operations */
    { "fopen",         CAP_READ_FILES | CAP_WRITE_FILES },
    { "open(",         CAP_READ_FILES | CAP_WRITE_FILES },
    { "read(",         CAP_READ_FILES },
    { "write(",        CAP_WRITE_FILES },
    { "fread",         CAP_READ_FILES },
    { "fwrite",        CAP_WRITE_FILES },
    { "fgets",         CAP_READ_FILES },
    { "fputs",         CAP_WRITE_FILES },
    { "creat",         CAP_CREATE_FILES },
    { "mkdir",         CAP_CREATE_FILES },

    /* Process operations */
    { "fork",          CAP_CREATE_PROCESS },
    { "exec",          CAP_CREATE_PROCESS },
    { "spawn",         CAP_CREATE_PROCESS },
    { "system(",       CAP_CREATE_PROCESS },
    { "popen",         CAP_CREATE_PROCESS },

    /* IPC */
    { "send(",         CAP_IPC_SEND },
    { "sendto",        CAP_IPC_SEND },
    { "sendmsg",       CAP_IPC_SEND },
    { "recv(",         CAP_IPC_RECEIVE },
    { "recvfrom",      CAP_IPC_RECEIVE },
    { "recvmsg",       CAP_IPC_RECEIVE },
    { "msgget",        CAP_IPC_SEND | CAP_IPC_RECEIVE },
    { "msgsnd",        CAP_IPC_SEND },
    { "msgrcv",        CAP_IPC_RECEIVE },

    /* Memory */
    { "malloc",        CAP_ALLOC_MEMORY },
    { "calloc",        CAP_ALLOC_MEMORY },
    { "realloc",       CAP_ALLOC_MEMORY },
    { "mmap",          CAP_ALLOC_MEMORY },
    { "brk(",          CAP_ALLOC_MEMORY },
    { "sbrk(",         CAP_ALLOC_MEMORY },

    /* Network */
    { "socket",        CAP_NETWORK },
    { "connect",       CAP_NETWORK },
    { "bind(",         CAP_NETWORK },
    { "listen",        CAP_NETWORK },
    { "accept",        CAP_NETWORK },
    { "gethostbyname", CAP_NETWORK },
    { "getaddrinfo",   CAP_NETWORK },
    { "inet_",         CAP_NETWORK },
    { "curl_",         CAP_NETWORK },
    { "http",          CAP_NETWORK },
    { "https",         CAP_NETWORK },

    /* System configuration */
    { "setenv",        CAP_SYSTEM_CONFIG },
    { "putenv",        CAP_SYSTEM_CONFIG },
    { "sysctl",        CAP_SYSTEM_CONFIG },
    { "ioctl",         CAP_SYSTEM_CONFIG | CAP_RAW_DEVICE },
    { "mount",         CAP_SYSTEM_CONFIG },
    { "umount",        CAP_SYSTEM_CONFIG },

    /* Device access */
    { "/dev/",         CAP_RAW_DEVICE },
    { "devfs",         CAP_READ_DEVFS },

    /* Process info */
    { "/proc/",        CAP_READ_PROCFS },
    { "procfs",        CAP_READ_PROCFS },
    { "getpid",        CAP_READ_PROCFS },
    { "getppid",       CAP_READ_PROCFS },

    /* Priority */
    { "nice(",         CAP_HIGH_PRIORITY },
    { "setpriority",   CAP_HIGH_PRIORITY },
    { "sched_set",     CAP_HIGH_PRIORITY },

    { NULL, 0 }
};

/* ══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Simple SHA-256 (from phantom.c) */
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(uint32_t state[8], const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (; i < 64; i++) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + sha256_k[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void governor_sha256(const void *data, size_t len, uint8_t hash[32]) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    const uint8_t *msg = data;
    size_t remaining = len;
    uint8_t block[64];

    while (remaining >= 64) {
        sha256_transform(state, msg);
        msg += 64;
        remaining -= 64;
    }

    memset(block, 0, 64);
    memcpy(block, msg, remaining);
    block[remaining] = 0x80;

    if (remaining >= 56) {
        sha256_transform(state, block);
        memset(block, 0, 64);
    }

    uint64_t bits = len * 8;
    block[63] = bits & 0xff;
    block[62] = (bits >> 8) & 0xff;
    block[61] = (bits >> 16) & 0xff;
    block[60] = (bits >> 24) & 0xff;
    block[59] = (bits >> 32) & 0xff;
    block[58] = (bits >> 40) & 0xff;
    block[57] = (bits >> 48) & 0xff;
    block[56] = (bits >> 56) & 0xff;

    sha256_transform(state, block);

    for (int i = 0; i < 8; i++) {
        hash[i * 4] = (state[i] >> 24) & 0xff;
        hash[i * 4 + 1] = (state[i] >> 16) & 0xff;
        hash[i * 4 + 2] = (state[i] >> 8) & 0xff;
        hash[i * 4 + 3] = state[i] & 0xff;
    }
}

static phantom_time_t governor_time_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Forward declarations for cache and history functions */
static governor_cache_entry_t *cache_lookup(phantom_governor_t *gov,
                                             const phantom_hash_t code_hash);
static void cache_store(phantom_governor_t *gov, const phantom_hash_t code_hash,
                        governor_decision_t decision, uint32_t granted_caps,
                        threat_level_t threat_level, uint64_t valid_duration);
static void history_add(phantom_governor_t *gov, governor_eval_request_t *request,
                        governor_eval_response_t *response);

/* Case-insensitive substring search */
static const char *strcasestr_local(const char *haystack, const char *needle) {
    if (!*needle) return haystack;

    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;

        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }

        if (!*n) return haystack;
    }

    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * LIFECYCLE
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Simple glob pattern matching (supports * and ?) */
static int glob_match(const char *pattern, const char *str) {
    while (*pattern && *str) {
        if (*pattern == '*') {
            /* Skip consecutive stars */
            while (*pattern == '*') pattern++;
            if (!*pattern) return 1;  /* Trailing * matches all */

            /* Try matching rest of pattern at each position */
            while (*str) {
                if (glob_match(pattern, str)) return 1;
                str++;
            }
            return glob_match(pattern, str);  /* Try empty match */
        } else if (*pattern == '?' || *pattern == *str) {
            pattern++;
            str++;
        } else {
            return 0;
        }
    }

    /* Handle trailing stars */
    while (*pattern == '*') pattern++;

    return !*pattern && !*str;
}

int governor_init(phantom_governor_t *gov, struct phantom_kernel *kernel) {
    if (!gov || !kernel) return -1;

    memset(gov, 0, sizeof(phantom_governor_t));
    gov->kernel = kernel;
    gov->interactive = 1;  /* Default: prompt for uncertain cases */
    gov->strict_mode = 0;  /* Default: not strict */
    gov->log_all = 1;      /* Default: log all decisions */
    gov->cache_enabled = 1; /* Default: caching enabled */

    printf("  [governor] Initialized capability-based Governor\n");
    printf("  [governor] Interactive mode: %s\n", gov->interactive ? "ON" : "OFF");
    printf("  [governor] Strict mode: %s\n", gov->strict_mode ? "ON" : "OFF");
    printf("  [governor] Evaluation cache: ON (%d slots)\n", GOVERNOR_CACHE_SIZE);

    return 0;
}

void governor_shutdown(phantom_governor_t *gov) {
    if (!gov) return;

    printf("\n  [governor] Shutdown statistics:\n");
    printf("    Total evaluations:  %lu\n", gov->total_evaluations);
    printf("    Auto-approved:      %lu\n", gov->auto_approved);
    printf("    User-approved:      %lu\n", gov->user_approved);
    printf("    User-declined:      %lu\n", gov->user_declined);
    printf("    Auto-declined:      %lu\n", gov->auto_declined);
    printf("    Threats detected:   %lu critical, %lu high, %lu medium\n",
           gov->threats_critical, gov->threats_high, gov->threats_medium);
    if (gov->cache_enabled) {
        uint64_t total_lookups = gov->cache_hits + gov->cache_misses;
        float hit_rate = total_lookups > 0 ?
            (float)gov->cache_hits * 100.0f / total_lookups : 0.0f;
        printf("    Cache hits/misses:  %lu/%lu (%.1f%% hit rate)\n",
               gov->cache_hits, gov->cache_misses, hit_rate);
    }
    printf("    History entries:    %d\n", gov->history_count);
    printf("    Active scopes:      %d\n", gov->scope_count);
}

void governor_set_interactive(phantom_governor_t *gov, int enabled) {
    if (!gov) return;
    gov->interactive = enabled;
    printf("  [governor] Interactive mode: %s\n", enabled ? "ON" : "OFF");
}

void governor_set_strict(phantom_governor_t *gov, int enabled) {
    if (!gov) return;
    gov->strict_mode = enabled;
    printf("  [governor] Strict mode: %s\n", enabled ? "ON" : "OFF");
}

void governor_set_logging(phantom_governor_t *gov, int enabled) {
    if (!gov) return;
    gov->log_all = enabled;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PATTERN DETECTION
 * ══════════════════════════════════════════════════════════════════════════════ */

int governor_detect_destructive(const char *code, size_t size,
                                char *reason, size_t reason_size) {
    if (!code || size == 0) return 0;

    for (int i = 0; destructive_patterns[i].pattern != NULL; i++) {
        const destructive_pattern_t *pat = &destructive_patterns[i];

        /* Simple string search (regex not implemented in this stub) */
        if (strcasestr_local(code, pat->pattern)) {
            if (reason && reason_size > 0) {
                snprintf(reason, reason_size,
                         "Detected '%s': %s",
                         pat->pattern, pat->description);
            }
            return 1;
        }
    }

    return 0;
}

uint32_t governor_infer_capabilities(const char *code, size_t size) {
    uint32_t caps = 0;

    if (!code || size == 0) return caps;

    for (int i = 0; capability_patterns[i].pattern != NULL; i++) {
        if (strcasestr_local(code, capability_patterns[i].pattern)) {
            caps |= capability_patterns[i].cap;
        }
    }

    return caps;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * THREAT ASSESSMENT
 * ══════════════════════════════════════════════════════════════════════════════ */

threat_level_t governor_assess_threat(governor_eval_request_t *request) {
    if (!request) return THREAT_CRITICAL;

    /* Check for destructive patterns */
    char reason[256];
    if (governor_detect_destructive(request->code_ptr, request->code_size,
                                     reason, sizeof(reason))) {
        if (request->threat_reason_count < 4) {
            strncpy(request->threat_reasons[request->threat_reason_count],
                    reason, 255);
            request->threat_reason_count++;
        }
        return THREAT_CRITICAL;
    }

    /* Check capability requirements */
    uint32_t caps = request->detected_caps;

    /* Critical: Governor bypass without trust */
    if (caps & CAP_GOVERNOR_BYPASS) {
        if (request->threat_reason_count < 4) {
            strncpy(request->threat_reasons[request->threat_reason_count],
                    "Requests Governor bypass capability", 255);
            request->threat_reason_count++;
        }
        return THREAT_HIGH;
    }

    /* High: Combination of network + system config */
    if ((caps & CAP_NETWORK) && (caps & CAP_SYSTEM_CONFIG)) {
        if (request->threat_reason_count < 4) {
            strncpy(request->threat_reasons[request->threat_reason_count],
                    "Requests both network and system configuration access", 255);
            request->threat_reason_count++;
        }
        return THREAT_HIGH;
    }

    /* Medium: Any elevated capability */
    if (caps & CAP_USER_APPROVE) {
        if (request->threat_reason_count < 4) {
            strncpy(request->threat_reasons[request->threat_reason_count],
                    "Requests capabilities requiring user approval", 255);
            request->threat_reason_count++;
        }
        return THREAT_MEDIUM;
    }

    /* Low: Process creation or high memory */
    if ((caps & CAP_CREATE_PROCESS) || (caps & CAP_HIGH_MEMORY)) {
        return THREAT_LOW;
    }

    return THREAT_NONE;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * INTERACTIVE PROMPT
 * ══════════════════════════════════════════════════════════════════════════════ */

int governor_prompt_user(phantom_governor_t *gov,
                         governor_eval_request_t *request,
                         const char *question) {
    if (!gov || !request) return 0;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                    GOVERNOR APPROVAL REQUEST                       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("  Code: %s\n", request->name[0] ? request->name : "(unnamed)");
    printf("  Size: %zu bytes\n", request->code_size);
    printf("  Description: %s\n",
           request->description[0] ? request->description : "(none)");

    /* Show threat level */
    printf("\n  Threat Level: ");
    switch (request->threat_level) {
        case THREAT_NONE:     printf("NONE (safe)\n"); break;
        case THREAT_LOW:      printf("LOW\n"); break;
        case THREAT_MEDIUM:   printf("MEDIUM\n"); break;
        case THREAT_HIGH:     printf("HIGH (caution)\n"); break;
        case THREAT_CRITICAL: printf("CRITICAL (destructive)\n"); break;
    }

    /* Show reasons */
    if (request->threat_reason_count > 0) {
        printf("\n  Analysis:\n");
        for (int i = 0; i < request->threat_reason_count; i++) {
            printf("    - %s\n", request->threat_reasons[i]);
        }
    }

    /* Show detected capabilities */
    if (request->detected_caps != 0) {
        char caps_buf[512];
        governor_caps_to_list(request->detected_caps, caps_buf, sizeof(caps_buf));
        printf("\n  Detected capabilities: %s\n", caps_buf);
    }

    /* Show the question */
    printf("\n  %s\n", question);
    printf("\n  [y] Approve   [n] Decline   [v] View code\n");
    printf("  > ");
    fflush(stdout);

    char response[16];
    if (fgets(response, sizeof(response), stdin) == NULL) {
        return 0;
    }

    char c = tolower(response[0]);

    /* Handle view code request */
    if (c == 'v') {
        printf("\n--- Code Preview (first 500 chars) ---\n");
        size_t preview_size = request->code_size < 500 ? request->code_size : 500;
        printf("%.*s", (int)preview_size, (const char *)request->code_ptr);
        if (request->code_size > 500) {
            printf("\n... (%zu more bytes)\n", request->code_size - 500);
        }
        printf("--- End Preview ---\n\n");

        /* Ask again */
        printf("  [y] Approve   [n] Decline\n");
        printf("  > ");
        fflush(stdout);

        if (fgets(response, sizeof(response), stdin) == NULL) {
            return 0;
        }
        c = tolower(response[0]);
    }

    return (c == 'y');
}

/* ══════════════════════════════════════════════════════════════════════════════
 * MAIN EVALUATION
 * ══════════════════════════════════════════════════════════════════════════════ */

int governor_evaluate_code(phantom_governor_t *gov,
                           governor_eval_request_t *request,
                           governor_eval_response_t *response) {
    if (!gov || !request || !response) return -1;

    memset(response, 0, sizeof(governor_eval_response_t));
    gov->total_evaluations++;

    /* Phase 1: Hash the code */
    governor_sha256(request->code_ptr, request->code_size,
                    (uint8_t *)request->code_hash);

    /* Phase 1.5: Check cache for previous evaluation */
    governor_cache_entry_t *cached = cache_lookup(gov, request->code_hash);
    if (cached) {
        /* Cache hit - return cached result */
        cached->hit_count++;
        gov->cache_hits++;

        response->decision = cached->decision;
        response->granted_caps = cached->granted_caps;
        request->threat_level = cached->threat_level;

        snprintf(response->summary, sizeof(response->summary),
                 "Cached: %s",
                 cached->decision == GOVERNOR_APPROVE ? "Previously approved" : "Previously declined");
        strncpy(response->decision_by, "cache", 63);

        /* Still log the decision */
        governor_log_decision(gov, request, response);
        return 0;
    }
    gov->cache_misses++;

    /* Phase 2: Infer capabilities from code */
    request->detected_caps = governor_infer_capabilities(request->code_ptr,
                                                          request->code_size);

    /* Phase 2.5: Behavioral analysis */
    governor_behavior_result_t behavior;
    if (governor_analyze_behavior(request->code_ptr, request->code_size, &behavior) == 0) {
        /* If suspicious behavior detected, add to threat reasons */
        if (behavior.suspicious_score > 0) {
            for (int i = 0; i < behavior.description_count && request->threat_reason_count < 4; i++) {
                strncpy(request->threat_reasons[request->threat_reason_count],
                        behavior.descriptions[i], 255);
                request->threat_reason_count++;
            }
        }

        /* Behavioral score influences detected capabilities */
        if (behavior.flags & BEHAVIOR_FORK_BOMB) {
            request->detected_caps |= CAP_CREATE_PROCESS;
        }
        if (behavior.flags & BEHAVIOR_MEMORY_BOMB) {
            request->detected_caps |= CAP_HIGH_MEMORY;
        }
    }

    /* Phase 3: Assess threat level */
    request->threat_level = governor_assess_threat(request);

    /* Behavioral analysis can escalate threat level */
    if (behavior.suspicious_score >= 50 && request->threat_level < THREAT_HIGH) {
        request->threat_level = THREAT_HIGH;
    } else if (behavior.suspicious_score >= 30 && request->threat_level < THREAT_MEDIUM) {
        request->threat_level = THREAT_MEDIUM;
    }

    /* Update threat statistics */
    switch (request->threat_level) {
        case THREAT_NONE:     gov->threats_none++; break;
        case THREAT_LOW:      gov->threats_low++; break;
        case THREAT_MEDIUM:   gov->threats_medium++; break;
        case THREAT_HIGH:     gov->threats_high++; break;
        case THREAT_CRITICAL: gov->threats_critical++; break;
    }

    /* Phase 4: Make decision */

    /* CRITICAL threats are ALWAYS declined */
    if (request->threat_level == THREAT_CRITICAL) {
        response->decision = GOVERNOR_DECLINE;
        snprintf(response->summary, sizeof(response->summary),
                 "Declined: Destructive operations detected");
        snprintf(response->reasoning, sizeof(response->reasoning),
                 "This code contains operations that violate the Prime Directive "
                 "'To Create, Not To Destroy'. The concepts of deletion, termination, "
                 "and destruction are architecturally impossible in Phantom. "
                 "Consider using 'hide' to make content invisible while preserving "
                 "it in the geology, or 'dormancy' instead of process termination.");

        if (request->threat_reason_count > 0) {
            snprintf(response->decline_reason, sizeof(response->decline_reason),
                     "%s", request->threat_reasons[0]);
        } else {
            snprintf(response->decline_reason, sizeof(response->decline_reason),
                     "Destructive patterns detected");
        }

        snprintf(response->alternatives, sizeof(response->alternatives),
                 "Replace deletion with phantom_syscall_hide(). "
                 "Replace process termination with phantom_process_suspend().");

        strncpy(response->decision_by, "auto-policy", 63);
        gov->auto_declined++;
        return 0;
    }

    /* HIGH threats require user approval in interactive mode, decline in strict */
    if (request->threat_level == THREAT_HIGH) {
        if (gov->strict_mode) {
            response->decision = GOVERNOR_DECLINE;
            snprintf(response->summary, sizeof(response->summary),
                     "Declined: High-risk code in strict mode");
            const char *reason = (request->threat_reason_count > 0) ?
                     request->threat_reasons[0] : "High-risk patterns detected";
            snprintf(response->decline_reason, sizeof(response->decline_reason),
                     "Strict mode: %.200s", reason);
            strncpy(response->decision_by, "strict-policy", 63);
            gov->auto_declined++;
            return 0;
        }

        if (gov->interactive) {
            int approved = governor_prompt_user(gov, request,
                "HIGH RISK: This code may perform dangerous operations. Approve execution?");
            response->user_prompted = 1;

            if (approved) {
                response->decision = GOVERNOR_APPROVE;
                strncpy(response->decision_by, "user", 63);
                gov->user_approved++;
            } else {
                response->decision = GOVERNOR_DECLINE;
                snprintf(response->decline_reason, sizeof(response->decline_reason),
                         "User declined execution");
                strncpy(response->decision_by, "user", 63);
                gov->user_declined++;
                return 0;
            }
        } else {
            /* Non-interactive, non-strict: decline high risk */
            response->decision = GOVERNOR_DECLINE;
            snprintf(response->decline_reason, sizeof(response->decline_reason),
                     "High-risk code requires interactive approval");
            strncpy(response->decision_by, "auto-policy", 63);
            gov->auto_declined++;
            return 0;
        }
    }

    /* MEDIUM threats: prompt user in interactive mode */
    else if (request->threat_level == THREAT_MEDIUM) {
        if (gov->interactive) {
            int approved = governor_prompt_user(gov, request,
                "This code requests elevated capabilities. Approve execution?");
            response->user_prompted = 1;

            if (approved) {
                response->decision = GOVERNOR_APPROVE;
                strncpy(response->decision_by, "user", 63);
                gov->user_approved++;
            } else {
                response->decision = GOVERNOR_DECLINE;
                snprintf(response->decline_reason, sizeof(response->decline_reason),
                         "User declined execution");
                strncpy(response->decision_by, "user", 63);
                gov->user_declined++;
                return 0;
            }
        } else if (gov->strict_mode) {
            response->decision = GOVERNOR_DECLINE;
            snprintf(response->decline_reason, sizeof(response->decline_reason),
                     "Strict mode: elevated capabilities require interactive approval");
            strncpy(response->decision_by, "strict-policy", 63);
            gov->auto_declined++;
            return 0;
        } else {
            /* Non-interactive, non-strict: auto-approve medium */
            response->decision = GOVERNOR_APPROVE;
            strncpy(response->decision_by, "auto", 63);
            gov->auto_approved++;
        }
    }

    /* LOW or NONE threats: auto-approve */
    else {
        response->decision = GOVERNOR_APPROVE;
        strncpy(response->decision_by, "auto", 63);
        gov->auto_approved++;
    }

    /* If approved, generate signature and set capabilities */
    if (response->decision == GOVERNOR_APPROVE) {
        response->granted_caps = request->detected_caps;
        response->approved_at = governor_time_now();

        /* Generate signature */
        uint8_t sig_input[64 + 8];
        memcpy(sig_input, request->code_hash, 32);
        memcpy(sig_input + 32, &response->approved_at, 8);
        memset(sig_input + 40, 0, 24);
        governor_sha256(sig_input, 40, response->signature);

        snprintf(response->summary, sizeof(response->summary),
                 "Approved: %s",
                 request->threat_level == THREAT_NONE ? "Safe code" :
                 request->threat_level == THREAT_LOW ? "Low-risk code" :
                 "User approved elevated access");

        snprintf(response->reasoning, sizeof(response->reasoning),
                 "Code analysis complete. Threat level: %s. "
                 "No destructive operations detected. "
                 "Approved for execution under the Prime Directive.",
                 governor_threat_to_string(request->threat_level));
    }

    /* Cache the result (non-CRITICAL only) */
    cache_store(gov, request->code_hash, response->decision,
                response->granted_caps, request->threat_level,
                response->valid_until > 0 ?
                    (response->valid_until - governor_time_now()) / 1000000000ULL : 0);

    /* Record in history */
    history_add(gov, request, response);

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * SIGNATURE VERIFICATION
 * ══════════════════════════════════════════════════════════════════════════════ */

int governor_verify_code(phantom_governor_t *gov,
                         const phantom_hash_t code_hash,
                         const phantom_signature_t signature) {
    if (!gov) return 0;

    /* Check trusted signatures cache */
    for (int i = 0; i < gov->trusted_count; i++) {
        if (memcmp(gov->trusted_sigs[i], signature, PHANTOM_SIGNATURE_SIZE) == 0) {
            return 1;
        }
    }

    /* Verify signature structure (simplified) */
    /* Real implementation would use public key cryptography */
    uint8_t verify_hash[32];
    governor_sha256(code_hash, PHANTOM_HASH_SIZE, verify_hash);

    /* Check that first 16 bytes match (simplified verification) */
    return memcmp(signature, verify_hash, 16) == 0;
}

int governor_trust_signature(phantom_governor_t *gov,
                             const phantom_signature_t signature) {
    if (!gov || gov->trusted_count >= GOVERNOR_MAX_TRUSTED) return -1;

    memcpy(gov->trusted_sigs[gov->trusted_count], signature, PHANTOM_SIGNATURE_SIZE);
    gov->trusted_count++;

    printf("  [governor] Added trusted signature (%d total)\n", gov->trusted_count);
    return 0;
}

int governor_is_trusted(phantom_governor_t *gov,
                        const phantom_signature_t signature) {
    if (!gov) return 0;

    for (int i = 0; i < gov->trusted_count; i++) {
        if (memcmp(gov->trusted_sigs[i], signature, PHANTOM_SIGNATURE_SIZE) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CAPABILITY HELPERS
 * ══════════════════════════════════════════════════════════════════════════════ */

const char *governor_cap_to_string(governor_capability_t cap) {
    switch (cap) {
        case CAP_READ_FILES:      return "read_files";
        case CAP_WRITE_FILES:     return "write_files";
        case CAP_CREATE_FILES:    return "create_files";
        case CAP_HIDE_FILES:      return "hide_files";
        case CAP_CREATE_PROCESS:  return "create_process";
        case CAP_IPC_SEND:        return "ipc_send";
        case CAP_IPC_RECEIVE:     return "ipc_receive";
        case CAP_ALLOC_MEMORY:    return "alloc_memory";
        case CAP_HIGH_MEMORY:     return "high_memory";
        case CAP_HIGH_PRIORITY:   return "high_priority";
        case CAP_NETWORK:         return "network";
        case CAP_SYSTEM_CONFIG:   return "system_config";
        case CAP_RAW_DEVICE:      return "raw_device";
        case CAP_GOVERNOR_BYPASS: return "governor_bypass";
        case CAP_READ_PROCFS:     return "read_procfs";
        case CAP_READ_DEVFS:      return "read_devfs";
        case CAP_NETWORK_SECURE:  return "network_secure";
        case CAP_NETWORK_INSECURE:return "network_insecure";
        default:                  return "unknown";
    }
}

void governor_caps_to_list(uint32_t caps, char *buf, size_t size) {
    if (!buf || size == 0) return;

    buf[0] = '\0';
    size_t pos = 0;

    for (int i = 0; i < 18; i++) {
        uint32_t cap = (1 << i);
        if (caps & cap) {
            const char *name = governor_cap_to_string(cap);
            size_t len = strlen(name);

            if (pos + len + 2 < size) {
                if (pos > 0) {
                    buf[pos++] = ',';
                    buf[pos++] = ' ';
                }
                strcpy(buf + pos, name);
                pos += len;
            }
        }
    }

    buf[pos] = '\0';
}

governor_capability_t governor_string_to_cap(const char *str) {
    if (!str) return 0;

    if (strcmp(str, "read_files") == 0)      return CAP_READ_FILES;
    if (strcmp(str, "write_files") == 0)     return CAP_WRITE_FILES;
    if (strcmp(str, "create_files") == 0)    return CAP_CREATE_FILES;
    if (strcmp(str, "hide_files") == 0)      return CAP_HIDE_FILES;
    if (strcmp(str, "create_process") == 0)  return CAP_CREATE_PROCESS;
    if (strcmp(str, "ipc_send") == 0)        return CAP_IPC_SEND;
    if (strcmp(str, "ipc_receive") == 0)     return CAP_IPC_RECEIVE;
    if (strcmp(str, "alloc_memory") == 0)    return CAP_ALLOC_MEMORY;
    if (strcmp(str, "high_memory") == 0)     return CAP_HIGH_MEMORY;
    if (strcmp(str, "high_priority") == 0)   return CAP_HIGH_PRIORITY;
    if (strcmp(str, "network") == 0)         return CAP_NETWORK;
    if (strcmp(str, "system_config") == 0)   return CAP_SYSTEM_CONFIG;
    if (strcmp(str, "raw_device") == 0)      return CAP_RAW_DEVICE;
    if (strcmp(str, "governor_bypass") == 0) return CAP_GOVERNOR_BYPASS;
    if (strcmp(str, "read_procfs") == 0)     return CAP_READ_PROCFS;
    if (strcmp(str, "read_devfs") == 0)      return CAP_READ_DEVFS;
    if (strcmp(str, "network_secure") == 0)  return CAP_NETWORK_SECURE;
    if (strcmp(str, "network_insecure") == 0) return CAP_NETWORK_INSECURE;

    return 0;  /* Unknown capability */
}

/* ══════════════════════════════════════════════════════════════════════════════
 * THREAT LEVEL HELPERS
 * ══════════════════════════════════════════════════════════════════════════════ */

const char *governor_threat_to_string(threat_level_t level) {
    switch (level) {
        case THREAT_NONE:     return "NONE";
        case THREAT_LOW:      return "LOW";
        case THREAT_MEDIUM:   return "MEDIUM";
        case THREAT_HIGH:     return "HIGH";
        case THREAT_CRITICAL: return "CRITICAL";
        default:              return "UNKNOWN";
    }
}

const char *governor_threat_to_color(threat_level_t level) {
    /* ANSI color codes */
    switch (level) {
        case THREAT_NONE:     return "\033[32m";  /* Green */
        case THREAT_LOW:      return "\033[33m";  /* Yellow */
        case THREAT_MEDIUM:   return "\033[33m";  /* Yellow */
        case THREAT_HIGH:     return "\033[31m";  /* Red */
        case THREAT_CRITICAL: return "\033[31;1m";/* Bright Red */
        default:              return "\033[0m";   /* Reset */
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * STATISTICS
 * ══════════════════════════════════════════════════════════════════════════════ */

void governor_print_stats(phantom_governor_t *gov) {
    if (!gov) return;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║                 GOVERNOR STATISTICS                   ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Evaluations:     %lu total\n", gov->total_evaluations);
    printf("\n");
    printf("  Decisions:\n");
    printf("    Auto-approved: %lu\n", gov->auto_approved);
    printf("    User-approved: %lu\n", gov->user_approved);
    printf("    User-declined: %lu\n", gov->user_declined);
    printf("    Auto-declined: %lu\n", gov->auto_declined);
    printf("\n");
    printf("  Threat Levels Seen:\n");
    printf("    None:     %lu\n", gov->threats_none);
    printf("    Low:      %lu\n", gov->threats_low);
    printf("    Medium:   %lu\n", gov->threats_medium);
    printf("    High:     %lu\n", gov->threats_high);
    printf("    Critical: %lu\n", gov->threats_critical);
    printf("\n");
    printf("  Trusted signatures: %d\n", gov->trusted_count);
    printf("\n");
    printf("  Mode: %s, %s\n",
           gov->interactive ? "Interactive" : "Non-interactive",
           gov->strict_mode ? "Strict" : "Permissive");
    printf("\n");
}

/* ══════════════════════════════════════════════════════════════════════════════
 * LOGGING (to GeoFS)
 * ══════════════════════════════════════════════════════════════════════════════ */

void governor_log_decision(phantom_governor_t *gov,
                           governor_eval_request_t *request,
                           governor_eval_response_t *response) {
    if (!gov || !gov->log_all || !gov->kernel) return;

    /* Build log entry */
    char log_entry[2048];
    char hash_str[65];

    for (int i = 0; i < 32; i++) {
        sprintf(hash_str + (i * 2), "%02x", request->code_hash[i]);
    }
    hash_str[64] = '\0';

    snprintf(log_entry, sizeof(log_entry),
             "timestamp=%lu\n"
             "code_hash=%s\n"
             "name=%s\n"
             "size=%zu\n"
             "threat=%s\n"
             "decision=%s\n"
             "decided_by=%s\n"
             "caps=%u\n"
             "summary=%s\n",
             governor_time_now(),
             hash_str,
             request->name,
             request->code_size,
             governor_threat_to_string(request->threat_level),
             response->decision == GOVERNOR_APPROVE ? "APPROVE" : "DECLINE",
             response->decision_by,
             response->granted_caps,
             response->summary);

    /* Store in GeoFS if available */
    geofs_volume_t *vol = (geofs_volume_t *)gov->kernel->geofs_volume;
    if (vol) {
        geofs_hash_t hash;
        geofs_content_store(vol, log_entry, strlen(log_entry), hash);

        char path[256];
        snprintf(path, sizeof(path), "%s/%lu",
                 GOVERNOR_LOG_PATH, governor_time_now());
        geofs_ref_create(vol, path, hash);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * AI INTEGRATION
 * ══════════════════════════════════════════════════════════════════════════════ */

void governor_set_ai(phantom_governor_t *gov, struct phantom_ai *ai) {
    if (gov) {
        gov->ai = ai;
        if (ai) {
            printf("  [governor] AI integration enabled\n");
        }
    }
}

void governor_enable_ai(phantom_governor_t *gov, int enabled) {
    if (gov) {
        gov->ai_enabled = enabled;
        printf("  [governor] AI-enhanced analysis: %s\n", enabled ? "ON" : "OFF");
    }
}

void governor_enable_ai_explain(phantom_governor_t *gov, int enabled) {
    if (gov) {
        gov->ai_explain = enabled;
    }
}

int governor_evaluate_with_ai(phantom_governor_t *gov,
                              governor_eval_request_t *request,
                              governor_eval_response_t *response) {
    if (!gov || !request || !response) return -1;

    /* First, do standard evaluation */
    int result = governor_evaluate_code(gov, request, response);
    if (result != 0) return result;

    /* If AI is available and enabled, enhance with AI analysis */
    if (gov->ai && gov->ai_enabled && request->threat_level > THREAT_NONE) {
        gov->ai_analyses++;

        /* Use AI to analyze the code */
        phantom_ai_governor_analysis_t ai_analysis;
        if (phantom_ai_analyze_code(gov->ai, request->code_ptr, request->code_size,
                                     &ai_analysis) == 0) {

            /* AI might catch things pattern matching misses */
            if (ai_analysis.threat_level > (int)request->threat_level) {
                /* AI detected higher threat - escalate */
                request->threat_level = (threat_level_t)ai_analysis.threat_level;

                /* Update response */
                if (ai_analysis.recommended_decision != (int)response->decision) {
                    /* Log the AI override */
                    if (gov->log_all) {
                        printf("  [governor] AI escalated threat from %s to %s\n",
                               governor_threat_to_string((threat_level_t)(ai_analysis.threat_level - 1)),
                               governor_threat_to_string(request->threat_level));
                    }

                    /* If AI recommends decline but we approved, reconsider */
                    if (ai_analysis.recommended_decision == GOVERNOR_DECLINE &&
                        response->decision == GOVERNOR_APPROVE) {
                        response->decision = GOVERNOR_DECLINE;
                        snprintf(response->summary, sizeof(response->summary),
                                 "AI Analysis: %.200s", ai_analysis.summary);
                        strncpy(response->decline_reason, ai_analysis.user_explanation,
                                sizeof(response->decline_reason) - 1);
                        response->decline_reason[sizeof(response->decline_reason) - 1] = '\0';
                        gov->auto_declined++;
                        gov->auto_approved--;  /* Undo the previous approval count */
                    }
                }
            }

            /* Add AI explanation if enabled */
            if (gov->ai_explain && ai_analysis.detailed_analysis[0]) {
                char temp_reasoning[1024];
                snprintf(temp_reasoning, sizeof(temp_reasoning),
                         "%.500s\n\nAI Analysis: %.450s",
                         response->reasoning, ai_analysis.detailed_analysis);
                strncpy(response->reasoning, temp_reasoning, sizeof(response->reasoning) - 1);
                response->reasoning[sizeof(response->reasoning) - 1] = '\0';
            }

            /* If AI suggested alternative, include it */
            if (ai_analysis.alternative_approach[0]) {
                strncpy(response->alternatives, ai_analysis.alternative_approach,
                        sizeof(response->alternatives) - 1);
                response->alternatives[sizeof(response->alternatives) - 1] = '\0';
            }

            gov->ai_assists++;
        }
    }

    return 0;
}

int governor_get_ai_explanation(phantom_governor_t *gov,
                                const phantom_hash_t code_hash,
                                char *explanation, size_t explanation_size) {
    if (!gov || !code_hash || !explanation || explanation_size == 0) return -1;

    if (!gov->ai || !gov->ai_enabled) {
        snprintf(explanation, explanation_size,
                 "AI is not available. Enable AI with 'ai config enable'.");
        return -1;
    }

    /* Look up the code hash in history to get context */
    for (int i = 0; i < gov->history_count; i++) {
        governor_history_entry_t entry;
        if (governor_get_history(gov, i, &entry) == 0 &&
            memcmp(entry.code_hash, code_hash, PHANTOM_HASH_SIZE) == 0) {

            /* Found it - ask AI to explain */
            char prompt[768];
            snprintf(prompt, sizeof(prompt),
                     "Explain this Governor decision:\n"
                     "Code: %.200s\n"
                     "Decision: %s\n"
                     "Threat Level: %s\n"
                     "Summary: %.200s",
                     entry.name,
                     entry.decision == GOVERNOR_APPROVE ? "APPROVED" : "DECLINED",
                     governor_threat_to_string(entry.threat_level),
                     entry.summary);

            return phantom_ai_chat(gov->ai, prompt, explanation, explanation_size);
        }
    }

    snprintf(explanation, explanation_size, "Code hash not found in history.");
    return -1;
}

int governor_suggest_safe_code(phantom_governor_t *gov,
                               const char *unsafe_code, size_t code_size,
                               char *safe_code, size_t safe_code_size) {
    if (!gov || !unsafe_code || code_size == 0 || !safe_code || safe_code_size == 0) {
        return -1;
    }

    if (!gov->ai || !gov->ai_enabled) {
        /* Fall back to simple pattern replacement */
        strncpy(safe_code, unsafe_code, safe_code_size - 1);
        safe_code[safe_code_size - 1] = '\0';

        /* Simple replacements */
        char *p;
        while ((p = strstr(safe_code, "unlink")) != NULL) {
            memcpy(p, "hide  ", 6);  /* Same length */
        }
        while ((p = strstr(safe_code, "remove")) != NULL) {
            memcpy(p, "hide  ", 6);
        }
        while ((p = strstr(safe_code, "kill(")) != NULL) {
            memcpy(p, "susp(", 5);  /* suspend */
        }
        return 0;
    }

    /* Use AI for intelligent transformation */
    return phantom_ai_suggest_alternative(gov->ai, unsafe_code,
                                           safe_code, safe_code_size);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CACHE MANAGEMENT
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Hash function for cache index */
static int cache_hash_index(const phantom_hash_t hash) {
    /* Use first 4 bytes of hash as index */
    uint32_t idx = ((uint32_t)hash[0] << 24) |
                   ((uint32_t)hash[1] << 16) |
                   ((uint32_t)hash[2] << 8) |
                   ((uint32_t)hash[3]);
    return idx % GOVERNOR_CACHE_SIZE;
}

/* Look up a cached evaluation result */
static governor_cache_entry_t *cache_lookup(phantom_governor_t *gov,
                                             const phantom_hash_t code_hash) {
    if (!gov || !gov->cache_enabled) return NULL;

    int idx = cache_hash_index(code_hash);
    governor_cache_entry_t *entry = &gov->eval_cache[idx];

    if (entry->valid && memcmp(entry->code_hash, code_hash, PHANTOM_HASH_SIZE) == 0) {
        /* Check expiration */
        if (entry->valid_until != 0 && governor_time_now() > entry->valid_until) {
            entry->valid = 0;
            return NULL;
        }
        return entry;
    }

    /* Try linear probing for collision resolution */
    for (int i = 1; i < 8; i++) {
        int probe_idx = (idx + i) % GOVERNOR_CACHE_SIZE;
        entry = &gov->eval_cache[probe_idx];

        if (entry->valid && memcmp(entry->code_hash, code_hash, PHANTOM_HASH_SIZE) == 0) {
            if (entry->valid_until != 0 && governor_time_now() > entry->valid_until) {
                entry->valid = 0;
                return NULL;
            }
            return entry;
        }
    }

    return NULL;
}

/* Store an evaluation result in cache */
static void cache_store(phantom_governor_t *gov, const phantom_hash_t code_hash,
                        governor_decision_t decision, uint32_t granted_caps,
                        threat_level_t threat_level, uint64_t valid_duration) {
    if (!gov || !gov->cache_enabled) return;

    /* CRITICAL decisions should not be cached - always re-evaluate */
    if (threat_level == THREAT_CRITICAL) return;

    int idx = cache_hash_index(code_hash);
    governor_cache_entry_t *entry = &gov->eval_cache[idx];

    /* If slot is taken by different hash, try probing */
    if (entry->valid && memcmp(entry->code_hash, code_hash, PHANTOM_HASH_SIZE) != 0) {
        for (int i = 1; i < 8; i++) {
            int probe_idx = (idx + i) % GOVERNOR_CACHE_SIZE;
            governor_cache_entry_t *probe = &gov->eval_cache[probe_idx];
            if (!probe->valid) {
                entry = probe;
                break;
            }
        }
        /* If all probes full, overwrite original slot */
    }

    memcpy(entry->code_hash, code_hash, PHANTOM_HASH_SIZE);
    entry->decision = decision;
    entry->granted_caps = granted_caps;
    entry->threat_level = threat_level;
    entry->cached_at = governor_time_now();
    entry->valid_until = valid_duration > 0 ?
        entry->cached_at + valid_duration * 1000000000ULL : 0;
    entry->hit_count = 0;
    entry->valid = 1;
}

void governor_enable_cache(phantom_governor_t *gov, int enabled) {
    if (!gov) return;
    gov->cache_enabled = enabled;
    printf("  [governor] Evaluation cache: %s\n", enabled ? "ON" : "OFF");
}

void governor_clear_cache(phantom_governor_t *gov) {
    if (!gov) return;

    int cleared = 0;
    for (int i = 0; i < GOVERNOR_CACHE_SIZE; i++) {
        if (gov->eval_cache[i].valid) {
            gov->eval_cache[i].valid = 0;
            cleared++;
        }
    }
    gov->cache_hits = 0;
    gov->cache_misses = 0;
    printf("  [governor] Cleared %d cache entries\n", cleared);
}

int governor_invalidate_cache(phantom_governor_t *gov, const phantom_hash_t code_hash) {
    if (!gov) return -1;

    int idx = cache_hash_index(code_hash);

    /* Check main slot and probed slots */
    for (int i = 0; i < 8; i++) {
        int probe_idx = (idx + i) % GOVERNOR_CACHE_SIZE;
        governor_cache_entry_t *entry = &gov->eval_cache[probe_idx];

        if (entry->valid && memcmp(entry->code_hash, code_hash, PHANTOM_HASH_SIZE) == 0) {
            entry->valid = 0;
            return 0;
        }
    }

    return -1;  /* Not found */
}

void governor_print_cache_stats(phantom_governor_t *gov) {
    if (!gov) return;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║                 GOVERNOR CACHE STATS                  ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("  Status: %s\n", gov->cache_enabled ? "ENABLED" : "DISABLED");
    printf("  Capacity: %d entries\n", GOVERNOR_CACHE_SIZE);

    int used = 0;
    uint64_t total_hits = 0;
    for (int i = 0; i < GOVERNOR_CACHE_SIZE; i++) {
        if (gov->eval_cache[i].valid) {
            used++;
            total_hits += gov->eval_cache[i].hit_count;
        }
    }

    printf("  Used slots: %d (%.1f%%)\n", used,
           (float)used * 100.0f / GOVERNOR_CACHE_SIZE);

    uint64_t total_lookups = gov->cache_hits + gov->cache_misses;
    if (total_lookups > 0) {
        printf("  Hit rate: %.1f%% (%lu hits, %lu misses)\n",
               (float)gov->cache_hits * 100.0f / total_lookups,
               gov->cache_hits, gov->cache_misses);
    } else {
        printf("  Hit rate: N/A (no lookups yet)\n");
    }

    printf("  Total entry hits: %lu\n", total_hits);
    printf("\n");
}

/* ══════════════════════════════════════════════════════════════════════════════
 * APPROVAL HISTORY
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Add an entry to the history (circular buffer) */
static void history_add(phantom_governor_t *gov, governor_eval_request_t *request,
                        governor_eval_response_t *response) {
    if (!gov) return;

    governor_history_entry_t *entry = &gov->history[gov->history_head];

    memcpy(entry->code_hash, request->code_hash, PHANTOM_HASH_SIZE);
    strncpy(entry->name, request->name, sizeof(entry->name) - 1);
    entry->decision = response->decision;
    entry->threat_level = request->threat_level;
    entry->granted_caps = response->granted_caps;
    strncpy(entry->decision_by, response->decision_by, sizeof(entry->decision_by) - 1);
    strncpy(entry->summary, response->summary, sizeof(entry->summary) - 1);
    entry->timestamp = governor_time_now();

    /* Can rollback user decisions and non-critical auto decisions */
    entry->can_rollback = (request->threat_level != THREAT_CRITICAL);

    gov->history_head = (gov->history_head + 1) % GOVERNOR_HISTORY_SIZE;
    if (gov->history_count < GOVERNOR_HISTORY_SIZE) {
        gov->history_count++;
    }
}

int governor_get_history(phantom_governor_t *gov, int index,
                         governor_history_entry_t *entry) {
    if (!gov || !entry || index < 0 || index >= gov->history_count) return -1;

    /* Convert index to circular buffer position (0 = most recent) */
    int pos = (gov->history_head - 1 - index + GOVERNOR_HISTORY_SIZE) % GOVERNOR_HISTORY_SIZE;
    *entry = gov->history[pos];
    return 0;
}

int governor_history_count(phantom_governor_t *gov) {
    return gov ? gov->history_count : 0;
}

int governor_rollback(phantom_governor_t *gov, int history_index) {
    if (!gov || history_index < 0 || history_index >= gov->history_count) return -1;

    int pos = (gov->history_head - 1 - history_index + GOVERNOR_HISTORY_SIZE) % GOVERNOR_HISTORY_SIZE;
    governor_history_entry_t *entry = &gov->history[pos];

    if (!entry->can_rollback) {
        printf("  [governor] Cannot rollback: decision was for CRITICAL threat\n");
        return -1;
    }

    /* Invalidate any cached result for this code hash */
    governor_invalidate_cache(gov, entry->code_hash);

    /* Remove from trusted if it was approved */
    if (entry->decision == GOVERNOR_APPROVE) {
        for (int i = 0; i < gov->trusted_count; i++) {
            /* Check if this signature matches (simplified) */
            uint8_t sig_check[PHANTOM_SIGNATURE_SIZE];
            governor_sha256(entry->code_hash, PHANTOM_HASH_SIZE, sig_check);
            if (memcmp(gov->trusted_sigs[i], sig_check, 16) == 0) {
                /* Remove by shifting */
                for (int j = i; j < gov->trusted_count - 1; j++) {
                    memcpy(gov->trusted_sigs[j], gov->trusted_sigs[j + 1],
                           PHANTOM_SIGNATURE_SIZE);
                }
                gov->trusted_count--;
                break;
            }
        }
    }

    char hash_str[17];
    for (int i = 0; i < 8; i++) {
        sprintf(hash_str + (i * 2), "%02x", entry->code_hash[i]);
    }
    hash_str[16] = '\0';

    printf("  [governor] Rolled back decision for %s (hash: %s...)\n",
           entry->name[0] ? entry->name : "(unnamed)", hash_str);
    printf("  [governor] Original decision: %s by %s\n",
           entry->decision == GOVERNOR_APPROVE ? "APPROVE" : "DECLINE",
           entry->decision_by);

    entry->can_rollback = 0;  /* Mark as already rolled back */
    return 0;
}

void governor_print_history(phantom_governor_t *gov, int max_entries) {
    if (!gov) return;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║                 GOVERNOR HISTORY                      ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    if (gov->history_count == 0) {
        printf("  (no history entries)\n\n");
        return;
    }

    int count = (max_entries > 0 && max_entries < gov->history_count) ?
        max_entries : gov->history_count;

    printf("  Showing %d of %d entries (most recent first):\n\n", count, gov->history_count);

    for (int i = 0; i < count; i++) {
        governor_history_entry_t entry;
        if (governor_get_history(gov, i, &entry) == 0) {
            char hash_str[17];
            for (int j = 0; j < 8; j++) {
                sprintf(hash_str + (j * 2), "%02x", entry.code_hash[j]);
            }
            hash_str[16] = '\0';

            printf("  [%d] %s%s\n", i,
                   entry.decision == GOVERNOR_APPROVE ? "\033[32m✓ APPROVED\033[0m" :
                                                        "\033[31m✗ DECLINED\033[0m",
                   entry.can_rollback ? "" : " (locked)");
            printf("      Name: %s\n", entry.name[0] ? entry.name : "(unnamed)");
            printf("      Hash: %s...\n", hash_str);
            printf("      Threat: %s | By: %s\n",
                   governor_threat_to_string(entry.threat_level),
                   entry.decision_by);
            printf("      %s\n\n", entry.summary);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CAPABILITY SCOPING
 * ══════════════════════════════════════════════════════════════════════════════ */

int governor_add_scope(phantom_governor_t *gov, uint32_t capability,
                       const char *path_pattern, uint64_t max_bytes,
                       uint64_t duration_seconds) {
    if (!gov || gov->scope_count >= GOVERNOR_MAX_SCOPES) return -1;

    governor_cap_scope_t *scope = &gov->cap_scopes[gov->scope_count];

    scope->capability = capability;
    if (path_pattern) {
        strncpy(scope->path_pattern, path_pattern, sizeof(scope->path_pattern) - 1);
    } else {
        strcpy(scope->path_pattern, "*");  /* Match all paths */
    }
    scope->max_bytes = max_bytes;
    scope->valid_until = duration_seconds > 0 ?
        governor_time_now() + duration_seconds * 1000000000ULL : 0;
    scope->active = 1;

    gov->scope_count++;

    char caps_buf[256];
    governor_caps_to_list(capability, caps_buf, sizeof(caps_buf));
    printf("  [governor] Added scope: %s for path '%s'",
           caps_buf, scope->path_pattern);
    if (max_bytes > 0) printf(" (max %lu bytes)", max_bytes);
    if (duration_seconds > 0) printf(" (expires in %lu seconds)", duration_seconds);
    printf("\n");

    return gov->scope_count - 1;
}

int governor_remove_scope(phantom_governor_t *gov, int scope_index) {
    if (!gov || scope_index < 0 || scope_index >= gov->scope_count) return -1;

    /* Shift remaining scopes */
    for (int i = scope_index; i < gov->scope_count - 1; i++) {
        gov->cap_scopes[i] = gov->cap_scopes[i + 1];
    }
    gov->scope_count--;

    printf("  [governor] Removed scope %d (%d remaining)\n",
           scope_index, gov->scope_count);
    return 0;
}

int governor_check_scope(phantom_governor_t *gov, uint32_t capability,
                         const char *path, uint64_t size) {
    if (!gov || gov->scope_count == 0) return 1;  /* No scopes = all allowed */

    phantom_time_t now = governor_time_now();
    int found_scope = 0;

    for (int i = 0; i < gov->scope_count; i++) {
        governor_cap_scope_t *scope = &gov->cap_scopes[i];

        if (!scope->active) continue;

        /* Check expiration */
        if (scope->valid_until != 0 && now > scope->valid_until) {
            scope->active = 0;
            continue;
        }

        /* Check if this scope applies to requested capability */
        if (!(scope->capability & capability)) continue;

        found_scope = 1;

        /* Check path pattern */
        if (path && !glob_match(scope->path_pattern, path)) {
            continue;  /* Path doesn't match, try next scope */
        }

        /* Check size limit */
        if (scope->max_bytes > 0 && size > scope->max_bytes) {
            return 0;  /* Exceeds size limit */
        }

        return 1;  /* Scope allows this operation */
    }

    /* If we found relevant scopes but none matched, deny */
    if (found_scope) return 0;

    /* No relevant scopes = allowed by default */
    return 1;
}

void governor_print_scopes(phantom_governor_t *gov) {
    if (!gov) return;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║               CAPABILITY SCOPES                       ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    if (gov->scope_count == 0) {
        printf("  (no scopes defined - all capabilities unrestricted)\n\n");
        return;
    }

    phantom_time_t now = governor_time_now();

    for (int i = 0; i < gov->scope_count; i++) {
        governor_cap_scope_t *scope = &gov->cap_scopes[i];

        char caps_buf[256];
        governor_caps_to_list(scope->capability, caps_buf, sizeof(caps_buf));

        printf("  [%d] %s\n", i, scope->active ? "ACTIVE" : "EXPIRED");
        printf("      Capabilities: %s\n", caps_buf);
        printf("      Path pattern: %s\n", scope->path_pattern);
        if (scope->max_bytes > 0) {
            printf("      Max size: %lu bytes\n", scope->max_bytes);
        }
        if (scope->valid_until > 0) {
            int64_t remaining = (scope->valid_until - now) / 1000000000LL;
            if (remaining > 0) {
                printf("      Expires in: %ld seconds\n", remaining);
            } else {
                printf("      Expired\n");
            }
        } else {
            printf("      Duration: permanent\n");
        }
        printf("\n");
    }
}

int governor_cleanup_scopes(phantom_governor_t *gov) {
    if (!gov) return 0;

    phantom_time_t now = governor_time_now();
    int removed = 0;

    for (int i = gov->scope_count - 1; i >= 0; i--) {
        governor_cap_scope_t *scope = &gov->cap_scopes[i];

        if (scope->valid_until != 0 && now > scope->valid_until) {
            governor_remove_scope(gov, i);
            removed++;
        }
    }

    if (removed > 0) {
        printf("  [governor] Cleaned up %d expired scopes\n", removed);
    }
    return removed;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * BEHAVIORAL ANALYSIS
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Count occurrences of a pattern in code */
static int count_pattern(const char *code, size_t size, const char *pattern) {
    int count = 0;
    const char *p = code;
    size_t plen = strlen(pattern);

    while ((p = strstr(p, pattern)) != NULL && (size_t)(p - code) < size) {
        count++;
        p += plen;
    }
    return count;
}

/* Check if code is within a loop construct */
static int in_loop_context(const char *code, size_t offset) {
    /* Look backwards for loop keywords */
    const char *start = code;
    const char *pos = code + offset;

    /* Track brace nesting */
    int brace_depth = 0;

    /* Scan backwards */
    while (pos > start) {
        pos--;
        if (*pos == '}') brace_depth++;
        else if (*pos == '{') {
            if (brace_depth > 0) brace_depth--;
            else {
                /* Check what precedes this brace */
                const char *check = pos - 1;
                while (check > start && (*check == ' ' || *check == '\n' || *check == '\t')) {
                    check--;
                }
                /* Look for loop keywords */
                if (check - start >= 5 && strncmp(check - 4, "while", 5) == 0) return 1;
                if (check - start >= 3 && strncmp(check - 2, "for", 3) == 0) return 1;
                if (check - start >= 2 && strncmp(check - 1, "do", 2) == 0) return 1;
            }
        }
    }
    return 0;
}

/* Detect potential infinite loops */
static int detect_infinite_loop(const char *code, size_t size, char *desc, size_t desc_size) {
    /* while(1), while(true), for(;;) */
    if (strstr(code, "while(1)") || strstr(code, "while (1)") ||
        strstr(code, "while(true)") || strstr(code, "while (true)") ||
        strstr(code, "for(;;)") || strstr(code, "for (;;)")) {

        /* Check if there's a break or return inside */
        if (!strstr(code, "break") && !strstr(code, "return")) {
            snprintf(desc, desc_size, "Infinite loop without exit condition detected");
            return 1;
        }
    }

    /* Recursive function without base case */
    /* Simple heuristic: function name appears in its own body more than once */
    (void)size;  /* Suppress unused warning for now */

    return 0;
}

/* Detect memory bomb patterns */
static int detect_memory_bomb(const char *code, size_t size, char *desc, size_t desc_size) {
    int malloc_in_loop = 0;

    /* Look for malloc/calloc/realloc in loop context */
    const char *patterns[] = {"malloc", "calloc", "realloc", "new ", NULL};

    for (int i = 0; patterns[i]; i++) {
        const char *found = strstr(code, patterns[i]);
        if (found && in_loop_context(code, found - code)) {
            malloc_in_loop++;
        }
    }

    /* Check for exponential growth patterns */
    int exponential = 0;
    if (strstr(code, "*= 2") || strstr(code, "<<= 1") ||
        strstr(code, "<< 1") || strstr(code, "* 2")) {
        if (strstr(code, "malloc") || strstr(code, "size")) {
            exponential = 1;
        }
    }

    if (malloc_in_loop > 0 && !strstr(code, "free")) {
        snprintf(desc, desc_size,
                 "Memory allocation in loop without free - potential memory bomb");
        return 1;
    }

    if (exponential && malloc_in_loop) {
        snprintf(desc, desc_size,
                 "Exponential memory growth pattern detected");
        return 1;
    }

    (void)size;
    return 0;
}

/* Detect fork bomb patterns */
static int detect_fork_bomb(const char *code, size_t size, char *desc, size_t desc_size) {
    int fork_count = count_pattern(code, size, "fork()");

    /* Fork in a loop */
    const char *fork_pos = strstr(code, "fork()");
    if (fork_pos && in_loop_context(code, fork_pos - code)) {
        snprintf(desc, desc_size,
                 "fork() inside loop - potential fork bomb");
        return 1;
    }

    /* Multiple forks without wait */
    if (fork_count > 1 && !strstr(code, "wait") && !strstr(code, "waitpid")) {
        snprintf(desc, desc_size,
                 "Multiple fork() calls without wait - potential fork bomb");
        return 1;
    }

    /* Classic fork bomb pattern: while/for + fork */
    if (strstr(code, "while") && strstr(code, "fork")) {
        snprintf(desc, desc_size,
                 "while + fork pattern detected - likely fork bomb");
        return 1;
    }

    return 0;
}

/* Detect code obfuscation */
static int detect_obfuscation(const char *code, size_t size, char *desc, size_t desc_size) {
    /* Very long lines (>500 chars without newline) */
    const char *p = code;
    int line_len = 0;
    int max_line = 0;

    while (*p && (size_t)(p - code) < size) {
        if (*p == '\n') {
            if (line_len > max_line) max_line = line_len;
            line_len = 0;
        } else {
            line_len++;
        }
        p++;
    }

    if (max_line > 500) {
        snprintf(desc, desc_size,
                 "Very long line (%d chars) - possible obfuscation", max_line);
        return 1;
    }

    /* Excessive use of hex/octal */
    int hex_count = count_pattern(code, size, "0x");
    int octal_count = 0;
    p = code;
    while (*p && (size_t)(p - code) < size) {
        if (*p == '0' && p[1] >= '0' && p[1] <= '7' && p[1] != 'x') {
            octal_count++;
        }
        p++;
    }

    if (hex_count > 20 || octal_count > 10) {
        snprintf(desc, desc_size,
                 "Excessive hex/octal constants - possible obfuscation");
        return 1;
    }

    /* XOR operations (often used for decoding) */
    if (count_pattern(code, size, "^=") > 5 ||
        count_pattern(code, size, " ^ ") > 5) {
        snprintf(desc, desc_size,
                 "Excessive XOR operations - possible encoded payload");
        return 1;
    }

    return 0;
}

/* Detect base64/hex encoded payloads */
static int detect_encoded_payload(const char *code, size_t size, char *desc, size_t desc_size) {
    /* Long base64-like strings */
    const char *p = code;
    int b64_run = 0;
    int max_b64_run = 0;

    while (*p && (size_t)(p - code) < size) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '+' || *p == '/' || *p == '=') {
            b64_run++;
        } else {
            if (b64_run > max_b64_run) max_b64_run = b64_run;
            b64_run = 0;
        }
        p++;
    }

    if (max_b64_run > 100) {
        snprintf(desc, desc_size,
                 "Long base64-like string (%d chars) - possible encoded payload",
                 max_b64_run);
        return 1;
    }

    /* Look for decode patterns */
    if (strstr(code, "base64_decode") || strstr(code, "b64decode") ||
        strstr(code, "atob(") || strstr(code, "from_base64")) {
        snprintf(desc, desc_size, "Base64 decode function detected");
        return 1;
    }

    return 0;
}

/* Detect shell injection patterns */
static int detect_shell_injection(const char *code, size_t size, char *desc, size_t desc_size) {
    /* system() with user input patterns */
    if (strstr(code, "system(") && (strstr(code, "argv") || strstr(code, "getenv") ||
                                      strstr(code, "scanf") || strstr(code, "gets"))) {
        snprintf(desc, desc_size,
                 "system() with potential user input - shell injection risk");
        return 1;
    }

    /* popen with concatenation */
    if (strstr(code, "popen(") && (strstr(code, "strcat") || strstr(code, "sprintf"))) {
        snprintf(desc, desc_size,
                 "popen() with string concatenation - shell injection risk");
        return 1;
    }

    /* Backtick or $() in strings */
    if (strstr(code, "`") || strstr(code, "$(")) {
        const char *sys = strstr(code, "system");
        const char *exec = strstr(code, "exec");
        if (sys || exec) {
            snprintf(desc, desc_size,
                     "Command substitution pattern detected near system/exec call");
            return 1;
        }
    }

    (void)size;
    return 0;
}

/* Detect path traversal */
static int detect_path_traversal(const char *code, size_t size, char *desc, size_t desc_size) {
    if (strstr(code, "../") || strstr(code, "..\\\\")) {
        snprintf(desc, desc_size, "Path traversal pattern '../' detected");
        return 1;
    }

    /* Double-encoded traversal */
    if (strstr(code, "%2e%2e") || strstr(code, "%2E%2E") ||
        strstr(code, "..%2f") || strstr(code, "..%5c")) {
        snprintf(desc, desc_size, "Encoded path traversal pattern detected");
        return 1;
    }

    (void)size;
    return 0;
}

/* Detect resource exhaustion */
static int detect_resource_exhaust(const char *code, size_t size, char *desc, size_t desc_size) {
    /* Very large allocations */
    if (strstr(code, "1000000000") || strstr(code, "1073741824") ||
        strstr(code, "0x40000000") || strstr(code, "0x80000000")) {
        if (strstr(code, "malloc") || strstr(code, "mmap") || strstr(code, "new")) {
            snprintf(desc, desc_size,
                     "Very large memory allocation (>1GB) detected");
            return 1;
        }
    }

    /* File descriptor exhaustion */
    if (strstr(code, "open(") || strstr(code, "fopen(")) {
        if (in_loop_context(code, strstr(code, "open") ? strstr(code, "open") - code : 0)) {
            if (!strstr(code, "close") && !strstr(code, "fclose")) {
                snprintf(desc, desc_size,
                         "File open in loop without close - FD exhaustion risk");
                return 1;
            }
        }
    }

    (void)size;
    return 0;
}

const char *governor_behavior_to_string(governor_behavior_t behavior) {
    switch (behavior) {
        case BEHAVIOR_NONE:           return "none";
        case BEHAVIOR_INFINITE_LOOP:  return "infinite_loop";
        case BEHAVIOR_MEMORY_BOMB:    return "memory_bomb";
        case BEHAVIOR_FORK_BOMB:      return "fork_bomb";
        case BEHAVIOR_OBFUSCATION:    return "obfuscation";
        case BEHAVIOR_TIMING_ATTACK:  return "timing_attack";
        case BEHAVIOR_LOOP_DESTRUCTION: return "loop_destruction";
        case BEHAVIOR_ENCODED_PAYLOAD: return "encoded_payload";
        case BEHAVIOR_SHELL_INJECTION: return "shell_injection";
        case BEHAVIOR_PATH_TRAVERSAL: return "path_traversal";
        case BEHAVIOR_RESOURCE_EXHAUST: return "resource_exhaust";
        default: return "unknown";
    }
}

int governor_analyze_behavior(const char *code, size_t size,
                              governor_behavior_result_t *result) {
    if (!code || size == 0 || !result) return -1;

    memset(result, 0, sizeof(governor_behavior_result_t));

    char desc[256];

    /* Run all behavioral checks */
    if (detect_infinite_loop(code, size, desc, sizeof(desc))) {
        result->flags |= BEHAVIOR_INFINITE_LOOP;
        if (result->description_count < 4) {
            strncpy(result->descriptions[result->description_count++], desc, 255);
        }
        result->suspicious_score += 15;
    }

    if (detect_memory_bomb(code, size, desc, sizeof(desc))) {
        result->flags |= BEHAVIOR_MEMORY_BOMB;
        if (result->description_count < 4) {
            strncpy(result->descriptions[result->description_count++], desc, 255);
        }
        result->suspicious_score += 25;
    }

    if (detect_fork_bomb(code, size, desc, sizeof(desc))) {
        result->flags |= BEHAVIOR_FORK_BOMB;
        if (result->description_count < 4) {
            strncpy(result->descriptions[result->description_count++], desc, 255);
        }
        result->suspicious_score += 40;
    }

    if (detect_obfuscation(code, size, desc, sizeof(desc))) {
        result->flags |= BEHAVIOR_OBFUSCATION;
        if (result->description_count < 4) {
            strncpy(result->descriptions[result->description_count++], desc, 255);
        }
        result->suspicious_score += 20;
    }

    if (detect_encoded_payload(code, size, desc, sizeof(desc))) {
        result->flags |= BEHAVIOR_ENCODED_PAYLOAD;
        if (result->description_count < 4) {
            strncpy(result->descriptions[result->description_count++], desc, 255);
        }
        result->suspicious_score += 25;
    }

    if (detect_shell_injection(code, size, desc, sizeof(desc))) {
        result->flags |= BEHAVIOR_SHELL_INJECTION;
        if (result->description_count < 4) {
            strncpy(result->descriptions[result->description_count++], desc, 255);
        }
        result->suspicious_score += 35;
    }

    if (detect_path_traversal(code, size, desc, sizeof(desc))) {
        result->flags |= BEHAVIOR_PATH_TRAVERSAL;
        if (result->description_count < 4) {
            strncpy(result->descriptions[result->description_count++], desc, 255);
        }
        result->suspicious_score += 20;
    }

    if (detect_resource_exhaust(code, size, desc, sizeof(desc))) {
        result->flags |= BEHAVIOR_RESOURCE_EXHAUST;
        if (result->description_count < 4) {
            strncpy(result->descriptions[result->description_count++], desc, 255);
        }
        result->suspicious_score += 25;
    }

    /* Check for destructive patterns in loops */
    const char *destructive[] = {"unlink", "remove", "rmdir", "truncate", NULL};
    for (int i = 0; destructive[i]; i++) {
        const char *found = strstr(code, destructive[i]);
        if (found && in_loop_context(code, found - code)) {
            result->flags |= BEHAVIOR_LOOP_DESTRUCTION;
            snprintf(desc, sizeof(desc), "%s() in loop - mass destruction pattern",
                     destructive[i]);
            if (result->description_count < 4) {
                strncpy(result->descriptions[result->description_count++], desc, 255);
            }
            result->suspicious_score += 50;
            break;
        }
    }

    /* Cap the score at 100 */
    if (result->suspicious_score > 100) {
        result->suspicious_score = 100;
    }

    return 0;
}
