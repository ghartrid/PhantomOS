/*
 * PhantomOS BioSense Authentication Driver Implementation
 *
 * Hardware abstraction layer for biometric blood/vein sensors
 *
 * Copyright (c) 2025 PhantomOS Project
 * License: CC BY-NC-SA 4.0
 */

#include "phantom_biosense.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#ifdef HAVE_OPENSSL
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#else
/* Fallback stubs */
static void sha256_hash(const void *data, size_t len, uint8_t *out) {
    uint32_t h = 0x811c9dc5;
    const uint8_t *p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x01000193;
    }
    memset(out, 0, 32);
    memcpy(out, &h, 4);
}
#endif

/* Maximum registered drivers */
#define MAX_DRIVERS 16

/* Driver registry */
static struct {
    const biosense_driver_ops_t *ops[MAX_DRIVERS];
    int count;
    bool initialized;
    biosense_config_t config;
} g_biosense = {0};

/* Internal driver context */
struct biosense_driver {
    const biosense_driver_ops_t *ops;
    biosense_state_t state;
    biosense_device_info_t info;
    int fd;                     /* File descriptor for device */
    void *driver_data;          /* Driver-specific data */
    uint32_t scan_sequence;
    bool async_pending;
    biosense_scan_callback_t async_callback;
    void *async_userdata;
};

/* Error strings */
static const char *error_strings[] = {
    "Success",
    "No device found",
    "Initialization failed",
    "Scan failed",
    "No finger detected",
    "Poor scan quality",
    "Operation timed out",
    "Calibration required",
    "Template mismatch",
    "Memory allocation failed",
    "Permission denied",
    "Account locked",
    "Cryptographic error"
};

/* State strings */
static const char *state_strings[] = {
    "Disconnected",
    "Initializing",
    "Ready",
    "Scanning",
    "Processing",
    "Error",
    "Calibrating"
};

/*
 * Utility: Secure random bytes
 */
static int secure_random(uint8_t *buf, size_t len) {
#ifdef HAVE_OPENSSL
    if (RAND_bytes(buf, len) == 1) {
        return 0;
    }
#endif
    /* Fallback to /dev/urandom */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    ssize_t n = read(fd, buf, len);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -1;
}

/*
 * Utility: Get current timestamp in ms
 */
static uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*
 * Utility: Constant-time comparison
 */
static int secure_compare(const void *a, const void *b, size_t len) {
    const volatile uint8_t *pa = a;
    const volatile uint8_t *pb = b;
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= pa[i] ^ pb[i];
    }
    return diff == 0 ? 0 : -1;
}

/*
 * Derive key from password using PBKDF2
 */
static int derive_key(const uint8_t *password, size_t pass_len,
                      const uint8_t *salt, size_t salt_len,
                      uint8_t *key_out, size_t key_len) {
#ifdef HAVE_OPENSSL
    if (PKCS5_PBKDF2_HMAC((const char *)password, pass_len,
                          salt, salt_len,
                          100000,  /* iterations */
                          EVP_sha256(),
                          key_len, key_out) == 1) {
        return 0;
    }
    return -1;
#else
    /* Simple fallback - NOT secure, just for compilation */
    memset(key_out, 0, key_len);
    for (size_t i = 0; i < pass_len && i < key_len; i++) {
        key_out[i] = password[i] ^ salt[i % salt_len];
    }
    return 0;
#endif
}

/*
 * Encrypt data using AES-256-GCM
 */
static int encrypt_data(const uint8_t *plaintext, size_t plain_len,
                        const uint8_t *key,
                        uint8_t *iv, uint8_t *ciphertext,
                        uint8_t *auth_tag) {
#ifdef HAVE_OPENSSL
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    /* Generate random IV */
    if (secure_random(iv, 12) != 0) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    int ret = -1;
    int len;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, iv) == 1 &&
        EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plain_len) == 1) {
        int final_len;
        if (EVP_EncryptFinal_ex(ctx, ciphertext + len, &final_len) == 1 &&
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, auth_tag) == 1) {
            ret = 0;
        }
    }

    EVP_CIPHER_CTX_free(ctx);
    return ret;
