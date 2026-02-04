/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM WEB BROWSER APP
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * A Governor-controlled web browser application for PhantomOS.
 *
 * Security Model:
 * - All network access requires Governor approval via CAP_NETWORK capability
 * - HTTPS connections require CAP_NETWORK_SECURE capability
 * - Unverified TLS requires explicit CAP_NETWORK_INSECURE approval
 * - All browsing is logged and preserved in geology
 * - No data is ever deleted - history is permanent
 *
 * Features:
 * - Governor-controlled network access (no silent connections)
 * - Domain allowlist/blocklist with Governor review
 * - Content filtering and safety checks
 * - Full audit trail of all network activity
 * - AI-powered content analysis
 */

#ifndef PHANTOM_WEBBROWSER_H
#define PHANTOM_WEBBROWSER_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* Forward declarations */
struct phantom_kernel;
struct phantom_governor;
struct phantom_net;
struct phantom_tls;
struct phantom_browser;

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────── */

#define WEBBROWSER_MAX_URL          4096
#define WEBBROWSER_MAX_DOMAIN       256
#define WEBBROWSER_MAX_TITLE        512
#define WEBBROWSER_MAX_ALLOWLIST    128
#define WEBBROWSER_MAX_BLOCKLIST    128
#define WEBBROWSER_MAX_PENDING      32
#define WEBBROWSER_HISTORY_PATH     "/geo/var/log/browser"

/* ─────────────────────────────────────────────────────────────────────────────
 * Result Codes
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    WEBBROWSER_OK = 0,
    WEBBROWSER_ERR_INVALID = -1,
    WEBBROWSER_ERR_NOMEM = -2,
    WEBBROWSER_ERR_NETWORK = -3,
    WEBBROWSER_ERR_GOVERNOR_DENIED = -4,
    WEBBROWSER_ERR_BLOCKED_DOMAIN = -5,
    WEBBROWSER_ERR_TLS_REQUIRED = -6,
    WEBBROWSER_ERR_TLS_UNAVAILABLE = -7,
    WEBBROWSER_ERR_TIMEOUT = -8,
    WEBBROWSER_ERR_NOT_INITIALIZED = -9,
    WEBBROWSER_ERR_CONTENT_BLOCKED = -10,
} webbrowser_result_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Connection Security Level
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    WEBBROWSER_SEC_NONE = 0,        /* HTTP (unencrypted) - requires approval */
    WEBBROWSER_SEC_TLS,             /* HTTPS with valid certificate */
    WEBBROWSER_SEC_TLS_UNVERIFIED,  /* HTTPS with unverified cert - requires approval */
    WEBBROWSER_SEC_TLS_EXPIRED,     /* HTTPS with expired cert - requires approval */
    WEBBROWSER_SEC_TLS_SELF_SIGNED, /* HTTPS with self-signed cert - requires approval */
} webbrowser_security_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Domain Policy
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    DOMAIN_POLICY_ASK = 0,          /* Ask Governor each time */
    DOMAIN_POLICY_ALLOW,            /* Pre-approved by user */
    DOMAIN_POLICY_BLOCK,            /* Blocked by user or Governor */
    DOMAIN_POLICY_ALLOW_SESSION,    /* Allowed for this session only */
} domain_policy_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Domain Entry (for allowlist/blocklist)
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct webbrowser_domain_entry {
    char domain[WEBBROWSER_MAX_DOMAIN];
    domain_policy_t policy;
    webbrowser_security_t min_security;  /* Minimum required security level */
    time_t added_at;
    time_t last_access;
    uint64_t access_count;
    char reason[256];                    /* Why blocked/allowed */
    int include_subdomains;              /* *.domain.com */
} webbrowser_domain_entry_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Pending Request (awaiting Governor approval)
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct webbrowser_pending_request {
    uint32_t request_id;
    char url[WEBBROWSER_MAX_URL];
    char domain[WEBBROWSER_MAX_DOMAIN];
    webbrowser_security_t security;
    time_t requested_at;
    int is_redirect;
    char redirect_from[WEBBROWSER_MAX_URL];

    /* Governor decision */
    int approved;
    int decided;
    char decision_reason[256];
} webbrowser_pending_request_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Connection Info (for logging)
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct webbrowser_connection_info {
    char url[WEBBROWSER_MAX_URL];
    char domain[WEBBROWSER_MAX_DOMAIN];
    webbrowser_security_t security;

    /* TLS info (if applicable) */
    char tls_version[32];
    char cipher_suite[64];
    char cert_subject[256];
    char cert_issuer[256];
    int cert_valid;
    time_t cert_expires;

    /* Connection stats */
    time_t connected_at;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint32_t response_time_ms;
    int status_code;
} webbrowser_connection_info_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Browser Statistics
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct webbrowser_stats {
    /* Request counts */
    uint64_t total_requests;
    uint64_t approved_requests;
    uint64_t denied_requests;
    uint64_t blocked_domains;

    /* Network stats */
    uint64_t total_bytes_sent;
    uint64_t total_bytes_received;
    uint64_t total_connections;

    /* Security stats */
    uint64_t https_connections;
    uint64_t http_connections;
    uint64_t cert_warnings;
    uint64_t blocked_content;

    /* Session stats */
    time_t session_start;
    uint32_t pages_visited;
    uint32_t unique_domains;
} webbrowser_stats_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Web Browser Application Context
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_webbrowser {
    /* References */
    struct phantom_kernel *kernel;
    struct phantom_governor *governor;
    struct phantom_net *net;
    struct phantom_tls *tls;
    struct phantom_browser *browser;    /* Underlying browser implementation */
    struct vfs_context *vfs;            /* VFS for logging to GeoFS */

    /* Domain policies */
    webbrowser_domain_entry_t allowlist[WEBBROWSER_MAX_ALLOWLIST];
    int allowlist_count;
    webbrowser_domain_entry_t blocklist[WEBBROWSER_MAX_BLOCKLIST];
    int blocklist_count;

    /* Pending requests */
    webbrowser_pending_request_t pending[WEBBROWSER_MAX_PENDING];
    int pending_count;
    uint32_t next_request_id;

    /* Current connection */
    webbrowser_connection_info_t current_connection;
    int connection_active;

    /* Response buffer for fetched content */
    char *response_buffer;          /* Dynamically allocated response */
    size_t response_size;           /* Size of response data */
    size_t response_capacity;       /* Allocated capacity */
    int response_status;            /* HTTP status code */
    char response_content_type[128]; /* Content-Type header */
    char response_location[WEBBROWSER_MAX_URL]; /* Location header for redirects */

    /* Configuration */
    int require_https;              /* Require HTTPS for all connections */
    int require_valid_cert;         /* Require valid certificate */
    int auto_approve_allowlist;     /* Auto-approve allowlisted domains */
    int log_all_requests;           /* Log all requests to geology */
    int block_mixed_content;        /* Block HTTP resources on HTTPS pages */

    /* Default security level */
    webbrowser_security_t default_security;

    /* Statistics */
    webbrowser_stats_t stats;

    /* State */
    int initialized;
    int network_enabled;
    int tls_available;

} phantom_webbrowser_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Initialization & Lifecycle
 * ───────────────────────────────────────────────────────────────────────────── */

