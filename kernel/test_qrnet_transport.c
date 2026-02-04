/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                    PHANTOM QRNET TRANSPORT TEST SUITE
 *                      "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

#include "phantom_qrnet_transport.h"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) do { \
    printf("\n[TEST] %s...\n", name); \
    tests_run++; \
} while(0)

#define TEST_PASS() do { \
    printf("[PASS]\n"); \
    tests_passed++; \
} while(0)

#define TEST_FAIL(msg) do { \
    printf("[FAIL] %s\n", msg); \
    tests_failed++; \
} while(0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        printf("  Assertion failed: %s (%d != %d)\n", msg, (int)(a), (int)(b)); \
        TEST_FAIL(msg); \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b, msg) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  Assertion failed: %s ('%s' != '%s')\n", msg, (a), (b)); \
        TEST_FAIL(msg); \
        return; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr, msg) do { \
    if ((ptr) == NULL) { \
        printf("  Assertion failed: %s (NULL)\n", msg); \
        TEST_FAIL(msg); \
        return; \
    } \
} while(0)

/* ==============================================================================
 * Test: Hash Computation and Verification
 * ============================================================================== */
static void test_hash_computation(void) {
    TEST_START("Hash computation");

    const char *test_data = "Hello, QRNet Transport!";
    uint8_t hash_bytes[32];
    char hash_hex[65];

    qrnet_hash_data(test_data, strlen(test_data), hash_bytes, hash_hex);

    /* Verify hash is 64 hex characters */
    ASSERT_EQ(strlen(hash_hex), 64, "Hash length should be 64");

    /* Verify hash is consistent */
    uint8_t hash_bytes2[32];
    char hash_hex2[65];
    qrnet_hash_data(test_data, strlen(test_data), hash_bytes2, hash_hex2);

    ASSERT_STR_EQ(hash_hex, hash_hex2, "Same data should produce same hash");

    /* Verify different data produces different hash */
    const char *different_data = "Different content";
    char different_hash[65];
    qrnet_hash_data(different_data, strlen(different_data), hash_bytes, different_hash);

    if (strcmp(hash_hex, different_hash) == 0) {
        TEST_FAIL("Different data should produce different hash");
        return;
    }

    TEST_PASS();
}

static void test_hash_verification(void) {
    TEST_START("Hash verification");

    const char *test_data = "Verify this content";
    uint8_t hash_bytes[32];
    char hash_hex[65];

    qrnet_hash_data(test_data, strlen(test_data), hash_bytes, hash_hex);

    /* Verify correct hash matches */
    int match = qrnet_verify_content(test_data, strlen(test_data), hash_hex);
    ASSERT_EQ(match, 1, "Correct hash should verify");

    /* Verify incorrect hash fails */
    char wrong_hash[65];
    memset(wrong_hash, '0', 64);
    wrong_hash[64] = '\0';

    int mismatch = qrnet_verify_content(test_data, strlen(test_data), wrong_hash);
    ASSERT_EQ(mismatch, 0, "Wrong hash should not verify");

    TEST_PASS();
}

/* ==============================================================================
 * Test: Content Store Operations
 * ============================================================================== */
static void test_store_init(void) {
    TEST_START("Content store initialization");

    qrnet_content_store_t *store = NULL;
    qrnet_transport_result_t result = qrnet_store_init(&store,
                                                         "/tmp/qrnet_test/content",
                                                         1024 * 1024 * 100);  /* 100MB */

    ASSERT_EQ(result, QRNET_TRANSPORT_OK, "Store init should succeed");
    ASSERT_NOT_NULL(store, "Store should not be NULL");

    /* Verify directory was created */
    struct stat st;
    ASSERT_EQ(stat("/tmp/qrnet_test/content", &st), 0, "Store directory should exist");

    qrnet_store_cleanup(store);
    TEST_PASS();
}

