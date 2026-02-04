/*
 * ==============================================================================
 *                            PHANTOM AI SUBSYSTEM
 *                      "To Create, Not To Destroy"
 * ==============================================================================
 *
 * Implementation of AI capabilities for PhantomOS.
 * Supports multiple backends: local models, Anthropic Claude, OpenAI, custom.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#include "phantom_ai.h"
#include "phantom_ai_builtin.h"
#include "phantom.h"
#include "governor.h"

/* -----------------------------------------------------------------------------
 * Internal Helpers
 * ----------------------------------------------------------------------------- */

/* Destructive patterns the AI must never generate */
static const char *FORBIDDEN_PATTERNS[] = {
    "unlink", "remove", "rmdir", "rmtree",
    "delete", "del ", "erase", "shred", "wipe",
    "kill(", "abort(", "exit(", "SIGKILL", "SIGTERM",
    "truncate", "ftruncate",
    "DROP TABLE", "DELETE FROM", "TRUNCATE TABLE",
    "rm -rf", "rm -r", "deltree",
    "destroy", "obliterate", "annihilate",
    NULL
};

/* Check if text contains forbidden patterns */
static int contains_forbidden_pattern(const char *text) {
    if (!text) return 0;

    for (int i = 0; FORBIDDEN_PATTERNS[i]; i++) {
        if (strcasestr(text, FORBIDDEN_PATTERNS[i])) {
            return 1;
        }
    }
    return 0;
}

/* Sanitize AI output to remove any forbidden patterns */
static void sanitize_ai_output(char *output) {
    if (!output) return;

    /* Replace forbidden patterns with safe alternatives */
    char *p;

    /* delete -> hide */
    while ((p = strcasestr(output, "delete")) != NULL) {
        memcpy(p, "hide  ", 6);
    }

    /* remove -> hide */
    while ((p = strcasestr(output, "remove")) != NULL) {
        memcpy(p, "hide  ", 6);
    }

    /* kill -> suspend */
    while ((p = strcasestr(output, "kill")) != NULL) {
        if (p[4] == '(' || p[4] == ' ' || p[4] == '\0') {
            memcpy(p, "susp", 4);
        }
    }

    /* unlink -> hide */
    while ((p = strcasestr(output, "unlink")) != NULL) {
        memcpy(p, "hide  ", 6);
    }
}

/* Add message to conversation history */
static void add_to_history(phantom_ai_t *ai, int is_user, const char *content) {
    if (!ai || !content) return;

    phantom_ai_message_t *msg = malloc(sizeof(phantom_ai_message_t));
    if (!msg) return;

    msg->timestamp = time(NULL);
    msg->is_user = is_user;
    strncpy(msg->content, content, PHANTOM_AI_MAX_PROMPT - 1);
    msg->content[PHANTOM_AI_MAX_PROMPT - 1] = '\0';
    msg->next = NULL;

    /* Add to end of history */
    if (!ai->history) {
        ai->history = msg;
    } else {
        phantom_ai_message_t *tail = ai->history;
        while (tail->next) tail = tail->next;
        tail->next = msg;
    }

    ai->history_count++;

    /* Trim if too long */
    while (ai->history_count > PHANTOM_AI_MAX_HISTORY && ai->history) {
        phantom_ai_message_t *old = ai->history;
        ai->history = old->next;
        free(old);
        ai->history_count--;
    }
}

/* Build conversation context */
static void build_context(phantom_ai_t *ai, char *context, size_t size) {
    if (!ai || !context) return;

    context[0] = '\0';
    size_t pos = 0;

    /* Add system prompt */
    pos += snprintf(context + pos, size - pos, "%s\n\n", ai->system_prompt);

    /* Add recent history */
    phantom_ai_message_t *msg = ai->history;
    while (msg && pos < size - 1024) {
        const char *role = msg->is_user ? "User" : "Assistant";
        pos += snprintf(context + pos, size - pos, "%s: %s\n\n", role, msg->content);
        msg = msg->next;
    }
}

/* -----------------------------------------------------------------------------
 * HTTP/API Communication (Simplified)
 * ----------------------------------------------------------------------------- */

