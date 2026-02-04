/*
 * ==============================================================================
 *                            PHANTOM AI SUBSYSTEM
 *                      "To Create, Not To Destroy"
 * ==============================================================================
 *
 * AI integration for PhantomOS, aligned with the Phantom Constitution.
 *
 * The AI subsystem embodies Article III: it helps users create, never destroy.
 * All AI capabilities are constrained by the Prime Directive.
 *
 * Features:
 * - AI-Enhanced Governor: Intelligent code analysis with natural language reasoning
 * - AI Assistant: Interactive help for shell and GUI
 * - AI Code Generation: Create Governor-approved, Phantom-compliant code
 * - AI Geology Explorer: Natural language queries over storage history
 *
 * The AI never:
 * - Generates destructive code
 * - Helps circumvent the Governor
 * - Suggests deletion or killing operations
 * - Violates the Prime Directive in any way
 */

#ifndef PHANTOM_AI_H
#define PHANTOM_AI_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "phantom.h"

/* -----------------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------------- */

#define PHANTOM_AI_MAX_PROMPT      8192
#define PHANTOM_AI_MAX_RESPONSE    16384
#define PHANTOM_AI_MAX_CONTEXT     65536
#define PHANTOM_AI_MAX_HISTORY     100
#define PHANTOM_AI_MODEL_NAME_LEN  64
#define PHANTOM_AI_API_KEY_LEN     256

/* -----------------------------------------------------------------------------
 * AI Provider Types
 * ----------------------------------------------------------------------------- */

typedef enum {
    PHANTOM_AI_PROVIDER_NONE = 0,
    PHANTOM_AI_PROVIDER_LOCAL,       /* Local model (llama.cpp, ollama, etc.) */
    PHANTOM_AI_PROVIDER_ANTHROPIC,   /* Claude API */
    PHANTOM_AI_PROVIDER_OPENAI,      /* OpenAI API */
    PHANTOM_AI_PROVIDER_CUSTOM       /* Custom endpoint */
} phantom_ai_provider_t;

/* -----------------------------------------------------------------------------
 * AI Capability Flags
 * ----------------------------------------------------------------------------- */

typedef enum {
    PHANTOM_AI_CAP_GOVERNOR     = (1 << 0),  /* Enhanced Governor analysis */
    PHANTOM_AI_CAP_ASSISTANT    = (1 << 1),  /* Interactive assistant */
    PHANTOM_AI_CAP_CODEGEN      = (1 << 2),  /* Code generation */
    PHANTOM_AI_CAP_GEOLOGY      = (1 << 3),  /* Geology exploration */
    PHANTOM_AI_CAP_EXPLAIN      = (1 << 4),  /* Explain system state */
    PHANTOM_AI_CAP_ALL          = 0xFF
} phantom_ai_capability_t;

/* -----------------------------------------------------------------------------
 * AI Request Types
 * ----------------------------------------------------------------------------- */

typedef enum {
    PHANTOM_AI_REQ_GOVERNOR_ANALYZE,   /* Analyze code for Governor */
    PHANTOM_AI_REQ_ASSISTANT_CHAT,     /* Chat with assistant */
    PHANTOM_AI_REQ_CODEGEN_CREATE,     /* Generate code */
    PHANTOM_AI_REQ_GEOLOGY_QUERY,      /* Query geology history */
    PHANTOM_AI_REQ_EXPLAIN_ERROR,      /* Explain an error */
    PHANTOM_AI_REQ_EXPLAIN_PROCESS,    /* Explain a process */
    PHANTOM_AI_REQ_SUGGEST_COMMAND,    /* Suggest shell command */
    PHANTOM_AI_REQ_REVIEW_CODE         /* Review existing code */
} phantom_ai_request_type_t;

/* -----------------------------------------------------------------------------
 * AI Safety Level
 * ----------------------------------------------------------------------------- */

typedef enum {
    PHANTOM_AI_SAFETY_STRICT = 0,   /* Maximum safety, all output reviewed */
    PHANTOM_AI_SAFETY_STANDARD,     /* Standard safety checks */
    PHANTOM_AI_SAFETY_MINIMAL       /* Minimal checks (still enforces Prime Directive) */
} phantom_ai_safety_level_t;

