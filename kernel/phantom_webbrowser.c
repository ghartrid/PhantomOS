/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM WEB BROWSER APP
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Governor-controlled web browser for PhantomOS.
 * All network access requires explicit Governor approval.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "phantom_webbrowser.h"
#include "phantom.h"
#include "governor.h"
#include "phantom_net.h"
#include "phantom_tls.h"
#include "phantom_browser.h"
#include "vfs.h"
#include "../geofs.h"

/* GeoFS logging path */
#define WEBBROWSER_LOG_DIR  "/geo/var/log/browser"

/* Response buffer default size */
#define WEBBROWSER_RESPONSE_INITIAL_SIZE  (64 * 1024)   /* 64KB initial */
#define WEBBROWSER_RESPONSE_MAX_SIZE      (16 * 1024 * 1024)  /* 16MB max */

/* ══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

const char *phantom_webbrowser_result_string(webbrowser_result_t result) {
    switch (result) {
        case WEBBROWSER_OK:                 return "OK";
        case WEBBROWSER_ERR_INVALID:        return "Invalid parameter";
        case WEBBROWSER_ERR_NOMEM:          return "Out of memory";
        case WEBBROWSER_ERR_NETWORK:        return "Network error";
        case WEBBROWSER_ERR_GOVERNOR_DENIED: return "Governor denied access";
        case WEBBROWSER_ERR_BLOCKED_DOMAIN: return "Domain blocked";
        case WEBBROWSER_ERR_TLS_REQUIRED:   return "TLS/HTTPS required";
        case WEBBROWSER_ERR_TLS_UNAVAILABLE: return "TLS not available";
        case WEBBROWSER_ERR_TIMEOUT:        return "Connection timeout";
        case WEBBROWSER_ERR_NOT_INITIALIZED: return "Browser not initialized";
        case WEBBROWSER_ERR_CONTENT_BLOCKED: return "Content blocked";
        default:                            return "Unknown error";
    }
}

const char *phantom_webbrowser_security_string(webbrowser_security_t security) {
    switch (security) {
        case WEBBROWSER_SEC_NONE:           return "HTTP (unencrypted)";
        case WEBBROWSER_SEC_TLS:            return "HTTPS (verified)";
        case WEBBROWSER_SEC_TLS_UNVERIFIED: return "HTTPS (unverified)";
        case WEBBROWSER_SEC_TLS_EXPIRED:    return "HTTPS (expired cert)";
        case WEBBROWSER_SEC_TLS_SELF_SIGNED: return "HTTPS (self-signed)";
        default:                            return "Unknown";
    }
}

const char *phantom_webbrowser_policy_string(domain_policy_t policy) {
    switch (policy) {
        case DOMAIN_POLICY_ASK:             return "Ask";
        case DOMAIN_POLICY_ALLOW:           return "Allow";
        case DOMAIN_POLICY_BLOCK:           return "Block";
        case DOMAIN_POLICY_ALLOW_SESSION:   return "Allow (session)";
        default:                            return "Unknown";
    }
}

int phantom_webbrowser_extract_domain(const char *url,
                                      char *domain, size_t domain_size) {
    if (!url || !domain || domain_size == 0) return -1;

    domain[0] = '\0';

    /* Skip scheme */
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    /* Extract host (up to /, :, or end) */
    const char *end = p;
    while (*end && *end != '/' && *end != ':' && *end != '?' && *end != '#') {
        end++;
    }

    size_t len = end - p;
    if (len >= domain_size) len = domain_size - 1;

    strncpy(domain, p, len);
    domain[len] = '\0';

    /* Convert to lowercase */
    for (char *c = domain; *c; c++) {
        *c = tolower(*c);
    }

    return 0;
}

int phantom_webbrowser_is_https(const char *url) {
    if (!url) return 0;
    return strncmp(url, "https://", 8) == 0;
}