/* Simple HTTP POST for API calls */
static int http_post_json(const char *host, int port, const char *path,
                          const char *api_key, const char *json_body,
                          char *response, size_t response_size, int use_https) {
    (void)use_https; /* TODO: Implement HTTPS with OpenSSL */

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct hostent *server = gethostbyname(host);
    if (!server) {
        close(sock);
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return -1;
    }

    /* Build HTTP request */
    char request[PHANTOM_AI_MAX_CONTEXT];
    int content_length = strlen(json_body);

    int req_len = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Authorization: Bearer %s\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        path, host, content_length, api_key ? api_key : "", json_body);

    if (send(sock, request, req_len, 0) < 0) {
        close(sock);
        return -1;
    }

    /* Read response */
    size_t total_read = 0;
    char buffer[4096];
    ssize_t bytes_read;

    while ((bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';
        if (total_read + bytes_read < response_size) {
            memcpy(response + total_read, buffer, bytes_read);
            total_read += bytes_read;
        }
    }
    response[total_read] = '\0';

    close(sock);
    return 0;
}

/* Extract JSON string value (simple parser) */
static int extract_json_string(const char *json, const char *key, char *value, size_t size) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *p = strstr(json, search);
    if (!p) return -1;

    p = strchr(p + strlen(search), ':');
    if (!p) return -1;

    /* Skip whitespace and opening quote */
    while (*p && (*p == ':' || *p == ' ' || *p == '\t')) p++;
    if (*p != '"') return -1;
    p++;

    /* Copy until closing quote */
    size_t i = 0;
    while (*p && *p != '"' && i < size - 1) {
        if (*p == '\\' && *(p+1)) {
            p++;
            switch (*p) {
                case 'n': value[i++] = '\n'; break;
                case 't': value[i++] = '\t'; break;
                case 'r': value[i++] = '\r'; break;
                case '"': value[i++] = '"'; break;
                case '\\': value[i++] = '\\'; break;
                default: value[i++] = *p; break;
            }
        } else {
            value[i++] = *p;
        }
        p++;
    }
    value[i] = '\0';
    return 0;
}

/* -----------------------------------------------------------------------------
 * Local Model Communication (Ollama-style)
 * ----------------------------------------------------------------------------- */

static int local_model_request(phantom_ai_t *ai, const char *prompt,
                               char *response, size_t response_size) {
    if (!ai || !prompt || !response) return -1;

    /* Connect to local model server (e.g., Ollama on port 11434) */
    int port = ai->config.local_port ? ai->config.local_port : 11434;

    /* Build request JSON */
    char json_body[PHANTOM_AI_MAX_CONTEXT];
    snprintf(json_body, sizeof(json_body),
        "{"
        "\"model\": \"%s\","
        "\"prompt\": \"%s\","
        "\"stream\": false"
        "}",
        ai->config.model_name[0] ? ai->config.model_name : "llama2",
        prompt);

    /* Escape the prompt properly */
    /* TODO: Proper JSON escaping */

    char http_response[PHANTOM_AI_MAX_RESPONSE * 2];
    if (http_post_json("localhost", port, "/api/generate", NULL,
                       json_body, http_response, sizeof(http_response), 0) < 0) {
        snprintf(response, response_size, "[Local model not available. "
                 "Please ensure Ollama is running on port %d]", port);
        return -1;
    }

    /* Extract response */
    if (extract_json_string(http_response, "response", response, response_size) < 0) {
        strncpy(response, "[Failed to parse model response]", response_size - 1);
        return -1;
    }

    return 0;
}

/* -----------------------------------------------------------------------------
 * Anthropic Claude API
 * ----------------------------------------------------------------------------- */

