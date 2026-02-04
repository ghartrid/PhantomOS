/*
 * PhantomOS Stress Testing Suite
 * Tests system under heavy load conditions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/wait.h>

#define DRAWNET_MAGIC 0x444E4554
#define DRAWNET_VERSION 1
#define DRAWNET_MSG_CHAT 13

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

typedef struct __attribute__((packed)) {
    char message[512];
} drawnet_msg_chat_t;

static uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* ========== Stress Test: Rapid Network Connections ========== */

typedef struct {
    int port;
    int connections;
    int messages_per_conn;
    int success_count;
    int fail_count;
} conn_stress_args_t;

static void *connection_stress_client(void *arg) {
    conn_stress_args_t *args = (conn_stress_args_t *)arg;

    for (int c = 0; c < args->connections; c++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            args->fail_count++;
            continue;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(args->port);

        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(fd);
            args->fail_count++;
            continue;
        }

        /* Send messages rapidly */
        for (int m = 0; m < args->messages_per_conn; m++) {
            drawnet_wire_header_t hdr;
            drawnet_msg_chat_t chat;

            memset(&hdr, 0, sizeof(hdr));
            hdr.magic = DRAWNET_MAGIC;
            hdr.version = DRAWNET_VERSION;
            hdr.msg_type = DRAWNET_MSG_CHAT;
            hdr.sender_id = c;
            hdr.seq_num = m;
            hdr.timestamp = get_time_ms();
            hdr.payload_len = sizeof(chat);

            memset(&chat, 0, sizeof(chat));
            snprintf(chat.message, sizeof(chat.message), "Stress message %d-%d", c, m);

            if (send(fd, &hdr, sizeof(hdr), MSG_NOSIGNAL) != sizeof(hdr) ||
                send(fd, &chat, sizeof(chat), MSG_NOSIGNAL) != sizeof(chat)) {
                args->fail_count++;
                break;
            }
            args->success_count++;
        }

        close(fd);
    }

    return NULL;
}

static int stress_rapid_connections(void) {
    printf("  Stress testing rapid connections...\n");

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
    addr.sin_port = htons(0);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        printf("    SKIP: Could not bind\n");
        return 0;
    }

    socklen_t addrlen = sizeof(addr);
    getsockname(server_fd, (struct sockaddr *)&addr, &addrlen);
    int port = ntohs(addr.sin_port);

    listen(server_fd, 128);

    /* Fork for concurrent processing */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child: run multiple client threads */
        close(server_fd);
        usleep(50000);

        #define NUM_THREADS 4
        pthread_t threads[NUM_THREADS];
        conn_stress_args_t args[NUM_THREADS];

        for (int i = 0; i < NUM_THREADS; i++) {
            args[i].port = port;
            args[i].connections = 25;
            args[i].messages_per_conn = 10;
            args[i].success_count = 0;
            args[i].fail_count = 0;
            pthread_create(&threads[i], NULL, connection_stress_client, &args[i]);
        }

        int total_success = 0, total_fail = 0;
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_join(threads[i], NULL);
            total_success += args[i].success_count;
            total_fail += args[i].fail_count;
        }

        /* Return success rate encoded in exit code */
        int success_rate = (total_success * 100) / (total_success + total_fail + 1);
        exit(success_rate > 90 ? 0 : 1);
    }

    /* Parent: accept connections and receive messages */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int connections_accepted = 0;
    int messages_received = 0;
    uint64_t start = get_time_ms();

    while (get_time_ms() - start < 5000) { /* 5 second timeout */
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        connections_accepted++;

        /* Receive all messages from this client */
        uint8_t buf[1024];
        ssize_t n;
        while ((n = recv(client_fd, buf, sizeof(buf), MSG_DONTWAIT)) > 0) {
            messages_received += n / (sizeof(drawnet_wire_header_t) + sizeof(drawnet_msg_chat_t));
        }

        close(client_fd);
    }

    close(server_fd);

    int status;
    waitpid(pid, &status, 0);
    int client_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;

    printf("    Connections accepted: %d\n", connections_accepted);
    printf("    Messages received: ~%d\n", messages_received);
    printf("    Client threads: %s\n", client_ok ? "OK" : "HAD FAILURES");

    return client_ok ? 0 : 1;
}

/* ========== Stress Test: Memory Pressure ========== */

static int stress_memory_pressure(void) {
    printf("  Stress testing memory allocation patterns...\n");

    #define MAX_ALLOCS 1000
    void *allocs[MAX_ALLOCS];
    size_t sizes[MAX_ALLOCS];
    int count = 0;

    uint64_t start = get_time_ms();
    int operations = 0;
    int alloc_fails = 0;

    /* Run for 2 seconds */
    while (get_time_ms() - start < 2000) {
        int op = rand() % 100;

        if (op < 40 && count < MAX_ALLOCS) {
            /* Allocate - varying sizes */
            size_t size;
            int size_class = rand() % 4;
            switch (size_class) {
                case 0: size = rand() % 64 + 1; break;        /* Small */
                case 1: size = rand() % 1024 + 64; break;     /* Medium */
                case 2: size = rand() % 65536 + 1024; break;  /* Large */
                case 3: size = rand() % 1048576 + 65536; break; /* Very large */
                default: size = 128;
            }

            void *p = malloc(size);
            if (p) {
                memset(p, rand() & 0xFF, size);
                allocs[count] = p;
                sizes[count] = size;
                count++;
            } else {
                alloc_fails++;
            }
        } else if (op < 70 && count > 0) {
            /* Free random */
            int idx = rand() % count;
            free(allocs[idx]);
            allocs[idx] = allocs[count - 1];
            sizes[idx] = sizes[count - 1];
            count--;
        } else if (op < 90 && count > 0) {
            /* Realloc random */
            int idx = rand() % count;
            size_t new_size = rand() % 100000 + 1;
            void *p = realloc(allocs[idx], new_size);
            if (p) {
                allocs[idx] = p;
                sizes[idx] = new_size;
            }
        } else if (count > 0) {
            /* Access random (read/write) */
            int idx = rand() % count;
            if (sizes[idx] > 0) {
                ((char *)allocs[idx])[rand() % sizes[idx]] = rand() & 0xFF;
            }
        }

        operations++;
    }

    /* Cleanup */
    for (int i = 0; i < count; i++) {
        free(allocs[i]);
    }

    printf("    Operations performed: %d\n", operations);
    printf("    Allocation failures: %d\n", alloc_fails);
    printf("    Peak allocations: %d\n", MAX_ALLOCS);

    return 0;
}

