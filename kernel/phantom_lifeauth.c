/*
 * PhantomOS LifeAuth - Blood Plasma Authentication Implementation
 *
 * Uses unique biochemical characteristics in blood plasma as a biometric key.
 *
 * Copyright (c) 2025 PhantomOS Project
 * License: CC BY-NC-SA 4.0
 */

#include "phantom_lifeauth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef HAVE_OPENSSL
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

/* Global state */
static struct {
    bool initialized;
    lifeauth_config_t config;
} g_lifeauth = {0};

/* Driver context */
struct lifeauth_driver {
    lifeauth_state_t state;
    lifeauth_sensor_info_t info;
    int fd;
    void *driver_data;
    uint32_t sample_count;

    /* Simulated stable markers for repeatability in testing */
    uint32_t sim_seed;
    lifeauth_plasma_signature_t sim_baseline;
    bool sim_baseline_set;
};

/* Error strings */
static const char *error_strings[] = {
    "Success",
    "No sensor found",
    "Initialization failed",
    "Sample collection failed",
    "No finger contact",
    "Insufficient sample",
    "Sample contamination",
    "Poor sample quality",
    "Operation timed out",
    "Calibration required",
    "Profile mismatch",
    "Memory allocation failed",
    "Permission denied",
    "Account locked",
    "Cryptographic error",
    "Health anomaly detected"
};

/* State strings */
static const char *state_strings[] = {
    "Disconnected",
    "Initializing",
    "Ready",
    "Sampling",
    "Analyzing",
    "Error",
    "Calibrating",
    "Cleaning"
};

/*
 * Cryptographic helpers
 */

static int secure_random(uint8_t *buf, size_t len) {
#ifdef HAVE_OPENSSL
    if (RAND_bytes(buf, len) == 1) return 0;
#endif
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, len);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -1;
}

static uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int secure_compare(const void *a, const void *b, size_t len) {
    const volatile uint8_t *pa = a;
    const volatile uint8_t *pb = b;
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= pa[i] ^ pb[i];
    }
    return diff == 0 ? 0 : -1;
}

static int derive_key(const uint8_t *password, size_t pass_len,
                      const uint8_t *salt, size_t salt_len,
                      uint8_t *key_out, size_t key_len) {
#ifdef HAVE_OPENSSL
    if (PKCS5_PBKDF2_HMAC((const char *)password, pass_len,
                          salt, salt_len, 100000,
                          EVP_sha256(), key_len, key_out) == 1) {
        return 0;
    }
    return -1;
#else
    /* Simple fallback */
    memset(key_out, 0, key_len);
    for (size_t i = 0; i < pass_len && i < key_len; i++) {
        key_out[i] = password[i] ^ salt[i % salt_len];
    }
    return 0;
#endif
}

static int encrypt_data(const uint8_t *plaintext, size_t plain_len,
                        const uint8_t *key, uint8_t *iv,
                        uint8_t *ciphertext, uint8_t *auth_tag) {
#ifdef HAVE_OPENSSL
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    if (secure_random(iv, LIFEAUTH_IV_SIZE) != 0) {
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
    secure_random(iv, LIFEAUTH_IV_SIZE);
    for (size_t i = 0; i < plain_len; i++) {
        ciphertext[i] = plaintext[i] ^ key[i % LIFEAUTH_KEY_SIZE] ^ iv[i % LIFEAUTH_IV_SIZE];
    }
    memset(auth_tag, 0xAB, LIFEAUTH_TAG_SIZE);
    return 0;
#endif
}

static int decrypt_data(const uint8_t *ciphertext, size_t cipher_len,
                        const uint8_t *key, const uint8_t *iv,
                        const uint8_t *auth_tag, uint8_t *plaintext) {
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
    for (size_t i = 0; i < cipher_len; i++) {
        plaintext[i] = ciphertext[i] ^ key[i % LIFEAUTH_KEY_SIZE] ^ iv[i % LIFEAUTH_IV_SIZE];
    }
    return 0;
#endif
}

static void hash_data(const void *data, size_t len, uint8_t *hash_out) {
#ifdef HAVE_OPENSSL
    SHA256(data, len, hash_out);
#else
    uint32_t h = 0x811c9dc5;
    const uint8_t *p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x01000193;
    }
    memset(hash_out, 0, LIFEAUTH_HASH_SIZE);
    memcpy(hash_out, &h, 4);
#endif
}

/*
 * Plasma Simulation
 *
 * Generates realistic but synthetic plasma biomarker data.
 * In a real system, this would come from actual sensor hardware.
 */

static float sim_gaussian(uint32_t *seed) {
    /* Box-Muller transform for gaussian distribution */
    *seed = *seed * 1103515245 + 12345;
    float u1 = (*seed % 10000) / 10000.0f + 0.0001f;
    *seed = *seed * 1103515245 + 12345;
    float u2 = (*seed % 10000) / 10000.0f;
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * 3.14159f * u2);
}

