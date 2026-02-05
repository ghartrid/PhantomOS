/*
 * PhantomOS LifeAuth - Blood Plasma Authentication System
 *
 * Uses unique biochemical characteristics in blood plasma as a biometric key.
 * Each person's plasma contains a distinctive "molecular fingerprint" based on:
 *   - Protein profiles (albumin, globulin ratios)
 *   - Antibody signatures (immunoglobulin patterns)
 *   - Metabolite fingerprints
 *   - Lipid profiles
 *   - Enzyme activity patterns
 *   - Cell-free DNA fragments
 *
 * Copyright (c) 2025 PhantomOS Project
 * License: CC BY-NC-SA 4.0
 */

#ifndef PHANTOM_LIFEAUTH_H
#define PHANTOM_LIFEAUTH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Plasma Component Categories
 */

/* Number of biomarkers tracked per category */
#define LIFEAUTH_PROTEIN_MARKERS     32
#define LIFEAUTH_ANTIBODY_MARKERS    24
#define LIFEAUTH_METABOLITE_MARKERS  48
#define LIFEAUTH_LIPID_MARKERS       16
#define LIFEAUTH_ENZYME_MARKERS      12
#define LIFEAUTH_ELECTROLYTE_MARKERS 8

#define LIFEAUTH_TOTAL_MARKERS       (LIFEAUTH_PROTEIN_MARKERS + \
                                      LIFEAUTH_ANTIBODY_MARKERS + \
                                      LIFEAUTH_METABOLITE_MARKERS + \
                                      LIFEAUTH_LIPID_MARKERS + \
                                      LIFEAUTH_ENZYME_MARKERS + \
                                      LIFEAUTH_ELECTROLYTE_MARKERS)

/* Cryptographic constants */
#define LIFEAUTH_HASH_SIZE          32      /* SHA-256 */
#define LIFEAUTH_SALT_SIZE          16
#define LIFEAUTH_KEY_SIZE           32      /* AES-256 */
#define LIFEAUTH_IV_SIZE            12      /* GCM nonce */
#define LIFEAUTH_TAG_SIZE           16      /* GCM auth tag */

/* Sensor types */
typedef enum {
    LIFEAUTH_SENSOR_MICRONEEDLE,        /* Microneedle array sampler */
    LIFEAUTH_SENSOR_SPECTROSCOPIC,      /* NIR/Raman spectroscopy */
    LIFEAUTH_SENSOR_MICROFLUIDIC,       /* Lab-on-chip analysis */
    LIFEAUTH_SENSOR_ELECTROCHEMICAL,    /* Electrochemical biosensor */
    LIFEAUTH_SENSOR_SIMULATED           /* Software simulation */
} lifeauth_sensor_type_t;

/* Sensor state */
typedef enum {
    LIFEAUTH_STATE_DISCONNECTED,
    LIFEAUTH_STATE_INITIALIZING,
    LIFEAUTH_STATE_READY,
    LIFEAUTH_STATE_SAMPLING,            /* Collecting plasma sample */
    LIFEAUTH_STATE_ANALYZING,           /* Processing biochemistry */
    LIFEAUTH_STATE_ERROR,
    LIFEAUTH_STATE_CALIBRATING,
    LIFEAUTH_STATE_CLEANING             /* Self-cleaning cycle */
} lifeauth_state_t;

/* Error codes */
typedef enum {
    LIFEAUTH_OK = 0,
    LIFEAUTH_ERR_NO_SENSOR,
    LIFEAUTH_ERR_INIT_FAILED,
    LIFEAUTH_ERR_SAMPLE_FAILED,
    LIFEAUTH_ERR_NO_CONTACT,            /* Finger not on sensor */
    LIFEAUTH_ERR_INSUFFICIENT_SAMPLE,
    LIFEAUTH_ERR_CONTAMINATION,
    LIFEAUTH_ERR_POOR_QUALITY,
    LIFEAUTH_ERR_TIMEOUT,
    LIFEAUTH_ERR_CALIBRATION,
    LIFEAUTH_ERR_PROFILE_MISMATCH,
    LIFEAUTH_ERR_MEMORY,
    LIFEAUTH_ERR_PERMISSION,
    LIFEAUTH_ERR_LOCKED,
    LIFEAUTH_ERR_CRYPTO,
    LIFEAUTH_ERR_HEALTH_ALERT           /* Detected health anomaly */
} lifeauth_error_t;

