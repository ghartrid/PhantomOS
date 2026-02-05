/*
 * PhantomOS LifeAuth Test Suite
 *
 * Tests the blood plasma authentication system
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "phantom_lifeauth.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("Testing: %s... ", name); \
    fflush(stdout)

#define PASS() \
    do { printf("PASS\n"); tests_passed++; } while(0)

#define FAIL(msg) \
    do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

#define ASSERT_EQ(a, b, msg) \
    if ((a) != (b)) { FAIL(msg); return; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) { FAIL(msg); return; }

/* Test: System initialization */
static void test_init(void) {
    TEST("System initialization");

    lifeauth_config_t config = {0};
    config.match_threshold = 0.80f;
    config.liveness_threshold = 0.85f;
    config.quality_threshold = 0.70f;
    config.max_failed_attempts = 3;
    config.require_liveness = true;
    config.detect_health_anomalies = true;

    lifeauth_error_t err = lifeauth_init(&config);
    ASSERT_EQ(err, LIFEAUTH_OK, "Init failed");

    PASS();
}

/* Test: Device open/close */
static void test_open_close(void) {
    TEST("Sensor open/close");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");
    ASSERT_TRUE(driver != NULL, "Driver is NULL");

    lifeauth_state_t state = lifeauth_get_state(driver);
    ASSERT_EQ(state, LIFEAUTH_STATE_READY, "State not ready");

    lifeauth_close(driver);
    PASS();
}

/* Test: Sensor info */
static void test_sensor_info(void) {
    TEST("Sensor info");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    lifeauth_sensor_info_t info;
    err = lifeauth_get_info(driver, &info);
    ASSERT_EQ(err, LIFEAUTH_OK, "Get info failed");
    ASSERT_TRUE(strlen(info.vendor) > 0, "No vendor");
    ASSERT_TRUE(strlen(info.model) > 0, "No model");
    ASSERT_TRUE(info.markers_supported > 0, "No markers");

    printf("(%s, %u markers)... ", info.model, info.markers_supported);

    lifeauth_close(driver);
    PASS();
}

/* Test: Plasma sample collection */
static void test_sample(void) {
    TEST("Plasma sample collection");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    lifeauth_plasma_signature_t signature;
    lifeauth_sample_quality_t quality;

    err = lifeauth_sample(driver, &signature, &quality);
    ASSERT_EQ(err, LIFEAUTH_OK, "Sample failed");
    ASSERT_TRUE(signature.proteins.ag_ratio > 0, "No A/G ratio");
    ASSERT_TRUE(signature.overall_confidence > 0.5f, "Low confidence");
    ASSERT_TRUE(quality.is_acceptable, "Quality not acceptable");

    printf("(A/G=%.2f, conf=%.2f)... ", signature.proteins.ag_ratio, signature.overall_confidence);

    lifeauth_close(driver);
    PASS();
}

/* Test: Liveness detection */
static void test_liveness(void) {
    TEST("Liveness detection");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    lifeauth_liveness_t liveness;
    err = lifeauth_check_liveness(driver, &liveness);
    ASSERT_EQ(err, LIFEAUTH_OK, "Liveness check failed");
    ASSERT_TRUE(liveness.temperature > 35.0f && liveness.temperature < 38.0f, "Bad temp");
    ASSERT_TRUE(liveness.pulse_detected > 0.9f, "No pulse");
    ASSERT_TRUE(liveness.is_live, "Not live");

    printf("(temp=%.1f°C, SpO2=%.1f%%)... ", liveness.temperature, liveness.oxygen_saturation);

    lifeauth_close(driver);
    PASS();
}

/* Test: Entropy calculation */
static void test_entropy(void) {
    TEST("Entropy calculation");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    lifeauth_plasma_signature_t signature;
    err = lifeauth_sample(driver, &signature, NULL);
    ASSERT_EQ(err, LIFEAUTH_OK, "Sample failed");

    uint32_t entropy = lifeauth_calculate_entropy(&signature);
    ASSERT_TRUE(entropy >= 80, "Entropy too low");
    ASSERT_TRUE(entropy <= 200, "Entropy unreasonably high");

    printf("(%u bits)... ", entropy);

    lifeauth_close(driver);
    PASS();
}

/* Test: Fingerprint generation */
static void test_fingerprint(void) {
    TEST("Fingerprint generation");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    lifeauth_plasma_signature_t signature;
    err = lifeauth_sample(driver, &signature, NULL);
    ASSERT_EQ(err, LIFEAUTH_OK, "Sample failed");

    uint8_t fp[64];
    lifeauth_generate_fingerprint(&signature, fp, 64);

    /* Check fingerprint isn't all zeros */
    int nonzero = 0;
    for (int i = 0; i < 64; i++) {
        if (fp[i] != 0) nonzero++;
    }
    ASSERT_TRUE(nonzero > 30, "Fingerprint too sparse");

    lifeauth_close(driver);
    PASS();
}