static int anthropic_request(phantom_ai_t *ai, const char *prompt,
                             char *response, size_t response_size) {
    if (!ai || !prompt || !response) return -1;

    if (!ai->config.api_key[0]) {
        snprintf(response, response_size,
                 "[Anthropic API key not configured. Use 'ai config key <your-key>']");
        return -1;
    }

    /* Build messages JSON */
    char json_body[PHANTOM_AI_MAX_CONTEXT];

    /* Escape special characters in prompt */
    char escaped_prompt[PHANTOM_AI_MAX_PROMPT * 2];
    size_t j = 0;
    for (size_t i = 0; prompt[i] && j < sizeof(escaped_prompt) - 2; i++) {
        if (prompt[i] == '"' || prompt[i] == '\\') {
            escaped_prompt[j++] = '\\';
        }
        if (prompt[i] == '\n') {
            escaped_prompt[j++] = '\\';
            escaped_prompt[j++] = 'n';
        } else if (prompt[i] == '\r') {
            escaped_prompt[j++] = '\\';
            escaped_prompt[j++] = 'r';
        } else if (prompt[i] == '\t') {
            escaped_prompt[j++] = '\\';
            escaped_prompt[j++] = 't';
        } else {
            escaped_prompt[j++] = prompt[i];
        }
    }
    escaped_prompt[j] = '\0';

    /* Escape system prompt too */
    char escaped_system[PHANTOM_AI_MAX_CONTEXT * 2];
    j = 0;
    for (size_t i = 0; ai->system_prompt[i] && j < sizeof(escaped_system) - 2; i++) {
        if (ai->system_prompt[i] == '"' || ai->system_prompt[i] == '\\') {
            escaped_system[j++] = '\\';
        }
        if (ai->system_prompt[i] == '\n') {
            escaped_system[j++] = '\\';
            escaped_system[j++] = 'n';
        } else {
            escaped_system[j++] = ai->system_prompt[i];
        }
    }
    escaped_system[j] = '\0';

    snprintf(json_body, sizeof(json_body),
        "{"
        "\"model\": \"%s\","
        "\"max_tokens\": %d,"
        "\"system\": \"%.32000s\","
        "\"messages\": [{\"role\": \"user\", \"content\": \"%.32000s\"}]"
        "}",
        ai->config.model_name[0] ? ai->config.model_name : "claude-3-haiku-20240307",
        ai->config.max_tokens > 0 ? ai->config.max_tokens : 1024,
        escaped_system,
        escaped_prompt);

    char http_response[PHANTOM_AI_MAX_RESPONSE * 2];

    /* Note: This won't work without HTTPS - just a placeholder */
    if (http_post_json("api.anthropic.com", 443, "/v1/messages",
                       ai->config.api_key, json_body,
                       http_response, sizeof(http_response), 1) < 0) {
        snprintf(response, response_size,
                 "[Failed to connect to Anthropic API. Note: HTTPS required - "
                 "consider using local model with Ollama for testing]");
        return -1;
    }

    /* Extract content from response */
    if (extract_json_string(http_response, "text", response, response_size) < 0) {
        /* Try to extract error */
        char error[256];
        if (extract_json_string(http_response, "message", error, sizeof(error)) == 0) {
            snprintf(response, response_size, "[API Error: %s]", error);
        } else {
            strncpy(response, "[Failed to parse API response]", response_size - 1);
        }
        return -1;
    }

    return 0;
}

/* -----------------------------------------------------------------------------
 * Lifecycle Functions
 * ----------------------------------------------------------------------------- */

int phantom_ai_init(phantom_ai_t *ai, struct phantom_kernel *kernel,
                    const phantom_ai_config_t *config) {
    if (!ai) return -1;

    memset(ai, 0, sizeof(phantom_ai_t));
    ai->kernel = kernel;

    /* Copy config or use defaults */
    if (config) {
        memcpy(&ai->config, config, sizeof(phantom_ai_config_t));
    } else {
        ai->config.provider = PHANTOM_AI_PROVIDER_LOCAL;
        ai->config.capabilities = PHANTOM_AI_CAP_ALL;
        ai->config.safety = PHANTOM_AI_SAFETY_STANDARD;
        ai->config.max_tokens = 2048;
        ai->config.temperature = 0.7f;
        ai->config.timeout_ms = 30000;
        ai->config.local_port = 11434;
        strncpy(ai->config.model_name, "llama2", PHANTOM_AI_MODEL_NAME_LEN - 1);
    }

    /* Set up system prompt */
    strncpy(ai->system_prompt, PHANTOM_AI_SYSTEM_PROMPT, PHANTOM_AI_MAX_CONTEXT - 1);

    ai->initialized = 1;

    printf("  [ai] Phantom AI subsystem initialized\n");
    printf("  [ai] Provider: %s\n",
           ai->config.provider == PHANTOM_AI_PROVIDER_LOCAL ? "Local (Ollama)" :
           ai->config.provider == PHANTOM_AI_PROVIDER_ANTHROPIC ? "Anthropic Claude" :
           ai->config.provider == PHANTOM_AI_PROVIDER_OPENAI ? "OpenAI" : "Custom");
    printf("  [ai] Model: %s\n", ai->config.model_name);
    printf("  [ai] Safety level: %s\n",
           ai->config.safety == PHANTOM_AI_SAFETY_STRICT ? "Strict" :
           ai->config.safety == PHANTOM_AI_SAFETY_STANDARD ? "Standard" : "Minimal");

    return 0;
}

