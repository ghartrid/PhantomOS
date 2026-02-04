/*
 * ==============================================================================
 *                       PHANTOM BUILT-IN AI
 *                    "To Create, Not To Destroy"
 * ==============================================================================
 *
 * A simple rule-based AI assistant that works without external dependencies.
 * This is the fallback when no external AI model is available.
 */

#ifndef PHANTOM_AI_BUILTIN_H
#define PHANTOM_AI_BUILTIN_H

#include <stddef.h>
#include "phantom_ai.h"

/* Get a response from the built-in AI */
int phantom_ai_builtin_respond(const char *query, char *response, size_t response_size);

/* Check if query is asking to do something destructive */
int phantom_ai_builtin_is_destructive_request(const char *query);

/* Get safe alternative suggestion for destructive request */
int phantom_ai_builtin_suggest_safe(const char *query, char *response, size_t response_size);

/* Process a chat message with the built-in AI */
int phantom_ai_builtin_chat(phantom_ai_t *ai, const char *message,
                            char *response, size_t response_size);

/* Get a greeting message */
void phantom_ai_builtin_greeting(char *response, size_t response_size);

/* Get command suggestion for natural language task */
int phantom_ai_builtin_suggest_command(const char *task, char *command, size_t command_size);

/* Analyze code for Phantom compliance */
int phantom_ai_builtin_analyze_code(const char *code, char *analysis, size_t analysis_size);

#endif /* PHANTOM_AI_BUILTIN_H */
