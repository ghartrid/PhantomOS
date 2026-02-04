/*
 * PhantomOS MusiKey Authentication System - Implementation
 *
 * Uses musical entropy for authentication: generates unique songs,
 * scrambles them with user keys, verifies by detecting musical structure.
 *
 * SECURITY UPGRADE: Now uses proper cryptographic primitives:
 * - SHA-256 for hashing (OpenSSL)
 * - AES-256-GCM for encryption (OpenSSL)
 * - PBKDF2 for key derivation (OpenSSL)
 * - /dev/urandom for entropy
 */

#include "phantom_musikey.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef HAVE_OPENSSL
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#define USE_OPENSSL_CRYPTO 1
#else
#define USE_OPENSSL_CRYPTO 0
#endif

/* Default configuration */
static musikey_config_t g_config = {
    .song_length = MUSIKEY_DEFAULT_LENGTH,
    .scramble_iterations = 100000,  /* Increased for PBKDF2 */
    .musicality_threshold = 0.7f,
    .max_failed_attempts = 5,
    .use_hardware_entropy = true,
    .preferred_scale = SCALE_PENTATONIC
};

static bool g_initialized = false;

/* Scale interval definitions (semitones from root) */
static const uint8_t SCALE_MAJOR_INTERVALS[] = {0, 2, 4, 5, 7, 9, 11};
static const uint8_t SCALE_MINOR_INTERVALS[] = {0, 2, 3, 5, 7, 8, 10};
static const uint8_t SCALE_PENTATONIC_INTERVALS[] = {0, 2, 4, 7, 9};
static const uint8_t SCALE_BLUES_INTERVALS[] = {0, 3, 5, 6, 7, 10};
static const uint8_t SCALE_DORIAN_INTERVALS[] = {0, 2, 3, 5, 7, 9, 10};
static const uint8_t SCALE_MIXOLYDIAN_INTERVALS[] = {0, 2, 4, 5, 7, 9, 10};

/* Secure random number generation using /dev/urandom */
static int secure_random_bytes(uint8_t *buffer, size_t len) {
#if USE_OPENSSL_CRYPTO
    if (RAND_bytes(buffer, len) == 1) {
        return 0;
    }
    /* Fallback to /dev/urandom */
#endif
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        /* Last resort: use time-based seed (NOT secure, but functional) */
        srand((unsigned int)(time(NULL) ^ clock()));
        for (size_t i = 0; i < len; i++) {
            buffer[i] = rand() & 0xFF;
        }
        return -1;
    }

    ssize_t bytes_read = read(fd, buffer, len);
    close(fd);

    return (bytes_read == (ssize_t)len) ? 0 : -1;
}

/* Secure random 64-bit integer */
static uint64_t secure_random_u64(void) {
    uint64_t value;
    secure_random_bytes((uint8_t*)&value, sizeof(value));
    return value;
}

/* SHA-256 hash function */
static void musikey_hash(const uint8_t *data, size_t len, uint8_t *output) {
#if USE_OPENSSL_CRYPTO
    SHA256(data, len, output);
#else
    /* Fallback: improved custom hash (still not as secure as SHA-256) */
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    for (size_t i = 0; i < len; i++) {
        uint32_t idx = i % 8;
        h[idx] = ((h[idx] << 5) | (h[idx] >> 27)) ^ data[i];
        h[(idx + 1) % 8] += h[idx] * 0x5bd1e995;
        h[(idx + 3) % 8] ^= ((h[idx] >> 11) | (h[idx] << 21));
    }

    /* Final mixing */
    for (int round = 0; round < 4; round++) {
        for (int i = 0; i < 8; i++) {
            h[i] ^= h[(i + 1) % 8] >> 13;
            h[i] *= 0xc2b2ae35;
            h[i] ^= h[i] >> 16;
        }
    }

    for (int i = 0; i < 8; i++) {
        output[i * 4 + 0] = (h[i] >> 24) & 0xFF;
        output[i * 4 + 1] = (h[i] >> 16) & 0xFF;
        output[i * 4 + 2] = (h[i] >> 8) & 0xFF;
        output[i * 4 + 3] = h[i] & 0xFF;
    }
#endif
}

