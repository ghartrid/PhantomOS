/*
 * ==============================================================================
 *                        PHANTOM URL SCANNER
 *                     "To Create, Not To Destroy"
 * ==============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <time.h>
#include "phantom_urlscan.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Secure Random for DNS Query IDs (prevents DNS cache poisoning)
 * ───────────────────────────────────────────────────────────────────────────── */
static uint16_t secure_random_dns_id(void) {
    uint16_t id;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, &id, sizeof(id)) == sizeof(id)) {
            close(fd);
            return id;
        }
        close(fd);
    }
    /* Fallback: mix time with address randomization (still not ideal) */
    return (uint16_t)((time(NULL) ^ (uintptr_t)&id) & 0xFFFF);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * KNOWN BRAND DOMAINS - For typosquatting detection
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *brand;
    const char *domain;
} known_brand_t;

static const known_brand_t known_brands[] = {
    {"Google", "google.com"},
    {"Facebook", "facebook.com"},
    {"Amazon", "amazon.com"},
    {"Apple", "apple.com"},
    {"Microsoft", "microsoft.com"},
    {"PayPal", "paypal.com"},
    {"Netflix", "netflix.com"},
    {"Twitter", "twitter.com"},
    {"Instagram", "instagram.com"},
    {"LinkedIn", "linkedin.com"},
    {"GitHub", "github.com"},
    {"Dropbox", "dropbox.com"},
    {"Yahoo", "yahoo.com"},
    {"eBay", "ebay.com"},
    {"Walmart", "walmart.com"},
    {"Target", "target.com"},
    {"Chase", "chase.com"},
    {"BankOfAmerica", "bankofamerica.com"},
    {"WellsFargo", "wellsfargo.com"},
    {"Citibank", "citibank.com"},
    {"USPS", "usps.com"},
    {"FedEx", "fedex.com"},
    {"UPS", "ups.com"},
    {"DHL", "dhl.com"},
    {"Steam", "steampowered.com"},
    {"Discord", "discord.com"},
    {"Twitch", "twitch.tv"},
    {"Reddit", "reddit.com"},
    {"Wikipedia", "wikipedia.org"},
    {"WhatsApp", "whatsapp.com"},
    {"Zoom", "zoom.us"},
    {"Slack", "slack.com"},
    {"Adobe", "adobe.com"},
    {"Spotify", "spotify.com"},
    {"iCloud", "icloud.com"},
    {"Office365", "office365.com"},
    {"Outlook", "outlook.com"},
    {"Hotmail", "hotmail.com"},
    {"Gmail", "gmail.com"},
    {NULL, NULL}
};

/* ═══════════════════════════════════════════════════════════════════════════════
 * SUSPICIOUS TLDs - Commonly used for phishing/malware
 * ═══════════════════════════════════════════════════════════════════════════════ */

static const char *suspicious_tlds[] = {
    /* Free/cheap TLDs popular with scammers */
    ".tk", ".ml", ".ga", ".cf", ".gq",
    ".xyz", ".top", ".work", ".click", ".link",
    ".club", ".online", ".site", ".website", ".space",
    ".pw", ".cc", ".ws", ".buzz", ".fit",
    ".rest", ".icu", ".surf", ".monster", ".quest",
    /* New gTLDs with high abuse rates */
    ".download", ".review", ".stream", ".racing",
    ".win", ".party", ".science", ".cricket",
    ".loan", ".trade", ".webcam", ".date",
    ".faith", ".accountant", ".bid", ".gdn",
    NULL
};

/* ═══════════════════════════════════════════════════════════════════════════════
 * PHISHING KEYWORDS - Suspicious words in URL paths
 * ═══════════════════════════════════════════════════════════════════════════════ */

static const char *phishing_keywords[] = {
    "login", "signin", "sign-in", "log-in",
    "verify", "verification", "validate",
    "secure", "security", "account",
    "update", "confirm", "suspend",
    "unlock", "restore", "recover",
    "password", "credential", "auth",
    "banking", "payment", "billing",
    "wallet", "invoice", "receipt",
    "urgent", "immediately", "limited",
    "expire", "suspended", "unusual",
    "webscr", "cmd=_", "dispatch",  /* PayPal phishing patterns */
    ".php?", "redirect=", "return=",
    NULL
};

/* ═══════════════════════════════════════════════════════════════════════════════
 * FREE HOSTING / REDIRECT SERVICES - Often used to hide real URLs
 * ═══════════════════════════════════════════════════════════════════════════════ */

static const char *free_hosting_domains[] = {
    "000webhostapp.com", "weebly.com", "wixsite.com",
    "blogspot.com", "wordpress.com", "github.io",
    "netlify.app", "vercel.app", "herokuapp.com",
    "firebaseapp.com", "web.app", "pages.dev",
    "glitch.me", "repl.co", "codepen.io",
    NULL
};

static const char *redirect_services[] = {
    "bit.ly", "tinyurl.com", "t.co", "goo.gl",
    "ow.ly", "is.gd", "buff.ly", "adf.ly",
    "shorte.st", "bc.vc", "j.mp", "su.pr",
    "cutt.ly", "rebrand.ly", "short.io",
    NULL
};

/* ═══════════════════════════════════════════════════════════════════════════════
 * HOMOGRAPH LOOKALIKE CHARACTERS
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *lookalike;  /* Unicode or similar ASCII lookalike */
    char target;            /* What it looks like */
} homograph_char_t;