#else
    /* Fallback XOR "encryption" - NOT secure */
    secure_random(iv, 12);
    for (size_t i = 0; i < plain_len; i++) {
        ciphertext[i] = plaintext[i] ^ key[i % 32] ^ iv[i % 12];
    }
    memset(auth_tag, 0xAB, 16);
    return 0;
#endif
}

/*
 * Decrypt data using AES-256-GCM
 */
static int decrypt_data(const uint8_t *ciphertext, size_t cipher_len,
                        const uint8_t *key,
                        const uint8_t *iv, const uint8_t *auth_tag,
                        uint8_t *plaintext) {
#ifdef HAVE_OPENSSL
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    int ret = -1;
    int len;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, iv) == 1 &&
        EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, cipher_len) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)auth_tag) == 1) {
        int final_len;
        if (EVP_DecryptFinal_ex(ctx, plaintext + len, &final_len) == 1) {
            ret = 0;
        }
    }

    EVP_CIPHER_CTX_free(ctx);
    return ret;
#else
    /* Fallback XOR */
    for (size_t i = 0; i < cipher_len; i++) {
        plaintext[i] = ciphertext[i] ^ key[i % 32] ^ iv[i % 12];
    }
    return 0;
#endif
}

/*
 * Hash data using SHA-256
 */
static void hash_data(const void *data, size_t len, uint8_t *hash_out) {
#ifdef HAVE_OPENSSL
    SHA256(data, len, hash_out);
#else
    sha256_hash(data, len, hash_out);
#endif
}

/*
 * Driver Registration
 */

biosense_error_t biosense_register_driver(const biosense_driver_ops_t *ops) {
    if (!ops || !ops->name) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    if (g_biosense.count >= MAX_DRIVERS) {
        return BIOSENSE_ERR_MEMORY;
    }

    /* Check for duplicate */
    for (int i = 0; i < g_biosense.count; i++) {
        if (strcmp(g_biosense.ops[i]->name, ops->name) == 0) {
            return BIOSENSE_ERR_INIT_FAILED;
        }
    }

    g_biosense.ops[g_biosense.count++] = ops;
    return BIOSENSE_OK;
}

void biosense_unregister_driver(const char *name) {
    for (int i = 0; i < g_biosense.count; i++) {
        if (strcmp(g_biosense.ops[i]->name, name) == 0) {
            /* Shift remaining drivers */
            for (int j = i; j < g_biosense.count - 1; j++) {
                g_biosense.ops[j] = g_biosense.ops[j + 1];
            }
            g_biosense.count--;
            return;
        }
    }
}

/*
 * Core API Implementation
 */

biosense_error_t biosense_init(biosense_config_t *config) {
    if (g_biosense.initialized) {
        return BIOSENSE_OK;
    }

    /* Set defaults */
    g_biosense.config.match_threshold = 0.85f;
    g_biosense.config.liveness_threshold = 0.90f;
    g_biosense.config.quality_threshold = 0.70f;
    g_biosense.config.max_failed_attempts = 5;
    g_biosense.config.lockout_duration_sec = 300;
    g_biosense.config.require_liveness = true;
    g_biosense.config.store_raw_images = false;

    /* Apply user config */
    if (config) {
        if (config->match_threshold > 0) {
            g_biosense.config.match_threshold = config->match_threshold;
        }
        if (config->liveness_threshold > 0) {
            g_biosense.config.liveness_threshold = config->liveness_threshold;
        }
        if (config->quality_threshold > 0) {
            g_biosense.config.quality_threshold = config->quality_threshold;
        }
        if (config->max_failed_attempts > 0) {
            g_biosense.config.max_failed_attempts = config->max_failed_attempts;
        }
        g_biosense.config.require_liveness = config->require_liveness;
        g_biosense.config.store_raw_images = config->store_raw_images;
        if (config->device_path[0]) {
            strncpy(g_biosense.config.device_path, config->device_path,
                    sizeof(g_biosense.config.device_path) - 1);
            g_biosense.config.device_path[sizeof(g_biosense.config.device_path) - 1] = '\0';
        }
    }

    g_biosense.initialized = true;
    return BIOSENSE_OK;
}