static float sim_uniform(uint32_t *seed, float min, float max) {
    *seed = *seed * 1103515245 + 12345;
    return min + (*seed % 10000) / 10000.0f * (max - min);
}

static void simulate_protein_profile(lifeauth_protein_profile_t *p, uint32_t *seed, bool stable) {
    float base_albumin = 4.0f + (stable ? 0 : sim_gaussian(seed) * 0.3f);
    float base_globulin = 2.5f + (stable ? 0 : sim_gaussian(seed) * 0.2f);

    p->albumin.value = base_albumin;
    p->albumin.confidence = 95;
    p->alpha1_globulin.value = 0.2f + sim_uniform(seed, -0.02f, 0.02f);
    p->alpha2_globulin.value = 0.6f + sim_uniform(seed, -0.05f, 0.05f);
    p->beta_globulin.value = 0.8f + sim_uniform(seed, -0.05f, 0.05f);
    p->gamma_globulin.value = base_globulin - 1.6f + sim_uniform(seed, -0.1f, 0.1f);
    p->fibrinogen.value = 300.0f + sim_uniform(seed, -30.0f, 30.0f);
    p->transferrin.value = 250.0f + sim_uniform(seed, -20.0f, 20.0f);
    p->ceruloplasmin.value = 30.0f + sim_uniform(seed, -5.0f, 5.0f);

    /* A/G ratio is highly stable and unique */
    p->ag_ratio = base_albumin / base_globulin;

    for (int i = 0; i < LIFEAUTH_PROTEIN_MARKERS; i++) {
        p->markers[i].marker_id = 100 + i;
        p->markers[i].value = sim_uniform(seed, 0.1f, 10.0f);
        p->markers[i].confidence = 80 + (rand() % 20);
    }
}

static void simulate_antibody_profile(lifeauth_antibody_profile_t *a, uint32_t *seed, bool stable) {
    /* IgG subclass ratios are extremely stable - key identifier */
    float igg_total = 1000.0f + (stable ? 0 : sim_gaussian(seed) * 100.0f);

    a->igg_total.value = igg_total;
    a->igg_total.confidence = 95;
    a->iga_total.value = 200.0f + sim_uniform(seed, -20.0f, 20.0f);
    a->igm_total.value = 100.0f + sim_uniform(seed, -15.0f, 15.0f);
    a->ige_total.value = 50.0f + sim_uniform(seed, -10.0f, 10.0f);

    /* These ratios are unique to each person and very stable */
    a->igg_subclass_ratios[0] = 0.60f + (stable ? 0 : sim_uniform(seed, -0.02f, 0.02f));  /* IgG1 */
    a->igg_subclass_ratios[1] = 0.25f + (stable ? 0 : sim_uniform(seed, -0.01f, 0.01f));  /* IgG2 */
    a->igg_subclass_ratios[2] = 0.08f + (stable ? 0 : sim_uniform(seed, -0.005f, 0.005f)); /* IgG3 */
    a->igg_subclass_ratios[3] = 0.07f + (stable ? 0 : sim_uniform(seed, -0.005f, 0.005f)); /* IgG4 */

    for (int i = 0; i < LIFEAUTH_ANTIBODY_MARKERS; i++) {
        a->markers[i].marker_id = 200 + i;
        a->markers[i].value = sim_uniform(seed, 1.0f, 100.0f);
        a->markers[i].confidence = 85 + (rand() % 15);
    }
}

static void simulate_metabolite_profile(lifeauth_metabolite_profile_t *m, uint32_t *seed, bool stable) {
    m->glucose.value = 95.0f + (stable ? 0 : sim_uniform(seed, -10.0f, 20.0f));
    m->urea.value = 15.0f + sim_uniform(seed, -3.0f, 3.0f);
    m->creatinine.value = 1.0f + sim_uniform(seed, -0.1f, 0.1f);
    m->uric_acid.value = 5.0f + sim_uniform(seed, -1.0f, 1.0f);
    m->bilirubin.value = 0.8f + sim_uniform(seed, -0.2f, 0.2f);

    /* Generate metabolome hash - unique pattern */
    uint32_t hash = *seed;
    for (int i = 0; i < LIFEAUTH_METABOLITE_MARKERS; i++) {
        m->markers[i].marker_id = 300 + i;
        m->markers[i].value = sim_uniform(seed, 0.01f, 5.0f);
        hash ^= (uint32_t)(m->markers[i].value * 1000);
    }
    m->metabolome_hash = hash;
}

