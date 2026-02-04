/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM USER SYSTEM
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Implementation of the Phantom user and permission system.
 * Users are never deleted - they become dormant.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#include "phantom_user.h"
#include "phantom.h"
#include "governor.h"
#include "phantom_dnauth.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Cryptographically Secure Random Number Generation
 * Uses /dev/urandom for security-critical operations
 * ───────────────────────────────────────────────────────────────────────────── */

static int secure_random_bytes(void *buf, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;

    ssize_t bytes_read = 0;
    size_t total = 0;
    uint8_t *ptr = (uint8_t *)buf;

    while (total < len) {
        bytes_read = read(fd, ptr + total, len - total);
        if (bytes_read <= 0) {
            close(fd);
            return -1;
        }
        total += bytes_read;
    }

    close(fd);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * SHA-256 Implementation for Password Hashing
 * ───────────────────────────────────────────────────────────────────────────── */

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x) (SHA256_ROTR(x, 2) ^ SHA256_ROTR(x, 13) ^ SHA256_ROTR(x, 22))
#define SHA256_EP1(x) (SHA256_ROTR(x, 6) ^ SHA256_ROTR(x, 11) ^ SHA256_ROTR(x, 25))
#define SHA256_SIG0(x) (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ ((x) >> 10))

typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} sha256_ctx;