void biosense_shutdown(void) {
    g_biosense.initialized = false;
    g_biosense.count = 0;
}

biosense_error_t biosense_open(const char *device_path,
                                biosense_driver_t **driver) {
    if (!driver) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    /* Allocate driver context */
    biosense_driver_t *drv = calloc(1, sizeof(biosense_driver_t));
    if (!drv) {
        return BIOSENSE_ERR_MEMORY;
    }

    drv->state = BIOSENSE_STATE_INITIALIZING;
    drv->fd = -1;

    /* Try each registered driver */
    for (int i = 0; i < g_biosense.count; i++) {
        if (g_biosense.ops[i]->probe) {
            drv->ops = g_biosense.ops[i];
            if (g_biosense.ops[i]->probe(drv, device_path) == 0) {
                /* Get device info */
                if (drv->ops->get_info) {
                    drv->ops->get_info(drv, &drv->info);
                }
                drv->state = BIOSENSE_STATE_READY;
                *driver = drv;
                return BIOSENSE_OK;
            }
        }
    }

    /* No driver matched - create simulated device for testing */
    drv->state = BIOSENSE_STATE_READY;
    drv->info.type = BIOSENSE_TYPE_VEIN_NIR;
    drv->info.connection = BIOSENSE_CONN_USB;
    strncpy(drv->info.vendor, "PhantomOS", sizeof(drv->info.vendor) - 1);
    strncpy(drv->info.model, "BioSense Simulator", sizeof(drv->info.model) - 1);
    strncpy(drv->info.serial, "SIM-001", sizeof(drv->info.serial) - 1);
    strncpy(drv->info.firmware, "1.0.0", sizeof(drv->info.firmware) - 1);
    drv->info.image_width = 128;
    drv->info.image_height = 128;
    drv->info.capabilities = BIOSENSE_CAP_VEIN_PATTERN |
                              BIOSENSE_CAP_LIVENESS |
                              BIOSENSE_CAP_ENCRYPTION;

    *driver = drv;
    return BIOSENSE_OK;
}

void biosense_close(biosense_driver_t *driver) {
    if (!driver) return;

    if (driver->ops && driver->ops->disconnect) {
        driver->ops->disconnect(driver);
    }

    if (driver->fd >= 0) {
        close(driver->fd);
    }

    free(driver);
}

biosense_error_t biosense_get_info(biosense_driver_t *driver,
                                    biosense_device_info_t *info) {
    if (!driver || !info) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    *info = driver->info;
    return BIOSENSE_OK;
}

biosense_state_t biosense_get_state(biosense_driver_t *driver) {
    return driver ? driver->state : BIOSENSE_STATE_DISCONNECTED;
}

/*
 * Vein Pattern Analysis
 */

/* Simulate vein pattern extraction from "scan" */
static void simulate_vein_scan(biosense_vein_data_t *data) {
    /* Generate random but consistent vein-like pattern */
    data->point_count = 0;

    /* Create bifurcation points in a vein-like pattern */
    for (int i = 0; i < 50 + (rand() % 50); i++) {
        if (data->point_count >= BIOSENSE_VEIN_MAX_POINTS) break;

        int idx = data->point_count++;
        data->points[idx].x = rand() % 128;
        data->points[idx].y = rand() % 128;
        data->points[idx].angle = rand() % 256;
        data->points[idx].type = rand() % 4;
    }

    data->pattern_complexity = 0.75f + (float)(rand() % 250) / 1000.0f;
    data->entropy_bits = 80 + (rand() % 40);
}

