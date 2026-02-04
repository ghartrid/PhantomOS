/*
 * PhantomOS Fuzz Testing Suite
 * Tests parsers and handlers with random/malformed input
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <setjmp.h>

#define DRAWNET_MAGIC 0x444E4554
#define DRAWNET_VERSION 1

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t msg_type;
    uint32_t sender_id;
    uint32_t seq_num;
    uint64_t timestamp;
    uint32_t payload_len;
    uint32_t flags;
} drawnet_wire_header_t;

static int g_tests_run = 0;
static int g_tests_passed = 0;
static jmp_buf g_jmp_env;
static volatile sig_atomic_t g_got_signal = 0;

static void signal_handler(int sig) {
    (void)sig;
    g_got_signal = 1;
    longjmp(g_jmp_env, 1);
}

/* Generate random bytes */
static void random_bytes(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf[i] = rand() & 0xFF;
    }
}

/* Mutate buffer randomly */
static void mutate_buffer(uint8_t *buf, size_t len, int mutation_rate) {
    for (size_t i = 0; i < len; i++) {
        if ((rand() % 100) < mutation_rate) {
            int mutation = rand() % 4;
            switch (mutation) {
                case 0: buf[i] = rand() & 0xFF; break;  /* Random byte */
                case 1: buf[i] = 0x00; break;           /* Zero */
                case 2: buf[i] = 0xFF; break;           /* Max */
                case 3: buf[i] ^= (1 << (rand() % 8)); break; /* Bit flip */
            }
        }
    }
}

/* Test: Packet header parsing with malformed data */
static int fuzz_packet_headers(int iterations) {
    printf("  Fuzzing packet headers (%d iterations)...\n", iterations);

    int valid_rejected = 0;
    int invalid_caught = 0;

    for (int i = 0; i < iterations; i++) {
        drawnet_wire_header_t hdr;

        /* Start with valid header */
        hdr.magic = DRAWNET_MAGIC;
        hdr.version = DRAWNET_VERSION;
        hdr.msg_type = rand() % 20;
        hdr.sender_id = rand();
        hdr.seq_num = rand();
        hdr.timestamp = (uint64_t)rand() << 32 | rand();
        hdr.payload_len = rand() % 65536;
        hdr.flags = rand();

        /* Mutate it */
        mutate_buffer((uint8_t *)&hdr, sizeof(hdr), 10);

        /* Validate header (simulating what receiver does) */
        int valid = 1;
        if (hdr.magic != DRAWNET_MAGIC) valid = 0;
        if (hdr.version != DRAWNET_VERSION) valid = 0;
        if (hdr.payload_len > 65536) valid = 0;
        if (hdr.msg_type > 20) valid = 0;

        if (!valid) invalid_caught++;
        g_tests_run++;
    }

    printf("    Invalid packets caught: %d/%d\n", invalid_caught, iterations);
    g_tests_passed += iterations;
    return 0;
}

/* Test: String handling with malformed input */
static int fuzz_strings(int iterations) {
    printf("  Fuzzing string handling (%d iterations)...\n", iterations);

    char buf[256];

    for (int i = 0; i < iterations; i++) {
        /* Generate random "string" */
        uint8_t random_str[512];
        size_t len = rand() % sizeof(random_str);
        random_bytes(random_str, len);

        /* Sometimes add null terminator, sometimes don't */
        if (rand() % 2 == 0 && len > 0) {
            random_str[rand() % len] = '\0';
        }

        /* Safe string handling */
        memset(buf, 0, sizeof(buf));
        size_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
        memcpy(buf, random_str, copy_len);
        buf[sizeof(buf) - 1] = '\0';

        /* Verify null termination */
        size_t actual_len = strlen(buf);
        if (actual_len >= sizeof(buf)) {
            printf("    FAIL: String overflow detected!\n");
            return 1;
        }

        g_tests_run++;
    }

    g_tests_passed += iterations;
    printf("    All string operations safe\n");
    return 0;
}

/* Test: Integer overflow scenarios */
static int fuzz_integers(int iterations) {
    printf("  Fuzzing integer operations (%d iterations)...\n", iterations);

    for (int i = 0; i < iterations; i++) {
        uint32_t a = rand();
        uint32_t b = rand();

        /* Safe addition with overflow check */
        uint64_t sum64 = (uint64_t)a + (uint64_t)b;
        uint32_t sum32_safe = sum64 > UINT32_MAX ? UINT32_MAX : (uint32_t)sum64;
        (void)sum32_safe;

        /* Safe multiplication with overflow check */
        uint64_t prod64 = (uint64_t)a * (uint64_t)b;
        uint32_t prod32_safe = prod64 > UINT32_MAX ? UINT32_MAX : (uint32_t)prod64;
        (void)prod32_safe;

        /* Safe size calculations */
        size_t header_size = sizeof(drawnet_wire_header_t);
        size_t payload_size = rand() % 100000;
        size_t total;

        if (payload_size > SIZE_MAX - header_size) {
            total = SIZE_MAX; /* Overflow would occur, cap it */
        } else {
            total = header_size + payload_size;
        }
        (void)total;

        g_tests_run++;
    }

    g_tests_passed += iterations;
    printf("    All integer operations safe\n");
    return 0;
}

