/*
 * Security Function Unit Tests for PhantomOS
 * Tests path canonicalization, safe parsing, shell escaping, etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST FRAMEWORK
 * ═══════════════════════════════════════════════════════════════════════════ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Testing %s... ", #name); \
    test_##name(); \
    printf("\033[32mPASSED\033[0m\n"); \
    tests_passed++; \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("\033[31mFAILED\033[0m\n    Expected %d, got %d at line %d\n", (int)(b), (int)(a), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("\033[31mFAILED\033[0m\n    Expected \"%s\", got \"%s\" at line %d\n", (b), (a), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("\033[31mFAILED\033[0m\n    Condition false at line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ═══════════════════════════════════════════════════════════════════════════
 * PATH CANONICALIZATION (copied from vfs.c for testing)
 * ═══════════════════════════════════════════════════════════════════════════ */

#define VFS_MAX_PATH 4096
#define VFS_MAX_NAME 255

static int vfs_canonicalize_path(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size < 2) return -1;

    char components[64][VFS_MAX_NAME + 1];
    int depth = 0;

    const char *p = input;
    int is_absolute = (*p == '/');

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        const char *start = p;
        while (*p && *p != '/') p++;
        size_t len = p - start;

        if (len == 0) continue;

        if (len == 1 && start[0] == '.') {
            continue;
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (depth > 0) {
                depth--;
            } else if (!is_absolute) {
                if (depth < 64) {
                    strncpy(components[depth], "..", VFS_MAX_NAME);
                    components[depth][VFS_MAX_NAME] = '\0';
                    depth++;
                }
            }
        } else {
            if (depth >= 64) return -1;
            if (len > VFS_MAX_NAME) len = VFS_MAX_NAME;
            memcpy(components[depth], start, len);
            components[depth][len] = '\0';
            depth++;
        }
    }

    char *out = output;
    char *end = output + output_size - 1;

    if (is_absolute) {
        *out++ = '/';
    }

    for (int i = 0; i < depth && out < end; i++) {
        if (i > 0 && out < end) *out++ = '/';
        size_t clen = strlen(components[i]);
        if (out + clen >= end) return -1;
        memcpy(out, components[i], clen);
        out += clen;
    }

    if (out == output) {
        if (is_absolute) {
            *out++ = '/';
        } else {
            *out++ = '.';
        }
    }

    *out = '\0';
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SAFE PORT PARSING (copied from phantom_net.c)
 * ═══════════════════════════════════════════════════════════════════════════ */

static int safe_parse_port(const char *str, uint16_t *out) {
    if (!str || !out) return -1;

    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);

    /* Strict: no trailing characters allowed */
    if (errno != 0 || endptr == str || *endptr != '\0') {
        return -1;
    }

    if (val < 0 || val > 65535) return -1;

    *out = (uint16_t)val;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SHELL ESCAPE (copied from phantom_pods.c)
 * ═══════════════════════════════════════════════════════════════════════════ */

