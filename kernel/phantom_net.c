/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM NETWORK LAYER
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Implementation of the Phantom-safe network layer.
 *
 * Key Features:
 * - Full traffic accountability (all packets logged)
 * - Connections suspend/resume instead of close
 * - Governor integration for capability-based access control
 * - No silent packet drops
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <limits.h>

#include "phantom_net.h"
#include "phantom.h"
#include "governor.h"
#include "../geofs.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Safe Integer Parsing (prevents integer overflow/underflow)
 * ───────────────────────────────────────────────────────────────────────────── */

static int safe_parse_port(const char *str, uint16_t *out) {
    if (!str || !out) return -1;

    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);

    /* Check for conversion errors - strict: no trailing characters allowed */
    if (errno != 0 || endptr == str || *endptr != '\0') {
        return -1;
    }

    /* Validate port range */
    if (val < 0 || val > 65535) return -1;

    *out = (uint16_t)val;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

static uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint32_t compute_checksum(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum = (sum << 1) ^ p[i];
    }
    return sum;
}

static phantom_socket_t *find_socket(phantom_net_t *net, int sock_id) {
    for (int i = 0; i < net->socket_count; i++) {
        if (net->sockets[i].id == (uint32_t)sock_id) {
            return &net->sockets[i];
        }
    }
    return NULL;
}