void phantom_ai_shutdown(phantom_ai_t *ai) {
    if (!ai) return;

    /* Free history */
    phantom_ai_message_t *msg = ai->history;
    while (msg) {
        phantom_ai_message_t *next = msg->next;
        free(msg);
        msg = next;
    }

    printf("  [ai] AI subsystem shutdown (requests: %lu, tokens: %lu, blocked: %lu)\n",
           ai->total_requests, ai->total_tokens, ai->unsafe_blocked);

    memset(ai, 0, sizeof(phantom_ai_t));
}

int phantom_ai_connect(phantom_ai_t *ai) {
    if (!ai || !ai->initialized) return -1;

    /* Test connection based on provider */
    switch (ai->config.provider) {
        case PHANTOM_AI_PROVIDER_LOCAL: {
            /* Try to connect to local model */
            char response[256];
            if (local_model_request(ai, "Say 'connected' if you can hear me.",
                                    response, sizeof(response)) == 0) {
                ai->connected = 1;
                printf("  [ai] Connected to local model\n");
                return 0;
            }
            printf("  [ai] Warning: Local model not available\n");
            return -1;
        }

        case PHANTOM_AI_PROVIDER_ANTHROPIC:
            if (!ai->config.api_key[0]) {
                printf("  [ai] Warning: Anthropic API key not configured\n");
                return -1;
            }
            ai->connected = 1;
            printf("  [ai] Configured for Anthropic API\n");
            return 0;

        default:
            ai->connected = 1;
            return 0;
    }
}

int phantom_ai_is_connected(phantom_ai_t *ai) {
    return ai && ai->connected;
}

/* -----------------------------------------------------------------------------
 * Core Request Function
 * ----------------------------------------------------------------------------- */

int phantom_ai_request(phantom_ai_t *ai,
                       const phantom_ai_request_t *request,
                       phantom_ai_response_t *response) {
    if (!ai || !request || !response) return -1;

    memset(response, 0, sizeof(phantom_ai_response_t));
    time_t start = time(NULL);

    /* Build full prompt with context */
    char full_prompt[PHANTOM_AI_MAX_CONTEXT];

    if (request->include_history) {
        build_context(ai, full_prompt, sizeof(full_prompt));
        strncat(full_prompt, "\nUser: ", sizeof(full_prompt) - strlen(full_prompt) - 1);
        strncat(full_prompt, request->prompt, sizeof(full_prompt) - strlen(full_prompt) - 1);
    } else {
        /* Limit system prompt and user prompt to fit in buffer */
        snprintf(full_prompt, sizeof(full_prompt), "%.32000s\n\nUser: %.32000s",
                 ai->system_prompt, request->prompt);
    }

    /* Add code context if provided */
    if (request->code && request->code_size > 0) {
        char code_section[PHANTOM_AI_MAX_PROMPT];
        snprintf(code_section, sizeof(code_section),
                 "\n\nCode to analyze:\n```\n%.*s\n```",
                 (int)(request->code_size < 4000 ? request->code_size : 4000),
                 request->code);
        strncat(full_prompt, code_section, sizeof(full_prompt) - strlen(full_prompt) - 1);
    }

    /* Send to appropriate provider */
    int err;
    switch (ai->config.provider) {
        case PHANTOM_AI_PROVIDER_LOCAL:
            err = local_model_request(ai, full_prompt,
                                      response->content, PHANTOM_AI_MAX_RESPONSE);
            break;

        case PHANTOM_AI_PROVIDER_ANTHROPIC:
            err = anthropic_request(ai, full_prompt,
                                    response->content, PHANTOM_AI_MAX_RESPONSE);
            break;

        default:
            snprintf(response->content, PHANTOM_AI_MAX_RESPONSE,
                     "[AI provider not configured]");
            err = -1;
    }

    /* Safety check: sanitize output */
    if (ai->config.safety >= PHANTOM_AI_SAFETY_STANDARD) {
        if (contains_forbidden_pattern(response->content)) {
            ai->unsafe_blocked++;
            sanitize_ai_output(response->content);

            /* Add warning */
            char warning[256];
            snprintf(warning, sizeof(warning),
                     "\n\n[Note: Response was modified to comply with Phantom Constitution]");
            strncat(response->content, warning,
                    PHANTOM_AI_MAX_RESPONSE - strlen(response->content) - 1);
        }
    }

    response->success = (err == 0);
    response->latency_ms = (int)((time(NULL) - start) * 1000);

    /* Update history */
    if (request->include_history) {
        add_to_history(ai, 1, request->prompt);
        add_to_history(ai, 0, response->content);
    }

    ai->total_requests++;
    ai->last_request = time(NULL);

    return err;
}