static const homograph_char_t homograph_chars[] = {
    {"0", 'o'}, {"О", 'O'}, {"о", 'o'},  /* Cyrillic O */
    {"1", 'l'}, {"l", 'I'}, {"І", 'I'},  /* Cyrillic I */
    {"а", 'a'}, {"е", 'e'}, {"і", 'i'},  /* Cyrillic lookalikes */
    {"ѕ", 's'}, {"р", 'p'}, {"с", 'c'},
    {"ԁ", 'd'}, {"һ", 'h'}, {"ј", 'j'},
    {"ҝ", 'k'}, {"ӏ", 'l'}, {"ո", 'n'},
    {"ԛ", 'q'}, {"г", 'r'}, {"ս", 'u'},
    {"ν", 'v'}, {"ѡ", 'w'}, {"х", 'x'},
    {"у", 'y'}, {"ʐ", 'z'},
    {"rn", 'm'},  /* ASCII trick: rn looks like m */
    {"vv", 'w'},  /* ASCII trick: vv looks like w */
    {"cl", 'd'},  /* ASCII trick: cl looks like d */
    {NULL, 0}
};

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPER FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Calculate Levenshtein distance between two strings */
static int levenshtein_distance(const char *s1, const char *s2) {
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);

    if (len1 == 0) return (int)len2;
    if (len2 == 0) return (int)len1;

    /* Use simplified algorithm for short strings */
    if (len1 > 64 || len2 > 64) return 100;  /* Too long, skip */

    int matrix[65][65];

    for (size_t i = 0; i <= len1; i++) matrix[i][0] = (int)i;
    for (size_t j = 0; j <= len2; j++) matrix[0][j] = (int)j;

    for (size_t i = 1; i <= len1; i++) {
        for (size_t j = 1; j <= len2; j++) {
            int cost = (tolower(s1[i-1]) == tolower(s2[j-1])) ? 0 : 1;
            int del = matrix[i-1][j] + 1;
            int ins = matrix[i][j-1] + 1;
            int sub = matrix[i-1][j-1] + cost;

            matrix[i][j] = del;
            if (ins < matrix[i][j]) matrix[i][j] = ins;
            if (sub < matrix[i][j]) matrix[i][j] = sub;
        }
    }

    return matrix[len1][len2];
}

/* Extract domain from URL */
static int extract_domain(const char *url, char *domain, size_t size) {
    if (!url || !domain || size == 0) return -1;

    const char *start = url;

    /* Skip scheme */
    if (strncmp(start, "https://", 8) == 0) start += 8;
    else if (strncmp(start, "http://", 7) == 0) start += 7;
    else if (strncmp(start, "//", 2) == 0) start += 2;

    /* Find end of domain (first / or : or end of string) */
    const char *end = start;
    while (*end && *end != '/' && *end != ':' && *end != '?' && *end != '#') {
        end++;
    }

    size_t len = end - start;
    if (len >= size) len = size - 1;

    strncpy(domain, start, len);
    domain[len] = '\0';

    /* Convert to lowercase */
    for (char *p = domain; *p; p++) {
        *p = tolower(*p);
    }

    return 0;
}

/* Extract TLD from domain */
static int extract_tld(const char *domain, char *tld, size_t size) {
    if (!domain || !tld || size == 0) return -1;

    const char *dot = strrchr(domain, '.');
    if (!dot) return -1;

    strncpy(tld, dot, size - 1);
    tld[size - 1] = '\0';

    return 0;
}

/* Count subdomains */
static int count_subdomains(const char *domain) {
    int count = 0;
    for (const char *p = domain; *p; p++) {
        if (*p == '.') count++;
    }
    return count;  /* Subdomains = dots (e.g., www.example.com has 1 subdomain) */
}

/* Check if string looks random (high entropy) */
static int looks_random(const char *str) {
    if (!str || strlen(str) < 8) return 0;

    int consonants = 0, vowels = 0, digits = 0;
    int max_consonant_run = 0, current_consonant_run = 0;
    size_t len = strlen(str);

    for (size_t i = 0; i < len && str[i] != '.'; i++) {
        char c = tolower(str[i]);
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') {
            vowels++;
            current_consonant_run = 0;
        } else if (isalpha(c)) {
            consonants++;
            current_consonant_run++;
            if (current_consonant_run > max_consonant_run) {
                max_consonant_run = current_consonant_run;
            }
        } else if (isdigit(c)) {
            digits++;
            current_consonant_run = 0;
        }
    }

    /* High consonant-to-vowel ratio or long consonant runs suggest randomness */
    if (vowels > 0 && consonants / vowels > 5) return 1;
    if (max_consonant_run >= 5) return 1;
    if (digits > 3 && len < 20) return 1;

    return 0;
}