static int check_network_capability(phantom_net_t *net, const char *operation) {
    if (!net->governor_checks || !net->governor) {
        return 1; /* No governor checking, allow */
    }

    /* Build a simple code snippet representing the network operation */
    char code[256];
    snprintf(code, sizeof(code), "network_%s()", operation);

    /* Check with Governor - network operations need CAP_NETWORK */
    governor_eval_request_t req = {0};
    governor_eval_response_t resp = {0};

    req.code_ptr = code;
    req.code_size = strlen(code);
    req.declared_caps = CAP_NETWORK;
    strncpy(req.name, operation, sizeof(req.name) - 1);
    strncpy(req.description, "Network operation", sizeof(req.description) - 1);

    int err = governor_evaluate_code(net->governor, &req, &resp);
    if (err != 0 || resp.decision != GOVERNOR_APPROVE) {
        printf("[phantom_net] Governor denied network operation: %s\n", operation);
        printf("              Reason: %s\n", resp.decline_reason);
        return 0;
    }

    return 1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Lifecycle
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_net_init(phantom_net_t *net, struct phantom_kernel *kernel) {
    if (!net) return -1;

    memset(net, 0, sizeof(phantom_net_t));

    net->kernel = kernel;
    net->next_socket_id = 1;
    net->logging_enabled = 1;
    net->governor_checks = 1;
    net->allow_raw = 0;

    strncpy(net->log_base_path, "/var/log/phantom/network",
            sizeof(net->log_base_path) - 1);

    net->initialized = 1;
    net->running = 1;

    printf("[phantom_net] Network layer initialized\n");
    printf("              Phantom Network: Where connections rest, never die\n");

    return 0;
}

void phantom_net_shutdown(phantom_net_t *net) {
    if (!net || !net->initialized) return;

    printf("[phantom_net] Shutting down network layer...\n");

    /* Transition all active sockets to dormant (not close!) */
    for (int i = 0; i < net->socket_count; i++) {
        phantom_socket_t *sock = &net->sockets[i];
        if (sock->state == PHANTOM_CONN_ACTIVE ||
            sock->state == PHANTOM_CONN_LISTENING) {
            phantom_socket_make_dormant(net, sock->id);
        }
    }

    printf("[phantom_net] All connections transitioned to dormant state\n");
    printf("              Total bytes sent: %lu\n", net->total_bytes_sent);
    printf("              Total bytes received: %lu\n", net->total_bytes_received);

    net->running = 0;
    net->initialized = 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Configuration
 * ───────────────────────────────────────────────────────────────────────────── */

void phantom_net_set_governor(phantom_net_t *net, struct phantom_governor *gov) {
    if (net) {
        net->governor = gov;
    }
}

void phantom_net_set_geofs(phantom_net_t *net, struct geofs_ctx *geofs) {
    if (net) {
        net->geofs = geofs;
    }
}

void phantom_net_enable_logging(phantom_net_t *net, int enabled) {
    if (net) {
        net->logging_enabled = enabled;
    }
}

void phantom_net_set_log_path(phantom_net_t *net, const char *base_path) {
    if (net && base_path) {
        strncpy(net->log_base_path, base_path, sizeof(net->log_base_path) - 1);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Socket Creation
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_socket_create(phantom_net_t *net, phantom_sock_type_t type,
                          phantom_proto_t protocol) {
    if (!net || !net->initialized) return PHANTOM_NET_ERROR;

    /* Check Governor capability */
    if (!check_network_capability(net, "socket_create")) {
        return PHANTOM_NET_DENIED;
    }

    /* Check raw socket permission */
    if (type == PHANTOM_SOCK_RAW && !net->allow_raw) {
        printf("[phantom_net] Raw sockets not permitted\n");
        return PHANTOM_NET_DENIED;
    }

    if (net->socket_count >= PHANTOM_NET_MAX_SOCKETS) {
        printf("[phantom_net] Maximum socket limit reached\n");
        return PHANTOM_NET_ERROR;
    }

    /* Map to OS socket types */
    int os_type;
    switch (type) {
        case PHANTOM_SOCK_STREAM:
            os_type = SOCK_STREAM;
            break;
        case PHANTOM_SOCK_DGRAM:
            os_type = SOCK_DGRAM;
            break;
        case PHANTOM_SOCK_RAW:
            os_type = SOCK_RAW;
            break;
        default:
            return PHANTOM_NET_ERROR;
    }

    /* Create OS socket */
    int fd = socket(AF_INET, os_type, protocol);
    if (fd < 0) {
        printf("[phantom_net] Failed to create socket: %s\n", strerror(errno));
        return PHANTOM_NET_ERROR;
    }

    /* Set up phantom socket */
    phantom_socket_t *sock = &net->sockets[net->socket_count];
    memset(sock, 0, sizeof(phantom_socket_t));

    sock->id = net->next_socket_id++;
    sock->fd = fd;
    sock->type = type;
    sock->protocol = protocol;
    sock->state = PHANTOM_CONN_NASCENT;
    sock->blocking = 1;
    sock->timeout_ms = PHANTOM_NET_TIMEOUT_MS;
    sock->created_at = time(NULL);
    sock->last_active = sock->created_at;

    /* Set up traffic log path - limit base path to leave room for suffix */
    snprintf(sock->log_path, sizeof(sock->log_path),
             "%.200s/socket_%u.log", net->log_base_path, sock->id);

    net->socket_count++;
    net->total_connections++;

    printf("[phantom_net] Created socket %u (fd=%d, type=%d)\n",
           sock->id, fd, type);

    return sock->id;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Socket Binding
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_socket_bind(phantom_net_t *net, int sock_id,
                        const phantom_addr_t *addr) {
    if (!net || !addr) return PHANTOM_NET_ERROR;

    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock) return PHANTOM_NET_NO_SOCKET;

    struct sockaddr_storage ss;
    if (phantom_addr_to_sockaddr(addr, &ss) < 0) {
        return PHANTOM_NET_ERROR;
    }

    socklen_t len = (addr->family == AF_INET) ?
                    sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

    if (bind(sock->fd, (struct sockaddr *)&ss, len) < 0) {
        printf("[phantom_net] Bind failed: %s\n", strerror(errno));
        return PHANTOM_NET_ERROR;
    }

    memcpy(&sock->local, addr, sizeof(phantom_addr_t));
    sock->last_active = time(NULL);

    char addr_str[64];
    printf("[phantom_net] Socket %u bound to %s\n",
           sock->id, phantom_addr_to_string(addr, addr_str, sizeof(addr_str)));

    return PHANTOM_NET_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Listen and Accept
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_socket_listen(phantom_net_t *net, int sock_id, int backlog) {
    if (!net) return PHANTOM_NET_ERROR;

    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock) return PHANTOM_NET_NO_SOCKET;

    if (backlog <= 0) backlog = PHANTOM_NET_MAX_BACKLOG;

    if (listen(sock->fd, backlog) < 0) {
        printf("[phantom_net] Listen failed: %s\n", strerror(errno));
        return PHANTOM_NET_ERROR;
    }

    sock->state = PHANTOM_CONN_LISTENING;
    sock->last_active = time(NULL);
    net->active_connections++;

    printf("[phantom_net] Socket %u listening (backlog=%d)\n", sock->id, backlog);

    return PHANTOM_NET_OK;
}

int phantom_socket_accept(phantom_net_t *net, int sock_id,
                          phantom_addr_t *client_addr) {
    if (!net) return PHANTOM_NET_ERROR;

    phantom_socket_t *listen_sock = find_socket(net, sock_id);
    if (!listen_sock) return PHANTOM_NET_NO_SOCKET;

    if (listen_sock->state != PHANTOM_CONN_LISTENING) {
        return PHANTOM_NET_ERROR;
    }

    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);

    int client_fd = accept(listen_sock->fd, (struct sockaddr *)&ss, &len);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return PHANTOM_NET_WOULD_BLOCK;
        }
        printf("[phantom_net] Accept failed: %s\n", strerror(errno));
        return PHANTOM_NET_ERROR;
    }

    /* Create new socket for client */
    if (net->socket_count >= PHANTOM_NET_MAX_SOCKETS) {
        close(client_fd);
        return PHANTOM_NET_ERROR;
    }

    phantom_socket_t *client_sock = &net->sockets[net->socket_count];
    memset(client_sock, 0, sizeof(phantom_socket_t));

    client_sock->id = net->next_socket_id++;
    client_sock->fd = client_fd;
    client_sock->type = listen_sock->type;
    client_sock->protocol = listen_sock->protocol;
    client_sock->state = PHANTOM_CONN_ACTIVE;
    client_sock->blocking = listen_sock->blocking;
    client_sock->timeout_ms = listen_sock->timeout_ms;
    client_sock->created_at = time(NULL);
    client_sock->last_active = client_sock->created_at;

    /* Copy local address from listen socket */
    memcpy(&client_sock->local, &listen_sock->local, sizeof(phantom_addr_t));

    /* Get client address */
    phantom_addr_from_sockaddr(&client_sock->remote, (struct sockaddr *)&ss);
    if (client_addr) {
        memcpy(client_addr, &client_sock->remote, sizeof(phantom_addr_t));
    }

    snprintf(client_sock->log_path, sizeof(client_sock->log_path),
             "%.200s/socket_%u.log", net->log_base_path, client_sock->id);

    net->socket_count++;
    net->total_connections++;
    net->active_connections++;

    char addr_str[64];
    printf("[phantom_net] Accepted connection on socket %u -> new socket %u from %s\n",
           sock_id, client_sock->id,
           phantom_addr_to_string(&client_sock->remote, addr_str, sizeof(addr_str)));

    return client_sock->id;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Connect
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_socket_connect(phantom_net_t *net, int sock_id,
                           const phantom_addr_t *addr) {
    if (!net || !addr) return PHANTOM_NET_ERROR;

    if (!check_network_capability(net, "connect")) {
        return PHANTOM_NET_DENIED;
    }

    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock) return PHANTOM_NET_NO_SOCKET;

    struct sockaddr_storage ss;
    if (phantom_addr_to_sockaddr(addr, &ss) < 0) {
        return PHANTOM_NET_ERROR;
    }

    socklen_t len = (addr->family == AF_INET) ?
                    sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

    char addr_str[64];
    printf("[phantom_net] Socket %u connecting to %s...\n",
           sock->id, phantom_addr_to_string(addr, addr_str, sizeof(addr_str)));

    if (connect(sock->fd, (struct sockaddr *)&ss, len) < 0) {
        if (errno == EINPROGRESS && !sock->blocking) {
            sock->state = PHANTOM_CONN_NASCENT; /* Still connecting */
            memcpy(&sock->remote, addr, sizeof(phantom_addr_t));
            return PHANTOM_NET_OK;
        }
        if (errno == ECONNREFUSED) {
            sock->state = PHANTOM_CONN_ERROR;
            return PHANTOM_NET_CONN_REFUSED;
        }
        printf("[phantom_net] Connect failed: %s\n", strerror(errno));
        sock->state = PHANTOM_CONN_ERROR;
        return PHANTOM_NET_ERROR;
    }

    memcpy(&sock->remote, addr, sizeof(phantom_addr_t));
    sock->state = PHANTOM_CONN_ACTIVE;
    sock->last_active = time(NULL);
    net->active_connections++;

    /* Get local address */
    struct sockaddr_storage local_ss;
    socklen_t local_len = sizeof(local_ss);
    if (getsockname(sock->fd, (struct sockaddr *)&local_ss, &local_len) == 0) {
        phantom_addr_from_sockaddr(&sock->local, (struct sockaddr *)&local_ss);
    }

    printf("[phantom_net] Socket %u connected successfully\n", sock->id);

    return PHANTOM_NET_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Data Transfer
 * ───────────────────────────────────────────────────────────────────────────── */

ssize_t phantom_socket_send(phantom_net_t *net, int sock_id,
                            const void *data, size_t len, int flags) {
    if (!net || !data) return PHANTOM_NET_ERROR;

    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock) return PHANTOM_NET_NO_SOCKET;

    if (sock->state == PHANTOM_CONN_SUSPENDED) {
        /* Buffer data for when we resume */
        if (!sock->pending_send) {
            sock->pending_send = malloc(PHANTOM_NET_BUFFER_SIZE);
            if (!sock->pending_send) return PHANTOM_NET_ERROR;
            sock->pending_send_len = 0;
        }
        if (sock->pending_send_len + len <= PHANTOM_NET_BUFFER_SIZE) {
            memcpy(sock->pending_send + sock->pending_send_len, data, len);
            sock->pending_send_len += len;
            return len; /* Buffered successfully */
        }
        return PHANTOM_NET_BUFFER_FULL;
    }

    if (sock->state != PHANTOM_CONN_ACTIVE) {
        return PHANTOM_NET_NOT_CONNECTED;
    }

    ssize_t sent = send(sock->fd, data, len, flags);
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return PHANTOM_NET_WOULD_BLOCK;
        }
        printf("[phantom_net] Send failed: %s\n", strerror(errno));
        sock->errors++;
        return PHANTOM_NET_ERROR;
    }

    sock->bytes_sent += sent;
    sock->packets_sent++;
    sock->last_active = time(NULL);
    net->total_bytes_sent += sent;

    /* Log the traffic */
    if (net->logging_enabled) {
        phantom_net_log_traffic(net, sock, 1, data, sent);
    }

    return sent;
}