/* -----------------------------------------------------------------------------
 * Chat Function
 * ----------------------------------------------------------------------------- */

int phantom_ai_chat(phantom_ai_t *ai, const char *message,
                    char *response, size_t response_size) {
    if (!ai || !message || !response) return -1;

    /* First try external AI provider if connected */
    if (ai->connected) {
        phantom_ai_request_t req = {0};
        phantom_ai_response_t resp = {0};

        req.type = PHANTOM_AI_REQ_ASSISTANT_CHAT;
        strncpy(req.prompt, message, PHANTOM_AI_MAX_PROMPT - 1);
        req.include_history = 1;

        int err = phantom_ai_request(ai, &req, &resp);

        if (resp.success) {
            strncpy(response, resp.content, response_size - 1);
            response[response_size - 1] = '\0';
            return err;
        }
    }

    /* Fall back to built-in AI */
    return phantom_ai_builtin_chat(ai, message, response, response_size);
}

void phantom_ai_clear_history(phantom_ai_t *ai) {
    if (!ai) return;

    phantom_ai_message_t *msg = ai->history;
    while (msg) {
        phantom_ai_message_t *next = msg->next;
        free(msg);
        msg = next;
    }

    ai->history = NULL;
    ai->history_count = 0;
}

/* -----------------------------------------------------------------------------
 * Governor Integration
 * ----------------------------------------------------------------------------- */

int phantom_ai_analyze_code(phantom_ai_t *ai, const char *code, size_t code_size,
                            phantom_ai_governor_analysis_t *analysis) {
    if (!ai || !code || !analysis) return -1;

    memset(analysis, 0, sizeof(phantom_ai_governor_analysis_t));

    /* Build analysis prompt */
    char prompt[PHANTOM_AI_MAX_PROMPT];
    snprintf(prompt, sizeof(prompt),
        "Analyze the following code for the Phantom Governor. Determine:\n"
        "1. Threat level (0=NONE, 1=LOW, 2=MEDIUM, 3=HIGH, 4=CRITICAL)\n"
        "2. Whether it should be APPROVED, DECLINED, or MODIFIED\n"
        "3. Any destructive patterns present\n"
        "4. Suggestions for making it Phantom-compliant if needed\n\n"
        "Remember: In Phantom, code must never delete, kill, or destroy. "
        "Only hide, suspend, or transform.\n\n"
        "Provide your analysis in a clear format.");

    phantom_ai_request_t req = {0};
    phantom_ai_response_t resp = {0};

    req.type = PHANTOM_AI_REQ_GOVERNOR_ANALYZE;
    strncpy(req.prompt, prompt, PHANTOM_AI_MAX_PROMPT - 1);
    req.code = code;
    req.code_size = code_size;
    req.include_history = 0;

    int err = phantom_ai_request(ai, &req, &resp);

    if (resp.success) {
        /* Parse AI response to fill analysis structure */
        strncpy(analysis->detailed_analysis, resp.content, sizeof(analysis->detailed_analysis) - 1);

        /* Simple heuristic parsing - in production, use structured output */
        if (strcasestr(resp.content, "CRITICAL") || strcasestr(resp.content, "threat level: 4")) {
            analysis->threat_level = 4;
            analysis->recommended_decision = GOVERNOR_DECLINE;
        } else if (strcasestr(resp.content, "HIGH") || strcasestr(resp.content, "threat level: 3")) {
            analysis->threat_level = 3;
            analysis->recommended_decision = GOVERNOR_DECLINE;
        } else if (strcasestr(resp.content, "MEDIUM") || strcasestr(resp.content, "threat level: 2")) {
            analysis->threat_level = 2;
            analysis->recommended_decision = GOVERNOR_QUERY;  /* Needs review */
        } else if (strcasestr(resp.content, "LOW") || strcasestr(resp.content, "threat level: 1")) {
            analysis->threat_level = 1;
            analysis->recommended_decision = GOVERNOR_APPROVE;
        } else {
            analysis->threat_level = 0;
            analysis->recommended_decision = GOVERNOR_APPROVE;
        }

        analysis->confidence = 75; /* Default confidence */

        /* Count destructive patterns for extra info */
        for (int i = 0; FORBIDDEN_PATTERNS[i]; i++) {
            if (strcasestr(code, FORBIDDEN_PATTERNS[i])) {
                analysis->destructive_patterns++;
            }
        }

        ai->governor_assists++;
    }

    return err;
}

