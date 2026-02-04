/*
 * Cryptographic Function Tests for PhantomOS
 * Tests PBKDF2, SHA-256, and secure random number generation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Testing %s... ", #name); \
    test_##name(); \
    printf("\033[32mPASSED\033[0m\n"); \
    tests_passed++; \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("\033[31mFAILED\033[0m at line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ═══════════════════════════════════════════════════════════════════════════
 * SECURE RANDOM (copied from phantom_user.c)
 * ═══════════════════════════════════════════════════════════════════════════ */

static int secure_random_bytes(void *buf, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;

    size_t total = 0;
    uint8_t *p = (uint8_t *)buf;
    while (total < len) {
        ssize_t n = read(fd, p + total, len - total);
        if (n <= 0) {
            close(fd);
            return -1;
        }
        total += n;
    }
    close(fd);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SHA-256 IMPLEMENTATION (copied from phantom_user.c)
 * ═══════════════════════════════════════════════════════════════════════════ */

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

static void sha256_transform(sha256_ctx *ctx, const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) | ((uint32_t)data[i * 4 + 3]);
    }
    for (; i < 64; i++) {
        m[i] = SHA256_SIG1(m[i - 2]) + m[i - 7] + SHA256_SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + sha256_k[i] + m[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
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

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t hash[32]) {
    uint32_t i = ctx->datalen;

    ctx->data[i++] = 0x80;
    if (ctx->datalen < 56) {
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
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

    for (i = 0; i < 8; i++) {
        hash[i * 4] = (ctx->state[i] >> 24) & 0xff;
        hash[i * 4 + 1] = (ctx->state[i] >> 16) & 0xff;
        hash[i * 4 + 2] = (ctx->state[i] >> 8) & 0xff;
        hash[i * 4 + 3] = ctx->state[i] & 0xff;
    }
}

static void sha256(const void *data, size_t len, uint8_t hash[32]) {
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)data, len);
    sha256_final(&ctx, hash);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * HMAC-SHA256
 * ═══════════════════════════════════════════════════════════════════════════ */

static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[32]) {
    uint8_t k_ipad[64], k_opad[64];
    uint8_t tk[32];

    if (key_len > 64) {
        sha256(key, key_len, tk);
        key = tk;
        key_len = 32;
    }

    memset(k_ipad, 0x36, 64);
    memset(k_opad, 0x5c, 64);
    for (size_t i = 0; i < key_len; i++) {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }

    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, data, data_len);
    uint8_t inner[32];
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PBKDF2-SHA256
 * ═══════════════════════════════════════════════════════════════════════════ */

static int pbkdf2_sha256(const char *password, const uint8_t *salt, size_t salt_len,
                         int iterations, uint8_t *out, size_t out_len) {
    if (!password || !salt || !out || iterations < 1) return -1;

    size_t pass_len = strlen(password);
    uint8_t *salt_block = malloc(salt_len + 4);
    if (!salt_block) return -1;
    memcpy(salt_block, salt, salt_len);

    size_t out_pos = 0;
    uint32_t block_num = 1;

    while (out_pos < out_len) {
        salt_block[salt_len] = (block_num >> 24) & 0xff;
        salt_block[salt_len + 1] = (block_num >> 16) & 0xff;
        salt_block[salt_len + 2] = (block_num >> 8) & 0xff;
        salt_block[salt_len + 3] = block_num & 0xff;

        uint8_t u[32], t[32];
        hmac_sha256((const uint8_t *)password, pass_len, salt_block, salt_len + 4, u);
        memcpy(t, u, 32);

        for (int i = 1; i < iterations; i++) {
            hmac_sha256((const uint8_t *)password, pass_len, u, 32, u);
            for (int j = 0; j < 32; j++) t[j] ^= u[j];
        }

        size_t to_copy = (out_len - out_pos < 32) ? out_len - out_pos : 32;
        memcpy(out + out_pos, t, to_copy);
        out_pos += to_copy;
        block_num++;
    }

    free(salt_block);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SHA-256 TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *hex) {
    for (size_t i = 0; i < len; i++) {
        sprintf(hex + i * 2, "%02x", bytes[i]);
    }
}

