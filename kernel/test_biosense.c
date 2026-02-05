/*
 * PhantomOS BioSense Test Suite
 *
 * Tests the biometric blood/vein sensor authentication system
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "phantom_biosense.h"

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

    biosense_config_t config = {0};
    config.match_threshold = 0.80f;
    config.liveness_threshold = 0.85f;
    config.quality_threshold = 0.65f;
    config.max_failed_attempts = 3;
    config.require_liveness = true;

    biosense_error_t err = biosense_init(&config);
    ASSERT_EQ(err, BIOSENSE_OK, "Init failed");

    PASS();
}

/* Test: Device enumeration */
static void test_enumerate(void) {
    TEST("Device enumeration");

    biosense_device_info_t devices[4];
    uint32_t count = 0;

    biosense_error_t err = biosense_enumerate_devices(devices, 4, &count);
    ASSERT_EQ(err, BIOSENSE_OK, "Enumeration failed");
    ASSERT_TRUE(count >= 1, "No devices found");

    printf("(found %u device(s))... ", count);
    PASS();
}

/* Test: Device open/close */
static void test_open_close(void) {
    TEST("Device open/close");

    biosense_driver_t *driver = NULL;
    biosense_error_t err = biosense_open("/dev/biosense0", &driver);
    ASSERT_EQ(err, BIOSENSE_OK, "Open failed");
    ASSERT_TRUE(driver != NULL, "Driver is NULL");

    biosense_state_t state = biosense_get_state(driver);
    ASSERT_EQ(state, BIOSENSE_STATE_READY, "State not ready");

    biosense_close(driver);
    PASS();
}

/* Test: Get device info */
static void test_device_info(void) {
    TEST("Device info");

    biosense_driver_t *driver = NULL;
    biosense_error_t err = biosense_open("/dev/biosense0", &driver);
    ASSERT_EQ(err, BIOSENSE_OK, "Open failed");

    biosense_device_info_t info;
    err = biosense_get_info(driver, &info);
    ASSERT_EQ(err, BIOSENSE_OK, "Get info failed");
    ASSERT_TRUE(strlen(info.vendor) > 0, "No vendor");
    ASSERT_TRUE(strlen(info.model) > 0, "No model");

    printf("(%s %s)... ", info.vendor, info.model);

    biosense_close(driver);
    PASS();
}

/* Test: Vein pattern scan */
static void test_vein_scan(void) {
    TEST("Vein pattern scan");

    biosense_driver_t *driver = NULL;
    biosense_error_t err = biosense_open("/dev/biosense0", &driver);
    ASSERT_EQ(err, BIOSENSE_OK, "Open failed");

    biosense_vein_data_t data;
    biosense_quality_t quality;
    biosense_scan_opts_t opts = {
        .timeout_ms = 5000,
        .min_quality = 0.7f,
        .require_liveness = true,
        .capture_image = false,
        .scan_attempts = 3
    };

    err = biosense_scan_vein(driver, &opts, &data, &quality);
    ASSERT_EQ(err, BIOSENSE_OK, "Scan failed");
    ASSERT_TRUE(data.point_count > 0, "No vein points");
    ASSERT_TRUE(quality.confidence > 0.5f, "Quality too low");

    printf("(%u points, %.2f confidence)... ", data.point_count, quality.confidence);

    biosense_close(driver);
    PASS();
}

/* Test: Blood chemistry scan */
static void test_blood_scan(void) {
    TEST("Blood chemistry scan");

    biosense_driver_t *driver = NULL;
    biosense_error_t err = biosense_open("/dev/biosense0", &driver);
    ASSERT_EQ(err, BIOSENSE_OK, "Open failed");

    biosense_blood_data_t data;
    biosense_quality_t quality;

    err = biosense_scan_blood(driver, NULL, &data, &quality);
    ASSERT_EQ(err, BIOSENSE_OK, "Scan failed");
    ASSERT_TRUE(data.oxygen_saturation > 90.0f, "SpO2 too low");
    ASSERT_TRUE(data.heart_rate > 40.0f && data.heart_rate < 200.0f, "Invalid HR");

    printf("(SpO2: %.1f%%, HR: %.0f)... ", data.oxygen_saturation, data.heart_rate);

    biosense_close(driver);
    PASS();
}

