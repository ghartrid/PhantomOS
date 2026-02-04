/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM NETWORK LAYER
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * A network layer that embodies the Phantom philosophy:
 * - Connections are never "closed", they transition to DORMANT state
 * - No silent packet drops - all traffic is accounted for
 * - Full traffic logging to geology for audit trails
 * - Governor integration for capability-based network access
 *
 * Key Principles:
 * 1. ACCOUNTABILITY: Every packet sent/received is logged
 * 2. PERSISTENCE: Connections can suspend and resume, not just close
 * 3. TRANSPARENCY: All network operations are auditable
 * 4. SAFETY: Governor controls which code can access the network
 */

#ifndef PHANTOM_NET_H
#define PHANTOM_NET_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/socket.h>

/* Forward declarations */
struct phantom_kernel;
struct phantom_governor;
struct geofs_ctx;

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────── */

#define PHANTOM_NET_MAX_SOCKETS     256
#define PHANTOM_NET_MAX_LISTENERS   64
#define PHANTOM_NET_BUFFER_SIZE     65536
#define PHANTOM_NET_MAX_BACKLOG     128
#define PHANTOM_NET_TIMEOUT_MS      30000
#define PHANTOM_NET_LOG_ENTRY_SIZE  512

/* ─────────────────────────────────────────────────────────────────────────────
 * Connection States
 *
 * Unlike traditional networking where connections are "open" or "closed",
 * Phantom connections have richer lifecycle states. A connection is never
 * truly destroyed - it transitions to DORMANT where it can be resumed.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PHANTOM_CONN_NASCENT = 0,   /* Being created, not yet connected */
    PHANTOM_CONN_ACTIVE,        /* Fully connected and operational */
    PHANTOM_CONN_SUSPENDED,     /* Temporarily paused (can resume) */
    PHANTOM_CONN_DORMANT,       /* Inactive but preserved (traditional "closed") */
    PHANTOM_CONN_LISTENING,     /* Server socket awaiting connections */
    PHANTOM_CONN_ACCEPTING,     /* In process of accepting connection */
    PHANTOM_CONN_ERROR          /* Error state (preserved for diagnosis) */
} phantom_conn_state_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Socket Types
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PHANTOM_SOCK_STREAM = 1,    /* TCP-like reliable stream */
    PHANTOM_SOCK_DGRAM,         /* UDP-like datagrams */
    PHANTOM_SOCK_RAW            /* Raw packets (requires elevated capability) */
} phantom_sock_type_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Protocol Types
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PHANTOM_PROTO_TCP = 6,
    PHANTOM_PROTO_UDP = 17,
    PHANTOM_PROTO_ICMP = 1
} phantom_proto_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Address Structure
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_addr {
    int family;                 /* AF_INET or AF_INET6 */
    uint16_t port;
    union {
        uint32_t ipv4;          /* IPv4 address */
        uint8_t ipv6[16];       /* IPv6 address */
    } addr;
    char hostname[256];         /* Original hostname (for logging) */
} phantom_addr_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Traffic Log Entry
 *
 * Every packet sent or received is logged for accountability.
 * These logs are written to geology, making them immutable and auditable.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_traffic_log {
    uint64_t timestamp_ns;      /* Nanosecond timestamp */
    uint32_t socket_id;         /* Socket that handled this traffic */
    uint32_t sequence;          /* Sequence number for ordering */

    int direction;              /* 0 = incoming, 1 = outgoing */
    phantom_addr_t local;       /* Local address */
    phantom_addr_t remote;      /* Remote address */

    size_t bytes;               /* Number of bytes transferred */
    uint32_t checksum;          /* Data checksum for integrity */

    /* Metadata */
    int protocol;
    int flags;
    char label[64];             /* Optional label for this transfer */
} phantom_traffic_log_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Socket Structure
 *
 * A phantom_socket wraps a real socket with Phantom semantics:
 * - Tracks all traffic for accountability
 * - Supports suspend/resume instead of just close
 * - Integrates with Governor for access control
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_socket {
    uint32_t id;                /* Unique socket identifier */
    int fd;                     /* Underlying OS file descriptor (-1 if dormant) */

    phantom_sock_type_t type;
    phantom_proto_t protocol;
    phantom_conn_state_t state;

    phantom_addr_t local;       /* Local binding */
    phantom_addr_t remote;      /* Remote endpoint (for connected sockets) */

    /* Statistics */
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t packets_sent;
    uint64_t packets_received;
    uint64_t errors;

    /* Timestamps */
    time_t created_at;
    time_t last_active;
    time_t suspended_at;        /* When suspended (0 if not suspended) */

    /* Options */
    int blocking;               /* Blocking mode */
    int timeout_ms;             /* Operation timeout */
    int keep_alive;             /* Keep-alive enabled */

    /* Buffers for suspended state */
    uint8_t *pending_send;      /* Data waiting to be sent on resume */
    size_t pending_send_len;
    uint8_t *pending_recv;      /* Data received while processing */
    size_t pending_recv_len;

    /* Owner information */
    uint32_t owner_pid;         /* Process that created this socket */
    char owner_name[64];        /* Process name for logging */

    /* Geology logging */
    char log_path[256];         /* Path to traffic log file */
    uint32_t log_sequence;      /* Current sequence number */

} phantom_socket_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Network Context
 *
 * Central management structure for all network operations.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_net {
    /* Socket management */
    phantom_socket_t sockets[PHANTOM_NET_MAX_SOCKETS];
    int socket_count;
    uint32_t next_socket_id;

    /* Statistics */
    uint64_t total_bytes_sent;
    uint64_t total_bytes_received;
    uint64_t total_connections;
    uint64_t active_connections;
    uint64_t suspended_connections;
    uint64_t dormant_connections;

    /* Configuration */
    int logging_enabled;        /* Log all traffic to geology */
    int governor_checks;        /* Enforce Governor capability checks */
    int allow_raw;              /* Allow raw socket access */

    /* References */
    struct phantom_kernel *kernel;
    struct phantom_governor *governor;
    struct geofs_ctx *geofs;

    /* Logging */
    char log_base_path[256];    /* Base path for traffic logs */

    /* State */
    int initialized;
    int running;

} phantom_net_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Result Codes
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PHANTOM_NET_OK = 0,
    PHANTOM_NET_ERROR = -1,
    PHANTOM_NET_DENIED = -2,        /* Governor denied access */
    PHANTOM_NET_NO_SOCKET = -3,     /* Invalid socket */
    PHANTOM_NET_NOT_CONNECTED = -4, /* Socket not in connected state */
    PHANTOM_NET_SUSPENDED = -5,     /* Socket is suspended */
    PHANTOM_NET_TIMEOUT = -6,       /* Operation timed out */
    PHANTOM_NET_WOULD_BLOCK = -7,   /* Non-blocking operation would block */
    PHANTOM_NET_BUFFER_FULL = -8,   /* Send buffer full */
    PHANTOM_NET_CONN_REFUSED = -9,  /* Connection refused by remote */
    PHANTOM_NET_HOST_UNREACHABLE = -10
} phantom_net_result_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Network Layer API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Lifecycle */
int phantom_net_init(phantom_net_t *net, struct phantom_kernel *kernel);
void phantom_net_shutdown(phantom_net_t *net);

