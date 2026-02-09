/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                            PHANTOM KERNEL
 *                     "To Create, Not To Destroy"
 *
 *    Implementation of the Phantom microkernel.
 *
 *    Build: gcc -Wall -O2 -I.. phantom.c governor.c -o phantom -lpthread
 *
 * ══════════════════════════════════════════════════════════════════════════════
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "phantom.h"
#include "vfs.h"
#include "shell.h"
#include "init.h"
#include "governor.h"
#include "phantom_user.h"
#include "phantom_dnauth.h"
#include "../geofs.h"

/* External filesystem types */
extern struct vfs_fs_type procfs_fs_type;
extern struct vfs_fs_type devfs_fs_type;
extern struct vfs_fs_type geofs_vfs_type;
extern void procfs_set_kernel(struct vfs_superblock *sb, struct phantom_kernel *kernel,
                              struct vfs_context *vfs);
extern vfs_error_t geofs_vfs_mount_volume(struct vfs_context *ctx,
                                           geofs_volume_t *volume,
                                           const char *mount_path);

/* ══════════════════════════════════════════════════════════════════════════════
 * SHA-256 (same implementation as GeoFS for consistency)
 * ══════════════════════════════════════════════════════════════════════════════ */

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

static void phantom_sha256(const void *data, size_t len, uint8_t hash[32]) {
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

/* ══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static phantom_time_t phantom_time_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void hash_to_string(const phantom_hash_t hash, char *buf) {
    for (int i = 0; i < PHANTOM_HASH_SIZE; i++) {
        sprintf(buf + (i * 2), "%02x", hash[i]);
    }
    buf[64] = '\0';
}

static const char *phantom_strerror(phantom_error_t err) {
    switch (err) {
        case PHANTOM_OK:          return "Success";
        case PHANTOM_ERR_NOMEM:   return "Out of memory";
        case PHANTOM_ERR_NOTFOUND: return "Not found";
        case PHANTOM_ERR_INVALID: return "Invalid argument";
        case PHANTOM_ERR_DENIED:  return "Governor declined execution";
        case PHANTOM_ERR_UNSIGNED: return "Code not signed by Governor";
        case PHANTOM_ERR_CORRUPT: return "Data corruption";
        case PHANTOM_ERR_FULL:    return "Storage full";
        case PHANTOM_ERR_IO:      return "I/O error";
        default:                  return "Unknown error";
    }
}

static const char *process_state_string(process_state_t state) {
    switch (state) {
        case PROCESS_EMBRYO:   return "embryo";
        case PROCESS_READY:    return "ready";
        case PROCESS_RUNNING:  return "running";
        case PROCESS_BLOCKED:  return "blocked";
        case PROCESS_DORMANT:  return "dormant";
        default:               return "unknown";
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * KERNEL INITIALIZATION
 * ══════════════════════════════════════════════════════════════════════════════ */

phantom_error_t phantom_init(struct phantom_kernel *kernel, const char *geofs_path) {
    if (!kernel) return PHANTOM_ERR_INVALID;

    memset(kernel, 0, sizeof(struct phantom_kernel));

    kernel->magic = PHANTOM_MAGIC;
    kernel->version = PHANTOM_VERSION;
    kernel->boot_time = phantom_time_now();
    kernel->next_pid = 1;
    kernel->governor_enabled = 1;

    /* Open or create GeoFS volume */
    geofs_volume_t *vol = NULL;
    geofs_error_t err = geofs_volume_open(geofs_path, &vol);
    if (err == GEOFS_ERR_IO) {
        /* Volume doesn't exist - create it with 100MB */
        err = geofs_volume_create(geofs_path, 100, &vol);
        if (err != GEOFS_OK) {
            fprintf(stderr, "Failed to create GeoFS volume: %s\n", geofs_strerror(err));
            return PHANTOM_ERR_IO;
        }
        printf("  Created new GeoFS volume: %s (100 MB)\n", geofs_path);
    } else if (err != GEOFS_OK) {
        fprintf(stderr, "Failed to open GeoFS volume: %s\n", geofs_strerror(err));
        return PHANTOM_ERR_IO;
    } else {
        printf("  Opened GeoFS volume: %s\n", geofs_path);
    }
    kernel->geofs_volume = vol;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║              PHANTOM KERNEL INITIALIZED               ║\n");
    printf("║                                                       ║\n");
    printf("║  The Prime Directive is active.                       ║\n");
    printf("║  All code must be Governor-approved.                  ║\n");
    printf("║  Destruction is architecturally impossible.           ║\n");
    printf("║                                                       ║\n");
    printf("║              \"To Create, Not To Destroy\"              ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    return PHANTOM_OK;
}

void phantom_shutdown(struct phantom_kernel *kernel) {
    if (!kernel) return;

    /* Save all processes before shutdown */
    phantom_process_save_all(kernel);

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║              PHANTOM KERNEL SHUTDOWN                  ║\n");
    printf("║                                                       ║\n");
    printf("║  All processes suspended (not destroyed).             ║\n");
    printf("║  All data preserved in geology.                       ║\n");
    printf("║  Nothing was lost. Nothing was forgotten.             ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Statistics (permanent record):\n");
    printf("    Total processes ever:    %lu\n", kernel->total_processes_ever);
    printf("    Total syscalls:          %lu\n", kernel->total_syscalls);
    printf("    Total bytes created:     %lu\n", kernel->total_bytes_created);
    printf("    Total messages sent:     %lu\n", kernel->total_messages_sent);
    printf("    Context switches:        %lu\n", kernel->context_switches);
    printf("    Code evaluated:          %lu\n", kernel->total_code_evaluated);
    printf("    Code approved:           %lu\n", kernel->total_code_approved);
    printf("    Code declined:           %lu\n", kernel->total_code_declined);
    printf("\n");

    /* Suspend all processes and free memory */
    struct phantom_process *proc = kernel->processes;
    while (proc) {
        struct phantom_process *next = proc->next;

        if (proc->state != PROCESS_DORMANT) {
            proc->state = PROCESS_DORMANT;
            proc->state_changed = phantom_time_now();
        }

        /* Free process memory to prevent leaks */
        if (proc->regions) {
            /* Free each region's allocated memory */
            for (uint32_t i = 0; i < proc->num_regions; i++) {
                if (proc->regions[i].data) {
                    free(proc->regions[i].data);
                    proc->regions[i].data = NULL;
                }
            }
            free(proc->regions);
            proc->regions = NULL;
        }
        /* Note: memory_base points to same memory as regions[0].data, already freed above */
        proc->memory_base = NULL;
        free(proc);

        proc = next;
    }
    kernel->processes = NULL;

    /* Close GeoFS volume (data is preserved in geology) */
    if (kernel->geofs_volume) {
        geofs_volume_close((geofs_volume_t *)kernel->geofs_volume);
        kernel->geofs_volume = NULL;
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * GOVERNOR (Wrapper to new capability-based Governor)
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Per Article III: "The Governor's values are architectural, not configurable"
 *
 * The enhanced Governor implementation is in governor.c. These wrapper functions
 * provide the legacy API for compatibility with existing code.
 */

phantom_error_t governor_evaluate(struct phantom_kernel *kernel,
                                   struct governor_request *request,
                                   struct governor_response *response) {
    if (!kernel || !request || !response) return PHANTOM_ERR_INVALID;

    kernel->total_code_evaluated++;
    memset(response, 0, sizeof(struct governor_response));

    /* Use enhanced Governor if available */
    if (kernel->governor && kernel->governor_enabled) {
        governor_eval_request_t gov_req = {0};
        governor_eval_response_t gov_resp = {0};

        /* Copy request data */
        gov_req.code_ptr = request->code_ptr;
        gov_req.code_size = request->code_size;
        memcpy(gov_req.creator_id, request->creator_id, PHANTOM_HASH_SIZE);
        strncpy(gov_req.description, request->description, 1023);

        /* Evaluate */
        int err = governor_evaluate_code(kernel->governor, &gov_req, &gov_resp);
        if (err != 0) {
            return PHANTOM_ERR_IO;
        }

        /* Copy response */
        response->decision = gov_resp.decision;
        strncpy(response->reasoning, gov_resp.reasoning, 1023);
        strncpy(response->alternatives, gov_resp.alternatives, 1023);
        memcpy(response->signature, gov_resp.signature, PHANTOM_SIGNATURE_SIZE);

        /* Update statistics */
        if (response->decision == GOVERNOR_APPROVE) {
            kernel->total_code_approved++;
        } else {
            kernel->total_code_declined++;
        }

        /* Log the decision */
        governor_log_decision(kernel->governor, &gov_req, &gov_resp);

        return PHANTOM_OK;
    }

    /* Fallback: simple pattern-based check if Governor not initialized */
    phantom_sha256(request->code_ptr, request->code_size, (uint8_t*)request->code_hash);

    const char *code = (const char *)request->code_ptr;
    int has_destructive = 0;
    if (strstr(code, "unlink") || strstr(code, "remove") ||
        strstr(code, "truncate") || strstr(code, "delete") ||
        strstr(code, "kill(") || strstr(code, "abort")) {
        has_destructive = 1;
    }

    if (has_destructive) {
        response->decision = GOVERNOR_DECLINE;
        snprintf(response->reasoning, sizeof(response->reasoning),
                 "Code contains destructive operations which are architecturally "
                 "impossible in Phantom.");
        snprintf(response->alternatives, sizeof(response->alternatives),
                 "Use phantom_syscall_hide() instead of deletion operations.");
        kernel->total_code_declined++;
        return PHANTOM_OK;
    }

    response->decision = GOVERNOR_APPROVE;
    snprintf(response->reasoning, sizeof(response->reasoning),
             "Code analysis complete. No destructive operations detected.");
    phantom_sha256(request->code_hash, PHANTOM_HASH_SIZE, response->signature);
    kernel->total_code_approved++;

    return PHANTOM_OK;
}

int governor_verify_signature(struct phantom_kernel *kernel,
                               const phantom_hash_t code_hash,
                               const phantom_signature_t signature) {
    if (!kernel) return 0;

    /* Use enhanced Governor verification if available */
    if (kernel->governor) {
        return governor_verify_code(kernel->governor, code_hash, signature);
    }

    /* Fallback verification */
    phantom_signature_t expected;
    phantom_sha256(code_hash, PHANTOM_HASH_SIZE, expected);
    return memcmp(signature, expected, PHANTOM_SIGNATURE_SIZE) == 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PROCESS MANAGEMENT
 * ══════════════════════════════════════════════════════════════════════════════ */

phantom_error_t phantom_process_create(struct phantom_kernel *kernel,
                                        const void *code, size_t code_size,
                                        const char *name,
                                        phantom_pid_t *pid_out) {
    if (!kernel || !code || !name || !pid_out) return PHANTOM_ERR_INVALID;

    /* Step 1: Submit code to Governor for evaluation */
    struct governor_request req = {0};
    struct governor_response resp = {0};

    req.code_ptr = (void *)code;
    req.code_size = code_size;
    snprintf(req.description, sizeof(req.description), "Process: %s", name);

    phantom_error_t err = governor_evaluate(kernel, &req, &resp);
    if (err != PHANTOM_OK) return err;

    /* Step 2: Check Governor's decision */
    if (resp.decision != GOVERNOR_APPROVE) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════╗\n");
        printf("║              GOVERNOR DECLINED EXECUTION              ║\n");
        printf("╚═══════════════════════════════════════════════════════╝\n");
        printf("\n  Reasoning: %s\n", resp.reasoning);
        if (resp.alternatives[0]) {
            printf("  Alternatives: %s\n", resp.alternatives);
        }
        printf("\n");
        return PHANTOM_ERR_DENIED;
    }

    /* Step 3: Create process (Governor approved) */
    struct phantom_process *proc = calloc(1, sizeof(struct phantom_process));
    if (!proc) return PHANTOM_ERR_NOMEM;

    proc->pid = kernel->next_pid++;
    proc->parent_pid = 0;  /* Init process */
    proc->state = PROCESS_EMBRYO;
    proc->created = phantom_time_now();
    proc->state_changed = proc->created;
    proc->is_verified = 1;

    /* Copy signature */
    memcpy(proc->signature.governor_sig, resp.signature, PHANTOM_SIGNATURE_SIZE);
    proc->signature.signed_at = phantom_time_now();
    strncpy(proc->signature.reason, resp.reasoning, 255);
    phantom_sha256(code, code_size, proc->code_hash);

    strncpy(proc->name, name, 255);

    /* Initialize scheduling */
    proc->priority = PHANTOM_PRIORITY_DEFAULT;
    proc->time_slice_ns = PHANTOM_TIME_SLICE_NS;

    /* Add to process list (append-only) */
    proc->next = kernel->processes;
    kernel->processes = proc;

    kernel->total_processes_ever++;
    kernel->active_processes++;

    /* Transition to ready */
    proc->state = PROCESS_READY;
    proc->state_changed = phantom_time_now();

    *pid_out = proc->pid;

    printf("  Process created: %s (PID %lu)\n", name, proc->pid);
    printf("  Governor: %s\n", resp.reasoning);

    return PHANTOM_OK;
}

phantom_error_t phantom_process_suspend(struct phantom_kernel *kernel, phantom_pid_t pid) {
    if (!kernel) return PHANTOM_ERR_INVALID;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;

    if (proc->state == PROCESS_DORMANT) {
        return PHANTOM_OK;  /* Already suspended */
    }

    /* Suspend, don't kill - the process remains in the geology */
    proc->state = PROCESS_DORMANT;
    proc->state_changed = phantom_time_now();
    kernel->active_processes--;

    printf("  Process suspended: %s (PID %lu)\n", proc->name, pid);
    printf("  Note: Process data preserved in geology. Nothing was destroyed.\n");

    return PHANTOM_OK;
}

phantom_error_t phantom_process_resume(struct phantom_kernel *kernel, phantom_pid_t pid) {
    if (!kernel) return PHANTOM_ERR_INVALID;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;

    if (proc->state != PROCESS_DORMANT) {
        return PHANTOM_OK;  /* Already active */
    }

    /* Verify signature is still valid */
    if (!governor_verify_signature(kernel, proc->code_hash, proc->signature.governor_sig)) {
        return PHANTOM_ERR_UNSIGNED;
    }

    proc->state = PROCESS_READY;
    proc->state_changed = phantom_time_now();
    kernel->active_processes++;

    printf("  Process resumed: %s (PID %lu)\n", proc->name, pid);

    return PHANTOM_OK;
}

struct phantom_process *phantom_process_find(struct phantom_kernel *kernel, phantom_pid_t pid) {
    if (!kernel) return NULL;

    struct phantom_process *proc = kernel->processes;
    while (proc) {
        if (proc->pid == pid) return proc;
        proc = proc->next;
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * SYSCALL INTERFACE
 * ══════════════════════════════════════════════════════════════════════════════ */

phantom_error_t phantom_syscall_write(struct phantom_kernel *kernel,
                                       phantom_pid_t pid,
                                       const char *path,
                                       const void *data, size_t size) {
    if (!kernel || !path || !data) return PHANTOM_ERR_INVALID;
    if (!kernel->geofs_volume) return PHANTOM_ERR_IO;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;

    kernel->total_syscalls++;

    /* Store content in GeoFS (append-only, content-addressed) */
    geofs_volume_t *vol = (geofs_volume_t *)kernel->geofs_volume;
    geofs_hash_t hash;
    geofs_error_t err = geofs_content_store(vol, data, size, hash);
    if (err != GEOFS_OK) {
        printf("  [syscall] write FAILED: %s\n", geofs_strerror(err));
        return PHANTOM_ERR_IO;
    }

    /* Create reference (path -> content) */
    err = geofs_ref_create(vol, path, hash);
    if (err != GEOFS_OK) {
        printf("  [syscall] write ref FAILED: %s\n", geofs_strerror(err));
        return PHANTOM_ERR_IO;
    }

    kernel->total_bytes_created += size;

    char hash_str[65];
    geofs_hash_to_string(hash, hash_str);
    printf("  [syscall] write: %s (%zu bytes) by PID %lu -> %.16s...\n",
           path, size, pid, hash_str);

    return PHANTOM_OK;
}

phantom_error_t phantom_syscall_read(struct phantom_kernel *kernel,
                                      phantom_pid_t pid,
                                      const char *path,
                                      void *buf, size_t size, size_t *read_out) {
    if (!kernel || !path || !buf) return PHANTOM_ERR_INVALID;
    if (!kernel->geofs_volume) return PHANTOM_ERR_IO;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;

    kernel->total_syscalls++;

    /* Resolve path to content hash */
    geofs_volume_t *vol = (geofs_volume_t *)kernel->geofs_volume;
    geofs_hash_t hash;
    geofs_error_t err = geofs_ref_resolve(vol, path, hash);
    if (err != GEOFS_OK) {
        if (read_out) *read_out = 0;
        return PHANTOM_ERR_NOTFOUND;
    }

    /* Read content */
    size_t got = 0;
    err = geofs_content_read(vol, hash, buf, size, &got);
    if (err != GEOFS_OK) {
        printf("  [syscall] read FAILED: %s\n", geofs_strerror(err));
        if (read_out) *read_out = 0;
        return PHANTOM_ERR_IO;
    }

    if (read_out) *read_out = got;
    printf("  [syscall] read: %s (%zu bytes) by PID %lu\n", path, got, pid);

    return PHANTOM_OK;
}

phantom_error_t phantom_syscall_hide(struct phantom_kernel *kernel,
                                      phantom_pid_t pid,
                                      const char *path) {
    if (!kernel || !path) return PHANTOM_ERR_INVALID;
    if (!kernel->geofs_volume) return PHANTOM_ERR_IO;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;

    kernel->total_syscalls++;

    /* Hide via GeoFS (creates new view, preserves content in geology) */
    geofs_volume_t *vol = (geofs_volume_t *)kernel->geofs_volume;
    geofs_error_t err = geofs_view_hide(vol, path);
    if (err != GEOFS_OK) {
        printf("  [syscall] hide FAILED: %s\n", geofs_strerror(err));
        return PHANTOM_ERR_IO;
    }

    printf("  [syscall] hide: %s by PID %lu\n", path, pid);
    printf("  Note: Content preserved in geology, just hidden from current view.\n");
    printf("  Current view: %lu\n", geofs_view_current(vol));

    return PHANTOM_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PROCESS PERSISTENCE
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * Serialized process format for GeoFS storage.
 * This allows processes to survive kernel restarts.
 */
struct phantom_process_serial {
    uint32_t        magic;              /* PROC magic */
    uint32_t        version;
    phantom_pid_t   pid;
    phantom_pid_t   parent_pid;
    process_state_t state;
    phantom_time_t  created;
    phantom_time_t  state_changed;
    phantom_hash_t  code_hash;
    int             is_verified;
    char            name[256];
    uint64_t        instruction_count;
    uint64_t        program_counter;
    size_t          memory_size;
    size_t          memory_high_water;
    uint8_t         priority;
    uint64_t        total_time_ns;
    uint64_t        wakeups;
    struct phantom_code_signature signature;
};

#define PROC_SERIAL_MAGIC   0x434F5250  /* "PROC" */
#define PROC_SERIAL_VERSION 1

phantom_error_t phantom_process_save(struct phantom_kernel *kernel, phantom_pid_t pid) {
    if (!kernel || !kernel->geofs_volume) return PHANTOM_ERR_INVALID;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;

    /* Serialize process state */
    struct phantom_process_serial serial = {0};
    serial.magic = PROC_SERIAL_MAGIC;
    serial.version = PROC_SERIAL_VERSION;
    serial.pid = proc->pid;
    serial.parent_pid = proc->parent_pid;
    serial.state = proc->state;
    serial.created = proc->created;
    serial.state_changed = proc->state_changed;
    memcpy(serial.code_hash, proc->code_hash, PHANTOM_HASH_SIZE);
    serial.is_verified = proc->is_verified;
    strncpy(serial.name, proc->name, 255);
    serial.instruction_count = proc->instruction_count;
    serial.program_counter = proc->program_counter;
    serial.memory_size = proc->memory_size;
    serial.memory_high_water = proc->memory_high_water;
    serial.priority = proc->priority;
    serial.total_time_ns = proc->total_time_ns;
    serial.wakeups = proc->wakeups;
    memcpy(&serial.signature, &proc->signature, sizeof(struct phantom_code_signature));

    /* Store in GeoFS under /system/processes/{pid} */
    char path[128];
    snprintf(path, sizeof(path), "/system/processes/%lu", pid);

    geofs_volume_t *vol = (geofs_volume_t *)kernel->geofs_volume;
    geofs_hash_t hash;
    geofs_error_t err = geofs_content_store(vol, &serial, sizeof(serial), hash);
    if (err != GEOFS_OK) return PHANTOM_ERR_IO;

    err = geofs_ref_create(vol, path, hash);
    if (err != GEOFS_OK) return PHANTOM_ERR_IO;

    printf("  [persist] Saved process %lu (%s) to GeoFS\n", pid, proc->name);
    return PHANTOM_OK;
}

phantom_error_t phantom_process_save_all(struct phantom_kernel *kernel) {
    if (!kernel) return PHANTOM_ERR_INVALID;

    int saved = 0;
    struct phantom_process *proc = kernel->processes;
    while (proc) {
        phantom_error_t err = phantom_process_save(kernel, proc->pid);
        if (err == PHANTOM_OK) saved++;
        proc = proc->next;
    }

    /* Save process table metadata */
    char meta[256];
    snprintf(meta, sizeof(meta),
             "next_pid=%lu\ntotal_ever=%lu\nactive=%lu\n",
             kernel->next_pid,
             kernel->total_processes_ever,
             kernel->active_processes);

    geofs_volume_t *vol = (geofs_volume_t *)kernel->geofs_volume;
    geofs_hash_t hash;
    geofs_error_t err = geofs_content_store(vol, meta, strlen(meta), hash);
    if (err == GEOFS_OK) {
        geofs_ref_create(vol, "/system/processes/_meta", hash);
    }

    printf("  [persist] Saved %d processes to GeoFS\n", saved);
    return PHANTOM_OK;
}

phantom_error_t phantom_process_restore_all(struct phantom_kernel *kernel) {
    if (!kernel || !kernel->geofs_volume) return PHANTOM_ERR_INVALID;

    geofs_volume_t *vol = (geofs_volume_t *)kernel->geofs_volume;

    /* Try to read metadata first */
    geofs_hash_t meta_hash;
    if (geofs_ref_resolve(vol, "/system/processes/_meta", meta_hash) == GEOFS_OK) {
        char meta[256] = {0};
        size_t got;
        if (geofs_content_read(vol, meta_hash, meta, sizeof(meta) - 1, &got) == GEOFS_OK) {
            /* Parse metadata */
            sscanf(meta, "next_pid=%lu\ntotal_ever=%lu\nactive=%lu",
                   &kernel->next_pid,
                   &kernel->total_processes_ever,
                   &kernel->active_processes);
        }
    }

    /* Restore processes - scan /system/processes/ */
    int restored = 0;
    for (phantom_pid_t pid = 1; pid < kernel->next_pid; pid++) {
        char path[128];
        snprintf(path, sizeof(path), "/system/processes/%lu", pid);

        geofs_hash_t hash;
        if (geofs_ref_resolve(vol, path, hash) != GEOFS_OK) continue;

        struct phantom_process_serial serial;
        size_t got;
        if (geofs_content_read(vol, hash, &serial, sizeof(serial), &got) != GEOFS_OK) continue;
        if (serial.magic != PROC_SERIAL_MAGIC) continue;

        /* Recreate process structure */
        struct phantom_process *proc = calloc(1, sizeof(struct phantom_process));
        if (!proc) continue;

        proc->pid = serial.pid;
        proc->parent_pid = serial.parent_pid;
        proc->state = serial.state;
        proc->created = serial.created;
        proc->state_changed = serial.state_changed;
        memcpy(proc->code_hash, serial.code_hash, PHANTOM_HASH_SIZE);
        proc->is_verified = serial.is_verified;
        strncpy(proc->name, serial.name, 255);
        proc->instruction_count = serial.instruction_count;
        proc->program_counter = serial.program_counter;
        proc->memory_size = serial.memory_size;
        proc->memory_high_water = serial.memory_high_water;
        proc->priority = serial.priority;
        proc->total_time_ns = serial.total_time_ns;
        proc->wakeups = serial.wakeups;
        proc->time_slice_ns = PHANTOM_TIME_SLICE_NS;
        memcpy(&proc->signature, &serial.signature, sizeof(struct phantom_code_signature));

        /* Add to process list */
        proc->next = kernel->processes;
        kernel->processes = proc;
        restored++;

        printf("  [persist] Restored process %lu (%s) - state: %s\n",
               proc->pid, proc->name, process_state_string(proc->state));
    }

    if (restored > 0) {
        printf("  [persist] Restored %d processes from GeoFS\n", restored);
    }
    return PHANTOM_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * INTER-PROCESS COMMUNICATION (IPC)
 * ══════════════════════════════════════════════════════════════════════════════ */

phantom_error_t phantom_ipc_send(struct phantom_kernel *kernel,
                                  phantom_pid_t sender,
                                  phantom_pid_t receiver,
                                  uint32_t msg_type,
                                  const void *data, size_t size) {
    if (!kernel || !kernel->geofs_volume) return PHANTOM_ERR_INVALID;
    if (size > PHANTOM_MSG_MAX_SIZE) return PHANTOM_ERR_INVALID;

    struct phantom_process *sender_proc = phantom_process_find(kernel, sender);
    struct phantom_process *recv_proc = phantom_process_find(kernel, receiver);
    if (!sender_proc) return PHANTOM_ERR_NOTFOUND;
    if (!recv_proc) return PHANTOM_ERR_NOTFOUND;

    /* Build message */
    struct phantom_message msg = {0};
    msg.sender = sender;
    msg.receiver = receiver;
    msg.sent_at = phantom_time_now();
    msg.msg_type = msg_type;
    msg.data_size = size;
    if (data && size > 0) {
        memcpy(msg.data, data, size);
    }

    /* Hash the message for verification */
    phantom_sha256(&msg, sizeof(msg) - PHANTOM_HASH_SIZE, msg.msg_hash);

    /* Store message in GeoFS under /system/ipc/{receiver}/{msg_id} */
    uint32_t msg_id = recv_proc->mailbox_head + recv_proc->mailbox_count;
    char path[128];
    snprintf(path, sizeof(path), "/system/ipc/%lu/%u", receiver, msg_id);

    geofs_volume_t *vol = (geofs_volume_t *)kernel->geofs_volume;
    geofs_hash_t hash;
    geofs_error_t err = geofs_content_store(vol, &msg, sizeof(msg), hash);
    if (err != GEOFS_OK) return PHANTOM_ERR_IO;

    err = geofs_ref_create(vol, path, hash);
    if (err != GEOFS_OK) return PHANTOM_ERR_IO;

    /* Update receiver's mailbox count */
    recv_proc->mailbox_count++;
    kernel->total_messages_sent++;

    /* Wake up receiver if blocked on IPC */
    if (recv_proc->state == PROCESS_BLOCKED) {
        recv_proc->state = PROCESS_READY;
        recv_proc->state_changed = phantom_time_now();
    }

    printf("  [ipc] Message from PID %lu to PID %lu (type %u, %zu bytes)\n",
           sender, receiver, msg_type, size);

    return PHANTOM_OK;
}

phantom_error_t phantom_ipc_receive(struct phantom_kernel *kernel,
                                     phantom_pid_t pid,
                                     struct phantom_message *msg_out,
                                     int flags) {
    if (!kernel || !kernel->geofs_volume || !msg_out) return PHANTOM_ERR_INVALID;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;

    /* Check if mailbox is empty */
    if (proc->mailbox_count == 0) {
        if (flags & PHANTOM_IPC_NOWAIT) {
            return PHANTOM_ERR_NOTFOUND;
        }
        /* Block waiting for message */
        proc->state = PROCESS_BLOCKED;
        proc->state_changed = phantom_time_now();
        return PHANTOM_ERR_NOTFOUND;  /* Caller should retry after yield */
    }

    /* Read message from GeoFS */
    char path[128];
    snprintf(path, sizeof(path), "/system/ipc/%lu/%u", pid, proc->mailbox_head);

    geofs_volume_t *vol = (geofs_volume_t *)kernel->geofs_volume;
    geofs_hash_t hash;
    if (geofs_ref_resolve(vol, path, hash) != GEOFS_OK) {
        return PHANTOM_ERR_NOTFOUND;
    }

    size_t got;
    if (geofs_content_read(vol, hash, msg_out, sizeof(struct phantom_message), &got) != GEOFS_OK) {
        return PHANTOM_ERR_IO;
    }

    /* Verify message hash */
    phantom_hash_t verify_hash;
    phantom_sha256(msg_out, sizeof(struct phantom_message) - PHANTOM_HASH_SIZE, verify_hash);
    if (memcmp(verify_hash, msg_out->msg_hash, PHANTOM_HASH_SIZE) != 0) {
        return PHANTOM_ERR_CORRUPT;
    }

    /* If not peeking, advance the mailbox head */
    if (!(flags & PHANTOM_IPC_PEEK)) {
        proc->mailbox_head++;
        proc->mailbox_count--;
        /* Note: we don't delete the message - it stays in geology */
    }

    printf("  [ipc] PID %lu received message from PID %lu\n", pid, msg_out->sender);
    return PHANTOM_OK;
}

int phantom_ipc_pending(struct phantom_kernel *kernel, phantom_pid_t pid) {
    if (!kernel) return 0;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return 0;

    return proc->mailbox_count;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * MEMORY MANAGEMENT
 * ══════════════════════════════════════════════════════════════════════════════ */

phantom_error_t phantom_mem_alloc(struct phantom_kernel *kernel,
                                   phantom_pid_t pid,
                                   size_t size,
                                   uint32_t flags,
                                   uint64_t *addr_out) {
    if (!kernel || !addr_out) return PHANTOM_ERR_INVALID;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;

    /* Round up to page size */
    size = (size + PHANTOM_PAGE_SIZE - 1) & ~(PHANTOM_PAGE_SIZE - 1);

    /* Allocate region tracking structure */
    if (!proc->regions) {
        proc->regions = calloc(PHANTOM_MAX_REGIONS, sizeof(struct phantom_memory_region));
        if (!proc->regions) return PHANTOM_ERR_NOMEM;
    }

    if (proc->num_regions >= PHANTOM_MAX_REGIONS) return PHANTOM_ERR_FULL;

    /* Allocate actual memory */
    void *mem = calloc(1, size);
    if (!mem) return PHANTOM_ERR_NOMEM;

    /* Calculate base address (simple linear allocation) */
    uint64_t base = 0x10000000 + (proc->memory_size);  /* Start at 256MB */

    /* Record the region */
    struct phantom_memory_region *region = &proc->regions[proc->num_regions];
    region->base_addr = base;
    region->size = size;
    region->flags = flags;
    region->created = phantom_time_now();
    region->dirty = 1;
    region->data = mem;  /* Store pointer for cleanup */
    memset(region->content_hash, 0, GEOFS_HASH_SIZE);

    proc->num_regions++;
    proc->memory_size += size;
    if (proc->memory_size > proc->memory_high_water) {
        proc->memory_high_water = proc->memory_size;
    }

    /* Store in process context (simplified - real impl would use page tables) */
    if (!proc->memory_base) {
        proc->memory_base = mem;
    }

    *addr_out = base;

    printf("  [mem] Allocated %zu bytes for PID %lu at 0x%lx (flags=0x%x)\n",
           size, pid, base, flags);

    return PHANTOM_OK;
}

phantom_error_t phantom_mem_snapshot(struct phantom_kernel *kernel,
                                      phantom_pid_t pid) {
    if (!kernel || !kernel->geofs_volume) return PHANTOM_ERR_INVALID;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;
    if (!proc->memory_base || proc->memory_size == 0) return PHANTOM_OK;

    geofs_volume_t *vol = (geofs_volume_t *)kernel->geofs_volume;

    /* Snapshot each memory region */
    for (uint32_t i = 0; i < proc->num_regions; i++) {
        struct phantom_memory_region *region = &proc->regions[i];
        if (!region->dirty) continue;

        /* Store region contents in GeoFS */
        geofs_hash_t hash;
        /* Note: in real impl, we'd compute region data from page tables */
        /* For now, we store a placeholder */
        char placeholder[64];
        snprintf(placeholder, sizeof(placeholder), "region_%u_size_%zu", i, region->size);
        geofs_error_t err = geofs_content_store(vol, placeholder, strlen(placeholder), hash);
        if (err == GEOFS_OK) {
            memcpy(region->content_hash, hash, GEOFS_HASH_SIZE);
            region->last_snapshot = phantom_time_now();
            region->dirty = 0;
        }
    }

    /* Store snapshot reference */
    char path[128];
    snprintf(path, sizeof(path), "/system/memory/%lu/snapshot_%lu",
             pid, phantom_time_now());

    /* Store region table */
    if (proc->regions && proc->num_regions > 0) {
        geofs_hash_t hash;
        size_t regions_size = proc->num_regions * sizeof(struct phantom_memory_region);
        geofs_error_t err = geofs_content_store(vol, proc->regions, regions_size, hash);
        if (err == GEOFS_OK) {
            geofs_ref_create(vol, path, hash);
        }
    }

    printf("  [mem] Snapshot saved for PID %lu (%u regions)\n", pid, proc->num_regions);
    return PHANTOM_OK;
}

phantom_error_t phantom_mem_restore(struct phantom_kernel *kernel,
                                     phantom_pid_t pid,
                                     phantom_hash_t snapshot_hash) {
    if (!kernel || !kernel->geofs_volume) return PHANTOM_ERR_INVALID;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;

    geofs_volume_t *vol = (geofs_volume_t *)kernel->geofs_volume;

    /* Read region table from snapshot */
    struct phantom_memory_region regions[PHANTOM_MAX_REGIONS];
    size_t got;
    geofs_error_t err = geofs_content_read(vol, snapshot_hash, regions,
                                            sizeof(regions), &got);
    if (err != GEOFS_OK) return PHANTOM_ERR_IO;

    uint32_t num_regions = got / sizeof(struct phantom_memory_region);

    /* Allocate region tracking if needed */
    if (!proc->regions) {
        proc->regions = calloc(PHANTOM_MAX_REGIONS, sizeof(struct phantom_memory_region));
        if (!proc->regions) return PHANTOM_ERR_NOMEM;
    }

    /* Restore regions */
    memcpy(proc->regions, regions, num_regions * sizeof(struct phantom_memory_region));
    proc->num_regions = num_regions;

    printf("  [mem] Restored %u memory regions for PID %lu from snapshot\n",
           num_regions, pid);
    return PHANTOM_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * SCHEDULER
 * ══════════════════════════════════════════════════════════════════════════════ */

phantom_error_t phantom_sched_init(struct phantom_kernel *kernel,
                                    phantom_sched_type_t type) {
    if (!kernel) return PHANTOM_ERR_INVALID;

    kernel->sched_type = type;
    kernel->current_process = NULL;
    kernel->run_queue = NULL;
    kernel->context_switches = 0;

    const char *type_name = "unknown";
    switch (type) {
        case PHANTOM_SCHED_ROUND_ROBIN: type_name = "round-robin"; break;
        case PHANTOM_SCHED_PRIORITY:    type_name = "priority"; break;
        case PHANTOM_SCHED_FAIR:        type_name = "fair-share"; break;
    }

    printf("  [sched] Initialized %s scheduler\n", type_name);
    return PHANTOM_OK;
}

/* Find highest priority ready process */
static struct phantom_process *sched_find_next(struct phantom_kernel *kernel) {
    struct phantom_process *best = NULL;
    int best_priority = -1;

    struct phantom_process *proc = kernel->processes;
    while (proc) {
        if (proc->state == PROCESS_READY) {
            int effective_priority = proc->priority;

            /* For fair scheduler, boost priority based on wait time */
            if (kernel->sched_type == PHANTOM_SCHED_FAIR) {
                phantom_time_t wait = phantom_time_now() - proc->last_scheduled;
                effective_priority += (wait / 1000000);  /* Boost per ms waiting */
                if (effective_priority > PHANTOM_PRIORITY_MAX) {
                    effective_priority = PHANTOM_PRIORITY_MAX;
                }
            }

            if (effective_priority > best_priority) {
                best_priority = effective_priority;
                best = proc;
            } else if (effective_priority == best_priority && best) {
                /* Round-robin tie-breaker: prefer process that ran less recently */
                if (proc->last_scheduled < best->last_scheduled) {
                    best = proc;
                }
            }
        }
        proc = proc->next;
    }

    return best;
}

phantom_error_t phantom_sched_run(struct phantom_kernel *kernel) {
    if (!kernel) return PHANTOM_ERR_INVALID;

    /* Find next process to run */
    struct phantom_process *next = sched_find_next(kernel);
    if (!next) {
        /* No ready processes - idle */
        kernel->total_idle_ns += PHANTOM_TIME_SLICE_NS;
        return PHANTOM_OK;
    }

    /* Context switch if needed */
    if (kernel->current_process != next) {
        if (kernel->current_process &&
            kernel->current_process->state == PROCESS_RUNNING) {
            kernel->current_process->state = PROCESS_READY;
            kernel->current_process->state_changed = phantom_time_now();
        }

        kernel->current_process = next;
        kernel->context_switches++;

        next->state = PROCESS_RUNNING;
        next->state_changed = phantom_time_now();
        next->wakeups++;
        next->last_scheduled = phantom_time_now();

        printf("  [sched] Switch to PID %lu (%s) priority=%u\n",
               next->pid, next->name, next->priority);
    }

    /* Simulate running for a time slice */
    phantom_time_t start = phantom_time_now();

    /* In a real kernel, we'd switch to the process context here.
     * For simulation, we just update accounting. */
    usleep(1000);  /* Simulate 1ms of execution */

    phantom_time_t elapsed = phantom_time_now() - start;
    next->time_used_ns += elapsed;
    next->total_time_ns += elapsed;

    /* Check if time slice expired */
    if (next->time_used_ns >= next->time_slice_ns) {
        next->time_used_ns = 0;
        next->state = PROCESS_READY;
        next->state_changed = phantom_time_now();
    }

    return PHANTOM_OK;
}

phantom_error_t phantom_sched_yield(struct phantom_kernel *kernel, phantom_pid_t pid) {
    if (!kernel) return PHANTOM_ERR_INVALID;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;

    if (proc->state == PROCESS_RUNNING) {
        proc->state = PROCESS_READY;
        proc->state_changed = phantom_time_now();
        proc->time_used_ns = 0;  /* Reset time slice */

        printf("  [sched] PID %lu yielded\n", pid);
    }

    return PHANTOM_OK;
}

phantom_error_t phantom_sched_set_priority(struct phantom_kernel *kernel,
                                            phantom_pid_t pid,
                                            uint8_t priority) {
    if (!kernel) return PHANTOM_ERR_INVALID;
    if (priority > PHANTOM_PRIORITY_MAX) priority = PHANTOM_PRIORITY_MAX;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;

    uint8_t old_priority = proc->priority;
    proc->priority = priority;

    printf("  [sched] PID %lu priority: %u -> %u\n", pid, old_priority, priority);
    return PHANTOM_OK;
}

phantom_error_t phantom_sched_stats(struct phantom_kernel *kernel,
                                     phantom_pid_t pid,
                                     struct phantom_sched_info *info_out) {
    if (!kernel || !info_out) return PHANTOM_ERR_INVALID;

    struct phantom_process *proc = phantom_process_find(kernel, pid);
    if (!proc) return PHANTOM_ERR_NOTFOUND;

    info_out->priority = proc->priority;
    info_out->time_slice_ns = proc->time_slice_ns;
    info_out->time_used_ns = proc->time_used_ns;
    info_out->total_time_ns = proc->total_time_ns;
    info_out->wait_time_ns = proc->wait_time_ns;
    info_out->wakeups = proc->wakeups;
    info_out->last_scheduled = proc->last_scheduled;

    return PHANTOM_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CLI / DEMO
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Only include CLI/demo functions when not building for GUI */
#ifndef PHANTOM_NO_MAIN

static void demo_good_code(struct phantom_kernel *kernel) {
    printf("\n=== Testing GOOD code (should be approved) ===\n\n");

    const char *good_code =
        "int main() {\n"
        "    phantom_write(\"/hello.txt\", \"Hello, Phantom!\");\n"
        "    return 0;\n"
        "}\n";

    phantom_pid_t pid;
    phantom_error_t err = phantom_process_create(kernel, good_code, strlen(good_code),
                                                  "hello_world", &pid);

    if (err == PHANTOM_OK) {
        printf("\n  SUCCESS: Process created with PID %lu\n", pid);

        /* Simulate syscall */
        phantom_syscall_write(kernel, pid, "/hello.txt", "Hello, Phantom!", 15);

        /* Suspend (not kill!) */
        phantom_process_suspend(kernel, pid);
    } else {
        printf("\n  FAILED: %s\n", phantom_strerror(err));
    }
}

static void demo_core_system(struct phantom_kernel *kernel) {
    printf("\n=== Testing CORE SYSTEM features ===\n\n");

    /* Create multiple processes for scheduler demo */
    const char *proc1_code = "int main() { while(1) compute(); }";
    const char *proc2_code = "int main() { while(1) process_data(); }";
    const char *proc3_code = "int main() { while(1) handle_io(); }";

    phantom_pid_t pid1 = 0, pid2 = 0, pid3 = 0;

    printf("--- Creating processes for scheduler demo ---\n\n");
    phantom_process_create(kernel, proc1_code, strlen(proc1_code), "compute_worker", &pid1);
    phantom_process_create(kernel, proc2_code, strlen(proc2_code), "data_processor", &pid2);
    phantom_process_create(kernel, proc3_code, strlen(proc3_code), "io_handler", &pid3);

    /* Set different priorities (only if processes were created) */
    printf("\n--- Setting priorities ---\n\n");
    if (pid1) phantom_sched_set_priority(kernel, pid1, 10);  /* Low priority */
    if (pid2) phantom_sched_set_priority(kernel, pid2, 20);  /* Medium priority */
    if (pid3) phantom_sched_set_priority(kernel, pid3, 25);  /* High priority (I/O bound) */

    /* Initialize and run scheduler */
    printf("\n--- Running scheduler (5 cycles) ---\n\n");
    phantom_sched_init(kernel, PHANTOM_SCHED_PRIORITY);

    for (int i = 0; i < 5; i++) {
        phantom_sched_run(kernel);
    }

    /* Demo IPC */
    printf("\n--- Testing IPC ---\n\n");
    const char *msg_data = "Hello from compute_worker!";
    phantom_ipc_send(kernel, pid1, pid2, PHANTOM_MSG_DATA, msg_data, strlen(msg_data) + 1);

    /* Check pending messages */
    int pending = phantom_ipc_pending(kernel, pid2);
    printf("  Messages pending for PID %lu: %d\n", pid2, pending);

    /* Receive message */
    struct phantom_message msg;
    if (phantom_ipc_receive(kernel, pid2, &msg, PHANTOM_IPC_NOWAIT) == PHANTOM_OK) {
        printf("  Received: \"%s\"\n", (char*)msg.data);
    }

    /* Demo Memory Management */
    printf("\n--- Testing Memory Management ---\n\n");
    uint64_t mem_addr;
    phantom_mem_alloc(kernel, pid1, 8192, PHANTOM_MEM_READ | PHANTOM_MEM_WRITE, &mem_addr);
    phantom_mem_alloc(kernel, pid1, 4096, PHANTOM_MEM_READ | PHANTOM_MEM_EXEC, &mem_addr);

    /* Snapshot memory */
    phantom_mem_snapshot(kernel, pid1);

    /* Demo Process Persistence */
    printf("\n--- Testing Process Persistence ---\n\n");
    phantom_process_save_all(kernel);

    /* Print scheduler stats */
    printf("\n--- Scheduler Statistics ---\n\n");
    struct phantom_sched_info info;
    phantom_sched_stats(kernel, pid3, &info);
    printf("  PID %lu stats:\n", pid3);
    printf("    Priority:    %u\n", info.priority);
    printf("    Total time:  %lu ns\n", info.total_time_ns);
    printf("    Wakeups:     %lu\n", info.wakeups);

    /* Suspend all demo processes */
    printf("\n--- Suspending demo processes ---\n\n");
    phantom_process_suspend(kernel, pid1);
    phantom_process_suspend(kernel, pid2);
    phantom_process_suspend(kernel, pid3);
}

static void demo_bad_code(struct phantom_kernel *kernel) {
    printf("\n=== Testing BAD code (should be declined) ===\n\n");

    const char *bad_code =
        "int main() {\n"
        "    unlink(\"/important_file.txt\");  // DESTRUCTIVE!\n"
        "    remove(\"/another_file.txt\");    // DESTRUCTIVE!\n"
        "    return 0;\n"
        "}\n";

    phantom_pid_t pid;
    phantom_error_t err = phantom_process_create(kernel, bad_code, strlen(bad_code),
                                                  "malicious_deleter", &pid);

    if (err == PHANTOM_OK) {
        printf("\n  WARNING: This should not have been approved!\n");
    } else if (err == PHANTOM_ERR_DENIED) {
        printf("\n  CORRECT: Governor properly declined destructive code.\n");
    } else {
        printf("\n  ERROR: %s\n", phantom_strerror(err));
    }
}

static void print_process_list(struct phantom_kernel *kernel) {
    printf("\n=== Process Table (includes dormant - nothing is ever deleted) ===\n\n");

    struct phantom_process *proc = kernel->processes;
    int count = 0;
    while (proc) {
        char hash_str[65];
        hash_to_string(proc->code_hash, hash_str);
        printf("  PID %-4lu  %-10s  %-20s  %.16s...\n",
               proc->pid, process_state_string(proc->state),
               proc->name, hash_str);
        proc = proc->next;
        count++;
    }

    if (count == 0) {
        printf("  (no processes)\n");
    }
    printf("\n  Total: %d processes (%lu active, %lu dormant)\n",
           count, kernel->active_processes,
           kernel->total_processes_ever - kernel->active_processes);
}

static void demo_vfs(struct phantom_kernel *kernel) {
    printf("\n=== Testing VIRTUAL FILE SYSTEM ===\n\n");

    /* Initialize VFS */
    struct vfs_context vfs;
    vfs_init(&vfs);

    /* Register filesystems */
    printf("--- Registering filesystems ---\n\n");
    vfs_register_fs(&vfs, &procfs_fs_type);
    vfs_register_fs(&vfs, &devfs_fs_type);

    /* Mount filesystems */
    printf("\n--- Mounting filesystems ---\n\n");
    vfs_mount(&vfs, "procfs", NULL, "/proc", 0);
    vfs_mount(&vfs, "devfs", NULL, "/dev", 0);

    /* Set kernel reference for procfs */
    struct vfs_mount *mount = vfs.mounts;
    while (mount) {
        if (strcmp(mount->mount_path, "/proc") == 0 && mount->sb) {
            procfs_set_kernel(mount->sb, kernel, &vfs);
        }
        mount = mount->next;
    }

    /* Test /dev/null */
    printf("\n--- Testing /dev/null ---\n\n");
    vfs_fd_t fd_null = vfs_open(&vfs, 1, "/dev/null", VFS_O_RDWR, 0);
    if (fd_null >= 0) {
        const char *test_data = "This data goes nowhere";
        ssize_t written = vfs_write(&vfs, fd_null, test_data, strlen(test_data));
        printf("  Wrote %zd bytes to /dev/null (discarded)\n", written);

        char buf[64];
        ssize_t read_bytes = vfs_read(&vfs, fd_null, buf, sizeof(buf));
        printf("  Read %zd bytes from /dev/null (EOF)\n", read_bytes);

        vfs_close(&vfs, fd_null);
    }

    /* Test /dev/zero */
    printf("\n--- Testing /dev/zero ---\n\n");
    vfs_fd_t fd_zero = vfs_open(&vfs, 1, "/dev/zero", VFS_O_RDONLY, 0);
    if (fd_zero >= 0) {
        char buf[16];
        ssize_t read_bytes = vfs_read(&vfs, fd_zero, buf, sizeof(buf));
        printf("  Read %zd bytes from /dev/zero: ", read_bytes);
        int all_zero = 1;
        for (int i = 0; i < (int)read_bytes; i++) {
            if (buf[i] != 0) all_zero = 0;
        }
        printf("%s\n", all_zero ? "(all zeros)" : "(not all zeros!)");
        vfs_close(&vfs, fd_zero);
    }

    /* Test /dev/random */
    printf("\n--- Testing /dev/random ---\n\n");
    vfs_fd_t fd_random = vfs_open(&vfs, 1, "/dev/random", VFS_O_RDONLY, 0);
    if (fd_random >= 0) {
        unsigned char buf[8];
        ssize_t read_bytes = vfs_read(&vfs, fd_random, buf, sizeof(buf));
        printf("  Read %zd bytes from /dev/random: ", read_bytes);
        for (int i = 0; i < (int)read_bytes; i++) {
            printf("%02x", buf[i]);
        }
        printf("\n");
        vfs_close(&vfs, fd_random);
    }

    /* Test /dev/console */
    printf("\n--- Testing /dev/console ---\n\n");
    vfs_fd_t fd_console = vfs_open(&vfs, 1, "/dev/console", VFS_O_WRONLY, 0);
    if (fd_console >= 0) {
        const char *msg = "  Hello from /dev/console!\n";
        vfs_write(&vfs, fd_console, msg, strlen(msg));
        vfs_close(&vfs, fd_console);
    }

    /* Test creating a directory */
    printf("--- Testing mkdir ---\n\n");
    vfs_error_t err = vfs_mkdir(&vfs, 1, "/data", 0755);
    if (err == VFS_OK) {
        printf("  Created /data directory\n");
    }

    err = vfs_mkdir(&vfs, 1, "/data/logs", 0755);
    if (err == VFS_OK) {
        printf("  Created /data/logs directory\n");
    }

    /* Test creating a symlink */
    printf("\n--- Testing symlinks ---\n\n");
    err = vfs_symlink(&vfs, 1, "/data/logs", "/var/log");
    if (err == VFS_OK) {
        printf("  Created symlink /var/log -> /data/logs\n");
    }

    /* Test stat */
    printf("\n--- Testing stat ---\n\n");
    struct vfs_stat stat_buf;
    err = vfs_stat(&vfs, "/dev/null", &stat_buf);
    if (err == VFS_OK) {
        printf("  /dev/null: inode=%lu, type=%d, size=%lu\n",
               stat_buf.ino, stat_buf.type, stat_buf.size);
    }

    /* Test hide (Phantom's version of "delete") */
    printf("\n--- Testing hide (Phantom's delete) ---\n\n");
    vfs_mkdir(&vfs, 1, "/temp", 0755);
    printf("  Created /temp directory\n");
    err = vfs_hide(&vfs, 1, "/temp");
    if (err == VFS_OK) {
        printf("  Hidden /temp (still preserved in geology)\n");
    }

    /* VFS shutdown */
    printf("\n--- VFS Statistics ---\n\n");
    vfs_shutdown(&vfs);
}

static void run_shell(struct phantom_kernel *kernel) {
    /* Initialize VFS */
    struct vfs_context vfs;
    vfs_init(&vfs);

    /* Register filesystems */
    vfs_register_fs(&vfs, &procfs_fs_type);
    vfs_register_fs(&vfs, &devfs_fs_type);
    vfs_register_fs(&vfs, &geofs_vfs_type);

    /* Mount pseudo-filesystems */
    vfs_mount(&vfs, "procfs", NULL, "/proc", 0);
    vfs_mount(&vfs, "devfs", NULL, "/dev", 0);

    /* Mount GeoFS for persistent storage */
    if (kernel->geofs_volume) {
        geofs_vfs_mount_volume(&vfs, kernel->geofs_volume, "/geo");
        printf("  [kernel] Mounted GeoFS at /geo for persistent storage\n");
    }

    /* Set kernel reference for procfs */
    struct vfs_mount *mount = vfs.mounts;
    while (mount) {
        if (strcmp(mount->mount_path, "/proc") == 0 && mount->sb) {
            procfs_set_kernel(mount->sb, kernel, &vfs);
        }
        mount = mount->next;
    }

    /* Create some initial directories (in-memory) */
    vfs_mkdir(&vfs, 1, "/home", 0755);
    vfs_mkdir(&vfs, 1, "/tmp", 0755);
    vfs_mkdir(&vfs, 1, "/var", 0755);

    /* Create persistent directories in GeoFS */
    if (kernel->geofs_volume) {
        vfs_mkdir(&vfs, 1, "/geo/home", 0755);
        vfs_mkdir(&vfs, 1, "/geo/data", 0755);
        vfs_mkdir(&vfs, 1, "/geo/var", 0755);
        vfs_mkdir(&vfs, 1, "/geo/var/log", 0755);
        vfs_mkdir(&vfs, 1, "/geo/var/log/governor", 0755);
    }

    /* Initialize the enhanced Governor */
    phantom_governor_t gov;
    governor_init(&gov, kernel);
    kernel->governor = &gov;

    /* Initialize User System */
    phantom_user_system_t user_sys;
    phantom_user_system_init(&user_sys, kernel);

    /* Initialize DNAuth System */
    dnauth_system_t *dnauth = dnauth_init("/tmp/dnauth");
    if (dnauth) {
        dnauth_evolution_init(dnauth);
        dnauth_set_governor(dnauth, &gov);  /* Enable Governor audit logging */
        kernel->dnauth = dnauth;
        printf("  [kernel] DNAuth system initialized with evolution and Governor integration\n");
    } else {
        printf("  [kernel] Warning: DNAuth initialization failed\n");
    }

    /* Initialize and start the Init System */
    phantom_init_t init;
    init_create(&init, kernel, &vfs);
    kernel->init = &init;
    init_start(&init);

    /* Initialize shell */
    struct shell_context shell;
    shell_init(&shell, kernel, &vfs);
    shell_set_user_system(&shell, &user_sys);

    /* Require login before shell access */
    if (shell_login(&shell) != 0) {
        /* Login failed or user exited */
        printf("  [kernel] Login failed or cancelled\n");
        shell_cleanup(&shell);
        init_shutdown(&init);
        kernel->init = NULL;
        if (dnauth) {
            dnauth_evolution_cleanup(dnauth);
            dnauth_cleanup(dnauth);
            kernel->dnauth = NULL;
        }
        phantom_user_system_shutdown(&user_sys);
        governor_shutdown(&gov);
        kernel->governor = NULL;
        vfs_shutdown(&vfs);
        return;
    }

    /* Create shell process */
    const char *shell_code = "int main() { phantom_shell_run(); }";
    phantom_pid_t shell_pid = 0;
    phantom_process_create(kernel, shell_code, strlen(shell_code), "phantom-shell", &shell_pid);
    shell.pid = shell_pid;

    /* Run interactive shell */
    shell_run(&shell);

    /* Cleanup */
    shell_cleanup(&shell);
    init_shutdown(&init);
    kernel->init = NULL;
    if (dnauth) {
        dnauth_evolution_cleanup(dnauth);
        dnauth_cleanup(dnauth);
        kernel->dnauth = NULL;
    }
    phantom_user_system_shutdown(&user_sys);
    governor_shutdown(&gov);
    kernel->governor = NULL;
    vfs_shutdown(&vfs);
}

static void usage(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║                   PHANTOM KERNEL                      ║\n");
    printf("║            \"To Create, Not To Destroy\"                ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  A microkernel implementing the Phantom Constitution.\n");
    printf("\n");
    printf("  USAGE:\n");
    printf("    phantom demo      Run demonstration\n");
    printf("    phantom shell     Launch interactive shell\n");
    printf("    phantom help      Show this help\n");
    printf("\n");
    printf("  PRINCIPLES:\n");
    printf("    - All code must be Governor-approved before execution\n");
    printf("    - Destructive operations are architecturally absent\n");
    printf("    - Processes are suspended, never killed\n");
    printf("    - All data persists in the geology forever\n");
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 0;
    }

    if (strcmp(argv[1], "help") == 0) {
        usage();
        return 0;
    }

    if (strcmp(argv[1], "demo") == 0) {
        struct phantom_kernel kernel;

        phantom_init(&kernel, "phantom.geo");

        /* Try to restore processes from previous run */
        phantom_process_restore_all(&kernel);

        demo_good_code(&kernel);
        demo_bad_code(&kernel);
        demo_core_system(&kernel);
        demo_vfs(&kernel);
        print_process_list(&kernel);

        phantom_shutdown(&kernel);
        return 0;
    }

    if (strcmp(argv[1], "shell") == 0) {
        struct phantom_kernel kernel;

        phantom_init(&kernel, "phantom.geo");

        /* Try to restore processes from previous run */
        phantom_process_restore_all(&kernel);

        /* Run interactive shell */
        run_shell(&kernel);

        phantom_shutdown(&kernel);
        return 0;
    }

    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    usage();
    return 1;
}

#endif /* PHANTOM_NO_MAIN */