static void test_store_put_get(void) {
    TEST_START("Content store put/get");

    qrnet_content_store_t *store = NULL;
    qrnet_store_init(&store, "/tmp/qrnet_test/content2", 1024 * 1024 * 100);

    /* Store some content */
    const char *content = "This is test content for the QRNet store.";
    char hash_out[65];

    qrnet_transport_result_t result = qrnet_store_put(store, content, strlen(content),
                                                        "test.txt", "text/plain", hash_out);

    ASSERT_EQ(result, QRNET_TRANSPORT_OK, "Put should succeed");
    ASSERT_EQ(strlen(hash_out), 64, "Hash output should be 64 chars");

    printf("  Stored with hash: %s\n", hash_out);

    /* Verify content exists */
    int exists = qrnet_store_has(store, hash_out);
    ASSERT_EQ(exists, 1, "Content should exist after put");

    /* Retrieve content */
    void *data_out = NULL;
    size_t size_out = 0;

    result = qrnet_store_get(store, hash_out, &data_out, &size_out);
    ASSERT_EQ(result, QRNET_TRANSPORT_OK, "Get should succeed");
    ASSERT_EQ(size_out, strlen(content), "Size should match");
    ASSERT_NOT_NULL(data_out, "Data should not be NULL");

    /* Verify content matches */
    if (memcmp(data_out, content, size_out) != 0) {
        free(data_out);
        qrnet_store_cleanup(store);
        TEST_FAIL("Retrieved content should match stored content");
        return;
    }

    free(data_out);
    qrnet_store_cleanup(store);
    TEST_PASS();
}

static void test_store_duplicate(void) {
    TEST_START("Content store duplicate handling");

    qrnet_content_store_t *store = NULL;
    qrnet_store_init(&store, "/tmp/qrnet_test/content3", 1024 * 1024 * 100);

    const char *content = "Duplicate test content";
    char hash1[65], hash2[65];

    /* Store same content twice */
    qrnet_transport_result_t r1 = qrnet_store_put(store, content, strlen(content),
                                                    "file1.txt", "text/plain", hash1);
    qrnet_transport_result_t r2 = qrnet_store_put(store, content, strlen(content),
                                                    "file2.txt", "text/plain", hash2);

    ASSERT_EQ(r1, QRNET_TRANSPORT_OK, "First put should succeed");
    ASSERT_EQ(r2, QRNET_TRANSPORT_OK, "Second put should succeed (deduplicated)");
    ASSERT_STR_EQ(hash1, hash2, "Same content should have same hash");

    /* Store should only have one copy */
    ASSERT_EQ(store->entry_count, 1, "Should only have one entry (deduplicated)");

    qrnet_store_cleanup(store);
    TEST_PASS();
}

static void test_store_lookup(void) {
    TEST_START("Content store lookup metadata");

    qrnet_content_store_t *store = NULL;
    qrnet_store_init(&store, "/tmp/qrnet_test/content4", 1024 * 1024 * 100);

    const char *content = "Metadata test content";
    char hash[65];

    qrnet_store_put(store, content, strlen(content),
                    "metadata.txt", "text/plain", hash);

    /* Lookup entry */
    qrnet_content_entry_t *entry = qrnet_store_lookup(store, hash);
    ASSERT_NOT_NULL(entry, "Entry should be found");
    ASSERT_STR_EQ(entry->original_name, "metadata.txt", "Name should match");
    ASSERT_STR_EQ(entry->content_type, "text/plain", "Content type should match");
    ASSERT_EQ(entry->size, strlen(content), "Size should match");
    ASSERT_EQ(entry->status, QRNET_CONTENT_LOCAL, "Status should be LOCAL");

    qrnet_store_cleanup(store);
    TEST_PASS();
}

static void test_store_pin(void) {
    TEST_START("Content store pin/unpin");

    qrnet_content_store_t *store = NULL;
    qrnet_store_init(&store, "/tmp/qrnet_test/content5", 1024 * 1024 * 100);

    const char *content = "Pinned content";
    char hash[65];

    qrnet_store_put(store, content, strlen(content), "pinned.txt", "text/plain", hash);

    /* Pin content */
    qrnet_transport_result_t result = qrnet_store_pin(store, hash);
    ASSERT_EQ(result, QRNET_TRANSPORT_OK, "Pin should succeed");

    qrnet_content_entry_t *entry = qrnet_store_lookup(store, hash);
    ASSERT_EQ(entry->status, QRNET_CONTENT_PINNED, "Status should be PINNED");

    /* Try to pin non-existent content */
    result = qrnet_store_pin(store, "0000000000000000000000000000000000000000000000000000000000000000");
    ASSERT_EQ(result, QRNET_TRANSPORT_NOT_FOUND, "Pin of non-existent should fail");

    qrnet_store_cleanup(store);
    TEST_PASS();
}

