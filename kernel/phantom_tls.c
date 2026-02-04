/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM TLS LAYER
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Implementation of TLS support for PhantomOS.
 *
 * This implementation can work in two modes:
 * 1. With mbedTLS linked - full TLS functionality
 * 2. Without mbedTLS - stub implementation with clear error messages
 *
 * Security features:
 * - Governor integration (CAP_NETWORK_SECURE capability)
 * - Certificate validation with proper hostname checking
 * - Minimum TLS 1.2 (no legacy protocols)
 * - Connection metadata logging (even for encrypted traffic)
 * - User prompt for insecure operations (self-signed certs, etc.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#include "phantom_tls.h"

/* Safe port parsing to prevent integer overflow - strict: no trailing chars */
static int safe_parse_port(const char *str, uint16_t *out) {
    if (!str || !out) return -1;
    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);
    if (errno != 0 || endptr == str || *endptr != '\0') return -1;
    if (val < 1 || val > 65535) return -1;
    *out = (uint16_t)val;
    return 0;
}
#include "phantom_net.h"
#include "phantom.h"
#include "governor.h"

/* Check if mbedTLS is available */
#ifdef HAVE_MBEDTLS
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#define TLS_AVAILABLE 1
#else
#define TLS_AVAILABLE 0
#endif

/* ══════════════════════════════════════════════════════════════════════════════
 * INTERNAL HELPERS
 * ══════════════════════════════════════════════════════════════════════════════ */

static uint64_t tls_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

static phantom_tls_context_t *find_context(phantom_tls_t *tls, int ctx_id) {
    for (int i = 0; i < tls->context_count; i++) {
        if (tls->contexts[i].id == (uint32_t)ctx_id) {
            return &tls->contexts[i];
        }
    }
    return NULL;
}

static phantom_tls_context_t *find_context_by_socket(phantom_tls_t *tls, int socket_id) {
    for (int i = 0; i < tls->context_count; i++) {
        if (tls->contexts[i].socket_id == socket_id) {
            return &tls->contexts[i];
        }
    }
    return NULL;
}

/* Check Governor capability for secure network access */
static int check_tls_capability(phantom_tls_t *tls, const char *operation,
                                const char *hostname, int insecure) {
    if (!tls->governor) {
        return 1;  /* No governor, allow */
    }

    /* Build operation description */
    char code[512];
    if (insecure) {
        snprintf(code, sizeof(code),
                 "tls_%s(\"%s\", insecure=true)", operation, hostname);
    } else {
        snprintf(code, sizeof(code),
                 "tls_%s(\"%s\")", operation, hostname);
    }

    governor_eval_request_t req = {0};
    governor_eval_response_t resp = {0};

    req.code_ptr = code;
    req.code_size = strlen(code);
    req.declared_caps = CAP_NETWORK;  /* TLS requires network capability */
    strncpy(req.name, "TLS Connection", sizeof(req.name) - 1);
    snprintf(req.description, sizeof(req.description),
             "TLS %s connection to %s%s",
             operation, hostname, insecure ? " (INSECURE)" : "");

    int err = governor_evaluate_code(tls->governor, &req, &resp);
    if (err != 0 || resp.decision != GOVERNOR_APPROVE) {
        printf("[phantom_tls] Governor denied TLS operation: %s\n", operation);
        printf("              Reason: %s\n", resp.decline_reason);
        return 0;
    }

    return 1;
}

/* Log TLS connection metadata to geology */
static void log_tls_connection(phantom_tls_t *tls, phantom_tls_context_t *ctx,
                               const char *event) {
    printf("[phantom_tls] %s: ctx=%u socket=%d host=%s\n",
           event, ctx->id, ctx->socket_id, ctx->hostname);

    if (ctx->state == PHANTOM_TLS_STATE_CONNECTED) {
        printf("              Cipher: %s, Version: %s\n",
               ctx->session.cipher_suite,
               phantom_tls_version_string(ctx->session.version));
        printf("              Peer: %s\n", ctx->session.peer_cert.subject);
    }

    /* In a full implementation, this would write to GeoFS for audit trail */
}

/* ══════════════════════════════════════════════════════════════════════════════
 * TLS MANAGER LIFECYCLE
 * ══════════════════════════════════════════════════════════════════════════════ */

