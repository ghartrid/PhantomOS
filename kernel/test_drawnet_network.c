/*
 * DrawNet Network Self-Test
 * Tests the low-level networking functions without requiring GTK GUI
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <time.h>
#include <stdint.h>

/* Wire format from phantom_artos.h */
#define DRAWNET_MAGIC           0x444E4554  /* "DNET" */
#define DRAWNET_VERSION         1
#define DRAWNET_DEFAULT_PORT    34567

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

/* Message types */
#define DRAWNET_MSG_HELLO       1
#define DRAWNET_MSG_ACK         2
#define DRAWNET_MSG_PING        3
#define DRAWNET_MSG_PONG        4
#define DRAWNET_MSG_CHAT        13

typedef struct __attribute__((packed)) {
    char session_id[32];
    char name[64];
    uint32_t color_rgba;
    uint32_t capabilities;
} drawnet_msg_hello_t;

typedef struct __attribute__((packed)) {
    uint32_t result;
    uint32_t assigned_id;
    uint32_t assigned_perm;
    char session_name[128];
    uint32_t peer_count;
} drawnet_msg_ack_t;

typedef struct __attribute__((packed)) {
    char message[512];
} drawnet_msg_chat_t;

static uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int send_packet(int fd, uint16_t msg_type, uint32_t sender_id,
                       uint32_t seq, const void *payload, size_t payload_len) {
    drawnet_wire_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = DRAWNET_MAGIC;
    hdr.version = DRAWNET_VERSION;
    hdr.msg_type = msg_type;
    hdr.sender_id = sender_id;
    hdr.seq_num = seq;
    hdr.timestamp = get_timestamp_ms();
    hdr.payload_len = payload_len;

    if (send(fd, &hdr, sizeof(hdr), 0) != sizeof(hdr)) {
        perror("send header");
        return -1;
    }

    if (payload_len > 0 && payload) {
        if (send(fd, payload, payload_len, 0) != (ssize_t)payload_len) {
            perror("send payload");
            return -1;
        }
    }

    return 0;
}

static int recv_packet(int fd, drawnet_wire_header_t *hdr, void *payload, size_t max_payload) {
    ssize_t n = recv(fd, hdr, sizeof(*hdr), MSG_WAITALL);
    if (n != sizeof(*hdr)) {
        if (n == 0) return -2; /* Connection closed */
        perror("recv header");
        return -1;
    }

    if (hdr->magic != DRAWNET_MAGIC) {
        fprintf(stderr, "Bad magic: 0x%08X\n", hdr->magic);
        return -1;
    }

    if (hdr->payload_len > 0) {
        size_t to_read = hdr->payload_len < max_payload ? hdr->payload_len : max_payload;
        n = recv(fd, payload, to_read, MSG_WAITALL);
        if (n != (ssize_t)to_read) {
            perror("recv payload");
            return -1;
        }
    }

    return 0;
}

/* Server process */
static int run_server(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 1) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("[SERVER] Listening on port %d...\n", port);

    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        close(listen_fd);
        return 1;
    }

    printf("[SERVER] Client connected\n");

    /* Receive HELLO */
    drawnet_wire_header_t hdr;
    drawnet_msg_hello_t hello;
    if (recv_packet(client_fd, &hdr, &hello, sizeof(hello)) < 0) {
        close(client_fd);
        close(listen_fd);
        return 1;
    }

    if (hdr.msg_type != DRAWNET_MSG_HELLO) {
        fprintf(stderr, "[SERVER] Expected HELLO, got %d\n", hdr.msg_type);
        close(client_fd);
        close(listen_fd);
        return 1;
    }

    printf("[SERVER] Received HELLO from '%s' for session '%s'\n", hello.name, hello.session_id);

    /* Send ACK */
    drawnet_msg_ack_t ack;
    memset(&ack, 0, sizeof(ack));
    ack.result = 0; /* Success */
    ack.assigned_id = 100;
    ack.assigned_perm = 3;
    strncpy(ack.session_name, "Test Session", sizeof(ack.session_name) - 1);
    ack.peer_count = 1;

    if (send_packet(client_fd, DRAWNET_MSG_ACK, 1, 1, &ack, sizeof(ack)) < 0) {
        close(client_fd);
        close(listen_fd);
        return 1;
    }

    printf("[SERVER] Sent ACK (assigned ID: %u)\n", ack.assigned_id);

    /* Receive CHAT */
    drawnet_msg_chat_t chat;
    if (recv_packet(client_fd, &hdr, &chat, sizeof(chat)) < 0) {
        close(client_fd);
        close(listen_fd);
        return 1;
    }

    if (hdr.msg_type != DRAWNET_MSG_CHAT) {
        fprintf(stderr, "[SERVER] Expected CHAT, got %d\n", hdr.msg_type);
        close(client_fd);
        close(listen_fd);
        return 1;
    }

    printf("[SERVER] Received CHAT: '%s'\n", chat.message);

    /* Send CHAT reply */
    memset(&chat, 0, sizeof(chat));
    strncpy(chat.message, "Hello from server!", sizeof(chat.message) - 1);

    if (send_packet(client_fd, DRAWNET_MSG_CHAT, 1, 2, &chat, sizeof(chat)) < 0) {
        close(client_fd);
        close(listen_fd);
        return 1;
    }

    printf("[SERVER] Sent CHAT reply\n");

    /* Wait for client to close */
    usleep(100000);

    close(client_fd);
    close(listen_fd);
    printf("[SERVER] Done\n");
    return 0;
}

