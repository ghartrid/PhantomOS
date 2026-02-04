/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM TLS LAYER
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * TLS/SSL support for PhantomOS network layer.
 *
 * Security Design Principles:
 * 1. Governor integration - CAP_NETWORK_SECURE capability for encrypted connections
 * 2. Metadata logging - connection info logged even though payload is encrypted
 * 3. Certificate validation - proper chain verification by default
 * 4. Minimal attack surface - only client-side TLS, limited cipher suites
 *
 * Uses mbedTLS for implementation (smaller, cleaner than OpenSSL).
 */

#ifndef PHANTOM_TLS_H
#define PHANTOM_TLS_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
struct phantom_net;
struct phantom_socket;

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────── */

#define PHANTOM_TLS_MAX_CONTEXTS    64
#define PHANTOM_TLS_CERT_PATH       "/geo/etc/ssl/certs"
#define PHANTOM_TLS_MAX_HOSTNAME    256
#define PHANTOM_TLS_BUFFER_SIZE     16384

/* TLS versions */
#define PHANTOM_TLS_VERSION_1_2     0x0303
#define PHANTOM_TLS_VERSION_1_3     0x0304
#define PHANTOM_TLS_VERSION_MIN     PHANTOM_TLS_VERSION_1_2  /* No TLS 1.0/1.1 */

/* ─────────────────────────────────────────────────────────────────────────────
 * TLS Result Codes
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PHANTOM_TLS_OK = 0,
    PHANTOM_TLS_ERROR = -1,
    PHANTOM_TLS_WANT_READ = -2,
    PHANTOM_TLS_WANT_WRITE = -3,
    PHANTOM_TLS_CERT_VERIFY_FAILED = -4,
    PHANTOM_TLS_HANDSHAKE_FAILED = -5,
    PHANTOM_TLS_NOT_INITIALIZED = -6,
    PHANTOM_TLS_ALREADY_CONNECTED = -7,
    PHANTOM_TLS_NO_MEMORY = -8,
    PHANTOM_TLS_INVALID_PARAM = -9,
    PHANTOM_TLS_GOVERNOR_DENIED = -10,
    PHANTOM_TLS_HOSTNAME_MISMATCH = -11,
    PHANTOM_TLS_EXPIRED_CERT = -12,
    PHANTOM_TLS_SELF_SIGNED = -13,
    PHANTOM_TLS_UNKNOWN_CA = -14,
} phantom_tls_result_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * TLS Verification Mode
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PHANTOM_TLS_VERIFY_REQUIRED = 0,  /* Full verification (default) */
    PHANTOM_TLS_VERIFY_OPTIONAL,       /* Verify but allow failures (logs warning) */
    PHANTOM_TLS_VERIFY_NONE            /* No verification (DANGEROUS - requires user approval) */
} phantom_tls_verify_mode_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * TLS Connection State
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PHANTOM_TLS_STATE_NONE = 0,
    PHANTOM_TLS_STATE_HANDSHAKING,
    PHANTOM_TLS_STATE_CONNECTED,
    PHANTOM_TLS_STATE_SHUTDOWN,
    PHANTOM_TLS_STATE_ERROR
} phantom_tls_state_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Certificate Information (for logging/display)
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_tls_cert_info {
    char subject[256];          /* Certificate subject (CN) */
    char issuer[256];           /* Certificate issuer */
    char serial[64];            /* Serial number (hex) */
    uint64_t not_before;        /* Validity start (unix timestamp) */
    uint64_t not_after;         /* Validity end (unix timestamp) */
    int key_bits;               /* Key size in bits */
    char fingerprint_sha256[65]; /* SHA-256 fingerprint */
    int is_ca;                  /* Is this a CA certificate? */
    int self_signed;            /* Is this self-signed? */
} phantom_tls_cert_info_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * TLS Session Information (for logging)
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_tls_session_info {
    uint16_t version;           /* TLS version negotiated */
    char cipher_suite[64];      /* Cipher suite name */
    int key_exchange_bits;      /* Key exchange strength */
    int cipher_bits;            /* Cipher strength */

    /* Certificate chain info */
    phantom_tls_cert_info_t peer_cert;
    int chain_depth;            /* Certificate chain depth */
    int verify_result;          /* Verification result code */

    /* Session statistics */
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t handshake_time_ms;

    /* Connection metadata (always logged) */
    char hostname[PHANTOM_TLS_MAX_HOSTNAME];
    uint16_t port;
    uint64_t connected_at;
} phantom_tls_session_info_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * TLS Context (per-connection)
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_tls_context {
    uint32_t id;                        /* Context ID */
    int socket_id;                      /* Associated phantom socket */
    phantom_tls_state_t state;

    /* Hostname for SNI and verification */
    char hostname[PHANTOM_TLS_MAX_HOSTNAME];

    /* Configuration */
    phantom_tls_verify_mode_t verify_mode;
    int min_version;                    /* Minimum TLS version */
    int max_version;                    /* Maximum TLS version */

    /* Session info */
    phantom_tls_session_info_t session;

    /* Internal mbedTLS handles (opaque to callers) */
    void *ssl;                          /* mbedtls_ssl_context */
    void *conf;                         /* mbedtls_ssl_config */
    void *cacert;                       /* CA certificate chain */
    void *ctr_drbg;                     /* Random number generator */
    void *entropy;                      /* Entropy source */

    /* Buffers */
    uint8_t *read_buf;
    size_t read_buf_len;
    uint8_t *write_buf;
    size_t write_buf_len;

    /* Error tracking */
    int last_error;
    char last_error_msg[256];

    /* Governor approval tracking */
    int governor_approved;
    uint64_t approval_timestamp;

    /* Network reference for I/O callbacks */
    struct phantom_net *net;

} phantom_tls_context_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * TLS Manager
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_tls {
    /* Contexts */
    phantom_tls_context_t contexts[PHANTOM_TLS_MAX_CONTEXTS];
    int context_count;
    uint32_t next_context_id;

    /* Global configuration */
    char ca_cert_path[256];             /* Path to CA certificates */
    phantom_tls_verify_mode_t default_verify_mode;
    int allow_insecure;                 /* Allow VERIFY_NONE (requires Governor) */

    /* Statistics (for logging/accountability) */
    uint64_t total_connections;
    uint64_t successful_handshakes;
    uint64_t failed_handshakes;
    uint64_t cert_verify_failures;
    uint64_t total_bytes_encrypted;
    uint64_t total_bytes_decrypted;

    /* References */
    struct phantom_net *net;
    struct phantom_governor *governor;

    /* State */
    int initialized;
    int ca_loaded;

    /* Loaded CA certificate chain (opaque mbedtls_x509_crt) */
    void *cacert;

} phantom_tls_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * TLS Manager API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Initialize TLS subsystem */
int phantom_tls_init(phantom_tls_t *tls, struct phantom_net *net);