int phantom_tls_init(phantom_tls_t *tls, struct phantom_net *net) {
    if (!tls) return PHANTOM_TLS_INVALID_PARAM;

    memset(tls, 0, sizeof(phantom_tls_t));

    tls->net = net;
    tls->next_context_id = 1;
    tls->default_verify_mode = PHANTOM_TLS_VERIFY_REQUIRED;
    tls->allow_insecure = 0;  /* Require Governor approval for insecure */
    strncpy(tls->ca_cert_path, PHANTOM_TLS_CERT_PATH, sizeof(tls->ca_cert_path) - 1);

#if TLS_AVAILABLE
    printf("[phantom_tls] TLS subsystem initialized (mbedTLS)\n");
    printf("              Minimum version: TLS 1.2\n");
    printf("              Verification: REQUIRED (default)\n");
    tls->initialized = 1;

    /* Auto-load system CA certificates */
    /* Try standard Linux locations in order of preference */
    const char *ca_paths[] = {
        "/etc/ssl/certs/ca-certificates.crt",  /* Debian/Ubuntu bundle */
        "/etc/pki/tls/certs/ca-bundle.crt",    /* Fedora/RHEL bundle */
        "/etc/ssl/ca-bundle.pem",              /* OpenSUSE bundle */
        "/etc/ssl/certs",                       /* Debian/Ubuntu directory */
        "/etc/pki/tls/certs",                   /* Fedora/RHEL directory */
        NULL
    };

    for (int i = 0; ca_paths[i] != NULL; i++) {
        if (phantom_tls_load_ca_certs(tls, ca_paths[i]) == PHANTOM_TLS_OK) {
            printf("[phantom_tls] Loaded CA certificates from: %s\n", ca_paths[i]);
            break;
        }
    }

    if (!tls->ca_loaded) {
        printf("[phantom_tls] WARNING: No CA certificates loaded!\n");
        printf("              HTTPS connections will fail certificate verification.\n");
        printf("              Install ca-certificates package or set path manually.\n");
    }

    return PHANTOM_TLS_OK;
#else
    printf("[phantom_tls] TLS subsystem initialized (STUB MODE)\n");
    printf("              mbedTLS not linked - TLS connections will fail\n");
    printf("              To enable TLS, install mbedtls-dev and rebuild with -DHAVE_MBEDTLS\n");
    tls->initialized = 1;
    return PHANTOM_TLS_OK;
#endif
}

void phantom_tls_shutdown(phantom_tls_t *tls) {
    if (!tls || !tls->initialized) return;

    printf("\n[phantom_tls] Shutdown statistics:\n");
    printf("              Total connections:     %lu\n", tls->total_connections);
    printf("              Successful handshakes: %lu\n", tls->successful_handshakes);
    printf("              Failed handshakes:     %lu\n", tls->failed_handshakes);
    printf("              Cert verify failures:  %lu\n", tls->cert_verify_failures);
    printf("              Bytes encrypted:       %lu\n", tls->total_bytes_encrypted);
    printf("              Bytes decrypted:       %lu\n", tls->total_bytes_decrypted);

    /* Clean up all contexts */
    for (int i = 0; i < tls->context_count; i++) {
        phantom_tls_context_t *ctx = &tls->contexts[i];
        if (ctx->state != PHANTOM_TLS_STATE_NONE) {
            phantom_tls_close(tls, ctx->id);
        }

#if TLS_AVAILABLE
        /* Free mbedTLS resources */
        if (ctx->ssl) {
            mbedtls_ssl_free(ctx->ssl);
            free(ctx->ssl);
        }
        if (ctx->conf) {
            mbedtls_ssl_config_free(ctx->conf);
            free(ctx->conf);
        }
        if (ctx->ctr_drbg) {
            mbedtls_ctr_drbg_free(ctx->ctr_drbg);
            free(ctx->ctr_drbg);
        }
        if (ctx->entropy) {
            mbedtls_entropy_free(ctx->entropy);
            free(ctx->entropy);
        }
#endif
        free(ctx->read_buf);
        free(ctx->write_buf);
    }

#if TLS_AVAILABLE
    /* Free global CA certificate chain */
    if (tls->cacert) {
        mbedtls_x509_crt_free(tls->cacert);
        free(tls->cacert);
        tls->cacert = NULL;
    }
#endif

    tls->initialized = 0;
}

void phantom_tls_set_governor(phantom_tls_t *tls, struct phantom_governor *gov) {
    if (tls) {
        tls->governor = gov;
        printf("[phantom_tls] Governor integration enabled\n");
    }
}

int phantom_tls_load_ca_certs(phantom_tls_t *tls, const char *path) {
    if (!tls || !path) return PHANTOM_TLS_INVALID_PARAM;

#if TLS_AVAILABLE
    /* Security fix: Actually load CA certificates into mbedTLS */
    if (!tls->cacert) {
        tls->cacert = malloc(sizeof(mbedtls_x509_crt));
        if (!tls->cacert) {
            return PHANTOM_TLS_NO_MEMORY;
        }
        mbedtls_x509_crt_init(tls->cacert);
    }

    /* Load CA certificates from path (file or directory) */
    int ret = mbedtls_x509_crt_parse_path(tls->cacert, path);
    if (ret < 0) {
        /* Try as single file if directory parse failed */
        ret = mbedtls_x509_crt_parse_file(tls->cacert, path);
        if (ret < 0) {
            char error_buf[100];
            mbedtls_strerror(ret, error_buf, sizeof(error_buf));
            printf("[phantom_tls] Failed to load CA certs from %s: %s\n", path, error_buf);
            return PHANTOM_TLS_ERROR;
        }
    }

    strncpy(tls->ca_cert_path, path, sizeof(tls->ca_cert_path) - 1);
    tls->ca_cert_path[sizeof(tls->ca_cert_path) - 1] = '\0';
    tls->ca_loaded = 1;
    printf("[phantom_tls] CA certificates loaded from: %s (%d certs parsed)\n", path, ret >= 0 ? ret : 1);
    return PHANTOM_TLS_OK;
#else
    printf("[phantom_tls] Cannot load CA certs - mbedTLS not available\n");
    return PHANTOM_TLS_NOT_INITIALIZED;
#endif
}