/* ========== Stress Test: Concurrent Network Messages ========== */

static int g_server_port = 0;
static volatile int g_running = 1;

static void *message_sender(void *arg) {
    int thread_id = *(int *)arg;
    int messages_sent = 0;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(g_server_port);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }

    while (g_running) {
        drawnet_wire_header_t hdr;
        drawnet_msg_chat_t chat;

        memset(&hdr, 0, sizeof(hdr));
        hdr.magic = DRAWNET_MAGIC;
        hdr.version = DRAWNET_VERSION;
        hdr.msg_type = DRAWNET_MSG_CHAT;
        hdr.sender_id = thread_id;
        hdr.seq_num = messages_sent;
        hdr.timestamp = get_time_ms();
        hdr.payload_len = sizeof(chat);

        memset(&chat, 0, sizeof(chat));
        snprintf(chat.message, sizeof(chat.message), "Thread %d msg %d", thread_id, messages_sent);

        if (send(fd, &hdr, sizeof(hdr), MSG_NOSIGNAL) != sizeof(hdr)) break;
        if (send(fd, &chat, sizeof(chat), MSG_NOSIGNAL) != sizeof(chat)) break;

        messages_sent++;
        usleep(1000); /* 1ms between messages = 1000 msg/sec per thread */
    }

    close(fd);

    int *result = malloc(sizeof(int));
    *result = messages_sent;
    return result;
}

static int stress_concurrent_messages(void) {
    printf("  Stress testing concurrent message streams...\n");

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
    addr.sin_port = htons(0);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        return 0;
    }

    socklen_t addrlen = sizeof(addr);
    getsockname(server_fd, (struct sockaddr *)&addr, &addrlen);
    g_server_port = ntohs(addr.sin_port);

    listen(server_fd, 16);

    /* Start sender threads */
    #define SENDER_THREADS 4
    pthread_t senders[SENDER_THREADS];
    int thread_ids[SENDER_THREADS];

    g_running = 1;

    for (int i = 0; i < SENDER_THREADS; i++) {
        thread_ids[i] = i;
    }

    /* Fork receiver */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child: start senders after small delay */
        close(server_fd);
        usleep(100000);

        for (int i = 0; i < SENDER_THREADS; i++) {
            pthread_create(&senders[i], NULL, message_sender, &thread_ids[i]);
        }

        sleep(2); /* Run for 2 seconds */
        g_running = 0;

        int total_sent = 0;
        for (int i = 0; i < SENDER_THREADS; i++) {
            int *result;
            pthread_join(senders[i], (void **)&result);
            if (result) {
                total_sent += *result;
                free(result);
            }
        }

        exit(total_sent > 1000 ? 0 : 1); /* Expect at least 1000 messages */
    }

    /* Parent: receive messages */
    fd_set readfds;
    int max_fd = server_fd;
    int client_fds[16];
    int client_count = 0;
    int total_received = 0;

    uint64_t start = get_time_ms();

    while (get_time_ms() - start < 3000) { /* 3 second timeout */
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        for (int i = 0; i < client_count; i++) {
            FD_SET(client_fds[i], &readfds);
            if (client_fds[i] > max_fd) max_fd = client_fds[i];
        }

        struct timeval tv = {0, 100000};
        int ready = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (ready <= 0) continue;

        /* Accept new connections */
        if (FD_ISSET(server_fd, &readfds) && client_count < 16) {
            int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd >= 0) {
                client_fds[client_count++] = client_fd;
            }
        }

        /* Receive from clients */
        for (int i = 0; i < client_count; i++) {
            if (FD_ISSET(client_fds[i], &readfds)) {
                uint8_t buf[4096];
                ssize_t n = recv(client_fds[i], buf, sizeof(buf), MSG_DONTWAIT);
                if (n > 0) {
                    total_received += n / (sizeof(drawnet_wire_header_t) + sizeof(drawnet_msg_chat_t));
                }
            }
        }
    }

    /* Cleanup */
    for (int i = 0; i < client_count; i++) {
        close(client_fds[i]);
    }
    close(server_fd);

    int status;
    waitpid(pid, &status, 0);

    printf("    Concurrent senders: %d\n", SENDER_THREADS);
    printf("    Messages received: ~%d\n", total_received);
    printf("    Throughput: ~%d msg/sec\n", total_received / 2);

    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : 1;
}

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("  PhantomOS Stress Testing Suite\n");
    printf("========================================\n");
    printf("\n");

    srand(time(NULL) ^ getpid());

    int failures = 0;

    failures += stress_memory_pressure();
    failures += stress_rapid_connections();
    failures += stress_concurrent_messages();

    printf("\n");
    printf("========================================\n");
    printf("  Results\n");
    printf("========================================\n");

    if (failures == 0) {
        printf("  ALL STRESS TESTS PASSED!\n");
    } else {
        printf("  %d STRESS TEST(S) HAD ISSUES\n", failures);
    }
    printf("\n");

    return failures;
}