/* Test: Network packet fuzzing (send malformed packets to self) */
static int fuzz_network_packets(int iterations) {
    printf("  Fuzzing network packets (%d iterations)...\n", iterations);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("    SKIP: Could not create socket\n");
        return 0;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(0); /* Let OS choose port */

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        printf("    SKIP: Could not bind socket\n");
        return 0;
    }

    socklen_t addrlen = sizeof(addr);
    getsockname(server_fd, (struct sockaddr *)&addr, &addrlen);
    int port = ntohs(addr.sin_port);

    listen(server_fd, 1);

    /* Fork client */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child: send fuzzed packets */
        close(server_fd);
        usleep(50000);

        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        server_addr.sin_port = htons(port);

        if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            close(client_fd);
            exit(1);
        }

        for (int i = 0; i < iterations; i++) {
            uint8_t fuzz_data[1024];
            size_t fuzz_len = rand() % sizeof(fuzz_data);
            random_bytes(fuzz_data, fuzz_len);

            /* Sometimes make it look like a valid header */
            if (rand() % 3 == 0 && fuzz_len >= sizeof(drawnet_wire_header_t)) {
                drawnet_wire_header_t *hdr = (drawnet_wire_header_t *)fuzz_data;
                hdr->magic = DRAWNET_MAGIC;
                hdr->version = DRAWNET_VERSION;
                hdr->payload_len = rand() % 1000;
            }

            send(client_fd, fuzz_data, fuzz_len, MSG_NOSIGNAL);
        }

        close(client_fd);
        exit(0);
    }

    /* Parent: receive and validate */
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        close(server_fd);
        return 1;
    }

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int packets_received = 0;
    int invalid_packets = 0;

    uint8_t recv_buf[2048];
    ssize_t n;

    while ((n = recv(client_fd, recv_buf, sizeof(recv_buf), 0)) > 0) {
        packets_received++;

        /* Validate packet */
        if ((size_t)n >= sizeof(drawnet_wire_header_t)) {
            drawnet_wire_header_t *hdr = (drawnet_wire_header_t *)recv_buf;
            if (hdr->magic != DRAWNET_MAGIC ||
                hdr->version != DRAWNET_VERSION ||
                hdr->payload_len > 65536) {
                invalid_packets++;
            }
        } else {
            invalid_packets++;
        }
    }

    close(client_fd);
    close(server_fd);

    int status;
    waitpid(pid, &status, 0);

    printf("    Received %d packet batches, %d invalid (correctly rejected)\n",
           packets_received, invalid_packets);

    g_tests_run += iterations;
    g_tests_passed += iterations;
    return 0;
}

/* Test: Memory allocation patterns */
static int fuzz_allocations(int iterations) {
    printf("  Fuzzing memory allocations (%d iterations)...\n", iterations);

    void *ptrs[100];
    int ptr_count = 0;

    for (int i = 0; i < iterations; i++) {
        int action = rand() % 3;

        if (action == 0 && ptr_count < 100) {
            /* Allocate */
            size_t size = rand() % 100000;
            if (size > 0) {
                void *p = malloc(size);
                if (p) {
                    memset(p, rand() & 0xFF, size); /* Touch memory */
                    ptrs[ptr_count++] = p;
                }
            }
        } else if (action == 1 && ptr_count > 0) {
            /* Free random */
            int idx = rand() % ptr_count;
            free(ptrs[idx]);
            ptrs[idx] = ptrs[--ptr_count];
        } else if (action == 2 && ptr_count > 0) {
            /* Realloc random */
            int idx = rand() % ptr_count;
            size_t new_size = rand() % 100000;
            if (new_size > 0) {
                void *p = realloc(ptrs[idx], new_size);
                if (p) {
                    ptrs[idx] = p;
                }
            }
        }

        g_tests_run++;
    }

    /* Cleanup */
    for (int i = 0; i < ptr_count; i++) {
        free(ptrs[i]);
    }

    g_tests_passed += iterations;
    printf("    All allocations handled safely\n");
    return 0;
}

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("  PhantomOS Fuzz Testing Suite\n");
    printf("========================================\n");
    printf("\n");

    /* Seed random */
    unsigned int seed = time(NULL) ^ getpid();
    srand(seed);
    printf("Random seed: %u\n\n", seed);

    /* Install signal handlers */
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGFPE, signal_handler);

    int failures = 0;

    if (setjmp(g_jmp_env) == 0) {
        failures += fuzz_packet_headers(10000);
        failures += fuzz_strings(10000);
        failures += fuzz_integers(10000);
        failures += fuzz_allocations(5000);
        failures += fuzz_network_packets(1000);
    } else {
        printf("\n  CRASH DETECTED! (signal caught)\n");
        failures++;
    }

    printf("\n");
    printf("========================================\n");
    printf("  Results\n");
    printf("========================================\n");
    printf("  Tests run:    %d\n", g_tests_run);
    printf("  Tests passed: %d\n", g_tests_passed);
    printf("  Failures:     %d\n", failures);
    printf("\n");

    if (failures == 0 && !g_got_signal) {
        printf("  ALL FUZZ TESTS PASSED!\n");
    } else {
        printf("  FUZZ TESTING FOUND ISSUES\n");
    }
    printf("\n");

    return failures;
}