biosense_error_t biosense_scan_vein(biosense_driver_t *driver,
                                     biosense_scan_opts_t *opts,
                                     biosense_vein_data_t *data,
                                     biosense_quality_t *quality) {
    if (!driver || !data) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    driver->state = BIOSENSE_STATE_SCANNING;

    /* Use hardware if available */
    if (driver->ops && driver->ops->start_scan) {
        int ret = driver->ops->start_scan(driver);
        if (ret != 0) {
            driver->state = BIOSENSE_STATE_ERROR;
            return BIOSENSE_ERR_SCAN_FAILED;
        }

        /* Wait for scan completion */
        uint32_t timeout = opts ? opts->timeout_ms : 5000;
        (void)timeout; /* Would implement actual timeout */

        /* Get scan data */
        if (driver->ops->get_scan_data) {
            size_t size = sizeof(biosense_vein_data_t);
            driver->ops->get_scan_data(driver, data, &size);
        }

        driver->ops->stop_scan(driver);
    } else {
        /* Simulate scan */
        memset(data, 0, sizeof(*data));
        simulate_vein_scan(data);
    }

    driver->state = BIOSENSE_STATE_PROCESSING;

    /* Calculate quality metrics */
    if (quality) {
        quality->clarity = 0.85f + (float)(rand() % 150) / 1000.0f;
        quality->coverage = 0.90f + (float)(rand() % 100) / 1000.0f;
        quality->stability = 0.88f + (float)(rand() % 120) / 1000.0f;
        quality->confidence = (quality->clarity + quality->coverage +
                               quality->stability) / 3.0f;
        quality->is_acceptable = quality->confidence >=
                                  g_biosense.config.quality_threshold;
    }

    driver->state = BIOSENSE_STATE_READY;
    driver->scan_sequence++;

    return BIOSENSE_OK;
}

biosense_error_t biosense_scan_blood(biosense_driver_t *driver,
                                      biosense_scan_opts_t *opts,
                                      biosense_blood_data_t *data,
                                      biosense_quality_t *quality) {
    if (!driver || !data) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    (void)opts;
    driver->state = BIOSENSE_STATE_SCANNING;

    memset(data, 0, sizeof(*data));

    /* Simulate blood chemistry readings */
    data->oxygen_saturation = 95.0f + (float)(rand() % 50) / 10.0f;
    data->heart_rate = 60.0f + (float)(rand() % 400) / 10.0f;
    data->glucose_level = 80.0f + (float)(rand() % 400) / 10.0f;
    data->hemoglobin = 12.0f + (float)(rand() % 40) / 10.0f;
    data->timestamp = get_timestamp_ms();

    /* Generate spectral signature */
    for (int i = 0; i < 64; i++) {
        data->spectral_signature[i] = rand() % 65536;
    }

    if (quality) {
        quality->clarity = 0.90f;
        quality->coverage = 0.95f;
        quality->stability = 0.92f;
        quality->confidence = 0.92f;
        quality->is_acceptable = true;
    }

    driver->state = BIOSENSE_STATE_READY;
    return BIOSENSE_OK;
}

biosense_error_t biosense_check_liveness(biosense_driver_t *driver,
                                          float *score) {
    if (!driver || !score) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    /* Liveness detection checks:
     * - Blood flow (pulse detection)
     * - Temperature variation
     * - Movement/tremor
     * - Spectral properties
     */

    /* Simulate liveness check */
    *score = 0.92f + (float)(rand() % 80) / 1000.0f;

    return BIOSENSE_OK;
}

/*
 * Template API
 */