static void sha256_transform(sha256_ctx *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    for (; i < 64; ++i)
        m[i] = SHA256_SIG1(m[i - 2]) + m[i - 7] + SHA256_SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + sha256_k[i] + m[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t data[], size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t hash[32]) {
    uint32_t i = ctx->datalen;

    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = ctx->bitlen;
    ctx->data[62] = ctx->bitlen >> 8;
    ctx->data[61] = ctx->bitlen >> 16;
    ctx->data[60] = ctx->bitlen >> 24;
    ctx->data[59] = ctx->bitlen >> 32;
    ctx->data[58] = ctx->bitlen >> 40;
    ctx->data[57] = ctx->bitlen >> 48;
    ctx->data[56] = ctx->bitlen >> 56;
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        hash[i]      = (ctx->state[0] >> (24 - i * 8)) & 0xff;
        hash[i + 4]  = (ctx->state[1] >> (24 - i * 8)) & 0xff;
        hash[i + 8]  = (ctx->state[2] >> (24 - i * 8)) & 0xff;
        hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0xff;
        hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0xff;
        hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0xff;
        hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0xff;
        hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0xff;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * PBKDF2-SHA256 Implementation for Secure Password Hashing
 * ───────────────────────────────────────────────────────────────────────────── */

#define PBKDF2_ITERATIONS 100000  /* OWASP recommended minimum */

static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[32]) {
    uint8_t k_ipad[64], k_opad[64];
    uint8_t tk[32];
    sha256_ctx ctx;

    /* If key is longer than 64 bytes, hash it first */
    if (key_len > 64) {
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, tk);
        key = tk;
        key_len = 32;
    }

    memset(k_ipad, 0x36, 64);
    memset(k_opad, 0x5c, 64);
    for (size_t i = 0; i < key_len; i++) {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }

    /* Inner hash: H(K XOR ipad, data) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, out);

    /* Outer hash: H(K XOR opad, inner_hash) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, out, 32);
    sha256_final(&ctx, out);
}

static int pbkdf2_sha256(const char *password, const uint8_t *salt, size_t salt_len,
                         int iterations, uint8_t *out, size_t out_len) {
    uint8_t U[32], T[32];
    uint8_t salt_block[128];
    size_t password_len = strlen(password);

    if (salt_len > 120) return -1;  /* Salt too long */

    size_t blocks = (out_len + 31) / 32;
    for (size_t block = 1; block <= blocks; block++) {
        /* Salt || INT(block) */
        memcpy(salt_block, salt, salt_len);
        salt_block[salt_len] = (block >> 24) & 0xff;
        salt_block[salt_len + 1] = (block >> 16) & 0xff;
        salt_block[salt_len + 2] = (block >> 8) & 0xff;
        salt_block[salt_len + 3] = block & 0xff;

        /* U1 = PRF(Password, Salt || INT(i)) */
        hmac_sha256((const uint8_t *)password, password_len,
                    salt_block, salt_len + 4, U);
        memcpy(T, U, 32);

        /* Iterate: U2 = PRF(Password, U1), T ^= U2, etc. */
        for (int j = 1; j < iterations; j++) {
            hmac_sha256((const uint8_t *)password, password_len, U, 32, U);
            for (int k = 0; k < 32; k++) T[k] ^= U[k];
        }

        /* Copy result block */
        size_t offset = (block - 1) * 32;
        size_t copy_len = (out_len - offset < 32) ? out_len - offset : 32;
        memcpy(out + offset, T, copy_len);
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Password Utilities - Secure Implementation
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_password_generate_salt(char *salt_out, size_t salt_len) {
    if (!salt_out || salt_len < 16) return -1;

    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    size_t len = salt_len - 1;
    if (len > 31) len = 31;

    uint8_t random_bytes[32];
    if (secure_random_bytes(random_bytes, len) != 0) {
        return -1;  /* Failed to get secure random */
    }

    for (size_t i = 0; i < len; i++) {
        salt_out[i] = charset[random_bytes[i] % (sizeof(charset) - 1)];
    }
    salt_out[len] = '\0';

    /* Clear sensitive data */
    memset(random_bytes, 0, sizeof(random_bytes));

    return 0;
}

int phantom_password_hash(const char *password, const char *salt,
                          char *hash_out, size_t hash_len) {
    if (!password || !salt || !hash_out || hash_len < 65) return -1;

    uint8_t derived_key[32];

    /* Use PBKDF2-SHA256 with 100,000 iterations */
    if (pbkdf2_sha256(password, (const uint8_t *)salt, strlen(salt),
                      PBKDF2_ITERATIONS, derived_key, 32) != 0) {
        return -1;
    }

    /* Convert to hex string */
    for (int i = 0; i < 32; i++) {
        snprintf(hash_out + (i * 2), 3, "%02x", derived_key[i]);
    }
    hash_out[64] = '\0';

    /* Clear sensitive data */
    memset(derived_key, 0, sizeof(derived_key));

    return 0;
}

int phantom_password_check_strength(const char *password) {
    if (!password) return 0;

    size_t len = strlen(password);
    if (len < 8) return 0;  /* Too short */

    int has_upper = 0, has_lower = 0, has_digit = 0, has_special = 0;

    for (size_t i = 0; i < len; i++) {
        if (isupper(password[i])) has_upper = 1;
        else if (islower(password[i])) has_lower = 1;
        else if (isdigit(password[i])) has_digit = 1;
        else has_special = 1;
    }

    int strength = has_upper + has_lower + has_digit + has_special;
    return strength >= 3 ? 1 : 0;  /* Need 3 of 4 categories */
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Lifecycle
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_user_system_init(phantom_user_system_t *sys, struct phantom_kernel *kernel) {
    if (!sys) return -1;

    memset(sys, 0, sizeof(phantom_user_system_t));

    sys->kernel = kernel;
    sys->next_uid = PHANTOM_UID_FIRST_USER;
    sys->next_gid = 1000;
    sys->next_session_id = 1;

    /* Configuration defaults */
    sys->require_strong_passwords = 1;
    sys->max_failed_logins = 5;
    sys->lockout_duration_sec = 300;  /* 5 minutes */
    sys->session_timeout_sec = 3600;  /* 1 hour */

    /* Create PHantomOS admin user */
    phantom_user_t *root = &sys->users[0];
    root->uid = PHANTOM_UID_ROOT;
    root->primary_gid = PHANTOM_GID_ROOT;
    root->state = USER_STATE_ACTIVE;
    strncpy(root->username, "PHaNtoM687", PHANTOM_MAX_USERNAME - 1);
    strncpy(root->full_name, "PhantomOS Administrator", 127);
    strncpy(root->home_dir, "/home/PHaNtoM687", 255);
    strncpy(root->shell, "/bin/phantom", 127);
    root->permissions = PERM_ADMIN;
    root->capabilities = 0xFFFFFFFF;  /* All capabilities */
    root->created_at = time(NULL);
    phantom_password_generate_salt(root->password_salt, PHANTOM_SALT_LEN);
    phantom_password_hash("Dghcxa!j4m", root->password_salt,
                          root->password_hash, PHANTOM_HASH_LEN);
    sys->user_count = 1;

    /* Create system user */
    phantom_user_t *system_user = &sys->users[1];
    system_user->uid = PHANTOM_UID_SYSTEM;
    system_user->primary_gid = PHANTOM_GID_ROOT;
    system_user->state = USER_STATE_ACTIVE;
    strncpy(system_user->username, "system", PHANTOM_MAX_USERNAME - 1);
    strncpy(system_user->full_name, "System Services", 127);
    strncpy(system_user->home_dir, "/", 255);
    strncpy(system_user->shell, "/bin/false", 127);
    system_user->permissions = PERM_NONE;  /* Cannot login */
    system_user->capabilities = 0xFFFFFFFF;
    system_user->created_at = time(NULL);
    sys->user_count = 2;

    /* Create nobody user */
    phantom_user_t *nobody = &sys->users[2];
    nobody->uid = PHANTOM_UID_NOBODY;
    nobody->primary_gid = PHANTOM_GID_USERS;
    nobody->state = USER_STATE_ACTIVE;
    strncpy(nobody->username, "nobody", PHANTOM_MAX_USERNAME - 1);
    strncpy(nobody->full_name, "Unprivileged User", 127);
    strncpy(nobody->home_dir, "/nonexistent", 255);
    strncpy(nobody->shell, "/bin/false", 127);
    nobody->permissions = PERM_NONE;
    nobody->capabilities = 0;
    nobody->created_at = time(NULL);
    sys->user_count = 3;

    /* Create root group */
    phantom_group_t *root_group = &sys->groups[0];
    root_group->gid = PHANTOM_GID_ROOT;
    root_group->state = USER_STATE_ACTIVE;
    strncpy(root_group->name, "root", PHANTOM_MAX_GROUPNAME - 1);
    strncpy(root_group->description, "System administrators", 255);
    root_group->permissions = PERM_ADMIN;
    root_group->capabilities = 0xFFFFFFFF;
    root_group->created_at = time(NULL);
    sys->group_count = 1;

    /* Create wheel group (admin) */
    phantom_group_t *wheel = &sys->groups[1];
    wheel->gid = PHANTOM_GID_WHEEL;
    wheel->state = USER_STATE_ACTIVE;
    strncpy(wheel->name, "wheel", PHANTOM_MAX_GROUPNAME - 1);
    strncpy(wheel->description, "Sudo access group", 255);
    wheel->permissions = PERM_SUDO | PERM_VIEW_LOGS;
    wheel->created_at = time(NULL);
    sys->group_count = 2;

    /* Create users group */
    phantom_group_t *users_group = &sys->groups[2];
    users_group->gid = PHANTOM_GID_USERS;
    users_group->state = USER_STATE_ACTIVE;
    strncpy(users_group->name, "users", PHANTOM_MAX_GROUPNAME - 1);
    strncpy(users_group->description, "Regular users", 255);
    users_group->permissions = PERM_BASIC;
    users_group->created_at = time(NULL);
    sys->group_count = 3;

    sys->initialized = 1;

    printf("[phantom_user] User system initialized\n");
    printf("              Admin user: PHantomOS\n");
    printf("              Users are never deleted, only made dormant\n");

    return 0;
}

void phantom_user_system_shutdown(phantom_user_system_t *sys) {
    if (!sys || !sys->initialized) return;

    printf("[phantom_user] User system shutting down...\n");
    printf("              %d users (%lu dormant), %d groups\n",
           sys->user_count, sys->users_dormant, sys->group_count);
    printf("              Total logins: %lu, Failed: %lu\n",
           sys->total_logins, sys->failed_logins);

    /* Sessions remain logged - don't clear them */
    sys->initialized = 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * User Management
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_user_create(phantom_user_system_t *sys, const char *username,
                        const char *password, const char *full_name,
                        uint32_t creator_uid, uint32_t *uid_out) {
    if (!sys || !username || !password) return USER_ERR_INVALID;

    /* Check if creator has permission */
    phantom_user_t *creator = phantom_user_find_by_uid(sys, creator_uid);
    if (!creator || !(creator->permissions & PERM_CREATE_USER)) {
        if (creator_uid != PHANTOM_UID_ROOT) {
            printf("[phantom_user] Permission denied: user %u cannot create users\n",
                   creator_uid);
            return USER_ERR_DENIED;
        }
    }

    /* Check for duplicate username */
    if (phantom_user_find_by_name(sys, username)) {
        printf("[phantom_user] Username '%s' already exists\n", username);
        return USER_ERR_EXISTS;
    }

    /* Check password strength */
    if (sys->require_strong_passwords && !phantom_password_check_strength(password)) {
        printf("[phantom_user] Password too weak\n");
        return USER_ERR_WEAK_PASSWORD;
    }

    if (sys->user_count >= PHANTOM_MAX_USERS) {
        return USER_ERR_FULL;
    }

    /* Find slot (may reuse slot of dormant user for storage, but uid never reused) */
    phantom_user_t *user = &sys->users[sys->user_count];
    memset(user, 0, sizeof(phantom_user_t));

    user->uid = sys->next_uid++;
    user->primary_gid = PHANTOM_GID_USERS;
    user->state = USER_STATE_ACTIVE;
    strncpy(user->username, username, PHANTOM_MAX_USERNAME - 1);
    if (full_name) {
        strncpy(user->full_name, full_name, 127);
    }
    snprintf(user->home_dir, sizeof(user->home_dir), "/home/%s", username);
    strncpy(user->shell, "/bin/phantom", 127);

    /* Set password */
    phantom_password_generate_salt(user->password_salt, PHANTOM_SALT_LEN);
    phantom_password_hash(password, user->password_salt,
                          user->password_hash, PHANTOM_HASH_LEN);
    user->password_version = 1;

    /* Default permissions */
    user->permissions = PERM_STANDARD;
    user->capabilities = CAP_BASIC | CAP_INFO;

    /* Add to users group */
    user->groups[0] = PHANTOM_GID_USERS;
    user->group_count = 1;

    /* Timestamps */
    user->created_at = time(NULL);
    user->last_password_change = user->created_at;
    user->created_by_uid = creator_uid;

    sys->user_count++;
    sys->users_created++;

    if (uid_out) *uid_out = user->uid;

    printf("[phantom_user] Created user '%s' (uid=%u) by uid=%u\n",
           username, user->uid, creator_uid);

    return USER_OK;
}

int phantom_user_set_password(phantom_user_system_t *sys, uint32_t uid,
                              const char *new_password, uint32_t actor_uid) {
    if (!sys || !new_password) return USER_ERR_INVALID;

    phantom_user_t *user = phantom_user_find_by_uid(sys, uid);
    if (!user) return USER_ERR_NOT_FOUND;

    /* Check permission: can change own password, or admin can change any */
    if (actor_uid != uid) {
        phantom_user_t *actor = phantom_user_find_by_uid(sys, actor_uid);
        if (!actor || !(actor->permissions & PERM_MANAGE_USER)) {
            if (actor_uid != PHANTOM_UID_ROOT) {
                return USER_ERR_DENIED;
            }
        }
    }

    /* Check password strength */
    if (sys->require_strong_passwords && !phantom_password_check_strength(new_password)) {
        return USER_ERR_WEAK_PASSWORD;
    }

    /* Generate new salt and hash (old password preserved in geology) */
    phantom_password_generate_salt(user->password_salt, PHANTOM_SALT_LEN);
    phantom_password_hash(new_password, user->password_salt,
                          user->password_hash, PHANTOM_HASH_LEN);
    user->password_version++;
    user->last_password_change = time(NULL);

    printf("[phantom_user] Password changed for '%s' (version %u) by uid=%u\n",
           user->username, user->password_version, actor_uid);

    return USER_OK;
}

int phantom_user_set_state(phantom_user_system_t *sys, uint32_t uid,
                           phantom_user_state_t state, uint32_t actor_uid) {
    if (!sys) return USER_ERR_INVALID;

    phantom_user_t *user = phantom_user_find_by_uid(sys, uid);
    if (!user) return USER_ERR_NOT_FOUND;

    /* Only admins can change user state */
    if (actor_uid != PHANTOM_UID_ROOT) {
        phantom_user_t *actor = phantom_user_find_by_uid(sys, actor_uid);
        if (!actor || !(actor->permissions & PERM_MANAGE_USER)) {
            return USER_ERR_DENIED;
        }
    }

    /* Cannot "delete" root */
    if (uid == PHANTOM_UID_ROOT && state == USER_STATE_DORMANT) {
        printf("[phantom_user] Cannot make root dormant\n");
        return USER_ERR_DENIED;
    }

    phantom_user_state_t old_state = user->state;
    user->state = state;
    user->state_changed_at = time(NULL);

    if (state == USER_STATE_DORMANT) {
        sys->users_dormant++;
    } else if (old_state == USER_STATE_DORMANT) {
        sys->users_dormant--;
    }

    printf("[phantom_user] User '%s' state: %s -> %s (by uid=%u)\n",
           user->username,
           phantom_user_state_string(old_state),
           phantom_user_state_string(state),
           actor_uid);

    return USER_OK;
}

int phantom_user_grant_permission(phantom_user_system_t *sys, uint32_t uid,
                                  uint32_t permission, uint32_t actor_uid) {
    if (!sys) return USER_ERR_INVALID;

    phantom_user_t *user = phantom_user_find_by_uid(sys, uid);
    if (!user) return USER_ERR_NOT_FOUND;

    /* Only admins can grant permissions */
    if (actor_uid != PHANTOM_UID_ROOT) {
        phantom_user_t *actor = phantom_user_find_by_uid(sys, actor_uid);
        if (!actor || !(actor->permissions & PERM_MANAGE_USER)) {
            return USER_ERR_DENIED;
        }
    }

    user->permissions |= permission;

    printf("[phantom_user] Granted permission 0x%x to '%s' by uid=%u\n",
           permission, user->username, actor_uid);

    return USER_OK;
}

int phantom_user_revoke_permission(phantom_user_system_t *sys, uint32_t uid,
                                   uint32_t permission, uint32_t actor_uid) {
    if (!sys) return USER_ERR_INVALID;

    phantom_user_t *user = phantom_user_find_by_uid(sys, uid);
    if (!user) return USER_ERR_NOT_FOUND;

    if (actor_uid != PHANTOM_UID_ROOT) {
        phantom_user_t *actor = phantom_user_find_by_uid(sys, actor_uid);
        if (!actor || !(actor->permissions & PERM_MANAGE_USER)) {
            return USER_ERR_DENIED;
        }
    }

    user->permissions &= ~permission;

    printf("[phantom_user] Revoked permission 0x%x from '%s' by uid=%u\n",
           permission, user->username, actor_uid);

    return USER_OK;
}

int phantom_user_grant_capability(phantom_user_system_t *sys, uint32_t uid,
                                  uint32_t capability, uint32_t actor_uid) {
    if (!sys) return USER_ERR_INVALID;

    phantom_user_t *user = phantom_user_find_by_uid(sys, uid);
    if (!user) return USER_ERR_NOT_FOUND;

    if (actor_uid != PHANTOM_UID_ROOT) {
        phantom_user_t *actor = phantom_user_find_by_uid(sys, actor_uid);
        if (!actor || !(actor->permissions & PERM_GOVERNOR_ADMIN)) {
            return USER_ERR_DENIED;
        }
    }

    user->capabilities |= capability;

    printf("[phantom_user] Granted capability 0x%x to '%s' by uid=%u\n",
           capability, user->username, actor_uid);

    return USER_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * User Lookup
 * ───────────────────────────────────────────────────────────────────────────── */

phantom_user_t *phantom_user_find_by_uid(phantom_user_system_t *sys, uint32_t uid) {
    if (!sys) return NULL;

    for (int i = 0; i < sys->user_count; i++) {
        if (sys->users[i].uid == uid) {
            return &sys->users[i];
        }
    }
    return NULL;
}

phantom_user_t *phantom_user_find_by_name(phantom_user_system_t *sys, const char *username) {
    if (!sys || !username) return NULL;

    for (int i = 0; i < sys->user_count; i++) {
        if (strcmp(sys->users[i].username, username) == 0) {
            return &sys->users[i];
        }
    }
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Group Management
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_group_create(phantom_user_system_t *sys, const char *name,
                         const char *description, uint32_t creator_uid,
                         uint32_t *gid_out) {
    if (!sys || !name) return USER_ERR_INVALID;

    /* Check permission */
    if (creator_uid != PHANTOM_UID_ROOT) {
        phantom_user_t *creator = phantom_user_find_by_uid(sys, creator_uid);
        if (!creator || !(creator->permissions & PERM_CREATE_GROUP)) {
            return USER_ERR_DENIED;
        }
    }

    /* Check for duplicate */
    if (phantom_group_find_by_name(sys, name)) {
        return USER_ERR_EXISTS;
    }

    if (sys->group_count >= PHANTOM_MAX_GROUPS) {
        return USER_ERR_FULL;
    }

    phantom_group_t *group = &sys->groups[sys->group_count];
    memset(group, 0, sizeof(phantom_group_t));

    group->gid = sys->next_gid++;
    group->state = USER_STATE_ACTIVE;
    strncpy(group->name, name, PHANTOM_MAX_GROUPNAME - 1);
    if (description) {
        strncpy(group->description, description, 255);
    }
    group->permissions = PERM_BASIC;
    group->created_at = time(NULL);
    group->created_by_uid = creator_uid;

    sys->group_count++;

    if (gid_out) *gid_out = group->gid;

    printf("[phantom_user] Created group '%s' (gid=%u) by uid=%u\n",
           name, group->gid, creator_uid);

    return USER_OK;
}

int phantom_group_add_user(phantom_user_system_t *sys, uint32_t gid,
                           uint32_t uid, uint32_t actor_uid) {
    if (!sys) return USER_ERR_INVALID;

    phantom_group_t *group = phantom_group_find_by_gid(sys, gid);
    if (!group) return USER_ERR_NOT_FOUND;

    phantom_user_t *user = phantom_user_find_by_uid(sys, uid);
    if (!user) return USER_ERR_NOT_FOUND;

    /* Check permission */
    if (actor_uid != PHANTOM_UID_ROOT) {
        phantom_user_t *actor = phantom_user_find_by_uid(sys, actor_uid);
        if (!actor || !(actor->permissions & PERM_MANAGE_GROUP)) {
            return USER_ERR_DENIED;
        }
    }

    /* Check if already member */
    for (int i = 0; i < user->group_count; i++) {
        if (user->groups[i] == gid) {
            return USER_OK;  /* Already a member */
        }
    }

    if (user->group_count >= 16) {
        return USER_ERR_FULL;
    }

    user->groups[user->group_count++] = gid;

    printf("[phantom_user] Added user '%s' to group '%s' by uid=%u\n",
           user->username, group->name, actor_uid);

    return USER_OK;
}

int phantom_group_remove_user(phantom_user_system_t *sys, uint32_t gid,
                              uint32_t uid, uint32_t actor_uid) {
    if (!sys) return USER_ERR_INVALID;

    phantom_group_t *group = phantom_group_find_by_gid(sys, gid);
    if (!group) return USER_ERR_NOT_FOUND;

    phantom_user_t *user = phantom_user_find_by_uid(sys, uid);
    if (!user) return USER_ERR_NOT_FOUND;

    if (actor_uid != PHANTOM_UID_ROOT) {
        phantom_user_t *actor = phantom_user_find_by_uid(sys, actor_uid);
        if (!actor || !(actor->permissions & PERM_MANAGE_GROUP)) {
            return USER_ERR_DENIED;
        }
    }

    /* Find and remove from group list */
    for (int i = 0; i < user->group_count; i++) {
        if (user->groups[i] == gid) {
            /* Shift remaining groups */
            for (int j = i; j < user->group_count - 1; j++) {
                user->groups[j] = user->groups[j + 1];
            }
            user->group_count--;

            printf("[phantom_user] Removed user '%s' from group '%s' by uid=%u\n",
                   user->username, group->name, actor_uid);
            return USER_OK;
        }
    }

    return USER_ERR_NOT_FOUND;
}

phantom_group_t *phantom_group_find_by_gid(phantom_user_system_t *sys, uint32_t gid) {
    if (!sys) return NULL;

    for (int i = 0; i < sys->group_count; i++) {
        if (sys->groups[i].gid == gid) {
            return &sys->groups[i];
        }
    }
    return NULL;
}

phantom_group_t *phantom_group_find_by_name(phantom_user_system_t *sys, const char *name) {
    if (!sys || !name) return NULL;

    for (int i = 0; i < sys->group_count; i++) {
        if (strcmp(sys->groups[i].name, name) == 0) {
            return &sys->groups[i];
        }
    }
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Authentication
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_user_authenticate(phantom_user_system_t *sys, const char *username,
                              const char *password, phantom_session_t **session_out) {
    if (!sys || !username || !password) return USER_ERR_INVALID;

    phantom_user_t *user = phantom_user_find_by_name(sys, username);
    if (!user) {
        sys->failed_logins++;
        return USER_ERR_NOT_FOUND;
    }

    /* Check user state */
    if (user->state == USER_STATE_DORMANT) {
        return USER_ERR_DORMANT;
    }
    if (user->state == USER_STATE_LOCKED) {
        /* Check if lockout expired */
        time_t now = time(NULL);
        if (now - user->state_changed_at < sys->lockout_duration_sec) {
            return USER_ERR_LOCKED;
        }
        /* Lockout expired, reactivate */
        user->state = USER_STATE_ACTIVE;
        user->failed_logins = 0;
    }
    if (user->state == USER_STATE_SUSPENDED) {
        return USER_ERR_DENIED;
    }

    /* Verify password */
    char computed_hash[PHANTOM_HASH_LEN + 1];
    phantom_password_hash(password, user->password_salt, computed_hash, PHANTOM_HASH_LEN);

    if (strcmp(computed_hash, user->password_hash) != 0) {
        user->failed_logins++;
        sys->failed_logins++;

        /* Check for lockout */
        if (user->failed_logins >= (uint32_t)sys->max_failed_logins) {
            user->state = USER_STATE_LOCKED;
            user->state_changed_at = time(NULL);
            printf("[phantom_user] User '%s' locked after %u failed attempts\n",
                   username, user->failed_logins);
        }

        return USER_ERR_BAD_PASSWORD;
    }

    /* Successful authentication */
    user->failed_logins = 0;
    user->total_logins++;
    user->last_login = time(NULL);
    sys->total_logins++;

    /* Create session */
    if (sys->session_count >= PHANTOM_MAX_SESSIONS) {
        return USER_ERR_FULL;
    }

    phantom_session_t *session = &sys->sessions[sys->session_count];
    memset(session, 0, sizeof(phantom_session_t));

    session->session_id = sys->next_session_id++;
    session->uid = user->uid;
    session->started_at = time(NULL);
    session->last_activity = session->started_at;
    if (sys->session_timeout_sec > 0) {
        session->expires_at = session->started_at + sys->session_timeout_sec;
    }
    session->effective_uid = user->uid;

    sys->session_count++;
    sys->current_session = session;

    if (session_out) *session_out = session;

    printf("[phantom_user] User '%s' authenticated (session %lu)\n",
           username, session->session_id);

    return USER_OK;
}

int phantom_user_authenticate_dna(phantom_user_system_t *sys, const char *username,
                                  const char *dna_sequence, void *dnauth_system,
                                  phantom_session_t **session_out) {
    if (!sys || !username || !dna_sequence || !dnauth_system) return USER_ERR_INVALID;

    dnauth_system_t *dnauth = (dnauth_system_t *)dnauth_system;

    phantom_user_t *user = phantom_user_find_by_name(sys, username);
    if (!user) {
        sys->failed_logins++;
        return USER_ERR_NOT_FOUND;
    }

    /* Check user state */
    if (user->state == USER_STATE_DORMANT) {
        return USER_ERR_DORMANT;
    }
    if (user->state == USER_STATE_LOCKED) {
        /* Check if lockout expired */
        time_t now = time(NULL);
        if (now - user->state_changed_at < sys->lockout_duration_sec) {
            return USER_ERR_LOCKED;
        }
        /* Lockout expired, reactivate */
        user->state = USER_STATE_ACTIVE;
        user->failed_logins = 0;
    }
    if (user->state == USER_STATE_SUSPENDED) {
        return USER_ERR_DENIED;
    }

    /* Authenticate using DNAuth */
    dnauth_result_t dna_result = dnauth_authenticate(dnauth, username, dna_sequence);

    /* If exact match fails, try ancestor authentication */
    if (dna_result == DNAUTH_ERR_NO_MATCH && dnauth->evolution_enabled) {
        int gen_matched = -1;
        dna_result = dnauth_authenticate_ancestor(dnauth, username, dna_sequence, 5, &gen_matched);
        if (dna_result == DNAUTH_OK) {
            printf("[phantom_user] DNA matched ancestor sequence (gen %d back)\n", gen_matched);
        }
    }

    if (dna_result != DNAUTH_OK) {
        user->failed_logins++;
        sys->failed_logins++;

        /* Check for lockout */
        if (user->failed_logins >= (uint32_t)sys->max_failed_logins) {
            user->state = USER_STATE_LOCKED;
            user->state_changed_at = time(NULL);
            printf("[phantom_user] User '%s' locked after %u failed DNA auth attempts\n",
                   username, user->failed_logins);
        }

        return USER_ERR_BAD_PASSWORD;  /* Re-use this error for DNA auth failure */
    }

    /* Successful DNA authentication */
    user->failed_logins = 0;
    user->total_logins++;
    user->last_login = time(NULL);
    sys->total_logins++;

    /* Create session */
    if (sys->session_count >= PHANTOM_MAX_SESSIONS) {
        return USER_ERR_FULL;
    }

    phantom_session_t *session = &sys->sessions[sys->session_count];
    memset(session, 0, sizeof(phantom_session_t));

    session->session_id = sys->next_session_id++;
    session->uid = user->uid;
    session->started_at = time(NULL);
    session->last_activity = session->started_at;
    if (sys->session_timeout_sec > 0) {
        session->expires_at = session->started_at + sys->session_timeout_sec;
    }
    session->effective_uid = user->uid;

    sys->session_count++;
    sys->current_session = session;

    if (session_out) *session_out = session;

    /* Show fitness info for DNA-authenticated users */
    double fitness = dnauth_get_fitness(dnauth, username);
    int gen = dnauth_get_generation_number(dnauth, username);

    printf("[phantom_user] User '%s' DNA-authenticated (session %lu, gen %d, fitness %.0f%%)\n",
           username, session->session_id, gen, fitness * 100.0);

    return USER_OK;
}

int phantom_user_logout(phantom_user_system_t *sys, uint64_t session_id) {
    if (!sys) return USER_ERR_INVALID;

    for (int i = 0; i < sys->session_count; i++) {
        if (sys->sessions[i].session_id == session_id) {
            phantom_user_t *user = phantom_user_find_by_uid(sys, sys->sessions[i].uid);

            /* Don't delete session - just mark inactive (Phantom style) */
            sys->sessions[i].expires_at = time(NULL);  /* Expired now */

            if (sys->current_session == &sys->sessions[i]) {
                sys->current_session = NULL;
            }

            printf("[phantom_user] User '%s' logged out (session %lu preserved)\n",
                   user ? user->username : "unknown", session_id);

            return USER_OK;
        }
    }

    return USER_ERR_NOT_FOUND;
}

int phantom_user_elevate(phantom_user_system_t *sys, uint64_t session_id,
                         const char *password) {
    if (!sys || !password) return USER_ERR_INVALID;

    phantom_session_t *session = phantom_session_get(sys, session_id);
    if (!session) return USER_ERR_NO_SESSION;

    phantom_user_t *user = phantom_user_find_by_uid(sys, session->uid);
    if (!user) return USER_ERR_NOT_FOUND;

    /* Check if user has sudo permission */
    if (!(user->permissions & PERM_SUDO)) {
        printf("[phantom_user] User '%s' not in sudoers\n", user->username);
        return USER_ERR_DENIED;
    }

    /* Verify password */
    char computed_hash[PHANTOM_HASH_LEN + 1];
    phantom_password_hash(password, user->password_salt, computed_hash, PHANTOM_HASH_LEN);

    if (strcmp(computed_hash, user->password_hash) != 0) {
        printf("[phantom_user] Elevation failed: incorrect password\n");
        return USER_ERR_BAD_PASSWORD;
    }

    session->is_elevated = 1;
    session->effective_uid = PHANTOM_UID_ROOT;

    printf("[phantom_user] Session %lu elevated to root\n", session_id);

    return USER_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Session Management
 * ───────────────────────────────────────────────────────────────────────────── */

phantom_session_t *phantom_session_get(phantom_user_system_t *sys, uint64_t session_id) {
    if (!sys) return NULL;

    for (int i = 0; i < sys->session_count; i++) {
        if (sys->sessions[i].session_id == session_id) {
            return &sys->sessions[i];
        }
    }
    return NULL;
}

int phantom_session_refresh(phantom_user_system_t *sys, uint64_t session_id) {
    phantom_session_t *session = phantom_session_get(sys, session_id);
    if (!session) return USER_ERR_NO_SESSION;

    session->last_activity = time(NULL);
    if (session->expires_at > 0) {
        session->expires_at = session->last_activity + sys->session_timeout_sec;
    }

    return USER_OK;
}

int phantom_session_check(phantom_user_system_t *sys, uint64_t session_id) {
    phantom_session_t *session = phantom_session_get(sys, session_id);
    if (!session) return USER_ERR_NO_SESSION;

    if (session->expires_at > 0 && time(NULL) > session->expires_at) {
        return USER_ERR_SESSION_EXPIRED;
    }

    return USER_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Permission Checking
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_user_has_permission(phantom_user_system_t *sys, uint32_t uid,
                                uint32_t permission) {
    phantom_user_t *user = phantom_user_find_by_uid(sys, uid);
    if (!user) return 0;

    /* Direct user permission */
    if (user->permissions & permission) return 1;

    /* Check group permissions */
    for (int i = 0; i < user->group_count; i++) {
        phantom_group_t *group = phantom_group_find_by_gid(sys, user->groups[i]);
        if (group && (group->permissions & permission)) {
            return 1;
        }
    }

    return 0;
}

int phantom_user_has_capability(phantom_user_system_t *sys, uint32_t uid,
                                uint32_t capability) {
    phantom_user_t *user = phantom_user_find_by_uid(sys, uid);
    if (!user) return 0;

    /* Direct user capability */
    if (user->capabilities & capability) return 1;

    /* Check group capabilities */
    for (int i = 0; i < user->group_count; i++) {
        phantom_group_t *group = phantom_group_find_by_gid(sys, user->groups[i]);
        if (group && (group->capabilities & capability)) {
            return 1;
        }
    }

    return 0;
}

int phantom_user_can_access(phantom_user_system_t *sys, uint32_t uid,
                            const char *path, int mode) {
    (void)path; (void)mode;  /* Would integrate with VFS */

    /* Root can access everything */
    if (uid == PHANTOM_UID_ROOT) return 1;

    phantom_user_t *user = phantom_user_find_by_uid(sys, uid);
    if (!user || user->state != USER_STATE_ACTIVE) return 0;

    /* Basic access check - would integrate with VFS permissions */
    return 1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility Functions
 * ───────────────────────────────────────────────────────────────────────────── */

const char *phantom_user_state_string(phantom_user_state_t state) {
    switch (state) {
        case USER_STATE_ACTIVE:     return "active";
        case USER_STATE_LOCKED:     return "locked";
        case USER_STATE_SUSPENDED:  return "suspended";
        case USER_STATE_DORMANT:    return "dormant";
        default:                    return "unknown";
    }
}

const char *phantom_user_result_string(phantom_user_result_t result) {
    switch (result) {
        case USER_OK:               return "success";
        case USER_ERR_INVALID:      return "invalid parameter";
        case USER_ERR_EXISTS:       return "already exists";
        case USER_ERR_NOT_FOUND:    return "not found";
        case USER_ERR_DENIED:       return "permission denied";
        case USER_ERR_LOCKED:       return "account locked";
        case USER_ERR_DORMANT:      return "account dormant";
        case USER_ERR_BAD_PASSWORD: return "incorrect password";
        case USER_ERR_WEAK_PASSWORD: return "password too weak";
        case USER_ERR_SESSION_EXPIRED: return "session expired";
        case USER_ERR_NO_SESSION:   return "no active session";
        case USER_ERR_FULL:         return "maximum reached";
        default:                    return "unknown error";
    }
}

void phantom_user_print_info(phantom_user_t *user) {
    if (!user) return;

    printf("User: %s (uid=%u)\n", user->username, user->uid);
    printf("  Full name:  %s\n", user->full_name);
    printf("  State:      %s\n", phantom_user_state_string(user->state));
    printf("  Home:       %s\n", user->home_dir);
    printf("  Shell:      %s\n", user->shell);
    printf("  Primary GID: %u\n", user->primary_gid);
    printf("  Permissions: 0x%08x\n", user->permissions);
    printf("  Capabilities: 0x%08x\n", user->capabilities);
    printf("  Total logins: %u\n", user->total_logins);

    char time_buf[64];
    struct tm *tm = localtime(&user->created_at);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);
    printf("  Created:    %s\n", time_buf);

    if (user->last_login > 0) {
        tm = localtime(&user->last_login);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);
        printf("  Last login: %s\n", time_buf);
    }
}

void phantom_user_system_print_stats(phantom_user_system_t *sys) {
    if (!sys) return;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                  USER SYSTEM STATISTICS                        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Users:          %d total, %lu dormant\n", sys->user_count, sys->users_dormant);
    printf("  Groups:         %d\n", sys->group_count);
    printf("  Sessions:       %d active\n", sys->session_count);
    printf("\n");
    printf("  Total logins:   %lu\n", sys->total_logins);
    printf("  Failed logins:  %lu\n", sys->failed_logins);
    printf("  Users created:  %lu\n", sys->users_created);
    printf("\n");
}