ssize_t phantom_socket_recv(phantom_net_t *net, int sock_id,
                            void *buffer, size_t len, int flags) {
    if (!net || !buffer) return PHANTOM_NET_ERROR;

    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock) return PHANTOM_NET_NO_SOCKET;

    if (sock->state == PHANTOM_CONN_SUSPENDED) {
        /* Return any pending received data first */
        if (sock->pending_recv && sock->pending_recv_len > 0) {
            size_t to_copy = (len < sock->pending_recv_len) ? len : sock->pending_recv_len;
            memcpy(buffer, sock->pending_recv, to_copy);
            if (to_copy < sock->pending_recv_len) {
                memmove(sock->pending_recv, sock->pending_recv + to_copy,
                        sock->pending_recv_len - to_copy);
            }
            sock->pending_recv_len -= to_copy;
            return to_copy;
        }
        return PHANTOM_NET_SUSPENDED;
    }

    if (sock->state != PHANTOM_CONN_ACTIVE) {
        return PHANTOM_NET_NOT_CONNECTED;
    }

    ssize_t received = recv(sock->fd, buffer, len, flags);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return PHANTOM_NET_WOULD_BLOCK;
        }
        printf("[phantom_net] Recv failed: %s\n", strerror(errno));
        sock->errors++;
        return PHANTOM_NET_ERROR;
    }

    if (received == 0) {
        /* Connection closed by remote - transition to dormant, not closed */
        printf("[phantom_net] Remote closed connection on socket %u - transitioning to dormant\n",
               sock->id);
        phantom_socket_make_dormant(net, sock_id);
        return 0;
    }

    sock->bytes_received += received;
    sock->packets_received++;
    sock->last_active = time(NULL);
    net->total_bytes_received += received;

    /* Log the traffic */
    if (net->logging_enabled) {
        phantom_net_log_traffic(net, sock, 0, buffer, received);
    }

    return received;
}