biosense_error_t biosense_enroll(biosense_driver_t *driver,
                                  const char *user_id,
                                  const uint8_t *password, size_t pass_len,
                                  biosense_scan_opts_t *opts,
                                  biosense_template_t *template_out) {
    if (!driver || !user_id || !password || !template_out) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    if (pass_len == 0 || pass_len > 256) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    memset(template_out, 0, sizeof(*template_out));

    /* Perform initial scan */
    biosense_vein_data_t vein_data;
    biosense_quality_t quality;

    biosense_error_t err = biosense_scan_vein(driver, opts, &vein_data, &quality);
    if (err != BIOSENSE_OK) {
        return err;
    }

    if (!quality.is_acceptable) {
        return BIOSENSE_ERR_POOR_QUALITY;
    }

    /* Check liveness */
    if (g_biosense.config.require_liveness) {
        float liveness;
        err = biosense_check_liveness(driver, &liveness);
        if (err != BIOSENSE_OK) {
            return err;
        }
        if (liveness < g_biosense.config.liveness_threshold) {
            return BIOSENSE_ERR_SCAN_FAILED;
        }
        template_out->liveness_score = liveness;
    }

    /* Generate salt */
    if (secure_random(template_out->salt, 16) != 0) {
        return BIOSENSE_ERR_CRYPTO;
    }

    /* Derive encryption key from password */
    uint8_t key[32];
    if (derive_key(password, pass_len, template_out->salt, 16, key, 32) != 0) {
        return BIOSENSE_ERR_CRYPTO;
    }

    /* Serialize vein data */
    size_t data_size = sizeof(vein_data);
    if (data_size > BIOSENSE_TEMPLATE_MAX_SIZE) {
        return BIOSENSE_ERR_MEMORY;
    }

    /* Hash original data for verification */
    hash_data(&vein_data, data_size, template_out->verification_hash);

    /* Encrypt vein data */
    if (encrypt_data((uint8_t *)&vein_data, data_size, key,
                     template_out->iv,
                     template_out->encrypted_data,
                     template_out->auth_tag) != 0) {
        memset(key, 0, sizeof(key));
        return BIOSENSE_ERR_CRYPTO;
    }

    /* Clear key from memory */
    memset(key, 0, sizeof(key));

    /* Fill template metadata */
    template_out->version = BIOSENSE_TEMPLATE_VERSION;
    template_out->type = driver->info.type;
    template_out->data_size = data_size;
    strncpy(template_out->user_id, user_id, sizeof(template_out->user_id) - 1);
    template_out->created_timestamp = get_timestamp_ms();
    template_out->verify_count = 0;
    template_out->failed_count = 0;
    template_out->is_locked = false;

    return BIOSENSE_OK;
}