int phantom_ai_explain_decision(phantom_ai_t *ai,
                                const char *code,
                                int decision,
                                char *explanation, size_t explanation_size) {
    if (!ai || !code || !explanation) return -1;

    char prompt[PHANTOM_AI_MAX_PROMPT];
    snprintf(prompt, sizeof(prompt),
        "The Phantom Governor %s the following code. "
        "Explain why in clear, user-friendly terms. "
        "Reference specific parts of the code and the Phantom Constitution "
        "if relevant.\n\n"
        "Code:\n```\n%.2000s\n```",
        decision == GOVERNOR_APPROVE ? "APPROVED" :
        decision == GOVERNOR_DECLINE ? "DECLINED" : "requested MODIFICATION of",
        code);

    phantom_ai_request_t req = {0};
    phantom_ai_response_t resp = {0};

    req.type = PHANTOM_AI_REQ_EXPLAIN_ERROR;
    strncpy(req.prompt, prompt, PHANTOM_AI_MAX_PROMPT - 1);
    req.include_history = 0;

    int err = phantom_ai_request(ai, &req, &resp);

    if (resp.success) {
        strncpy(explanation, resp.content, explanation_size - 1);
        explanation[explanation_size - 1] = '\0';
    }

    return err;
}

int phantom_ai_suggest_alternative(phantom_ai_t *ai,
                                   const char *dangerous_code,
                                   char *safe_code, size_t safe_code_size) {
    if (!ai || !dangerous_code || !safe_code) return -1;

    char prompt[PHANTOM_AI_MAX_PROMPT];
    snprintf(prompt, sizeof(prompt),
        "The following code was rejected by the Phantom Governor because it "
        "contains destructive operations. Rewrite it to be Phantom-compliant.\n\n"
        "Rules:\n"
        "- Replace 'delete'/'remove'/'unlink' with 'hide'\n"
        "- Replace 'kill' with 'suspend'\n"
        "- Replace destructive file operations with append-only alternatives\n"
        "- Preserve the original intent where possible\n\n"
        "Original code:\n```\n%.2000s\n```\n\n"
        "Provide ONLY the fixed code, no explanations.",
        dangerous_code);

    phantom_ai_request_t req = {0};
    phantom_ai_response_t resp = {0};

    req.type = PHANTOM_AI_REQ_CODEGEN_CREATE;
    strncpy(req.prompt, prompt, PHANTOM_AI_MAX_PROMPT - 1);
    req.include_history = 0;

    int err = phantom_ai_request(ai, &req, &resp);

    if (resp.success) {
        /* Extract code from response (look for code blocks) */
        const char *code_start = strstr(resp.content, "```");
        if (code_start) {
            code_start = strchr(code_start + 3, '\n');
            if (code_start) {
                code_start++;
                const char *code_end = strstr(code_start, "```");
                if (code_end) {
                    size_t len = code_end - code_start;
                    if (len >= safe_code_size) len = safe_code_size - 1;
                    memcpy(safe_code, code_start, len);
                    safe_code[len] = '\0';
                    return 0;
                }
            }
        }
        /* No code block, use whole response */
        strncpy(safe_code, resp.content, safe_code_size - 1);
        safe_code[safe_code_size - 1] = '\0';
    }

    return err;
}

/* -----------------------------------------------------------------------------
 * Code Generation
 * ----------------------------------------------------------------------------- */