TEST(sha256_empty) {
    uint8_t hash[32];
    sha256("", 0, hash);
    char hex[65];
    bytes_to_hex(hash, 32, hex);
    /* SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
    ASSERT_TRUE(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
}

TEST(sha256_hello) {
    uint8_t hash[32];
    sha256("hello", 5, hash);
    char hex[65];
    bytes_to_hex(hash, 32, hex);
    /* SHA-256("hello") = 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824 */
    ASSERT_TRUE(strcmp(hex, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824") == 0);
}

TEST(sha256_abc) {
    uint8_t hash[32];
    sha256("abc", 3, hash);
    char hex[65];
    bytes_to_hex(hash, 32, hex);
    /* SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad */
    ASSERT_TRUE(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PBKDF2-SHA256 TESTS (verified with Python hashlib.pbkdf2_hmac)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(pbkdf2_sha256_1) {
    /* password="password", salt="salt", c=1, dkLen=20 */
    uint8_t out[20];
    pbkdf2_sha256("password", (const uint8_t *)"salt", 4, 1, out, 20);
    char hex[41];
    bytes_to_hex(out, 20, hex);
    /* Expected (from Python hashlib): 120fb6cffcf8b32c43e7225256c4f837a86548c9 */
    ASSERT_TRUE(strcmp(hex, "120fb6cffcf8b32c43e7225256c4f837a86548c9") == 0);
}

TEST(pbkdf2_sha256_2) {
    /* password="password", salt="salt", c=2, dkLen=20 */
    uint8_t out[20];
    pbkdf2_sha256("password", (const uint8_t *)"salt", 4, 2, out, 20);
    char hex[41];
    bytes_to_hex(out, 20, hex);
    /* Expected: ae4d0c95af6b46d32d0adff928f06dd02a303f8e */
    ASSERT_TRUE(strcmp(hex, "ae4d0c95af6b46d32d0adff928f06dd02a303f8e") == 0);
}

TEST(pbkdf2_sha256_4096) {
    /* password="password", salt="salt", c=4096, dkLen=20 */
    uint8_t out[20];
    pbkdf2_sha256("password", (const uint8_t *)"salt", 4, 4096, out, 20);
    char hex[41];
    bytes_to_hex(out, 20, hex);
    /* Expected: c5e478d59288c841aa530db6845c4c8d962893a0 */
    ASSERT_TRUE(strcmp(hex, "c5e478d59288c841aa530db6845c4c8d962893a0") == 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SECURE RANDOM TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(random_not_all_zeros) {
    uint8_t buf[32];
    ASSERT_TRUE(secure_random_bytes(buf, sizeof(buf)) == 0);

    /* Check it's not all zeros (astronomically unlikely if working) */
    int all_zero = 1;
    for (int i = 0; i < 32; i++) {
        if (buf[i] != 0) { all_zero = 0; break; }
    }
    ASSERT_TRUE(!all_zero);
}

TEST(random_different_each_call) {
    uint8_t buf1[32], buf2[32];
    ASSERT_TRUE(secure_random_bytes(buf1, sizeof(buf1)) == 0);
    ASSERT_TRUE(secure_random_bytes(buf2, sizeof(buf2)) == 0);

    /* Two calls should produce different results */
    ASSERT_TRUE(memcmp(buf1, buf2, 32) != 0);
}

TEST(random_distribution) {
    /* Generate 10000 bytes and check rough distribution */
    uint8_t buf[10000];
    ASSERT_TRUE(secure_random_bytes(buf, sizeof(buf)) == 0);

    int counts[256] = {0};
    for (int i = 0; i < 10000; i++) {
        counts[buf[i]]++;
    }

    /* Each byte value should appear roughly 39 times (10000/256) */
    /* Allow for statistical variance - each should be between 10 and 80 */
    int suspicious = 0;
    for (int i = 0; i < 256; i++) {
        if (counts[i] < 10 || counts[i] > 80) suspicious++;
    }
    /* Allow up to 5 outliers due to random variance */
    ASSERT_TRUE(suspicious < 5);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║         PHANTOMOS CRYPTOGRAPHIC FUNCTION TESTS                ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    printf("SHA-256 Tests (NIST vectors):\n");
    RUN_TEST(sha256_empty);
    RUN_TEST(sha256_hello);
    RUN_TEST(sha256_abc);

    printf("\nPBKDF2-SHA256 Tests:\n");
    RUN_TEST(pbkdf2_sha256_1);
    RUN_TEST(pbkdf2_sha256_2);
    RUN_TEST(pbkdf2_sha256_4096);

    printf("\nSecure Random Tests:\n");
    RUN_TEST(random_not_all_zeros);
    RUN_TEST(random_different_each_call);
    RUN_TEST(random_distribution);

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════════════════\n\n");

    return tests_failed > 0 ? 1 : 0;
}