biosense_error_t biosense_verify(biosense_driver_t *driver,
                                  biosense_template_t *template,
                                  const uint8_t *password, size_t pass_len,
                                  biosense_scan_opts_t *opts,
                                  biosense_match_result_t *result) {
    if (!driver || !template || !password || !result) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    memset(result, 0, sizeof(*result));

    /* Check lockout */
    if (template->is_locked) {
        return BIOSENSE_ERR_LOCKED;
    }

    /* Derive key from password */
    uint8_t key[32];
    if (derive_key(password, pass_len, template->salt, 16, key, 32) != 0) {
        return BIOSENSE_ERR_CRYPTO;
    }

    /* Decrypt stored template */
    biosense_vein_data_t stored_data;
    if (decrypt_data(template->encrypted_data, template->data_size,
                     key, template->iv, template->auth_tag,
                     (uint8_t *)&stored_data) != 0) {
        memset(key, 0, sizeof(key));
        template->failed_count++;

        if (template->failed_count >= g_biosense.config.max_failed_attempts) {
            template->is_locked = true;
            return BIOSENSE_ERR_LOCKED;
        }
        return BIOSENSE_ERR_CRYPTO;
    }

    memset(key, 0, sizeof(key));

    /* Verify hash */
    uint8_t check_hash[BIOSENSE_HASH_SIZE];
    hash_data(&stored_data, sizeof(stored_data), check_hash);
    if (secure_compare(check_hash, template->verification_hash,
                       BIOSENSE_HASH_SIZE) != 0) {
        template->failed_count++;
        return BIOSENSE_ERR_TEMPLATE_MISMATCH;
    }

    /* Scan current biometric */
    biosense_vein_data_t current_data;
    biosense_quality_t quality;

    uint64_t start_time = get_timestamp_ms();
    biosense_error_t err = biosense_scan_vein(driver, opts, &current_data, &quality);
    if (err != BIOSENSE_OK) {
        return err;
    }

    result->quality = quality;

    /* Check liveness */
    if (g_biosense.config.require_liveness) {
        err = biosense_check_liveness(driver, &result->liveness_score);
        if (err != BIOSENSE_OK) {
            return err;
        }
        result->is_live = result->liveness_score >=
                          g_biosense.config.liveness_threshold;

        if (!result->is_live) {
            template->failed_count++;
            return BIOSENSE_ERR_SCAN_FAILED;
        }
    } else {
        result->is_live = true;
        result->liveness_score = 1.0f;
    }

    /* Compare patterns */
    result->similarity = biosense_compare_patterns(&stored_data, &current_data);
    result->match_time_ms = (uint32_t)(get_timestamp_ms() - start_time);

    /* Check threshold */
    result->is_match = result->similarity >= g_biosense.config.match_threshold;

    if (result->is_match) {
        template->verify_count++;
        template->last_verify_timestamp = get_timestamp_ms();
        template->failed_count = 0;  /* Reset on success */
        return BIOSENSE_OK;
    } else {
        template->failed_count++;

        if (template->failed_count >= g_biosense.config.max_failed_attempts) {
            template->is_locked = true;
            return BIOSENSE_ERR_LOCKED;
        }
        return BIOSENSE_ERR_TEMPLATE_MISMATCH;
    }
}

biosense_error_t biosense_reset_lockout(biosense_template_t *template) {
    if (!template) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    template->is_locked = false;
    template->failed_count = 0;
    return BIOSENSE_OK;
}

/*
 * Utility Functions
 */

uint32_t biosense_calculate_entropy(const biosense_vein_data_t *data) {
    if (!data) return 0;

    /* Entropy based on:
     * - Number of bifurcation points
     * - Distribution of points
     * - Angle variance
     */
    uint32_t entropy = 0;

    /* Each point contributes ~2 bits (position uncertainty) */
    entropy += data->point_count * 2;

    /* Complexity factor */
    entropy = (uint32_t)(entropy * data->pattern_complexity);

    /* Minimum 64 bits for usable security */
    if (entropy < 64) entropy = 64;

    return entropy;
}

float biosense_compare_patterns(const biosense_vein_data_t *a,
                                 const biosense_vein_data_t *b) {
    if (!a) return 0.0f;
    if (!b) return 0.0f;

    /* Read point counts with validation */
    uint32_t a_count = a->point_count;
    uint32_t b_count = b->point_count;

    /* Validate point counts are reasonable */
    if (a_count > BIOSENSE_VEIN_MAX_POINTS ||
        b_count > BIOSENSE_VEIN_MAX_POINTS) {
        return 0.0f;
    }

    /* Simple comparison algorithm:
     * - Find matching bifurcation points within tolerance
     * - Calculate match ratio
     */
    float tolerance = 5.0f;  /* Pixel tolerance */
    uint32_t matches = 0;

    for (uint32_t i = 0; i < a_count; i++) {
        for (uint32_t j = 0; j < b_count; j++) {
            float dx = (float)a->points[i].x - (float)b->points[j].x;
            float dy = (float)a->points[i].y - (float)b->points[j].y;
            float dist = sqrtf(dx * dx + dy * dy);

            if (dist < tolerance) {
                /* Also check angle similarity */
                int angle_diff = abs((int)a->points[i].angle -
                                     (int)b->points[j].angle);
                if (angle_diff > 128) angle_diff = 256 - angle_diff;

                if (angle_diff < 30) {
                    matches++;
                    break;
                }
            }
        }
    }

    /* Calculate similarity */
    uint32_t total = (a_count + b_count) / 2;
    if (total == 0) return 0.0f;

    float similarity = (float)matches / (float)total;

    /* Clamp to 0.0-1.0 */
    if (similarity > 1.0f) similarity = 1.0f;

    return similarity;
}