static void simulate_lipid_profile(lifeauth_lipid_profile_t *l, uint32_t *seed, bool stable) {
    l->total_cholesterol.value = 200.0f + (stable ? 0 : sim_uniform(seed, -20.0f, 20.0f));
    l->hdl.value = 55.0f + sim_uniform(seed, -5.0f, 5.0f);
    l->ldl.value = 120.0f + sim_uniform(seed, -15.0f, 15.0f);
    l->triglycerides.value = 150.0f + sim_uniform(seed, -30.0f, 30.0f);

    /* Lipid ratios are characteristic */
    l->lipid_ratios[0] = l->total_cholesterol.value / l->hdl.value;
    l->lipid_ratios[1] = l->ldl.value / l->hdl.value;
    l->lipid_ratios[2] = l->triglycerides.value / l->hdl.value;
    l->lipid_ratios[3] = (l->total_cholesterol.value - l->hdl.value) / l->hdl.value;

    for (int i = 0; i < LIFEAUTH_LIPID_MARKERS; i++) {
        l->markers[i].marker_id = 400 + i;
        l->markers[i].value = sim_uniform(seed, 0.5f, 50.0f);
    }
}

static void simulate_enzyme_profile(lifeauth_enzyme_profile_t *e, uint32_t *seed, bool stable) {
    e->alt.value = 25.0f + (stable ? 0 : sim_uniform(seed, -5.0f, 5.0f));
    e->ast.value = 22.0f + sim_uniform(seed, -4.0f, 4.0f);
    e->alp.value = 70.0f + sim_uniform(seed, -10.0f, 10.0f);
    e->ggt.value = 30.0f + sim_uniform(seed, -8.0f, 8.0f);
    e->ldh.value = 180.0f + sim_uniform(seed, -20.0f, 20.0f);

    /* Enzyme ratios are individual-specific */
    float total = e->alt.value + e->ast.value + e->alp.value + e->ggt.value + e->ldh.value;
    e->enzyme_signature[0] = e->alt.value / total;
    e->enzyme_signature[1] = e->ast.value / total;
    e->enzyme_signature[2] = e->alp.value / total;
    e->enzyme_signature[3] = e->ggt.value / total;
    e->enzyme_signature[4] = e->ldh.value / total;
    e->enzyme_signature[5] = e->ast.value / e->alt.value;  /* AST/ALT ratio */
    e->enzyme_signature[6] = e->ggt.value / e->alp.value;
    e->enzyme_signature[7] = 0;

    for (int i = 0; i < LIFEAUTH_ENZYME_MARKERS; i++) {
        e->markers[i].marker_id = 500 + i;
        e->markers[i].value = sim_uniform(seed, 5.0f, 100.0f);
    }
}

static void simulate_electrolyte_profile(lifeauth_electrolyte_profile_t *el, uint32_t *seed, bool stable) {
    (void)stable;
    el->sodium.value = 140.0f + sim_uniform(seed, -2.0f, 2.0f);
    el->potassium.value = 4.2f + sim_uniform(seed, -0.3f, 0.3f);
    el->chloride.value = 102.0f + sim_uniform(seed, -2.0f, 2.0f);
    el->bicarbonate.value = 24.0f + sim_uniform(seed, -2.0f, 2.0f);
    el->calcium.value = 9.5f + sim_uniform(seed, -0.3f, 0.3f);
    el->magnesium.value = 2.0f + sim_uniform(seed, -0.2f, 0.2f);
    el->phosphate.value = 3.5f + sim_uniform(seed, -0.3f, 0.3f);

    for (int i = 0; i < LIFEAUTH_ELECTROLYTE_MARKERS; i++) {
        el->markers[i].marker_id = 600 + i;
        el->markers[i].value = sim_uniform(seed, 0.1f, 10.0f);
    }
}

static void simulate_plasma_signature(lifeauth_driver_t *drv,
                                       lifeauth_plasma_signature_t *sig,
                                       bool use_baseline) {
    memset(sig, 0, sizeof(*sig));

    /* Use consistent seed for same "person" */
    uint32_t seed = drv->sim_seed;
    bool stable = use_baseline && drv->sim_baseline_set;

    if (stable) {
        /* Return baseline with small variations */
        memcpy(sig, &drv->sim_baseline, sizeof(*sig));

        /* Add small measurement noise */
        sig->proteins.albumin.value += sim_uniform(&seed, -0.05f, 0.05f);
        sig->metabolites.glucose.value += sim_uniform(&seed, -3.0f, 3.0f);
        sig->sample_timestamp = get_timestamp_ms();
        return;
    }

    simulate_protein_profile(&sig->proteins, &seed, false);
    simulate_antibody_profile(&sig->antibodies, &seed, false);
    simulate_metabolite_profile(&sig->metabolites, &seed, false);
    simulate_lipid_profile(&sig->lipids, &seed, false);
    simulate_enzyme_profile(&sig->enzymes, &seed, false);
    simulate_electrolyte_profile(&sig->electrolytes, &seed, false);

    sig->sample_timestamp = get_timestamp_ms();
    sig->overall_confidence = 0.92f + sim_uniform(&seed, -0.05f, 0.05f);
    sig->stability_score = 0.88f;
    sig->is_fasting_sample = (rand() % 2) == 0;

    /* Generate fingerprint */
    lifeauth_generate_fingerprint(sig, sig->plasma_fingerprint, 64);
    sig->entropy_bits = lifeauth_calculate_entropy(sig);

    /* Save as baseline */
    if (!drv->sim_baseline_set) {
        memcpy(&drv->sim_baseline, sig, sizeof(*sig));
        drv->sim_baseline_set = true;
    }
}

