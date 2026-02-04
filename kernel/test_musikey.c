/*
 * MusiKey Authentication Test Suite
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "phantom_musikey.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("Testing: %s... ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

void test_init(void) {
    TEST("musikey_init");

    musikey_config_t config = {
        .song_length = 64,
        .scramble_iterations = 1000,
        .musicality_threshold = 0.6f,
        .max_failed_attempts = 3,
        .use_hardware_entropy = false,
        .preferred_scale = SCALE_PENTATONIC
    };

    musikey_error_t err = musikey_init(&config);
    if (err == MUSIKEY_OK) {
        PASS();
    } else {
        FAIL("init failed");
    }
}

void test_song_generation(void) {
    TEST("musikey_generate_song");

    musikey_song_t song;
    musikey_error_t err = musikey_generate_song(&song, 64);

    if (err != MUSIKEY_OK) {
        FAIL("generation failed");
        return;
    }

    if (song.event_count != 64) {
        FAIL("wrong event count");
        return;
    }

    if (song.entropy_bits < 50) {
        FAIL("insufficient entropy");
        return;
    }

    PASS();
    printf("  Generated song: %u notes, %u ms duration, %u bits entropy\n",
           song.event_count, song.total_duration, song.entropy_bits);
}

void test_musical_analysis(void) {
    TEST("musikey_analyze (valid music)");

    musikey_song_t song;
    musikey_generate_song(&song, 64);

    musikey_analysis_t analysis;
    musikey_error_t err = musikey_analyze(&song, &analysis);

    if (err != MUSIKEY_OK) {
        FAIL("analysis failed");
        return;
    }

    if (!analysis.is_valid_music) {
        FAIL("generated song not recognized as music");
        return;
    }

    PASS();
    printf("  Harmonic: %.2f, Melody: %.2f, Rhythm: %.2f, Scale: %.2f, Overall: %.2f\n",
           analysis.harmonic_score, analysis.melody_score,
           analysis.rhythm_score, analysis.scale_adherence,
           analysis.overall_musicality);
}

void test_scramble_descramble(void) {
    TEST("musikey_scramble/descramble");

    musikey_song_t original, recovered;
    musikey_scrambled_t scrambled;

    musikey_generate_song(&original, 64);

    const uint8_t key[] = "my_secret_passphrase_123";

    musikey_error_t err = musikey_scramble(&original, key, sizeof(key), &scrambled);
    if (err != MUSIKEY_OK) {
        FAIL("scramble failed");
        return;
    }

    err = musikey_descramble(&scrambled, key, sizeof(key), &recovered);
    if (err != MUSIKEY_OK) {
        FAIL("descramble failed");
        return;
    }

    /* Verify songs match */
    if (memcmp(original.events, recovered.events,
               original.event_count * sizeof(musikey_event_t)) != 0) {
        FAIL("recovered song doesn't match original");
        return;
    }

    PASS();
}

void test_wrong_key(void) {
    TEST("wrong key rejection");

    musikey_song_t original, recovered;
    musikey_scrambled_t scrambled;

    musikey_generate_song(&original, 64);

    const uint8_t correct_key[] = "correct_password";
    const uint8_t wrong_key[] = "wrong_password!!";

    musikey_scramble(&original, correct_key, sizeof(correct_key), &scrambled);

    musikey_error_t err = musikey_descramble(&scrambled, wrong_key, sizeof(wrong_key), &recovered);

    if (err == MUSIKEY_ERR_DESCRAMBLE_FAILED) {
        PASS();
    } else {
        FAIL("wrong key was accepted");
    }
}

void test_enrollment(void) {
    TEST("musikey_enroll");

    musikey_credential_t cred;
    const uint8_t key[] = "user_master_key_phrase";

    musikey_error_t err = musikey_enroll("testuser", key, sizeof(key), &cred);

    if (err != MUSIKEY_OK) {
        FAIL("enrollment failed");
        return;
    }

    if (strcmp(cred.user_id, "testuser") != 0) {
        FAIL("user_id mismatch");
        return;
    }

    if (cred.scrambled_song.data_size == 0) {
        FAIL("no scrambled data");
        return;
    }

    PASS();
}

void test_authentication_success(void) {
    TEST("musikey_authenticate (correct key)");

    musikey_credential_t cred;
    const uint8_t key[] = "authenticate_me_123";

    musikey_enroll("authuser", key, sizeof(key), &cred);

    musikey_error_t err = musikey_authenticate(&cred, key, sizeof(key));

    if (err == MUSIKEY_OK) {
        PASS();
    } else {
        FAIL("authentication with correct key failed");
    }
}