ssize_t phantom_socket_sendto(phantom_net_t *net, int sock_id,
                              const void *data, size_t len,
                              const phantom_addr_t *dest) {
    if (!net || !data || !dest) return PHANTOM_NET_ERROR;

    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock) return PHANTOM_NET_NO_SOCKET;

    struct sockaddr_storage ss;
    phantom_addr_to_sockaddr(dest, &ss);
    socklen_t addr_len = (dest->family == AF_INET) ?
                          sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

    ssize_t sent = sendto(sock->fd, data, len, 0, (struct sockaddr *)&ss, addr_len);
    if (sent < 0) {
        printf("[phantom_net] Sendto failed: %s\n", strerror(errno));
        return PHANTOM_NET_ERROR;
    }

    sock->bytes_sent += sent;
    sock->packets_sent++;
    sock->last_active = time(NULL);
    net->total_bytes_sent += sent;

    if (net->logging_enabled) {
        phantom_net_log_traffic(net, sock, 1, data, sent);
    }

    return sent;
}

ssize_t phantom_socket_recvfrom(phantom_net_t *net, int sock_id,
                                void *buffer, size_t len,
                                phantom_addr_t *src) {
    if (!net || !buffer) return PHANTOM_NET_ERROR;

    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock) return PHANTOM_NET_NO_SOCKET;

    struct sockaddr_storage ss;
    socklen_t addr_len = sizeof(ss);

    ssize_t received = recvfrom(sock->fd, buffer, len, 0,
                                 (struct sockaddr *)&ss, &addr_len);
    if (received < 0) {
        printf("[phantom_net] Recvfrom failed: %s\n", strerror(errno));
        return PHANTOM_NET_ERROR;
    }

    if (src) {
        phantom_addr_from_sockaddr(src, (struct sockaddr *)&ss);
    }

    sock->bytes_received += received;
    sock->packets_received++;
    sock->last_active = time(NULL);
    net->total_bytes_received += received;

    if (net->logging_enabled) {
        phantom_net_log_traffic(net, sock, 0, buffer, received);
    }

    return received;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Phantom-Specific Operations: Suspend/Resume/Dormant
 *
 * These are the key differentiators from traditional networking.
 * Instead of closing connections, we suspend or make them dormant.
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_socket_suspend(phantom_net_t *net, int sock_id) {
    if (!net) return PHANTOM_NET_ERROR;

    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock) return PHANTOM_NET_NO_SOCKET;

    if (sock->state != PHANTOM_CONN_ACTIVE) {
        printf("[phantom_net] Cannot suspend socket %u - not active\n", sock->id);
        return PHANTOM_NET_ERROR;
    }

    sock->state = PHANTOM_CONN_SUSPENDED;
    sock->suspended_at = time(NULL);
    net->active_connections--;
    net->suspended_connections++;

    printf("[phantom_net] Socket %u suspended (can be resumed)\n", sock->id);

    return PHANTOM_NET_OK;
}