/*
 * Plasma Profile Data Structures
 */

/* Single biomarker reading */
typedef struct {
    uint16_t marker_id;         /* Unique marker identifier */
    float value;                /* Concentration/activity level */
    float variance;             /* Reading variance (quality) */
    uint8_t confidence;         /* 0-100 confidence score */
} lifeauth_marker_t;

/* Protein profile (albumin, globulins, fibrinogen, etc.) */
typedef struct {
    lifeauth_marker_t albumin;
    lifeauth_marker_t alpha1_globulin;
    lifeauth_marker_t alpha2_globulin;
    lifeauth_marker_t beta_globulin;
    lifeauth_marker_t gamma_globulin;
    lifeauth_marker_t fibrinogen;
    lifeauth_marker_t transferrin;
    lifeauth_marker_t ceruloplasmin;
    lifeauth_marker_t markers[LIFEAUTH_PROTEIN_MARKERS];
    float ag_ratio;             /* Albumin/Globulin ratio - stable identifier */
} lifeauth_protein_profile_t;

/* Antibody/Immunoglobulin signature */
typedef struct {
    lifeauth_marker_t igg_total;
    lifeauth_marker_t iga_total;
    lifeauth_marker_t igm_total;
    lifeauth_marker_t ige_total;
    lifeauth_marker_t markers[LIFEAUTH_ANTIBODY_MARKERS];
    float igg_subclass_ratios[4];   /* IgG1-4 ratios - very stable */
} lifeauth_antibody_profile_t;

/* Metabolite fingerprint */
typedef struct {
    lifeauth_marker_t glucose;
    lifeauth_marker_t urea;
    lifeauth_marker_t creatinine;
    lifeauth_marker_t uric_acid;
    lifeauth_marker_t bilirubin;
    lifeauth_marker_t markers[LIFEAUTH_METABOLITE_MARKERS];
    uint32_t metabolome_hash;       /* Hash of full metabolite pattern */
} lifeauth_metabolite_profile_t;

/* Lipid profile */
typedef struct {
    lifeauth_marker_t total_cholesterol;
    lifeauth_marker_t hdl;
    lifeauth_marker_t ldl;
    lifeauth_marker_t triglycerides;
    lifeauth_marker_t markers[LIFEAUTH_LIPID_MARKERS];
    float lipid_ratios[4];          /* Characteristic ratios */
} lifeauth_lipid_profile_t;

/* Enzyme activity pattern */
typedef struct {
    lifeauth_marker_t alt;          /* Alanine aminotransferase */
    lifeauth_marker_t ast;          /* Aspartate aminotransferase */
    lifeauth_marker_t alp;          /* Alkaline phosphatase */
    lifeauth_marker_t ggt;          /* Gamma-glutamyl transferase */
    lifeauth_marker_t ldh;          /* Lactate dehydrogenase */
    lifeauth_marker_t markers[LIFEAUTH_ENZYME_MARKERS];
    float enzyme_signature[8];      /* Normalized enzyme ratios */
} lifeauth_enzyme_profile_t;

/* Electrolyte balance */
typedef struct {
    lifeauth_marker_t sodium;
    lifeauth_marker_t potassium;
    lifeauth_marker_t chloride;
    lifeauth_marker_t bicarbonate;
    lifeauth_marker_t calcium;
    lifeauth_marker_t magnesium;
    lifeauth_marker_t phosphate;
    lifeauth_marker_t markers[LIFEAUTH_ELECTROLYTE_MARKERS];
} lifeauth_electrolyte_profile_t;

