/*
 * PhantomOS MusiKey Authentication System - Implementation
 *
 * Uses musical entropy for authentication: generates unique songs,
 * scrambles them with user keys, verifies by detecting musical structure.
 */

#include "phantom_musikey.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* Default configuration */
static musikey_config_t g_config = {
    .song_length = MUSIKEY_DEFAULT_LENGTH,
    .scramble_iterations = 10000,
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

/* Harmonic ratios for consonance detection (used in documentation/future features) */
/* Ratios: Unison=1.0, Fifth=1.5(3:2), Fourth=1.333(4:3), MajThird=1.25(5:4), MinThird=1.2(6:5) */

/* Simple PRNG for reproducible generation (seeded by entropy) */
static uint64_t g_prng_state = 0;

static uint64_t prng_next(void) {
    g_prng_state ^= g_prng_state >> 12;
    g_prng_state ^= g_prng_state << 25;
    g_prng_state ^= g_prng_state >> 27;
    return g_prng_state * 0x2545F4914F6CDD1DULL;
}

static void prng_seed(uint64_t seed) {
    g_prng_state = seed ? seed : 0x853C49E6748FEA9BULL;
    for (int i = 0; i < 10; i++) prng_next();
}

/* Simple SHA-256-like hash (simplified for demonstration) */
static void musikey_hash(const uint8_t *data, size_t len, uint8_t *output) {
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    for (size_t i = 0; i < len; i++) {
        uint32_t idx = i % 8;
        h[idx] = ((h[idx] << 5) | (h[idx] >> 27)) ^ data[i];
        h[(idx + 1) % 8] += h[idx] * 0x5bd1e995;
    }

    for (int i = 0; i < 8; i++) {
        output[i * 4 + 0] = (h[i] >> 24) & 0xFF;
        output[i * 4 + 1] = (h[i] >> 16) & 0xFF;
        output[i * 4 + 2] = (h[i] >> 8) & 0xFF;
        output[i * 4 + 3] = h[i] & 0xFF;
    }
}

/* XOR-based stream cipher for scrambling */
static void musikey_cipher(uint8_t *data, size_t len,
                           const uint8_t *key, size_t key_len,
                           const uint8_t *salt, uint32_t iterations) {
    uint8_t keystream[MUSIKEY_HASH_SIZE];
    uint8_t block[MUSIKEY_HASH_SIZE + MUSIKEY_SALT_SIZE + 4];

    for (uint32_t iter = 0; iter < iterations; iter++) {
        memset(block, 0, sizeof(block));  /* Initialize to avoid uninitialized memory */
        memcpy(block, key, key_len < MUSIKEY_HASH_SIZE ? key_len : MUSIKEY_HASH_SIZE);
        memcpy(block + MUSIKEY_HASH_SIZE, salt, MUSIKEY_SALT_SIZE);
        block[MUSIKEY_HASH_SIZE + MUSIKEY_SALT_SIZE + 0] = (iter >> 24) & 0xFF;
        block[MUSIKEY_HASH_SIZE + MUSIKEY_SALT_SIZE + 1] = (iter >> 16) & 0xFF;
        block[MUSIKEY_HASH_SIZE + MUSIKEY_SALT_SIZE + 2] = (iter >> 8) & 0xFF;
        block[MUSIKEY_HASH_SIZE + MUSIKEY_SALT_SIZE + 3] = iter & 0xFF;

        musikey_hash(block, sizeof(block), keystream);

        for (size_t i = 0; i < len; i++) {
            data[i] ^= keystream[i % MUSIKEY_HASH_SIZE];
        }
    }
}

musikey_error_t musikey_init(musikey_config_t *config) {
    if (config) {
        memcpy(&g_config, config, sizeof(musikey_config_t));
    }

    /* Seed PRNG with time-based entropy */
    uint64_t seed = (uint64_t)time(NULL);
    seed ^= (uint64_t)clock() << 32;
    prng_seed(seed);

    g_initialized = true;
    return MUSIKEY_OK;
}

void musikey_shutdown(void) {
    memset(&g_config, 0, sizeof(g_config));
    g_prng_state = 0;
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

    /* Choose musical parameters */
    song->scale = g_config.preferred_scale;
    song->root_note = prng_next() % 12;  /* Random root note */
    song->tempo = 80 + (prng_next() % 80); /* 80-160 BPM */
    song->time_sig.beats_per_measure = 4;
    song->time_sig.beat_unit = 4;

    uint8_t scale_count;
    const uint8_t *scale_intervals = musikey_get_scale_intervals(song->scale, &scale_count);

    /* Generate notes with musical coherence */
    uint8_t current_note = 48 + song->root_note; /* Start in middle octave */
    uint16_t current_time = 0;
    uint16_t beat_duration = 60000 / song->tempo; /* ms per beat */

    for (uint32_t i = 0; i < length; i++) {
        musikey_event_t *event = &song->events[i];

        /* Melodic movement - prefer stepwise motion with occasional leaps */
        int movement = (prng_next() % 5) - 2; /* -2 to +2 scale degrees */
        if (prng_next() % 8 == 0) {
            movement = (prng_next() % 9) - 4; /* Occasional larger leap */
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
        event->velocity = 60 + (prng_next() % 60); /* 60-120 */

        /* Rhythmic variety */
        uint32_t rhythm_choice = prng_next() % 16;
        if (rhythm_choice < 4) {
            event->duration = beat_duration / 4;  /* Sixteenth */
        } else if (rhythm_choice < 10) {
            event->duration = beat_duration / 2;  /* Eighth */
        } else if (rhythm_choice < 14) {
            event->duration = beat_duration;      /* Quarter */
        } else {
            event->duration = beat_duration * 2;  /* Half */
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

    /* Calculate entropy based on note variety and unpredictability */
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

    /* Combine entropies, scale to bits */
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

    /* Generate salt */
    for (int i = 0; i < MUSIKEY_SALT_SIZE; i++) {
        output->salt[i] = prng_next() & 0xFF;
    }

    /* Hash original song for verification */
    musikey_hash((const uint8_t*)song->events,
                 song->event_count * sizeof(musikey_event_t),
                 output->verification_hash);

    /* Copy and scramble */
    output->data_size = song->event_count * sizeof(musikey_event_t);
    memcpy(output->scrambled_data, song->events, output->data_size);

    output->scramble_iterations = g_config.scramble_iterations;
    musikey_cipher(output->scrambled_data, output->data_size,
                   key, key_len, output->salt, output->scramble_iterations);

    return MUSIKEY_OK;
}

musikey_error_t musikey_descramble(const musikey_scrambled_t *scrambled,
                                    const uint8_t *key, size_t key_len,
                                    musikey_song_t *output) {
    if (!scrambled || !key || !output || key_len == 0) {
        return MUSIKEY_ERR_INVALID_INPUT;
    }

    memset(output, 0, sizeof(musikey_song_t));

    /* Copy scrambled data */
    uint8_t temp[MUSIKEY_MAX_SONG_LENGTH * sizeof(musikey_event_t)];
    memcpy(temp, scrambled->scrambled_data, scrambled->data_size);

    /* Descramble */
    musikey_cipher(temp, scrambled->data_size,
                   key, key_len, scrambled->salt, scrambled->scramble_iterations);

    /* Verify hash */
    uint8_t hash[MUSIKEY_HASH_SIZE];
    musikey_hash(temp, scrambled->data_size, hash);

    if (memcmp(hash, scrambled->verification_hash, MUSIKEY_HASH_SIZE) != 0) {
        return MUSIKEY_ERR_DESCRAMBLE_FAILED;
    }

    /* Restore song structure */
    output->event_count = scrambled->data_size / sizeof(musikey_event_t);
    memcpy(output->events, temp, scrambled->data_size);

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

    /* Analyze harmonic relationships between consecutive notes */
    for (uint32_t i = 1; i < song->event_count; i++) {
        harmonic_sum += musikey_harmonic_ratio(song->events[i-1].note,
                                               song->events[i].note);

        /* Melodic contour - prefer stepwise motion */
        int interval = abs((int)song->events[i].note - (int)song->events[i-1].note);
        if (interval <= 2) melodic_sum += 1.0f;
        else if (interval <= 4) melodic_sum += 0.7f;
        else if (interval <= 7) melodic_sum += 0.4f;
        else melodic_sum += 0.2f;

        /* Check scale adherence */
        if (musikey_note_in_scale(song->events[i].note, song->scale, song->root_note)) {
            scale_hits++;
        }
    }

    /* Rhythm analysis - check for regular patterns */
    uint16_t durations[MUSIKEY_MAX_SONG_LENGTH];
    for (uint32_t i = 0; i < song->event_count; i++) {
        durations[i] = song->events[i].duration;
    }

    /* Check for repeating rhythmic patterns */
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

    /* Calculate final scores */
    analysis->harmonic_score = harmonic_sum / (song->event_count - 1);
    analysis->melody_score = melodic_sum / (song->event_count - 1);
    analysis->rhythm_score = rhythm_regularity;
    analysis->scale_adherence = (float)scale_hits / (song->event_count - 1);

    /* Overall musicality */
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

    /* Generate unique song */
    musikey_song_t song;
    musikey_error_t err = musikey_generate_song(&song, g_config.song_length);
    if (err != MUSIKEY_OK) return err;

    /* Verify it's actually musical */
    musikey_analysis_t analysis;
    err = musikey_analyze(&song, &analysis);
    if (err != MUSIKEY_OK) return err;
    if (!analysis.is_valid_music) return MUSIKEY_ERR_NOT_MUSIC;

    /* Scramble with user's key */
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

    /* Try to descramble */
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

    /* Verify the descrambled data is valid music */
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

    /* Success */
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

    uint64_t additional = 0;
    for (size_t i = 0; i < len && i < 8; i++) {
        additional |= ((uint64_t)data[i]) << (i * 8);
    }

    prng_seed(g_prng_state ^ additional);
    return MUSIKEY_OK;
}

/* Simple audio rendering (sine wave synthesis) */
musikey_error_t musikey_render_audio(const musikey_song_t *song,
                                      int16_t *buffer, size_t buffer_samples,
                                      uint32_t sample_rate) {
    if (!song || !buffer || buffer_samples == 0) {
        return MUSIKEY_ERR_INVALID_INPUT;
    }

    memset(buffer, 0, buffer_samples * sizeof(int16_t));

    for (uint32_t i = 0; i < song->event_count; i++) {
        const musikey_event_t *event = &song->events[i];

        /* Convert MIDI note to frequency: f = 440 * 2^((n-69)/12) */
        float freq = 440.0f * powf(2.0f, (event->note - 69) / 12.0f);

        uint32_t start_sample = (event->timestamp * sample_rate) / 1000;
        uint32_t duration_samples = (event->duration * sample_rate) / 1000;
        uint32_t end_sample = start_sample + duration_samples;

        if (end_sample > buffer_samples) end_sample = buffer_samples;

        float amplitude = (event->velocity / 127.0f) * 16000.0f;

        for (uint32_t s = start_sample; s < end_sample; s++) {
            float t = (float)(s - start_sample) / sample_rate;
            float envelope = 1.0f;

            /* Simple ADSR envelope */
            uint32_t rel_sample = s - start_sample;
            if (rel_sample < duration_samples / 10) {
                envelope = (float)rel_sample / (duration_samples / 10); /* Attack */
            } else if (rel_sample > duration_samples * 9 / 10) {
                envelope = (float)(duration_samples - rel_sample) / (duration_samples / 10); /* Release */
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