/* Check if domain is an IP address */
static int is_ip_address(const char *domain) {
    int dots = 0, digits = 0;

    for (const char *p = domain; *p; p++) {
        if (*p == '.') dots++;
        else if (isdigit(*p)) digits++;
        else if (*p != ':' && *p != '[' && *p != ']') return 0;
    }

    /* IPv4: x.x.x.x */
    if (dots == 3 && digits >= 4) return 1;

    /* IPv6: contains colons */
    if (strchr(domain, ':') != NULL) return 1;

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * SCANNER IMPLEMENTATION
 * ═══════════════════════════════════════════════════════════════════════════════ */

int phantom_urlscan_init(phantom_urlscan_t *scanner) {
    if (!scanner) return -1;

    memset(scanner, 0, sizeof(*scanner));

    scanner->strict_mode = 0;
    scanner->warn_http_login = 1;
    scanner->check_homographs = 1;
    scanner->max_subdomain_depth = 3;

    /* Initialize blocklist hash table */
    for (int i = 0; i < URLSCAN_HASH_BUCKETS; i++) {
        scanner->blocklist_table[i] = NULL;
    }
    scanner->blocklist_count = 0;

    /* Initialize DNS blocking (disabled by default) */
    scanner->dns_blocking_enabled = 0;
    scanner->dns_timeout_ms = 1000;
    strcpy(scanner->dns_server, URLSCAN_DNS_QUAD9);

    /* Initialize allowlist */
    scanner->allowlist = NULL;
    scanner->allowlist_count = 0;
    scanner->allowlist_capacity = 0;

    scanner->initialized = 1;

    printf("[urlscan] Phantom URL Scanner initialized\n");
    printf("  Known brands: %d\n", (int)(sizeof(known_brands) / sizeof(known_brands[0]) - 1));
    printf("  Suspicious TLDs: %d\n", (int)(sizeof(suspicious_tlds) / sizeof(suspicious_tlds[0]) - 1));
    printf("  Phishing keywords: %d\n", (int)(sizeof(phishing_keywords) / sizeof(phishing_keywords[0]) - 1));

    /* Try to load blocklists from standard location */
    phantom_urlscan_load_blocklist_dir(scanner, "/geo/etc/blocklists");

    return 0;
}

void phantom_urlscan_shutdown(phantom_urlscan_t *scanner) {
    if (!scanner) return;

    printf("[urlscan] Scanner statistics:\n");
    printf("  Total scans: %lu\n", scanner->total_scans);
    printf("  Safe: %lu\n", scanner->safe_count);
    printf("  Suspicious: %lu\n", scanner->suspicious_count);
    printf("  Dangerous: %lu\n", scanner->dangerous_count);
    printf("  Blocked: %lu\n", scanner->blocked_count);
    printf("  Blocklist entries: %u\n", scanner->blocklist_count);

    /* Free blocklist */
    phantom_urlscan_clear_blocklist(scanner);

    memset(scanner, 0, sizeof(*scanner));
}

int phantom_urlscan_check_typosquat(const char *domain,
                                     char *target, size_t target_size) {
    if (!domain) return 0;

    /* Extract just the main domain (remove subdomains) */
    const char *main_domain = domain;
    int dots = count_subdomains(domain);
    if (dots > 1) {
        /* Find the second-to-last dot */
        const char *p = domain;
        int skip = dots - 1;
        while (*p && skip > 0) {
            if (*p == '.') skip--;
            p++;
        }
        main_domain = p;
    }

    /* Check against known brands */
    for (int i = 0; known_brands[i].brand != NULL; i++) {
        int dist = levenshtein_distance(main_domain, known_brands[i].domain);

        /* If distance is 1-2 (small typo) and not exact match, it's suspicious */
        if (dist > 0 && dist <= 2) {
            if (target && target_size > 0) {
                strncpy(target, known_brands[i].brand, target_size - 1);
                target[target_size - 1] = '\0';
            }
            return dist;
        }

        /* Also check for common substitutions */
        const char *brand_lower = known_brands[i].domain;

        /* Check for number substitutions (paypa1.com, g00gle.com) */
        char normalized[256];
        strncpy(normalized, main_domain, sizeof(normalized) - 1);
        normalized[sizeof(normalized) - 1] = '\0';

        /* Replace common number->letter substitutions */
        for (char *p = normalized; *p; p++) {
            if (*p == '0') *p = 'o';
            else if (*p == '1') *p = 'l';
            else if (*p == '3') *p = 'e';
            else if (*p == '4') *p = 'a';
            else if (*p == '5') *p = 's';
            else if (*p == '8') *p = 'b';
        }

        dist = levenshtein_distance(normalized, brand_lower);
        if (dist == 0 && strcmp(main_domain, brand_lower) != 0) {
            if (target && target_size > 0) {
                strncpy(target, known_brands[i].brand, target_size - 1);
                target[target_size - 1] = '\0';
            }
            return 1;  /* Number substitution detected */
        }
    }

    return 0;
}

int phantom_urlscan_check_tld(const char *domain, char *tld, size_t tld_size) {
    char extracted_tld[32];
    if (extract_tld(domain, extracted_tld, sizeof(extracted_tld)) != 0) {
        return 0;
    }

    for (int i = 0; suspicious_tlds[i] != NULL; i++) {
        if (strcasecmp(extracted_tld, suspicious_tlds[i]) == 0) {
            if (tld && tld_size > 0) {
                strncpy(tld, extracted_tld, tld_size - 1);
                tld[tld_size - 1] = '\0';
            }
            return 1;
        }
    }

    return 0;
}

int phantom_urlscan_check_homograph(const char *domain) {
    if (!domain) return 0;

    int count = 0;

    /* Check for punycode (xn--) which indicates IDN */
    if (strstr(domain, "xn--") != NULL) {
        count += 5;  /* Punycode is high risk */
    }

    /* Check for mixed scripts / lookalike sequences */
    for (int i = 0; homograph_chars[i].lookalike != NULL; i++) {
        if (strstr(domain, homograph_chars[i].lookalike) != NULL) {
            count++;
        }
    }

    return count;
}

int phantom_urlscan_check_path(const char *path) {
    if (!path) return 0;

    int count = 0;

    /* Convert to lowercase for comparison */
    char lower_path[1024];
    strncpy(lower_path, path, sizeof(lower_path) - 1);
    lower_path[sizeof(lower_path) - 1] = '\0';
    for (char *p = lower_path; *p; p++) {
        *p = tolower(*p);
    }

    for (int i = 0; phishing_keywords[i] != NULL; i++) {
        if (strstr(lower_path, phishing_keywords[i]) != NULL) {
            count++;
        }
    }

    return count;
}

int phantom_urlscan_check(phantom_urlscan_t *scanner,
                          const char *url,
                          urlscan_result_t *result) {
    if (!scanner || !scanner->initialized || !url || !result) {
        return -1;
    }

    memset(result, 0, sizeof(*result));
    scanner->total_scans++;

    /* Extract domain */
    if (extract_domain(url, result->domain, sizeof(result->domain)) != 0) {
        result->threat_level = URLSCAN_UNKNOWN;
        strncpy(result->reason, "Could not parse URL", sizeof(result->reason) - 1);
        return 0;
    }

    /* Check if HTTPS */
    result->is_https = (strncmp(url, "https://", 8) == 0);

    /* ─────────────────────────────────────────────────────────────────────────
     * CHECK 0a: Allowlist (skip all checks if allowed)
     * ───────────────────────────────────────────────────────────────────────── */
    if (phantom_urlscan_is_allowed(scanner, result->domain)) {
        result->threat_level = URLSCAN_SAFE;
        strncpy(result->reason, "Domain in allowlist", sizeof(result->reason) - 1);
        scanner->safe_count++;
        return 0;
    }

    /* ─────────────────────────────────────────────────────────────────────────
     * CHECK 0b: Local blocklist (hash-based lookup - Option 2)
     * ───────────────────────────────────────────────────────────────────────── */
    if (phantom_urlscan_is_blocked(scanner, result->domain)) {
        result->threat_level = URLSCAN_BLOCKED;
        result->flags |= URLSCAN_FLAG_KNOWN_MALWARE;
        strncpy(result->reason, "Domain in malware blocklist", sizeof(result->reason) - 1);
        scanner->blocked_count++;
        return 0;
    }

    /* ─────────────────────────────────────────────────────────────────────────
     * CHECK 0c: DNS-based blocking (Option 4 - Quad9/Cloudflare)
     * ───────────────────────────────────────────────────────────────────────── */
    if (scanner->dns_blocking_enabled) {
        int dns_result = phantom_urlscan_dns_check(scanner, result->domain);
        if (dns_result == 1) {
            result->threat_level = URLSCAN_BLOCKED;
            result->flags |= URLSCAN_FLAG_KNOWN_MALWARE;
            snprintf(result->reason, sizeof(result->reason),
                     "Blocked by DNS security (%s)", scanner->dns_server);
            scanner->blocked_count++;
            return 0;
        }
    }

    int score = 0;

    /* ─────────────────────────────────────────────────────────────────────────
     * CHECK 1: Typosquatting
     * ───────────────────────────────────────────────────────────────────────── */
    int typo_dist = phantom_urlscan_check_typosquat(result->domain,
                                                     result->typosquat_target,
                                                     sizeof(result->typosquat_target));
    if (typo_dist > 0) {
        result->flags |= URLSCAN_FLAG_TYPOSQUAT;
        score += 40;  /* High risk */
    }

    /* ─────────────────────────────────────────────────────────────────────────
     * CHECK 2: Suspicious TLD
     * ───────────────────────────────────────────────────────────────────────── */
    if (phantom_urlscan_check_tld(result->domain,
                                   result->suspicious_tld,
                                   sizeof(result->suspicious_tld))) {
        result->flags |= URLSCAN_FLAG_SUSPICIOUS_TLD;
        score += 20;
    }

    /* ─────────────────────────────────────────────────────────────────────────
     * CHECK 3: IP Address instead of domain
     * ───────────────────────────────────────────────────────────────────────── */
    if (is_ip_address(result->domain)) {
        result->flags |= URLSCAN_FLAG_IP_ADDRESS;
        score += 25;
    }

    /* ─────────────────────────────────────────────────────────────────────────
     * CHECK 4: Deep subdomain nesting
     * ───────────────────────────────────────────────────────────────────────── */
    result->subdomain_depth = count_subdomains(result->domain);
    if (result->subdomain_depth > scanner->max_subdomain_depth) {
        result->flags |= URLSCAN_FLAG_DEEP_SUBDOMAIN;
        score += 15;
    }

    /* ─────────────────────────────────────────────────────────────────────────
     * CHECK 5: Homograph attacks
     * ───────────────────────────────────────────────────────────────────────── */
    if (scanner->check_homographs) {
        result->homograph_chars = phantom_urlscan_check_homograph(result->domain);
        if (result->homograph_chars > 0) {
            result->flags |= URLSCAN_FLAG_HOMOGRAPH;
            score += result->homograph_chars * 10;
        }

        /* Check for punycode specifically */
        if (strstr(result->domain, "xn--") != NULL) {
            result->flags |= URLSCAN_FLAG_PUNYCODE;
            score += 15;
        }
    }

    /* ─────────────────────────────────────────────────────────────────────────
     * CHECK 6: Phishing keywords in path
     * ───────────────────────────────────────────────────────────────────────── */
    const char *path = strchr(url, '/');
    if (path && path[1] != '\0') {  /* Skip the third slash in https:// */
        path = strchr(path + 2, '/');
        if (path) {
            int keyword_count = phantom_urlscan_check_path(path);
            if (keyword_count > 0) {
                result->flags |= URLSCAN_FLAG_PHISHING_WORDS;
                score += keyword_count * 10;

                /* HTTP + login keywords = very suspicious */
                if (!result->is_https && keyword_count >= 2) {
                    result->flags |= URLSCAN_FLAG_HTTP_LOGIN;
                    score += 20;
                }
            }
        }
    }

    /* ─────────────────────────────────────────────────────────────────────────
     * CHECK 7: Long or random-looking domain
     * ───────────────────────────────────────────────────────────────────────── */
    if (strlen(result->domain) > 50) {
        result->flags |= URLSCAN_FLAG_LONG_DOMAIN;
        score += 10;
    }

    if (looks_random(result->domain)) {
        result->flags |= URLSCAN_FLAG_RANDOM_DOMAIN;
        score += 15;
    }

    /* ─────────────────────────────────────────────────────────────────────────
     * CHECK 8: Free hosting / redirect services
     * ───────────────────────────────────────────────────────────────────────── */
    for (int i = 0; free_hosting_domains[i] != NULL; i++) {
        if (strstr(result->domain, free_hosting_domains[i]) != NULL) {
            result->flags |= URLSCAN_FLAG_FREE_HOSTING;
            score += 10;
            break;
        }
    }

    for (int i = 0; redirect_services[i] != NULL; i++) {
        if (strcasecmp(result->domain, redirect_services[i]) == 0) {
            result->flags |= URLSCAN_FLAG_REDIRECT_CHAIN;
            score += 15;
            break;
        }
    }

    /* ─────────────────────────────────────────────────────────────────────────
     * CHECK 9: Data URI
     * ───────────────────────────────────────────────────────────────────────── */
    if (strncmp(url, "data:", 5) == 0) {
        result->flags |= URLSCAN_FLAG_DATA_URI;
        score += 50;  /* Very suspicious */
    }

    /* ─────────────────────────────────────────────────────────────────────────
     * DETERMINE THREAT LEVEL
     * ───────────────────────────────────────────────────────────────────────── */
    result->score = score;

    if (score >= 70) {
        result->threat_level = URLSCAN_DANGEROUS;
        scanner->dangerous_count++;
    } else if (score >= 50) {
        result->threat_level = URLSCAN_WARNING;
        scanner->suspicious_count++;
    } else if (score >= 30) {
        result->threat_level = URLSCAN_SUSPICIOUS;
        scanner->suspicious_count++;
    } else if (score >= 10) {
        result->threat_level = URLSCAN_UNKNOWN;
    } else {
        result->threat_level = URLSCAN_SAFE;
        scanner->safe_count++;
    }

    /* Build reason string */
    char *reason = result->reason;
    size_t remaining = sizeof(result->reason);
    int written = 0;

    if (result->flags & URLSCAN_FLAG_TYPOSQUAT) {
        written = snprintf(reason, remaining, "Possible typosquatting of %s. ",
                          result->typosquat_target);
        reason += written; remaining -= written;
    }
    if (result->flags & URLSCAN_FLAG_SUSPICIOUS_TLD) {
        written = snprintf(reason, remaining, "Suspicious TLD (%s). ",
                          result->suspicious_tld);
        reason += written; remaining -= written;
    }
    if (result->flags & URLSCAN_FLAG_IP_ADDRESS) {
        written = snprintf(reason, remaining, "IP address instead of domain. ");
        reason += written; remaining -= written;
    }
    if (result->flags & URLSCAN_FLAG_DEEP_SUBDOMAIN) {
        written = snprintf(reason, remaining, "Excessive subdomains (%d). ",
                          result->subdomain_depth);
        reason += written; remaining -= written;
    }
    if (result->flags & URLSCAN_FLAG_HOMOGRAPH) {
        written = snprintf(reason, remaining, "Possible homograph attack. ");
        reason += written; remaining -= written;
    }
    if (result->flags & URLSCAN_FLAG_PHISHING_WORDS) {
        written = snprintf(reason, remaining, "Suspicious keywords in URL. ");
        reason += written; remaining -= written;
    }
    if (result->flags & URLSCAN_FLAG_HTTP_LOGIN) {
        written = snprintf(reason, remaining, "Login page over HTTP (insecure). ");
        reason += written; remaining -= written;
    }
    if (result->flags & URLSCAN_FLAG_RANDOM_DOMAIN) {
        written = snprintf(reason, remaining, "Random-looking domain. ");
        reason += written; remaining -= written;
    }
    if (result->flags & URLSCAN_FLAG_REDIRECT_CHAIN) {
        written = snprintf(reason, remaining, "URL shortener (destination hidden). ");
        reason += written; remaining -= written;
    }
    if (result->flags & URLSCAN_FLAG_DATA_URI) {
        snprintf(reason, remaining, "Data URI (can hide malicious content). ");
    }

    if (result->reason[0] == '\0') {
        strncpy(result->reason, "No threats detected", sizeof(result->reason) - 1);
    }

    return 0;
}

urlscan_threat_t phantom_urlscan_quick(phantom_urlscan_t *scanner,
                                        const char *url) {
    urlscan_result_t result;
    if (phantom_urlscan_check(scanner, url, &result) != 0) {
        return URLSCAN_UNKNOWN;
    }
    return result.threat_level;
}

const char *phantom_urlscan_threat_str(urlscan_threat_t level) {
    switch (level) {
        case URLSCAN_SAFE:       return "Safe";
        case URLSCAN_UNKNOWN:    return "Unknown";
        case URLSCAN_SUSPICIOUS: return "Suspicious";
        case URLSCAN_WARNING:    return "Warning";
        case URLSCAN_DANGEROUS:  return "Dangerous";
        case URLSCAN_BLOCKED:    return "Blocked";
        default:                 return "Unknown";
    }
}

const char *phantom_urlscan_threat_icon(urlscan_threat_t level) {
    switch (level) {
        case URLSCAN_SAFE:       return "✓";
        case URLSCAN_UNKNOWN:    return "?";
        case URLSCAN_SUSPICIOUS: return "⚠";
        case URLSCAN_WARNING:    return "⚠";
        case URLSCAN_DANGEROUS:  return "🚫";
        case URLSCAN_BLOCKED:    return "⛔";
        default:                 return "?";
    }
}

const char *phantom_urlscan_threat_class(urlscan_threat_t level) {
    switch (level) {
        case URLSCAN_SAFE:       return "secure";
        case URLSCAN_UNKNOWN:    return "";
        case URLSCAN_SUSPICIOUS: return "warning";
        case URLSCAN_WARNING:    return "warning";
        case URLSCAN_DANGEROUS:  return "insecure";
        case URLSCAN_BLOCKED:    return "insecure";
        default:                 return "";
    }
}

void phantom_urlscan_format_flags(uint32_t flags, char *buf, size_t size) {
    if (!buf || size == 0) return;

    buf[0] = '\0';
    char *p = buf;
    size_t remaining = size;

    if (flags & URLSCAN_FLAG_TYPOSQUAT) {
        int n = snprintf(p, remaining, "Typosquatting ");
        p += n; remaining -= n;
    }
    if (flags & URLSCAN_FLAG_SUSPICIOUS_TLD) {
        int n = snprintf(p, remaining, "BadTLD ");
        p += n; remaining -= n;
    }
    if (flags & URLSCAN_FLAG_IP_ADDRESS) {
        int n = snprintf(p, remaining, "IP ");
        p += n; remaining -= n;
    }
    if (flags & URLSCAN_FLAG_DEEP_SUBDOMAIN) {
        int n = snprintf(p, remaining, "DeepSub ");
        p += n; remaining -= n;
    }
    if (flags & URLSCAN_FLAG_HOMOGRAPH) {
        int n = snprintf(p, remaining, "Homograph ");
        p += n; remaining -= n;
    }
    if (flags & URLSCAN_FLAG_PHISHING_WORDS) {
        int n = snprintf(p, remaining, "Phishing ");
        p += n; remaining -= n;
    }
    if (flags & URLSCAN_FLAG_REDIRECT_CHAIN) {
        snprintf(p, remaining, "Redirect ");
    }
}

void phantom_urlscan_get_stats(phantom_urlscan_t *scanner,
                               uint64_t *total, uint64_t *safe,
                               uint64_t *suspicious, uint64_t *dangerous) {
    if (!scanner) return;

    if (total) *total = scanner->total_scans;
    if (safe) *safe = scanner->safe_count;
    if (suspicious) *suspicious = scanner->suspicious_count;
    if (dangerous) *dangerous = scanner->dangerous_count;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HASH FUNCTIONS FOR BLOCKLIST LOOKUP (Option 2)
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Simple DJB2 hash function - fast and has good distribution */
static uint32_t hash_domain(const char *domain) {
    uint32_t hash = 5381;
    int c;

    while ((c = (unsigned char)*domain++)) {
        /* Convert to lowercase during hashing */
        if (c >= 'A' && c <= 'Z') c += 32;
        hash = ((hash << 5) + hash) + c;  /* hash * 33 + c */
    }

    return hash;
}

/* Get bucket index from hash */
static uint32_t hash_to_bucket(uint32_t hash) {
    return hash % URLSCAN_HASH_BUCKETS;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * BLOCKLIST MANAGEMENT (Option 1: External blocklists)
 * ═══════════════════════════════════════════════════════════════════════════════ */

int phantom_urlscan_add_blocked_domain(phantom_urlscan_t *scanner,
                                        const char *domain) {
    if (!scanner || !domain || strlen(domain) == 0) return -1;
    if (scanner->blocklist_count >= URLSCAN_MAX_BLOCKLIST) return -1;

    /* Skip common prefixes */
    const char *clean_domain = domain;
    if (strncmp(clean_domain, "www.", 4) == 0) {
        clean_domain += 4;
    }

    /* Check if already in blocklist */
    if (phantom_urlscan_is_blocked(scanner, clean_domain)) {
        return 0;  /* Already blocked */
    }

    /* Create new entry */
    urlscan_blocklist_entry_t *entry = malloc(sizeof(urlscan_blocklist_entry_t));
    if (!entry) return -1;

    uint32_t hash = hash_domain(clean_domain);
    entry->hash_prefix = hash;
    strncpy(entry->domain, clean_domain, URLSCAN_MAX_DOMAIN - 1);
    entry->domain[URLSCAN_MAX_DOMAIN - 1] = '\0';

    /* Convert to lowercase */
    for (char *p = entry->domain; *p; p++) {
        *p = tolower(*p);
    }

    /* Insert into hash table */
    uint32_t bucket = hash_to_bucket(hash);
    entry->next = scanner->blocklist_table[bucket];
    scanner->blocklist_table[bucket] = entry;
    scanner->blocklist_count++;

    return 0;
}

int phantom_urlscan_is_blocked(phantom_urlscan_t *scanner,
                                const char *domain) {
    if (!scanner || !domain) return 0;

    /* Skip www. prefix */
    const char *check_domain = domain;
    if (strncmp(check_domain, "www.", 4) == 0) {
        check_domain += 4;
    }

    /* Convert to lowercase for comparison */
    char lower_domain[URLSCAN_MAX_DOMAIN];
    strncpy(lower_domain, check_domain, sizeof(lower_domain) - 1);
    lower_domain[sizeof(lower_domain) - 1] = '\0';
    for (char *p = lower_domain; *p; p++) {
        *p = tolower(*p);
    }

    /* Hash lookup */
    uint32_t hash = hash_domain(lower_domain);
    uint32_t bucket = hash_to_bucket(hash);

    urlscan_blocklist_entry_t *entry = scanner->blocklist_table[bucket];
    while (entry) {
        if (entry->hash_prefix == hash &&
            strcmp(entry->domain, lower_domain) == 0) {
            return 1;  /* Found in blocklist */
        }
        entry = entry->next;
    }

    /* Also check parent domains (e.g., if evil.com is blocked, sub.evil.com should be too) */
    const char *dot = strchr(lower_domain, '.');
    while (dot && *(dot + 1)) {
        const char *parent = dot + 1;
        hash = hash_domain(parent);
        bucket = hash_to_bucket(hash);

        entry = scanner->blocklist_table[bucket];
        while (entry) {
            if (entry->hash_prefix == hash &&
                strcmp(entry->domain, parent) == 0) {
                return 1;  /* Parent domain is blocked */
            }
            entry = entry->next;
        }

        dot = strchr(parent, '.');
    }

    return 0;
}

int phantom_urlscan_add_allowed_domain(phantom_urlscan_t *scanner,
                                        const char *domain) {
    if (!scanner || !domain) return -1;

    /* Grow allowlist if needed */
    if (scanner->allowlist_count >= scanner->allowlist_capacity) {
        uint32_t new_capacity = scanner->allowlist_capacity == 0 ? 64 :
                                scanner->allowlist_capacity * 2;
        char **new_list = realloc(scanner->allowlist,
                                   new_capacity * sizeof(char*));
        if (!new_list) return -1;
        scanner->allowlist = new_list;
        scanner->allowlist_capacity = new_capacity;
    }

    /* Add domain */
    scanner->allowlist[scanner->allowlist_count] = strdup(domain);
    if (!scanner->allowlist[scanner->allowlist_count]) return -1;

    /* Convert to lowercase */
    for (char *p = scanner->allowlist[scanner->allowlist_count]; *p; p++) {
        *p = tolower(*p);
    }

    scanner->allowlist_count++;
    return 0;
}

int phantom_urlscan_is_allowed(phantom_urlscan_t *scanner,
                                const char *domain) {
    if (!scanner || !domain) return 0;

    char lower_domain[URLSCAN_MAX_DOMAIN];
    strncpy(lower_domain, domain, sizeof(lower_domain) - 1);
    lower_domain[sizeof(lower_domain) - 1] = '\0';
    for (char *p = lower_domain; *p; p++) {
        *p = tolower(*p);
    }

    for (uint32_t i = 0; i < scanner->allowlist_count; i++) {
        if (strcmp(scanner->allowlist[i], lower_domain) == 0) {
            return 1;
        }
        /* Check if allowlist entry is a parent domain */
        size_t allow_len = strlen(scanner->allowlist[i]);
        size_t domain_len = strlen(lower_domain);
        if (domain_len > allow_len + 1) {
            const char *suffix = lower_domain + domain_len - allow_len;
            if (*(suffix - 1) == '.' &&
                strcmp(suffix, scanner->allowlist[i]) == 0) {
                return 1;
            }
        }
    }

    return 0;
}

void phantom_urlscan_clear_blocklist(phantom_urlscan_t *scanner) {
    if (!scanner) return;

    for (int i = 0; i < URLSCAN_HASH_BUCKETS; i++) {
        urlscan_blocklist_entry_t *entry = scanner->blocklist_table[i];
        while (entry) {
            urlscan_blocklist_entry_t *next = entry->next;
            free(entry);
            entry = next;
        }
        scanner->blocklist_table[i] = NULL;
    }
    scanner->blocklist_count = 0;

    /* Clear allowlist */
    for (uint32_t i = 0; i < scanner->allowlist_count; i++) {
        free(scanner->allowlist[i]);
    }
    free(scanner->allowlist);
    scanner->allowlist = NULL;
    scanner->allowlist_count = 0;
    scanner->allowlist_capacity = 0;
}

uint32_t phantom_urlscan_get_blocklist_count(phantom_urlscan_t *scanner) {
    return scanner ? scanner->blocklist_count : 0;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * BLOCKLIST FILE LOADING (Option 1: External blocklists)
 * ═══════════════════════════════════════════════════════════════════════════════ */

int phantom_urlscan_load_hosts_blocklist(phantom_urlscan_t *scanner,
                                          const char *filepath) {
    if (!scanner || !filepath) return -1;

    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        printf("[urlscan] Could not open blocklist: %s\n", filepath);
        return -1;
    }

    char line[1024];
    int loaded = 0;
    int errors = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* Skip comments and empty lines */
        char *p = line;
        while (*p && isspace(*p)) p++;

        if (*p == '#' || *p == '\0' || *p == '\n') continue;

        /* Parse hosts format: "IP domain [domain2 ...]" */
        char ip[64], domain[256];

        /* Handle different hosts formats */
        if (sscanf(p, "%63s %255s", ip, domain) >= 2) {
            /* Skip localhost entries and common false positives */
            if (strcmp(domain, "localhost") == 0 ||
                strcmp(domain, "localhost.localdomain") == 0 ||
                strcmp(domain, "local") == 0 ||
                strcmp(domain, "broadcasthost") == 0 ||
                strstr(domain, "._") != NULL) {  /* Skip mDNS entries */
                continue;
            }

            /* Only accept blocking IPs (0.0.0.0 or 127.0.0.1) */
            if (strcmp(ip, "0.0.0.0") == 0 ||
                strcmp(ip, "127.0.0.1") == 0 ||
                strncmp(ip, "::1", 3) == 0) {

                if (phantom_urlscan_add_blocked_domain(scanner, domain) == 0) {
                    loaded++;
                } else {
                    errors++;
                }
            }
        }
    }

    fclose(fp);

    printf("[urlscan] Loaded %d domains from %s (errors: %d)\n",
           loaded, filepath, errors);

    return loaded;
}

int phantom_urlscan_load_domain_blocklist(phantom_urlscan_t *scanner,
                                           const char *filepath) {
    if (!scanner || !filepath) return -1;

    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        printf("[urlscan] Could not open blocklist: %s\n", filepath);
        return -1;
    }

    char line[1024];
    int loaded = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* Skip comments and empty lines */
        char *p = line;
        while (*p && isspace(*p)) p++;

        if (*p == '#' || *p == '\0' || *p == '\n') continue;

        /* Remove trailing whitespace and newline */
        char *end = p + strlen(p) - 1;
        while (end > p && (isspace(*end) || *end == '\n' || *end == '\r')) {
            *end-- = '\0';
        }

        /* Skip if too short */
        if (strlen(p) < 3) continue;

        /* Skip URLs - extract domain if present */
        if (strncmp(p, "http://", 7) == 0) p += 7;
        else if (strncmp(p, "https://", 8) == 0) p += 8;

        /* Remove path if present */
        char *slash = strchr(p, '/');
        if (slash) *slash = '\0';

        /* Add domain */
        if (strlen(p) >= 3 && strchr(p, '.')) {
            if (phantom_urlscan_add_blocked_domain(scanner, p) == 0) {
                loaded++;
            }
        }
    }

    fclose(fp);

    printf("[urlscan] Loaded %d domains from %s\n", loaded, filepath);

    return loaded;
}