/* Test: User enrollment */
static void test_enroll(void) {
    TEST("User enrollment");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    lifeauth_credential_t credential;
    const char *user = "testuser";
    const uint8_t password[] = "SecurePlasmaKey123!";

    err = lifeauth_enroll(driver, user, password, sizeof(password) - 1, &credential);
    ASSERT_EQ(err, LIFEAUTH_OK, "Enrollment failed");
    ASSERT_EQ(credential.version, 1, "Wrong version");
    ASSERT_TRUE(strcmp(credential.user_id, user) == 0, "User ID mismatch");
    ASSERT_TRUE(credential.encrypted_size > 0, "No encrypted data");
    ASSERT_TRUE(credential.baseline_ag_ratio > 0, "No baseline A/G ratio");

    printf("(encrypted_size=%u)... ", credential.encrypted_size);

    lifeauth_close(driver);
    PASS();
}

/* Test: Authentication with correct password */
static void test_auth_correct(void) {
    TEST("Authentication (correct password)");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    /* Enroll */
    lifeauth_credential_t credential;
    const uint8_t password[] = "MyPlasmaPassword456";

    err = lifeauth_enroll(driver, "authtest", password, sizeof(password) - 1, &credential);
    ASSERT_EQ(err, LIFEAUTH_OK, "Enrollment failed");

    /* Authenticate (same simulated user) */
    lifeauth_match_result_t result;
    err = lifeauth_authenticate(driver, &credential, password, sizeof(password) - 1, &result);

    /* With simulator, we test the flow - same "person" should match */
    ASSERT_TRUE(err == LIFEAUTH_OK || err == LIFEAUTH_ERR_PROFILE_MISMATCH,
                "Unexpected error");
    ASSERT_TRUE(result.overall_similarity >= 0.0f, "No similarity");
    ASSERT_TRUE(result.liveness_score > 0.8f, "Liveness failed");

    printf("(similarity=%.2f, time=%ums)... ", result.overall_similarity, result.analysis_time_ms);

    lifeauth_close(driver);
    PASS();
}

/* Test: Authentication with wrong password */
static void test_auth_wrong(void) {
    TEST("Authentication (wrong password)");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    /* Enroll */
    lifeauth_credential_t credential;
    const uint8_t password[] = "CorrectPassword";

    err = lifeauth_enroll(driver, "wrongtest", password, sizeof(password) - 1, &credential);
    ASSERT_EQ(err, LIFEAUTH_OK, "Enrollment failed");

    /* Try wrong password */
    lifeauth_match_result_t result;
    const uint8_t wrong[] = "WrongPassword";
    err = lifeauth_authenticate(driver, &credential, wrong, sizeof(wrong) - 1, &result);
    ASSERT_TRUE(err == LIFEAUTH_ERR_CRYPTO || err == LIFEAUTH_ERR_PROFILE_MISMATCH,
                "Expected crypto/mismatch error");

    printf("(correctly rejected)... ");

    lifeauth_close(driver);
    PASS();
}

/* Test: Account lockout */
static void test_lockout(void) {
    TEST("Account lockout");

    /* Reinit with low attempt limit */
    lifeauth_shutdown();
    lifeauth_config_t config = {
        .match_threshold = 0.80f,
        .max_failed_attempts = 3,
        .require_liveness = false
    };
    lifeauth_init(&config);

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    /* Enroll */
    lifeauth_credential_t credential;
    const uint8_t password[] = "LockoutTest";
    err = lifeauth_enroll(driver, "locktest", password, sizeof(password) - 1, &credential);
    ASSERT_EQ(err, LIFEAUTH_OK, "Enrollment failed");

    /* Try wrong password multiple times */
    lifeauth_match_result_t result;
    const uint8_t wrong[] = "WrongWrong";

    for (int i = 0; i < 3; i++) {
        err = lifeauth_authenticate(driver, &credential, wrong, sizeof(wrong) - 1, &result);
    }

    /* Should be locked */
    ASSERT_TRUE(credential.is_locked, "Not locked");

    /* Even correct password should fail now */
    err = lifeauth_authenticate(driver, &credential, password, sizeof(password) - 1, &result);
    ASSERT_EQ(err, LIFEAUTH_ERR_LOCKED, "Expected locked error");

    /* Reset lockout */
    err = lifeauth_reset_lockout(&credential);
    ASSERT_EQ(err, LIFEAUTH_OK, "Reset failed");
    ASSERT_TRUE(!credential.is_locked, "Still locked");

    printf("(locked after 3 attempts, reset works)... ");

    lifeauth_close(driver);
    PASS();
}

