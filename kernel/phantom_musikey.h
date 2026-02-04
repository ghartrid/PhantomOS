/*
 * PhantomOS MusiKey Authentication System
 *
 * Musical entropy-based authentication using one-time generated compositions
 * scrambled with user keys. Authentication verified by musical structure detection.
 */

#ifndef PHANTOM_MUSIKEY_H
#define PHANTOM_MUSIKEY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Musical constants */
#define MUSIKEY_NOTES_PER_OCTAVE    12
#define MUSIKEY_MAX_OCTAVES         8
#define MUSIKEY_TOTAL_NOTES         (MUSIKEY_NOTES_PER_OCTAVE * MUSIKEY_MAX_OCTAVES)

#define MUSIKEY_MAX_SONG_LENGTH     256   /* Maximum notes in generated song */
#define MUSIKEY_MIN_SONG_LENGTH     32    /* Minimum for sufficient entropy */
#define MUSIKEY_DEFAULT_LENGTH      64

#define MUSIKEY_MAX_KEY_SIZE        256   /* Max user key size in bytes */
#define MUSIKEY_HASH_SIZE           32    /* SHA-256 output */
#define MUSIKEY_SALT_SIZE           16

/* Note representation */
typedef enum {
    NOTE_C = 0, NOTE_CS, NOTE_D, NOTE_DS, NOTE_E, NOTE_F,
    NOTE_FS, NOTE_G, NOTE_GS, NOTE_A, NOTE_AS, NOTE_B
} musikey_note_t;

/* Musical scales for structure detection */
typedef enum {
    SCALE_CHROMATIC = 0,
    SCALE_MAJOR,
    SCALE_MINOR,
    SCALE_PENTATONIC,
    SCALE_BLUES,
    SCALE_DORIAN,
    SCALE_MIXOLYDIAN
} musikey_scale_t;

/* Time signature */
typedef struct {
    uint8_t beats_per_measure;
    uint8_t beat_unit;
} musikey_time_sig_t;

/* Single note event */
typedef struct {
    uint8_t note;           /* MIDI note number (0-127) */
    uint8_t velocity;       /* Intensity (0-127) */
    uint16_t duration;      /* Duration in milliseconds */
    uint16_t timestamp;     /* Offset from song start */
} musikey_event_t;

/* Generated song structure */
typedef struct {
    musikey_event_t events[MUSIKEY_MAX_SONG_LENGTH];
    uint32_t event_count;
    uint32_t total_duration;
    musikey_scale_t scale;
    uint8_t root_note;
    musikey_time_sig_t time_sig;
    uint8_t tempo;          /* BPM */
    uint8_t entropy_bits;   /* Estimated entropy */
} musikey_song_t;

/* Scrambled song (stored version) */
typedef struct {
    uint8_t scrambled_data[MUSIKEY_MAX_SONG_LENGTH * sizeof(musikey_event_t)];
    uint32_t data_size;
    uint8_t salt[MUSIKEY_SALT_SIZE];
    uint8_t iv[12];                                 /* AES-GCM initialization vector */
    uint8_t auth_tag[16];                          /* AES-GCM authentication tag */
    uint8_t verification_hash[MUSIKEY_HASH_SIZE];  /* Hash of original for verify */
    uint32_t scramble_iterations;
} musikey_scrambled_t;

/* User credential */
typedef struct {
    char user_id[64];
    musikey_scrambled_t scrambled_song;
    uint64_t created_timestamp;
    uint64_t last_auth_timestamp;
    uint32_t auth_attempts;
    uint32_t failed_attempts;
    bool locked;
} musikey_credential_t;

/* Musical structure analysis result */
typedef struct {
    float harmonic_score;       /* 0.0-1.0: harmonic relationships */
    float rhythm_score;         /* 0.0-1.0: rhythmic patterns */
    float melody_score;         /* 0.0-1.0: melodic contour */
    float scale_adherence;      /* 0.0-1.0: fits musical scale */
    float overall_musicality;   /* Combined score */
    bool is_valid_music;        /* Threshold check */
} musikey_analysis_t;