/*
 * Core API Implementation
 */

lifeauth_error_t lifeauth_init(lifeauth_config_t *config) {
    if (g_lifeauth.initialized) {
        return LIFEAUTH_OK;
    }

    /* Defaults */
    g_lifeauth.config.match_threshold = 0.85f;
    g_lifeauth.config.liveness_threshold = 0.90f;
    g_lifeauth.config.quality_threshold = 0.75f;
    g_lifeauth.config.max_failed_attempts = 5;
    g_lifeauth.config.lockout_duration_sec = 300;
    g_lifeauth.config.require_liveness = true;
    g_lifeauth.config.detect_health_anomalies = true;
    g_lifeauth.config.require_fasting_sample = false;
    g_lifeauth.config.drift_tolerance = 0.10f;

    if (config) {
        if (config->match_threshold > 0)
            g_lifeauth.config.match_threshold = config->match_threshold;
        if (config->liveness_threshold > 0)
            g_lifeauth.config.liveness_threshold = config->liveness_threshold;
        if (config->quality_threshold > 0)
            g_lifeauth.config.quality_threshold = config->quality_threshold;
        if (config->max_failed_attempts > 0)
            g_lifeauth.config.max_failed_attempts = config->max_failed_attempts;
        g_lifeauth.config.require_liveness = config->require_liveness;
        g_lifeauth.config.detect_health_anomalies = config->detect_health_anomalies;
    }

    g_lifeauth.initialized = true;
    return LIFEAUTH_OK;
}

void lifeauth_shutdown(void) {
    g_lifeauth.initialized = false;
}

lifeauth_error_t lifeauth_open(const char *device_path,
                                lifeauth_driver_t **driver) {
    if (!driver) return LIFEAUTH_ERR_INIT_FAILED;
    (void)device_path;

    lifeauth_driver_t *drv = calloc(1, sizeof(*drv));
    if (!drv) return LIFEAUTH_ERR_MEMORY;

    /* Initialize simulated sensor */
    drv->state = LIFEAUTH_STATE_READY;
    drv->fd = -1;

    /* Generate unique seed for this "sensor" (simulates unique user) */
    secure_random((uint8_t*)&drv->sim_seed, sizeof(drv->sim_seed));

    strncpy(drv->info.vendor, "PhantomOS", sizeof(drv->info.vendor) - 1);
    strncpy(drv->info.model, "LifeAuth Plasma Analyzer", sizeof(drv->info.model) - 1);
    strncpy(drv->info.serial, "LA-SIM-001", sizeof(drv->info.serial) - 1);
    strncpy(drv->info.firmware, "1.0.0", sizeof(drv->info.firmware) - 1);
    drv->info.type = LIFEAUTH_SENSOR_SIMULATED;
    drv->info.markers_supported = LIFEAUTH_TOTAL_MARKERS;
    drv->info.has_spectroscopy = true;
    drv->info.has_microfluidics = true;
    drv->info.has_self_cleaning = true;
    drv->info.sample_volume_ul = 50;  /* 50 microliters */
    drv->info.analysis_time_ms = 3000;

    *driver = drv;
    return LIFEAUTH_OK;
}

void lifeauth_close(lifeauth_driver_t *driver) {
    if (driver) {
        if (driver->fd >= 0) close(driver->fd);
        free(driver);
    }
}

lifeauth_error_t lifeauth_get_info(lifeauth_driver_t *driver,
                                    lifeauth_sensor_info_t *info) {
    if (!driver || !info) return LIFEAUTH_ERR_INIT_FAILED;
    *info = driver->info;
    return LIFEAUTH_OK;
}

lifeauth_state_t lifeauth_get_state(lifeauth_driver_t *driver) {
    return driver ? driver->state : LIFEAUTH_STATE_DISCONNECTED;
}

/*
 * Sampling
 */

lifeauth_error_t lifeauth_sample(lifeauth_driver_t *driver,
                                  lifeauth_plasma_signature_t *signature,
                                  lifeauth_sample_quality_t *quality) {
    if (!driver || !signature) return LIFEAUTH_ERR_INIT_FAILED;

    driver->state = LIFEAUTH_STATE_SAMPLING;

    /* Simulate plasma analysis */
    simulate_plasma_signature(driver, signature, true);

    driver->state = LIFEAUTH_STATE_ANALYZING;
    driver->sample_count++;

    if (quality) {
        quality->purity = 0.95f + (float)(rand() % 50) / 1000.0f;
        quality->concentration = 0.92f + (float)(rand() % 80) / 1000.0f;
        quality->freshness = 1.0f;
        quality->hemolysis_free = 0.98f;
        quality->lipemia_free = 0.96f;
        quality->overall_quality = (quality->purity + quality->concentration +
                                     quality->hemolysis_free + quality->lipemia_free) / 4.0f;
        quality->is_acceptable = quality->overall_quality >= g_lifeauth.config.quality_threshold;
    }

    driver->state = LIFEAUTH_STATE_READY;
    return LIFEAUTH_OK;
}

