/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                    PHANTOM QRNET TRANSPORT PROTOCOL
 *                      "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Content-addressed secure transport for QRNet distributed file network.
 * Files are identified by their SHA-256 hash and can be fetched from any
 * trusted node - content authenticity is verified by hash match.
 */

#ifndef PHANTOM_QRNET_TRANSPORT_H
#define PHANTOM_QRNET_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* Forward declarations */
struct qrnet_system;
struct qrnet_code;
struct qrnet_node;

/* ==============================================================================
 * Constants
 * ============================================================================== */

#define QRNET_TRANSPORT_VERSION     1
#define QRNET_MAX_CHUNK_SIZE        65536       /* 64KB chunks */
#define QRNET_MAX_CONTENT_SIZE      (256*1024*1024)  /* 256MB max file */
#define QRNET_CONTENT_STORE_DIR     "/tmp/qrnet/content"
#define QRNET_DEFAULT_PORT          7847        /* "QRNT" on phone keypad */
#define QRNET_MAX_PEERS             64
#define QRNET_HANDSHAKE_TIMEOUT     10000       /* 10 seconds */
#define QRNET_TRANSFER_TIMEOUT      300000      /* 5 minutes */

/* ==============================================================================
 * Protocol Messages
 * ============================================================================== */

typedef enum {
    QRNET_MSG_HANDSHAKE     = 0x01,     /* Initial connection handshake */
    QRNET_MSG_HANDSHAKE_ACK = 0x02,     /* Handshake acknowledgment */
    QRNET_MSG_CONTENT_QUERY = 0x10,     /* "Do you have content X?" */
    QRNET_MSG_CONTENT_HAVE  = 0x11,     /* "Yes, I have it" */
    QRNET_MSG_CONTENT_WANT  = 0x12,     /* "No, but I want it" */
    QRNET_MSG_CONTENT_REQ   = 0x20,     /* Request content by hash */
    QRNET_MSG_CONTENT_DATA  = 0x21,     /* Content data chunk */
    QRNET_MSG_CONTENT_END   = 0x22,     /* End of content transfer */
    QRNET_MSG_CONTENT_ERR   = 0x23,     /* Transfer error */
    QRNET_MSG_ANNOUNCE      = 0x30,     /* Announce available content */
    QRNET_MSG_PING          = 0x40,     /* Keep-alive ping */
    QRNET_MSG_PONG          = 0x41,     /* Keep-alive response */
    QRNET_MSG_GOODBYE       = 0xFF      /* Graceful disconnect */
} qrnet_msg_type_t;

/* Protocol message header */
typedef struct __attribute__((packed)) {
    uint8_t     version;                /* Protocol version */
    uint8_t     msg_type;               /* Message type */
    uint16_t    flags;                  /* Message flags */
    uint32_t    payload_len;            /* Payload length */
    uint32_t    sequence;               /* Sequence number */
    uint8_t     hash[32];               /* Content hash (context-dependent) */
} qrnet_msg_header_t;

/* Handshake message */
typedef struct __attribute__((packed)) {
    qrnet_msg_header_t header;
    char        node_id[64];            /* Sender's node ID */
    char        pubkey_hex[131];        /* Sender's public key */
    uint32_t    capabilities;           /* Supported features */
    uint32_t    governor_state;         /* Current governor state version */
} qrnet_msg_handshake_t;

/* Content query/response */
typedef struct __attribute__((packed)) {
    qrnet_msg_header_t header;
    uint64_t    content_size;           /* Size if known, 0 otherwise */
    char        content_type[64];       /* MIME type hint */
} qrnet_msg_content_info_t;

/* Content data chunk */
typedef struct __attribute__((packed)) {
    qrnet_msg_header_t header;
    uint32_t    chunk_index;            /* Chunk sequence number */
    uint32_t    chunk_size;             /* Actual data size in this chunk */
    uint64_t    total_size;             /* Total content size */
    /* Followed by chunk_size bytes of data */
} qrnet_msg_content_chunk_t;

/* ==============================================================================
 * Content Store
 * ============================================================================== */

typedef enum {
    QRNET_CONTENT_LOCAL     = 0,        /* Stored locally */
    QRNET_CONTENT_CACHED    = 1,        /* Cached from network */
    QRNET_CONTENT_PINNED    = 2,        /* Pinned (don't evict) */
    QRNET_CONTENT_PENDING   = 3         /* Transfer in progress */
} qrnet_content_status_t;

typedef struct qrnet_content_entry {
    char        hash_hex[65];           /* SHA-256 hash (hex) */
    uint8_t     hash_bytes[32];         /* SHA-256 hash (binary) */
    char        local_path[640];        /* Path in content store */
    char        original_name[256];     /* Original filename */
    char        content_type[64];       /* MIME type */
    uint64_t    size;                   /* Content size in bytes */
    time_t      created;                /* When stored */
    time_t      last_accessed;          /* Last access time */
    uint32_t    access_count;           /* Number of accesses */
    qrnet_content_status_t status;

    /* Source tracking */
    char        source_node[64];        /* Node we got it from */
    char        creator_identity[64];   /* Original creator */

    struct qrnet_content_entry *next;
} qrnet_content_entry_t;

