/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM AI WEB BROWSER
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * An AI-powered web browser that embodies the Phantom philosophy:
 * - Pages are NEVER deleted from history - preserved in geology forever
 * - AI summarizes, analyzes, and helps you understand content
 * - Bookmarks are versioned - see how sites changed over time
 * - Privacy through preservation - you control your complete browsing record
 *
 * Key Principles:
 * 1. PRESERVATION: Every page you visit is cached in geology
 * 2. TIME TRAVEL: View any page as it was when you visited it
 * 3. AI ASSISTANCE: Summarize, translate, explain, search your history
 * 4. ACCOUNTABILITY: Full audit trail of all browsing activity
 * 5. NO TRACKING: We don't track you - YOU track your own history
 *
 * Unique Features:
 * - "What did that page say?" - AI recalls content from your history
 * - "Find pages about X" - Semantic search across all visited pages
 * - "Compare versions" - See how a site changed between visits
 * - "Summarize my research" - AI aggregates related pages
 */

#ifndef PHANTOM_BROWSER_H
#define PHANTOM_BROWSER_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "phantom.h"
#include "phantom_ai.h"
#include "phantom_time.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────── */

#define BROWSER_MAX_URL             4096
#define BROWSER_MAX_TITLE           512
#define BROWSER_MAX_TABS            64
#define BROWSER_MAX_BOOKMARKS       1024
#define BROWSER_MAX_HISTORY         65536
#define BROWSER_CACHE_PATH          "/var/cache/browser"
#define BROWSER_HISTORY_PATH        "/home/.browser/history"
#define BROWSER_BOOKMARKS_PATH      "/home/.browser/bookmarks"

/* ─────────────────────────────────────────────────────────────────────────────
 * Page States
 *
 * Pages are never "deleted" - they transition to archived state.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PAGE_STATE_LOADING = 0,     /* Currently fetching */
    PAGE_STATE_LOADED,          /* Successfully loaded */
    PAGE_STATE_CACHED,          /* Stored in geology */
    PAGE_STATE_ARCHIVED,        /* Removed from active cache, in deep storage */
    PAGE_STATE_ERROR,           /* Failed to load (error preserved) */
} phantom_page_state_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Content Types
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    CONTENT_HTML = 0,
    CONTENT_TEXT,
    CONTENT_JSON,
    CONTENT_XML,
    CONTENT_IMAGE,
    CONTENT_PDF,
    CONTENT_BINARY,
    CONTENT_UNKNOWN,
} phantom_content_type_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Page Structure
 *
 * Represents a single web page visit.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_page {
    uint64_t page_id;                   /* Unique page ID */
    char url[BROWSER_MAX_URL];          /* Full URL */
    char title[BROWSER_MAX_TITLE];      /* Page title */
    char domain[256];                   /* Extracted domain */

    phantom_page_state_t state;
    phantom_content_type_t content_type;

    /* Timing */
    time_t visited_at;                  /* When first visited */
    time_t loaded_at;                   /* When fully loaded */
    uint32_t load_time_ms;              /* How long it took */

    /* Content info */
    uint64_t content_size;              /* Size in bytes */
    phantom_hash_t content_hash;        /* Hash for deduplication */
    char content_path[256];             /* Path in geology cache */

    /* AI analysis (populated on demand) */
    char summary[1024];                 /* AI-generated summary */
    char keywords[512];                 /* Extracted keywords */
    char language[32];                  /* Detected language */
    float sentiment;                    /* -1.0 to 1.0 */
    int ai_analyzed;                    /* Has AI processed this? */

    /* Navigation */
    uint64_t referrer_id;               /* Page that linked here */
    uint32_t visit_count;               /* Times visited */
    uint32_t link_count;                /* Outgoing links */

    /* User interaction */
    uint32_t scroll_depth;              /* How far user scrolled (%) */
    uint32_t time_on_page_sec;          /* Time spent reading */
    int is_bookmarked;
    int is_favorite;

    /* Version tracking */
    uint64_t previous_version_id;       /* Previous visit to same URL */
    int content_changed;                /* Did content change since last visit? */

} phantom_page_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Tab Structure
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_tab {
    uint32_t tab_id;
    char title[BROWSER_MAX_TITLE];
    phantom_page_t *current_page;

    /* Navigation history for this tab */
    uint64_t *history;                  /* Array of page IDs */
    uint32_t history_count;
    uint32_t history_position;          /* Current position in history */
    uint32_t history_capacity;

    /* State */
    int is_active;
    int is_loading;
    int is_pinned;
    time_t created_at;
    time_t last_active;

} phantom_tab_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Bookmark Structure
 *
 * Bookmarks are versioned - preserving the page as it was when bookmarked.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_bookmark {
    uint64_t bookmark_id;
    char url[BROWSER_MAX_URL];
    char title[BROWSER_MAX_TITLE];
    char folder[256];                   /* Bookmark folder */
    char notes[1024];                   /* User notes */
    char tags[256];                     /* Comma-separated tags */

    uint64_t page_id;                   /* Page when bookmarked */
    time_t created_at;
    time_t last_visited;
    uint32_t visit_count;

    /* Versioning */
    uint64_t versions[32];              /* Page IDs of different versions */
    uint32_t version_count;

    /* State */
    int is_archived;                    /* Bookmark 'deleted' */
    time_t archived_at;

} phantom_bookmark_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Search Result
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_search_result {
    uint64_t page_id;
    char url[BROWSER_MAX_URL];
    char title[BROWSER_MAX_TITLE];
    char snippet[512];                  /* Relevant text excerpt */
    float relevance;                    /* 0.0 to 1.0 */
    time_t visited_at;
} phantom_search_result_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * AI Features
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    AI_SUMMARIZE,           /* Summarize page content */
    AI_EXPLAIN,             /* Explain complex content */
    AI_TRANSLATE,           /* Translate to another language */
    AI_EXTRACT_FACTS,       /* Extract key facts */
    AI_FIND_RELATED,        /* Find related pages in history */
    AI_COMPARE,             /* Compare two page versions */
    AI_RESEARCH,            /* Aggregate info across pages */
    AI_ANSWER,              /* Answer question from page content */
} phantom_browser_ai_op_t;