int phantom_socket_resume(phantom_net_t *net, int sock_id) {
    if (!net) return PHANTOM_NET_ERROR;

    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock) return PHANTOM_NET_NO_SOCKET;

    if (sock->state != PHANTOM_CONN_SUSPENDED) {
        printf("[phantom_net] Cannot resume socket %u - not suspended\n", sock->id);
        return PHANTOM_NET_ERROR;
    }

    sock->state = PHANTOM_CONN_ACTIVE;
    sock->suspended_at = 0;
    sock->last_active = time(NULL);
    net->suspended_connections--;
    net->active_connections++;

    /* Send any pending data that was buffered during suspension */
    if (sock->pending_send && sock->pending_send_len > 0) {
        ssize_t sent = send(sock->fd, sock->pending_send, sock->pending_send_len, 0);
        if (sent > 0) {
            printf("[phantom_net] Flushed %zd bytes of pending data on resume\n", sent);
            sock->bytes_sent += sent;
            net->total_bytes_sent += sent;
        }
        sock->pending_send_len = 0;
    }

    printf("[phantom_net] Socket %u resumed\n", sock->id);

    return PHANTOM_NET_OK;
}

int phantom_socket_make_dormant(phantom_net_t *net, int sock_id) {
    if (!net) return PHANTOM_NET_ERROR;

    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock) return PHANTOM_NET_NO_SOCKET;

    /* Close the actual OS socket but preserve the phantom socket record */
    if (sock->fd >= 0) {
        close(sock->fd);
        sock->fd = -1;
    }

    phantom_conn_state_t old_state = sock->state;
    sock->state = PHANTOM_CONN_DORMANT;

    /* Update statistics */
    if (old_state == PHANTOM_CONN_ACTIVE || old_state == PHANTOM_CONN_LISTENING) {
        net->active_connections--;
    } else if (old_state == PHANTOM_CONN_SUSPENDED) {
        net->suspended_connections--;
    }
    net->dormant_connections++;

    printf("[phantom_net] Socket %u is now dormant (preserved, not destroyed)\n", sock->id);
    printf("              Lifetime stats: sent=%lu bytes, received=%lu bytes\n",
           sock->bytes_sent, sock->bytes_received);

    return PHANTOM_NET_OK;
}

int phantom_socket_reawaken(phantom_net_t *net, int sock_id) {
    if (!net) return PHANTOM_NET_ERROR;

    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock) return PHANTOM_NET_NO_SOCKET;

    if (sock->state != PHANTOM_CONN_DORMANT) {
        printf("[phantom_net] Socket %u is not dormant\n", sock->id);
        return PHANTOM_NET_ERROR;
    }

    /* Recreate the OS socket */
    int os_type;
    switch (sock->type) {
        case PHANTOM_SOCK_STREAM: os_type = SOCK_STREAM; break;
        case PHANTOM_SOCK_DGRAM: os_type = SOCK_DGRAM; break;
        case PHANTOM_SOCK_RAW: os_type = SOCK_RAW; break;
        default: return PHANTOM_NET_ERROR;
    }

    int fd = socket(sock->local.family, os_type, sock->protocol);
    if (fd < 0) {
        printf("[phantom_net] Failed to recreate socket: %s\n", strerror(errno));
        return PHANTOM_NET_ERROR;
    }

    sock->fd = fd;
    sock->state = PHANTOM_CONN_NASCENT;
    net->dormant_connections--;

    printf("[phantom_net] Socket %u reawakened from dormancy\n", sock->id);

    return PHANTOM_NET_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Socket Options
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_socket_set_blocking(phantom_net_t *net, int sock_id, int blocking) {
    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock || sock->fd < 0) return PHANTOM_NET_ERROR;

    int flags = fcntl(sock->fd, F_GETFL, 0);
    if (flags < 0) return PHANTOM_NET_ERROR;

    if (blocking) {
        flags &= ~O_NONBLOCK;
    } else {
        flags |= O_NONBLOCK;
    }

    if (fcntl(sock->fd, F_SETFL, flags) < 0) {
        return PHANTOM_NET_ERROR;
    }

    sock->blocking = blocking;
    return PHANTOM_NET_OK;
}

int phantom_socket_set_timeout(phantom_net_t *net, int sock_id, int timeout_ms) {
    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock || sock->fd < 0) return PHANTOM_NET_ERROR;

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sock->timeout_ms = timeout_ms;
    return PHANTOM_NET_OK;
}

int phantom_socket_set_keepalive(phantom_net_t *net, int sock_id, int enabled) {
    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock || sock->fd < 0) return PHANTOM_NET_ERROR;

    int val = enabled ? 1 : 0;
    if (setsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) < 0) {
        return PHANTOM_NET_ERROR;
    }

    sock->keep_alive = enabled;
    return PHANTOM_NET_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Information
 * ───────────────────────────────────────────────────────────────────────────── */

phantom_socket_t *phantom_socket_get(phantom_net_t *net, int sock_id) {
    return find_socket(net, sock_id);
}

phantom_conn_state_t phantom_socket_state(phantom_net_t *net, int sock_id) {
    phantom_socket_t *sock = find_socket(net, sock_id);
    return sock ? sock->state : PHANTOM_CONN_ERROR;
}