void phantom_tls_set_verify_mode(phantom_tls_t *tls, phantom_tls_verify_mode_t mode) {
    if (!tls) return;

    if (mode == PHANTOM_TLS_VERIFY_NONE && !tls->allow_insecure) {
        printf("[phantom_tls] WARNING: VERIFY_NONE requires allow_insecure=true\n");
        return;
    }

    tls->default_verify_mode = mode;
    printf("[phantom_tls] Default verify mode: %s\n",
           mode == PHANTOM_TLS_VERIFY_REQUIRED ? "REQUIRED" :
           mode == PHANTOM_TLS_VERIFY_OPTIONAL ? "OPTIONAL" : "NONE (INSECURE)");
}

/* ══════════════════════════════════════════════════════════════════════════════
 * TLS CONNECTION API
 * ══════════════════════════════════════════════════════════════════════════════ */

int phantom_tls_create(phantom_tls_t *tls, int socket_id, const char *hostname) {
    if (!tls || !tls->initialized || !hostname) {
        return PHANTOM_TLS_INVALID_PARAM;
    }

#if !TLS_AVAILABLE
    printf("[phantom_tls] Cannot create TLS context - mbedTLS not linked\n");
    return PHANTOM_TLS_NOT_INITIALIZED;
#endif

    if (tls->context_count >= PHANTOM_TLS_MAX_CONTEXTS) {
        printf("[phantom_tls] Maximum TLS contexts reached\n");
        return PHANTOM_TLS_NO_MEMORY;
    }

    /* Check Governor capability */
    if (!check_tls_capability(tls, "connect", hostname, 0)) {
        return PHANTOM_TLS_GOVERNOR_DENIED;
    }

    /* Allocate context */
    phantom_tls_context_t *ctx = &tls->contexts[tls->context_count];
    memset(ctx, 0, sizeof(phantom_tls_context_t));

    /* Security fix: Detect context ID collision (wraps after 4 billion) */
    uint32_t new_id = tls->next_context_id++;
    if (tls->next_context_id == 0) {
        /* ID wrapped around - find next unused ID */
        tls->next_context_id = 1;
        for (int i = 0; i < tls->context_count; i++) {
            if (tls->contexts[i].id >= tls->next_context_id) {
                tls->next_context_id = tls->contexts[i].id + 1;
            }
        }
    }

    ctx->id = new_id;
    ctx->socket_id = socket_id;
    ctx->state = PHANTOM_TLS_STATE_NONE;
    ctx->verify_mode = tls->default_verify_mode;
    ctx->min_version = PHANTOM_TLS_VERSION_MIN;
    ctx->max_version = PHANTOM_TLS_VERSION_1_3;
    strncpy(ctx->hostname, hostname, PHANTOM_TLS_MAX_HOSTNAME - 1);
    ctx->hostname[PHANTOM_TLS_MAX_HOSTNAME - 1] = '\0';  /* Ensure null termination */

    /* Security fix: Store net reference for I/O callbacks */
    ctx->net = tls->net;

    /* Allocate buffers */
    ctx->read_buf = malloc(PHANTOM_TLS_BUFFER_SIZE);
    ctx->write_buf = malloc(PHANTOM_TLS_BUFFER_SIZE);
    if (!ctx->read_buf || !ctx->write_buf) {
        free(ctx->read_buf);
        free(ctx->write_buf);
        return PHANTOM_TLS_NO_MEMORY;
    }

#if TLS_AVAILABLE
    /* Initialize mbedTLS structures */
    ctx->ssl = malloc(sizeof(mbedtls_ssl_context));
    ctx->conf = malloc(sizeof(mbedtls_ssl_config));
    ctx->ctr_drbg = malloc(sizeof(mbedtls_ctr_drbg_context));
    ctx->entropy = malloc(sizeof(mbedtls_entropy_context));

    if (!ctx->ssl || !ctx->conf || !ctx->ctr_drbg || !ctx->entropy) {
        free(ctx->ssl);
        free(ctx->conf);
        free(ctx->ctr_drbg);
        free(ctx->entropy);
        free(ctx->read_buf);
        free(ctx->write_buf);
        return PHANTOM_TLS_NO_MEMORY;
    }

    mbedtls_ssl_init(ctx->ssl);
    mbedtls_ssl_config_init(ctx->conf);
    mbedtls_ctr_drbg_init(ctx->ctr_drbg);
    mbedtls_entropy_init(ctx->entropy);

    /* Seed random number generator */
    const char *pers = "phantom_tls";
    int ret = mbedtls_ctr_drbg_seed(ctx->ctr_drbg, mbedtls_entropy_func,
                                     ctx->entropy, (const unsigned char *)pers,
                                     strlen(pers));
    if (ret != 0) {
        snprintf(ctx->last_error_msg, sizeof(ctx->last_error_msg),
                 "Failed to seed RNG: -0x%04x", -ret);
        ctx->last_error = ret;
        return PHANTOM_TLS_ERROR;
    }

    /* Set up SSL config */
    ret = mbedtls_ssl_config_defaults(ctx->conf,
                                       MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        snprintf(ctx->last_error_msg, sizeof(ctx->last_error_msg),
                 "Failed to set SSL defaults: -0x%04x", -ret);
        return PHANTOM_TLS_ERROR;
    }

    /* Set minimum TLS version (1.2+) */
    mbedtls_ssl_conf_min_version(ctx->conf,
                                  MBEDTLS_SSL_MAJOR_VERSION_3,
                                  MBEDTLS_SSL_MINOR_VERSION_3);

    /* Set verification mode */
    int authmode = MBEDTLS_SSL_VERIFY_REQUIRED;
    if (ctx->verify_mode == PHANTOM_TLS_VERIFY_OPTIONAL) {
        authmode = MBEDTLS_SSL_VERIFY_OPTIONAL;
    } else if (ctx->verify_mode == PHANTOM_TLS_VERIFY_NONE) {
        authmode = MBEDTLS_SSL_VERIFY_NONE;
    }
    mbedtls_ssl_conf_authmode(ctx->conf, authmode);

    /* Security fix: Configure CA certificate chain for verification */
    if (tls->ca_loaded && tls->cacert) {
        mbedtls_ssl_conf_ca_chain(ctx->conf, (mbedtls_x509_crt *)tls->cacert, NULL);
    } else if (authmode != MBEDTLS_SSL_VERIFY_NONE) {
        printf("[phantom_tls] WARNING: No CA certificates loaded, verification may fail\n");
    }

    mbedtls_ssl_conf_rng(ctx->conf, mbedtls_ctr_drbg_random, ctx->ctr_drbg);

    ret = mbedtls_ssl_setup(ctx->ssl, ctx->conf);
    if (ret != 0) {
        snprintf(ctx->last_error_msg, sizeof(ctx->last_error_msg),
                 "Failed to setup SSL: -0x%04x", -ret);
        return PHANTOM_TLS_ERROR;
    }

    /* Set hostname for SNI and verification */
    ret = mbedtls_ssl_set_hostname(ctx->ssl, hostname);
    if (ret != 0) {
        snprintf(ctx->last_error_msg, sizeof(ctx->last_error_msg),
                 "Failed to set hostname: -0x%04x", -ret);
        return PHANTOM_TLS_ERROR;
    }
#endif

    ctx->governor_approved = 1;
    ctx->approval_timestamp = time(NULL);

    tls->context_count++;
    tls->total_connections++;

    log_tls_connection(tls, ctx, "TLS context created");

    return ctx->id;
}