int phantom_urlscan_load_blocklist_dir(phantom_urlscan_t *scanner,
                                        const char *dirpath) {
    if (!scanner || !dirpath) return -1;

    DIR *dir = opendir(dirpath);
    if (!dir) {
        printf("[urlscan] Could not open blocklist directory: %s\n", dirpath);
        return -1;
    }

    int total_loaded = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (entry->d_name[0] == '.') continue;

        /* Build full path */
        char filepath[4096];
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);

        /* Determine file type by extension or name */
        const char *ext = strrchr(entry->d_name, '.');
        int loaded = 0;

        if (ext && (strcasecmp(ext, ".hosts") == 0 ||
                    strcasecmp(entry->d_name, "hosts") == 0)) {
            loaded = phantom_urlscan_load_hosts_blocklist(scanner, filepath);
        } else if (ext && (strcasecmp(ext, ".txt") == 0 ||
                           strcasecmp(ext, ".list") == 0 ||
                           strcasecmp(ext, ".domains") == 0)) {
            loaded = phantom_urlscan_load_domain_blocklist(scanner, filepath);
        } else {
            /* Try as domain list by default */
            loaded = phantom_urlscan_load_domain_blocklist(scanner, filepath);
        }

        if (loaded > 0) {
            total_loaded += loaded;
        }
    }

    closedir(dir);

    printf("[urlscan] Total domains loaded from %s: %d\n", dirpath, total_loaded);

    return total_loaded;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * DNS-BASED BLOCKING (Option 4: Quad9, Cloudflare)
 * ═══════════════════════════════════════════════════════════════════════════════ */