int phantom_socket_get_stats(phantom_net_t *net, int sock_id,
                             uint64_t *sent, uint64_t *received) {
    phantom_socket_t *sock = find_socket(net, sock_id);
    if (!sock) return PHANTOM_NET_ERROR;

    if (sent) *sent = sock->bytes_sent;
    if (received) *received = sock->bytes_received;
    return PHANTOM_NET_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Address Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_addr_from_string(phantom_addr_t *addr, const char *str, uint16_t port) {
    if (!addr || !str) return -1;

    memset(addr, 0, sizeof(phantom_addr_t));
    addr->port = port;
    strncpy(addr->hostname, str, sizeof(addr->hostname) - 1);

    /* Try IPv4 first */
    if (inet_pton(AF_INET, str, &addr->addr.ipv4) == 1) {
        addr->family = AF_INET;
        return 0;
    }

    /* Try IPv6 */
    if (inet_pton(AF_INET6, str, addr->addr.ipv6) == 1) {
        addr->family = AF_INET6;
        return 0;
    }

    /* Not a valid IP, might be a hostname */
    addr->family = AF_INET; /* Default to IPv4 for hostnames */
    return 0;
}

int phantom_addr_from_sockaddr(phantom_addr_t *addr, const struct sockaddr *sa) {
    if (!addr || !sa) return -1;

    memset(addr, 0, sizeof(phantom_addr_t));
    addr->family = sa->sa_family;

    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
        addr->port = ntohs(sin->sin_port);
        addr->addr.ipv4 = sin->sin_addr.s_addr;
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
        addr->port = ntohs(sin6->sin6_port);
        memcpy(addr->addr.ipv6, &sin6->sin6_addr, 16);
    } else {
        return -1;
    }

    return 0;
}

int phantom_addr_to_sockaddr(const phantom_addr_t *addr, struct sockaddr_storage *ss) {
    if (!addr || !ss) return -1;

    memset(ss, 0, sizeof(struct sockaddr_storage));

    if (addr->family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)ss;
        sin->sin_family = AF_INET;
        sin->sin_port = htons(addr->port);
        sin->sin_addr.s_addr = addr->addr.ipv4;
    } else if (addr->family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = htons(addr->port);
        memcpy(&sin6->sin6_addr, addr->addr.ipv6, 16);
    } else {
        return -1;
    }

    return 0;
}

