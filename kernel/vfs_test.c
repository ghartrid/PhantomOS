/* VFS Functionality Test for PhantomOS */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vfs.h"
#include "../geofs.h"

/* External functions and types from geofs_vfs.c */
extern vfs_error_t geofs_vfs_mount_volume(struct vfs_context *ctx,
                                           geofs_volume_t *volume,
                                           const char *mount_path);
extern struct vfs_fs_type geofs_vfs_type;

#define TEST_PASS "\033[32mPASS\033[0m"
#define TEST_FAIL "\033[31mFAIL\033[0m"

static int tests_passed = 0;
static int tests_failed = 0;
int g_search_count = 0;

/* Search callback function */
static void search_callback_fn(const char *path, struct vfs_stat *stat, void *ctx) {
    (void)stat; (void)ctx;
    printf("    Found: %s\n", path);
    g_search_count++;
}

static void test_result(const char *name, int passed) {
    if (passed) {
        printf("  [%s] %s\n", TEST_PASS, name);
        tests_passed++;
    } else {
        printf("  [%s] %s\n", TEST_FAIL, name);
        tests_failed++;
    }
}

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║       PhantomOS VFS Functionality Test Suite                 ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    /* Initialize GeoFS */
    printf("▶ Initializing GeoFS volume...\n");
    geofs_volume_t *vol = NULL;
    geofs_error_t gerr = geofs_volume_create("test_geology.db", 50, &vol);
    if (gerr != GEOFS_OK) {
        printf("  Failed to create GeoFS volume: %d\n", gerr);
        return 1;
    }
    printf("  GeoFS volume created successfully\n\n");

    /* Initialize VFS */
    printf("▶ Initializing VFS context...\n");
    struct vfs_context vfs_ctx;
    struct vfs_context *vfs = &vfs_ctx;
    vfs_error_t err = vfs_init(vfs);
    test_result("VFS initialization", err == VFS_OK);

    /* Register GeoFS filesystem type */
    printf("\n▶ Registering GeoFS filesystem...\n");
    err = vfs_register_fs(vfs, &geofs_vfs_type);
    test_result("GeoFS registration", err == VFS_OK);

    /* Mount GeoFS */
    printf("\n▶ Mounting GeoFS at /home...\n");
    err = geofs_vfs_mount_volume(vfs, vol, "/home");
    test_result("GeoFS mount at /home", err == VFS_OK);

    /* ══════════════════════════════════════════════════════════════════
     * TEST 1: Directory Creation
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n▶ TEST 1: Directory Creation\n");

    err = vfs_mkdir(vfs, 1, "/home/testdir", 0755);
    test_result("Create /home/testdir", err == VFS_OK);

    err = vfs_mkdir(vfs, 1, "/home/testdir/subdir", 0755);
    test_result("Create /home/testdir/subdir", err == VFS_OK);

    /* Try to create existing directory */
    err = vfs_mkdir(vfs, 1, "/home/testdir", 0755);
    test_result("Reject duplicate directory", err == VFS_ERR_EXIST);

    /* ══════════════════════════════════════════════════════════════════
     * TEST 2: File Creation
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n▶ TEST 2: File Creation\n");

    vfs_fd_t fd = vfs_open(vfs, 1, "/home/testdir/test.txt", VFS_O_CREATE | VFS_O_RDWR, 0644);
    test_result("Create test.txt", fd >= 0);

    if (fd >= 0) {
        vfs_close(vfs, fd);
    }

    fd = vfs_open(vfs, 1, "/home/testdir/code.c", VFS_O_CREATE | VFS_O_RDWR, 0644);
    test_result("Create code.c", fd >= 0);
    if (fd >= 0) vfs_close(vfs, fd);

    fd = vfs_open(vfs, 1, "/home/testdir/data.json", VFS_O_CREATE | VFS_O_RDWR, 0644);
    test_result("Create data.json", fd >= 0);
    if (fd >= 0) vfs_close(vfs, fd);

    /* ══════════════════════════════════════════════════════════════════
     * TEST 3: File Writing
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n▶ TEST 3: File Writing\n");

    fd = vfs_open(vfs, 1, "/home/testdir/test.txt", VFS_O_RDWR, 0);
    test_result("Open test.txt for writing", fd >= 0);

    if (fd >= 0) {
        const char *test_content = "Hello from PhantomOS!\nThis is a test file.\nLine 3 of content.";
        ssize_t written = vfs_write(vfs, fd, test_content, strlen(test_content));
        test_result("Write content to test.txt", written == (ssize_t)strlen(test_content));

        err = vfs_sync(vfs, fd);
        test_result("Sync test.txt", err == VFS_OK);

        vfs_close(vfs, fd);
    }

    /* Write to code.c */
    fd = vfs_open(vfs, 1, "/home/testdir/code.c", VFS_O_RDWR, 0);
    if (fd >= 0) {
        const char *code = "#include <stdio.h>\n\nint main() {\n    printf(\"PhantomOS!\\n\");\n    return 0;\n}\n";
        ssize_t written = vfs_write(vfs, fd, code, strlen(code));
        test_result("Write C code to code.c", written == (ssize_t)strlen(code));
        vfs_sync(vfs, fd);
        vfs_close(vfs, fd);
    }

    /* ══════════════════════════════════════════════════════════════════
     * TEST 4: File Reading
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n▶ TEST 4: File Reading\n");

    fd = vfs_open(vfs, 1, "/home/testdir/test.txt", VFS_O_RDONLY, 0);
    test_result("Open test.txt for reading", fd >= 0);

    if (fd >= 0) {
        char buffer[256] = {0};
        ssize_t bytes_read = vfs_read(vfs, fd, buffer, sizeof(buffer) - 1);
        test_result("Read content from test.txt", bytes_read > 0);

        int content_correct = (strstr(buffer, "Hello from PhantomOS") != NULL);
        test_result("Content verification", content_correct);

        if (bytes_read > 0) {
            printf("    Read %zd bytes: \"%.*s...\"\n", bytes_read, 30, buffer);
        }

        vfs_close(vfs, fd);
    }

    /* ══════════════════════════════════════════════════════════════════
     * TEST 5: Directory Listing (readdir)
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n▶ TEST 5: Directory Listing\n");

    fd = vfs_open(vfs, 1, "/home/testdir", VFS_O_RDONLY | VFS_O_DIRECTORY, 0);
    test_result("Open /home/testdir as directory", fd >= 0);

    if (fd >= 0) {
        struct vfs_dirent entries[20];
        size_t count = 0;

        err = vfs_readdir(vfs, fd, entries, 20, &count);
        test_result("Read directory entries", err == VFS_OK);
        test_result("Found entries in directory", count > 0);

        printf("    Found %zu entries:\n", count);
        int found_txt = 0, found_c = 0, found_json = 0, found_subdir = 0;
        for (size_t i = 0; i < count; i++) {
            printf("      - %s (%s)\n", entries[i].name,
                   entries[i].type == VFS_TYPE_DIRECTORY ? "dir" : "file");
            if (strcmp(entries[i].name, "test.txt") == 0) found_txt = 1;
            if (strcmp(entries[i].name, "code.c") == 0) found_c = 1;
            if (strcmp(entries[i].name, "data.json") == 0) found_json = 1;
            if (strcmp(entries[i].name, "subdir") == 0) found_subdir = 1;
        }

        test_result("Found test.txt in listing", found_txt);
        test_result("Found code.c in listing", found_c);
        test_result("Found subdir in listing", found_subdir);

        vfs_close(vfs, fd);
    }

    /* ══════════════════════════════════════════════════════════════════
     * TEST 6: File Stats
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n▶ TEST 6: File Statistics\n");

    struct vfs_stat st;
    err = vfs_stat(vfs, "/home/testdir/test.txt", &st);
    test_result("Stat test.txt", err == VFS_OK);

    if (err == VFS_OK) {
        test_result("File type is regular", st.type == VFS_TYPE_REGULAR);
        test_result("File has size > 0", st.size > 0);
        printf("    Size: %lu bytes\n", st.size);
    }

    err = vfs_stat(vfs, "/home/testdir", &st);
    test_result("Stat directory", err == VFS_OK);
    if (err == VFS_OK) {
        test_result("Directory type correct", st.type == VFS_TYPE_DIRECTORY);
    }

    /* ══════════════════════════════════════════════════════════════════
     * TEST 7: File Copy
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n▶ TEST 7: File Copy\n");

    err = vfs_copy(vfs, 1, "/home/testdir/test.txt", "/home/testdir/test_copy.txt");
    test_result("Copy test.txt to test_copy.txt", err == VFS_OK);

    /* Verify copy */
    err = vfs_stat(vfs, "/home/testdir/test_copy.txt", &st);
    test_result("Copy exists", err == VFS_OK);

    /* Read and verify content */
    fd = vfs_open(vfs, 1, "/home/testdir/test_copy.txt", VFS_O_RDONLY, 0);
    if (fd >= 0) {
        char buffer[256] = {0};
        ssize_t bytes_read = vfs_read(vfs, fd, buffer, sizeof(buffer) - 1);
        int copy_content_correct = (bytes_read > 0 && strstr(buffer, "Hello from PhantomOS") != NULL);
        test_result("Copy content matches original", copy_content_correct);
        vfs_close(vfs, fd);
    }

    /* ══════════════════════════════════════════════════════════════════
     * TEST 8: File Rename
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n▶ TEST 8: File Rename\n");

    err = vfs_rename(vfs, 1, "/home/testdir/data.json", "/home/testdir/config.json");
    test_result("Rename data.json to config.json", err == VFS_OK);

    /* Old name should still exist (preserved in geology) or new name should exist */
    err = vfs_stat(vfs, "/home/testdir/config.json", &st);
    test_result("New name exists", err == VFS_OK);

    /* ══════════════════════════════════════════════════════════════════
     * TEST 9: File Hide (PhantomOS delete)
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n▶ TEST 9: File Hide (Phantom Delete)\n");

    /* Create a file to hide */
    fd = vfs_open(vfs, 1, "/home/testdir/to_hide.txt", VFS_O_CREATE | VFS_O_RDWR, 0644);
    if (fd >= 0) {
        vfs_write(vfs, fd, "This will be hidden", 19);
        vfs_close(vfs, fd);
    }

    err = vfs_hide(vfs, 1, "/home/testdir/to_hide.txt");
    test_result("Hide file (phantom delete)", err == VFS_OK);

    /* File should not appear in normal listing now */
    fd = vfs_open(vfs, 1, "/home/testdir", VFS_O_RDONLY | VFS_O_DIRECTORY, 0);
    if (fd >= 0) {
        struct vfs_dirent entries[20];
        size_t count = 0;
        vfs_readdir(vfs, fd, entries, 20, &count);

        int found_hidden = 0;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(entries[i].name, "to_hide.txt") == 0) {
                found_hidden = 1;
                break;
            }
        }
        test_result("Hidden file not in listing", !found_hidden);
        vfs_close(vfs, fd);
    }

    /* ══════════════════════════════════════════════════════════════════
     * TEST 10: Search Functionality
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n▶ TEST 10: File Search\n");

    g_search_count = 0;
    err = vfs_search(vfs, "/home/testdir", "*.txt", search_callback_fn, NULL);
    test_result("Search for *.txt", err == VFS_OK);
    test_result("Found .txt files", g_search_count > 0);
    printf("    Total .txt files found: %d\n", g_search_count);

    g_search_count = 0;
    err = vfs_search(vfs, "/home/testdir", "*.c", search_callback_fn, NULL);
    test_result("Search for *.c", err == VFS_OK);
    printf("    Total .c files found: %d\n", g_search_count);

    /* ══════════════════════════════════════════════════════════════════
     * TEST 11: Nested Directory Operations
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n▶ TEST 11: Nested Directory Operations\n");

    fd = vfs_open(vfs, 1, "/home/testdir/subdir/nested.txt", VFS_O_CREATE | VFS_O_RDWR, 0644);
    test_result("Create file in nested directory", fd >= 0);
    if (fd >= 0) {
        const char *nested_content = "Nested file content";
        vfs_write(vfs, fd, nested_content, strlen(nested_content));
        vfs_close(vfs, fd);
    }

    fd = vfs_open(vfs, 1, "/home/testdir/subdir/nested.txt", VFS_O_RDONLY, 0);
    if (fd >= 0) {
        char buffer[64] = {0};
        ssize_t bytes = vfs_read(vfs, fd, buffer, sizeof(buffer) - 1);
        test_result("Read nested file", bytes > 0 && strstr(buffer, "Nested") != NULL);
        vfs_close(vfs, fd);
    }

    /* ══════════════════════════════════════════════════════════════════
     * TEST 12: Path Resolution
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n▶ TEST 12: Path Resolution\n");

    err = vfs_stat(vfs, "/home/testdir/../testdir/test.txt", &st);
    test_result("Resolve path with ..", err == VFS_OK);

    err = vfs_stat(vfs, "/home/testdir/./test.txt", &st);
    test_result("Resolve path with .", err == VFS_OK);

    /* ══════════════════════════════════════════════════════════════════
     * SUMMARY
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                    TEST SUMMARY                              ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Passed: %-3d                                                 ║\n", tests_passed);
    printf("║  Failed: %-3d                                                 ║\n", tests_failed);
    printf("║  Total:  %-3d                                                 ║\n", tests_passed + tests_failed);
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    if (tests_failed == 0) {
        printf("\n✓ All tests passed! PhantomOS VFS is fully functional.\n\n");
    } else {
        printf("\n✗ Some tests failed. Review output above.\n\n");
    }

    /* Cleanup */
    geofs_volume_close(vol);
    remove("test_geology.db");

    return tests_failed > 0 ? 1 : 0;
}