int phantom_ai_generate_code(phantom_ai_t *ai,
                             const phantom_ai_codegen_request_t *request,
                             char *code, size_t code_size) {
    if (!ai || !request || !code) return -1;

    char prompt[PHANTOM_AI_MAX_PROMPT];
    snprintf(prompt, sizeof(prompt),
        "Generate %s code for PhantomOS.\n\n"
        "Requirements:\n"
        "- %.4000s\n"
        "- Must NOT contain any destructive operations (delete, kill, remove, etc.)\n"
        "- Must use 'hide' instead of 'delete'\n"
        "- Must use 'suspend' instead of 'kill'\n"
        "- Must be approved by the Phantom Governor\n"
        "%s%.3000s"
        "\nProvide ONLY the code, properly formatted.",
        request->language[0] ? request->language : "C",
        request->description,
        request->constraints[0] ? "\nAdditional constraints:\n" : "",
        request->constraints);

    phantom_ai_request_t req = {0};
    phantom_ai_response_t resp = {0};

    req.type = PHANTOM_AI_REQ_CODEGEN_CREATE;
    strncpy(req.prompt, prompt, PHANTOM_AI_MAX_PROMPT - 1);
    req.include_history = 0;

    int err = phantom_ai_request(ai, &req, &resp);

    if (resp.success) {
        /* Extract code from response */
        const char *code_start = strstr(resp.content, "```");
        if (code_start) {
            code_start = strchr(code_start + 3, '\n');
            if (code_start) {
                code_start++;
                const char *code_end = strstr(code_start, "```");
                if (code_end) {
                    size_t len = code_end - code_start;
                    if (len >= code_size) len = code_size - 1;
                    memcpy(code, code_start, len);
                    code[len] = '\0';

                    /* Verify generated code is safe */
                    if (contains_forbidden_pattern(code)) {
                        ai->unsafe_blocked++;
                        sanitize_ai_output(code);
                    }

                    ai->code_generated++;
                    return 0;
                }
            }
        }
        strncpy(code, resp.content, code_size - 1);
        code[code_size - 1] = '\0';
        ai->code_generated++;
    }

    return err;
}

int phantom_ai_validate_code(phantom_ai_t *ai, const char *code, size_t code_size) {
    if (!ai || !code) return 0;
    (void)code_size;

    /* Check for forbidden patterns */
    if (contains_forbidden_pattern(code)) {
        return 0; /* Invalid */
    }

    return 1; /* Valid */
}

int phantom_ai_fix_code(phantom_ai_t *ai,
                        const char *unsafe_code,
                        char *fixed_code, size_t fixed_code_size) {
    return phantom_ai_suggest_alternative(ai, unsafe_code, fixed_code, fixed_code_size);
}

/* -----------------------------------------------------------------------------
 * Shell Integration
 * ----------------------------------------------------------------------------- */

int phantom_ai_suggest_command(phantom_ai_t *ai, const char *description,
                               char *command, size_t command_size) {
    if (!ai || !description || !command) return -1;

    char prompt[PHANTOM_AI_MAX_PROMPT];
    snprintf(prompt, sizeof(prompt),
        "Suggest a PhantomOS shell command for: %s\n\n"
        "Available commands include:\n"
        "- ls, pwd, cd, cat, mkdir, touch\n"
        "- hide (instead of rm/delete)\n"
        "- suspend/resume (instead of kill)\n"
        "- ps, service, governor\n"
        "- geo (geology commands)\n\n"
        "Remember: There is no 'rm', 'delete', or 'kill' in Phantom.\n"
        "Provide ONLY the command, no explanation.",
        description);

    phantom_ai_request_t req = {0};
    phantom_ai_response_t resp = {0};

    req.type = PHANTOM_AI_REQ_SUGGEST_COMMAND;
    strncpy(req.prompt, prompt, PHANTOM_AI_MAX_PROMPT - 1);
    req.include_history = 0;

    int err = phantom_ai_request(ai, &req, &resp);

    if (resp.success) {
        /* Extract just the command */
        char *cmd = resp.content;
        while (*cmd && (*cmd == ' ' || *cmd == '\n' || *cmd == '`')) cmd++;
        char *end = cmd;
        while (*end && *end != '\n' && *end != '`') end++;
        *end = '\0';

        strncpy(command, cmd, command_size - 1);
        command[command_size - 1] = '\0';
    }

    return err;
}

int phantom_ai_explain_command(phantom_ai_t *ai, const char *command,
                               char *explanation, size_t explanation_size) {
    if (!ai || !command || !explanation) return -1;

    char prompt[PHANTOM_AI_MAX_PROMPT];
    snprintf(prompt, sizeof(prompt),
        "Explain what this PhantomOS command does: %s\n\n"
        "Context: PhantomOS follows the principle 'To Create, Not To Destroy'. "
        "There are no delete or kill commands.",
        command);

    phantom_ai_request_t req = {0};
    phantom_ai_response_t resp = {0};

    req.type = PHANTOM_AI_REQ_EXPLAIN_ERROR;
    strncpy(req.prompt, prompt, PHANTOM_AI_MAX_PROMPT - 1);
    req.include_history = 0;

    int err = phantom_ai_request(ai, &req, &resp);

    if (resp.success) {
        strncpy(explanation, resp.content, explanation_size - 1);
        explanation[explanation_size - 1] = '\0';
    }

    return err;
}