int phantom_urlscan_enable_dns_blocking(phantom_urlscan_t *scanner,
                                         const char *dns_server,
                                         int timeout_ms) {
    if (!scanner) return -1;

    scanner->dns_blocking_enabled = 1;
    scanner->dns_timeout_ms = timeout_ms > 0 ? timeout_ms : 1000;

    if (dns_server) {
        strncpy(scanner->dns_server, dns_server, sizeof(scanner->dns_server) - 1);
        scanner->dns_server[sizeof(scanner->dns_server) - 1] = '\0';
    } else {
        /* Default to Quad9 */
        strcpy(scanner->dns_server, URLSCAN_DNS_QUAD9);
    }

    printf("[urlscan] DNS blocking enabled using %s (timeout: %dms)\n",
           scanner->dns_server, scanner->dns_timeout_ms);

    return 0;
}

void phantom_urlscan_disable_dns_blocking(phantom_urlscan_t *scanner) {
    if (!scanner) return;
    scanner->dns_blocking_enabled = 0;
    printf("[urlscan] DNS blocking disabled\n");
}

/* Simple DNS query to check if domain is blocked
 * Quad9 and Cloudflare return NXDOMAIN or a blocked IP for malicious domains */
int phantom_urlscan_dns_check(phantom_urlscan_t *scanner,
                               const char *domain) {
    if (!scanner || !domain || !scanner->dns_blocking_enabled) {
        return 0;  /* Not checking */
    }

    /* Create UDP socket for DNS query */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }

    /* Set non-blocking */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    /* DNS server address */
    struct sockaddr_in dns_addr;
    memset(&dns_addr, 0, sizeof(dns_addr));
    dns_addr.sin_family = AF_INET;
    dns_addr.sin_port = htons(53);
    inet_pton(AF_INET, scanner->dns_server, &dns_addr.sin_addr);

    /* Build DNS query packet */
    unsigned char query[512];
    memset(query, 0, sizeof(query));

    /* DNS header - use secure random ID to prevent DNS cache poisoning */
    uint16_t id = secure_random_dns_id();
    query[0] = (id >> 8) & 0xFF;
    query[1] = id & 0xFF;
    query[2] = 0x01;  /* RD (recursion desired) */
    query[3] = 0x00;
    query[4] = 0x00;  /* QDCOUNT = 1 */
    query[5] = 0x01;
    query[6] = 0x00;  /* ANCOUNT = 0 */
    query[7] = 0x00;
    query[8] = 0x00;  /* NSCOUNT = 0 */
    query[9] = 0x00;
    query[10] = 0x00; /* ARCOUNT = 0 */
    query[11] = 0x00;

    /* DNS question - encode domain name */
    int qpos = 12;
    const char *p = domain;
    while (*p) {
        const char *dot = strchr(p, '.');
        int len = dot ? (int)(dot - p) : (int)strlen(p);

        if (len > 63 || qpos + len + 2 >= 500) {
            close(sock);
            return -1;  /* Invalid domain */
        }

        query[qpos++] = (unsigned char)len;
        memcpy(&query[qpos], p, len);
        qpos += len;

        if (dot) {
            p = dot + 1;
        } else {
            break;
        }
    }
    query[qpos++] = 0;  /* End of domain name */

    /* QTYPE = A (1) */
    query[qpos++] = 0x00;
    query[qpos++] = 0x01;

    /* QCLASS = IN (1) */
    query[qpos++] = 0x00;
    query[qpos++] = 0x01;

    /* Send query */
    if (sendto(sock, query, qpos, 0,
               (struct sockaddr*)&dns_addr, sizeof(dns_addr)) < 0) {
        close(sock);
        return -1;
    }

    /* Wait for response with timeout */
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, scanner->dns_timeout_ms);
    if (ret <= 0) {
        close(sock);
        return -1;  /* Timeout or error */
    }

    /* Receive response */
    unsigned char response[512];
    ssize_t rlen = recv(sock, response, sizeof(response), 0);
    close(sock);

    if (rlen < 12) {
        return -1;  /* Invalid response */
    }

    /* Check response code */
    int rcode = response[3] & 0x0F;

    if (rcode == 3) {
        /* NXDOMAIN - domain blocked by DNS provider */
        return 1;
    }

    if (rcode == 0) {
        /* NOERROR - check if response contains blocked IP
         * Quad9 returns 0.0.0.0 or specific blocked IPs
         * Cloudflare returns 0.0.0.0 for blocked domains */

        /* Parse answer count */
        int ancount = (response[6] << 8) | response[7];

        if (ancount > 0) {
            /* Skip question section to find answers */
            int pos = 12;
            while (pos < rlen && response[pos] != 0) {
                if ((response[pos] & 0xC0) == 0xC0) {
                    pos += 2;
                    break;
                }
                pos += response[pos] + 1;
            }
            if (response[pos] == 0) pos++;
            pos += 4;  /* Skip QTYPE and QCLASS */

            /* Check first answer */
            if (pos + 12 <= rlen) {
                /* Skip name (compressed or not) */
                if ((response[pos] & 0xC0) == 0xC0) {
                    pos += 2;
                } else {
                    while (pos < rlen && response[pos] != 0) {
                        pos += response[pos] + 1;
                    }
                    pos++;
                }

                if (pos + 10 <= rlen) {
                    /* Get record type and data length */
                    uint16_t rtype = (response[pos] << 8) | response[pos + 1];
                    uint16_t rdlength = (response[pos + 8] << 8) | response[pos + 9];

                    /* If A record (type 1) with 4-byte address */
                    if (rtype == 1 && rdlength == 4 && pos + 14 <= rlen) {
                        unsigned char *ip = &response[pos + 10];

                        /* Check for blocked IPs (0.0.0.0, 127.0.0.1, etc) */
                        if ((ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) ||
                            (ip[0] == 127 && ip[1] == 0 && ip[2] == 0 && ip[3] == 1)) {
                            return 1;  /* Blocked */
                        }
                    }
                }
            }
        }
    }

    return 0;  /* Not blocked */
}