/* Initialize the web browser app */
int phantom_webbrowser_init(phantom_webbrowser_t *wb,
                            struct phantom_kernel *kernel,
                            struct phantom_governor *governor);

/* Shutdown the web browser app */
void phantom_webbrowser_shutdown(phantom_webbrowser_t *wb);

/* Set network layer (optional - enables actual connections) */
void phantom_webbrowser_set_network(phantom_webbrowser_t *wb,
                                    struct phantom_net *net);

/* Set TLS layer (optional - enables HTTPS) */
void phantom_webbrowser_set_tls(phantom_webbrowser_t *wb,
                                struct phantom_tls *tls);

/* Set VFS for GeoFS logging (optional - enables audit logging) */
void phantom_webbrowser_set_vfs(phantom_webbrowser_t *wb,
                                struct vfs_context *vfs);

/* ─────────────────────────────────────────────────────────────────────────────
 * Navigation (Governor-Controlled)
 * ───────────────────────────────────────────────────────────────────────────── */

/* Navigate to URL - requests Governor approval */
int phantom_webbrowser_navigate(phantom_webbrowser_t *wb, const char *url);

/* Navigate with explicit security level */
int phantom_webbrowser_navigate_secure(phantom_webbrowser_t *wb,
                                       const char *url,
                                       webbrowser_security_t min_security);

/* Check if URL would be allowed (without navigating) */
int phantom_webbrowser_check_url(phantom_webbrowser_t *wb,
                                 const char *url,
                                 char *reason, size_t reason_size);

/* Get pending request status */
int phantom_webbrowser_get_pending(phantom_webbrowser_t *wb,
                                   uint32_t request_id,
                                   webbrowser_pending_request_t *request_out);