/* Complete plasma signature - the "key" */
typedef struct {
    lifeauth_protein_profile_t proteins;
    lifeauth_antibody_profile_t antibodies;
    lifeauth_metabolite_profile_t metabolites;
    lifeauth_lipid_profile_t lipids;
    lifeauth_enzyme_profile_t enzymes;
    lifeauth_electrolyte_profile_t electrolytes;

    /* Derived signature values */
    uint8_t plasma_fingerprint[64];     /* Compressed unique signature */
    uint32_t entropy_bits;              /* Estimated uniqueness */
    uint64_t sample_timestamp;

    /* Quality metrics */
    float overall_confidence;
    float stability_score;              /* How stable vs. variable markers */
    bool is_fasting_sample;
} lifeauth_plasma_signature_t;

/* Liveness indicators from plasma */
typedef struct {
    float temperature;                  /* Blood temperature */
    float oxygen_saturation;            /* SpO2 */
    float pulse_detected;               /* Pulsatile flow */
    float glucose_dynamics;             /* Real-time glucose changes */
    float enzyme_activity;              /* Active enzyme detection */
    float cell_viability;               /* Living cell markers */
    float overall_liveness;
    bool is_live;
} lifeauth_liveness_t;

/* Sample quality assessment */
typedef struct {
    float purity;                       /* Free of contamination */
    float concentration;                /* Adequate sample volume */
    float freshness;                    /* Time since collection */
    float hemolysis_free;               /* No red cell breakdown */
    float lipemia_free;                 /* Not too lipid-rich */
    float overall_quality;
    bool is_acceptable;
} lifeauth_sample_quality_t;

/*
 * Stored Credential (encrypted)
 */

typedef struct {
    uint32_t version;
    char user_id[64];

    /* Encrypted plasma signature */
    uint8_t encrypted_signature[sizeof(lifeauth_plasma_signature_t) + 64];
    uint32_t encrypted_size;

    /* Cryptographic fields */
    uint8_t salt[LIFEAUTH_SALT_SIZE];
    uint8_t iv[LIFEAUTH_IV_SIZE];
    uint8_t auth_tag[LIFEAUTH_TAG_SIZE];
    uint8_t verification_hash[LIFEAUTH_HASH_SIZE];

    /* Reference ranges (for drift detection) */
    float baseline_ag_ratio;
    float baseline_igg_ratios[4];

    /* Metadata */
    uint64_t enrolled_timestamp;
    uint64_t last_auth_timestamp;
    uint32_t auth_count;
    uint32_t failed_count;
    bool is_locked;

    /* Liveness score at enrollment */
    float enrollment_liveness;
} lifeauth_credential_t;

/* Match result */
typedef struct {
    float protein_similarity;
    float antibody_similarity;
    float metabolite_similarity;
    float lipid_similarity;
    float enzyme_similarity;
    float electrolyte_similarity;

    float overall_similarity;
    float liveness_score;
    bool is_match;
    bool is_live;

    lifeauth_sample_quality_t quality;
    uint32_t analysis_time_ms;

    /* Health indicators (optional) */
    bool health_alert;
    char health_message[128];
} lifeauth_match_result_t;

/* Sensor device info */
typedef struct {
    char vendor[64];
    char model[64];
    char serial[32];
    char firmware[16];
    lifeauth_sensor_type_t type;

    /* Capabilities */
    uint32_t markers_supported;
    bool has_spectroscopy;
    bool has_microfluidics;
    bool has_self_cleaning;
    uint32_t sample_volume_ul;          /* Microliters required */
    uint32_t analysis_time_ms;          /* Typical analysis time */
} lifeauth_sensor_info_t;

/* Configuration */
typedef struct {
    float match_threshold;              /* Default 0.85 */
    float liveness_threshold;           /* Default 0.90 */
    float quality_threshold;            /* Default 0.75 */
    uint32_t max_failed_attempts;       /* Before lockout */
    uint32_t lockout_duration_sec;

    /* Analysis options */
    bool require_liveness;
    bool detect_health_anomalies;
    bool require_fasting_sample;
    float drift_tolerance;              /* Allow for natural variation */

    char device_path[256];
} lifeauth_config_t;

/* Driver context */
typedef struct lifeauth_driver lifeauth_driver_t;

/*
 * Core API
 */

/* Initialize LifeAuth system */
lifeauth_error_t lifeauth_init(lifeauth_config_t *config);

/* Shutdown */
void lifeauth_shutdown(void);

