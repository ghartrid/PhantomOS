/*
 * PhantomOS BioSense Authentication Driver Interface
 *
 * Hardware abstraction layer for biometric blood/vein sensors
 * Supports: Vein pattern recognition, blood oxygen, glucose patterns
 *
 * Copyright (c) 2025 PhantomOS Project
 * License: CC BY-NC-SA 4.0
 */

#ifndef PHANTOM_BIOSENSE_H
#define PHANTOM_BIOSENSE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Sensor types */
typedef enum {
    BIOSENSE_TYPE_VEIN_NIR,         /* Near-infrared vein pattern */
    BIOSENSE_TYPE_VEIN_THERMAL,     /* Thermal vein imaging */
    BIOSENSE_TYPE_PULSE_OX,         /* Blood oxygen/pulse */
    BIOSENSE_TYPE_GLUCOSE,          /* Blood glucose pattern */
    BIOSENSE_TYPE_SPECTRAL,         /* Full spectral analysis */
    BIOSENSE_TYPE_MICRO_SAMPLE,     /* Micro blood sampling */
    BIOSENSE_TYPE_UNKNOWN
} biosense_type_t;

/* Connection interfaces */
typedef enum {
    BIOSENSE_CONN_USB,
    BIOSENSE_CONN_SERIAL,
    BIOSENSE_CONN_I2C,
    BIOSENSE_CONN_SPI,
    BIOSENSE_CONN_GPIO
} biosense_conn_t;

/* Sensor state */
typedef enum {
    BIOSENSE_STATE_DISCONNECTED,
    BIOSENSE_STATE_INITIALIZING,
    BIOSENSE_STATE_READY,
    BIOSENSE_STATE_SCANNING,
    BIOSENSE_STATE_PROCESSING,
    BIOSENSE_STATE_ERROR,
    BIOSENSE_STATE_CALIBRATING
} biosense_state_t;

/* Error codes */
typedef enum {
    BIOSENSE_OK = 0,
    BIOSENSE_ERR_NO_DEVICE,
    BIOSENSE_ERR_INIT_FAILED,
    BIOSENSE_ERR_SCAN_FAILED,
    BIOSENSE_ERR_NO_FINGER,
    BIOSENSE_ERR_POOR_QUALITY,
    BIOSENSE_ERR_TIMEOUT,
    BIOSENSE_ERR_CALIBRATION,
    BIOSENSE_ERR_TEMPLATE_MISMATCH,
    BIOSENSE_ERR_MEMORY,
    BIOSENSE_ERR_PERMISSION,
    BIOSENSE_ERR_LOCKED,
    BIOSENSE_ERR_CRYPTO
} biosense_error_t;

/* Scan quality metrics */
typedef struct {
    float clarity;          /* 0.0-1.0: image/signal clarity */
    float coverage;         /* 0.0-1.0: sensor coverage */
    float stability;        /* 0.0-1.0: reading stability */
    float confidence;       /* 0.0-1.0: overall confidence */
    bool is_acceptable;     /* Meets minimum threshold */
} biosense_quality_t;

/* Vein pattern data */
#define BIOSENSE_VEIN_MAX_POINTS    512
#define BIOSENSE_VEIN_IMAGE_SIZE    (128 * 128)  /* Grayscale image */

typedef struct {
    /* Vein bifurcation points */
    struct {
        uint16_t x;
        uint16_t y;
        uint8_t angle;      /* 0-255 representing 0-360 degrees */
        uint8_t type;       /* Bifurcation type */
    } points[BIOSENSE_VEIN_MAX_POINTS];
    uint32_t point_count;

    /* Optional raw image */
    uint8_t *image_data;
    uint32_t image_width;
    uint32_t image_height;

    /* Pattern metrics */
    float pattern_complexity;
    uint32_t entropy_bits;
} biosense_vein_data_t;

/* Blood chemistry data */
typedef struct {
    float oxygen_saturation;    /* SpO2 percentage */
    float heart_rate;           /* BPM */
    float glucose_level;        /* mg/dL estimate */
    float hemoglobin;           /* g/dL estimate */
    uint32_t spectral_signature[64];  /* NIR absorption bands */
    uint64_t timestamp;
} biosense_blood_data_t;

