/*
 * ==============================================================================
 *                        PHANTOM URL SCANNER
 *                     "To Create, Not To Destroy"
 * ==============================================================================
 *
 * Local malware/phishing URL scanner for the Phantom Web Browser.
 * Provides real-time URL safety analysis using heuristic detection.
 *
 * Features:
 * - Typosquatting detection (paypa1.com, arnazon.com)
 * - Suspicious TLD analysis (.tk, .ml, .xyz commonly used for phishing)
 * - IP-based URL detection
 * - Excessive subdomain depth detection
 * - Homograph attack detection (unicode lookalikes)
 * - Known phishing keyword detection
 * - All local - no data sent externally
 */

#ifndef PHANTOM_URLSCAN_H
#define PHANTOM_URLSCAN_H

#include <stdint.h>
#include <stddef.h>

/* Maximum URL length to scan */
#define URLSCAN_MAX_URL 4096
#define URLSCAN_MAX_DOMAIN 256
#define URLSCAN_MAX_REASON 512

/* ─────────────────────────────────────────────────────────────────────────────
 * Threat Levels
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    URLSCAN_SAFE = 0,           /* URL appears safe */
    URLSCAN_UNKNOWN = 1,        /* Unknown domain, use caution */
    URLSCAN_SUSPICIOUS = 2,     /* Some red flags detected */
    URLSCAN_WARNING = 3,        /* Multiple red flags, likely dangerous */
    URLSCAN_DANGEROUS = 4,      /* High confidence malicious/phishing */
    URLSCAN_BLOCKED = 5         /* Known malicious, do not visit */
} urlscan_threat_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Threat Flags (bitfield)
 * ───────────────────────────────────────────────────────────────────────────── */

#define URLSCAN_FLAG_NONE           0x00000000
#define URLSCAN_FLAG_TYPOSQUAT      0x00000001  /* Looks like typosquatting */
#define URLSCAN_FLAG_SUSPICIOUS_TLD 0x00000002  /* High-risk TLD */
#define URLSCAN_FLAG_IP_ADDRESS     0x00000004  /* IP instead of domain */
#define URLSCAN_FLAG_DEEP_SUBDOMAIN 0x00000008  /* Too many subdomains */
#define URLSCAN_FLAG_HOMOGRAPH      0x00000010  /* Unicode lookalike chars */
#define URLSCAN_FLAG_PHISHING_WORDS 0x00000020  /* Suspicious path keywords */
#define URLSCAN_FLAG_KNOWN_MALWARE  0x00000040  /* In malware blocklist */
#define URLSCAN_FLAG_HTTP_LOGIN     0x00000080  /* HTTP with login form */
#define URLSCAN_FLAG_LONG_DOMAIN    0x00000100  /* Unusually long domain */
#define URLSCAN_FLAG_RANDOM_DOMAIN  0x00000200  /* Random-looking domain */
#define URLSCAN_FLAG_PUNYCODE       0x00000400  /* Punycode domain (xn--) */
#define URLSCAN_FLAG_DATA_URI       0x00000800  /* Data URI (can hide code) */
#define URLSCAN_FLAG_REDIRECT_CHAIN 0x00001000  /* Known redirect service */
#define URLSCAN_FLAG_FREE_HOSTING   0x00002000  /* Free hosting provider */
#define URLSCAN_FLAG_NEW_TLD        0x00004000  /* Recently created TLD */

/* ─────────────────────────────────────────────────────────────────────────────
 * Scan Result
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct urlscan_result {
    urlscan_threat_t threat_level;  /* Overall threat assessment */
    uint32_t flags;                  /* Specific threat flags detected */
    int score;                       /* Threat score (0-100, higher = worse) */
    char reason[URLSCAN_MAX_REASON]; /* Human-readable explanation */
    char domain[URLSCAN_MAX_DOMAIN]; /* Extracted domain */
    int is_https;                    /* Was HTTPS detected */

    /* Detailed findings */
    char typosquat_target[64];       /* If typosquatting, what brand */
    char suspicious_tld[16];         /* The suspicious TLD if any */
    int subdomain_depth;             /* Number of subdomains */
    int homograph_chars;             /* Count of lookalike characters */
} urlscan_result_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Blocklist Entry (for hash-based lookup)
 * ───────────────────────────────────────────────────────────────────────────── */

#define URLSCAN_HASH_SIZE 32         /* SHA-256 hash size */
#define URLSCAN_MAX_BLOCKLIST 100000 /* Max entries in blocklist */
#define URLSCAN_HASH_BUCKETS 65536   /* Hash table buckets */

typedef struct urlscan_blocklist_entry {
    uint32_t hash_prefix;            /* First 4 bytes of domain hash */
    char domain[URLSCAN_MAX_DOMAIN]; /* Full domain for verification */
    struct urlscan_blocklist_entry *next;
} urlscan_blocklist_entry_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Scanner Context
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_urlscan {
    int initialized;

    /* Statistics */
    uint64_t total_scans;
    uint64_t safe_count;
    uint64_t suspicious_count;
    uint64_t dangerous_count;
    uint64_t blocked_count;

    /* Configuration */
    int strict_mode;                 /* More aggressive detection */
    int warn_http_login;             /* Warn about HTTP on login pages */
    int check_homographs;            /* Check for unicode attacks */
    int max_subdomain_depth;         /* Max allowed subdomains (default 3) */

    /* Blocklist hash table (Option 2: hash-based lookup) */
    urlscan_blocklist_entry_t *blocklist_table[URLSCAN_HASH_BUCKETS];
    uint32_t blocklist_count;        /* Number of entries loaded */

    /* DNS-based blocking config (Option 4) */
    int dns_blocking_enabled;        /* Enable DNS-based checks */
    int dns_timeout_ms;              /* Timeout for DNS queries */
    char dns_server[64];             /* DNS server to use (e.g., "9.9.9.9") */

    /* Allowlist for false positives */
    char **allowlist;
    uint32_t allowlist_count;
    uint32_t allowlist_capacity;
} phantom_urlscan_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * API Functions
 * ───────────────────────────────────────────────────────────────────────────── */