lifeauth_error_t lifeauth_check_liveness(lifeauth_driver_t *driver,
                                          lifeauth_liveness_t *liveness) {
    if (!driver || !liveness) return LIFEAUTH_ERR_INIT_FAILED;

    memset(liveness, 0, sizeof(*liveness));

    /* Simulated liveness checks */
    liveness->temperature = 36.5f + (float)(rand() % 10) / 10.0f;
    liveness->oxygen_saturation = 96.0f + (float)(rand() % 30) / 10.0f;
    liveness->pulse_detected = 0.98f;
    liveness->glucose_dynamics = 0.85f + (float)(rand() % 100) / 1000.0f;
    liveness->enzyme_activity = 0.92f + (float)(rand() % 80) / 1000.0f;
    liveness->cell_viability = 0.95f;

    liveness->overall_liveness = (liveness->pulse_detected +
                                   liveness->enzyme_activity +
                                   liveness->cell_viability) / 3.0f;
    liveness->is_live = liveness->overall_liveness >= g_lifeauth.config.liveness_threshold;

    return LIFEAUTH_OK;
}

lifeauth_error_t lifeauth_clean_sensor(lifeauth_driver_t *driver) {
    if (!driver) return LIFEAUTH_ERR_INIT_FAILED;

    driver->state = LIFEAUTH_STATE_CLEANING;
    /* Simulate cleaning cycle */
    driver->state = LIFEAUTH_STATE_READY;

    return LIFEAUTH_OK;
}

lifeauth_error_t lifeauth_calibrate(lifeauth_driver_t *driver) {
    if (!driver) return LIFEAUTH_ERR_INIT_FAILED;

    driver->state = LIFEAUTH_STATE_CALIBRATING;
    /* Simulate calibration */
    driver->state = LIFEAUTH_STATE_READY;

    return LIFEAUTH_OK;
}

/*
 * Authentication
 */

lifeauth_error_t lifeauth_enroll(lifeauth_driver_t *driver,
                                  const char *user_id,
                                  const uint8_t *password, size_t pass_len,
                                  lifeauth_credential_t *credential) {
    if (!driver || !user_id || !password || !credential) {
        return LIFEAUTH_ERR_INIT_FAILED;
    }

    memset(credential, 0, sizeof(*credential));

    /* Collect plasma sample */
    lifeauth_plasma_signature_t signature;
    lifeauth_sample_quality_t quality;

    lifeauth_error_t err = lifeauth_sample(driver, &signature, &quality);
    if (err != LIFEAUTH_OK) return err;

    if (!quality.is_acceptable) {
        return LIFEAUTH_ERR_POOR_QUALITY;
    }

    /* Check liveness */
    if (g_lifeauth.config.require_liveness) {
        lifeauth_liveness_t liveness;
        err = lifeauth_check_liveness(driver, &liveness);
        if (err != LIFEAUTH_OK) return err;

        if (!liveness.is_live) {
            return LIFEAUTH_ERR_SAMPLE_FAILED;
        }
        credential->enrollment_liveness = liveness.overall_liveness;
    }

    /* Generate salt */
    if (secure_random(credential->salt, LIFEAUTH_SALT_SIZE) != 0) {
        return LIFEAUTH_ERR_CRYPTO;
    }

    /* Derive key */
    uint8_t key[LIFEAUTH_KEY_SIZE];
    if (derive_key(password, pass_len, credential->salt, LIFEAUTH_SALT_SIZE,
                   key, LIFEAUTH_KEY_SIZE) != 0) {
        return LIFEAUTH_ERR_CRYPTO;
    }

    /* Hash signature for verification */
    hash_data(&signature, sizeof(signature), credential->verification_hash);

    /* Encrypt signature */
    if (encrypt_data((uint8_t*)&signature, sizeof(signature), key,
                     credential->iv, credential->encrypted_signature,
                     credential->auth_tag) != 0) {
        memset(key, 0, sizeof(key));
        return LIFEAUTH_ERR_CRYPTO;
    }

    memset(key, 0, sizeof(key));

    /* Store baseline ratios */
    credential->baseline_ag_ratio = signature.proteins.ag_ratio;
    memcpy(credential->baseline_igg_ratios, signature.antibodies.igg_subclass_ratios,
           sizeof(credential->baseline_igg_ratios));

    /* Metadata */
    credential->version = 1;
    strncpy(credential->user_id, user_id, sizeof(credential->user_id) - 1);
    credential->encrypted_size = sizeof(signature);
    credential->enrolled_timestamp = get_timestamp_ms();

    return LIFEAUTH_OK;
}