typedef struct phantom_browser_ai_request {
    phantom_browser_ai_op_t operation;
    uint64_t page_id;                   /* Primary page */
    uint64_t page_id_2;                 /* Second page (for compare) */
    char query[1024];                   /* User question/request */
    char target_language[32];           /* For translation */
} phantom_browser_ai_request_t;

typedef struct phantom_browser_ai_response {
    int success;
    char result[4096];                  /* AI output */
    char error[256];                    /* Error if failed */
    uint32_t tokens_used;
    uint32_t processing_ms;
} phantom_browser_ai_response_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Browser Context
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_browser {
    /* Tabs */
    phantom_tab_t tabs[BROWSER_MAX_TABS];
    uint32_t tab_count;
    uint32_t active_tab;

    /* Page cache (in-memory) */
    phantom_page_t **page_cache;
    uint32_t cache_count;
    uint32_t cache_capacity;

    /* History index */
    uint64_t *history_index;            /* All page IDs ever visited */
    uint64_t history_count;
    uint64_t next_page_id;

    /* Bookmarks */
    phantom_bookmark_t *bookmarks;
    uint32_t bookmark_count;
    uint32_t bookmark_capacity;

    /* Statistics */
    uint64_t total_pages_visited;
    uint64_t total_bytes_cached;
    uint64_t total_time_browsing_sec;
    uint64_t ai_queries_made;

    /* Configuration */
    int cache_enabled;
    int ai_auto_summarize;              /* Auto-summarize pages */
    int preserve_images;                /* Cache images too */
    int javascript_enabled;             /* For future use */
    char home_page[BROWSER_MAX_URL];
    char search_engine[BROWSER_MAX_URL];

    /* References */
    struct phantom_kernel *kernel;
    struct phantom_ai *ai;
    struct phantom_temporal *temporal;
    void *geofs_volume;

    /* State */
    int initialized;

} phantom_browser_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Result Codes
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    BROWSER_OK = 0,
    BROWSER_ERR_INVALID = -1,
    BROWSER_ERR_NOT_FOUND = -2,
    BROWSER_ERR_NETWORK = -3,
    BROWSER_ERR_TIMEOUT = -4,
    BROWSER_ERR_PARSE = -5,
    BROWSER_ERR_CACHE = -6,
    BROWSER_ERR_NOMEM = -7,
    BROWSER_ERR_AI = -8,
    BROWSER_ERR_FULL = -9,
} phantom_browser_result_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Browser API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Lifecycle */
int phantom_browser_init(phantom_browser_t *browser, struct phantom_kernel *kernel);
void phantom_browser_shutdown(phantom_browser_t *browser);