static void test_store_not_found(void) {
    TEST_START("Content store not found handling");

    qrnet_content_store_t *store = NULL;
    qrnet_store_init(&store, "/tmp/qrnet_test/content6", 1024 * 1024 * 100);

    /* Try to get non-existent content */
    void *data = NULL;
    size_t size = 0;
    qrnet_transport_result_t result = qrnet_store_get(store,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        &data, &size);

    ASSERT_EQ(result, QRNET_TRANSPORT_NOT_FOUND, "Get of non-existent should return NOT_FOUND");
    if (data != NULL) {
        TEST_FAIL("Data should be NULL");
        return;
    }

    /* Check has returns false */
    int exists = qrnet_store_has(store,
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    ASSERT_EQ(exists, 0, "Has should return false for non-existent");

    qrnet_store_cleanup(store);
    TEST_PASS();
}

/* ==============================================================================
 * Test: File Storage
 * ============================================================================== */
static void test_store_put_file(void) {
    TEST_START("Content store put file");

    /* Create a test file */
    const char *test_file = "/tmp/qrnet_test_file.txt";
    const char *file_content = "This is file content for QRNet testing.\nLine 2.\n";

    FILE *f = fopen(test_file, "w");
    if (!f) {
        TEST_FAIL("Could not create test file");
        return;
    }
    fwrite(file_content, 1, strlen(file_content), f);
    fclose(f);

    qrnet_content_store_t *store = NULL;
    qrnet_store_init(&store, "/tmp/qrnet_test/content7", 1024 * 1024 * 100);

    char hash[65];
    qrnet_transport_result_t result = qrnet_store_put_file(store, test_file, hash);

    ASSERT_EQ(result, QRNET_TRANSPORT_OK, "Put file should succeed");

    /* Verify metadata */
    qrnet_content_entry_t *entry = qrnet_store_lookup(store, hash);
    ASSERT_NOT_NULL(entry, "Entry should exist");
    ASSERT_STR_EQ(entry->original_name, "qrnet_test_file.txt", "Filename should be extracted");
    ASSERT_STR_EQ(entry->content_type, "text/plain", "Content type should be detected");
    ASSERT_EQ(entry->size, strlen(file_content), "Size should match file size");

    /* Retrieve and verify content */
    void *data = NULL;
    size_t size = 0;
    result = qrnet_store_get(store, hash, &data, &size);
    ASSERT_EQ(result, QRNET_TRANSPORT_OK, "Get should succeed");

    if (memcmp(data, file_content, size) != 0) {
        free(data);
        qrnet_store_cleanup(store);
        unlink(test_file);
        TEST_FAIL("Retrieved content should match file content");
        return;
    }

    free(data);
    qrnet_store_cleanup(store);
    unlink(test_file);
    TEST_PASS();
}

/* ==============================================================================
 * Test: Transport System
 * ============================================================================== */
static void test_transport_init(void) {
    TEST_START("Transport system initialization");

    qrnet_transport_t *transport = NULL;
    qrnet_transport_result_t result = qrnet_transport_init(&transport, NULL, 0);

    ASSERT_EQ(result, QRNET_TRANSPORT_OK, "Transport init should succeed");
    ASSERT_NOT_NULL(transport, "Transport should not be NULL");
    ASSERT_NOT_NULL(transport->store, "Transport store should be initialized");
    ASSERT_EQ(transport->port, QRNET_DEFAULT_PORT, "Default port should be used");

    qrnet_transport_cleanup(transport);
    TEST_PASS();
}

static void test_transport_add_peer(void) {
    TEST_START("Transport add peer");

    qrnet_transport_t *transport = NULL;
    qrnet_transport_init(&transport, NULL, 8080);

    qrnet_transport_result_t result = qrnet_transport_add_peer(transport,
                                                                 "192.168.1.100",
                                                                 7847,
                                                                 "node-test-1");

    ASSERT_EQ(result, QRNET_TRANSPORT_OK, "Add peer should succeed");
    ASSERT_EQ(transport->peer_count, 1, "Peer count should be 1");

    /* Add another peer */
    result = qrnet_transport_add_peer(transport, "10.0.0.50", 7847, "node-test-2");
    ASSERT_EQ(result, QRNET_TRANSPORT_OK, "Add second peer should succeed");
    ASSERT_EQ(transport->peer_count, 2, "Peer count should be 2");

    /* Verify peer data */
    qrnet_peer_t *peer = transport->peers;
    ASSERT_NOT_NULL(peer, "Peer should exist");
    ASSERT_STR_EQ(peer->address, "10.0.0.50", "Latest peer address should match");
    ASSERT_EQ(peer->port, 7847, "Peer port should match");

    qrnet_transport_cleanup(transport);
    TEST_PASS();
}

static void test_transport_publish(void) {
    TEST_START("Transport publish content");

    qrnet_transport_t *transport = NULL;
    qrnet_transport_init(&transport, NULL, 0);

    const char *content = "Published content via transport layer";
    char hash[65];

    qrnet_transport_result_t result = qrnet_publish_content(transport,
                                                              content, strlen(content),
                                                              "published.txt", hash);

    ASSERT_EQ(result, QRNET_TRANSPORT_OK, "Publish should succeed");
    ASSERT_EQ(strlen(hash), 64, "Hash should be returned");

    /* Verify content is in store */
    int exists = qrnet_store_has(transport->store, hash);
    ASSERT_EQ(exists, 1, "Published content should be in store");

    qrnet_transport_cleanup(transport);
    TEST_PASS();
}

static void test_transport_fetch_local(void) {
    TEST_START("Transport fetch local content");

    qrnet_transport_t *transport = NULL;
    qrnet_transport_init(&transport, NULL, 0);

    /* Publish content first */
    const char *content = "Fetchable content for testing";
    char hash[65];
    qrnet_publish_content(transport, content, strlen(content), "fetch.txt", hash);

    /* Fetch it back */
    void *data = NULL;
    size_t size = 0;
    qrnet_transport_result_t result = qrnet_fetch_content(transport, hash, &data, &size);

    ASSERT_EQ(result, QRNET_TRANSPORT_OK, "Fetch should succeed");
    ASSERT_EQ(size, strlen(content), "Size should match");
    ASSERT_NOT_NULL(data, "Data should not be NULL");

    if (memcmp(data, content, size) != 0) {
        free(data);
        qrnet_transport_cleanup(transport);
        TEST_FAIL("Fetched content should match published content");
        return;
    }

    free(data);
    qrnet_transport_cleanup(transport);
    TEST_PASS();
}

static void test_transport_fetch_not_found(void) {
    TEST_START("Transport fetch non-existent content");

    qrnet_transport_t *transport = NULL;
    qrnet_transport_init(&transport, NULL, 0);

    void *data = NULL;
    size_t size = 0;
    qrnet_transport_result_t result = qrnet_fetch_content(transport,
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
        &data, &size);

    /* Should return NO_PEERS since there are no peers to query */
    ASSERT_EQ(result, QRNET_TRANSPORT_NO_PEERS, "Fetch should return NO_PEERS");

    qrnet_transport_cleanup(transport);
    TEST_PASS();
}

/* ==============================================================================
 * Test: Invalid Parameters
 * ============================================================================== */
static void test_invalid_params(void) {
    TEST_START("Invalid parameter handling");

    /* NULL store */
    qrnet_transport_result_t result = qrnet_store_init(NULL, "/tmp/test", 100);
    ASSERT_EQ(result, QRNET_TRANSPORT_INVALID_PARAM, "NULL store should fail");

    /* NULL path */
    qrnet_content_store_t *store = NULL;
    result = qrnet_store_init(&store, NULL, 100);
    ASSERT_EQ(result, QRNET_TRANSPORT_INVALID_PARAM, "NULL path should fail");

    /* NULL transport */
    result = qrnet_transport_init(NULL, NULL, 0);
    ASSERT_EQ(result, QRNET_TRANSPORT_INVALID_PARAM, "NULL transport should fail");

    /* NULL data to store */
    qrnet_store_init(&store, "/tmp/qrnet_test/content_inv", 100);
    result = qrnet_store_put(store, NULL, 100, "test", "text/plain", NULL);
    ASSERT_EQ(result, QRNET_TRANSPORT_INVALID_PARAM, "NULL data should fail");

    /* Zero size */
    char dummy[10] = "test";
    result = qrnet_store_put(store, dummy, 0, "test", "text/plain", NULL);
    ASSERT_EQ(result, QRNET_TRANSPORT_INVALID_PARAM, "Zero size should fail");

    qrnet_store_cleanup(store);
    TEST_PASS();
}

/* ==============================================================================
 * Test: Statistics Tracking
 * ============================================================================== */
static void test_statistics(void) {
    TEST_START("Statistics tracking");

    qrnet_content_store_t *store = NULL;
    qrnet_store_init(&store, "/tmp/qrnet_test/content_stats", 1024 * 1024 * 100);

    /* Initially zero */
    ASSERT_EQ(store->items_stored, 0, "Initial items_stored should be 0");
    ASSERT_EQ(store->bytes_stored, 0, "Initial bytes_stored should be 0");

    /* Store content */
    const char *content = "Statistics test content";
    char hash[65];
    qrnet_store_put(store, content, strlen(content), "stats.txt", "text/plain", hash);

    ASSERT_EQ(store->items_stored, 1, "items_stored should be 1");
    ASSERT_EQ(store->bytes_stored, strlen(content), "bytes_stored should match");
    ASSERT_EQ(store->entry_count, 1, "entry_count should be 1");

    /* Retrieve content */
    void *data = NULL;
    size_t size = 0;
    qrnet_store_get(store, hash, &data, &size);
    free(data);

    ASSERT_EQ(store->items_served, 1, "items_served should be 1");
    ASSERT_EQ(store->bytes_served, strlen(content), "bytes_served should match");

    /* Access count */
    qrnet_content_entry_t *entry = qrnet_store_lookup(store, hash);
    ASSERT_EQ(entry->access_count, 1, "access_count should be 1");

    /* Retrieve again */
    qrnet_store_get(store, hash, &data, &size);
    free(data);

    ASSERT_EQ(store->items_served, 2, "items_served should be 2");
    ASSERT_EQ(entry->access_count, 2, "access_count should be 2");

    qrnet_store_cleanup(store);
    TEST_PASS();
}

/* ==============================================================================
 * Test: Transfer Progress
 * ============================================================================== */
static void test_transfer_progress(void) {
    TEST_START("Transfer progress calculation");

    qrnet_transfer_t transfer;
    memset(&transfer, 0, sizeof(transfer));

    /* Zero size */
    int progress = qrnet_transfer_progress(&transfer);
    ASSERT_EQ(progress, 0, "Zero size should return 0% progress");

    /* Half complete */
    transfer.total_size = 1000;
    transfer.transferred = 500;
    progress = qrnet_transfer_progress(&transfer);
    ASSERT_EQ(progress, 50, "Half transferred should be 50%");

    /* Complete */
    transfer.transferred = 1000;
    progress = qrnet_transfer_progress(&transfer);
    ASSERT_EQ(progress, 100, "Fully transferred should be 100%");

    /* NULL */
    progress = qrnet_transfer_progress(NULL);
    ASSERT_EQ(progress, 0, "NULL transfer should return 0");

    TEST_PASS();
}

/* ==============================================================================
 * Main Test Runner
 * ============================================================================== */
int main(void) {
    printf("══════════════════════════════════════════════════════════════════════════════\n");
    printf("                    PHANTOM QRNET TRANSPORT TEST SUITE\n");
    printf("                      \"To Create, Not To Destroy\"\n");
    printf("══════════════════════════════════════════════════════════════════════════════\n");

    /* Create test directory */
    mkdir("/tmp/qrnet_test", 0755);

    /* Run tests */
    test_hash_computation();
    test_hash_verification();
    test_store_init();
    test_store_put_get();
    test_store_duplicate();
    test_store_lookup();
    test_store_pin();
    test_store_not_found();
    test_store_put_file();
    test_transport_init();
    test_transport_add_peer();
    test_transport_publish();
    test_transport_fetch_local();
    test_transport_fetch_not_found();
    test_invalid_params();
    test_statistics();
    test_transfer_progress();

    /* Summary */
    printf("\n══════════════════════════════════════════════════════════════════════════════\n");
    printf("                              TEST SUMMARY\n");
    printf("══════════════════════════════════════════════════════════════════════════════\n");
    printf("  Tests run:    %d\n", tests_run);
    printf("  Tests passed: %d\n", tests_passed);
    printf("  Tests failed: %d\n", tests_failed);
    printf("══════════════════════════════════════════════════════════════════════════════\n");

    if (tests_failed > 0) {
        printf("  STATUS: FAILED\n");
        return 1;
    } else {
        printf("  STATUS: ALL TESTS PASSED\n");
        return 0;
    }
}