/* Error codes */
typedef enum {
    MUSIKEY_OK = 0,
    MUSIKEY_ERR_INVALID_INPUT,
    MUSIKEY_ERR_INSUFFICIENT_ENTROPY,
    MUSIKEY_ERR_SCRAMBLE_FAILED,
    MUSIKEY_ERR_DESCRAMBLE_FAILED,
    MUSIKEY_ERR_NOT_MUSIC,
    MUSIKEY_ERR_AUTH_FAILED,
    MUSIKEY_ERR_LOCKED,
    MUSIKEY_ERR_MEMORY,
    MUSIKEY_ERR_CRYPTO
} musikey_error_t;

/* Configuration */
typedef struct {
    uint32_t song_length;
    uint32_t scramble_iterations;
    float musicality_threshold;     /* Minimum score to pass (default 0.7) */
    uint32_t max_failed_attempts;   /* Before lockout */
    bool use_hardware_entropy;
    musikey_scale_t preferred_scale;
} musikey_config_t;

/*
 * Core API
 */

/* Initialize MusiKey system */
musikey_error_t musikey_init(musikey_config_t *config);

/* Shutdown and cleanup */
void musikey_shutdown(void);

/* Generate a new random song with musical properties */
musikey_error_t musikey_generate_song(musikey_song_t *song, uint32_t length);

/* Scramble song using user's key */
musikey_error_t musikey_scramble(const musikey_song_t *song,
                                  const uint8_t *key, size_t key_len,
                                  musikey_scrambled_t *output);

/* Descramble using user's key */
musikey_error_t musikey_descramble(const musikey_scrambled_t *scrambled,
                                    const uint8_t *key, size_t key_len,
                                    musikey_song_t *output);

/* Analyze if data represents valid music */
musikey_error_t musikey_analyze(const musikey_song_t *song,
                                 musikey_analysis_t *analysis);

/*
 * Authentication API
 */

/* Enroll new user - generates song, scrambles with key, stores credential */
musikey_error_t musikey_enroll(const char *user_id,
                                const uint8_t *key, size_t key_len,
                                musikey_credential_t *credential);

/* Authenticate user - descrambles, verifies musicality */
musikey_error_t musikey_authenticate(musikey_credential_t *credential,
                                      const uint8_t *key, size_t key_len);

/* Reset failed attempt counter (admin function) */
musikey_error_t musikey_reset_lockout(musikey_credential_t *credential);

/*
 * Entropy functions
 */

/* Calculate entropy bits in a song */
uint32_t musikey_calculate_entropy(const musikey_song_t *song);

/* Add entropy from external source */
musikey_error_t musikey_add_entropy(const uint8_t *data, size_t len);

/*
 * Musical utility functions
 */

/* Check if note fits in scale */
bool musikey_note_in_scale(uint8_t note, musikey_scale_t scale, uint8_t root);

/* Get scale intervals */
const uint8_t* musikey_get_scale_intervals(musikey_scale_t scale, uint8_t *count);

/* Calculate harmonic relationship between two notes */
float musikey_harmonic_ratio(uint8_t note1, uint8_t note2);

/* Convert song to playable audio buffer (for verification/demo) */
musikey_error_t musikey_render_audio(const musikey_song_t *song,
                                      int16_t *buffer, size_t buffer_samples,
                                      uint32_t sample_rate);

/*
 * Serialization
 */

/* Export credential to bytes */
musikey_error_t musikey_credential_export(const musikey_credential_t *cred,
                                           uint8_t *buffer, size_t *size);

/* Import credential from bytes */
musikey_error_t musikey_credential_import(musikey_credential_t *cred,
                                           const uint8_t *buffer, size_t size);

#endif /* PHANTOM_MUSIKEY_H */