void test_authentication_failure(void) {
    TEST("musikey_authenticate (wrong key)");

    musikey_credential_t cred;
    const uint8_t correct_key[] = "the_real_password";
    const uint8_t wrong_key[] = "not_the_password";

    musikey_enroll("secureuser", correct_key, sizeof(correct_key), &cred);

    musikey_error_t err = musikey_authenticate(&cred, wrong_key, sizeof(wrong_key));

    if (err == MUSIKEY_ERR_AUTH_FAILED) {
        PASS();
    } else {
        FAIL("authentication with wrong key should fail");
    }
}

void test_lockout(void) {
    TEST("account lockout after failed attempts");

    musikey_config_t config = {
        .song_length = 32,
        .scramble_iterations = 100,
        .musicality_threshold = 0.5f,
        .max_failed_attempts = 3,
        .use_hardware_entropy = false,
        .preferred_scale = SCALE_PENTATONIC
    };
    musikey_init(&config);

    musikey_credential_t cred;
    const uint8_t correct_key[] = "lockout_test_key";
    const uint8_t wrong_key[] = "bad_key_attempt";

    musikey_enroll("lockuser", correct_key, sizeof(correct_key), &cred);

    /* Try wrong key 3 times */
    musikey_authenticate(&cred, wrong_key, sizeof(wrong_key));
    musikey_authenticate(&cred, wrong_key, sizeof(wrong_key));
    musikey_authenticate(&cred, wrong_key, sizeof(wrong_key));

    /* Now even correct key should fail */
    musikey_error_t err = musikey_authenticate(&cred, correct_key, sizeof(correct_key));

    if (err == MUSIKEY_ERR_LOCKED && cred.locked) {
        PASS();
    } else {
        FAIL("account should be locked");
    }
}

void test_entropy_calculation(void) {
    TEST("entropy calculation");

    musikey_song_t song;
    musikey_generate_song(&song, 128);

    uint32_t entropy = musikey_calculate_entropy(&song);

    /* 128 notes should have substantial entropy */
    if (entropy >= 80) {
        PASS();
        printf("  Calculated entropy: %u bits for %u notes\n", entropy, song.event_count);
    } else {
        FAIL("entropy too low");
    }
}

void test_scale_detection(void) {
    TEST("scale adherence");

    /* C major scale notes */
    uint8_t c_major_notes[] = {60, 62, 64, 65, 67, 69, 71, 72};

    int in_scale = 0;
    for (int i = 0; i < 8; i++) {
        if (musikey_note_in_scale(c_major_notes[i], SCALE_MAJOR, NOTE_C)) {
            in_scale++;
        }
    }

    if (in_scale == 8) {
        PASS();
    } else {
        FAIL("C major notes not detected in C major scale");
    }
}

void test_credential_serialization(void) {
    TEST("credential export/import");

    musikey_credential_t cred, imported;
    const uint8_t key[] = "serialize_test";

    musikey_enroll("serializeuser", key, sizeof(key), &cred);

    uint8_t buffer[4096];
    size_t size = sizeof(buffer);

    musikey_error_t err = musikey_credential_export(&cred, buffer, &size);
    if (err != MUSIKEY_OK) {
        FAIL("export failed");
        return;
    }

    err = musikey_credential_import(&imported, buffer, size);
    if (err != MUSIKEY_OK) {
        FAIL("import failed");
        return;
    }

    /* Verify imported credential works */
    err = musikey_authenticate(&imported, key, sizeof(key));
    if (err == MUSIKEY_OK) {
        PASS();
    } else {
        FAIL("imported credential authentication failed");
    }
}

void test_unique_songs(void) {
    TEST("unique song generation");

    musikey_song_t song1, song2;
    musikey_generate_song(&song1, 64);
    musikey_generate_song(&song2, 64);

    /* Songs should be different */
    int differences = 0;
    for (uint32_t i = 0; i < 64; i++) {
        if (song1.events[i].note != song2.events[i].note) {
            differences++;
        }
    }

    if (differences > 30) {  /* At least half should differ */
        PASS();
        printf("  %d/64 notes differ between generated songs\n", differences);
    } else {
        FAIL("songs too similar");
    }
}

int main(void) {
    printf("\n=== MusiKey Authentication Test Suite ===\n\n");

    test_init();
    test_song_generation();
    test_musical_analysis();
    test_scramble_descramble();
    test_wrong_key();
    test_enrollment();
    test_authentication_success();
    test_authentication_failure();
    test_lockout();
    test_entropy_calculation();
    test_scale_detection();
    test_credential_serialization();
    test_unique_songs();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);

    musikey_shutdown();

    return tests_failed > 0 ? 1 : 0;
}