/* Initialize the URL scanner */
int phantom_urlscan_init(phantom_urlscan_t *scanner);

/* Shutdown and cleanup */
void phantom_urlscan_shutdown(phantom_urlscan_t *scanner);

/* Scan a URL and get threat assessment */
int phantom_urlscan_check(phantom_urlscan_t *scanner,
                          const char *url,
                          urlscan_result_t *result);

/* Quick check - just returns threat level without full analysis */
urlscan_threat_t phantom_urlscan_quick(phantom_urlscan_t *scanner,
                                        const char *url);

/* Get threat level as string */
const char *phantom_urlscan_threat_str(urlscan_threat_t level);

/* Get threat level icon/emoji */
const char *phantom_urlscan_threat_icon(urlscan_threat_t level);

/* Get CSS class for threat level */
const char *phantom_urlscan_threat_class(urlscan_threat_t level);

/* Format flags as human-readable string */
void phantom_urlscan_format_flags(uint32_t flags, char *buf, size_t size);

/* Get scanner statistics */
void phantom_urlscan_get_stats(phantom_urlscan_t *scanner,
                               uint64_t *total, uint64_t *safe,
                               uint64_t *suspicious, uint64_t *dangerous);

/* ─────────────────────────────────────────────────────────────────────────────
 * Blocklist Functions (Option 1: External blocklists)
 * ───────────────────────────────────────────────────────────────────────────── */

/* Load blocklist from hosts-format file (e.g., Steven Black's hosts file)
 * Format: "0.0.0.0 malicious-domain.com" or "127.0.0.1 phishing-site.com"
 * Lines starting with # are comments
 * Returns number of entries loaded, or -1 on error */
int phantom_urlscan_load_hosts_blocklist(phantom_urlscan_t *scanner,
                                          const char *filepath);

/* Load blocklist from simple domain list (one domain per line)
 * Supports URLhaus, PhishTank export formats */
int phantom_urlscan_load_domain_blocklist(phantom_urlscan_t *scanner,
                                           const char *filepath);

/* Load all blocklists from a directory */
int phantom_urlscan_load_blocklist_dir(phantom_urlscan_t *scanner,
                                        const char *dirpath);

/* Add single domain to blocklist */
int phantom_urlscan_add_blocked_domain(phantom_urlscan_t *scanner,
                                        const char *domain);

/* Add domain to allowlist (overrides blocklist) */
int phantom_urlscan_add_allowed_domain(phantom_urlscan_t *scanner,
                                        const char *domain);

/* Check if domain is in blocklist (hash-based lookup) */
int phantom_urlscan_is_blocked(phantom_urlscan_t *scanner,
                                const char *domain);

/* Check if domain is in allowlist */
int phantom_urlscan_is_allowed(phantom_urlscan_t *scanner,
                                const char *domain);

/* Clear all blocklist entries */
void phantom_urlscan_clear_blocklist(phantom_urlscan_t *scanner);

/* Get blocklist statistics */
uint32_t phantom_urlscan_get_blocklist_count(phantom_urlscan_t *scanner);

/* ─────────────────────────────────────────────────────────────────────────────
 * DNS-Based Blocking (Option 4: Quad9, Cloudflare)
 * ───────────────────────────────────────────────────────────────────────────── */

/* DNS blocking services */
#define URLSCAN_DNS_QUAD9      "9.9.9.9"       /* Quad9 malware blocking */
#define URLSCAN_DNS_QUAD9_TLS  "9.9.9.9:853"   /* Quad9 with TLS */
#define URLSCAN_DNS_CLOUDFLARE "1.1.1.2"       /* Cloudflare malware blocking */
#define URLSCAN_DNS_OPENDNS    "208.67.222.222" /* OpenDNS with filtering */

/* Enable DNS-based blocking
 * dns_server: Use one of URLSCAN_DNS_* constants or NULL for default (Quad9)
 * timeout_ms: Query timeout in milliseconds (default 1000) */
int phantom_urlscan_enable_dns_blocking(phantom_urlscan_t *scanner,
                                         const char *dns_server,
                                         int timeout_ms);

/* Disable DNS-based blocking */
void phantom_urlscan_disable_dns_blocking(phantom_urlscan_t *scanner);

/* Check domain against DNS blocklist
 * Returns 1 if blocked (NXDOMAIN or blocked IP), 0 if safe, -1 on error */
int phantom_urlscan_dns_check(phantom_urlscan_t *scanner,
                               const char *domain);

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility Functions
 * ───────────────────────────────────────────────────────────────────────────── */

/* Check if domain is typosquatting a known brand */
int phantom_urlscan_check_typosquat(const char *domain,
                                     char *target, size_t target_size);

/* Check if TLD is suspicious */
int phantom_urlscan_check_tld(const char *domain, char *tld, size_t tld_size);

/* Check for homograph characters */
int phantom_urlscan_check_homograph(const char *domain);

/* Check for suspicious path keywords */
int phantom_urlscan_check_path(const char *path);

#endif /* PHANTOM_URLSCAN_H */