/* Combined biometric template */
#define BIOSENSE_TEMPLATE_VERSION   1
#define BIOSENSE_TEMPLATE_MAX_SIZE  4096
#define BIOSENSE_HASH_SIZE          32

typedef struct {
    uint32_t version;
    biosense_type_t type;

    /* Template data (encrypted) */
    uint8_t encrypted_data[BIOSENSE_TEMPLATE_MAX_SIZE];
    uint32_t data_size;

    /* Crypto fields */
    uint8_t salt[16];
    uint8_t iv[12];
    uint8_t auth_tag[16];
    uint8_t verification_hash[BIOSENSE_HASH_SIZE];

    /* Metadata */
    char user_id[64];
    uint64_t created_timestamp;
    uint64_t last_verify_timestamp;
    uint32_t verify_count;
    uint32_t failed_count;
    bool is_locked;

    /* Liveness detection results at enrollment */
    float liveness_score;
} biosense_template_t;

/* Sensor device info */
typedef struct {
    char vendor[64];
    char model[64];
    char serial[32];
    char firmware[16];
    biosense_type_t type;
    biosense_conn_t connection;
    uint32_t capabilities;      /* Bitmask of features */

    /* Resolution/specs */
    uint32_t image_width;
    uint32_t image_height;
    uint32_t scan_rate_hz;
    uint32_t spectral_bands;
} biosense_device_info_t;

/* Capability flags */
#define BIOSENSE_CAP_VEIN_PATTERN   (1 << 0)
#define BIOSENSE_CAP_PULSE_OX       (1 << 1)
#define BIOSENSE_CAP_GLUCOSE        (1 << 2)
#define BIOSENSE_CAP_SPECTRAL       (1 << 3)
#define BIOSENSE_CAP_LIVENESS       (1 << 4)
#define BIOSENSE_CAP_ENCRYPTION     (1 << 5)
#define BIOSENSE_CAP_TEMPLATE_STORE (1 << 6)

/* Scan options */
typedef struct {
    uint32_t timeout_ms;
    float min_quality;
    bool require_liveness;
    bool capture_image;
    uint32_t scan_attempts;
} biosense_scan_opts_t;

/* Match result */
typedef struct {
    float similarity;           /* 0.0-1.0 */
    float liveness_score;       /* 0.0-1.0 */
    bool is_match;
    bool is_live;
    biosense_quality_t quality;
    uint32_t match_time_ms;
} biosense_match_result_t;

/* Configuration */
typedef struct {
    float match_threshold;          /* Default 0.85 */
    float liveness_threshold;       /* Default 0.90 */
    float quality_threshold;        /* Default 0.70 */
    uint32_t max_failed_attempts;   /* Before lockout */
    uint32_t lockout_duration_sec;
    bool require_liveness;
    bool store_raw_images;
    char device_path[256];          /* e.g., /dev/biosense0 */
} biosense_config_t;

/* Driver context (opaque) */
typedef struct biosense_driver biosense_driver_t;

/*
 * Driver Registration API (for hardware drivers)
 */

/* Driver operations table */
typedef struct {
    const char *name;
    biosense_type_t type;

    /* Lifecycle */
    int (*probe)(biosense_driver_t *drv, const char *device);
    void (*disconnect)(biosense_driver_t *drv);

    /* Scanning */
    int (*start_scan)(biosense_driver_t *drv);
    int (*stop_scan)(biosense_driver_t *drv);
    int (*get_scan_data)(biosense_driver_t *drv, void *buffer, size_t *size);

    /* Device control */
    int (*get_info)(biosense_driver_t *drv, biosense_device_info_t *info);
    int (*calibrate)(biosense_driver_t *drv);
    int (*set_led)(biosense_driver_t *drv, uint8_t brightness);

    /* Raw I/O */
    int (*read)(biosense_driver_t *drv, uint8_t *buf, size_t len);
    int (*write)(biosense_driver_t *drv, const uint8_t *buf, size_t len);
    int (*ioctl)(biosense_driver_t *drv, uint32_t cmd, void *arg);

} biosense_driver_ops_t;