/* Configuration */
void phantom_net_set_governor(phantom_net_t *net, struct phantom_governor *gov);
void phantom_net_set_geofs(phantom_net_t *net, struct geofs_ctx *geofs);
void phantom_net_enable_logging(phantom_net_t *net, int enabled);
void phantom_net_set_log_path(phantom_net_t *net, const char *base_path);

/* Socket Operations */
int phantom_socket_create(phantom_net_t *net, phantom_sock_type_t type,
                          phantom_proto_t protocol);
int phantom_socket_bind(phantom_net_t *net, int sock_id,
                        const phantom_addr_t *addr);
int phantom_socket_listen(phantom_net_t *net, int sock_id, int backlog);
int phantom_socket_accept(phantom_net_t *net, int sock_id,
                          phantom_addr_t *client_addr);
int phantom_socket_connect(phantom_net_t *net, int sock_id,
                           const phantom_addr_t *addr);

/* Data Transfer */
ssize_t phantom_socket_send(phantom_net_t *net, int sock_id,
                            const void *data, size_t len, int flags);
ssize_t phantom_socket_recv(phantom_net_t *net, int sock_id,
                            void *buffer, size_t len, int flags);
ssize_t phantom_socket_sendto(phantom_net_t *net, int sock_id,
                              const void *data, size_t len,
                              const phantom_addr_t *dest);