/* PBKDF2-based key derivation */
static int musikey_derive_key(const uint8_t *password, size_t password_len,
                               const uint8_t *salt, size_t salt_len,
                               uint32_t iterations,
                               uint8_t *output, size_t output_len) {
#if USE_OPENSSL_CRYPTO
    if (PKCS5_PBKDF2_HMAC((const char*)password, password_len,
                          salt, salt_len, iterations,
                          EVP_sha256(), output_len, output) == 1) {
        return 0;
    }
    return -1;
#else
    /* Fallback: simplified PBKDF2-like derivation */
    uint8_t block[MUSIKEY_HASH_SIZE];
    uint8_t u[MUSIKEY_HASH_SIZE];

    for (size_t block_num = 0; block_num * MUSIKEY_HASH_SIZE < output_len; block_num++) {
        /* U1 = PRF(Password, Salt || INT(block_num + 1)) */
        uint8_t salt_block[MUSIKEY_SALT_SIZE + 4];
        memcpy(salt_block, salt, salt_len < MUSIKEY_SALT_SIZE ? salt_len : MUSIKEY_SALT_SIZE);
        salt_block[salt_len] = ((block_num + 1) >> 24) & 0xFF;
        salt_block[salt_len + 1] = ((block_num + 1) >> 16) & 0xFF;
        salt_block[salt_len + 2] = ((block_num + 1) >> 8) & 0xFF;
        salt_block[salt_len + 3] = (block_num + 1) & 0xFF;

        /* HMAC approximation */
        uint8_t inner[MUSIKEY_HASH_SIZE + MUSIKEY_SALT_SIZE + 4 + 256];
        memcpy(inner, password, password_len < 256 ? password_len : 256);
        memcpy(inner + (password_len < 256 ? password_len : 256), salt_block, salt_len + 4);
        musikey_hash(inner, (password_len < 256 ? password_len : 256) + salt_len + 4, u);
        memcpy(block, u, MUSIKEY_HASH_SIZE);

        /* Iterate */
        for (uint32_t i = 1; i < iterations; i++) {
            uint8_t prev[MUSIKEY_HASH_SIZE];
            memcpy(prev, u, MUSIKEY_HASH_SIZE);
            memcpy(inner, password, password_len < 256 ? password_len : 256);
            memcpy(inner + (password_len < 256 ? password_len : 256), prev, MUSIKEY_HASH_SIZE);
            musikey_hash(inner, (password_len < 256 ? password_len : 256) + MUSIKEY_HASH_SIZE, u);

            for (int j = 0; j < MUSIKEY_HASH_SIZE; j++) {
                block[j] ^= u[j];
            }
        }

        size_t copy_len = output_len - block_num * MUSIKEY_HASH_SIZE;
        if (copy_len > MUSIKEY_HASH_SIZE) copy_len = MUSIKEY_HASH_SIZE;
        memcpy(output + block_num * MUSIKEY_HASH_SIZE, block, copy_len);
    }

    return 0;
#endif
}

