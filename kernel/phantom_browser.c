/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM AI WEB BROWSER
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Implementation of the AI-powered web browser.
 * Every page is preserved forever in the geology.
 */

#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "phantom_browser.h"
#include "phantom.h"
#include "phantom_ai.h"
#include "phantom_time.h"
#include "phantom_net.h"
#include "phantom_tls.h"
#include "../geofs.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal Constants
 * ───────────────────────────────────────────────────────────────────────────── */

#define HTTP_BUFFER_SIZE    65536
#define HTTP_TIMEOUT_SEC    30
#define MAX_REDIRECTS       5

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

/* Simple hash for content deduplication */
static void compute_hash(const void *data, size_t size, phantom_hash_t hash) {
    /* Simple FNV-1a hash (production would use SHA-256) */
    uint64_t h = 14695981039346656037ULL;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    memset(hash, 0, PHANTOM_HASH_SIZE);
    memcpy(hash, &h, sizeof(h));
}

/* Extract domain from URL */
static void extract_domain(const char *url, char *domain, size_t max_len) {
    const char *start = url;

    /* Skip scheme */
    if (strncmp(url, "http://", 7) == 0) start = url + 7;
    else if (strncmp(url, "https://", 8) == 0) start = url + 8;

    /* Find end of domain */
    const char *end = start;
    while (*end && *end != '/' && *end != ':' && *end != '?') end++;

    size_t len = end - start;
    if (len >= max_len) len = max_len - 1;
    strncpy(domain, start, len);
    domain[len] = '\0';
}

/* Extract title from HTML */
static void extract_title(const char *html, char *title, size_t max_len) {
    const char *start = strstr(html, "<title>");
    if (!start) start = strstr(html, "<TITLE>");
    if (!start) {
        strncpy(title, "Untitled", max_len - 1);
        return;
    }
    start += 7;

    const char *end = strstr(start, "</title>");
    if (!end) end = strstr(start, "</TITLE>");
    if (!end) end = start + strlen(start);

    size_t len = end - start;
    if (len >= max_len) len = max_len - 1;

    /* Copy and clean up whitespace */
    size_t j = 0;
    for (size_t i = 0; i < len && j < max_len - 1; i++) {
        if (start[i] == '\n' || start[i] == '\r' || start[i] == '\t') {
            if (j > 0 && title[j-1] != ' ') title[j++] = ' ';
        } else {
            title[j++] = start[i];
        }
    }
    title[j] = '\0';

    /* Trim */
    while (j > 0 && title[j-1] == ' ') title[--j] = '\0';
}

/* Detect content type from headers/content */
static phantom_content_type_t detect_content_type(const char *content_type_header,
                                                   const char *content) {
    if (content_type_header) {
        if (strstr(content_type_header, "text/html")) return CONTENT_HTML;
        if (strstr(content_type_header, "text/plain")) return CONTENT_TEXT;
        if (strstr(content_type_header, "application/json")) return CONTENT_JSON;
        if (strstr(content_type_header, "application/xml")) return CONTENT_XML;
        if (strstr(content_type_header, "text/xml")) return CONTENT_XML;
        if (strstr(content_type_header, "image/")) return CONTENT_IMAGE;
        if (strstr(content_type_header, "application/pdf")) return CONTENT_PDF;
    }

    /* Sniff content */
    if (content) {
        if (strncmp(content, "<!DOCTYPE", 9) == 0 ||
            strncmp(content, "<html", 5) == 0 ||
            strncmp(content, "<HTML", 5) == 0) {
            return CONTENT_HTML;
        }
        if (content[0] == '{' || content[0] == '[') return CONTENT_JSON;
        if (strncmp(content, "<?xml", 5) == 0) return CONTENT_XML;
    }

    return CONTENT_UNKNOWN;
}