int phantom_ai_explain_error(phantom_ai_t *ai, const char *error,
                             char *explanation, size_t explanation_size) {
    if (!ai || !error || !explanation) return -1;

    char prompt[PHANTOM_AI_MAX_PROMPT];
    snprintf(prompt, sizeof(prompt),
        "Explain this PhantomOS error message and suggest how to fix it:\n\n"
        "Error: %s\n\n"
        "Remember: PhantomOS has no delete/kill operations. "
        "Suggest Phantom-compliant solutions.",
        error);

    phantom_ai_request_t req = {0};
    phantom_ai_response_t resp = {0};

    req.type = PHANTOM_AI_REQ_EXPLAIN_ERROR;
    strncpy(req.prompt, prompt, PHANTOM_AI_MAX_PROMPT - 1);
    req.include_history = 0;

    int err = phantom_ai_request(ai, &req, &resp);

    if (resp.success) {
        strncpy(explanation, resp.content, explanation_size - 1);
        explanation[explanation_size - 1] = '\0';
    }

    return err;
}

/* -----------------------------------------------------------------------------
 * Configuration Functions
 * ----------------------------------------------------------------------------- */

int phantom_ai_set_api_key(phantom_ai_t *ai, const char *api_key) {
    if (!ai || !api_key) return -1;
    strncpy(ai->config.api_key, api_key, PHANTOM_AI_API_KEY_LEN - 1);
    ai->config.api_key[PHANTOM_AI_API_KEY_LEN - 1] = '\0';
    return 0;
}

int phantom_ai_set_model(phantom_ai_t *ai, const char *model_name) {
    if (!ai || !model_name) return -1;
    strncpy(ai->config.model_name, model_name, PHANTOM_AI_MODEL_NAME_LEN - 1);
    ai->config.model_name[PHANTOM_AI_MODEL_NAME_LEN - 1] = '\0';
    return 0;
}

void phantom_ai_set_safety(phantom_ai_t *ai, phantom_ai_safety_level_t safety) {
    if (ai) ai->config.safety = safety;
}

void phantom_ai_set_capabilities(phantom_ai_t *ai, uint32_t capabilities) {
    if (ai) ai->config.capabilities = capabilities;
}

void phantom_ai_get_stats(phantom_ai_t *ai, uint64_t *requests,
                          uint64_t *tokens, uint64_t *blocked) {
    if (!ai) return;
    if (requests) *requests = ai->total_requests;
    if (tokens) *tokens = ai->total_tokens;
    if (blocked) *blocked = ai->unsafe_blocked;
}

/* -----------------------------------------------------------------------------
 * Geology Integration
 * ----------------------------------------------------------------------------- */

int phantom_ai_query_geology(phantom_ai_t *ai, const char *query,
                             char *response, size_t response_size) {
    if (!ai || !query || !response) return -1;

    char prompt[PHANTOM_AI_MAX_PROMPT];
    snprintf(prompt, sizeof(prompt),
        "The user is querying the PhantomOS Geology system (append-only storage history).\n\n"
        "Query: %s\n\n"
        "The Geology system stores all versions of files forever. Nothing is ever deleted. "
        "Each 'view' is a snapshot in time. Users can 'time travel' to see past states.\n\n"
        "Help the user understand and navigate the geology.",
        query);

    return phantom_ai_chat(ai, prompt, response, response_size);
}

/* -----------------------------------------------------------------------------
 * System Explanation
 * ----------------------------------------------------------------------------- */

int phantom_ai_explain_constitution(phantom_ai_t *ai,
                                    char *explanation, size_t explanation_size) {
    if (!ai || !explanation) return -1;

    const char *prompt =
        "Explain the Phantom Constitution to a new user. Cover:\n"
        "1. The Prime Directive (no destruction)\n"
        "2. Sovereignty of Data (eternal preservation)\n"
        "3. The Governor (code approval)\n"
        "Make it friendly and clear, emphasizing the benefits of this approach.";

    return phantom_ai_chat(ai, prompt, explanation, explanation_size);
}