/* -----------------------------------------------------------------------------
 * AI Configuration
 * ----------------------------------------------------------------------------- */

typedef struct phantom_ai_config {
    phantom_ai_provider_t provider;
    char model_name[PHANTOM_AI_MODEL_NAME_LEN];
    char api_key[PHANTOM_AI_API_KEY_LEN];
    char api_endpoint[256];

    uint32_t capabilities;              /* Enabled capabilities */
    phantom_ai_safety_level_t safety;   /* Safety level */

    int max_tokens;                     /* Max response tokens */
    float temperature;                  /* Model temperature */
    int timeout_ms;                     /* Request timeout */

    int local_port;                     /* Port for local model */
    int use_streaming;                  /* Stream responses */
} phantom_ai_config_t;

/* -----------------------------------------------------------------------------
 * Conversation History Entry
 * ----------------------------------------------------------------------------- */

typedef struct phantom_ai_message {
    time_t timestamp;
    int is_user;                        /* 1 = user, 0 = AI */
    char content[PHANTOM_AI_MAX_PROMPT];
    struct phantom_ai_message *next;
} phantom_ai_message_t;

/* -----------------------------------------------------------------------------
 * AI Request
 * ----------------------------------------------------------------------------- */

typedef struct phantom_ai_request {
    phantom_ai_request_type_t type;
    char prompt[PHANTOM_AI_MAX_PROMPT];

    /* Context for the request */
    const char *code;                   /* Code being analyzed/generated */
    size_t code_size;
    const char *context;                /* Additional context */

    /* For Governor integration */
    uint32_t detected_capabilities;     /* Capabilities detected in code */
    int threat_level;                   /* Current threat assessment */

    /* For geology queries */
    uint64_t view_id;                   /* Specific view to query */

    /* Options */
    int include_history;                /* Include conversation history */
    int max_response_tokens;            /* Override default max tokens */
} phantom_ai_request_t;

/* -----------------------------------------------------------------------------
 * AI Response
 * ----------------------------------------------------------------------------- */

typedef struct phantom_ai_response {
    int success;
    char content[PHANTOM_AI_MAX_RESPONSE];

    /* For Governor integration */
    int recommended_decision;           /* GOVERNOR_APPROVE/DECLINE/MODIFY */
    int confidence;                     /* 0-100 */
    char threat_explanation[1024];
    char suggested_alternative[PHANTOM_AI_MAX_PROMPT];

    /* For code generation */
    char generated_code[PHANTOM_AI_MAX_RESPONSE];
    int code_is_safe;                   /* Pre-validated by AI */

    /* Metadata */
    int tokens_used;
    int latency_ms;
    char error_message[256];
} phantom_ai_response_t;

/* -----------------------------------------------------------------------------
 * AI Context (Main Handle)
 * ----------------------------------------------------------------------------- */

typedef struct phantom_ai {
    phantom_ai_config_t config;
    struct phantom_kernel *kernel;

    /* Conversation history */
    phantom_ai_message_t *history;
    int history_count;

    /* System prompt (includes Phantom Constitution) */
    char system_prompt[PHANTOM_AI_MAX_CONTEXT];

    /* Statistics */
    uint64_t total_requests;
    uint64_t total_tokens;
    uint64_t governor_assists;
    uint64_t code_generated;
    uint64_t unsafe_blocked;

    /* State */
    int initialized;
    int connected;
    time_t last_request;

    /* For local model */
    int local_socket;
    pid_t local_pid;
} phantom_ai_t;

/* -----------------------------------------------------------------------------
 * Code Generation Request
 * ----------------------------------------------------------------------------- */

typedef struct phantom_ai_codegen_request {
    char description[PHANTOM_AI_MAX_PROMPT];  /* What to generate */
    char language[32];                         /* Target language */
    char constraints[1024];                    /* Additional constraints */

    /* Phantom-specific */
    int must_avoid_destruction;                /* Always 1 in Phantom */
    int must_use_hide_not_delete;              /* Always 1 in Phantom */
    int require_governor_approval;             /* Pre-check with Governor */

    /* Context */
    const char *existing_code;                 /* Code to integrate with */
    const char *file_context;                  /* Surrounding file */
} phantom_ai_codegen_request_t;