/* Register a hardware driver */
biosense_error_t biosense_register_driver(const biosense_driver_ops_t *ops);

/* Unregister driver */
void biosense_unregister_driver(const char *name);

/*
 * Core API
 */

/* Initialize subsystem */
biosense_error_t biosense_init(biosense_config_t *config);

/* Shutdown subsystem */
void biosense_shutdown(void);

/* Enumerate connected devices */
biosense_error_t biosense_enumerate_devices(biosense_device_info_t *devices,
                                             uint32_t max_devices,
                                             uint32_t *count);

/* Open specific device */
biosense_error_t biosense_open(const char *device_path,
                                biosense_driver_t **driver);

/* Close device */
void biosense_close(biosense_driver_t *driver);

/* Get device info */
biosense_error_t biosense_get_info(biosense_driver_t *driver,
                                    biosense_device_info_t *info);

/* Get current state */
biosense_state_t biosense_get_state(biosense_driver_t *driver);

/*
 * Scanning API
 */

/* Capture vein pattern scan */
biosense_error_t biosense_scan_vein(biosense_driver_t *driver,
                                     biosense_scan_opts_t *opts,
                                     biosense_vein_data_t *data,
                                     biosense_quality_t *quality);

/* Capture blood chemistry reading */
biosense_error_t biosense_scan_blood(biosense_driver_t *driver,
                                      biosense_scan_opts_t *opts,
                                      biosense_blood_data_t *data,
                                      biosense_quality_t *quality);

/* Perform liveness detection */
biosense_error_t biosense_check_liveness(biosense_driver_t *driver,
                                          float *score);

/*
 * Template API
 */

/* Enroll new biometric template */
biosense_error_t biosense_enroll(biosense_driver_t *driver,
                                  const char *user_id,
                                  const uint8_t *password, size_t pass_len,
                                  biosense_scan_opts_t *opts,
                                  biosense_template_t *template_out);

/* Verify against template */
biosense_error_t biosense_verify(biosense_driver_t *driver,
                                  biosense_template_t *template,
                                  const uint8_t *password, size_t pass_len,
                                  biosense_scan_opts_t *opts,
                                  biosense_match_result_t *result);

/* Update template with new scan (for drift compensation) */
biosense_error_t biosense_update_template(biosense_driver_t *driver,
                                           biosense_template_t *template,
                                           const uint8_t *password, size_t pass_len);

/* Export template to bytes (for storage) */
biosense_error_t biosense_template_export(const biosense_template_t *template,
                                           uint8_t *buffer, size_t *size);

/* Import template from bytes */
biosense_error_t biosense_template_import(biosense_template_t *template,
                                           const uint8_t *buffer, size_t size);

/* Reset lockout (admin function) */
biosense_error_t biosense_reset_lockout(biosense_template_t *template);

/*
 * Utility Functions
 */

/* Calculate template entropy */
uint32_t biosense_calculate_entropy(const biosense_vein_data_t *data);

/* Compare two vein patterns (returns similarity 0.0-1.0) */
float biosense_compare_patterns(const biosense_vein_data_t *a,
                                 const biosense_vein_data_t *b);

/* Get error string */
const char* biosense_error_string(biosense_error_t error);

/* Get state string */
const char* biosense_state_string(biosense_state_t state);

/*
 * Callbacks for async operation
 */

typedef void (*biosense_scan_callback_t)(biosense_error_t error,
                                          void *data, void *userdata);

/* Async scan with callback */
biosense_error_t biosense_scan_async(biosense_driver_t *driver,
                                      biosense_scan_opts_t *opts,
                                      biosense_scan_callback_t callback,
                                      void *userdata);

/* Cancel async operation */
void biosense_cancel_async(biosense_driver_t *driver);

#endif /* PHANTOM_BIOSENSE_H */