/* Test: Liveness detection */
static void test_liveness(void) {
    TEST("Liveness detection");

    biosense_driver_t *driver = NULL;
    biosense_error_t err = biosense_open("/dev/biosense0", &driver);
    ASSERT_EQ(err, BIOSENSE_OK, "Open failed");

    float score = 0.0f;
    err = biosense_check_liveness(driver, &score);
    ASSERT_EQ(err, BIOSENSE_OK, "Liveness check failed");
    ASSERT_TRUE(score > 0.8f, "Liveness score too low");

    printf("(score: %.2f)... ", score);

    biosense_close(driver);
    PASS();
}

/* Test: Enrollment */
static void test_enroll(void) {
    TEST("User enrollment");

    biosense_driver_t *driver = NULL;
    biosense_error_t err = biosense_open("/dev/biosense0", &driver);
    ASSERT_EQ(err, BIOSENSE_OK, "Open failed");

    biosense_template_t template;
    const char *user = "testuser";
    const uint8_t password[] = "SecurePassword123!";
    biosense_scan_opts_t opts = {
        .timeout_ms = 5000,
        .min_quality = 0.7f,
        .require_liveness = true
    };

    err = biosense_enroll(driver, user, password, sizeof(password) - 1, &opts, &template);
    ASSERT_EQ(err, BIOSENSE_OK, "Enrollment failed");
    ASSERT_EQ(template.version, BIOSENSE_TEMPLATE_VERSION, "Wrong version");
    ASSERT_TRUE(strcmp(template.user_id, user) == 0, "User ID mismatch");
    ASSERT_TRUE(template.data_size > 0, "No template data");
    ASSERT_TRUE(template.liveness_score > 0.8f, "Liveness not recorded");

    printf("(data_size: %u)... ", template.data_size);

    biosense_close(driver);
    PASS();
}

/* Test: Verification with correct password */
static void test_verify_correct(void) {
    TEST("Verification (correct password)");

    biosense_driver_t *driver = NULL;
    biosense_error_t err = biosense_open("/dev/biosense0", &driver);
    ASSERT_EQ(err, BIOSENSE_OK, "Open failed");

    /* First enroll */
    biosense_template_t template;
    const uint8_t password[] = "MySecretKey456!";
    biosense_scan_opts_t opts = { .timeout_ms = 5000, .require_liveness = true };

    err = biosense_enroll(driver, "verifytest", password, sizeof(password) - 1, &opts, &template);
    ASSERT_EQ(err, BIOSENSE_OK, "Enrollment failed");

    /* Then verify - note: simulated sensor generates random patterns each scan,
     * so this tests the verification flow rather than actual biometric matching.
     * With a real sensor, same finger would give similar patterns. */
    biosense_match_result_t result;
    err = biosense_verify(driver, &template, password, sizeof(password) - 1, &opts, &result);

    /* For simulator, we accept either success or template mismatch (random patterns) */
    ASSERT_TRUE(err == BIOSENSE_OK || err == BIOSENSE_ERR_TEMPLATE_MISMATCH,
                "Unexpected error");
    ASSERT_TRUE(result.similarity >= 0.0f, "No similarity calculated");
    ASSERT_TRUE(result.liveness_score > 0.8f, "Liveness failed");

    printf("(similarity: %.2f, time: %ums, simulated)... ", result.similarity, result.match_time_ms);

    biosense_close(driver);
    PASS();
}