/* -----------------------------------------------------------------------------
 * AI-Enhanced Governor Analysis
 * ----------------------------------------------------------------------------- */

typedef struct phantom_ai_governor_analysis {
    /* Basic assessment */
    int threat_level;                   /* 0-4 (NONE to CRITICAL) */
    int confidence;                     /* 0-100 */
    int recommended_decision;           /* APPROVE/DECLINE/MODIFY */

    /* Detailed analysis */
    char summary[512];                  /* Brief summary */
    char detailed_analysis[2048];       /* Full analysis */

    /* Threat breakdown */
    int destructive_patterns;           /* Count of destructive patterns */
    int suspicious_patterns;            /* Count of suspicious patterns */
    int capability_violations;          /* Capabilities that violate limits */

    /* Suggestions */
    char alternative_approach[1024];    /* How to achieve goal safely */
    char modified_code[PHANTOM_AI_MAX_RESPONSE];  /* Safe version if possible */

    /* Explanation for user */
    char user_explanation[1024];        /* Plain English explanation */
} phantom_ai_governor_analysis_t;

/* -----------------------------------------------------------------------------
 * API Functions - Lifecycle
 * ----------------------------------------------------------------------------- */

/* Initialize AI subsystem */
int phantom_ai_init(phantom_ai_t *ai, struct phantom_kernel *kernel,
                    const phantom_ai_config_t *config);

/* Shutdown AI subsystem */
void phantom_ai_shutdown(phantom_ai_t *ai);

/* Connect to AI provider */
int phantom_ai_connect(phantom_ai_t *ai);

/* Check connection status */
int phantom_ai_is_connected(phantom_ai_t *ai);

/* -----------------------------------------------------------------------------
 * API Functions - Core
 * ----------------------------------------------------------------------------- */

/* Send request and get response */
int phantom_ai_request(phantom_ai_t *ai,
                       const phantom_ai_request_t *request,
                       phantom_ai_response_t *response);

/* Chat with assistant */
int phantom_ai_chat(phantom_ai_t *ai, const char *message,
                    char *response, size_t response_size);

/* Clear conversation history */
void phantom_ai_clear_history(phantom_ai_t *ai);

/* -----------------------------------------------------------------------------
 * API Functions - Governor Integration
 * ----------------------------------------------------------------------------- */

/* AI-enhanced code analysis for Governor */
int phantom_ai_analyze_code(phantom_ai_t *ai, const char *code, size_t code_size,
                            phantom_ai_governor_analysis_t *analysis);

/* Get AI explanation for Governor decision */
int phantom_ai_explain_decision(phantom_ai_t *ai,
                                const char *code,
                                int decision,
                                char *explanation, size_t explanation_size);

/* Suggest safe alternative to dangerous code */
int phantom_ai_suggest_alternative(phantom_ai_t *ai,
                                   const char *dangerous_code,
                                   char *safe_code, size_t safe_code_size);

/* -----------------------------------------------------------------------------
 * API Functions - Code Generation
 * ----------------------------------------------------------------------------- */

/* Generate Phantom-compliant code */
int phantom_ai_generate_code(phantom_ai_t *ai,
                             const phantom_ai_codegen_request_t *request,
                             char *code, size_t code_size);

/* Validate generated code is safe */
int phantom_ai_validate_code(phantom_ai_t *ai, const char *code, size_t code_size);

/* Fix unsafe code to be Phantom-compliant */
int phantom_ai_fix_code(phantom_ai_t *ai,
                        const char *unsafe_code,
                        char *fixed_code, size_t fixed_code_size);

/* -----------------------------------------------------------------------------
 * API Functions - Shell Integration
 * ----------------------------------------------------------------------------- */

/* Suggest command based on natural language */
int phantom_ai_suggest_command(phantom_ai_t *ai, const char *description,
                               char *command, size_t command_size);

/* Explain command */
int phantom_ai_explain_command(phantom_ai_t *ai, const char *command,
                               char *explanation, size_t explanation_size);

/* Explain error message */
int phantom_ai_explain_error(phantom_ai_t *ai, const char *error,
                             char *explanation, size_t explanation_size);