/* Configuration */
void phantom_browser_set_ai(phantom_browser_t *browser, struct phantom_ai *ai);
void phantom_browser_set_temporal(phantom_browser_t *browser, struct phantom_temporal *temporal);
void phantom_browser_set_home(phantom_browser_t *browser, const char *url);
void phantom_browser_set_search(phantom_browser_t *browser, const char *url);

/* Navigation */
int phantom_browser_navigate(phantom_browser_t *browser, const char *url);
int phantom_browser_back(phantom_browser_t *browser);
int phantom_browser_forward(phantom_browser_t *browser);
int phantom_browser_refresh(phantom_browser_t *browser);
int phantom_browser_stop(phantom_browser_t *browser);

/* Tab Management */
int phantom_browser_new_tab(phantom_browser_t *browser, const char *url);
int phantom_browser_close_tab(phantom_browser_t *browser, uint32_t tab_id);
int phantom_browser_switch_tab(phantom_browser_t *browser, uint32_t tab_id);
phantom_tab_t *phantom_browser_get_tab(phantom_browser_t *browser, uint32_t tab_id);

/* Page Access */
phantom_page_t *phantom_browser_get_current_page(phantom_browser_t *browser);
phantom_page_t *phantom_browser_get_page(phantom_browser_t *browser, uint64_t page_id);
int phantom_browser_get_page_content(phantom_browser_t *browser, uint64_t page_id,
                                      char **content_out, size_t *size_out);

/* History (never deleted!) */
int phantom_browser_history_search(phantom_browser_t *browser, const char *query,
                                    phantom_search_result_t *results, uint32_t max_results,
                                    uint32_t *count_out);
int phantom_browser_history_by_date(phantom_browser_t *browser, time_t from, time_t to,
                                     phantom_page_t **pages, uint32_t max_pages,
                                     uint32_t *count_out);
int phantom_browser_history_by_domain(phantom_browser_t *browser, const char *domain,
                                       phantom_page_t **pages, uint32_t max_pages,
                                       uint32_t *count_out);

/* Bookmarks (archived, never deleted) */
int phantom_browser_bookmark_add(phantom_browser_t *browser, const char *url,
                                  const char *title, const char *folder);
int phantom_browser_bookmark_archive(phantom_browser_t *browser, uint64_t bookmark_id);
int phantom_browser_bookmark_restore(phantom_browser_t *browser, uint64_t bookmark_id);
int phantom_browser_bookmark_list(phantom_browser_t *browser, const char *folder,
                                   phantom_bookmark_t **bookmarks, uint32_t *count);
phantom_bookmark_t *phantom_browser_bookmark_find(phantom_browser_t *browser, const char *url);

/* AI Features */
int phantom_browser_ai_summarize(phantom_browser_t *browser, uint64_t page_id,
                                  char *summary_out, size_t max_len);
int phantom_browser_ai_explain(phantom_browser_t *browser, uint64_t page_id,
                                const char *what, char *explanation_out, size_t max_len);
int phantom_browser_ai_search(phantom_browser_t *browser, const char *query,
                               phantom_search_result_t *results, uint32_t max_results,
                               uint32_t *count_out);
int phantom_browser_ai_compare(phantom_browser_t *browser, uint64_t page_id_1,
                                uint64_t page_id_2, char *diff_out, size_t max_len);
int phantom_browser_ai_answer(phantom_browser_t *browser, const char *question,
                               char *answer_out, size_t max_len);
int phantom_browser_ai_research(phantom_browser_t *browser, const char *topic,
                                 char *report_out, size_t max_len);

/* Time Travel */
int phantom_browser_page_at_time(phantom_browser_t *browser, const char *url,
                                  time_t timestamp, phantom_page_t **page_out);
int phantom_browser_page_versions(phantom_browser_t *browser, const char *url,
                                   phantom_page_t **versions, uint32_t max_versions,
                                   uint32_t *count_out);

/* Cache Management */
int phantom_browser_cache_page(phantom_browser_t *browser, phantom_page_t *page,
                                const char *content, size_t size);
int phantom_browser_cache_stats(phantom_browser_t *browser, uint64_t *pages,
                                 uint64_t *bytes, uint64_t *oldest);

/* Utility */
const char *phantom_browser_result_string(phantom_browser_result_t code);
const char *phantom_browser_state_string(phantom_page_state_t state);
int phantom_browser_parse_url(const char *url, char *scheme, char *host,
                               char *path, uint16_t *port);
void phantom_browser_print_page(const phantom_page_t *page);
void phantom_browser_print_tab(const phantom_tab_t *tab);
void phantom_browser_print_stats(const phantom_browser_t *browser);

#endif /* PHANTOM_BROWSER_H */