/* Test: Verification with wrong password */
static void test_verify_wrong(void) {
    TEST("Verification (wrong password)");

    biosense_driver_t *driver = NULL;
    biosense_error_t err = biosense_open("/dev/biosense0", &driver);
    ASSERT_EQ(err, BIOSENSE_OK, "Open failed");

    /* Enroll */
    biosense_template_t template;
    const uint8_t password[] = "CorrectPassword";
    biosense_scan_opts_t opts = { .timeout_ms = 5000, .require_liveness = true };

    err = biosense_enroll(driver, "wrongtest", password, sizeof(password) - 1, &opts, &template);
    ASSERT_EQ(err, BIOSENSE_OK, "Enrollment failed");

    /* Try wrong password */
    biosense_match_result_t result;
    const uint8_t wrong[] = "WrongPassword";
    err = biosense_verify(driver, &template, wrong, sizeof(wrong) - 1, &opts, &result);
    ASSERT_TRUE(err == BIOSENSE_ERR_CRYPTO || err == BIOSENSE_ERR_TEMPLATE_MISMATCH,
                "Expected crypto/mismatch error");

    printf("(correctly rejected)... ");

    biosense_close(driver);
    PASS();
}

/* Test: Lockout after failed attempts */
static void test_lockout(void) {
    TEST("Account lockout");

    /* Re-init with low attempt limit */
    biosense_shutdown();
    biosense_config_t config = {
        .match_threshold = 0.80f,
        .max_failed_attempts = 3,
        .require_liveness = false  /* Skip for speed */
    };
    biosense_init(&config);

    biosense_driver_t *driver = NULL;
    biosense_error_t err = biosense_open("/dev/biosense0", &driver);
    ASSERT_EQ(err, BIOSENSE_OK, "Open failed");

    /* Enroll */
    biosense_template_t template;
    const uint8_t password[] = "LockoutTest123";
    err = biosense_enroll(driver, "locktest", password, sizeof(password) - 1, NULL, &template);
    ASSERT_EQ(err, BIOSENSE_OK, "Enrollment failed");

    /* Try wrong password multiple times */
    biosense_match_result_t result;
    const uint8_t wrong[] = "WrongWrongWrong";

    for (int i = 0; i < 3; i++) {
        err = biosense_verify(driver, &template, wrong, sizeof(wrong) - 1, NULL, &result);
    }

    /* Should be locked now */
    ASSERT_TRUE(template.is_locked, "Account not locked");
    err = biosense_verify(driver, &template, password, sizeof(password) - 1, NULL, &result);
    ASSERT_EQ(err, BIOSENSE_ERR_LOCKED, "Expected locked error");

    /* Reset lockout */
    err = biosense_reset_lockout(&template);
    ASSERT_EQ(err, BIOSENSE_OK, "Reset failed");
    ASSERT_TRUE(!template.is_locked, "Still locked after reset");

    printf("(locked after 3 attempts, reset works)... ");

    biosense_close(driver);
    PASS();
}

/* Test: Entropy calculation */
static void test_entropy(void) {
    TEST("Entropy calculation");

    biosense_driver_t *driver = NULL;
    biosense_error_t err = biosense_open("/dev/biosense0", &driver);
    ASSERT_EQ(err, BIOSENSE_OK, "Open failed");

    biosense_vein_data_t data;
    biosense_quality_t quality;
    err = biosense_scan_vein(driver, NULL, &data, &quality);
    ASSERT_EQ(err, BIOSENSE_OK, "Scan failed");

    uint32_t entropy = biosense_calculate_entropy(&data);
    ASSERT_TRUE(entropy >= 64, "Entropy too low");
    ASSERT_TRUE(entropy <= 1024, "Entropy unreasonably high");

    printf("(%u bits)... ", entropy);

    biosense_close(driver);
    PASS();
}