lifeauth_error_t lifeauth_authenticate(lifeauth_driver_t *driver,
                                        lifeauth_credential_t *credential,
                                        const uint8_t *password, size_t pass_len,
                                        lifeauth_match_result_t *result) {
    if (!driver || !credential || !password || !result) {
        return LIFEAUTH_ERR_INIT_FAILED;
    }

    memset(result, 0, sizeof(*result));

    if (credential->is_locked) {
        return LIFEAUTH_ERR_LOCKED;
    }

    uint64_t start_time = get_timestamp_ms();

    /* Derive key */
    uint8_t key[LIFEAUTH_KEY_SIZE];
    if (derive_key(password, pass_len, credential->salt, LIFEAUTH_SALT_SIZE,
                   key, LIFEAUTH_KEY_SIZE) != 0) {
        return LIFEAUTH_ERR_CRYPTO;
    }

    /* Decrypt stored signature */
    lifeauth_plasma_signature_t stored;
    if (decrypt_data(credential->encrypted_signature, credential->encrypted_size,
                     key, credential->iv, credential->auth_tag,
                     (uint8_t*)&stored) != 0) {
        memset(key, 0, sizeof(key));
        credential->failed_count++;
        if (credential->failed_count >= g_lifeauth.config.max_failed_attempts) {
            credential->is_locked = true;
            return LIFEAUTH_ERR_LOCKED;
        }
        return LIFEAUTH_ERR_CRYPTO;
    }

    memset(key, 0, sizeof(key));

    /* Verify hash */
    uint8_t check_hash[LIFEAUTH_HASH_SIZE];
    hash_data(&stored, sizeof(stored), check_hash);
    if (secure_compare(check_hash, credential->verification_hash, LIFEAUTH_HASH_SIZE) != 0) {
        credential->failed_count++;
        return LIFEAUTH_ERR_PROFILE_MISMATCH;
    }

    /* Collect current sample */
    lifeauth_plasma_signature_t current;
    lifeauth_error_t err = lifeauth_sample(driver, &current, &result->quality);
    if (err != LIFEAUTH_OK) return err;

    /* Check liveness */
    if (g_lifeauth.config.require_liveness) {
        lifeauth_liveness_t liveness;
        err = lifeauth_check_liveness(driver, &liveness);
        if (err != LIFEAUTH_OK) return err;

        result->liveness_score = liveness.overall_liveness;
        result->is_live = liveness.is_live;

        if (!result->is_live) {
            credential->failed_count++;
            return LIFEAUTH_ERR_SAMPLE_FAILED;
        }
    } else {
        result->liveness_score = 1.0f;
        result->is_live = true;
    }

    /* Compare signatures */
    result->overall_similarity = lifeauth_compare_signatures(&stored, &current);

    /* Component similarities */
    result->protein_similarity = 1.0f - fabsf(stored.proteins.ag_ratio - current.proteins.ag_ratio) / stored.proteins.ag_ratio;
    result->antibody_similarity = 0.0f;
    for (int i = 0; i < 4; i++) {
        result->antibody_similarity += 1.0f - fabsf(stored.antibodies.igg_subclass_ratios[i] -
                                                      current.antibodies.igg_subclass_ratios[i]) * 5.0f;
    }
    result->antibody_similarity /= 4.0f;
    if (result->antibody_similarity < 0) result->antibody_similarity = 0;

    result->enzyme_similarity = 0.0f;
    for (int i = 0; i < 6; i++) {
        result->enzyme_similarity += 1.0f - fabsf(stored.enzymes.enzyme_signature[i] -
                                                    current.enzymes.enzyme_signature[i]) * 10.0f;
    }
    result->enzyme_similarity /= 6.0f;
    if (result->enzyme_similarity < 0) result->enzyme_similarity = 0;

    result->lipid_similarity = 0.0f;
    for (int i = 0; i < 4; i++) {
        result->lipid_similarity += 1.0f - fabsf(stored.lipids.lipid_ratios[i] -
                                                   current.lipids.lipid_ratios[i]) / (stored.lipids.lipid_ratios[i] + 0.1f);
    }
    result->lipid_similarity /= 4.0f;

    result->metabolite_similarity = stored.metabolites.metabolome_hash == current.metabolites.metabolome_hash ? 1.0f : 0.5f;
    result->electrolyte_similarity = 0.95f;  /* Usually very stable */

    result->analysis_time_ms = (uint32_t)(get_timestamp_ms() - start_time);

    /* Determine match */
    result->is_match = result->overall_similarity >= g_lifeauth.config.match_threshold;

    if (result->is_match) {
        credential->auth_count++;
        credential->last_auth_timestamp = get_timestamp_ms();
        credential->failed_count = 0;

        /* Check for health anomalies */
        if (g_lifeauth.config.detect_health_anomalies) {
            lifeauth_health_flags_t health;
            lifeauth_check_health(&current, &stored, &health);
            if (health.glucose_abnormal || health.liver_enzymes_abnormal ||
                health.kidney_markers_abnormal || health.inflammation_detected) {
                result->health_alert = true;
                strncpy(result->health_message, health.summary, sizeof(result->health_message) - 1);
                result->health_message[sizeof(result->health_message) - 1] = '\0';
            }
        }

        return LIFEAUTH_OK;
    } else {
        credential->failed_count++;
        if (credential->failed_count >= g_lifeauth.config.max_failed_attempts) {
            credential->is_locked = true;
            return LIFEAUTH_ERR_LOCKED;
        }
        return LIFEAUTH_ERR_PROFILE_MISMATCH;
    }
}