/* Cancel pending request */
int phantom_webbrowser_cancel(phantom_webbrowser_t *wb, uint32_t request_id);

/* Get fetched response content */
int phantom_webbrowser_get_response(phantom_webbrowser_t *wb,
                                    const char **content, size_t *size);

/* Get HTTP status code from last request */
int phantom_webbrowser_get_status(phantom_webbrowser_t *wb);

/* Get content type from last request */
const char *phantom_webbrowser_get_content_type(phantom_webbrowser_t *wb);

/* Clear response buffer */
void phantom_webbrowser_clear_response(phantom_webbrowser_t *wb);

/* ─────────────────────────────────────────────────────────────────────────────
 * Domain Policy Management
 * ───────────────────────────────────────────────────────────────────────────── */

/* Add domain to allowlist */
int phantom_webbrowser_allow_domain(phantom_webbrowser_t *wb,
                                    const char *domain,
                                    int include_subdomains,
                                    const char *reason);

/* Add domain to blocklist */
int phantom_webbrowser_block_domain(phantom_webbrowser_t *wb,
                                    const char *domain,
                                    int include_subdomains,
                                    const char *reason);

/* Remove domain from lists (moves to ASK policy) */
int phantom_webbrowser_reset_domain(phantom_webbrowser_t *wb,
                                    const char *domain);

/* Get domain policy */
domain_policy_t phantom_webbrowser_get_domain_policy(phantom_webbrowser_t *wb,
                                                     const char *domain);

/* Check if domain matches pattern (with subdomain support) */
int phantom_webbrowser_domain_matches(const char *pattern,
                                      const char *domain,
                                      int include_subdomains);

/* ─────────────────────────────────────────────────────────────────────────────
 * Configuration
 * ───────────────────────────────────────────────────────────────────────────── */

/* Require HTTPS for all connections */
void phantom_webbrowser_require_https(phantom_webbrowser_t *wb, int required);

/* Require valid TLS certificates */
void phantom_webbrowser_require_valid_cert(phantom_webbrowser_t *wb, int required);

/* Auto-approve allowlisted domains */
void phantom_webbrowser_auto_approve(phantom_webbrowser_t *wb, int enabled);

/* Set default security level */
void phantom_webbrowser_set_default_security(phantom_webbrowser_t *wb,
                                             webbrowser_security_t level);

/* ─────────────────────────────────────────────────────────────────────────────
 * Information & Statistics
 * ───────────────────────────────────────────────────────────────────────────── */

/* Get current connection info */
int phantom_webbrowser_get_connection(phantom_webbrowser_t *wb,
                                      webbrowser_connection_info_t *info_out);

/* Get browser statistics */
void phantom_webbrowser_get_stats(phantom_webbrowser_t *wb,
                                  webbrowser_stats_t *stats_out);

/* Print status */
void phantom_webbrowser_print_status(phantom_webbrowser_t *wb);

/* Print domain policies */
void phantom_webbrowser_print_policies(phantom_webbrowser_t *wb);

/* Print statistics */
void phantom_webbrowser_print_stats(phantom_webbrowser_t *wb);

/* Print connection info */
void phantom_webbrowser_print_connection(const webbrowser_connection_info_t *info);

/* ─────────────────────────────────────────────────────────────────────────────
 * Governor Integration Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

/* Request network capability from Governor */
int phantom_webbrowser_request_network(phantom_webbrowser_t *wb,
                                       const char *url,
                                       const char *purpose);

/* Request secure network capability from Governor */
int phantom_webbrowser_request_secure_network(phantom_webbrowser_t *wb,
                                              const char *url,
                                              webbrowser_security_t security);

/* Log connection to Governor audit trail */
void phantom_webbrowser_log_connection(phantom_webbrowser_t *wb,
                                       const webbrowser_connection_info_t *info);

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility Functions
 * ───────────────────────────────────────────────────────────────────────────── */

/* Get result string */
const char *phantom_webbrowser_result_string(webbrowser_result_t result);

/* Get security level string */
const char *phantom_webbrowser_security_string(webbrowser_security_t security);

/* Get policy string */
const char *phantom_webbrowser_policy_string(domain_policy_t policy);

/* Extract domain from URL */
int phantom_webbrowser_extract_domain(const char *url,
                                      char *domain, size_t domain_size);

/* Check if URL is HTTPS */
int phantom_webbrowser_is_https(const char *url);

#endif /* PHANTOM_WEBBROWSER_H */