static int shell_escape_arg(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size < 3) return -1;

    size_t in_len = strlen(input);
    size_t out_idx = 0;
    size_t i = 0;

    output[out_idx++] = '\'';

    for (; i < in_len && out_idx < output_size - 2; i++) {
        if (input[i] == '\'') {
            if (out_idx + 4 >= output_size - 1) return -1;
            output[out_idx++] = '\'';
            output[out_idx++] = '\\';
            output[out_idx++] = '\'';
            output[out_idx++] = '\'';
        } else {
            output[out_idx++] = input[i];
        }
    }

    /* Check if we processed all input - if not, buffer too small */
    if (i < in_len) return -1;

    if (out_idx >= output_size - 1) return -1;
    output[out_idx++] = '\'';
    output[out_idx] = '\0';

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PATH CANONICALIZATION TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(path_simple) {
    char out[256];
    ASSERT_EQ(vfs_canonicalize_path("/foo/bar", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "/foo/bar");
}

TEST(path_trailing_slash) {
    char out[256];
    ASSERT_EQ(vfs_canonicalize_path("/foo/bar/", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "/foo/bar");
}

TEST(path_double_slash) {
    char out[256];
    ASSERT_EQ(vfs_canonicalize_path("/foo//bar///baz", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "/foo/bar/baz");
}

TEST(path_dot) {
    char out[256];
    ASSERT_EQ(vfs_canonicalize_path("/foo/./bar/./baz", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "/foo/bar/baz");
}

TEST(path_dotdot) {
    char out[256];
    ASSERT_EQ(vfs_canonicalize_path("/foo/bar/../baz", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "/foo/baz");
}

TEST(path_dotdot_multiple) {
    char out[256];
    ASSERT_EQ(vfs_canonicalize_path("/foo/bar/baz/../../qux", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "/foo/qux");
}

TEST(path_traversal_attack) {
    char out[256];
    /* Attempt to traverse above root - should stay at root */
    ASSERT_EQ(vfs_canonicalize_path("/foo/../../../etc/passwd", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "/etc/passwd");
}

TEST(path_traversal_at_root) {
    char out[256];
    /* Many ..s at root should collapse to root */
    ASSERT_EQ(vfs_canonicalize_path("/../../../..", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "/");
}

TEST(path_root) {
    char out[256];
    ASSERT_EQ(vfs_canonicalize_path("/", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "/");
}

TEST(path_relative) {
    char out[256];
    ASSERT_EQ(vfs_canonicalize_path("foo/bar", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "foo/bar");
}

TEST(path_relative_dotdot) {
    char out[256];
    /* Relative path with .. that goes above start - keeps the .. */
    ASSERT_EQ(vfs_canonicalize_path("../foo", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "../foo");
}

TEST(path_empty_result) {
    char out[256];
    ASSERT_EQ(vfs_canonicalize_path("/foo/..", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "/");
}

TEST(path_complex) {
    char out[256];
    ASSERT_EQ(vfs_canonicalize_path("/a/b/c/../../d/e/../f", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "/a/d/f");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SAFE PORT PARSING TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(port_valid) {
    uint16_t port;
    ASSERT_EQ(safe_parse_port("8080", &port), 0);
    ASSERT_EQ(port, 8080);
}

TEST(port_zero) {
    uint16_t port;
    ASSERT_EQ(safe_parse_port("0", &port), 0);
    ASSERT_EQ(port, 0);
}

TEST(port_max) {
    uint16_t port;
    ASSERT_EQ(safe_parse_port("65535", &port), 0);
    ASSERT_EQ(port, 65535);
}

TEST(port_overflow) {
    uint16_t port;
    /* 65536 is out of range */
    ASSERT_EQ(safe_parse_port("65536", &port), -1);
}

TEST(port_negative) {
    uint16_t port;
    ASSERT_EQ(safe_parse_port("-1", &port), -1);
}

TEST(port_non_numeric) {
    uint16_t port;
    ASSERT_EQ(safe_parse_port("abc", &port), -1);
}

TEST(port_mixed) {
    uint16_t port;
    /* "80abc" should fail - trailing characters */
    ASSERT_EQ(safe_parse_port("80abc", &port), -1);
}

TEST(port_empty) {
    uint16_t port;
    ASSERT_EQ(safe_parse_port("", &port), -1);
}

TEST(port_null) {
    uint16_t port;
    ASSERT_EQ(safe_parse_port(NULL, &port), -1);
}

TEST(port_large_overflow) {
    uint16_t port;
    /* Very large number */
    ASSERT_EQ(safe_parse_port("999999999999", &port), -1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SHELL ESCAPE TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(shell_simple) {
    char out[256];
    ASSERT_EQ(shell_escape_arg("hello", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "'hello'");
}

TEST(shell_with_space) {
    char out[256];
    ASSERT_EQ(shell_escape_arg("hello world", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "'hello world'");
}

TEST(shell_with_quote) {
    char out[256];
    ASSERT_EQ(shell_escape_arg("it's", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "'it'\\''s'");
}

TEST(shell_with_semicolon) {
    char out[256];
    /* Injection attempt: ; rm -rf / */
    ASSERT_EQ(shell_escape_arg("; rm -rf /", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "'; rm -rf /'");
}

TEST(shell_with_backtick) {
    char out[256];
    /* Command substitution attempt */
    ASSERT_EQ(shell_escape_arg("`whoami`", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "'`whoami`'");
}

TEST(shell_with_dollar) {
    char out[256];
    /* Variable expansion attempt */
    ASSERT_EQ(shell_escape_arg("$HOME", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "'$HOME'");
}

TEST(shell_with_pipe) {
    char out[256];
    ASSERT_EQ(shell_escape_arg("foo | bar", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "'foo | bar'");
}

TEST(shell_multiple_quotes) {
    char out[256];
    ASSERT_EQ(shell_escape_arg("it's a 'test'", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "'it'\\''s a '\\''test'\\'''");
}

TEST(shell_empty) {
    char out[256];
    ASSERT_EQ(shell_escape_arg("", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "''");
}

TEST(shell_buffer_too_small) {
    char out[5];  /* Too small for 'hello' -> "'hello'" (8 chars) */
    ASSERT_EQ(shell_escape_arg("hello", out, sizeof(out)), -1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║         PHANTOMOS SECURITY FUNCTION UNIT TESTS                ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    printf("Path Canonicalization Tests:\n");
    RUN_TEST(path_simple);
    RUN_TEST(path_trailing_slash);
    RUN_TEST(path_double_slash);
    RUN_TEST(path_dot);
    RUN_TEST(path_dotdot);
    RUN_TEST(path_dotdot_multiple);
    RUN_TEST(path_traversal_attack);
    RUN_TEST(path_traversal_at_root);
    RUN_TEST(path_root);
    RUN_TEST(path_relative);
    RUN_TEST(path_relative_dotdot);
    RUN_TEST(path_empty_result);
    RUN_TEST(path_complex);

    printf("\nSafe Port Parsing Tests:\n");
    RUN_TEST(port_valid);
    RUN_TEST(port_zero);
    RUN_TEST(port_max);
    RUN_TEST(port_overflow);
    RUN_TEST(port_negative);
    RUN_TEST(port_non_numeric);
    RUN_TEST(port_mixed);
    RUN_TEST(port_empty);
    RUN_TEST(port_null);
    RUN_TEST(port_large_overflow);

    printf("\nShell Escape Tests:\n");
    RUN_TEST(shell_simple);
    RUN_TEST(shell_with_space);
    RUN_TEST(shell_with_quote);
    RUN_TEST(shell_with_semicolon);
    RUN_TEST(shell_with_backtick);
    RUN_TEST(shell_with_dollar);
    RUN_TEST(shell_with_pipe);
    RUN_TEST(shell_multiple_quotes);
    RUN_TEST(shell_empty);
    RUN_TEST(shell_buffer_too_small);

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════════════════\n\n");

    return tests_failed > 0 ? 1 : 0;
}