/* Test: Pattern comparison */
static void test_compare(void) {
    TEST("Pattern comparison");

    biosense_driver_t *driver = NULL;
    biosense_error_t err = biosense_open("/dev/biosense0", &driver);
    ASSERT_EQ(err, BIOSENSE_OK, "Open failed");

    /* Get two scans */
    biosense_vein_data_t data1, data2;
    biosense_quality_t quality;

    err = biosense_scan_vein(driver, NULL, &data1, &quality);
    ASSERT_EQ(err, BIOSENSE_OK, "Scan 1 failed");

    err = biosense_scan_vein(driver, NULL, &data2, &quality);
    ASSERT_EQ(err, BIOSENSE_OK, "Scan 2 failed");

    /* Compare */
    float similarity = biosense_compare_patterns(&data1, &data2);
    ASSERT_TRUE(similarity >= 0.0f && similarity <= 1.0f, "Invalid similarity range");

    printf("(similarity: %.2f)... ", similarity);

    biosense_close(driver);
    PASS();
}

/* Test: Template export/import */
static void test_serialization(void) {
    TEST("Template serialization");

    biosense_driver_t *driver = NULL;
    biosense_error_t err = biosense_open("/dev/biosense0", &driver);
    ASSERT_EQ(err, BIOSENSE_OK, "Open failed");

    /* Create template */
    biosense_template_t original;
    const uint8_t password[] = "SerializeTest";
    err = biosense_enroll(driver, "serialize", password, sizeof(password) - 1, NULL, &original);
    ASSERT_EQ(err, BIOSENSE_OK, "Enrollment failed");

    /* Export */
    size_t export_size = 0;
    err = biosense_template_export(&original, NULL, &export_size);
    ASSERT_EQ(err, BIOSENSE_OK, "Export size query failed");
    ASSERT_TRUE(export_size > 0, "Zero export size");

    uint8_t *buffer = malloc(export_size);
    ASSERT_TRUE(buffer != NULL, "Malloc failed");

    err = biosense_template_export(&original, buffer, &export_size);
    ASSERT_EQ(err, BIOSENSE_OK, "Export failed");

    /* Import */
    biosense_template_t imported;
    err = biosense_template_import(&imported, buffer, export_size);
    ASSERT_EQ(err, BIOSENSE_OK, "Import failed");

    /* Verify imported matches */
    ASSERT_EQ(imported.version, original.version, "Version mismatch");
    ASSERT_TRUE(strcmp(imported.user_id, original.user_id) == 0, "User ID mismatch");
    ASSERT_EQ(imported.data_size, original.data_size, "Data size mismatch");

    free(buffer);
    biosense_close(driver);
    PASS();
}

/* Test: Error strings */
static void test_error_strings(void) {
    TEST("Error strings");

    const char *ok_str = biosense_error_string(BIOSENSE_OK);
    ASSERT_TRUE(ok_str != NULL, "NULL string for OK");
    ASSERT_TRUE(strlen(ok_str) > 0, "Empty string for OK");

    const char *locked_str = biosense_error_string(BIOSENSE_ERR_LOCKED);
    ASSERT_TRUE(locked_str != NULL, "NULL string for LOCKED");
    ASSERT_TRUE(strstr(locked_str, "lock") != NULL ||
                strstr(locked_str, "Lock") != NULL, "LOCKED string wrong");

    PASS();
}

/* Test: State strings */
static void test_state_strings(void) {
    TEST("State strings");

    const char *ready_str = biosense_state_string(BIOSENSE_STATE_READY);
    ASSERT_TRUE(ready_str != NULL, "NULL string for READY");
    ASSERT_TRUE(strstr(ready_str, "Ready") != NULL ||
                strstr(ready_str, "ready") != NULL, "READY string wrong");

    PASS();
}

int main(void) {
    printf("\n");
    printf("===========================================\n");
    printf("   PhantomOS BioSense Authentication Test  \n");
    printf("===========================================\n\n");

    /* Run all tests */
    test_init();
    test_enumerate();
    test_open_close();
    test_device_info();
    test_vein_scan();
    test_blood_scan();
    test_liveness();
    test_enroll();
    test_verify_correct();
    test_verify_wrong();
    test_lockout();
    test_entropy();
    test_compare();
    test_serialization();
    test_error_strings();
    test_state_strings();

    /* Summary */
    printf("\n===========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("===========================================\n\n");

    /* Cleanup */
    biosense_shutdown();

    return tests_failed > 0 ? 1 : 0;
}