ssize_t phantom_socket_recvfrom(phantom_net_t *net, int sock_id,
                                void *buffer, size_t len,
                                phantom_addr_t *src);

/* Phantom-Specific Operations */
int phantom_socket_suspend(phantom_net_t *net, int sock_id);
int phantom_socket_resume(phantom_net_t *net, int sock_id);
int phantom_socket_make_dormant(phantom_net_t *net, int sock_id);
int phantom_socket_reawaken(phantom_net_t *net, int sock_id);

/* Socket Options */
int phantom_socket_set_blocking(phantom_net_t *net, int sock_id, int blocking);
int phantom_socket_set_timeout(phantom_net_t *net, int sock_id, int timeout_ms);
int phantom_socket_set_keepalive(phantom_net_t *net, int sock_id, int enabled);

/* Information */
phantom_socket_t *phantom_socket_get(phantom_net_t *net, int sock_id);
phantom_conn_state_t phantom_socket_state(phantom_net_t *net, int sock_id);
int phantom_socket_get_stats(phantom_net_t *net, int sock_id,
                             uint64_t *sent, uint64_t *received);

/* Address Helpers */
int phantom_addr_from_string(phantom_addr_t *addr, const char *str, uint16_t port);
int phantom_addr_from_sockaddr(phantom_addr_t *addr, const struct sockaddr *sa);
int phantom_addr_to_sockaddr(const phantom_addr_t *addr, struct sockaddr_storage *ss);
const char *phantom_addr_to_string(const phantom_addr_t *addr, char *buf, size_t len);

/* DNS Resolution (logged) */
int phantom_resolve(phantom_net_t *net, const char *hostname,
                    phantom_addr_t *addrs, int max_addrs);

/* Traffic Logging */
int phantom_net_log_traffic(phantom_net_t *net, phantom_socket_t *sock,
                            int direction, const void *data, size_t len);
int phantom_net_get_traffic_log(phantom_net_t *net, int sock_id,
                                phantom_traffic_log_t *logs, int max_logs);

/* Status and Statistics */
void phantom_net_get_stats(phantom_net_t *net,
                           uint64_t *total_sent, uint64_t *total_recv,
                           uint64_t *active, uint64_t *suspended,
                           uint64_t *dormant);
const char *phantom_conn_state_string(phantom_conn_state_t state);
const char *phantom_net_error_string(phantom_net_result_t result);

/* ─────────────────────────────────────────────────────────────────────────────
 * High-Level Convenience API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Simple TCP client */
int phantom_tcp_connect(phantom_net_t *net, const char *host, uint16_t port);
ssize_t phantom_tcp_send_all(phantom_net_t *net, int sock_id,
                              const void *data, size_t len);
ssize_t phantom_tcp_recv_all(phantom_net_t *net, int sock_id,
                              void *buffer, size_t len);

/* Simple TCP server */
int phantom_tcp_listen(phantom_net_t *net, uint16_t port, int backlog);
int phantom_tcp_accept(phantom_net_t *net, int listen_sock);

/* Simple UDP */
int phantom_udp_create(phantom_net_t *net);
int phantom_udp_bind(phantom_net_t *net, int sock_id, uint16_t port);

/* HTTP helpers (basic) */
ssize_t phantom_http_get(phantom_net_t *net, const char *url,
                          char *response, size_t max_len);
ssize_t phantom_http_post(phantom_net_t *net, const char *url,
                           const char *body, size_t body_len,
                           char *response, size_t max_len);

#endif /* PHANTOM_NET_H */