/* Shutdown TLS subsystem */
void phantom_tls_shutdown(phantom_tls_t *tls);

/* Set Governor for capability checks */
void phantom_tls_set_governor(phantom_tls_t *tls, struct phantom_governor *gov);

/* Load CA certificates from path */
int phantom_tls_load_ca_certs(phantom_tls_t *tls, const char *path);

/* Set default verification mode */
void phantom_tls_set_verify_mode(phantom_tls_t *tls, phantom_tls_verify_mode_t mode);

/* ─────────────────────────────────────────────────────────────────────────────
 * TLS Connection API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Create TLS context for a socket */
int phantom_tls_create(phantom_tls_t *tls, int socket_id, const char *hostname);

/* Perform TLS handshake (call after TCP connect) */
int phantom_tls_handshake(phantom_tls_t *tls, int ctx_id);

/* Send data over TLS connection */
ssize_t phantom_tls_send(phantom_tls_t *tls, int ctx_id,
                         const void *data, size_t len);

/* Receive data over TLS connection */
ssize_t phantom_tls_recv(phantom_tls_t *tls, int ctx_id,
                         void *buffer, size_t len);

/* Graceful TLS shutdown */
int phantom_tls_close(phantom_tls_t *tls, int ctx_id);

/* ─────────────────────────────────────────────────────────────────────────────
 * TLS Context Configuration
 * ───────────────────────────────────────────────────────────────────────────── */

/* Set verification mode for specific context */
int phantom_tls_ctx_set_verify(phantom_tls_t *tls, int ctx_id,
                                phantom_tls_verify_mode_t mode);

/* Set minimum TLS version for context */
int phantom_tls_ctx_set_min_version(phantom_tls_t *tls, int ctx_id, int version);

/* ─────────────────────────────────────────────────────────────────────────────
 * TLS Information API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Get TLS context by ID */
phantom_tls_context_t *phantom_tls_get_context(phantom_tls_t *tls, int ctx_id);

/* Get context ID for socket */
int phantom_tls_get_ctx_for_socket(phantom_tls_t *tls, int socket_id);

/* Get session information */
int phantom_tls_get_session_info(phantom_tls_t *tls, int ctx_id,
                                  phantom_tls_session_info_t *info);

/* Get peer certificate info */
int phantom_tls_get_peer_cert(phantom_tls_t *tls, int ctx_id,
                               phantom_tls_cert_info_t *info);

/* Get TLS state */
phantom_tls_state_t phantom_tls_get_state(phantom_tls_t *tls, int ctx_id);

/* Get error string */
const char *phantom_tls_error_string(phantom_tls_result_t result);

/* ─────────────────────────────────────────────────────────────────────────────
 * High-Level Convenience API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Connect with TLS (TCP connect + TLS handshake) */
int phantom_tls_connect(phantom_tls_t *tls, struct phantom_net *net,
                        const char *host, uint16_t port);

/* HTTPS GET request */
ssize_t phantom_https_get(phantom_tls_t *tls, struct phantom_net *net,
                          const char *url, char *response, size_t max_len);

/* HTTPS POST request */
ssize_t phantom_https_post(phantom_tls_t *tls, struct phantom_net *net,
                           const char *url, const char *body, size_t body_len,
                           char *response, size_t max_len);

/* ─────────────────────────────────────────────────────────────────────────────
 * Statistics and Logging
 * ───────────────────────────────────────────────────────────────────────────── */

/* Print TLS statistics */
void phantom_tls_print_stats(phantom_tls_t *tls);

/* Print certificate info */
void phantom_tls_print_cert(const phantom_tls_cert_info_t *cert);

/* Print session info */
void phantom_tls_print_session(const phantom_tls_session_info_t *session);

/* ─────────────────────────────────────────────────────────────────────────────
 * Version Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

/* Get TLS version string */
const char *phantom_tls_version_string(int version);

/* Check if TLS is available (mbedTLS linked) */
int phantom_tls_available(void);

#endif /* PHANTOM_TLS_H */