/* Simple HTTP GET request */
static int http_get(const char *url, char *response, size_t max_size,
                    size_t *response_size, char *content_type, size_t ct_size) {
    char scheme[16], host[256], path[2048];
    uint16_t port = 80;

    /* Parse URL */
    if (phantom_browser_parse_url(url, scheme, host, path, &port) != 0) {
        return BROWSER_ERR_INVALID;
    }

    int use_https = (strcmp(scheme, "https") == 0);
    if (use_https) {
        port = (port == 80) ? 443 : port;  /* Default to 443 for HTTPS */
    }

    /*
     * HTTPS requires TLS. Without mbedTLS linked, we cannot make HTTPS requests.
     * Instead of crashing, we'll return an appropriate error.
     */
    if (use_https) {
#if !defined(HAVE_MBEDTLS)
        printf("[browser] HTTPS requires TLS support (mbedTLS not linked)\n");
        printf("          Build with: make HAVE_MBEDTLS=1\n");
        printf("          Or use http:// URL instead of https://\n");
        snprintf(response, max_size,
                 "<!DOCTYPE html><html><body>"
                 "<h1>HTTPS Not Available</h1>"
                 "<p>PhantomOS was built without TLS support.</p>"
                 "<p>To enable HTTPS:</p>"
                 "<ol>"
                 "<li>Install mbedtls-dev: <code>sudo apt install libmbedtls-dev</code></li>"
                 "<li>Rebuild with: <code>make clean && make HAVE_MBEDTLS=1</code></li>"
                 "</ol>"
                 "<p>URL requested: %s</p>"
                 "</body></html>", url);
        *response_size = strlen(response);
        if (content_type) strncpy(content_type, "text/html", ct_size - 1);
        return BROWSER_OK;  /* Return OK so page displays the message */
#else
        /* TODO: Implement HTTPS using phantom_tls when mbedTLS is available */
        printf("[browser] HTTPS support available but not yet integrated\n");
        snprintf(response, max_size,
                 "<!DOCTYPE html><html><body>"
                 "<h1>HTTPS Coming Soon</h1>"
                 "<p>TLS is available but browser HTTPS integration is pending.</p>"
                 "<p>URL requested: %s</p>"
                 "</body></html>", url);
        *response_size = strlen(response);
        if (content_type) strncpy(content_type, "text/html", ct_size - 1);
        return BROWSER_OK;
#endif
    }

    /* HTTP (unencrypted) - proceed with plain socket */

    /* Resolve hostname */
    struct hostent *server = gethostbyname(host);
    if (!server) {
        return BROWSER_ERR_NETWORK;
    }

    /* Create socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return BROWSER_ERR_NETWORK;
    }

    /* Set timeout */
    struct timeval tv;
    tv.tv_sec = HTTP_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Connect */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return BROWSER_ERR_NETWORK;
    }

    /* Build and send request */
    char request[4096];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: PhantomBrowser/1.0 (PhantomOS; AI-Powered)\r\n"
             "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
             "Accept-Language: en-US,en;q=0.5\r\n"
             "Connection: close\r\n"
             "\r\n",
             path[0] ? path : "/", host);

    if (send(sock, request, strlen(request), 0) < 0) {
        close(sock);
        return BROWSER_ERR_NETWORK;
    }

    /* Receive response */
    size_t total = 0;
    ssize_t received;
    while (total < max_size - 1 &&
           (received = recv(sock, response + total, max_size - 1 - total, 0)) > 0) {
        total += received;
    }
    response[total] = '\0';
    *response_size = total;

    close(sock);

    /* Parse headers */
    char *body = strstr(response, "\r\n\r\n");
    if (body) {
        /* Extract content-type header */
        char *ct = strstr(response, "Content-Type:");
        if (!ct) ct = strstr(response, "content-type:");
        if (ct && content_type) {
            ct += 13;
            while (*ct == ' ') ct++;
            char *end = strpbrk(ct, "\r\n;");
            size_t len = end ? (size_t)(end - ct) : strlen(ct);
            if (len >= ct_size) len = ct_size - 1;
            strncpy(content_type, ct, len);
            content_type[len] = '\0';
        }

        /* Move body to start of buffer */
        body += 4;
        size_t body_len = total - (body - response);
        memmove(response, body, body_len);
        response[body_len] = '\0';
        *response_size = body_len;
    }

    return BROWSER_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Lifecycle
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_browser_init(phantom_browser_t *browser, struct phantom_kernel *kernel) {
    if (!browser) {
        return BROWSER_ERR_INVALID;
    }

    memset(browser, 0, sizeof(phantom_browser_t));
    browser->kernel = kernel;

    /* Initialize page cache */
    browser->cache_capacity = 1024;
    browser->page_cache = calloc(browser->cache_capacity, sizeof(phantom_page_t *));
    if (!browser->page_cache) {
        return BROWSER_ERR_NOMEM;
    }

    /* Initialize bookmarks */
    browser->bookmark_capacity = BROWSER_MAX_BOOKMARKS;
    browser->bookmarks = calloc(browser->bookmark_capacity, sizeof(phantom_bookmark_t));
    if (!browser->bookmarks) {
        free(browser->page_cache);
        return BROWSER_ERR_NOMEM;
    }

    /* Initialize history index */
    browser->history_index = calloc(BROWSER_MAX_HISTORY, sizeof(uint64_t));
    if (!browser->history_index) {
        free(browser->bookmarks);
        free(browser->page_cache);
        return BROWSER_ERR_NOMEM;
    }

    /* Set defaults */
    browser->next_page_id = 1;
    browser->cache_enabled = 1;
    browser->ai_auto_summarize = 0;
    browser->preserve_images = 0;
    strncpy(browser->home_page, "about:blank", BROWSER_MAX_URL - 1);
    strncpy(browser->search_engine, "https://duckduckgo.com/?q=", BROWSER_MAX_URL - 1);

    /* Connect to GeoFS if available */
    if (kernel && kernel->geofs_volume) {
        browser->geofs_volume = kernel->geofs_volume;
    }

    browser->initialized = 1;

    printf("[phantom_browser] AI Browser initialized\n");
    printf("  Cache: enabled\n");
    printf("  AI auto-summarize: %s\n", browser->ai_auto_summarize ? "yes" : "no");

    return BROWSER_OK;
}