typedef struct qrnet_content_store {
    char        base_path[512];         /* Storage directory */
    qrnet_content_entry_t *entries;     /* Content entries */
    uint32_t    entry_count;
    uint64_t    total_size;             /* Total stored bytes */
    uint64_t    max_size;               /* Maximum store size */

    /* Statistics */
    uint64_t    bytes_stored;
    uint64_t    bytes_served;
    uint32_t    items_stored;
    uint32_t    items_served;
} qrnet_content_store_t;

/* ==============================================================================
 * Transfer State
 * ============================================================================== */

typedef enum {
    QRNET_TRANSFER_IDLE         = 0,
    QRNET_TRANSFER_CONNECTING   = 1,
    QRNET_TRANSFER_HANDSHAKING  = 2,
    QRNET_TRANSFER_QUERYING     = 3,
    QRNET_TRANSFER_RECEIVING    = 4,
    QRNET_TRANSFER_SENDING      = 5,
    QRNET_TRANSFER_VERIFYING    = 6,
    QRNET_TRANSFER_COMPLETE     = 7,
    QRNET_TRANSFER_FAILED       = 8
} qrnet_transfer_state_t;

typedef struct qrnet_transfer {
    uint32_t    transfer_id;            /* Unique transfer ID */
    char        content_hash[65];       /* Content being transferred */
    char        peer_node_id[64];       /* Remote node */
    char        peer_address[256];      /* Remote address */
    int         socket_fd;              /* Socket descriptor */
    int         is_encrypted;           /* TLS enabled */

    qrnet_transfer_state_t state;
    int         direction;              /* 0=receiving, 1=sending */

    /* Progress tracking */
    uint64_t    total_size;
    uint64_t    transferred;
    uint32_t    chunks_total;
    uint32_t    chunks_done;
    time_t      started;
    time_t      last_activity;

    /* Receive buffer */
    uint8_t     *buffer;
    size_t      buffer_size;
    size_t      buffer_used;

    /* Verification */
    void        *hash_ctx;              /* SHA256 context */

    struct qrnet_transfer *next;
} qrnet_transfer_t;

/* ==============================================================================
 * Peer Connection
 * ============================================================================== */

typedef enum {
    QRNET_PEER_DISCONNECTED = 0,
    QRNET_PEER_CONNECTING   = 1,
    QRNET_PEER_CONNECTED    = 2,
    QRNET_PEER_AUTHENTICATED = 3
} qrnet_peer_state_t;

typedef struct qrnet_peer {
    char        node_id[64];
    char        address[256];           /* IP:port or hostname */
    int         port;
    int         socket_fd;
    qrnet_peer_state_t state;

    /* Authentication */
    char        pubkey_hex[131];
    int         authenticated;
    uint32_t    governor_state;

    /* Statistics */
    uint64_t    bytes_sent;
    uint64_t    bytes_received;
    time_t      connected_at;
    time_t      last_seen;

    struct qrnet_peer *next;
} qrnet_peer_t;

/* ==============================================================================
 * Transport System
 * ============================================================================== */

typedef struct qrnet_transport {
    struct qrnet_system *qrnet;         /* Parent QRNet system */
    qrnet_content_store_t *store;       /* Content store */

    /* Network */
    int         listen_fd;              /* Listening socket */
    int         port;                   /* Listening port */
    qrnet_peer_t *peers;                /* Connected peers */
    uint32_t    peer_count;

    /* Transfers */
    qrnet_transfer_t *transfers;        /* Active transfers */
    uint32_t    transfer_count;
    uint32_t    next_transfer_id;

    /* Configuration */
    int         max_concurrent;         /* Max concurrent transfers */
    int         enable_tls;             /* Use TLS encryption */
    int         auto_announce;          /* Auto-announce new content */

    /* Statistics */
    uint64_t    total_bytes_sent;
    uint64_t    total_bytes_received;
    uint32_t    total_transfers;
    uint32_t    failed_transfers;

    int         running;
} qrnet_transport_t;

/* ==============================================================================
 * Result Codes
 * ============================================================================== */

typedef enum {
    QRNET_TRANSPORT_OK              = 0,
    QRNET_TRANSPORT_ERROR           = -1,
    QRNET_TRANSPORT_INVALID_PARAM   = -2,
    QRNET_TRANSPORT_NOT_FOUND       = -3,
    QRNET_TRANSPORT_HASH_MISMATCH   = -4,
    QRNET_TRANSPORT_TIMEOUT         = -5,
    QRNET_TRANSPORT_PEER_ERROR      = -6,
    QRNET_TRANSPORT_NETWORK_ERROR   = -7,
    QRNET_TRANSPORT_NO_PEERS        = -8,
    QRNET_TRANSPORT_STORE_FULL      = -9
} qrnet_transport_result_t;