/* I/O callbacks for mbedTLS */
#if TLS_AVAILABLE
static int tls_send_callback(void *ctx, const unsigned char *buf, size_t len) {
    phantom_tls_context_t *tls_ctx = (phantom_tls_context_t *)ctx;

    /* Security fix: Use the net reference stored in context instead of NULL */
    if (!tls_ctx || !tls_ctx->net) {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }

    phantom_socket_t *sock = phantom_socket_get(tls_ctx->net, tls_ctx->socket_id);
    if (!sock || sock->fd < 0) {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }

    ssize_t sent = send(sock->fd, buf, len, 0);
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }

    return (int)sent;
}

static int tls_recv_callback(void *ctx, unsigned char *buf, size_t len) {
    phantom_tls_context_t *tls_ctx = (phantom_tls_context_t *)ctx;

    /* Security fix: Use the net reference stored in context instead of NULL */
    if (!tls_ctx || !tls_ctx->net) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }

    phantom_socket_t *sock = phantom_socket_get(tls_ctx->net, tls_ctx->socket_id);
    if (!sock || sock->fd < 0) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }

    ssize_t received = recv(sock->fd, buf, len, 0);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }

    if (received == 0) {
        return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    }

    return (int)received;
}
#endif

int phantom_tls_handshake(phantom_tls_t *tls, int ctx_id) {
    if (!tls || !tls->initialized) return PHANTOM_TLS_NOT_INITIALIZED;

    phantom_tls_context_t *ctx = find_context(tls, ctx_id);
    if (!ctx) return PHANTOM_TLS_INVALID_PARAM;

    if (ctx->state == PHANTOM_TLS_STATE_CONNECTED) {
        return PHANTOM_TLS_ALREADY_CONNECTED;
    }

#if !TLS_AVAILABLE
    printf("[phantom_tls] Cannot perform handshake - mbedTLS not linked\n");
    ctx->state = PHANTOM_TLS_STATE_ERROR;
    snprintf(ctx->last_error_msg, sizeof(ctx->last_error_msg),
             "TLS not available - mbedTLS not linked");
    tls->failed_handshakes++;
    return PHANTOM_TLS_NOT_INITIALIZED;
#else
    ctx->state = PHANTOM_TLS_STATE_HANDSHAKING;
    uint64_t start_time = tls_time_ms();

    /* Set I/O callbacks */
    mbedtls_ssl_set_bio(ctx->ssl, ctx, tls_send_callback, tls_recv_callback, NULL);

    /* Perform handshake */
    int ret;
    while ((ret = mbedtls_ssl_handshake(ctx->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char error_buf[100];
            mbedtls_strerror(ret, error_buf, sizeof(error_buf));
            snprintf(ctx->last_error_msg, sizeof(ctx->last_error_msg),
                     "Handshake failed: %s (-0x%04x)", error_buf, -ret);
            ctx->last_error = ret;
            ctx->state = PHANTOM_TLS_STATE_ERROR;
            tls->failed_handshakes++;

            log_tls_connection(tls, ctx, "TLS handshake FAILED");
            return PHANTOM_TLS_HANDSHAKE_FAILED;
        }
    }

    /* Verify certificate */
    uint32_t flags = mbedtls_ssl_get_verify_result(ctx->ssl);
    if (flags != 0 && ctx->verify_mode == PHANTOM_TLS_VERIFY_REQUIRED) {
        char verify_buf[512];
        mbedtls_x509_crt_verify_info(verify_buf, sizeof(verify_buf), "  ! ", flags);
        snprintf(ctx->last_error_msg, sizeof(ctx->last_error_msg),
                 "Certificate verification failed:\n%s", verify_buf);

        tls->cert_verify_failures++;
        ctx->state = PHANTOM_TLS_STATE_ERROR;

        log_tls_connection(tls, ctx, "TLS cert verify FAILED");

        if (flags & MBEDTLS_X509_BADCERT_EXPIRED) {
            return PHANTOM_TLS_EXPIRED_CERT;
        } else if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
            return PHANTOM_TLS_UNKNOWN_CA;
        } else if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) {
            return PHANTOM_TLS_HOSTNAME_MISMATCH;
        }
        return PHANTOM_TLS_CERT_VERIFY_FAILED;
    }

    /* Record session info */
    /* Get TLS version - mbedtls_ssl_get_version returns string like "TLSv1.2" */
    const char *ver_str = mbedtls_ssl_get_version(ctx->ssl);
    if (ver_str) {
        if (strstr(ver_str, "1.3")) ctx->session.version = 0x0304;
        else if (strstr(ver_str, "1.2")) ctx->session.version = 0x0303;
        else if (strstr(ver_str, "1.1")) ctx->session.version = 0x0302;
        else if (strstr(ver_str, "1.0")) ctx->session.version = 0x0301;
        else ctx->session.version = 0x0300; /* SSL 3.0 */
    }
    strncpy(ctx->session.cipher_suite,
            mbedtls_ssl_get_ciphersuite(ctx->ssl),
            sizeof(ctx->session.cipher_suite) - 1);
    ctx->session.handshake_time_ms = tls_time_ms() - start_time;
    ctx->session.connected_at = time(NULL);
    strncpy(ctx->session.hostname, ctx->hostname, PHANTOM_TLS_MAX_HOSTNAME - 1);

    /* Get peer certificate info */
    const mbedtls_x509_crt *peer_cert = mbedtls_ssl_get_peer_cert(ctx->ssl);
    if (peer_cert) {
        char buf[256];
        mbedtls_x509_dn_gets(buf, sizeof(buf), &peer_cert->subject);
        strncpy(ctx->session.peer_cert.subject, buf, sizeof(ctx->session.peer_cert.subject) - 1);

        mbedtls_x509_dn_gets(buf, sizeof(buf), &peer_cert->issuer);
        strncpy(ctx->session.peer_cert.issuer, buf, sizeof(ctx->session.peer_cert.issuer) - 1);

        ctx->session.peer_cert.not_before = peer_cert->valid_from.year * 10000 +
                                             peer_cert->valid_from.mon * 100 +
                                             peer_cert->valid_from.day;
        ctx->session.peer_cert.not_after = peer_cert->valid_to.year * 10000 +
                                            peer_cert->valid_to.mon * 100 +
                                            peer_cert->valid_to.day;
    }

    ctx->state = PHANTOM_TLS_STATE_CONNECTED;
    tls->successful_handshakes++;

    log_tls_connection(tls, ctx, "TLS handshake SUCCESS");

    return PHANTOM_TLS_OK;