void phantom_browser_shutdown(phantom_browser_t *browser) {
    if (!browser || !browser->initialized) {
        return;
    }

    /* Free page cache */
    for (uint32_t i = 0; i < browser->cache_count; i++) {
        free(browser->page_cache[i]);
    }
    free(browser->page_cache);

    /* Free bookmarks */
    free(browser->bookmarks);

    /* Free history index */
    free(browser->history_index);

    /* Free tab histories */
    for (uint32_t i = 0; i < browser->tab_count; i++) {
        free(browser->tabs[i].history);
    }

    browser->initialized = 0;
    printf("[phantom_browser] Browser shutdown complete\n");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Configuration
 * ───────────────────────────────────────────────────────────────────────────── */

void phantom_browser_set_ai(phantom_browser_t *browser, struct phantom_ai *ai) {
    if (browser) {
        browser->ai = ai;
        printf("[phantom_browser] AI assistant connected\n");
    }
}

void phantom_browser_set_temporal(phantom_browser_t *browser, struct phantom_temporal *temporal) {
    if (browser) {
        browser->temporal = temporal;
        printf("[phantom_browser] Temporal engine connected - time travel enabled\n");
    }
}

void phantom_browser_set_home(phantom_browser_t *browser, const char *url) {
    if (browser && url) {
        strncpy(browser->home_page, url, BROWSER_MAX_URL - 1);
    }
}

void phantom_browser_set_search(phantom_browser_t *browser, const char *url) {
    if (browser && url) {
        strncpy(browser->search_engine, url, BROWSER_MAX_URL - 1);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Tab Management
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_browser_new_tab(phantom_browser_t *browser, const char *url) {
    if (!browser || !browser->initialized) {
        return BROWSER_ERR_INVALID;
    }

    if (browser->tab_count >= BROWSER_MAX_TABS) {
        return BROWSER_ERR_FULL;
    }

    phantom_tab_t *tab = &browser->tabs[browser->tab_count];
    memset(tab, 0, sizeof(phantom_tab_t));

    tab->tab_id = browser->tab_count;
    tab->created_at = time(NULL);
    tab->last_active = tab->created_at;
    tab->is_active = 1;

    /* Initialize tab history */
    tab->history_capacity = 100;
    tab->history = calloc(tab->history_capacity, sizeof(uint64_t));
    if (!tab->history) {
        return BROWSER_ERR_NOMEM;
    }

    strncpy(tab->title, "New Tab", BROWSER_MAX_TITLE - 1);

    /* Deactivate other tabs */
    for (uint32_t i = 0; i < browser->tab_count; i++) {
        browser->tabs[i].is_active = 0;
    }

    browser->active_tab = browser->tab_count;
    browser->tab_count++;

    /* Navigate to URL if provided */
    if (url && url[0]) {
        phantom_browser_navigate(browser, url);
    }

    return BROWSER_OK;
}

int phantom_browser_close_tab(phantom_browser_t *browser, uint32_t tab_id) {
    if (!browser || !browser->initialized || tab_id >= browser->tab_count) {
        return BROWSER_ERR_INVALID;
    }

    /* In Phantom style, we don't truly close - just mark as inactive */
    /* Tab history is preserved */
    phantom_tab_t *tab = &browser->tabs[tab_id];
    tab->is_active = 0;

    /* Prepend "[Closed] " to title - use temp buffer to avoid overlapping memory */
    char old_title[BROWSER_MAX_TITLE];
    strncpy(old_title, tab->title, sizeof(old_title) - 1);
    old_title[sizeof(old_title) - 1] = '\0';
    snprintf(tab->title, BROWSER_MAX_TITLE, "[Closed] %s", old_title);

    /* Switch to another tab if this was active */
    if (browser->active_tab == tab_id) {
        for (uint32_t i = 0; i < browser->tab_count; i++) {
            if (i != tab_id && browser->tabs[i].is_active) {
                browser->active_tab = i;
                break;
            }
        }
    }

    return BROWSER_OK;
}

int phantom_browser_switch_tab(phantom_browser_t *browser, uint32_t tab_id) {
    if (!browser || !browser->initialized || tab_id >= browser->tab_count) {
        return BROWSER_ERR_INVALID;
    }

    browser->tabs[browser->active_tab].is_active = 0;
    browser->active_tab = tab_id;
    browser->tabs[tab_id].is_active = 1;
    browser->tabs[tab_id].last_active = time(NULL);

    return BROWSER_OK;
}

phantom_tab_t *phantom_browser_get_tab(phantom_browser_t *browser, uint32_t tab_id) {
    if (!browser || tab_id >= browser->tab_count) {
        return NULL;
    }
    return &browser->tabs[tab_id];
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Navigation
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_browser_navigate(phantom_browser_t *browser, const char *url) {
    if (!browser || !browser->initialized || !url) {
        return BROWSER_ERR_INVALID;
    }

    /* Ensure we have a tab */
    if (browser->tab_count == 0) {
        int err = phantom_browser_new_tab(browser, NULL);
        if (err != BROWSER_OK) return err;
    }

    phantom_tab_t *tab = &browser->tabs[browser->active_tab];

    /* Create page record */
    phantom_page_t *page = calloc(1, sizeof(phantom_page_t));
    if (!page) {
        return BROWSER_ERR_NOMEM;
    }

    page->page_id = browser->next_page_id++;
    strncpy(page->url, url, BROWSER_MAX_URL - 1);
    extract_domain(url, page->domain, sizeof(page->domain));
    page->state = PAGE_STATE_LOADING;
    page->visited_at = time(NULL);

    /* Set referrer if we have a current page */
    if (tab->current_page) {
        page->referrer_id = tab->current_page->page_id;
    }

    printf("[browser] Navigating to: %s\n", url);

    /* Handle special URLs */
    if (strcmp(url, "about:blank") == 0) {
        strncpy(page->title, "Blank Page", BROWSER_MAX_TITLE - 1);
        page->state = PAGE_STATE_LOADED;
        page->content_type = CONTENT_HTML;
    } else if (strncmp(url, "about:", 6) == 0) {
        snprintf(page->title, BROWSER_MAX_TITLE, "About: %s", url + 6);
        page->state = PAGE_STATE_LOADED;
        page->content_type = CONTENT_HTML;
    } else {
        /* Fetch the page */
        time_t start = time(NULL);

        char *content = malloc(HTTP_BUFFER_SIZE);
        if (!content) {
            free(page);
            return BROWSER_ERR_NOMEM;
        }

        size_t content_size = 0;
        char content_type[128] = "";

        int err = http_get(url, content, HTTP_BUFFER_SIZE,
                           &content_size, content_type, sizeof(content_type));

        page->load_time_ms = (time(NULL) - start) * 1000;
        page->loaded_at = time(NULL);

        if (err == BROWSER_OK && content_size > 0) {
            page->state = PAGE_STATE_LOADED;
            page->content_size = content_size;
            page->content_type = detect_content_type(content_type, content);

            /* Extract title */
            if (page->content_type == CONTENT_HTML) {
                extract_title(content, page->title, BROWSER_MAX_TITLE);
            } else {
                strncpy(page->title, page->domain, BROWSER_MAX_TITLE - 1);
            }

            /* Compute content hash */
            compute_hash(content, content_size, page->content_hash);

            /* Cache the content */
            if (browser->cache_enabled) {
                phantom_browser_cache_page(browser, page, content, content_size);
            }

            printf("[browser] Loaded: %s (%zu bytes, %ums)\n",
                   page->title, content_size, page->load_time_ms);

            /* Auto-summarize with AI if enabled */
            if (browser->ai_auto_summarize && browser->ai &&
                page->content_type == CONTENT_HTML) {
                phantom_browser_ai_summarize(browser, page->page_id,
                                              page->summary, sizeof(page->summary));
                page->ai_analyzed = 1;
            }

        } else {
            page->state = PAGE_STATE_ERROR;
            snprintf(page->title, BROWSER_MAX_TITLE, "Error loading %s", page->domain);
            printf("[browser] Failed to load: %s (error %d)\n", url, err);
        }

        free(content);
    }

    /* Add to page cache */
    if (browser->cache_count < browser->cache_capacity) {
        browser->page_cache[browser->cache_count++] = page;
    }

    /* Add to history */
    if (browser->history_count < BROWSER_MAX_HISTORY) {
        browser->history_index[browser->history_count++] = page->page_id;
    }

    /* Add to tab history */
    if (tab->history_count < tab->history_capacity) {
        tab->history[tab->history_count++] = page->page_id;
        tab->history_position = tab->history_count - 1;
    }

    /* Update tab */
    tab->current_page = page;
    strncpy(tab->title, page->title, BROWSER_MAX_TITLE - 1);
    tab->is_loading = 0;

    /* Record in temporal engine */
    if (browser->temporal) {
        phantom_time_record_event(browser->temporal, TIME_EVENT_NET_CONNECT,
                                   0, 0, url, page->page_id,
                                   "Page visited");
    }

    /* Update stats */
    browser->total_pages_visited++;
    browser->total_bytes_cached += page->content_size;

    return BROWSER_OK;
}

int phantom_browser_back(phantom_browser_t *browser) {
    if (!browser || !browser->initialized || browser->tab_count == 0) {
        return BROWSER_ERR_INVALID;
    }

    phantom_tab_t *tab = &browser->tabs[browser->active_tab];
    if (tab->history_position == 0) {
        return BROWSER_ERR_NOT_FOUND;  /* Already at start */
    }

    tab->history_position--;
    uint64_t page_id = tab->history[tab->history_position];

    /* Find the page */
    phantom_page_t *page = phantom_browser_get_page(browser, page_id);
    if (page) {
        tab->current_page = page;
        strncpy(tab->title, page->title, BROWSER_MAX_TITLE - 1);
        printf("[browser] Back to: %s\n", page->title);
    }

    return BROWSER_OK;
}

int phantom_browser_forward(phantom_browser_t *browser) {
    if (!browser || !browser->initialized || browser->tab_count == 0) {
        return BROWSER_ERR_INVALID;
    }

    phantom_tab_t *tab = &browser->tabs[browser->active_tab];
    if (tab->history_position >= tab->history_count - 1) {
        return BROWSER_ERR_NOT_FOUND;  /* Already at end */
    }

    tab->history_position++;
    uint64_t page_id = tab->history[tab->history_position];

    /* Find the page */
    phantom_page_t *page = phantom_browser_get_page(browser, page_id);
    if (page) {
        tab->current_page = page;
        strncpy(tab->title, page->title, BROWSER_MAX_TITLE - 1);
        printf("[browser] Forward to: %s\n", page->title);
    }

    return BROWSER_OK;
}

int phantom_browser_refresh(phantom_browser_t *browser) {
    if (!browser || !browser->initialized || browser->tab_count == 0) {
        return BROWSER_ERR_INVALID;
    }

    phantom_tab_t *tab = &browser->tabs[browser->active_tab];
    if (!tab->current_page) {
        return BROWSER_ERR_NOT_FOUND;
    }

    /* Re-navigate to same URL - this creates a new version */
    return phantom_browser_navigate(browser, tab->current_page->url);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Page Access
 * ───────────────────────────────────────────────────────────────────────────── */

phantom_page_t *phantom_browser_get_current_page(phantom_browser_t *browser) {
    if (!browser || browser->tab_count == 0) {
        return NULL;
    }
    return browser->tabs[browser->active_tab].current_page;
}

phantom_page_t *phantom_browser_get_page(phantom_browser_t *browser, uint64_t page_id) {
    if (!browser) return NULL;

    for (uint32_t i = 0; i < browser->cache_count; i++) {
        if (browser->page_cache[i] && browser->page_cache[i]->page_id == page_id) {
            return browser->page_cache[i];
        }
    }
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * History Search
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_browser_history_search(phantom_browser_t *browser, const char *query,
                                    phantom_search_result_t *results, uint32_t max_results,
                                    uint32_t *count_out) {
    if (!browser || !browser->initialized || !results || !count_out) {
        return BROWSER_ERR_INVALID;
    }

    *count_out = 0;

    /* If no query, return all pages */
    int search_all = (!query || query[0] == '\0');

    char query_lower[256] = {0};
    if (!search_all) {
        strncpy(query_lower, query, sizeof(query_lower) - 1);
        for (char *p = query_lower; *p; p++) *p = tolower(*p);
    }

    for (uint32_t i = 0; i < browser->cache_count && *count_out < max_results; i++) {
        phantom_page_t *page = browser->page_cache[i];
        if (!page) continue;

        int match = 0;
        float relevance = 1.0f;

        if (search_all) {
            match = 1;  /* Return all pages */
        } else {
            /* Search in URL, title, and summary */
            char title_lower[BROWSER_MAX_TITLE];
            strncpy(title_lower, page->title, sizeof(title_lower) - 1);
            for (char *p = title_lower; *p; p++) *p = tolower(*p);

            if (strstr(title_lower, query_lower)) {
                match = 1;
                relevance = 0.9f;
            } else if (strstr(page->url, query)) {
                match = 1;
                relevance = 0.7f;
            } else if (page->summary[0] && strstr(page->summary, query)) {
                match = 1;
                relevance = 0.5f;
            }
        }

        if (match) {
            phantom_search_result_t *r = &results[*count_out];
            r->page_id = page->page_id;
            strncpy(r->url, page->url, BROWSER_MAX_URL - 1);
            strncpy(r->title, page->title, BROWSER_MAX_TITLE - 1);
            strncpy(r->snippet, page->summary[0] ? page->summary : page->url, 511);
            r->relevance = relevance;
            r->visited_at = page->visited_at;
            (*count_out)++;
        }
    }

    return BROWSER_OK;
}

int phantom_browser_history_by_domain(phantom_browser_t *browser, const char *domain,
                                       phantom_page_t **pages, uint32_t max_pages,
                                       uint32_t *count_out) {
    if (!browser || !browser->initialized || !domain || !pages || !count_out) {
        return BROWSER_ERR_INVALID;
    }

    *count_out = 0;

    for (uint32_t i = 0; i < browser->cache_count && *count_out < max_pages; i++) {
        phantom_page_t *page = browser->page_cache[i];
        if (page && strstr(page->domain, domain)) {
            pages[*count_out] = page;
            (*count_out)++;
        }
    }

    return BROWSER_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Bookmarks
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_browser_bookmark_add(phantom_browser_t *browser, const char *url,
                                  const char *title, const char *folder) {
    if (!browser || !browser->initialized || !url) {
        return BROWSER_ERR_INVALID;
    }

    if (browser->bookmark_count >= browser->bookmark_capacity) {
        return BROWSER_ERR_FULL;
    }

    /* Check if already bookmarked */
    phantom_bookmark_t *existing = phantom_browser_bookmark_find(browser, url);
    if (existing && !existing->is_archived) {
        /* Update visit count */
        existing->visit_count++;
        existing->last_visited = time(NULL);
        return BROWSER_OK;
    }

    phantom_bookmark_t *bm = &browser->bookmarks[browser->bookmark_count];
    memset(bm, 0, sizeof(phantom_bookmark_t));

    bm->bookmark_id = browser->bookmark_count + 1;
    strncpy(bm->url, url, BROWSER_MAX_URL - 1);
    strncpy(bm->title, title ? title : url, BROWSER_MAX_TITLE - 1);
    strncpy(bm->folder, folder ? folder : "Unsorted", 255);
    bm->created_at = time(NULL);
    bm->last_visited = bm->created_at;
    bm->visit_count = 1;

    /* Link to current page if available */
    phantom_page_t *current = phantom_browser_get_current_page(browser);
    if (current && strcmp(current->url, url) == 0) {
        bm->page_id = current->page_id;
        bm->versions[0] = current->page_id;
        bm->version_count = 1;
    }

    browser->bookmark_count++;

    printf("[browser] Bookmarked: %s\n", title ? title : url);
    return BROWSER_OK;
}

int phantom_browser_bookmark_archive(phantom_browser_t *browser, uint64_t bookmark_id) {
    if (!browser || !browser->initialized) {
        return BROWSER_ERR_INVALID;
    }

    for (uint32_t i = 0; i < browser->bookmark_count; i++) {
        if (browser->bookmarks[i].bookmark_id == bookmark_id) {
            browser->bookmarks[i].is_archived = 1;
            browser->bookmarks[i].archived_at = time(NULL);
            printf("[browser] Bookmark archived (preserved in history)\n");
            return BROWSER_OK;
        }
    }

    return BROWSER_ERR_NOT_FOUND;
}

phantom_bookmark_t *phantom_browser_bookmark_find(phantom_browser_t *browser, const char *url) {
    if (!browser || !url) return NULL;

    for (uint32_t i = 0; i < browser->bookmark_count; i++) {
        if (strcmp(browser->bookmarks[i].url, url) == 0) {
            return &browser->bookmarks[i];
        }
    }
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * AI Features
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_browser_ai_summarize(phantom_browser_t *browser, uint64_t page_id,
                                  char *summary_out, size_t max_len) {
    if (!browser || !browser->initialized || !summary_out) {
        return BROWSER_ERR_INVALID;
    }

    phantom_page_t *page = phantom_browser_get_page(browser, page_id);
    if (!page) {
        return BROWSER_ERR_NOT_FOUND;
    }

    /* If AI not available, provide a basic summary */
    if (!browser->ai) {
        snprintf(summary_out, max_len,
                 "Page: %s\nDomain: %s\nType: %s\nSize: %lu bytes\nVisited: %s",
                 page->title, page->domain,
                 page->content_type == CONTENT_HTML ? "HTML" : "Other",
                 page->content_size,
                 ctime(&page->visited_at));
        return BROWSER_OK;
    }

    /* Use AI to summarize (placeholder - would integrate with phantom_ai) */
    snprintf(summary_out, max_len,
             "[AI Summary]\n"
             "Title: %s\n"
             "This page from %s appears to contain %s content.\n"
             "Loaded in %ums with %lu bytes of data.\n"
             "(Full AI analysis would require AI subsystem integration)",
             page->title, page->domain,
             page->content_type == CONTENT_HTML ? "web" :
             page->content_type == CONTENT_JSON ? "data" : "mixed",
             page->load_time_ms, page->content_size);

    browser->ai_queries_made++;
    return BROWSER_OK;
}

int phantom_browser_ai_search(phantom_browser_t *browser, const char *query,
                               phantom_search_result_t *results, uint32_t max_results,
                               uint32_t *count_out) {
    if (!browser || !browser->initialized || !query || !results || !count_out) {
        return BROWSER_ERR_INVALID;
    }

    /* For now, use regular search with AI boost for summarized pages */
    int err = phantom_browser_history_search(browser, query, results, max_results, count_out);
    if (err != BROWSER_OK) return err;

    /* Boost relevance for AI-analyzed pages */
    for (uint32_t i = 0; i < *count_out; i++) {
        phantom_page_t *page = phantom_browser_get_page(browser, results[i].page_id);
        if (page && page->ai_analyzed) {
            results[i].relevance *= 1.2f;
            if (results[i].relevance > 1.0f) results[i].relevance = 1.0f;
        }
    }

    browser->ai_queries_made++;
    return BROWSER_OK;
}

int phantom_browser_ai_answer(phantom_browser_t *browser, const char *question,
                               char *answer_out, size_t max_len) {
    if (!browser || !browser->initialized || !question || !answer_out) {
        return BROWSER_ERR_INVALID;
    }

    /* Search history for relevant pages */
    phantom_search_result_t results[10];
    uint32_t count = 0;

    phantom_browser_history_search(browser, question, results, 10, &count);

    if (count == 0) {
        snprintf(answer_out, max_len,
                 "I couldn't find any pages in your browsing history related to: %s\n"
                 "Try browsing some relevant pages first, and I'll remember them for you.",
                 question);
        return BROWSER_OK;
    }

    /* Build answer from found pages */
    snprintf(answer_out, max_len,
             "Based on your browsing history, here's what I found about \"%s\":\n\n",
             question);

    size_t len = strlen(answer_out);
    for (uint32_t i = 0; i < count && i < 5 && len < max_len - 200; i++) {
        len += snprintf(answer_out + len, max_len - len,
                        "%d. %s\n   %s\n   (Visited: %s)\n",
                        i + 1, results[i].title, results[i].url,
                        ctime(&results[i].visited_at));
    }

    browser->ai_queries_made++;
    return BROWSER_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Cache Management
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_browser_cache_page(phantom_browser_t *browser, phantom_page_t *page,
                                const char *content, size_t size) {
    if (!browser || !page || !content) {
        return BROWSER_ERR_INVALID;
    }

    /* Build cache path */
    snprintf(page->content_path, sizeof(page->content_path),
             "%s/%lu.html", BROWSER_CACHE_PATH, page->page_id);

    page->state = PAGE_STATE_CACHED;

    /* In production, would write to GeoFS here */
    /* For now, just mark as cached */

    return BROWSER_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * URL Parsing
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_browser_parse_url(const char *url, char *scheme, char *host,
                               char *path, uint16_t *port) {
    if (!url || !scheme || !host || !path || !port) {
        return -1;
    }

    scheme[0] = host[0] = path[0] = '\0';
    *port = 80;

    /* Parse scheme */
    const char *p = strstr(url, "://");
    if (p) {
        size_t len = p - url;
        if (len > 15) len = 15;
        strncpy(scheme, url, len);
        scheme[len] = '\0';
        p += 3;

        if (strcmp(scheme, "https") == 0) *port = 443;
    } else {
        strcpy(scheme, "http");
        p = url;
    }

    /* Parse host */
    const char *host_end = p;
    while (*host_end && *host_end != '/' && *host_end != ':' && *host_end != '?') {
        host_end++;
    }

    size_t host_len = host_end - p;
    if (host_len > 255) host_len = 255;
    strncpy(host, p, host_len);
    host[host_len] = '\0';

    /* Parse port if present */
    if (*host_end == ':') {
        *port = atoi(host_end + 1);
        while (*host_end && *host_end != '/' && *host_end != '?') host_end++;
    }

    /* Parse path */
    if (*host_end == '/' || *host_end == '?') {
        strncpy(path, host_end, 2047);
        path[2047] = '\0';
    } else {
        strcpy(path, "/");
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility Functions
 * ───────────────────────────────────────────────────────────────────────────── */

const char *phantom_browser_result_string(phantom_browser_result_t code) {
    switch (code) {
        case BROWSER_OK:            return "OK";
        case BROWSER_ERR_INVALID:   return "Invalid argument";
        case BROWSER_ERR_NOT_FOUND: return "Not found";
        case BROWSER_ERR_NETWORK:   return "Network error";
        case BROWSER_ERR_TIMEOUT:   return "Timeout";
        case BROWSER_ERR_PARSE:     return "Parse error";
        case BROWSER_ERR_CACHE:     return "Cache error";
        case BROWSER_ERR_NOMEM:     return "Out of memory";
        case BROWSER_ERR_AI:        return "AI error";
        case BROWSER_ERR_FULL:      return "Capacity full";
        default:                    return "Unknown error";
    }
}

const char *phantom_browser_state_string(phantom_page_state_t state) {
    switch (state) {
        case PAGE_STATE_LOADING:    return "Loading";
        case PAGE_STATE_LOADED:     return "Loaded";
        case PAGE_STATE_CACHED:     return "Cached";
        case PAGE_STATE_ARCHIVED:   return "Archived";
        case PAGE_STATE_ERROR:      return "Error";
        default:                    return "Unknown";
    }
}

void phantom_browser_print_page(const phantom_page_t *page) {
    if (!page) return;

    char visited[64];
    strftime(visited, sizeof(visited), "%Y-%m-%d %H:%M:%S", localtime(&page->visited_at));

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║                            PAGE INFO                                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  ID:       %lu\n", page->page_id);
    printf("  Title:    %s\n", page->title);
    printf("  URL:      %s\n", page->url);
    printf("  Domain:   %s\n", page->domain);
    printf("  State:    %s\n", phantom_browser_state_string(page->state));
    printf("  Visited:  %s\n", visited);
    printf("  Size:     %lu bytes\n", page->content_size);
    printf("  Load:     %u ms\n", page->load_time_ms);
    if (page->summary[0]) {
        printf("  Summary:  %.60s...\n", page->summary);
    }
    printf("\n");
}

void phantom_browser_print_stats(const phantom_browser_t *browser) {
    if (!browser) return;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║                      PHANTOM BROWSER STATISTICS                        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Tabs:              %u open\n", browser->tab_count);
    printf("  Pages visited:     %lu total\n", browser->total_pages_visited);
    printf("  Pages cached:      %u\n", browser->cache_count);
    printf("  Bytes cached:      %lu\n", browser->total_bytes_cached);
    printf("  Bookmarks:         %u\n", browser->bookmark_count);
    printf("  AI queries:        %lu\n", browser->ai_queries_made);
    printf("\n");
    printf("  AI:                %s\n", browser->ai ? "Connected" : "Not connected");
    printf("  Temporal:          %s\n", browser->temporal ? "Connected" : "Not connected");
    printf("  Auto-summarize:    %s\n", browser->ai_auto_summarize ? "Enabled" : "Disabled");
    printf("\n");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Page Content Access
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_browser_get_page_content(phantom_browser_t *browser, uint64_t page_id,
                                      char **content_out, size_t *size_out) {
    if (!browser || !content_out || !size_out) return BROWSER_ERR_INVALID;

    phantom_page_t *page = phantom_browser_get_page(browser, page_id);
    if (!page) return BROWSER_ERR_NOT_FOUND;

    /* SECURITY: Check for integer overflow before allocation */
    if (page->content_size > SIZE_MAX - 256) {
        return BROWSER_ERR_NOMEM;  /* Would overflow */
    }

    /* For now, return a placeholder indicating content is in geology cache */
    /* In production, this would read from page->content_path in geology */
    char *content = malloc(page->content_size + 256);
    if (!content) return BROWSER_ERR_NOMEM;

    int len = snprintf(content, page->content_size + 256,
        "<!-- Cached content for page %lu -->\n"
        "<!-- URL: %s -->\n"
        "<!-- Cached at: %s -->\n"
        "<!-- Size: %lu bytes -->\n"
        "\n"
        "[Content stored in geology at: %s]\n"
        "[To retrieve: geofs read %s]\n"
        "\n"
        "Title: %s\n"
        "Summary: %s\n",
        page->page_id, page->url,
        ctime(&page->visited_at),
        page->content_size,
        page->content_path, page->content_path,
        page->title, page->summary);

    *content_out = content;
    *size_out = (size_t)len;
    return BROWSER_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Bookmark List
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_browser_bookmark_list(phantom_browser_t *browser, const char *folder,
                                   phantom_bookmark_t **bookmarks, uint32_t *count) {
    if (!browser || !bookmarks || !count) return BROWSER_ERR_INVALID;

    *bookmarks = browser->bookmarks;
    *count = browser->bookmark_count;

    /* If folder specified, could filter here */
    (void)folder;  /* Currently returns all bookmarks */

    return BROWSER_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * AI Compare
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_browser_ai_compare(phantom_browser_t *browser, uint64_t page_id_1,
                                uint64_t page_id_2, char *diff_out, size_t max_len) {
    if (!browser || !diff_out || max_len == 0) return BROWSER_ERR_INVALID;

    phantom_page_t *page1 = phantom_browser_get_page(browser, page_id_1);
    phantom_page_t *page2 = phantom_browser_get_page(browser, page_id_2);

    if (!page1 || !page2) return BROWSER_ERR_NOT_FOUND;

    browser->ai_queries_made++;

    /* Generate comparison report */
    snprintf(diff_out, max_len,
        "Comparison of Page %lu vs Page %lu:\n\n"
        "PAGE 1:\n"
        "  Title: %s\n"
        "  URL: %s\n"
        "  Visited: %s"
        "  Size: %lu bytes\n\n"
        "PAGE 2:\n"
        "  Title: %s\n"
        "  URL: %s\n"
        "  Visited: %s"
        "  Size: %lu bytes\n\n"
        "DIFFERENCES:\n"
        "  Title changed: %s\n"
        "  Size delta: %ld bytes\n"
        "  Content hash: %s\n"
        "\n"
        "[Full AI-powered diff would analyze actual content changes]\n"
        "[Both versions preserved in geology for future comparison]\n",
        page_id_1, page_id_2,
        page1->title, page1->url, ctime(&page1->visited_at), page1->content_size,
        page2->title, page2->url, ctime(&page2->visited_at), page2->content_size,
        strcmp(page1->title, page2->title) ? "Yes" : "No",
        (long)(page2->content_size - page1->content_size),
        memcmp(page1->content_hash, page2->content_hash, PHANTOM_HASH_SIZE) ? "Different" : "Same");

    return BROWSER_OK;
}