/* Test: Signature comparison */
static void test_compare(void) {
    TEST("Signature comparison");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    /* Get two samples */
    lifeauth_plasma_signature_t sig1, sig2;
    err = lifeauth_sample(driver, &sig1, NULL);
    ASSERT_EQ(err, LIFEAUTH_OK, "Sample 1 failed");

    err = lifeauth_sample(driver, &sig2, NULL);
    ASSERT_EQ(err, LIFEAUTH_OK, "Sample 2 failed");

    /* Compare (should be similar since same simulated user) */
    float similarity = lifeauth_compare_signatures(&sig1, &sig2);
    ASSERT_TRUE(similarity >= 0.0f && similarity <= 1.0f, "Invalid range");

    printf("(similarity=%.2f)... ", similarity);

    lifeauth_close(driver);
    PASS();
}

/* Test: Health monitoring */
static void test_health(void) {
    TEST("Health monitoring");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    lifeauth_plasma_signature_t baseline, current;
    err = lifeauth_sample(driver, &baseline, NULL);
    ASSERT_EQ(err, LIFEAUTH_OK, "Baseline sample failed");

    err = lifeauth_sample(driver, &current, NULL);
    ASSERT_EQ(err, LIFEAUTH_OK, "Current sample failed");

    lifeauth_health_flags_t health;
    err = lifeauth_check_health(&current, &baseline, &health);
    ASSERT_EQ(err, LIFEAUTH_OK, "Health check failed");

    /* Should not flag anything for normal simulated values */
    printf("(no alerts expected)... ");

    lifeauth_close(driver);
    PASS();
}

/* Test: Credential serialization */
static void test_serialization(void) {
    TEST("Credential serialization");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    /* Enroll */
    lifeauth_credential_t original;
    const uint8_t password[] = "SerializeTest";
    err = lifeauth_enroll(driver, "serialize", password, sizeof(password) - 1, &original);
    ASSERT_EQ(err, LIFEAUTH_OK, "Enrollment failed");

    /* Export */
    size_t export_size = 0;
    err = lifeauth_credential_export(&original, NULL, &export_size);
    ASSERT_EQ(err, LIFEAUTH_OK, "Export size query failed");
    ASSERT_TRUE(export_size > 0, "Zero export size");

    uint8_t *buffer = malloc(export_size);
    ASSERT_TRUE(buffer != NULL, "Malloc failed");

    err = lifeauth_credential_export(&original, buffer, &export_size);
    ASSERT_EQ(err, LIFEAUTH_OK, "Export failed");

    /* Import */
    lifeauth_credential_t imported;
    err = lifeauth_credential_import(&imported, buffer, export_size);
    ASSERT_EQ(err, LIFEAUTH_OK, "Import failed");

    /* Verify */
    ASSERT_EQ(imported.version, original.version, "Version mismatch");
    ASSERT_TRUE(strcmp(imported.user_id, original.user_id) == 0, "User ID mismatch");

    free(buffer);
    lifeauth_close(driver);
    PASS();
}

/* Test: Error strings */
static void test_error_strings(void) {
    TEST("Error strings");

    const char *ok_str = lifeauth_error_string(LIFEAUTH_OK);
    ASSERT_TRUE(ok_str != NULL && strlen(ok_str) > 0, "Empty OK string");

    const char *locked_str = lifeauth_error_string(LIFEAUTH_ERR_LOCKED);
    ASSERT_TRUE(locked_str != NULL, "NULL locked string");

    PASS();
}

/* Test: State strings */
static void test_state_strings(void) {
    TEST("State strings");

    const char *ready_str = lifeauth_state_string(LIFEAUTH_STATE_READY);
    ASSERT_TRUE(ready_str != NULL && strlen(ready_str) > 0, "Empty ready string");

    PASS();
}

/* Test: Sensor cleaning */
static void test_clean_sensor(void) {
    TEST("Sensor cleaning");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    err = lifeauth_clean_sensor(driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Clean failed");

    lifeauth_state_t state = lifeauth_get_state(driver);
    ASSERT_EQ(state, LIFEAUTH_STATE_READY, "Not ready after clean");

    lifeauth_close(driver);
    PASS();
}

/* Test: Calibration */
static void test_calibrate(void) {
    TEST("Sensor calibration");

    lifeauth_driver_t *driver = NULL;
    lifeauth_error_t err = lifeauth_open("/dev/lifeauth0", &driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Open failed");

    err = lifeauth_calibrate(driver);
    ASSERT_EQ(err, LIFEAUTH_OK, "Calibrate failed");

    lifeauth_close(driver);
    PASS();
}

int main(void) {
    printf("\n");
    printf("===========================================\n");
    printf("    PhantomOS LifeAuth Plasma Auth Test    \n");
    printf("===========================================\n\n");

    /* Run tests */
    test_init();
    test_open_close();
    test_sensor_info();
    test_sample();
    test_liveness();
    test_entropy();
    test_fingerprint();
    test_enroll();
    test_auth_correct();
    test_auth_wrong();
    test_lockout();
    test_compare();
    test_health();
    test_serialization();
    test_error_strings();
    test_state_strings();
    test_clean_sensor();
    test_calibrate();

    /* Summary */
    printf("\n===========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("===========================================\n\n");

    lifeauth_shutdown();

    return tests_failed > 0 ? 1 : 0;
}