lifeauth_error_t lifeauth_reset_lockout(lifeauth_credential_t *credential) {
    if (!credential) return LIFEAUTH_ERR_INIT_FAILED;

    credential->is_locked = false;
    credential->failed_count = 0;
    return LIFEAUTH_OK;
}

/*
 * Analysis Functions
 */

uint32_t lifeauth_calculate_entropy(const lifeauth_plasma_signature_t *sig) {
    if (!sig) return 0;

    /* Entropy calculation based on marker variability */
    uint32_t entropy = 0;

    /* Protein markers: ~2 bits each */
    entropy += LIFEAUTH_PROTEIN_MARKERS * 2;

    /* Antibody subclass ratios: ~8 bits total (highly unique) */
    entropy += 8;

    /* Metabolome: ~16 bits */
    entropy += 16;

    /* Enzyme ratios: ~6 bits */
    entropy += 6;

    /* Lipid ratios: ~4 bits */
    entropy += 4;

    /* Individual marker values add more entropy */
    entropy += LIFEAUTH_TOTAL_MARKERS / 2;

    /* Total typically 100-150 bits for good uniqueness */
    return entropy;
}

float lifeauth_compare_signatures(const lifeauth_plasma_signature_t *a,
                                   const lifeauth_plasma_signature_t *b) {
    if (!a || !b) return 0.0f;

    float similarity = 0.0f;
    float weights_sum = 0.0f;

    /* A/G ratio - highly stable, weight = 3 */
    float ag_diff = fabsf(a->proteins.ag_ratio - b->proteins.ag_ratio);
    float ag_sim = 1.0f - (ag_diff / (a->proteins.ag_ratio + 0.1f));
    if (ag_sim < 0) ag_sim = 0;
    similarity += ag_sim * 3.0f;
    weights_sum += 3.0f;

    /* IgG subclass ratios - extremely stable, weight = 4 */
    float igg_sim = 0.0f;
    for (int i = 0; i < 4; i++) {
        float diff = fabsf(a->antibodies.igg_subclass_ratios[i] -
                           b->antibodies.igg_subclass_ratios[i]);
        float s = 1.0f - (diff * 10.0f);  /* Sensitive to small changes */
        if (s < 0) s = 0;
        igg_sim += s;
    }
    igg_sim /= 4.0f;
    similarity += igg_sim * 4.0f;
    weights_sum += 4.0f;

    /* Enzyme signature - weight = 2 */
    float enz_sim = 0.0f;
    for (int i = 0; i < 6; i++) {
        float diff = fabsf(a->enzymes.enzyme_signature[i] -
                           b->enzymes.enzyme_signature[i]);
        float s = 1.0f - (diff * 8.0f);
        if (s < 0) s = 0;
        enz_sim += s;
    }
    enz_sim /= 6.0f;
    similarity += enz_sim * 2.0f;
    weights_sum += 2.0f;

    /* Lipid ratios - weight = 1 */
    float lip_sim = 0.0f;
    for (int i = 0; i < 4; i++) {
        float diff = fabsf(a->lipids.lipid_ratios[i] - b->lipids.lipid_ratios[i]);
        float s = 1.0f - (diff / (a->lipids.lipid_ratios[i] + 1.0f));
        if (s < 0) s = 0;
        lip_sim += s;
    }
    lip_sim /= 4.0f;
    similarity += lip_sim * 1.0f;
    weights_sum += 1.0f;

    return similarity / weights_sum;
}

void lifeauth_generate_fingerprint(const lifeauth_plasma_signature_t *sig,
                                    uint8_t *fingerprint, size_t len) {
    if (!sig || !fingerprint || len == 0) return;

    /* Create a compact fingerprint from key biomarkers */
    memset(fingerprint, 0, len);

    /* Pack key values into fingerprint */
    size_t pos = 0;

    /* A/G ratio */
    uint16_t ag = (uint16_t)(sig->proteins.ag_ratio * 1000);
    if (pos + 2 <= len) { memcpy(fingerprint + pos, &ag, 2); pos += 2; }

    /* IgG ratios */
    for (int i = 0; i < 4 && pos + 2 <= len; i++) {
        uint16_t r = (uint16_t)(sig->antibodies.igg_subclass_ratios[i] * 10000);
        memcpy(fingerprint + pos, &r, 2);
        pos += 2;
    }

    /* Metabolome hash */
    if (pos + 4 <= len) {
        memcpy(fingerprint + pos, &sig->metabolites.metabolome_hash, 4);
        pos += 4;
    }

    /* Enzyme signature */
    for (int i = 0; i < 6 && pos + 2 <= len; i++) {
        uint16_t e = (uint16_t)(sig->enzymes.enzyme_signature[i] * 10000);
        memcpy(fingerprint + pos, &e, 2);
        pos += 2;
    }

    /* Lipid ratios */
    for (int i = 0; i < 4 && pos + 2 <= len; i++) {
        uint16_t l = (uint16_t)(sig->lipids.lipid_ratios[i] * 100);
        memcpy(fingerprint + pos, &l, 2);
        pos += 2;
    }

    /* Hash the rest if space remains */
    if (pos < len) {
        uint8_t hash[32];
        hash_data(sig, sizeof(*sig), hash);
        size_t copy = len - pos;
        if (copy > 32) copy = 32;
        memcpy(fingerprint + pos, hash, copy);
    }
}