/* Client process */
static int run_client(int port) {
    /* Wait for server to start */
    usleep(100000);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);

    printf("[CLIENT] Connecting to localhost:%d...\n", port);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }

    printf("[CLIENT] Connected\n");

    /* Send HELLO */
    drawnet_msg_hello_t hello;
    memset(&hello, 0, sizeof(hello));
    strncpy(hello.session_id, "TEST123", sizeof(hello.session_id) - 1);
    strncpy(hello.name, "TestUser", sizeof(hello.name) - 1);
    hello.color_rgba = 0xFF0000FF; /* Red */
    hello.capabilities = 0xFFFF;

    if (send_packet(fd, DRAWNET_MSG_HELLO, 0, 1, &hello, sizeof(hello)) < 0) {
        close(fd);
        return 1;
    }

    printf("[CLIENT] Sent HELLO\n");

    /* Receive ACK */
    drawnet_wire_header_t hdr;
    drawnet_msg_ack_t ack;
    if (recv_packet(fd, &hdr, &ack, sizeof(ack)) < 0) {
        close(fd);
        return 1;
    }

    if (hdr.msg_type != DRAWNET_MSG_ACK) {
        fprintf(stderr, "[CLIENT] Expected ACK, got %d\n", hdr.msg_type);
        close(fd);
        return 1;
    }

    printf("[CLIENT] Received ACK - Session: '%s', Assigned ID: %u\n",
           ack.session_name, ack.assigned_id);

    /* Send CHAT */
    drawnet_msg_chat_t chat;
    memset(&chat, 0, sizeof(chat));
    strncpy(chat.message, "Hello from client!", sizeof(chat.message) - 1);

    if (send_packet(fd, DRAWNET_MSG_CHAT, ack.assigned_id, 2, &chat, sizeof(chat)) < 0) {
        close(fd);
        return 1;
    }

    printf("[CLIENT] Sent CHAT\n");

    /* Receive CHAT reply */
    if (recv_packet(fd, &hdr, &chat, sizeof(chat)) < 0) {
        close(fd);
        return 1;
    }

    if (hdr.msg_type != DRAWNET_MSG_CHAT) {
        fprintf(stderr, "[CLIENT] Expected CHAT, got %d\n", hdr.msg_type);
        close(fd);
        return 1;
    }

    printf("[CLIENT] Received CHAT: '%s'\n", chat.message);

    close(fd);
    printf("[CLIENT] Done\n");
    return 0;
}

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("  DrawNet Network Self-Test\n");
    printf("========================================\n");
    printf("\n");
    printf("Testing wire protocol: magic=0x%08X, header=%zu bytes\n",
           DRAWNET_MAGIC, sizeof(drawnet_wire_header_t));
    printf("\n");

    int port = DRAWNET_DEFAULT_PORT + (getpid() % 1000); /* Avoid port conflicts */

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* Child = client */
        return run_client(port);
    } else {
        /* Parent = server */
        int server_result = run_server(port);

        int status;
        waitpid(pid, &status, 0);
        int client_result = WIFEXITED(status) ? WEXITSTATUS(status) : 1;

        printf("\n");
        printf("========================================\n");
        printf("  Results\n");
        printf("========================================\n");
        printf("  Server: %s\n", server_result == 0 ? "PASS" : "FAIL");
        printf("  Client: %s\n", client_result == 0 ? "PASS" : "FAIL");
        printf("\n");

        if (server_result == 0 && client_result == 0) {
            printf("  ALL TESTS PASSED!\n");
            printf("\n");
            printf("  Wire protocol verified:\n");
            printf("    - TCP connection established\n");
            printf("    - HELLO/ACK handshake works\n");
            printf("    - CHAT messages bidirectional\n");
            printf("    - Packet framing correct\n");
            printf("\n");
            return 0;
        } else {
            printf("  TESTS FAILED\n");
            printf("\n");
            return 1;
        }
    }
}