#endif
}

ssize_t phantom_tls_send(phantom_tls_t *tls, int ctx_id,
                         const void *data, size_t len) {
    if (!tls || !tls->initialized || !data) return PHANTOM_TLS_INVALID_PARAM;

    phantom_tls_context_t *ctx = find_context(tls, ctx_id);
    if (!ctx) return PHANTOM_TLS_INVALID_PARAM;

    if (ctx->state != PHANTOM_TLS_STATE_CONNECTED) {
        return PHANTOM_TLS_NOT_INITIALIZED;
    }

#if !TLS_AVAILABLE
    return PHANTOM_TLS_NOT_INITIALIZED;
#else
    int ret = mbedtls_ssl_write(ctx->ssl, data, len);
    if (ret < 0) {
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return PHANTOM_TLS_WANT_WRITE;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            return PHANTOM_TLS_WANT_READ;
        }
        return PHANTOM_TLS_ERROR;
    }

    ctx->session.bytes_sent += ret;
    tls->total_bytes_encrypted += ret;

    return ret;
#endif
}

ssize_t phantom_tls_recv(phantom_tls_t *tls, int ctx_id,
                         void *buffer, size_t len) {
    if (!tls || !tls->initialized || !buffer) return PHANTOM_TLS_INVALID_PARAM;

    phantom_tls_context_t *ctx = find_context(tls, ctx_id);
    if (!ctx) return PHANTOM_TLS_INVALID_PARAM;

    if (ctx->state != PHANTOM_TLS_STATE_CONNECTED) {
        return PHANTOM_TLS_NOT_INITIALIZED;
    }

#if !TLS_AVAILABLE
    return PHANTOM_TLS_NOT_INITIALIZED;
#else
    int ret = mbedtls_ssl_read(ctx->ssl, buffer, len);
    if (ret < 0) {
        if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            return PHANTOM_TLS_WANT_READ;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return PHANTOM_TLS_WANT_WRITE;
        }
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            return 0;  /* Clean shutdown */
        }
        return PHANTOM_TLS_ERROR;
    }

    ctx->session.bytes_received += ret;
    tls->total_bytes_decrypted += ret;

    return ret;
#endif
}