/* AES-256-GCM encryption */
static int musikey_encrypt(const uint8_t *plaintext, size_t plaintext_len,
                            const uint8_t *key, const uint8_t *iv,
                            uint8_t *ciphertext, uint8_t *tag) {
#if USE_OPENSSL_CRYPTO
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    int len, ciphertext_len;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len = len;

    if (EVP_EncryptFinal_ex(ctx, ciphertext + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len += len;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    EVP_CIPHER_CTX_free(ctx);
    return ciphertext_len;
#else
    /* Fallback: XOR stream cipher (less secure) */
    uint8_t keystream[MUSIKEY_HASH_SIZE];
    uint8_t block[MUSIKEY_HASH_SIZE + 12 + 4];

    memcpy(ciphertext, plaintext, plaintext_len);

    for (size_t offset = 0; offset < plaintext_len; offset += MUSIKEY_HASH_SIZE) {
        memset(block, 0, sizeof(block));
        memcpy(block, key, MUSIKEY_HASH_SIZE);
        memcpy(block + MUSIKEY_HASH_SIZE, iv, 12);
        block[MUSIKEY_HASH_SIZE + 12] = (offset >> 24) & 0xFF;
        block[MUSIKEY_HASH_SIZE + 13] = (offset >> 16) & 0xFF;
        block[MUSIKEY_HASH_SIZE + 14] = (offset >> 8) & 0xFF;
        block[MUSIKEY_HASH_SIZE + 15] = offset & 0xFF;

        musikey_hash(block, sizeof(block), keystream);

        size_t chunk = plaintext_len - offset;
        if (chunk > MUSIKEY_HASH_SIZE) chunk = MUSIKEY_HASH_SIZE;

        for (size_t i = 0; i < chunk; i++) {
            ciphertext[offset + i] ^= keystream[i];
        }
    }

    /* Generate tag (MAC) */
    uint8_t mac_input[MUSIKEY_HASH_SIZE + plaintext_len];
    memcpy(mac_input, key, MUSIKEY_HASH_SIZE);
    memcpy(mac_input + MUSIKEY_HASH_SIZE, ciphertext, plaintext_len);
    musikey_hash(mac_input, MUSIKEY_HASH_SIZE + plaintext_len, tag);

    return plaintext_len;
#endif
}

/* AES-256-GCM decryption */
static int musikey_decrypt(const uint8_t *ciphertext, size_t ciphertext_len,
                            const uint8_t *key, const uint8_t *iv,
                            const uint8_t *tag, uint8_t *plaintext) {
#if USE_OPENSSL_CRYPTO
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    int len, plaintext_len;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    plaintext_len = len;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (EVP_DecryptFinal_ex(ctx, plaintext + len, &len) != 1) {
        /* Tag verification failed - data tampered or wrong key */
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    return plaintext_len;
#else
    /* Fallback: XOR stream cipher with MAC verification */
    uint8_t expected_tag[MUSIKEY_HASH_SIZE];
    uint8_t mac_input[MUSIKEY_HASH_SIZE + ciphertext_len];
    memcpy(mac_input, key, MUSIKEY_HASH_SIZE);
    memcpy(mac_input + MUSIKEY_HASH_SIZE, ciphertext, ciphertext_len);
    musikey_hash(mac_input, MUSIKEY_HASH_SIZE + ciphertext_len, expected_tag);

    /* Constant-time comparison */
    int diff = 0;
    for (int i = 0; i < MUSIKEY_HASH_SIZE; i++) {
        diff |= expected_tag[i] ^ tag[i];
    }
    if (diff != 0) {
        return -1;  /* Tag mismatch */
    }

    /* Decrypt */
    uint8_t keystream[MUSIKEY_HASH_SIZE];
    uint8_t block[MUSIKEY_HASH_SIZE + 12 + 4];

    memcpy(plaintext, ciphertext, ciphertext_len);

    for (size_t offset = 0; offset < ciphertext_len; offset += MUSIKEY_HASH_SIZE) {
        memset(block, 0, sizeof(block));
        memcpy(block, key, MUSIKEY_HASH_SIZE);
        memcpy(block + MUSIKEY_HASH_SIZE, iv, 12);
        block[MUSIKEY_HASH_SIZE + 12] = (offset >> 24) & 0xFF;
        block[MUSIKEY_HASH_SIZE + 13] = (offset >> 16) & 0xFF;
        block[MUSIKEY_HASH_SIZE + 14] = (offset >> 8) & 0xFF;
        block[MUSIKEY_HASH_SIZE + 15] = offset & 0xFF;

        musikey_hash(block, sizeof(block), keystream);

        size_t chunk = ciphertext_len - offset;
        if (chunk > MUSIKEY_HASH_SIZE) chunk = MUSIKEY_HASH_SIZE;

        for (size_t i = 0; i < chunk; i++) {
            plaintext[offset + i] ^= keystream[i];
        }
    }

    return ciphertext_len;
#endif
}

musikey_error_t musikey_init(musikey_config_t *config) {
    if (config) {
        memcpy(&g_config, config, sizeof(musikey_config_t));
    }

#if USE_OPENSSL_CRYPTO
    /* Initialize OpenSSL */
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
#endif

    g_initialized = true;
    return MUSIKEY_OK;
}

void musikey_shutdown(void) {
#if USE_OPENSSL_CRYPTO
    EVP_cleanup();
    ERR_free_strings();
#endif
    memset(&g_config, 0, sizeof(g_config));
    g_initialized = false;
}

const uint8_t* musikey_get_scale_intervals(musikey_scale_t scale, uint8_t *count) {
    switch (scale) {
        case SCALE_MAJOR:
            *count = 7;
            return SCALE_MAJOR_INTERVALS;
        case SCALE_MINOR:
            *count = 7;
            return SCALE_MINOR_INTERVALS;
        case SCALE_PENTATONIC:
            *count = 5;
            return SCALE_PENTATONIC_INTERVALS;
        case SCALE_BLUES:
            *count = 6;
            return SCALE_BLUES_INTERVALS;
        case SCALE_DORIAN:
            *count = 7;
            return SCALE_DORIAN_INTERVALS;
        case SCALE_MIXOLYDIAN:
            *count = 7;
            return SCALE_MIXOLYDIAN_INTERVALS;
        default:
            *count = 12;
            return NULL; /* Chromatic - all notes */
    }
}

bool musikey_note_in_scale(uint8_t note, musikey_scale_t scale, uint8_t root) {
    if (scale == SCALE_CHROMATIC) return true;

    uint8_t count;
    const uint8_t *intervals = musikey_get_scale_intervals(scale, &count);
    if (!intervals) return true;

    uint8_t relative = (note - root + 12) % 12;
    for (uint8_t i = 0; i < count; i++) {
        if (intervals[i] == relative) return true;
    }
    return false;
}

float musikey_harmonic_ratio(uint8_t note1, uint8_t note2) {
    int interval = abs((int)note1 - (int)note2) % 12;

    /* Score based on harmonic consonance */
    switch (interval) {
        case 0: return 1.0f;   /* Unison */
        case 7: return 0.95f;  /* Perfect fifth */
        case 5: return 0.90f;  /* Perfect fourth */
        case 4: return 0.85f;  /* Major third */
        case 3: return 0.80f;  /* Minor third */
        case 9: return 0.75f;  /* Major sixth */
        case 8: return 0.70f;  /* Minor sixth */
        case 2: return 0.60f;  /* Major second */
        case 10: return 0.55f; /* Minor seventh */
        case 11: return 0.50f; /* Major seventh */
        case 1: return 0.30f;  /* Minor second (dissonant) */
        case 6: return 0.35f;  /* Tritone (dissonant) */
        default: return 0.5f;
    }
}

musikey_error_t musikey_generate_song(musikey_song_t *song, uint32_t length) {
    if (!g_initialized) return MUSIKEY_ERR_INVALID_INPUT;
    if (!song) return MUSIKEY_ERR_INVALID_INPUT;
    if (length < MUSIKEY_MIN_SONG_LENGTH) return MUSIKEY_ERR_INSUFFICIENT_ENTROPY;
    if (length > MUSIKEY_MAX_SONG_LENGTH) length = MUSIKEY_MAX_SONG_LENGTH;

    memset(song, 0, sizeof(musikey_song_t));

    /* Get secure random bytes for song generation */
    uint8_t random_bytes[512];
    secure_random_bytes(random_bytes, sizeof(random_bytes));
    int random_idx = 0;

    /* Choose musical parameters */
    song->scale = g_config.preferred_scale;
    song->root_note = random_bytes[random_idx++] % 12;
    song->tempo = 80 + (random_bytes[random_idx++] % 80);
    song->time_sig.beats_per_measure = 4;
    song->time_sig.beat_unit = 4;

    uint8_t scale_count;
    const uint8_t *scale_intervals = musikey_get_scale_intervals(song->scale, &scale_count);

    /* Generate notes with musical coherence */
    uint8_t current_note = 48 + song->root_note;
    uint16_t current_time = 0;
    uint16_t beat_duration = 60000 / song->tempo;

    for (uint32_t i = 0; i < length; i++) {
        musikey_event_t *event = &song->events[i];

        /* Melodic movement - prefer stepwise motion with occasional leaps */
        int movement = ((int)(random_bytes[random_idx++ % 512] % 5)) - 2;
        if (random_bytes[random_idx++ % 512] % 8 == 0) {
            movement = ((int)(random_bytes[random_idx++ % 512] % 9)) - 4;
        }

        /* Move within scale */
        if (scale_intervals) {
            int scale_pos = 0;
            for (uint8_t j = 0; j < scale_count; j++) {
                if ((current_note % 12) == ((song->root_note + scale_intervals[j]) % 12)) {
                    scale_pos = j;
                    break;
                }
            }
            scale_pos = (scale_pos + movement + scale_count * 10) % scale_count;
            int octave = current_note / 12;
            if (movement > 2) octave++;
            if (movement < -2) octave--;
            if (octave < 3) octave = 3;
            if (octave > 6) octave = 6;
            current_note = octave * 12 + song->root_note + scale_intervals[scale_pos];
        } else {
            current_note = (current_note + movement + 128) % 128;
        }

        event->note = current_note;
        event->velocity = 60 + (random_bytes[random_idx++ % 512] % 60);

        /* Rhythmic variety */
        uint32_t rhythm_choice = random_bytes[random_idx++ % 512] % 16;
        if (rhythm_choice < 4) {
            event->duration = beat_duration / 4;
        } else if (rhythm_choice < 10) {
            event->duration = beat_duration / 2;
        } else if (rhythm_choice < 14) {
            event->duration = beat_duration;
        } else {
            event->duration = beat_duration * 2;
        }

        event->timestamp = current_time;
        current_time += event->duration;
    }

    song->event_count = length;
    song->total_duration = current_time;
    song->entropy_bits = musikey_calculate_entropy(song);

    return MUSIKEY_OK;
}

uint32_t musikey_calculate_entropy(const musikey_song_t *song) {
    if (!song || song->event_count == 0) return 0;

    uint32_t note_counts[128] = {0};
    uint32_t duration_counts[8] = {0};

    for (uint32_t i = 0; i < song->event_count; i++) {
        note_counts[song->events[i].note % 128]++;
        duration_counts[song->events[i].duration % 8]++;
    }

    float note_entropy = 0.0f;
    float duration_entropy = 0.0f;
    float n = (float)song->event_count;

    for (int i = 0; i < 128; i++) {
        if (note_counts[i] > 0) {
            float p = note_counts[i] / n;
            note_entropy -= p * log2f(p);
        }
    }

    for (int i = 0; i < 8; i++) {
        if (duration_counts[i] > 0) {
            float p = duration_counts[i] / n;
            duration_entropy -= p * log2f(p);
        }
    }

    float total_entropy = (note_entropy + duration_entropy) * song->event_count / 4;
    return (uint32_t)total_entropy;
}

musikey_error_t musikey_scramble(const musikey_song_t *song,
                                  const uint8_t *key, size_t key_len,
                                  musikey_scrambled_t *output) {
    if (!song || !key || !output || key_len == 0) {
        return MUSIKEY_ERR_INVALID_INPUT;
    }

    memset(output, 0, sizeof(musikey_scrambled_t));

    /* Generate random salt and IV */
    secure_random_bytes(output->salt, MUSIKEY_SALT_SIZE);
    uint8_t iv[12];
    secure_random_bytes(iv, 12);
    memcpy(output->iv, iv, 12);

    /* Derive encryption key using PBKDF2 */
    uint8_t derived_key[32];  /* 256 bits for AES-256 */
    output->scramble_iterations = g_config.scramble_iterations;

    if (musikey_derive_key(key, key_len, output->salt, MUSIKEY_SALT_SIZE,
                           output->scramble_iterations, derived_key, 32) != 0) {
        return MUSIKEY_ERR_CRYPTO;
    }

    /* Prepare plaintext (song events) */
    uint8_t plaintext[MUSIKEY_MAX_SONG_LENGTH * sizeof(musikey_event_t)];
    size_t plaintext_len = song->event_count * sizeof(musikey_event_t);
    memcpy(plaintext, song->events, plaintext_len);

    /* Encrypt with AES-256-GCM */
    int encrypted_len = musikey_encrypt(plaintext, plaintext_len,
                                         derived_key, iv,
                                         output->scrambled_data, output->auth_tag);

    if (encrypted_len < 0) {
        memset(derived_key, 0, sizeof(derived_key));
        return MUSIKEY_ERR_SCRAMBLE_FAILED;
    }

    output->data_size = encrypted_len;

    /* Store hash of original for additional verification */
    musikey_hash((const uint8_t*)song->events, plaintext_len, output->verification_hash);

    /* Clear sensitive data */
    memset(derived_key, 0, sizeof(derived_key));
    memset(plaintext, 0, sizeof(plaintext));

    return MUSIKEY_OK;
}

musikey_error_t musikey_descramble(const musikey_scrambled_t *scrambled,
                                    const uint8_t *key, size_t key_len,
                                    musikey_song_t *output) {
    if (!scrambled || !key || !output || key_len == 0) {
        return MUSIKEY_ERR_INVALID_INPUT;
    }

    memset(output, 0, sizeof(musikey_song_t));

    /* Derive decryption key using PBKDF2 */
    uint8_t derived_key[32];
    if (musikey_derive_key(key, key_len, scrambled->salt, MUSIKEY_SALT_SIZE,
                           scrambled->scramble_iterations, derived_key, 32) != 0) {
        return MUSIKEY_ERR_CRYPTO;
    }

    /* Decrypt with AES-256-GCM */
    uint8_t plaintext[MUSIKEY_MAX_SONG_LENGTH * sizeof(musikey_event_t)];
    int decrypted_len = musikey_decrypt(scrambled->scrambled_data, scrambled->data_size,
                                         derived_key, scrambled->iv,
                                         scrambled->auth_tag, plaintext);

    memset(derived_key, 0, sizeof(derived_key));

    if (decrypted_len < 0) {
        return MUSIKEY_ERR_DESCRAMBLE_FAILED;
    }

    /* Verify hash */
    uint8_t hash[MUSIKEY_HASH_SIZE];
    musikey_hash(plaintext, decrypted_len, hash);

    /* Constant-time comparison */
    int diff = 0;
    for (int i = 0; i < MUSIKEY_HASH_SIZE; i++) {
        diff |= hash[i] ^ scrambled->verification_hash[i];
    }

    if (diff != 0) {
        memset(plaintext, 0, sizeof(plaintext));
        return MUSIKEY_ERR_DESCRAMBLE_FAILED;
    }

    /* Restore song structure */
    output->event_count = decrypted_len / sizeof(musikey_event_t);
    memcpy(output->events, plaintext, decrypted_len);

    memset(plaintext, 0, sizeof(plaintext));
    return MUSIKEY_OK;
}

musikey_error_t musikey_analyze(const musikey_song_t *song,
                                 musikey_analysis_t *analysis) {
    if (!song || !analysis) return MUSIKEY_ERR_INVALID_INPUT;

    memset(analysis, 0, sizeof(musikey_analysis_t));

    if (song->event_count < 4) {
        analysis->is_valid_music = false;
        return MUSIKEY_OK;
    }

    float harmonic_sum = 0.0f;
    float melodic_sum = 0.0f;
    float rhythm_regularity = 0.0f;
    int scale_hits = 0;

    for (uint32_t i = 1; i < song->event_count; i++) {
        harmonic_sum += musikey_harmonic_ratio(song->events[i-1].note,
                                               song->events[i].note);

        int interval = abs((int)song->events[i].note - (int)song->events[i-1].note);
        if (interval <= 2) melodic_sum += 1.0f;
        else if (interval <= 4) melodic_sum += 0.7f;
        else if (interval <= 7) melodic_sum += 0.4f;
        else melodic_sum += 0.2f;

        if (musikey_note_in_scale(song->events[i].note, song->scale, song->root_note)) {
            scale_hits++;
        }
    }

    uint16_t durations[MUSIKEY_MAX_SONG_LENGTH];
    for (uint32_t i = 0; i < song->event_count; i++) {
        durations[i] = song->events[i].duration;
    }

    for (uint32_t pattern_len = 2; pattern_len <= 8; pattern_len++) {
        int matches = 0;
        for (uint32_t i = pattern_len; i < song->event_count; i++) {
            if (durations[i] == durations[i - pattern_len]) {
                matches++;
            }
        }
        float pattern_score = (float)matches / (song->event_count - pattern_len);
        if (pattern_score > rhythm_regularity) {
            rhythm_regularity = pattern_score;
        }
    }

    analysis->harmonic_score = harmonic_sum / (song->event_count - 1);
    analysis->melody_score = melodic_sum / (song->event_count - 1);
    analysis->rhythm_score = rhythm_regularity;
    analysis->scale_adherence = (float)scale_hits / (song->event_count - 1);

    analysis->overall_musicality = (analysis->harmonic_score * 0.3f +
                                    analysis->melody_score * 0.3f +
                                    analysis->rhythm_score * 0.2f +
                                    analysis->scale_adherence * 0.2f);

    analysis->is_valid_music = (analysis->overall_musicality >= g_config.musicality_threshold);

    return MUSIKEY_OK;
}

musikey_error_t musikey_enroll(const char *user_id,
                                const uint8_t *key, size_t key_len,
                                musikey_credential_t *credential) {
    if (!user_id || !key || !credential || key_len == 0) {
        return MUSIKEY_ERR_INVALID_INPUT;
    }

    memset(credential, 0, sizeof(musikey_credential_t));
    strncpy(credential->user_id, user_id, sizeof(credential->user_id) - 1);

    musikey_song_t song;
    musikey_error_t err = musikey_generate_song(&song, g_config.song_length);
    if (err != MUSIKEY_OK) return err;

    musikey_analysis_t analysis;
    err = musikey_analyze(&song, &analysis);
    if (err != MUSIKEY_OK) return err;
    if (!analysis.is_valid_music) return MUSIKEY_ERR_NOT_MUSIC;

    err = musikey_scramble(&song, key, key_len, &credential->scrambled_song);
    if (err != MUSIKEY_OK) return err;

    credential->created_timestamp = (uint64_t)time(NULL);
    credential->last_auth_timestamp = 0;
    credential->auth_attempts = 0;
    credential->failed_attempts = 0;
    credential->locked = false;

    /* Clear original song from memory */
    memset(&song, 0, sizeof(song));

    return MUSIKEY_OK;
}

musikey_error_t musikey_authenticate(musikey_credential_t *credential,
                                      const uint8_t *key, size_t key_len) {
    if (!credential || !key || key_len == 0) {
        return MUSIKEY_ERR_INVALID_INPUT;
    }

    if (credential->locked) {
        return MUSIKEY_ERR_LOCKED;
    }

    credential->auth_attempts++;

    musikey_song_t recovered_song;
    musikey_error_t err = musikey_descramble(&credential->scrambled_song,
                                              key, key_len, &recovered_song);

    if (err != MUSIKEY_OK) {
        credential->failed_attempts++;
        if (credential->failed_attempts >= g_config.max_failed_attempts) {
            credential->locked = true;
        }
        memset(&recovered_song, 0, sizeof(recovered_song));
        return MUSIKEY_ERR_AUTH_FAILED;
    }

    musikey_analysis_t analysis;
    err = musikey_analyze(&recovered_song, &analysis);

    memset(&recovered_song, 0, sizeof(recovered_song));

    if (err != MUSIKEY_OK || !analysis.is_valid_music) {
        credential->failed_attempts++;
        if (credential->failed_attempts >= g_config.max_failed_attempts) {
            credential->locked = true;
        }
        return MUSIKEY_ERR_AUTH_FAILED;
    }

    credential->failed_attempts = 0;
    credential->last_auth_timestamp = (uint64_t)time(NULL);

    return MUSIKEY_OK;
}

musikey_error_t musikey_reset_lockout(musikey_credential_t *credential) {
    if (!credential) return MUSIKEY_ERR_INVALID_INPUT;

    credential->locked = false;
    credential->failed_attempts = 0;

    return MUSIKEY_OK;
}

musikey_error_t musikey_add_entropy(const uint8_t *data, size_t len) {
    if (!data || len == 0) return MUSIKEY_ERR_INVALID_INPUT;

#if USE_OPENSSL_CRYPTO
    RAND_seed(data, len);
#endif

    return MUSIKEY_OK;
}

musikey_error_t musikey_render_audio(const musikey_song_t *song,
                                      int16_t *buffer, size_t buffer_samples,
                                      uint32_t sample_rate) {
    if (!song || !buffer || buffer_samples == 0) {
        return MUSIKEY_ERR_INVALID_INPUT;
    }

    memset(buffer, 0, buffer_samples * sizeof(int16_t));

    for (uint32_t i = 0; i < song->event_count; i++) {
        const musikey_event_t *event = &song->events[i];

        float freq = 440.0f * powf(2.0f, (event->note - 69) / 12.0f);

        uint32_t start_sample = (event->timestamp * sample_rate) / 1000;
        uint32_t duration_samples = (event->duration * sample_rate) / 1000;
        uint32_t end_sample = start_sample + duration_samples;

        if (end_sample > buffer_samples) end_sample = buffer_samples;

        float amplitude = (event->velocity / 127.0f) * 16000.0f;

        for (uint32_t s = start_sample; s < end_sample; s++) {
            float t = (float)(s - start_sample) / sample_rate;
            float envelope = 1.0f;

            uint32_t rel_sample = s - start_sample;
            if (rel_sample < duration_samples / 10) {
                envelope = (float)rel_sample / (duration_samples / 10);
            } else if (rel_sample > duration_samples * 9 / 10) {
                envelope = (float)(duration_samples - rel_sample) / (duration_samples / 10);
            }

            float sample = amplitude * envelope * sinf(2.0f * 3.14159f * freq * t);
            buffer[s] += (int16_t)sample;
        }
    }

    return MUSIKEY_OK;
}

musikey_error_t musikey_credential_export(const musikey_credential_t *cred,
                                           uint8_t *buffer, size_t *size) {
    if (!cred || !size) return MUSIKEY_ERR_INVALID_INPUT;

    size_t needed = sizeof(musikey_credential_t);
    if (!buffer) {
        *size = needed;
        return MUSIKEY_OK;
    }

    if (*size < needed) return MUSIKEY_ERR_MEMORY;

    memcpy(buffer, cred, needed);
    *size = needed;

    return MUSIKEY_OK;
}

musikey_error_t musikey_credential_import(musikey_credential_t *cred,
                                           const uint8_t *buffer, size_t size) {
    if (!cred || !buffer) return MUSIKEY_ERR_INVALID_INPUT;
    if (size < sizeof(musikey_credential_t)) return MUSIKEY_ERR_INVALID_INPUT;

    memcpy(cred, buffer, sizeof(musikey_credential_t));

    return MUSIKEY_OK;
}