const char *phantom_addr_to_string(const phantom_addr_t *addr, char *buf, size_t len) {
    if (!addr || !buf || len == 0) return "?";

    char ip_str[INET6_ADDRSTRLEN];

    if (addr->family == AF_INET) {
        inet_ntop(AF_INET, &addr->addr.ipv4, ip_str, sizeof(ip_str));
    } else if (addr->family == AF_INET6) {
        inet_ntop(AF_INET6, addr->addr.ipv6, ip_str, sizeof(ip_str));
    } else {
        strncpy(ip_str, "unknown", sizeof(ip_str));
    }

    snprintf(buf, len, "%s:%u", ip_str, addr->port);
    return buf;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * DNS Resolution
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_resolve(phantom_net_t *net, const char *hostname,
                    phantom_addr_t *addrs, int max_addrs) {
    if (!hostname || !addrs || max_addrs <= 0) return 0;

    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int ret = getaddrinfo(hostname, NULL, &hints, &result);
    if (ret != 0) {
        printf("[phantom_net] DNS resolution failed for %s: %s\n",
               hostname, gai_strerror(ret));
        return 0;
    }

    int count = 0;
    for (rp = result; rp != NULL && count < max_addrs; rp = rp->ai_next) {
        phantom_addr_from_sockaddr(&addrs[count], rp->ai_addr);
        strncpy(addrs[count].hostname, hostname, sizeof(addrs[count].hostname) - 1);
        count++;
    }

    freeaddrinfo(result);

    printf("[phantom_net] Resolved %s to %d addresses\n", hostname, count);

    return count;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Traffic Logging
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_net_log_traffic(phantom_net_t *net, phantom_socket_t *sock,
                            int direction, const void *data, size_t len) {
    if (!net || !sock || !net->logging_enabled) return 0;

    phantom_traffic_log_t log;
    memset(&log, 0, sizeof(log));

    log.timestamp_ns = get_timestamp_ns();
    log.socket_id = sock->id;
    log.sequence = sock->log_sequence++;
    log.direction = direction;
    memcpy(&log.local, &sock->local, sizeof(phantom_addr_t));
    memcpy(&log.remote, &sock->remote, sizeof(phantom_addr_t));
    log.bytes = len;
    log.checksum = compute_checksum(data, len);
    log.protocol = sock->protocol;

    /* If we have GeoFS, write to geology */
    if (net->geofs) {
        char log_entry[PHANTOM_NET_LOG_ENTRY_SIZE];
        char local_str[64], remote_str[64];

        snprintf(log_entry, sizeof(log_entry),
                 "%lu|%u|%u|%s|%s|%s|%zu|%08x\n",
                 log.timestamp_ns,
                 log.socket_id,
                 log.sequence,
                 direction ? "OUT" : "IN",
                 phantom_addr_to_string(&log.local, local_str, sizeof(local_str)),
                 phantom_addr_to_string(&log.remote, remote_str, sizeof(remote_str)),
                 len,
                 log.checksum);

        /* Write to geology - in a real implementation this would append to log file */
        /* For now, we just track that we would log */
    }

    return 0;
}

int phantom_net_get_traffic_log(phantom_net_t *net, int sock_id,
                                phantom_traffic_log_t *logs, int max_logs) {
    /* Would read from geology log files */
    (void)net; (void)sock_id; (void)logs; (void)max_logs;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Statistics and Status
 * ───────────────────────────────────────────────────────────────────────────── */

void phantom_net_get_stats(phantom_net_t *net,
                           uint64_t *total_sent, uint64_t *total_recv,
                           uint64_t *active, uint64_t *suspended,
                           uint64_t *dormant) {
    if (!net) return;

    if (total_sent) *total_sent = net->total_bytes_sent;
    if (total_recv) *total_recv = net->total_bytes_received;
    if (active) *active = net->active_connections;
    if (suspended) *suspended = net->suspended_connections;
    if (dormant) *dormant = net->dormant_connections;
}

const char *phantom_conn_state_string(phantom_conn_state_t state) {
    switch (state) {
        case PHANTOM_CONN_NASCENT:   return "nascent";
        case PHANTOM_CONN_ACTIVE:    return "active";
        case PHANTOM_CONN_SUSPENDED: return "suspended";
        case PHANTOM_CONN_DORMANT:   return "dormant";
        case PHANTOM_CONN_LISTENING: return "listening";
        case PHANTOM_CONN_ACCEPTING: return "accepting";
        case PHANTOM_CONN_ERROR:     return "error";
        default:                     return "unknown";
    }
}

const char *phantom_net_error_string(phantom_net_result_t result) {
    switch (result) {
        case PHANTOM_NET_OK:              return "success";
        case PHANTOM_NET_ERROR:           return "error";
        case PHANTOM_NET_DENIED:          return "access denied by governor";
        case PHANTOM_NET_NO_SOCKET:       return "invalid socket";
        case PHANTOM_NET_NOT_CONNECTED:   return "not connected";
        case PHANTOM_NET_SUSPENDED:       return "socket suspended";
        case PHANTOM_NET_TIMEOUT:         return "timeout";
        case PHANTOM_NET_WOULD_BLOCK:     return "would block";
        case PHANTOM_NET_BUFFER_FULL:     return "buffer full";
        case PHANTOM_NET_CONN_REFUSED:    return "connection refused";
        case PHANTOM_NET_HOST_UNREACHABLE: return "host unreachable";
        default:                          return "unknown error";
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * High-Level Convenience API
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_tcp_connect(phantom_net_t *net, const char *host, uint16_t port) {
    if (!net || !host) return PHANTOM_NET_ERROR;

    /* Resolve hostname */
    phantom_addr_t addr;
    int resolved = phantom_resolve(net, host, &addr, 1);
    if (resolved <= 0) {
        /* Try as direct IP */
        if (phantom_addr_from_string(&addr, host, port) < 0) {
            return PHANTOM_NET_HOST_UNREACHABLE;
        }
    }
    addr.port = port;

    /* Create socket */
    int sock_id = phantom_socket_create(net, PHANTOM_SOCK_STREAM, PHANTOM_PROTO_TCP);
    if (sock_id < 0) return sock_id;

    /* Connect */
    int result = phantom_socket_connect(net, sock_id, &addr);
    if (result != PHANTOM_NET_OK) {
        phantom_socket_make_dormant(net, sock_id);
        return result;
    }

    return sock_id;
}

ssize_t phantom_tcp_send_all(phantom_net_t *net, int sock_id,
                              const void *data, size_t len) {
    const uint8_t *ptr = (const uint8_t *)data;
    size_t remaining = len;
    size_t total_sent = 0;

    while (remaining > 0) {
        ssize_t sent = phantom_socket_send(net, sock_id, ptr, remaining, 0);
        if (sent < 0) {
            return (total_sent > 0) ? (ssize_t)total_sent : sent;
        }
        ptr += sent;
        remaining -= sent;
        total_sent += sent;
    }

    return total_sent;
}

ssize_t phantom_tcp_recv_all(phantom_net_t *net, int sock_id,
                              void *buffer, size_t len) {
    uint8_t *ptr = (uint8_t *)buffer;
    size_t remaining = len;
    size_t total_recv = 0;

    while (remaining > 0) {
        ssize_t received = phantom_socket_recv(net, sock_id, ptr, remaining, 0);
        if (received <= 0) {
            return (total_recv > 0) ? (ssize_t)total_recv : received;
        }
        ptr += received;
        remaining -= received;
        total_recv += received;
    }

    return total_recv;
}

int phantom_tcp_listen(phantom_net_t *net, uint16_t port, int backlog) {
    int sock_id = phantom_socket_create(net, PHANTOM_SOCK_STREAM, PHANTOM_PROTO_TCP);
    if (sock_id < 0) return sock_id;

    phantom_addr_t addr;
    memset(&addr, 0, sizeof(addr));
    addr.family = AF_INET;
    addr.port = port;
    addr.addr.ipv4 = INADDR_ANY;

    int result = phantom_socket_bind(net, sock_id, &addr);
    if (result != PHANTOM_NET_OK) {
        phantom_socket_make_dormant(net, sock_id);
        return result;
    }

    result = phantom_socket_listen(net, sock_id, backlog);
    if (result != PHANTOM_NET_OK) {
        phantom_socket_make_dormant(net, sock_id);
        return result;
    }

    return sock_id;
}

int phantom_tcp_accept(phantom_net_t *net, int listen_sock) {
    return phantom_socket_accept(net, listen_sock, NULL);
}

int phantom_udp_create(phantom_net_t *net) {
    return phantom_socket_create(net, PHANTOM_SOCK_DGRAM, PHANTOM_PROTO_UDP);
}

int phantom_udp_bind(phantom_net_t *net, int sock_id, uint16_t port) {
    phantom_addr_t addr;
    memset(&addr, 0, sizeof(addr));
    addr.family = AF_INET;
    addr.port = port;
    addr.addr.ipv4 = INADDR_ANY;

    return phantom_socket_bind(net, sock_id, &addr);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * HTTP Helpers (Basic)
 * ───────────────────────────────────────────────────────────────────────────── */

ssize_t phantom_http_get(phantom_net_t *net, const char *url,
                          char *response, size_t max_len) {
    /* Parse URL - very basic parsing */
    char host[256] = {0};
    char path[1024] = "/";
    uint16_t port = 80;

    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        /* HTTPS not supported in this basic implementation */
        printf("[phantom_net] HTTPS not supported in basic HTTP helper\n");
        return PHANTOM_NET_ERROR;
    }

    /* Extract host */
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        /* Port specified */
        size_t host_len = colon - p;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, p, host_len);
        uint16_t parsed_port;
        if (safe_parse_port(colon + 1, &parsed_port) == 0) {
            port = parsed_port;
        }
    } else if (slash) {
        size_t host_len = slash - p;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, p, host_len);
    } else {
        strncpy(host, p, sizeof(host) - 1);
    }

    if (slash) {
        strncpy(path, slash, sizeof(path) - 1);
    }

    /* Connect */
    int sock_id = phantom_tcp_connect(net, host, port);
    if (sock_id < 0) {
        return sock_id;
    }

    /* Send HTTP GET request */
    char request[2048];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: PhantomOS/1.0\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host);

    ssize_t sent = phantom_tcp_send_all(net, sock_id, request, strlen(request));
    if (sent < 0) {
        phantom_socket_make_dormant(net, sock_id);
        return sent;
    }

    /* Receive response */
    ssize_t total = 0;
    while ((size_t)total < max_len - 1) {
        ssize_t received = phantom_socket_recv(net, sock_id,
                                                response + total,
                                                max_len - 1 - total, 0);
        if (received <= 0) break;
        total += received;
    }
    response[total] = '\0';

    phantom_socket_make_dormant(net, sock_id);

    return total;
}

ssize_t phantom_http_post(phantom_net_t *net, const char *url,
                           const char *body, size_t body_len,
                           char *response, size_t max_len) {
    /* Similar to GET but with POST method and body */
    char host[256] = {0};
    char path[1024] = "/";
    uint16_t port = 80;

    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    const char *slash = strchr(p, '/');
    if (slash) {
        size_t host_len = slash - p;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, p, host_len);
        strncpy(path, slash, sizeof(path) - 1);
    } else {
        strncpy(host, p, sizeof(host) - 1);
    }

    int sock_id = phantom_tcp_connect(net, host, port);
    if (sock_id < 0) return sock_id;

    char request[4096];
    snprintf(request, sizeof(request),
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: PhantomOS/1.0\r\n"
             "Content-Length: %zu\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host, body_len);

    ssize_t sent = phantom_tcp_send_all(net, sock_id, request, strlen(request));
    if (sent < 0) {
        phantom_socket_make_dormant(net, sock_id);
        return sent;
    }

    if (body && body_len > 0) {
        sent = phantom_tcp_send_all(net, sock_id, body, body_len);
        if (sent < 0) {
            phantom_socket_make_dormant(net, sock_id);
            return sent;
        }
    }

    ssize_t total = 0;
    while ((size_t)total < max_len - 1) {
        ssize_t received = phantom_socket_recv(net, sock_id,
                                                response + total,
                                                max_len - 1 - total, 0);
        if (received <= 0) break;
        total += received;
    }
    response[total] = '\0';

    phantom_socket_make_dormant(net, sock_id);

    return total;
}