int phantom_tls_close(phantom_tls_t *tls, int ctx_id) {
    if (!tls || !tls->initialized) return PHANTOM_TLS_NOT_INITIALIZED;

    phantom_tls_context_t *ctx = find_context(tls, ctx_id);
    if (!ctx) return PHANTOM_TLS_INVALID_PARAM;

    if (ctx->state == PHANTOM_TLS_STATE_NONE) {
        return PHANTOM_TLS_OK;  /* Already closed */
    }

#if TLS_AVAILABLE
    if (ctx->state == PHANTOM_TLS_STATE_CONNECTED) {
        /* Send close_notify */
        mbedtls_ssl_close_notify(ctx->ssl);
    }
#endif

    ctx->state = PHANTOM_TLS_STATE_SHUTDOWN;
    log_tls_connection(tls, ctx, "TLS connection closed");

    return PHANTOM_TLS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CONTEXT CONFIGURATION
 * ══════════════════════════════════════════════════════════════════════════════ */

int phantom_tls_ctx_set_verify(phantom_tls_t *tls, int ctx_id,
                                phantom_tls_verify_mode_t mode) {
    if (!tls) return PHANTOM_TLS_INVALID_PARAM;

    phantom_tls_context_t *ctx = find_context(tls, ctx_id);
    if (!ctx) return PHANTOM_TLS_INVALID_PARAM;

    /* Require Governor approval for insecure mode */
    if (mode == PHANTOM_TLS_VERIFY_NONE) {
        if (!check_tls_capability(tls, "insecure_connect", ctx->hostname, 1)) {
            printf("[phantom_tls] VERIFY_NONE requires Governor approval\n");
            return PHANTOM_TLS_GOVERNOR_DENIED;
        }
        printf("[phantom_tls] WARNING: Certificate verification disabled for %s\n",
               ctx->hostname);
    }

    ctx->verify_mode = mode;
    return PHANTOM_TLS_OK;
}

int phantom_tls_ctx_set_min_version(phantom_tls_t *tls, int ctx_id, int version) {
    if (!tls) return PHANTOM_TLS_INVALID_PARAM;

    phantom_tls_context_t *ctx = find_context(tls, ctx_id);
    if (!ctx) return PHANTOM_TLS_INVALID_PARAM;

    /* Enforce minimum TLS 1.2 */
    if (version < PHANTOM_TLS_VERSION_1_2) {
        printf("[phantom_tls] Security policy: minimum TLS 1.2 required\n");
        version = PHANTOM_TLS_VERSION_1_2;
    }

    ctx->min_version = version;
    return PHANTOM_TLS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * INFORMATION API
 * ══════════════════════════════════════════════════════════════════════════════ */

phantom_tls_context_t *phantom_tls_get_context(phantom_tls_t *tls, int ctx_id) {
    return tls ? find_context(tls, ctx_id) : NULL;
}

int phantom_tls_get_ctx_for_socket(phantom_tls_t *tls, int socket_id) {
    phantom_tls_context_t *ctx = find_context_by_socket(tls, socket_id);
    return ctx ? (int)ctx->id : -1;
}

int phantom_tls_get_session_info(phantom_tls_t *tls, int ctx_id,
                                  phantom_tls_session_info_t *info) {
    if (!tls || !info) return PHANTOM_TLS_INVALID_PARAM;

    phantom_tls_context_t *ctx = find_context(tls, ctx_id);
    if (!ctx) return PHANTOM_TLS_INVALID_PARAM;

    *info = ctx->session;
    return PHANTOM_TLS_OK;
}

int phantom_tls_get_peer_cert(phantom_tls_t *tls, int ctx_id,
                               phantom_tls_cert_info_t *info) {
    if (!tls || !info) return PHANTOM_TLS_INVALID_PARAM;

    phantom_tls_context_t *ctx = find_context(tls, ctx_id);
    if (!ctx) return PHANTOM_TLS_INVALID_PARAM;

    *info = ctx->session.peer_cert;
    return PHANTOM_TLS_OK;
}

phantom_tls_state_t phantom_tls_get_state(phantom_tls_t *tls, int ctx_id) {
    phantom_tls_context_t *ctx = find_context(tls, ctx_id);
    return ctx ? ctx->state : PHANTOM_TLS_STATE_NONE;
}

const char *phantom_tls_error_string(phantom_tls_result_t result) {
    switch (result) {
        case PHANTOM_TLS_OK:                 return "Success";
        case PHANTOM_TLS_ERROR:              return "Generic error";
        case PHANTOM_TLS_WANT_READ:          return "Want read";
        case PHANTOM_TLS_WANT_WRITE:         return "Want write";
        case PHANTOM_TLS_CERT_VERIFY_FAILED: return "Certificate verification failed";
        case PHANTOM_TLS_HANDSHAKE_FAILED:   return "Handshake failed";
        case PHANTOM_TLS_NOT_INITIALIZED:    return "TLS not initialized";
        case PHANTOM_TLS_ALREADY_CONNECTED:  return "Already connected";
        case PHANTOM_TLS_NO_MEMORY:          return "Out of memory";
        case PHANTOM_TLS_INVALID_PARAM:      return "Invalid parameter";
        case PHANTOM_TLS_GOVERNOR_DENIED:    return "Governor denied operation";
        case PHANTOM_TLS_HOSTNAME_MISMATCH:  return "Hostname mismatch";
        case PHANTOM_TLS_EXPIRED_CERT:       return "Certificate expired";
        case PHANTOM_TLS_SELF_SIGNED:        return "Self-signed certificate";
        case PHANTOM_TLS_UNKNOWN_CA:         return "Unknown certificate authority";
        default:                             return "Unknown error";
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * HIGH-LEVEL CONVENIENCE API
 * ══════════════════════════════════════════════════════════════════════════════ */

int phantom_tls_connect(phantom_tls_t *tls, struct phantom_net *net,
                        const char *host, uint16_t port) {
    if (!tls || !net || !host) return PHANTOM_TLS_INVALID_PARAM;

    /* First, establish TCP connection */
    int sock_id = phantom_tcp_connect(net, host, port);
    if (sock_id < 0) {
        printf("[phantom_tls] TCP connection to %s:%u failed\n", host, port);
        return sock_id;
    }

    /* Create TLS context */
    int ctx_id = phantom_tls_create(tls, sock_id, host);
    if (ctx_id < 0) {
        phantom_socket_make_dormant(net, sock_id);
        return ctx_id;
    }

    /* Perform TLS handshake */
    int result = phantom_tls_handshake(tls, ctx_id);
    if (result != PHANTOM_TLS_OK) {
        phantom_tls_close(tls, ctx_id);
        phantom_socket_make_dormant(net, sock_id);
        return result;
    }

    return ctx_id;
}

ssize_t phantom_https_get(phantom_tls_t *tls, struct phantom_net *net,
                          const char *url, char *response, size_t max_len) {
    if (!tls || !net || !url || !response) return PHANTOM_TLS_INVALID_PARAM;

    /* Parse URL */
    char host[256] = {0};
    char path[1024] = "/";
    uint16_t port = 443;

    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        /* Security fix: Reject HTTP URLs in HTTPS function - no silent downgrade */
        printf("[phantom_tls] ERROR: http:// URL rejected by HTTPS function\n");
        printf("              Use phantom_http_get() for unencrypted requests\n");
        return PHANTOM_TLS_INVALID_PARAM;
    } else {
        /* Assume HTTPS for URLs without scheme */
        printf("[phantom_tls] No scheme in URL, assuming https://\n");
    }

    /* Extract host and path */
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        size_t host_len = colon - p;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, p, host_len);
        host[host_len] = '\0';
        uint16_t parsed_port;
        if (safe_parse_port(colon + 1, &parsed_port) != 0) {
            printf("[phantom_tls] ERROR: Invalid port number\n");
            return PHANTOM_TLS_INVALID_PARAM;
        }
        port = parsed_port;
    } else if (slash) {
        size_t host_len = slash - p;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, p, host_len);
        host[host_len] = '\0';  /* Security fix: Ensure null termination */
    } else {
        strncpy(host, p, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';  /* Security fix: Ensure null termination */
    }

    if (slash) {
        strncpy(path, slash, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';  /* Security fix: Ensure null termination */
    }

    /* Security fix: Validate host is not empty */
    if (host[0] == '\0') {
        printf("[phantom_tls] ERROR: Empty hostname in URL\n");
        return PHANTOM_TLS_INVALID_PARAM;
    }

    /* Security fix: Reject port 80 - no silent downgrade to HTTP */
    if (port == 80) {
        printf("[phantom_tls] ERROR: Port 80 rejected by HTTPS function\n");
        printf("              Use phantom_http_get() for unencrypted requests\n");
        return PHANTOM_TLS_INVALID_PARAM;
    }

    /* Connect with TLS */
    int ctx_id = phantom_tls_connect(tls, net, host, port);
    if (ctx_id < 0) {
        printf("[phantom_tls] Failed to connect to %s:%u: %s\n",
               host, port, phantom_tls_error_string(ctx_id));
        return ctx_id;
    }

    /* Get socket ID from context */
    phantom_tls_context_t *ctx = phantom_tls_get_context(tls, ctx_id);
    if (!ctx) {
        return PHANTOM_TLS_ERROR;
    }

    /* Build HTTP request */
    char request[2048];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: PhantomOS/1.0\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host);

    /* Send request */
    ssize_t sent = phantom_tls_send(tls, ctx_id, request, strlen(request));
    if (sent < 0) {
        phantom_tls_close(tls, ctx_id);
        phantom_socket_make_dormant(net, ctx->socket_id);
        return sent;
    }

    /* Receive response */
    ssize_t total = 0;
    while ((size_t)total < max_len - 1) {
        ssize_t received = phantom_tls_recv(tls, ctx_id,
                                             response + total,
                                             max_len - 1 - total);
        if (received <= 0) break;
        total += received;
    }
    response[total] = '\0';

    /* Clean up */
    phantom_tls_close(tls, ctx_id);
    phantom_socket_make_dormant(net, ctx->socket_id);

    return total;
}

ssize_t phantom_https_post(phantom_tls_t *tls, struct phantom_net *net,
                           const char *url, const char *body, size_t body_len,
                           char *response, size_t max_len) {
    if (!tls || !net || !url || !response) return PHANTOM_TLS_INVALID_PARAM;

    /* Parse URL with security validation */
    char host[256] = {0};
    char path[1024] = "/";
    uint16_t port = 443;

    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        /* Security fix: Reject HTTP URLs in HTTPS function */
        printf("[phantom_tls] ERROR: http:// URL rejected by HTTPS POST function\n");
        return PHANTOM_TLS_INVALID_PARAM;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        size_t host_len = colon - p;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, p, host_len);
        host[host_len] = '\0';
        uint16_t parsed_port;
        if (safe_parse_port(colon + 1, &parsed_port) != 0) {
            printf("[phantom_tls] ERROR: Invalid port number\n");
            return PHANTOM_TLS_INVALID_PARAM;
        }
        if (parsed_port == 80) {
            printf("[phantom_tls] ERROR: Port 80 rejected by HTTPS function\n");
            return PHANTOM_TLS_INVALID_PARAM;
        }
        port = parsed_port;
        if (slash) {
            strncpy(path, slash, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        }
    } else if (slash) {
        size_t host_len = slash - p;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, p, host_len);
        host[host_len] = '\0';
        strncpy(path, slash, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        strncpy(host, p, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    /* Security fix: Validate host is not empty */
    if (host[0] == '\0') {
        printf("[phantom_tls] ERROR: Empty hostname in URL\n");
        return PHANTOM_TLS_INVALID_PARAM;
    }

    /* Connect with TLS */
    int ctx_id = phantom_tls_connect(tls, net, host, port);
    if (ctx_id < 0) {
        return ctx_id;
    }

    phantom_tls_context_t *ctx = phantom_tls_get_context(tls, ctx_id);
    if (!ctx) return PHANTOM_TLS_ERROR;

    /* Build HTTP request */
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

    /* Send request headers */
    ssize_t sent = phantom_tls_send(tls, ctx_id, request, strlen(request));
    if (sent < 0) {
        phantom_tls_close(tls, ctx_id);
        phantom_socket_make_dormant(net, ctx->socket_id);
        return sent;
    }

    /* Send body */
    if (body && body_len > 0) {
        sent = phantom_tls_send(tls, ctx_id, body, body_len);
        if (sent < 0) {
            phantom_tls_close(tls, ctx_id);
            phantom_socket_make_dormant(net, ctx->socket_id);
            return sent;
        }
    }

    /* Receive response */
    ssize_t total = 0;
    while ((size_t)total < max_len - 1) {
        ssize_t received = phantom_tls_recv(tls, ctx_id,
                                             response + total,
                                             max_len - 1 - total);
        if (received <= 0) break;
        total += received;
    }
    response[total] = '\0';

    /* Clean up */
    phantom_tls_close(tls, ctx_id);
    phantom_socket_make_dormant(net, ctx->socket_id);

    return total;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * STATISTICS AND LOGGING
 * ══════════════════════════════════════════════════════════════════════════════ */

void phantom_tls_print_stats(phantom_tls_t *tls) {
    if (!tls) return;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║                    TLS STATISTICS                     ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("  Initialized: %s\n", tls->initialized ? "YES" : "NO");
    printf("  CA Loaded: %s\n", tls->ca_loaded ? "YES" : "NO");
    printf("  mbedTLS: %s\n", TLS_AVAILABLE ? "LINKED" : "NOT AVAILABLE");
    printf("\n");

    printf("  Connection Statistics:\n");
    printf("    Total connections:     %lu\n", tls->total_connections);
    printf("    Successful handshakes: %lu\n", tls->successful_handshakes);
    printf("    Failed handshakes:     %lu\n", tls->failed_handshakes);
    printf("    Cert verify failures:  %lu\n", tls->cert_verify_failures);
    printf("\n");

    printf("  Data Statistics:\n");
    printf("    Bytes encrypted:       %lu\n", tls->total_bytes_encrypted);
    printf("    Bytes decrypted:       %lu\n", tls->total_bytes_decrypted);
    printf("\n");

    printf("  Active Contexts: %d / %d\n", tls->context_count, PHANTOM_TLS_MAX_CONTEXTS);
    printf("\n");
}

void phantom_tls_print_cert(const phantom_tls_cert_info_t *cert) {
    if (!cert) return;

    printf("  Certificate Information:\n");
    printf("    Subject: %s\n", cert->subject[0] ? cert->subject : "(unknown)");
    printf("    Issuer:  %s\n", cert->issuer[0] ? cert->issuer : "(unknown)");
    printf("    Serial:  %s\n", cert->serial[0] ? cert->serial : "(unknown)");
    printf("    Key:     %d bits\n", cert->key_bits);
    if (cert->self_signed) {
        printf("    WARNING: Self-signed certificate\n");
    }
}

void phantom_tls_print_session(const phantom_tls_session_info_t *session) {
    if (!session) return;

    printf("  TLS Session:\n");
    printf("    Host:     %s:%u\n", session->hostname, session->port);
    printf("    Version:  %s\n", phantom_tls_version_string(session->version));
    printf("    Cipher:   %s\n", session->cipher_suite);
    printf("    Handshake: %lu ms\n", session->handshake_time_ms);
    printf("    Bytes:    %lu sent, %lu received\n",
           session->bytes_sent, session->bytes_received);
}

const char *phantom_tls_version_string(int version) {
    switch (version) {
        case 0x0300: return "SSL 3.0 (INSECURE)";
        case 0x0301: return "TLS 1.0 (DEPRECATED)";
        case 0x0302: return "TLS 1.1 (DEPRECATED)";
        case 0x0303: return "TLS 1.2";
        case 0x0304: return "TLS 1.3";
        default:     return "Unknown";
    }
}

int phantom_tls_available(void) {
    return TLS_AVAILABLE;
}
