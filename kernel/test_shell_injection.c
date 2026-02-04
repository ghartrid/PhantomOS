/*
 * Practical Shell Injection Test
 * Verifies that shell_escape_arg actually prevents command injection
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Copy of shell_escape_arg from phantom_pods.c */
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

    if (i < in_len) return -1;
    if (out_idx >= output_size - 1) return -1;
    output[out_idx++] = '\'';
    output[out_idx] = '\0';

    return 0;
}

static int tests_passed = 0;
static int tests_failed = 0;

/* Test that malicious input doesn't execute commands */
static void test_injection(const char *name, const char *malicious_input, const char *expected_literal) {
    char escaped[512];
    char cmd[1024];
    char result[256];
    FILE *fp;

    printf("  Testing %s... ", name);

    if (shell_escape_arg(malicious_input, escaped, sizeof(escaped)) != 0) {
        printf("\033[31mFAILED\033[0m (escape failed)\n");
        tests_failed++;
        return;
    }

    /* Run: echo <escaped> and capture output */
    snprintf(cmd, sizeof(cmd), "echo %s", escaped);
    fp = popen(cmd, "r");
    if (!fp) {
        printf("\033[31mFAILED\033[0m (popen failed)\n");
        tests_failed++;
        return;
    }

    if (fgets(result, sizeof(result), fp) == NULL) {
        result[0] = '\0';
    }
    pclose(fp);

    /* Remove trailing newline */
    size_t len = strlen(result);
    if (len > 0 && result[len-1] == '\n') {
        result[len-1] = '\0';
    }

    /* Verify the output is the literal string, not executed command result */
    if (strcmp(result, expected_literal) == 0) {
        printf("\033[32mPASSED\033[0m\n");
        tests_passed++;
    } else {
        printf("\033[31mFAILED\033[0m\n");
        printf("    Expected: \"%s\"\n", expected_literal);
        printf("    Got:      \"%s\"\n", result);
        tests_failed++;
    }
}

/* Test that command substitution doesn't work */
static void test_no_command_execution(const char *name, const char *injection) {
    char escaped[512];
    char cmd[1024];
    FILE *fp;

    printf("  Testing %s... ", name);

    if (shell_escape_arg(injection, escaped, sizeof(escaped)) != 0) {
        printf("\033[31mFAILED\033[0m (escape failed)\n");
        tests_failed++;
        return;
    }

    /* Create a marker file that would be created if injection works */
    unlink("/tmp/phantom_injection_test");

    /* Try to execute: if injection works, it would create the marker file */
    snprintf(cmd, sizeof(cmd), "echo %s > /dev/null", escaped);
    system(cmd);

    /* Check if marker file exists (it shouldn't) */
    fp = fopen("/tmp/phantom_injection_test", "r");
    if (fp) {
        fclose(fp);
        unlink("/tmp/phantom_injection_test");
        printf("\033[31mFAILED\033[0m (injection executed!)\n");
        tests_failed++;
    } else {
        printf("\033[32mPASSED\033[0m\n");
        tests_passed++;
    }
}

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║      PRACTICAL SHELL INJECTION PREVENTION TESTS               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    printf("Echo Output Tests (should output literal strings, not execute):\n");
    test_injection("basic_string", "hello world", "hello world");
    test_injection("semicolon_injection", "; echo HACKED", "; echo HACKED");
    test_injection("pipe_injection", "| cat /etc/passwd", "| cat /etc/passwd");
    test_injection("backtick_injection", "`whoami`", "`whoami`");
    test_injection("dollar_paren_injection", "$(whoami)", "$(whoami)");
    test_injection("dollar_var", "$HOME", "$HOME");
    /* Note: newline test expects only first line since fgets stops at newline.
     * The important thing is "echo HACKED" doesn't execute - it's just literal output */
    test_injection("newline_injection", "foo\nbar", "foo");
    test_injection("quote_escape", "'; echo HACKED #", "'; echo HACKED #");
    test_injection("double_quote", "\"$(whoami)\"", "\"$(whoami)\"");

    printf("\nCommand Execution Tests (should NOT create marker file):\n");
    test_no_command_execution("touch_via_semicolon",
        "; touch /tmp/phantom_injection_test");
    test_no_command_execution("touch_via_backtick",
        "`touch /tmp/phantom_injection_test`");
    test_no_command_execution("touch_via_dollar_paren",
        "$(touch /tmp/phantom_injection_test)");
    test_no_command_execution("touch_via_quote_break",
        "'; touch /tmp/phantom_injection_test #");

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════════════════\n\n");

    return tests_failed > 0 ? 1 : 0;
}