const char* biosense_error_string(biosense_error_t error) {
    if (error < 0 || error >= (int)(sizeof(error_strings) / sizeof(error_strings[0]))) {
        return "Unknown error";
    }
    return error_strings[error];
}

const char* biosense_state_string(biosense_state_t state) {
    if (state < 0 || state >= (int)(sizeof(state_strings) / sizeof(state_strings[0]))) {
        return "Unknown state";
    }
    return state_strings[state];
}

/*
 * Serialization
 */

biosense_error_t biosense_template_export(const biosense_template_t *template,
                                           uint8_t *buffer, size_t *size) {
    if (!template || !size) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    size_t required = sizeof(biosense_template_t);
    if (!buffer) {
        *size = required;
        return BIOSENSE_OK;
    }

    if (*size < required) {
        *size = required;
        return BIOSENSE_ERR_MEMORY;
    }

    memcpy(buffer, template, required);
    *size = required;
    return BIOSENSE_OK;
}

biosense_error_t biosense_template_import(biosense_template_t *template,
                                           const uint8_t *buffer, size_t size) {
    if (!template || !buffer) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    if (size < sizeof(biosense_template_t)) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    memcpy(template, buffer, sizeof(biosense_template_t));

    /* Validate version */
    if (template->version != BIOSENSE_TEMPLATE_VERSION) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    return BIOSENSE_OK;
}

/*
 * Async operations (stub for now)
 */

biosense_error_t biosense_scan_async(biosense_driver_t *driver,
                                      biosense_scan_opts_t *opts,
                                      biosense_scan_callback_t callback,
                                      void *userdata) {
    if (!driver || !callback) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    (void)opts;

    driver->async_pending = true;
    driver->async_callback = callback;
    driver->async_userdata = userdata;

    /* In a real implementation, this would start async I/O */
    /* For now, just call synchronously */
    biosense_vein_data_t data;
    biosense_quality_t quality;
    biosense_error_t err = biosense_scan_vein(driver, opts, &data, &quality);

    driver->async_pending = false;
    callback(err, &data, userdata);

    return BIOSENSE_OK;
}

void biosense_cancel_async(biosense_driver_t *driver) {
    if (driver) {
        driver->async_pending = false;
    }
}

/*
 * Device enumeration (stub)
 */

biosense_error_t biosense_enumerate_devices(biosense_device_info_t *devices,
                                             uint32_t max_devices,
                                             uint32_t *count) {
    if (!count) {
        return BIOSENSE_ERR_INIT_FAILED;
    }

    *count = 0;

    /* In a real implementation, this would scan USB/serial ports */
    /* For now, report a simulated device */
    if (devices && max_devices > 0) {
        memset(&devices[0], 0, sizeof(devices[0]));
        strncpy(devices[0].vendor, "PhantomOS", sizeof(devices[0].vendor) - 1);
        strncpy(devices[0].model, "BioSense Simulator", sizeof(devices[0].model) - 1);
        strncpy(devices[0].serial, "SIM-001", sizeof(devices[0].serial) - 1);
        devices[0].type = BIOSENSE_TYPE_VEIN_NIR;
        devices[0].connection = BIOSENSE_CONN_USB;
        devices[0].capabilities = BIOSENSE_CAP_VEIN_PATTERN |
                                   BIOSENSE_CAP_LIVENESS |
                                   BIOSENSE_CAP_ENCRYPTION;
        *count = 1;
    }

    return BIOSENSE_OK;
}