/* ==============================================================================
 * Content Store API
 * ============================================================================== */

/* Initialize content store */
qrnet_transport_result_t qrnet_store_init(qrnet_content_store_t **store,
                                           const char *base_path,
                                           uint64_t max_size);

/* Store content (returns hash) */
qrnet_transport_result_t qrnet_store_put(qrnet_content_store_t *store,
                                          const void *data, size_t size,
                                          const char *original_name,
                                          const char *content_type,
                                          char *hash_out);

/* Store content from file */
qrnet_transport_result_t qrnet_store_put_file(qrnet_content_store_t *store,
                                               const char *filepath,
                                               char *hash_out);

/* Retrieve content by hash */
qrnet_transport_result_t qrnet_store_get(qrnet_content_store_t *store,
                                          const char *hash_hex,
                                          void **data_out, size_t *size_out);

/* Check if content exists */
int qrnet_store_has(qrnet_content_store_t *store, const char *hash_hex);

/* Get content entry metadata */
qrnet_content_entry_t *qrnet_store_lookup(qrnet_content_store_t *store,
                                           const char *hash_hex);

/* Pin content (prevent eviction) */
qrnet_transport_result_t qrnet_store_pin(qrnet_content_store_t *store,
                                          const char *hash_hex);

/* Cleanup store */
void qrnet_store_cleanup(qrnet_content_store_t *store);

/* ==============================================================================
 * Transport API
 * ============================================================================== */

/* Initialize transport system */
qrnet_transport_result_t qrnet_transport_init(qrnet_transport_t **transport,
                                               struct qrnet_system *qrnet,
                                               int port);

/* Start listening for connections */
qrnet_transport_result_t qrnet_transport_listen(qrnet_transport_t *transport);

/* Stop transport */
qrnet_transport_result_t qrnet_transport_stop(qrnet_transport_t *transport);

/* Add peer */
qrnet_transport_result_t qrnet_transport_add_peer(qrnet_transport_t *transport,
                                                   const char *address,
                                                   int port,
                                                   const char *node_id);

/* Connect to peer */
qrnet_transport_result_t qrnet_transport_connect(qrnet_transport_t *transport,
                                                  const char *address,
                                                  int port);

/* Cleanup transport */
void qrnet_transport_cleanup(qrnet_transport_t *transport);

/* ==============================================================================
 * Content Transfer API
 * ============================================================================== */

/* Publish content (store locally and optionally announce) */
qrnet_transport_result_t qrnet_publish_content(qrnet_transport_t *transport,
                                                const void *data, size_t size,
                                                const char *name,
                                                char *hash_out);

/* Publish file */
qrnet_transport_result_t qrnet_publish_file(qrnet_transport_t *transport,
                                             const char *filepath,
                                             char *hash_out);

/* Fetch content by hash (from any available peer) */
qrnet_transport_result_t qrnet_fetch_content(qrnet_transport_t *transport,
                                              const char *hash_hex,
                                              void **data_out,
                                              size_t *size_out);

/* Fetch content for QRNet code */
qrnet_transport_result_t qrnet_fetch_for_code(qrnet_transport_t *transport,
                                               struct qrnet_code *code,
                                               void **data_out,
                                               size_t *size_out);

/* Query peers for content availability */
qrnet_transport_result_t qrnet_query_content(qrnet_transport_t *transport,
                                              const char *hash_hex,
                                              char **peer_addresses,
                                              int *peer_count);

/* Announce content to peers */
qrnet_transport_result_t qrnet_announce_content(qrnet_transport_t *transport,
                                                 const char *hash_hex);

/* ==============================================================================
 * Transfer Management
 * ============================================================================== */

/* Get transfer by ID */
qrnet_transfer_t *qrnet_get_transfer(qrnet_transport_t *transport,
                                      uint32_t transfer_id);

/* Cancel transfer */
qrnet_transport_result_t qrnet_cancel_transfer(qrnet_transport_t *transport,
                                                uint32_t transfer_id);

/* Get transfer progress (0-100) */
int qrnet_transfer_progress(qrnet_transfer_t *transfer);

/* ==============================================================================
 * Utility Functions
 * ============================================================================== */

/* Compute SHA-256 hash of data */
void qrnet_hash_data(const void *data, size_t size,
                     uint8_t *hash_out, char *hex_out);

/* Verify content matches hash */
int qrnet_verify_content(const void *data, size_t size, const char *expected_hash);

#endif /* PHANTOM_QRNET_TRANSPORT_H */