/* Open sensor device */
lifeauth_error_t lifeauth_open(const char *device_path,
                                lifeauth_driver_t **driver);

/* Close sensor */
void lifeauth_close(lifeauth_driver_t *driver);

/* Get sensor info */
lifeauth_error_t lifeauth_get_info(lifeauth_driver_t *driver,
                                    lifeauth_sensor_info_t *info);

/* Get current state */
lifeauth_state_t lifeauth_get_state(lifeauth_driver_t *driver);

/*
 * Sampling API
 */

/* Collect and analyze plasma sample */
lifeauth_error_t lifeauth_sample(lifeauth_driver_t *driver,
                                  lifeauth_plasma_signature_t *signature,
                                  lifeauth_sample_quality_t *quality);

/* Check liveness (from sample analysis) */
lifeauth_error_t lifeauth_check_liveness(lifeauth_driver_t *driver,
                                          lifeauth_liveness_t *liveness);

/* Trigger sensor cleaning cycle */
lifeauth_error_t lifeauth_clean_sensor(lifeauth_driver_t *driver);

/* Calibrate sensor */
lifeauth_error_t lifeauth_calibrate(lifeauth_driver_t *driver);

/*
 * Authentication API
 */

/* Enroll user - collect plasma signature, encrypt with password */
lifeauth_error_t lifeauth_enroll(lifeauth_driver_t *driver,
                                  const char *user_id,
                                  const uint8_t *password, size_t pass_len,
                                  lifeauth_credential_t *credential);

/* Authenticate user - sample plasma, compare to stored credential */
lifeauth_error_t lifeauth_authenticate(lifeauth_driver_t *driver,
                                        lifeauth_credential_t *credential,
                                        const uint8_t *password, size_t pass_len,
                                        lifeauth_match_result_t *result);

/* Update credential baseline (for natural drift) */
lifeauth_error_t lifeauth_update_baseline(lifeauth_driver_t *driver,
                                           lifeauth_credential_t *credential,
                                           const uint8_t *password, size_t pass_len);

/* Reset lockout */
lifeauth_error_t lifeauth_reset_lockout(lifeauth_credential_t *credential);

/*
 * Analysis Functions
 */

/* Calculate signature entropy (uniqueness) */
uint32_t lifeauth_calculate_entropy(const lifeauth_plasma_signature_t *sig);

/* Compare two plasma signatures */
float lifeauth_compare_signatures(const lifeauth_plasma_signature_t *a,
                                   const lifeauth_plasma_signature_t *b);

/* Generate fingerprint from full signature */
void lifeauth_generate_fingerprint(const lifeauth_plasma_signature_t *sig,
                                    uint8_t *fingerprint, size_t len);

/*
 * Serialization
 */

/* Export credential to bytes */
lifeauth_error_t lifeauth_credential_export(const lifeauth_credential_t *cred,
                                             uint8_t *buffer, size_t *size);

/* Import credential from bytes */
lifeauth_error_t lifeauth_credential_import(lifeauth_credential_t *cred,
                                             const uint8_t *buffer, size_t size);

/*
 * Utility
 */

/* Get error string */
const char* lifeauth_error_string(lifeauth_error_t error);

/* Get state string */
const char* lifeauth_state_string(lifeauth_state_t state);

/*
 * Health Monitoring (optional feature)
 *
 * While authenticating, LifeAuth can detect potential health anomalies
 * in the plasma sample. This is NOT a diagnostic tool but can flag
 * significant deviations from the user's baseline.
 */

typedef struct {
    bool glucose_abnormal;
    bool lipid_abnormal;
    bool liver_enzymes_abnormal;
    bool kidney_markers_abnormal;
    bool electrolyte_imbalance;
    bool inflammation_detected;
    char summary[256];
} lifeauth_health_flags_t;

/* Check for health anomalies (optional during auth) */
lifeauth_error_t lifeauth_check_health(const lifeauth_plasma_signature_t *current,
                                        const lifeauth_plasma_signature_t *baseline,
                                        lifeauth_health_flags_t *flags);

#endif /* PHANTOM_LIFEAUTH_H */