/*
 * Health Monitoring
 */

lifeauth_error_t lifeauth_check_health(const lifeauth_plasma_signature_t *current,
                                        const lifeauth_plasma_signature_t *baseline,
                                        lifeauth_health_flags_t *flags) {
    if (!current || !baseline || !flags) {
        return LIFEAUTH_ERR_INIT_FAILED;
    }

    memset(flags, 0, sizeof(*flags));

    /* Check glucose */
    if (current->metabolites.glucose.value > 126.0f ||
        current->metabolites.glucose.value < 70.0f) {
        flags->glucose_abnormal = true;
    }

    /* Check lipids */
    if (current->lipids.total_cholesterol.value > 240.0f ||
        current->lipids.ldl.value > 160.0f) {
        flags->lipid_abnormal = true;
    }

    /* Check liver enzymes */
    float alt_change = fabsf(current->enzymes.alt.value - baseline->enzymes.alt.value);
    float ast_change = fabsf(current->enzymes.ast.value - baseline->enzymes.ast.value);
    if (alt_change > baseline->enzymes.alt.value * 0.5f ||
        ast_change > baseline->enzymes.ast.value * 0.5f) {
        flags->liver_enzymes_abnormal = true;
    }

    /* Check kidney markers */
    if (current->metabolites.creatinine.value > 1.4f ||
        current->metabolites.urea.value > 25.0f) {
        flags->kidney_markers_abnormal = true;
    }

    /* Check electrolytes */
    if (current->electrolytes.sodium.value < 135.0f ||
        current->electrolytes.sodium.value > 145.0f ||
        current->electrolytes.potassium.value < 3.5f ||
        current->electrolytes.potassium.value > 5.0f) {
        flags->electrolyte_imbalance = true;
    }

    /* Build summary */
    if (flags->glucose_abnormal || flags->lipid_abnormal ||
        flags->liver_enzymes_abnormal || flags->kidney_markers_abnormal) {

        char *p = flags->summary;
        size_t remaining = sizeof(flags->summary) - 1;

        if (flags->glucose_abnormal) {
            int n = snprintf(p, remaining, "Glucose outside range. ");
            p += n; remaining -= n;
        }
        if (flags->liver_enzymes_abnormal) {
            int n = snprintf(p, remaining, "Liver enzyme changes. ");
            p += n; remaining -= n;
        }
        if (flags->kidney_markers_abnormal) {
            int n = snprintf(p, remaining, "Kidney markers elevated. ");
            p += n; remaining -= n;
        }
        if (flags->lipid_abnormal) {
            snprintf(p, remaining, "Lipid levels high. ");
        }
    }

    return LIFEAUTH_OK;
}

/*
 * Serialization
 */

lifeauth_error_t lifeauth_credential_export(const lifeauth_credential_t *cred,
                                             uint8_t *buffer, size_t *size) {
    if (!cred || !size) return LIFEAUTH_ERR_INIT_FAILED;

    size_t required = sizeof(lifeauth_credential_t);
    if (!buffer) {
        *size = required;
        return LIFEAUTH_OK;
    }

    if (*size < required) {
        *size = required;
        return LIFEAUTH_ERR_MEMORY;
    }

    memcpy(buffer, cred, required);
    *size = required;
    return LIFEAUTH_OK;
}

lifeauth_error_t lifeauth_credential_import(lifeauth_credential_t *cred,
                                             const uint8_t *buffer, size_t size) {
    if (!cred || !buffer) return LIFEAUTH_ERR_INIT_FAILED;

    if (size < sizeof(lifeauth_credential_t)) {
        return LIFEAUTH_ERR_INIT_FAILED;
    }

    memcpy(cred, buffer, sizeof(lifeauth_credential_t));

    if (cred->version != 1) {
        return LIFEAUTH_ERR_INIT_FAILED;
    }

    return LIFEAUTH_OK;
}

/*
 * Utility
 */

const char* lifeauth_error_string(lifeauth_error_t error) {
    if (error < 0 || error >= (int)(sizeof(error_strings) / sizeof(error_strings[0]))) {
        return "Unknown error";
    }
    return error_strings[error];
}

const char* lifeauth_state_string(lifeauth_state_t state) {
    if (state < 0 || state >= (int)(sizeof(state_strings) / sizeof(state_strings[0]))) {
        return "Unknown state";
    }
    return state_strings[state];
}