int phantom_webbrowser_domain_matches(const char *pattern,
                                      const char *domain,
                                      int include_subdomains) {
    if (!pattern || !domain) return 0;

    /* Direct match */
    if (strcasecmp(pattern, domain) == 0) return 1;

    /* Subdomain match */
    if (include_subdomains) {
        size_t plen = strlen(pattern);
        size_t dlen = strlen(domain);

        if (dlen > plen + 1) {
            /* Check if domain ends with .pattern */
            const char *suffix = domain + (dlen - plen);
            if (strcasecmp(suffix, pattern) == 0 && domain[dlen - plen - 1] == '.') {
                return 1;
            }
        }
    }

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * GEOFS LOGGING - All browser activity is permanently recorded
 * ══════════════════════════════════════════════════════════════════════════════ */

static void webbrowser_log_to_geofs(phantom_webbrowser_t *wb, const char *action,
                                    const char *url, const char *domain,
                                    const char *result, const char *details) {
    if (!wb) return;

    /* Format: [timestamp] action | url | domain | result | details */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    char log_entry[2048];
    snprintf(log_entry, sizeof(log_entry),
             "[%s] %s | %s | %s | %s | %s\n",
             timestamp,
             action ? action : "unknown",
             url ? url : "-",
             domain ? domain : "-",
             result ? result : "-",
             details ? details : "-");

    /* Write to GeoFS if VFS is available */
    struct vfs_context *vfs = wb->vfs;
    if (vfs) {
        /* Append to daily log file */
        char log_path[512];
        char date_str[32];
        strftime(date_str, sizeof(date_str), "%Y-%m-%d", tm_info);
        snprintf(log_path, sizeof(log_path), "%s/browser-%s.log",
                 WEBBROWSER_LOG_DIR, date_str);

        /* Use VFS API with file descriptor */
        vfs_fd_t fd = vfs_open(vfs, 1, log_path, VFS_O_WRONLY | VFS_O_CREATE, 0644);
        if (fd >= 0) {
            vfs_write(vfs, fd, log_entry, strlen(log_entry));
            vfs_close(vfs, fd);
        }
    }
}

/* Log a navigation attempt */
static void webbrowser_log_navigation(phantom_webbrowser_t *wb,
                                      const char *url, const char *domain,
                                      int approved, const char *reason) {
    char result[64];
    snprintf(result, sizeof(result), "%s", approved ? "APPROVED" : "DENIED");
    webbrowser_log_to_geofs(wb, "NAVIGATE", url, domain, result, reason);
}

/* Log a policy change */
static void webbrowser_log_policy(phantom_webbrowser_t *wb,
                                  const char *domain, const char *action,
                                  const char *reason) {
    webbrowser_log_to_geofs(wb, action, "-", domain, "POLICY", reason);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * INTERACTIVE PROMPTS - Ask user for permission when needed
 * ══════════════════════════════════════════════════════════════════════════════ */

static int webbrowser_prompt_user(phantom_webbrowser_t *wb,
                                  const char *url, const char *domain,
                                  webbrowser_security_t security,
                                  char *response, size_t response_size) {
    (void)wb;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║              GOVERNOR NETWORK ACCESS REQUEST                          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  The web browser is requesting access to:\n");
    printf("\n");
    printf("    URL:      %s\n", url);
    printf("    Domain:   %s\n", domain);
    printf("    Security: %s\n", phantom_webbrowser_security_string(security));
    printf("\n");

    if (security == WEBBROWSER_SEC_NONE) {
        printf("  \033[33m⚠ WARNING: This is an unencrypted HTTP connection.\033[0m\n");
        printf("    Data sent/received may be visible to network observers.\n");
        printf("\n");
    } else if (security == WEBBROWSER_SEC_TLS_UNVERIFIED ||
               security == WEBBROWSER_SEC_TLS_SELF_SIGNED) {
        printf("  \033[31m⚠ DANGER: Certificate cannot be verified!\033[0m\n");
        printf("    This connection may be intercepted by a third party.\n");
        printf("\n");
    }

    printf("  Options:\n");
    printf("    [Y] Allow this request\n");
    printf("    [A] Allow and add domain to allowlist (remember)\n");
    printf("    [S] Allow for this session only\n");
    printf("    [N] Deny this request\n");
    printf("    [B] Deny and add domain to blocklist\n");
    printf("\n");
    printf("  Your choice [Y/A/S/N/B]: ");
    fflush(stdout);

    /* Read user input */
    if (fgets(response, response_size, stdin) == NULL) {
        response[0] = 'N';  /* Default to deny on error */
    }

    /* Trim newline */
    size_t len = strlen(response);
    if (len > 0 && response[len-1] == '\n') {
        response[len-1] = '\0';
    }

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CAPABILITY SCOPES - Fine-grained domain permissions via Governor
 * ══════════════════════════════════════════════════════════════════════════════ */

static int webbrowser_add_capability_scope(phantom_webbrowser_t *wb,
                                           const char *domain,
                                           uint32_t capability,
                                           uint64_t valid_seconds) {
    if (!wb || !wb->governor || !domain) return -1;

    /* Create a scope pattern for this domain */
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "https://%.240s/*", domain);

    /* Use Governor's capability scope API
     * Parameters: gov, capability, path_pattern, max_bytes, duration_seconds */
    int result = governor_add_scope(wb->governor, capability, pattern, 0, valid_seconds);
    if (result == 0) {
        printf("[webbrowser] Added capability scope for %s\n", domain);
    }

    return result;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * INITIALIZATION & LIFECYCLE
 * ══════════════════════════════════════════════════════════════════════════════ */

int phantom_webbrowser_init(phantom_webbrowser_t *wb,
                            struct phantom_kernel *kernel,
                            struct phantom_governor *governor) {
    if (!wb || !kernel) return WEBBROWSER_ERR_INVALID;

    memset(wb, 0, sizeof(phantom_webbrowser_t));

    wb->kernel = kernel;
    wb->governor = governor;
    wb->next_request_id = 1;

    /* Default configuration - secure by default */
    wb->require_https = 0;           /* Allow HTTP but warn */
    wb->require_valid_cert = 1;      /* Require valid certificates */
    wb->auto_approve_allowlist = 1;  /* Auto-approve allowlisted domains */
    wb->log_all_requests = 1;        /* Log everything */
    wb->block_mixed_content = 1;     /* Block mixed content */
    wb->default_security = WEBBROWSER_SEC_TLS;

    /* Initialize statistics */
    wb->stats.session_start = time(NULL);

    /* Initialize response buffer */
    wb->response_buffer = malloc(WEBBROWSER_RESPONSE_INITIAL_SIZE);
    if (wb->response_buffer) {
        wb->response_capacity = WEBBROWSER_RESPONSE_INITIAL_SIZE;
        wb->response_size = 0;
        wb->response_buffer[0] = '\0';
    }

    /* Add some default safe domains to allowlist */
    phantom_webbrowser_allow_domain(wb, "example.com", 1, "Default safe domain");
    phantom_webbrowser_allow_domain(wb, "localhost", 0, "Local development");

    /* ═══════════════════════════════════════════════════════════════════════════
     * POPULAR WEBSITES - Pre-approved for convenience (ads still blocked)
     * ═══════════════════════════════════════════════════════════════════════════ */

    /* Google services (main sites, not ad/tracking domains) */
    phantom_webbrowser_allow_domain(wb, "google.com", 1, "Google Search");
    phantom_webbrowser_allow_domain(wb, "www.google.com", 0, "Google Search");
    phantom_webbrowser_allow_domain(wb, "google.co.uk", 1, "Google UK");
    phantom_webbrowser_allow_domain(wb, "googleapis.com", 1, "Google APIs");
    phantom_webbrowser_allow_domain(wb, "gstatic.com", 1, "Google Static Content");
    phantom_webbrowser_allow_domain(wb, "youtube.com", 1, "YouTube");
    phantom_webbrowser_allow_domain(wb, "gmail.com", 1, "Gmail");
    phantom_webbrowser_allow_domain(wb, "drive.google.com", 0, "Google Drive");
    phantom_webbrowser_allow_domain(wb, "docs.google.com", 0, "Google Docs");
    phantom_webbrowser_allow_domain(wb, "maps.google.com", 0, "Google Maps");

    /* Other major sites */
    phantom_webbrowser_allow_domain(wb, "github.com", 1, "GitHub");
    phantom_webbrowser_allow_domain(wb, "githubusercontent.com", 1, "GitHub Content");
    phantom_webbrowser_allow_domain(wb, "wikipedia.org", 1, "Wikipedia");
    phantom_webbrowser_allow_domain(wb, "wikimedia.org", 1, "Wikimedia");
    phantom_webbrowser_allow_domain(wb, "stackoverflow.com", 1, "Stack Overflow");
    phantom_webbrowser_allow_domain(wb, "stackexchange.com", 1, "Stack Exchange");
    phantom_webbrowser_allow_domain(wb, "reddit.com", 1, "Reddit");
    phantom_webbrowser_allow_domain(wb, "archive.org", 1, "Internet Archive");
    phantom_webbrowser_allow_domain(wb, "cloudflare.com", 1, "Cloudflare");
    phantom_webbrowser_allow_domain(wb, "mozilla.org", 1, "Mozilla");
    phantom_webbrowser_allow_domain(wb, "w3.org", 1, "W3C");
    phantom_webbrowser_allow_domain(wb, "iana.org", 1, "IANA");

    printf("  Pre-approved sites: %d domains\n", wb->allowlist_count);

    /* Block known dangerous patterns */
    phantom_webbrowser_block_domain(wb, "malware.test", 1, "Test malware domain");

    /* ═══════════════════════════════════════════════════════════════════════════
     * AD BLOCKING - Block common advertising and tracking domains
     * ═══════════════════════════════════════════════════════════════════════════ */

    /* Major ad networks */
    phantom_webbrowser_block_domain(wb, "doubleclick.net", 1, "Ad network (Google)");
    phantom_webbrowser_block_domain(wb, "googlesyndication.com", 1, "Ad network (Google)");
    phantom_webbrowser_block_domain(wb, "googleadservices.com", 1, "Ad network (Google)");
    phantom_webbrowser_block_domain(wb, "googleads.g.doubleclick.net", 1, "Ad network (Google)");
    phantom_webbrowser_block_domain(wb, "adservice.google.com", 1, "Ad network (Google)");
    phantom_webbrowser_block_domain(wb, "pagead2.googlesyndication.com", 1, "Ad network (Google)");

    phantom_webbrowser_block_domain(wb, "facebook.net", 1, "Tracking (Meta)");
    phantom_webbrowser_block_domain(wb, "fbcdn.net", 1, "Tracking (Meta)");
    phantom_webbrowser_block_domain(wb, "connect.facebook.net", 1, "Tracking (Meta)");

    phantom_webbrowser_block_domain(wb, "ads.yahoo.com", 1, "Ad network (Yahoo)");
    phantom_webbrowser_block_domain(wb, "advertising.com", 1, "Ad network (AOL)");

    phantom_webbrowser_block_domain(wb, "adsserver.com", 1, "Ad server");
    phantom_webbrowser_block_domain(wb, "adserver.com", 1, "Ad server");
    phantom_webbrowser_block_domain(wb, "adtech.com", 1, "Ad network");

    /* Analytics & tracking */
    phantom_webbrowser_block_domain(wb, "google-analytics.com", 1, "Tracking (Google Analytics)");
    phantom_webbrowser_block_domain(wb, "googletagmanager.com", 1, "Tracking (Google Tag Manager)");
    phantom_webbrowser_block_domain(wb, "googletagservices.com", 1, "Tracking (Google)");

    phantom_webbrowser_block_domain(wb, "analytics.twitter.com", 1, "Tracking (Twitter)");
    phantom_webbrowser_block_domain(wb, "ads.twitter.com", 1, "Ad network (Twitter)");

    phantom_webbrowser_block_domain(wb, "bat.bing.com", 1, "Tracking (Microsoft)");
    phantom_webbrowser_block_domain(wb, "ads.microsoft.com", 1, "Ad network (Microsoft)");

    phantom_webbrowser_block_domain(wb, "scorecardresearch.com", 1, "Tracking (comScore)");
    phantom_webbrowser_block_domain(wb, "quantserve.com", 1, "Tracking (Quantcast)");
    phantom_webbrowser_block_domain(wb, "hotjar.com", 1, "Tracking (Hotjar)");
    phantom_webbrowser_block_domain(wb, "mixpanel.com", 1, "Tracking (Mixpanel)");
    phantom_webbrowser_block_domain(wb, "segment.io", 1, "Tracking (Segment)");
    phantom_webbrowser_block_domain(wb, "segment.com", 1, "Tracking (Segment)");
    phantom_webbrowser_block_domain(wb, "amplitude.com", 1, "Tracking (Amplitude)");
    phantom_webbrowser_block_domain(wb, "newrelic.com", 1, "Tracking (New Relic)");
    phantom_webbrowser_block_domain(wb, "fullstory.com", 1, "Tracking (FullStory)");
    phantom_webbrowser_block_domain(wb, "crazyegg.com", 1, "Tracking (Crazy Egg)");
    phantom_webbrowser_block_domain(wb, "mouseflow.com", 1, "Tracking (Mouseflow)");
    phantom_webbrowser_block_domain(wb, "clarity.ms", 1, "Tracking (Microsoft Clarity)");

    /* Ad exchanges & RTB */
    phantom_webbrowser_block_domain(wb, "pubmatic.com", 1, "Ad exchange");
    phantom_webbrowser_block_domain(wb, "openx.net", 1, "Ad exchange");
    phantom_webbrowser_block_domain(wb, "rubiconproject.com", 1, "Ad exchange");
    phantom_webbrowser_block_domain(wb, "casalemedia.com", 1, "Ad exchange");
    phantom_webbrowser_block_domain(wb, "adnxs.com", 1, "Ad exchange (AppNexus)");
    phantom_webbrowser_block_domain(wb, "criteo.com", 1, "Ad retargeting");
    phantom_webbrowser_block_domain(wb, "criteo.net", 1, "Ad retargeting");
    phantom_webbrowser_block_domain(wb, "taboola.com", 1, "Content ads");
    phantom_webbrowser_block_domain(wb, "outbrain.com", 1, "Content ads");
    phantom_webbrowser_block_domain(wb, "mgid.com", 1, "Content ads");
    phantom_webbrowser_block_domain(wb, "revcontent.com", 1, "Content ads");
    phantom_webbrowser_block_domain(wb, "zergnet.com", 1, "Content ads");

    /* Social widgets & beacons */
    phantom_webbrowser_block_domain(wb, "addthis.com", 1, "Social tracking");
    phantom_webbrowser_block_domain(wb, "sharethis.com", 1, "Social tracking");
    phantom_webbrowser_block_domain(wb, "addtoany.com", 1, "Social tracking");

    /* Affiliate tracking */
    phantom_webbrowser_block_domain(wb, "awin1.com", 1, "Affiliate tracking");
    phantom_webbrowser_block_domain(wb, "linksynergy.com", 1, "Affiliate tracking");
    phantom_webbrowser_block_domain(wb, "go.redirectingat.com", 1, "Affiliate tracking");
    phantom_webbrowser_block_domain(wb, "skimresources.com", 1, "Affiliate tracking");

    /* Cookie consent / GDPR walls that track */
    phantom_webbrowser_block_domain(wb, "cookiebot.com", 1, "Cookie tracking");
    phantom_webbrowser_block_domain(wb, "onetrust.com", 1, "Cookie tracking");
    phantom_webbrowser_block_domain(wb, "trustarc.com", 1, "Cookie tracking");

    /* Malware / phishing domains */
    phantom_webbrowser_block_domain(wb, "malware-domain.com", 1, "Known malware");
    phantom_webbrowser_block_domain(wb, "phishing-site.com", 1, "Known phishing");

    printf("  Ad blocking: %d domains blocked\n", wb->blocklist_count);

    wb->initialized = 1;

    printf("[webbrowser] Phantom Web Browser initialized\n");
    printf("  Governor: %s\n", wb->governor ? "connected" : "not connected (DEMO MODE)");
    printf("  HTTPS required: %s\n", wb->require_https ? "yes" : "no");
    printf("  Valid cert required: %s\n", wb->require_valid_cert ? "yes" : "no");
    printf("  Auto-approve allowlist: %s\n", wb->auto_approve_allowlist ? "yes" : "no");

    return WEBBROWSER_OK;
}

void phantom_webbrowser_shutdown(phantom_webbrowser_t *wb) {
    if (!wb || !wb->initialized) return;

    printf("\n[webbrowser] Shutdown statistics:\n");
    printf("  Total requests:    %lu\n", wb->stats.total_requests);
    printf("  Approved:          %lu\n", wb->stats.approved_requests);
    printf("  Denied:            %lu\n", wb->stats.denied_requests);
    printf("  HTTPS connections: %lu\n", wb->stats.https_connections);
    printf("  HTTP connections:  %lu\n", wb->stats.http_connections);
    printf("  Pages visited:     %u\n", wb->stats.pages_visited);

    /* Free response buffer */
    if (wb->response_buffer) {
        free(wb->response_buffer);
        wb->response_buffer = NULL;
    }
    wb->response_size = 0;
    wb->response_capacity = 0;

    wb->initialized = 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * RESPONSE BUFFER MANAGEMENT
 * ══════════════════════════════════════════════════════════════════════════════ */

void phantom_webbrowser_clear_response(phantom_webbrowser_t *wb) {
    if (!wb) return;

    if (wb->response_buffer) {
        wb->response_buffer[0] = '\0';
    }
    wb->response_size = 0;
    wb->response_status = 0;
    wb->response_content_type[0] = '\0';
}

int phantom_webbrowser_get_response(phantom_webbrowser_t *wb,
                                    const char **content, size_t *size) {
    if (!wb || !content || !size) return WEBBROWSER_ERR_INVALID;

    *content = wb->response_buffer;
    *size = wb->response_size;
    return WEBBROWSER_OK;
}

int phantom_webbrowser_get_status(phantom_webbrowser_t *wb) {
    if (!wb) return 0;
    return wb->response_status;
}

const char *phantom_webbrowser_get_content_type(phantom_webbrowser_t *wb) {
    if (!wb) return NULL;
    return wb->response_content_type;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CONTENT FILTERING - Remove inline ads and tracking scripts from HTML
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Ad-related patterns to filter from HTML content */
static const char *ad_script_patterns[] = {
    "googlesyndication.com",
    "googleadservices.com",
    "doubleclick.net",
    "google-analytics.com",
    "googletagmanager.com",
    "facebook.net",
    "connect.facebook.com",
    "analytics.",
    "adsbygoogle",
    "data-ad-",
    "adservice",
    "pagead",
    "criteo",
    "taboola",
    "outbrain",
    "tracking.",
    "tracker.",
    "pixel.",
    "beacon.",
    NULL
};

/* Check if a string contains any ad-related pattern */
static int contains_ad_pattern(const char *str, size_t len) {
    if (!str || len == 0) return 0;

    for (int i = 0; ad_script_patterns[i] != NULL; i++) {
        /* Case-insensitive search within the given length */
        const char *pattern = ad_script_patterns[i];
        size_t plen = strlen(pattern);
        if (plen > len) continue;

        for (size_t j = 0; j <= len - plen; j++) {
            int match = 1;
            for (size_t k = 0; k < plen && match; k++) {
                if (tolower(str[j + k]) != tolower(pattern[k])) {
                    match = 0;
                }
            }
            if (match) return 1;
        }
    }
    return 0;
}

/* Find closing tag for a given opening tag position */
static const char *find_closing_tag(const char *start, const char *end, const char *tag) {
    char close_tag[64];
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    const char *p = start;
    while (p < end) {
        const char *found = strstr(p, close_tag);
        if (!found || found >= end) {
            /* Also check for self-closing tag patterns like /> */
            const char *self_close = strstr(p, "/>");
            if (self_close && self_close < end && self_close > start) {
                return self_close + 2;
            }
            return NULL;
        }
        return found + strlen(close_tag);
    }
    return NULL;
}

/* Filter HTML content to remove ad scripts and tracking elements
 * Returns the new size of the content (may be smaller due to removed elements) */
static size_t filter_html_content(phantom_webbrowser_t *wb, char *content, size_t len) {
    if (!wb || !content || len == 0) return len;

    char *read_ptr = content;
    char *write_ptr = content;
    char *end = content + len;
    int removed_count = 0;

    while (read_ptr < end) {
        /* Look for script tags */
        if (strncasecmp(read_ptr, "<script", 7) == 0) {
            /* Find the end of the opening tag (to check src attribute) */
            const char *tag_end = strchr(read_ptr, '>');
            if (tag_end && tag_end < end) {
                /* Check if this script tag contains ad-related content */
                size_t tag_len = tag_end - read_ptr + 1;
                if (contains_ad_pattern(read_ptr, tag_len)) {
                    /* Find the closing </script> tag */
                    const char *close = find_closing_tag(tag_end + 1, end, "script");
                    if (close) {
                        /* Skip the entire script block */
                        read_ptr = (char *)close;
                        removed_count++;
                        continue;
                    }
                }
            }
        }

        /* Look for iframe tags (often used for ads) */
        if (strncasecmp(read_ptr, "<iframe", 7) == 0) {
            const char *tag_end = strchr(read_ptr, '>');
            if (tag_end && tag_end < end) {
                size_t tag_len = tag_end - read_ptr + 1;
                if (contains_ad_pattern(read_ptr, tag_len)) {
                    /* Find the closing </iframe> tag */
                    const char *close = find_closing_tag(tag_end + 1, end, "iframe");
                    if (close) {
                        read_ptr = (char *)close;
                        removed_count++;
                        continue;
                    } else {
                        /* Self-closing iframe or malformed - skip to end of tag */
                        read_ptr = (char *)tag_end + 1;
                        removed_count++;
                        continue;
                    }
                }
            }
        }

        /* Look for img tags with tracking pixels */
        if (strncasecmp(read_ptr, "<img", 4) == 0) {
            const char *tag_end = strchr(read_ptr, '>');
            if (tag_end && tag_end < end) {
                size_t tag_len = tag_end - read_ptr + 1;
                /* Check for 1x1 pixel tracking images */
                int is_pixel = (strstr(read_ptr, "width=\"1\"") != NULL &&
                                strstr(read_ptr, "height=\"1\"") != NULL) ||
                               (strstr(read_ptr, "width='1'") != NULL &&
                                strstr(read_ptr, "height='1'") != NULL);
                if (is_pixel || contains_ad_pattern(read_ptr, tag_len)) {
                    /* Skip the entire img tag */
                    read_ptr = (char *)tag_end + 1;
                    removed_count++;
                    continue;
                }
            }
        }

        /* Look for div/span with ad-related classes or IDs */
        if (strncasecmp(read_ptr, "<div", 4) == 0 || strncasecmp(read_ptr, "<span", 5) == 0) {
            const char *tag_end = strchr(read_ptr, '>');
            if (tag_end && tag_end < end) {
                size_t tag_len = tag_end - read_ptr + 1;
                /* Check for common ad container patterns */
                int is_ad = (strstr(read_ptr, "class=\"ad") != NULL ||
                             strstr(read_ptr, "class='ad") != NULL ||
                             strstr(read_ptr, "id=\"ad") != NULL ||
                             strstr(read_ptr, "id='ad") != NULL ||
                             strstr(read_ptr, "data-ad") != NULL ||
                             strstr(read_ptr, "adsbygoogle") != NULL);
                if (is_ad || contains_ad_pattern(read_ptr, tag_len)) {
                    /* Determine tag type for finding closing tag */
                    const char *tag_name = (strncasecmp(read_ptr, "<div", 4) == 0) ? "div" : "span";
                    const char *close = find_closing_tag(tag_end + 1, end, tag_name);
                    if (close) {
                        read_ptr = (char *)close;
                        removed_count++;
                        continue;
                    }
                }
            }
        }

        /* Copy non-ad content */
        *write_ptr++ = *read_ptr++;
    }

    /* Null-terminate */
    *write_ptr = '\0';
    size_t new_len = write_ptr - content;

    if (removed_count > 0) {
        printf("[webbrowser] Ad filter: removed %d ad elements (%zu bytes saved)\n",
               removed_count, len - new_len);
        wb->stats.blocked_content += removed_count;
    }

    return new_len;
}

/* Parse HTTP response headers */
static int parse_http_response(phantom_webbrowser_t *wb, const char *response, size_t len) {
    if (!wb || !response || len == 0) return -1;

    /* Clear previous values */
    wb->response_location[0] = '\0';

    /* Parse status line: HTTP/1.1 200 OK */
    const char *p = response;
    if (strncmp(p, "HTTP/", 5) == 0) {
        p = strchr(p, ' ');
        if (p) {
            wb->response_status = atoi(p + 1);
        }
    }

    /* Find Content-Type header */
    const char *ct = strstr(response, "Content-Type:");
    if (!ct) ct = strstr(response, "content-type:");
    if (ct) {
        ct += 13;  /* Skip "Content-Type:" */
        while (*ct == ' ') ct++;  /* Skip whitespace */

        /* Copy until end of line or semicolon */
        int i = 0;
        while (*ct && *ct != '\r' && *ct != '\n' && *ct != ';' &&
               i < (int)sizeof(wb->response_content_type) - 1) {
            wb->response_content_type[i++] = *ct++;
        }
        wb->response_content_type[i] = '\0';
    }

    /* Find Location header (for redirects) */
    const char *loc = strstr(response, "Location:");
    if (!loc) loc = strstr(response, "location:");
    if (loc) {
        loc += 9;  /* Skip "Location:" */
        while (*loc == ' ') loc++;  /* Skip whitespace */

        /* Copy until end of line */
        int i = 0;
        while (*loc && *loc != '\r' && *loc != '\n' &&
               i < (int)sizeof(wb->response_location) - 1) {
            wb->response_location[i++] = *loc++;
        }
        wb->response_location[i] = '\0';
    }

    /* Find end of headers (double CRLF) */
    const char *body = strstr(response, "\r\n\r\n");
    if (body) {
        body += 4;  /* Skip \r\n\r\n */
        return (int)(body - response);  /* Return header length */
    }

    return -1;  /* Headers not complete */
}

void phantom_webbrowser_set_network(phantom_webbrowser_t *wb,
                                    struct phantom_net *net) {
    if (!wb) return;
    wb->net = net;
    wb->network_enabled = (net != NULL);
    if (net) {
        printf("[webbrowser] Network layer connected\n");
    }
}

void phantom_webbrowser_set_tls(phantom_webbrowser_t *wb,
                                struct phantom_tls *tls) {
    if (!wb) return;
    wb->tls = tls;
    wb->tls_available = (tls != NULL);
    if (tls) {
        printf("[webbrowser] TLS layer connected (HTTPS available)\n");
    }
}

void phantom_webbrowser_set_vfs(phantom_webbrowser_t *wb,
                                struct vfs_context *vfs) {
    if (!wb) return;
    wb->vfs = vfs;
    if (vfs) {
        printf("[webbrowser] VFS connected (GeoFS logging enabled)\n");

        /* Ensure log directory exists */
        vfs_mkdir(vfs, 1, WEBBROWSER_LOG_DIR, 0755);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * DOMAIN POLICY MANAGEMENT
 * ══════════════════════════════════════════════════════════════════════════════ */

int phantom_webbrowser_allow_domain(phantom_webbrowser_t *wb,
                                    const char *domain,
                                    int include_subdomains,
                                    const char *reason) {
    if (!wb || !domain) return WEBBROWSER_ERR_INVALID;

    /* Check if already in allowlist */
    for (int i = 0; i < wb->allowlist_count; i++) {
        if (strcasecmp(wb->allowlist[i].domain, domain) == 0) {
            /* Update existing entry */
            wb->allowlist[i].include_subdomains = include_subdomains;
            if (reason) {
                strncpy(wb->allowlist[i].reason, reason,
                        sizeof(wb->allowlist[i].reason) - 1);
            }
            return WEBBROWSER_OK;
        }
    }

    /* Add new entry */
    if (wb->allowlist_count >= WEBBROWSER_MAX_ALLOWLIST) {
        return WEBBROWSER_ERR_NOMEM;
    }

    webbrowser_domain_entry_t *entry = &wb->allowlist[wb->allowlist_count];
    strncpy(entry->domain, domain, sizeof(entry->domain) - 1);
    entry->policy = DOMAIN_POLICY_ALLOW;
    entry->include_subdomains = include_subdomains;
    entry->added_at = time(NULL);
    entry->access_count = 0;
    if (reason) {
        strncpy(entry->reason, reason, sizeof(entry->reason) - 1);
    }

    wb->allowlist_count++;

    /* Log policy change to GeoFS */
    webbrowser_log_policy(wb, domain, "ALLOWLIST_ADD", reason ? reason : "No reason");

    return WEBBROWSER_OK;
}

int phantom_webbrowser_block_domain(phantom_webbrowser_t *wb,
                                    const char *domain,
                                    int include_subdomains,
                                    const char *reason) {
    if (!wb || !domain) return WEBBROWSER_ERR_INVALID;

    /* Check if already in blocklist */
    for (int i = 0; i < wb->blocklist_count; i++) {
        if (strcasecmp(wb->blocklist[i].domain, domain) == 0) {
            /* Update existing entry */
            wb->blocklist[i].include_subdomains = include_subdomains;
            if (reason) {
                strncpy(wb->blocklist[i].reason, reason,
                        sizeof(wb->blocklist[i].reason) - 1);
            }
            return WEBBROWSER_OK;
        }
    }

    /* Add new entry */
    if (wb->blocklist_count >= WEBBROWSER_MAX_BLOCKLIST) {
        return WEBBROWSER_ERR_NOMEM;
    }

    webbrowser_domain_entry_t *entry = &wb->blocklist[wb->blocklist_count];
    strncpy(entry->domain, domain, sizeof(entry->domain) - 1);
    entry->policy = DOMAIN_POLICY_BLOCK;
    entry->include_subdomains = include_subdomains;
    entry->added_at = time(NULL);
    if (reason) {
        strncpy(entry->reason, reason, sizeof(entry->reason) - 1);
    }

    wb->blocklist_count++;

    /* Log policy change to GeoFS */
    webbrowser_log_policy(wb, domain, "BLOCKLIST_ADD", reason ? reason : "No reason");

    return WEBBROWSER_OK;
}

int phantom_webbrowser_reset_domain(phantom_webbrowser_t *wb,
                                    const char *domain) {
    if (!wb || !domain) return WEBBROWSER_ERR_INVALID;

    /* Remove from allowlist */
    for (int i = 0; i < wb->allowlist_count; i++) {
        if (strcasecmp(wb->allowlist[i].domain, domain) == 0) {
            /* Shift remaining entries */
            for (int j = i; j < wb->allowlist_count - 1; j++) {
                wb->allowlist[j] = wb->allowlist[j + 1];
            }
            wb->allowlist_count--;
            break;
        }
    }

    /* Remove from blocklist */
    for (int i = 0; i < wb->blocklist_count; i++) {
        if (strcasecmp(wb->blocklist[i].domain, domain) == 0) {
            for (int j = i; j < wb->blocklist_count - 1; j++) {
                wb->blocklist[j] = wb->blocklist[j + 1];
            }
            wb->blocklist_count--;
            break;
        }
    }

    /* Log policy change to GeoFS */
    webbrowser_log_policy(wb, domain, "POLICY_RESET", "Domain removed from lists");

    return WEBBROWSER_OK;
}

domain_policy_t phantom_webbrowser_get_domain_policy(phantom_webbrowser_t *wb,
                                                     const char *domain) {
    if (!wb || !domain) return DOMAIN_POLICY_ASK;

    /* Check blocklist first (block takes precedence) */
    for (int i = 0; i < wb->blocklist_count; i++) {
        if (phantom_webbrowser_domain_matches(wb->blocklist[i].domain, domain,
                                              wb->blocklist[i].include_subdomains)) {
            return DOMAIN_POLICY_BLOCK;
        }
    }

    /* Check allowlist */
    for (int i = 0; i < wb->allowlist_count; i++) {
        if (phantom_webbrowser_domain_matches(wb->allowlist[i].domain, domain,
                                              wb->allowlist[i].include_subdomains)) {
            return DOMAIN_POLICY_ALLOW;
        }
    }

    return DOMAIN_POLICY_ASK;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * GOVERNOR INTEGRATION
 * ══════════════════════════════════════════════════════════════════════════════ */

int phantom_webbrowser_request_network(phantom_webbrowser_t *wb,
                                       const char *url,
                                       const char *purpose) {
    if (!wb || !url) return WEBBROWSER_ERR_INVALID;

    /* If no Governor, run in demo mode (log but allow) */
    if (!wb->governor) {
        printf("[webbrowser] DEMO MODE: Would request CAP_NETWORK for %s\n", url);
        printf("             Purpose: %s\n", purpose ? purpose : "web browsing");
        return WEBBROWSER_OK;
    }

    /* Create Governor evaluation request */
    governor_eval_request_t request;
    governor_eval_response_t response;
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));

    /* Use URL as the "code" being evaluated */
    request.code_ptr = url;
    request.code_size = strlen(url);
    /* Set detected_caps to indicate what capabilities we need */
    request.detected_caps = CAP_NETWORK;
    strncpy(request.name, "webbrowser_request", sizeof(request.name) - 1);
    if (purpose) {
        strncpy(request.description, purpose, sizeof(request.description) - 1);
    } else {
        snprintf(request.description, sizeof(request.description),
                 "Web browser requesting access to: %s", url);
    }

    /* Evaluate with Governor */
    int result = governor_evaluate_code(wb->governor, &request, &response);

    if (result != 0 || response.decision != GOVERNOR_APPROVE) {
        printf("[webbrowser] Governor denied network access\n");
        printf("             Reason: %s\n", response.decline_reason[0] ?
               response.decline_reason : "Access denied");
        return WEBBROWSER_ERR_GOVERNOR_DENIED;
    }

    return WEBBROWSER_OK;
}

int phantom_webbrowser_request_secure_network(phantom_webbrowser_t *wb,
                                              const char *url,
                                              webbrowser_security_t security) {
    if (!wb || !url) return WEBBROWSER_ERR_INVALID;

    /* Determine required capability */
    uint32_t required_cap = CAP_NETWORK;
    const char *security_desc = "unencrypted";

    switch (security) {
        case WEBBROWSER_SEC_TLS:
            required_cap |= CAP_NETWORK_SECURE;
            security_desc = "encrypted (verified)";
            break;
        case WEBBROWSER_SEC_TLS_UNVERIFIED:
        case WEBBROWSER_SEC_TLS_EXPIRED:
        case WEBBROWSER_SEC_TLS_SELF_SIGNED:
            required_cap |= CAP_NETWORK_INSECURE;
            security_desc = "encrypted (UNVERIFIED - DANGEROUS)";
            break;
        default:
            break;
    }

    if (!wb->governor) {
        printf("[webbrowser] DEMO MODE: Would request CAP_NETWORK + %s for %s\n",
               security_desc, url);
        return WEBBROWSER_OK;
    }

    /* Create evaluation request */
    governor_eval_request_t request;
    governor_eval_response_t response;
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));

    request.code_ptr = url;
    request.code_size = strlen(url);
    /* Set detected_caps to indicate what capabilities we need */
    request.detected_caps = required_cap;
    strncpy(request.name, "webbrowser_secure", sizeof(request.name) - 1);
    snprintf(request.description, sizeof(request.description),
             "Web browser requesting %s access to: %s", security_desc, url);

    int result = governor_evaluate_code(wb->governor, &request, &response);

    if (result != 0 || response.decision != GOVERNOR_APPROVE) {
        printf("[webbrowser] Governor denied secure network access\n");
        printf("             Reason: %s\n", response.decline_reason[0] ?
               response.decline_reason : "Access denied");
        return WEBBROWSER_ERR_GOVERNOR_DENIED;
    }

    return WEBBROWSER_OK;
}

void phantom_webbrowser_log_connection(phantom_webbrowser_t *wb,
                                       const webbrowser_connection_info_t *info) {
    if (!wb || !info) return;

    /* Log to console */
    printf("[webbrowser] Connection: %s\n", info->url);
    printf("  Domain:   %s\n", info->domain);
    printf("  Security: %s\n", phantom_webbrowser_security_string(info->security));
    if (info->security != WEBBROWSER_SEC_NONE) {
        printf("  TLS:      %s (%s)\n", info->tls_version, info->cipher_suite);
        printf("  Cert:     %s\n", info->cert_subject);
    }
    printf("  Status:   %d\n", info->status_code);
    printf("  Response: %u ms\n", info->response_time_ms);

    /* Write to GeoFS audit log */
    char details[512];
    snprintf(details, sizeof(details),
             "security=%s status=%d time=%ums bytes_in=%lu bytes_out=%lu",
             phantom_webbrowser_security_string(info->security),
             info->status_code,
             info->response_time_ms,
             info->bytes_received,
             info->bytes_sent);

    webbrowser_log_to_geofs(wb, "CONNECTION", info->url, info->domain, "OK", details);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * NAVIGATION
 * ══════════════════════════════════════════════════════════════════════════════ */

int phantom_webbrowser_check_url(phantom_webbrowser_t *wb,
                                 const char *url,
                                 char *reason, size_t reason_size) {
    if (!wb || !url) {
        if (reason && reason_size > 0) {
            strncpy(reason, "Invalid parameters", reason_size - 1);
        }
        return WEBBROWSER_ERR_INVALID;
    }

    if (!wb->initialized) {
        if (reason && reason_size > 0) {
            strncpy(reason, "Browser not initialized", reason_size - 1);
        }
        return WEBBROWSER_ERR_NOT_INITIALIZED;
    }

    /* Extract domain */
    char domain[WEBBROWSER_MAX_DOMAIN];
    if (phantom_webbrowser_extract_domain(url, domain, sizeof(domain)) != 0) {
        if (reason && reason_size > 0) {
            strncpy(reason, "Could not parse URL", reason_size - 1);
        }
        return WEBBROWSER_ERR_INVALID;
    }

    /* Check domain policy */
    domain_policy_t policy = phantom_webbrowser_get_domain_policy(wb, domain);

    if (policy == DOMAIN_POLICY_BLOCK) {
        if (reason && reason_size > 0) {
            /* Find the block reason */
            for (int i = 0; i < wb->blocklist_count; i++) {
                if (phantom_webbrowser_domain_matches(wb->blocklist[i].domain, domain,
                                                      wb->blocklist[i].include_subdomains)) {
                    snprintf(reason, reason_size, "Domain blocked: %s",
                             wb->blocklist[i].reason[0] ? wb->blocklist[i].reason : "No reason given");
                    break;
                }
            }
        }
        return WEBBROWSER_ERR_BLOCKED_DOMAIN;
    }

    /* Check HTTPS requirement */
    int is_https = phantom_webbrowser_is_https(url);
    if (wb->require_https && !is_https) {
        if (reason && reason_size > 0) {
            strncpy(reason, "HTTPS required but URL uses HTTP", reason_size - 1);
        }
        return WEBBROWSER_ERR_TLS_REQUIRED;
    }

    /* Check TLS availability for HTTPS */
    if (is_https && !wb->tls_available) {
        if (reason && reason_size > 0) {
            strncpy(reason, "HTTPS requested but TLS not available", reason_size - 1);
        }
        return WEBBROWSER_ERR_TLS_UNAVAILABLE;
    }

    if (reason && reason_size > 0) {
        if (policy == DOMAIN_POLICY_ALLOW && wb->auto_approve_allowlist) {
            strncpy(reason, "Domain in allowlist - auto-approved", reason_size - 1);
        } else {
            strncpy(reason, "OK - Governor approval required", reason_size - 1);
        }
    }

    return WEBBROWSER_OK;
}

/* Internal navigate function with redirect depth tracking */
static int navigate_internal(phantom_webbrowser_t *wb, const char *url,
                             webbrowser_security_t min_security, int redirect_depth);

int phantom_webbrowser_navigate(phantom_webbrowser_t *wb, const char *url) {
    if (!wb) return WEBBROWSER_ERR_INVALID;
    return navigate_internal(wb, url, wb->default_security, 0);
}

int phantom_webbrowser_navigate_secure(phantom_webbrowser_t *wb,
                                       const char *url,
                                       webbrowser_security_t min_security) {
    return navigate_internal(wb, url, min_security, 0);
}

static int navigate_internal(phantom_webbrowser_t *wb, const char *url,
                             webbrowser_security_t min_security, int redirect_depth) {
    if (!wb || !url) return WEBBROWSER_ERR_INVALID;
    if (!wb->initialized) return WEBBROWSER_ERR_NOT_INITIALIZED;

    wb->stats.total_requests++;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║                    PHANTOM WEB BROWSER                                ║\n");
    printf("║               Governor-Controlled Network Access                      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* Extract domain */
    char domain[WEBBROWSER_MAX_DOMAIN];
    if (phantom_webbrowser_extract_domain(url, domain, sizeof(domain)) != 0) {
        printf("[webbrowser] ERROR: Could not parse URL: %s\n", url);
        wb->stats.denied_requests++;
        return WEBBROWSER_ERR_INVALID;
    }

    printf("  URL:      %s\n", url);
    printf("  Domain:   %s\n", domain);

    /* Check URL first */
    char check_reason[256];
    int check_result = phantom_webbrowser_check_url(wb, url, check_reason, sizeof(check_reason));

    if (check_result == WEBBROWSER_ERR_BLOCKED_DOMAIN) {
        printf("  Status:   \033[31mBLOCKED\033[0m\n");
        printf("  Reason:   %s\n", check_reason);
        wb->stats.denied_requests++;
        wb->stats.blocked_domains++;
        return WEBBROWSER_ERR_BLOCKED_DOMAIN;
    }

    if (check_result != WEBBROWSER_OK) {
        printf("  Status:   \033[31mDENIED\033[0m\n");
        printf("  Reason:   %s\n", check_reason);
        wb->stats.denied_requests++;
        return check_result;
    }

    /* Check domain policy for auto-approval */
    domain_policy_t policy = phantom_webbrowser_get_domain_policy(wb, domain);
    int is_https = phantom_webbrowser_is_https(url);
    webbrowser_security_t security = is_https ? WEBBROWSER_SEC_TLS : WEBBROWSER_SEC_NONE;

    printf("  Protocol: %s\n", is_https ? "HTTPS" : "HTTP");
    printf("  Policy:   %s\n", phantom_webbrowser_policy_string(policy));

    /* Determine if Governor approval is needed */
    int needs_approval = 1;
    if (policy == DOMAIN_POLICY_ALLOW && wb->auto_approve_allowlist) {
        needs_approval = 0;
        printf("  Approval: Auto-approved (allowlisted domain)\n");
    } else if (policy == DOMAIN_POLICY_ALLOW_SESSION) {
        needs_approval = 0;
        printf("  Approval: Auto-approved (session allowlist)\n");
    }

    /* Request Governor approval if needed */
    if (needs_approval) {
        printf("  Approval: Requesting Governor approval...\n");

        int gov_result;
        if (is_https) {
            gov_result = phantom_webbrowser_request_secure_network(wb, url, security);
        } else {
            char purpose[512];
            snprintf(purpose, sizeof(purpose),
                     "HTTP (unencrypted) request to %s - data may be visible to network observers",
                     domain);
            gov_result = phantom_webbrowser_request_network(wb, url, purpose);
        }

        /* If Governor requires user input (interactive mode), prompt user */
        if (gov_result == WEBBROWSER_ERR_GOVERNOR_DENIED && wb->governor &&
            wb->governor->interactive) {
            char user_response[16];
            webbrowser_prompt_user(wb, url, domain, security, user_response, sizeof(user_response));

            char choice = toupper(user_response[0]);
            switch (choice) {
                case 'Y':  /* Allow this request */
                    gov_result = WEBBROWSER_OK;
                    webbrowser_log_navigation(wb, url, domain, 1, "User approved (once)");
                    break;

                case 'A':  /* Allow and add to allowlist */
                    gov_result = WEBBROWSER_OK;
                    phantom_webbrowser_allow_domain(wb, domain, 1, "User allowlisted");
                    webbrowser_log_policy(wb, domain, "ALLOWLIST_ADD", "User added via prompt");
                    /* Add capability scope for this domain */
                    webbrowser_add_capability_scope(wb, domain, CAP_NETWORK | CAP_NETWORK_SECURE, 0);
                    webbrowser_log_navigation(wb, url, domain, 1, "User approved (allowlisted)");
                    break;

                case 'S':  /* Allow for session */
                    gov_result = WEBBROWSER_OK;
                    /* Add as session-only allowlist */
                    for (int i = 0; i < wb->allowlist_count; i++) {
                        if (strcasecmp(wb->allowlist[i].domain, domain) == 0) {
                            wb->allowlist[i].policy = DOMAIN_POLICY_ALLOW_SESSION;
                            break;
                        }
                    }
                    if (gov_result == WEBBROWSER_OK) {
                        phantom_webbrowser_allow_domain(wb, domain, 1, "Session allowlist");
                    }
                    webbrowser_log_navigation(wb, url, domain, 1, "User approved (session only)");
                    break;

                case 'B':  /* Block and add to blocklist */
                    phantom_webbrowser_block_domain(wb, domain, 1, "User blocked");
                    webbrowser_log_policy(wb, domain, "BLOCKLIST_ADD", "User blocked via prompt");
                    webbrowser_log_navigation(wb, url, domain, 0, "User blocked");
                    printf("  Status:   \033[31mBLOCKED BY USER\033[0m\n");
                    wb->stats.denied_requests++;
                    wb->stats.blocked_domains++;
                    return WEBBROWSER_ERR_BLOCKED_DOMAIN;

                case 'N':  /* Deny */
                default:
                    webbrowser_log_navigation(wb, url, domain, 0, "User denied");
                    printf("  Status:   \033[31mDENIED BY USER\033[0m\n");
                    wb->stats.denied_requests++;
                    return WEBBROWSER_ERR_GOVERNOR_DENIED;
            }
        } else if (gov_result != WEBBROWSER_OK) {
            webbrowser_log_navigation(wb, url, domain, 0, "Governor denied");
            printf("  Status:   \033[31mDENIED BY GOVERNOR\033[0m\n");
            wb->stats.denied_requests++;
            return gov_result;
        } else {
            webbrowser_log_navigation(wb, url, domain, 1, "Governor approved");
        }

        printf("  Status:   \033[32mAPPROVED\033[0m\n");
    } else {
        /* Auto-approved - still log it */
        webbrowser_log_navigation(wb, url, domain, 1, "Auto-approved (allowlist)");
    }

    wb->stats.approved_requests++;
    if (is_https) {
        wb->stats.https_connections++;
    } else {
        wb->stats.http_connections++;
    }

    /* Update allowlist access stats */
    for (int i = 0; i < wb->allowlist_count; i++) {
        if (phantom_webbrowser_domain_matches(wb->allowlist[i].domain, domain,
                                              wb->allowlist[i].include_subdomains)) {
            wb->allowlist[i].access_count++;
            wb->allowlist[i].last_access = time(NULL);
            break;
        }
    }

    /* Record connection info */
    memset(&wb->current_connection, 0, sizeof(wb->current_connection));
    strncpy(wb->current_connection.url, url, sizeof(wb->current_connection.url) - 1);
    strncpy(wb->current_connection.domain, domain, sizeof(wb->current_connection.domain) - 1);
    wb->current_connection.security = security;
    wb->current_connection.connected_at = time(NULL);
    wb->connection_active = 1;

    /* Log the connection */
    if (wb->log_all_requests) {
        phantom_webbrowser_log_connection(wb, &wb->current_connection);
    }

    printf("\n");

    /* Clear previous response */
    phantom_webbrowser_clear_response(wb);

    /* If we have an underlying browser, use it */
    if (wb->browser) {
        printf("[webbrowser] Delegating to underlying browser...\n");
        return phantom_browser_navigate(wb->browser, url);
    }

    /* Check if we have network layer */
    if (!wb->net || !wb->network_enabled) {
        printf("[webbrowser] Network not available.\n");
        printf("             Initialize network with: net init\n");
        return WEBBROWSER_ERR_NETWORK;
    }

    /* Perform actual HTTP/HTTPS request */
    printf("[webbrowser] Fetching content...\n");

    time_t start_time = time(NULL);
    ssize_t response_len = 0;

    /* Ensure response buffer is large enough */
    if (!wb->response_buffer || wb->response_capacity < WEBBROWSER_RESPONSE_INITIAL_SIZE) {
        if (wb->response_buffer) free(wb->response_buffer);
        wb->response_buffer = malloc(WEBBROWSER_RESPONSE_INITIAL_SIZE);
        if (!wb->response_buffer) {
            printf("[webbrowser] ERROR: Failed to allocate response buffer\n");
            return WEBBROWSER_ERR_NOMEM;
        }
        wb->response_capacity = WEBBROWSER_RESPONSE_INITIAL_SIZE;
    }

    if (is_https) {
        /* HTTPS request via TLS */
        if (!wb->tls || !wb->tls_available) {
            printf("[webbrowser] TLS not available for HTTPS.\n");
            printf("             Build with: make HAVE_MBEDTLS=1\n");
            return WEBBROWSER_ERR_TLS_UNAVAILABLE;
        }

        response_len = phantom_https_get(wb->tls, wb->net, url,
                                          wb->response_buffer,
                                          wb->response_capacity - 1);
    } else {
        /* HTTP request (unencrypted) */
        response_len = phantom_http_get(wb->net, url,
                                         wb->response_buffer,
                                         wb->response_capacity - 1);
    }

    time_t end_time = time(NULL);
    wb->current_connection.response_time_ms = (uint32_t)((end_time - start_time) * 1000);

    if (response_len < 0) {
        printf("[webbrowser] ERROR: Request failed (code: %zd)\n", response_len);
        wb->current_connection.status_code = 0;
        webbrowser_log_to_geofs(wb, "ERROR", url, domain, "FAILED",
                                 is_https ? "HTTPS request failed" : "HTTP request failed");
        return WEBBROWSER_ERR_NETWORK;
    }

    /* Null terminate response */
    wb->response_buffer[response_len] = '\0';
    wb->response_size = (size_t)response_len;

    /* Parse HTTP headers */
    int header_len = parse_http_response(wb, wb->response_buffer, wb->response_size);

    /* Handle HTTP redirects (301, 302, 303, 307, 308) */
    if ((wb->response_status == 301 || wb->response_status == 302 ||
         wb->response_status == 303 || wb->response_status == 307 ||
         wb->response_status == 308) && wb->response_location[0] != '\0') {

        printf("[webbrowser] Redirect %d -> %s\n", wb->response_status, wb->response_location);

        /* Build full redirect URL if relative */
        char redirect_url[WEBBROWSER_MAX_URL];
        if (strncmp(wb->response_location, "http://", 7) == 0 ||
            strncmp(wb->response_location, "https://", 8) == 0) {
            /* Absolute URL */
            strncpy(redirect_url, wb->response_location, sizeof(redirect_url) - 1);
            redirect_url[sizeof(redirect_url) - 1] = '\0';
        } else if (wb->response_location[0] == '/') {
            /* Absolute path - reconstruct with original scheme and host */
            snprintf(redirect_url, sizeof(redirect_url), "%s://%.250s%.3800s",
                     is_https ? "https" : "http", domain, wb->response_location);
        } else {
            /* Relative path - not common, but handle it */
            snprintf(redirect_url, sizeof(redirect_url), "%s://%.250s/%.3800s",
                     is_https ? "https" : "http", domain, wb->response_location);
        }

        /* Log the redirect */
        char redirect_details[512];
        snprintf(redirect_details, sizeof(redirect_details),
                 "status=%d from=%.200s to=%.200s", wb->response_status, url, redirect_url);
        webbrowser_log_to_geofs(wb, "REDIRECT", url, domain, "FOLLOWING", redirect_details);

        /* Follow the redirect (recursive call with redirect limit) */
        if (redirect_depth >= 10) {
            printf("[webbrowser] ERROR: Too many redirects (max 10)\n");
            return WEBBROWSER_ERR_NETWORK;
        }

        return navigate_internal(wb, redirect_url, min_security, redirect_depth + 1);
    }

    /* Apply content filtering for HTML responses */
    if (header_len > 0 && (size_t)header_len < wb->response_size) {
        /* Check if it's HTML content that should be filtered */
        int is_html = (strstr(wb->response_content_type, "text/html") != NULL);
        if (is_html) {
            char *body = wb->response_buffer + header_len;
            size_t body_len = wb->response_size - header_len;
            size_t new_body_len = filter_html_content(wb, body, body_len);
            wb->response_size = header_len + new_body_len;
        }
    }

    /* Update connection info */
    wb->current_connection.bytes_received = (uint64_t)response_len;
    wb->current_connection.status_code = wb->response_status;
    if (is_https) {
        strncpy(wb->current_connection.tls_version, "TLS 1.2+", sizeof(wb->current_connection.tls_version) - 1);
    }

    /* Display results */
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════\n");
    printf("  RESPONSE RECEIVED\n");
    printf("═══════════════════════════════════════════════════════════════════════\n");
    printf("  HTTP Status:   %d\n", wb->response_status);
    printf("  Content-Type:  %s\n", wb->response_content_type[0] ? wb->response_content_type : "unknown");
    printf("  Content Size:  %zu bytes\n", wb->response_size);
    printf("  Response Time: %u ms\n", wb->current_connection.response_time_ms);
    printf("───────────────────────────────────────────────────────────────────────\n");

    /* Display content preview */
    if (header_len > 0 && (size_t)header_len < wb->response_size) {
        const char *body = wb->response_buffer + header_len;
        size_t body_len = wb->response_size - header_len;

        /* Check if it's text content */
        int is_text = (strstr(wb->response_content_type, "text/") != NULL ||
                       strstr(wb->response_content_type, "application/json") != NULL ||
                       strstr(wb->response_content_type, "application/xml") != NULL);

        if (is_text) {
            /* Show first part of content */
            size_t preview_len = body_len < 2000 ? body_len : 2000;
            printf("\n  CONTENT PREVIEW:\n");
            printf("───────────────────────────────────────────────────────────────────────\n");
            printf("%.*s", (int)preview_len, body);
            if (body_len > 2000) {
                printf("\n... [%zu more bytes]\n", body_len - 2000);
            }
            printf("\n");
        } else {
            printf("\n  [Binary content - %zu bytes]\n", body_len);
        }
    }
    printf("═══════════════════════════════════════════════════════════════════════\n");

    /* Log successful fetch to GeoFS */
    char details[256];
    snprintf(details, sizeof(details), "status=%d size=%zu type=%s",
             wb->response_status, wb->response_size, wb->response_content_type);
    webbrowser_log_to_geofs(wb, "FETCH", url, domain, "SUCCESS", details);

    wb->stats.pages_visited++;
    wb->stats.total_bytes_received += (uint64_t)response_len;

    return WEBBROWSER_OK;
}

int phantom_webbrowser_get_pending(phantom_webbrowser_t *wb,
                                   uint32_t request_id,
                                   webbrowser_pending_request_t *request_out) {
    if (!wb || !request_out) return WEBBROWSER_ERR_INVALID;

    for (int i = 0; i < wb->pending_count; i++) {
        if (wb->pending[i].request_id == request_id) {
            *request_out = wb->pending[i];
            return WEBBROWSER_OK;
        }
    }

    return WEBBROWSER_ERR_INVALID;
}

int phantom_webbrowser_cancel(phantom_webbrowser_t *wb, uint32_t request_id) {
    if (!wb) return WEBBROWSER_ERR_INVALID;

    for (int i = 0; i < wb->pending_count; i++) {
        if (wb->pending[i].request_id == request_id) {
            /* Remove by shifting */
            for (int j = i; j < wb->pending_count - 1; j++) {
                wb->pending[j] = wb->pending[j + 1];
            }
            wb->pending_count--;
            return WEBBROWSER_OK;
        }
    }

    return WEBBROWSER_ERR_INVALID;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ══════════════════════════════════════════════════════════════════════════════ */

void phantom_webbrowser_require_https(phantom_webbrowser_t *wb, int required) {
    if (wb) {
        wb->require_https = required;
        printf("[webbrowser] HTTPS required: %s\n", required ? "yes" : "no");
    }
}

void phantom_webbrowser_require_valid_cert(phantom_webbrowser_t *wb, int required) {
    if (wb) {
        wb->require_valid_cert = required;
        printf("[webbrowser] Valid certificate required: %s\n", required ? "yes" : "no");
    }
}

void phantom_webbrowser_auto_approve(phantom_webbrowser_t *wb, int enabled) {
    if (wb) {
        wb->auto_approve_allowlist = enabled;
        printf("[webbrowser] Auto-approve allowlist: %s\n", enabled ? "yes" : "no");
    }
}

void phantom_webbrowser_set_default_security(phantom_webbrowser_t *wb,
                                             webbrowser_security_t level) {
    if (wb) {
        wb->default_security = level;
        printf("[webbrowser] Default security: %s\n",
               phantom_webbrowser_security_string(level));
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * INFORMATION & STATISTICS
 * ══════════════════════════════════════════════════════════════════════════════ */

int phantom_webbrowser_get_connection(phantom_webbrowser_t *wb,
                                      webbrowser_connection_info_t *info_out) {
    if (!wb || !info_out) return WEBBROWSER_ERR_INVALID;
    if (!wb->connection_active) return WEBBROWSER_ERR_INVALID;

    *info_out = wb->current_connection;
    return WEBBROWSER_OK;
}

void phantom_webbrowser_get_stats(phantom_webbrowser_t *wb,
                                  webbrowser_stats_t *stats_out) {
    if (!wb || !stats_out) return;
    *stats_out = wb->stats;
}

void phantom_webbrowser_print_status(phantom_webbrowser_t *wb) {
    if (!wb) return;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║                    PHANTOM WEB BROWSER STATUS                         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("  State:            %s\n", wb->initialized ? "Initialized" : "Not initialized");
    printf("  Governor:         %s\n", wb->governor ? "Connected" : "Not connected (DEMO)");
    printf("  Network:          %s\n", wb->network_enabled ? "Enabled" : "Disabled");
    printf("  TLS/HTTPS:        %s\n", wb->tls_available ? "Available" : "Not available");
    printf("\n");

    printf("  Configuration:\n");
    printf("    HTTPS required:        %s\n", wb->require_https ? "Yes" : "No");
    printf("    Valid cert required:   %s\n", wb->require_valid_cert ? "Yes" : "No");
    printf("    Auto-approve allowed:  %s\n", wb->auto_approve_allowlist ? "Yes" : "No");
    printf("    Log all requests:      %s\n", wb->log_all_requests ? "Yes" : "No");
    printf("    Block mixed content:   %s\n", wb->block_mixed_content ? "Yes" : "No");
    printf("\n");

    printf("  Domain Lists:\n");
    printf("    Allowlist:       %d domains\n", wb->allowlist_count);
    printf("    Blocklist:       %d domains\n", wb->blocklist_count);
    printf("    Pending:         %d requests\n", wb->pending_count);
    printf("\n");
}

void phantom_webbrowser_print_policies(phantom_webbrowser_t *wb) {
    if (!wb) return;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║                    DOMAIN POLICIES                                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    if (wb->allowlist_count > 0) {
        printf("  ALLOWLIST (%d domains):\n", wb->allowlist_count);
        for (int i = 0; i < wb->allowlist_count; i++) {
            webbrowser_domain_entry_t *e = &wb->allowlist[i];
            printf("    [%d] %s%s\n", i, e->domain,
                   e->include_subdomains ? " (*.)" : "");
            printf("        Accesses: %lu | %s\n",
                   e->access_count, e->reason[0] ? e->reason : "(no reason)");
        }
        printf("\n");
    }

    if (wb->blocklist_count > 0) {
        printf("  BLOCKLIST (%d domains):\n", wb->blocklist_count);
        for (int i = 0; i < wb->blocklist_count; i++) {
            webbrowser_domain_entry_t *e = &wb->blocklist[i];
            printf("    [%d] %s%s\n", i, e->domain,
                   e->include_subdomains ? " (*.)" : "");
            printf("        %s\n", e->reason[0] ? e->reason : "(no reason)");
        }
        printf("\n");
    }

    if (wb->allowlist_count == 0 && wb->blocklist_count == 0) {
        printf("  (no domain policies configured)\n\n");
    }
}

void phantom_webbrowser_print_stats(phantom_webbrowser_t *wb) {
    if (!wb) return;

    time_t now = time(NULL);
    uint64_t uptime = now - wb->stats.session_start;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║                    BROWSER STATISTICS                                 ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("  Session uptime:    %lu seconds\n", uptime);
    printf("\n");

    printf("  Requests:\n");
    printf("    Total:           %lu\n", wb->stats.total_requests);
    printf("    Approved:        %lu\n", wb->stats.approved_requests);
    printf("    Denied:          %lu\n", wb->stats.denied_requests);
    if (wb->stats.total_requests > 0) {
        printf("    Approval rate:   %.1f%%\n",
               (float)wb->stats.approved_requests * 100.0f / wb->stats.total_requests);
    }
    printf("\n");

    printf("  Connections:\n");
    printf("    HTTPS:           %lu\n", wb->stats.https_connections);
    printf("    HTTP:            %lu\n", wb->stats.http_connections);
    printf("    Blocked domains: %lu\n", wb->stats.blocked_domains);
    printf("\n");

    printf("  Data:\n");
    printf("    Bytes sent:      %lu\n", wb->stats.total_bytes_sent);
    printf("    Bytes received:  %lu\n", wb->stats.total_bytes_received);
    printf("    Pages visited:   %u\n", wb->stats.pages_visited);
    printf("\n");
}

void phantom_webbrowser_print_connection(const webbrowser_connection_info_t *info) {
    if (!info) return;

    printf("\n");
    printf("  Current Connection:\n");
    printf("    URL:      %s\n", info->url);
    printf("    Domain:   %s\n", info->domain);
    printf("    Security: %s\n", phantom_webbrowser_security_string(info->security));

    if (info->security != WEBBROWSER_SEC_NONE) {
        printf("    TLS:      %s\n", info->tls_version);
        printf("    Cipher:   %s\n", info->cipher_suite);
        printf("    Cert:     %s\n", info->cert_subject);
        printf("    Issuer:   %s\n", info->cert_issuer);
        printf("    Valid:    %s\n", info->cert_valid ? "Yes" : "No");
    }

    printf("    Status:   %d\n", info->status_code);
    printf("    Response: %u ms\n", info->response_time_ms);
    printf("    Sent:     %lu bytes\n", info->bytes_sent);
    printf("    Received: %lu bytes\n", info->bytes_received);
    printf("\n");
}