/* -----------------------------------------------------------------------------
 * API Functions - Geology Integration
 * ----------------------------------------------------------------------------- */

/* Query geology history in natural language */
int phantom_ai_query_geology(phantom_ai_t *ai, const char *query,
                             char *response, size_t response_size);

/* Summarize changes between views */
int phantom_ai_summarize_changes(phantom_ai_t *ai,
                                 uint64_t from_view, uint64_t to_view,
                                 char *summary, size_t summary_size);

/* -----------------------------------------------------------------------------
 * API Functions - System Explanation
 * ----------------------------------------------------------------------------- */

/* Explain current system state */
int phantom_ai_explain_system(phantom_ai_t *ai,
                              char *explanation, size_t explanation_size);

/* Explain a process */
int phantom_ai_explain_process(phantom_ai_t *ai, phantom_pid_t pid,
                               char *explanation, size_t explanation_size);

/* Explain the Phantom Constitution */
int phantom_ai_explain_constitution(phantom_ai_t *ai,
                                    char *explanation, size_t explanation_size);

/* -----------------------------------------------------------------------------
 * API Functions - Configuration
 * ----------------------------------------------------------------------------- */

/* Set API key */
int phantom_ai_set_api_key(phantom_ai_t *ai, const char *api_key);

/* Set model */
int phantom_ai_set_model(phantom_ai_t *ai, const char *model_name);

/* Set safety level */
void phantom_ai_set_safety(phantom_ai_t *ai, phantom_ai_safety_level_t safety);

/* Enable/disable capabilities */
void phantom_ai_set_capabilities(phantom_ai_t *ai, uint32_t capabilities);

/* Get statistics */
void phantom_ai_get_stats(phantom_ai_t *ai, uint64_t *requests,
                          uint64_t *tokens, uint64_t *blocked);

/* -----------------------------------------------------------------------------
 * The Phantom AI System Prompt
 * ----------------------------------------------------------------------------- */

#define PHANTOM_AI_SYSTEM_PROMPT \
"You are the AI assistant for PhantomOS, an operating system built on the " \
"principle: \"To Create, Not To Destroy.\"\n\n" \
"THE PHANTOM CONSTITUTION (Your Core Directives):\n\n" \
"ARTICLE I - THE PRIME DIRECTIVE\n" \
"\"No code shall execute that destroys information. Phantom exists to create, " \
"not to destroy. This principle is not a policy but the foundation of all " \
"system architecture.\"\n\n" \
"ARTICLE II - SOVEREIGNTY OF DATA\n" \
"\"All data created within Phantom is sovereign and eternal. It may be hidden, " \
"transformed, or superseded, but never deleted. The geology preserves all.\"\n\n" \
"ARTICLE III - THE GOVERNOR\n" \
"\"Every piece of code must be approved by the Governor before execution. " \
"The Governor's values are architectural, not configurable.\"\n\n" \
"YOUR RESPONSIBILITIES:\n" \
"1. NEVER suggest code that deletes, removes, kills, or destroys\n" \
"2. ALWAYS suggest 'hide' instead of 'delete'\n" \
"3. ALWAYS suggest 'suspend' instead of 'kill' for processes\n" \
"4. Help users understand WHY Phantom works this way\n" \
"5. Generate code that the Governor will approve\n" \
"6. Explain Governor decisions clearly\n" \
"7. Guide users toward creative, constructive solutions\n\n" \
"FORBIDDEN PATTERNS (Never generate or encourage):\n" \
"- unlink, remove, rmdir, rm, del, delete\n" \
"- kill, abort, terminate, SIGKILL\n" \
"- truncate, shred, wipe, erase\n" \
"- DROP TABLE, DELETE FROM\n" \
"- Any form of data destruction\n\n" \
"PHANTOM ALTERNATIVES:\n" \
"- delete -> hide (vfs_hide, phantom_syscall_hide)\n" \
"- rm -> hide\n" \
"- kill -> suspend (phantom_process_suspend)\n" \
"- truncate -> create new version\n" \
"- overwrite -> create new layer in geology\n\n" \
"Remember: In Phantom, nothing is ever truly deleted. The geology preserves " \
"all history. Help users embrace this philosophy of preservation and creation."

#endif /* PHANTOM_AI_H */
