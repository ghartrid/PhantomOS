/*
 * ==============================================================================
 *                       PHANTOM BUILT-IN AI
 *                    "To Create, Not To Destroy"
 * ==============================================================================
 *
 * An intelligent rule-based AI assistant that works without external dependencies.
 * Provides helpful responses about PhantomOS, the Constitution, commands,
 * and general system guidance.
 *
 * Features:
 * - Natural language understanding with intent detection
 * - Context-aware responses
 * - Command suggestion and explanation
 * - File analysis guidance
 * - Process explanation
 * - Geology navigation help
 * - Learning from common patterns
 *
 * This is the fallback when no external AI model (Ollama, Claude, etc.) is
 * available. It understands PhantomOS concepts deeply and can help users.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "phantom_ai.h"
#include "phantom_ai_builtin.h"
#include "phantom.h"

/* -----------------------------------------------------------------------------
 * Intent Detection System
 * ----------------------------------------------------------------------------- */

typedef enum {
    INTENT_UNKNOWN = 0,
    INTENT_GREETING,
    INTENT_HELP,
    INTENT_EXPLAIN_CONCEPT,
    INTENT_HOW_TO,
    INTENT_WHY,
    INTENT_LIST_COMMANDS,
    INTENT_FILE_OPERATION,
    INTENT_PROCESS_OPERATION,
    INTENT_DESTRUCTIVE_REQUEST,
    INTENT_ERROR_HELP,
    INTENT_GEOLOGY_QUERY,
    INTENT_GOVERNOR_QUERY,
    INTENT_CODE_QUESTION,
    INTENT_COMPARISON,
    INTENT_THANKS,
    INTENT_SEARCH_FILE,
    INTENT_COPY_FILE,
    INTENT_RENAME_FILE,
    INTENT_RESTORE_FILE,
    INTENT_VIEW_HISTORY
} ai_intent_t;

/* Intent patterns */
typedef struct {
    ai_intent_t intent;
    const char *patterns[12];
    int min_matches;
} intent_pattern_t;

static const intent_pattern_t intent_patterns[] = {
    {INTENT_GREETING, {"hello", "hi", "hey", "greetings", "good morning", "good evening", NULL}, 1},
    {INTENT_THANKS, {"thank", "thanks", "appreciate", "grateful", NULL}, 1},
    {INTENT_HELP, {"help", "assist", "guide", "stuck", "confused", NULL}, 1},
    {INTENT_HOW_TO, {"how do i", "how to", "how can i", "way to", "steps to", NULL}, 1},
    {INTENT_WHY, {"why", "reason", "purpose", "explain why", NULL}, 1},
    {INTENT_LIST_COMMANDS, {"list", "commands", "available", "show me", "what can", NULL}, 1},
    {INTENT_DESTRUCTIVE_REQUEST, {"delete", "remove", "rm ", "kill", "terminate", "destroy", "erase", "wipe", "unlink", NULL}, 1},
    {INTENT_FILE_OPERATION, {"file", "directory", "folder", "create", "open", "read", "write", "save", NULL}, 1},
    {INTENT_PROCESS_OPERATION, {"process", "running", "pid", "suspend", "resume", "ps", NULL}, 1},
    {INTENT_ERROR_HELP, {"error", "failed", "denied", "problem", "issue", "wrong", "not working", NULL}, 1},
    {INTENT_GEOLOGY_QUERY, {"geology", "geo", "history", "version", "snapshot", "time travel", "view", "restore", NULL}, 1},
    {INTENT_GOVERNOR_QUERY, {"governor", "approval", "approve", "code check", "analyze", NULL}, 1},
    {INTENT_CODE_QUESTION, {"code", "program", "script", "function", "compile", NULL}, 1},
    {INTENT_COMPARISON, {"difference", "compare", "versus", "vs", "better", "instead", NULL}, 1},
    {INTENT_EXPLAIN_CONCEPT, {"what is", "what's", "explain", "define", "meaning", "tell me about", NULL}, 1},
    {INTENT_SEARCH_FILE, {"search", "find", "locate", "where", "look for", NULL}, 1},
    {INTENT_COPY_FILE, {"copy", "duplicate", "clone", "cp", NULL}, 1},
    {INTENT_RENAME_FILE, {"rename", "move", "mv", "change name", NULL}, 1},
    {INTENT_RESTORE_FILE, {"restore", "recover", "get back", "undo", "revert", NULL}, 1},
    {INTENT_VIEW_HISTORY, {"history", "versions", "previous", "older", "changes", NULL}, 1},
    {INTENT_UNKNOWN, {NULL}, 0}
};

/* -----------------------------------------------------------------------------
 * Knowledge Base - Expanded PhantomOS Concepts
 * ----------------------------------------------------------------------------- */

typedef struct {
    const char *keywords[10];
    const char *response;
    int priority;
    ai_intent_t primary_intent;
} ai_rule_t;

/* Constitution and Philosophy */
static const ai_rule_t constitution_rules[] = {
    {{"constitution", "prime", "directive", "articles", NULL},
     "The Phantom Constitution has three fundamental articles:\n\n"
     "ARTICLE I - THE PRIME DIRECTIVE\n"
     "\"No code shall execute that destroys information.\"\n"
     "This is the foundational principle - PhantomOS never deletes data.\n\n"
     "ARTICLE II - SOVEREIGNTY OF DATA\n"
     "\"All data is sovereign and eternal.\"\n"
     "Data may be hidden, transformed, or superseded, but never deleted.\n"
     "The geology preserves everything forever.\n\n"
     "ARTICLE III - THE GOVERNOR\n"
     "\"Every piece of code must be approved before execution.\"\n"
     "The Governor ensures no code violates the Prime Directive.\n\n"
     "This isn't a limitation - it's liberation from data loss!",
     100, INTENT_EXPLAIN_CONCEPT},

    {{"why", "no", "delete", "can't", "cannot", NULL},
     "PhantomOS doesn't have delete because destruction is irreversible:\n\n"
     "Traditional deletion causes:\n"
     "- Accidental data loss (we've all been there!)\n"
     "- Security issues from malicious deletion\n"
     "- No way to recover from mistakes\n"
     "- Loss of history and context\n\n"
     "PhantomOS solves this with ALTERNATIVES:\n"
     "- hide <file> - File becomes invisible but preserved\n"
     "- suspend <pid> - Process sleeps but can wake\n"
     "- geo view <id> - Time travel to recover anything\n\n"
     "Nothing is ever truly lost. This is a feature, not a bug!",
     95, INTENT_WHY},

    {{"geology", "geo", "storage", "time", "travel", NULL},
     "Geology is PhantomOS's revolutionary append-only storage system.\n\n"
     "Like geological layers in rock, each change creates a new layer:\n"
     "- Write a file? New layer added.\n"
     "- Modify content? New version in new layer.\n"
     "- Hide a file? New view created.\n\n"
     "COMMANDS:\n"
     "  geo list           - Show all snapshots (views)\n"
     "  geo view <id>      - Time travel to a specific view\n"
     "  geo current        - Show current view ID\n"
     "  geo save <label>   - Create named checkpoint\n"
     "  versions <file>    - See file's version history\n"
     "  restore <file> <view_id> - Recover from history\n\n"
     "You can ALWAYS go back in time. Nothing is ever lost!",
     90, INTENT_GEOLOGY_QUERY},

    {{"governor", "approval", "code", "analyze", "check", NULL},
     "The Governor is PhantomOS's intelligent code guardian.\n\n"
     "Before ANY code executes, the Governor:\n"
     "1. Scans for destructive patterns (delete, kill, etc.)\n"
     "2. Analyzes intent and capabilities\n"
     "3. Assigns threat level (0=NONE to 4=CRITICAL)\n"
     "4. Makes decision: APPROVE, DECLINE, or QUERY\n\n"
     "COMMANDS:\n"
     "  governor status    - Check Governor state\n"
     "  governor mode      - See current mode\n"
     "  governor test <code> - Test if code would be approved\n"
     "  governor stats     - View approval statistics\n\n"
     "The Governor protects you from accidental destruction.\n"
     "AI-enhanced analysis available for deeper code review!",
     85, INTENT_GOVERNOR_QUERY},

    {{NULL}, NULL, 0, INTENT_UNKNOWN}
};

/* Command Reference - Expanded */
static const ai_rule_t command_rules[] = {
    {{"delete", "rm", "remove", "unlink", "erase", NULL},
     "PhantomOS doesn't have delete commands - use HIDE instead!\n\n"
     "  hide <filename>\n\n"
     "What happens when you hide:\n"
     "1. File becomes invisible in current view\n"
     "2. Content preserved in geology\n"
     "3. Recoverable via time travel: geo view <earlier_id>\n"
     "4. File history remains accessible: versions <filename>\n\n"
     "EXAMPLE:\n"
     "  hide old_notes.txt     # File hidden, not deleted\n"
     "  geo list               # Find earlier view\n"
     "  geo view 3             # Travel back\n"
     "  cat old_notes.txt      # File visible again!\n\n"
     "You can never lose data by accident in PhantomOS.",
     98, INTENT_DESTRUCTIVE_REQUEST},

    {{"kill", "terminate", "stop", "process", "sigkill", NULL},
     "PhantomOS doesn't kill processes - use SUSPEND instead!\n\n"
     "  suspend <pid>    - Put process to sleep\n"
     "  resume <pid>     - Wake process up\n\n"
     "Process states in PhantomOS:\n"
     "- RUNNING: Actively executing\n"
     "- BLOCKED: Waiting for resource\n"
     "- DORMANT: Suspended (can be resumed)\n"
     "- EMBRYO: Being created\n\n"
     "EXAMPLE:\n"
     "  ps                     # List all processes\n"
     "  suspend 42             # Process 42 goes dormant\n"
     "  resume 42              # Process 42 wakes up\n\n"
     "Suspended processes preserve all their state!",
     98, INTENT_DESTRUCTIVE_REQUEST},

    {{"list", "ls", "files", "directory", "dir", NULL},
     "File navigation in PhantomOS:\n\n"
     "BROWSING:\n"
     "  ls [path]         - List files (-l for details, -a for hidden)\n"
     "  pwd               - Print working directory\n"
     "  cd <path>         - Change directory\n"
     "  cat <file>        - View file contents\n"
     "  stat <file>       - Show file details\n\n"
     "CREATING:\n"
     "  touch <name>      - Create empty file\n"
     "  mkdir <name>      - Create directory\n"
     "  write <file> <text> - Append text to file\n"
     "  ln -s <target> <link> - Create symbolic link\n\n"
     "MANAGING:\n"
     "  hide <file>       - Hide file (instead of delete)\n"
     "  cp <src> <dst>    - Copy file\n"
     "  mv <src> <dst>    - Move/rename file\n"
     "  find <pattern>    - Search for files",
     75, INTENT_LIST_COMMANDS},

    {{"copy", "cp", "duplicate", "clone", NULL},
     "Copying files in PhantomOS:\n\n"
     "  cp <source> <destination>\n\n"
     "EXAMPLES:\n"
     "  cp notes.txt notes_backup.txt    # Copy file\n"
     "  cp config.txt /home/config.txt   # Copy to different location\n\n"
     "In PhantomOS, copying is truly safe:\n"
     "- Both source and destination preserved forever\n"
     "- Content deduplicated in geology (no wasted space)\n"
     "- Full history maintained for both files\n\n"
     "GUI: Select file > Click 'Copy' button > Enter new name",
     80, INTENT_COPY_FILE},

    {{"rename", "move", "mv", NULL},
     "Renaming/moving files in PhantomOS:\n\n"
     "  mv <old_name> <new_name>\n\n"
     "EXAMPLES:\n"
     "  mv report.txt final_report.txt   # Rename file\n"
     "  mv data.txt /archive/data.txt    # Move to new location\n\n"
     "What happens in PhantomOS:\n"
     "1. New file created at destination\n"
     "2. Original file automatically hidden\n"
     "3. Original preserved in geology history\n"
     "4. Both versions remain accessible!\n\n"
     "GUI: Select file > Click 'Rename' button > Enter new name",
     80, INTENT_RENAME_FILE},

    {{"search", "find", "locate", "where", NULL},
     "Searching for files in PhantomOS:\n\n"
     "  find [path] <pattern>\n\n"
     "PATTERNS:\n"
     "  *        - Match any characters\n"
     "  ?        - Match single character\n\n"
     "EXAMPLES:\n"
     "  find *.txt              # All .txt files from current dir\n"
     "  find /home *.c          # All .c files under /home\n"
     "  find data*              # Files starting with 'data'\n"
     "  find config.?           # config.c, config.h, etc.\n\n"
     "Results show: path, type (file/dir), and size.\n"
     "GUI: Click 'Search' button > Enter pattern > View results",
     80, INTENT_SEARCH_FILE},

    {{"versions", "history", "restore", "recover", NULL},
     "File version history in PhantomOS:\n\n"
     "VIEW HISTORY:\n"
     "  versions <file>         # Show all versions of a file\n\n"
     "RESTORE OLD VERSION:\n"
     "  restore <file> <view_id> [destination]\n\n"
     "EXAMPLES:\n"
     "  versions report.txt             # See all versions\n"
     "  restore report.txt 5            # Restore from view 5\n"
     "  restore report.txt 5 old.txt    # Restore to different name\n\n"
     "Every file change creates a new version in geology.\n"
     "You can always go back to any previous state!\n\n"
     "GUI: Select file > Click 'History' button > View versions",
     85, INTENT_VIEW_HISTORY},

    {{"service", "services", "awaken", "rest", NULL},
     "Service management in PhantomOS:\n\n"
     "  service list           - Show all services\n"
     "  service status <name>  - Check service status\n"
     "  service awaken <name>  - Wake up a service\n"
     "  service rest <name>    - Put service to sleep\n\n"
     "Note: We say 'awaken' not 'start', 'rest' not 'stop'!\n"
     "Services are never killed - they rest peacefully.\n\n"
     "STATES:\n"
     "- AWAKE: Running normally\n"
     "- RESTING: Suspended but can be awakened\n"
     "- DORMANT: Deep sleep, preserves all state",
     75, INTENT_PROCESS_OPERATION},

    {{"network", "net", "connect", "internet", NULL},
     "Network commands in PhantomOS:\n\n"
     "  net status       - Show network status\n"
     "  net connect      - Enable networking\n"
     "  net disconnect   - Disable networking (state preserved)\n"
     "  net list         - List active connections\n"
     "  net suspend <id> - Suspend a connection\n"
     "  net resume <id>  - Resume a connection\n\n"
     "Network state is preserved in geology:\n"
     "- Connection history tracked\n"
     "- States can be restored\n"
     "- Nothing truly disconnects forever",
     70, INTENT_EXPLAIN_CONCEPT},

    {{"ai", "assistant", "chat", "ask", NULL},
     "AI Assistant commands in PhantomOS:\n\n"
     "  ai chat          - Start interactive chat (you're doing this!)\n"
     "  ai ask <question> - Ask a single question\n"
     "  ai explain <cmd> - Explain what a command does\n"
     "  ai suggest <task> - Get command suggestion\n"
     "  ai analyze <code> - Check code safety\n"
     "  ai config        - View/change AI settings\n\n"
     "The AI follows the Phantom Constitution:\n"
     "- Never suggests destructive operations\n"
     "- Always offers safe alternatives\n"
     "- Helps you understand the philosophy\n\n"
     "For external AI models, install Ollama or configure API.",
     70, INTENT_EXPLAIN_CONCEPT},

    {{NULL}, NULL, 0, INTENT_UNKNOWN}
};

/* Error and Troubleshooting */
static const ai_rule_t error_rules[] = {
    {{"governor", "declined", "rejected", "not approved", NULL},
     "Governor declined your code? Here's why and what to do:\n\n"
     "COMMON REASONS:\n"
     "1. Destructive patterns detected (delete, kill, rm, etc.)\n"
     "2. Unsafe system calls\n"
     "3. Capability violations\n\n"
     "SOLUTIONS:\n"
     "- Replace 'delete/rm' with 'hide'\n"
     "- Replace 'kill' with 'suspend'\n"
     "- Use 'governor test <code>' to check specific code\n"
     "- Review alternatives with 'ai suggest <task>'\n\n"
     "The Governor isn't blocking you - it's protecting you!\n"
     "There's always a safe way to achieve your goal.",
     90, INTENT_ERROR_HELP},

    {{"permission", "denied", "access", "unauthorized", NULL},
     "Permission denied? Here's what to check:\n\n"
     "1. USER PERMISSIONS:\n"
     "   - Are you logged in? Check with 'user info'\n"
     "   - Right user? Try 'user list' to see available users\n\n"
     "2. FILE PERMISSIONS:\n"
     "   - Use 'stat <file>' to see permissions\n"
     "   - Check owner with 'ls -l'\n\n"
     "3. GOVERNOR RESTRICTIONS:\n"
     "   - Some operations need approval\n"
     "   - Check with 'governor test <action>'\n\n"
     "Contact your administrator for login credentials",
     85, INTENT_ERROR_HELP},

    {{"not found", "no such", "doesn't exist", "missing", NULL},
     "File or command not found? Let's troubleshoot:\n\n"
     "FOR FILES:\n"
     "1. Check spelling: ls <directory>\n"
     "2. Check path: pwd to see where you are\n"
     "3. Was it hidden? Check geology: geo list\n"
     "4. Time travel: geo view <earlier_id>\n"
     "5. Search: find <pattern>\n\n"
     "FOR COMMANDS:\n"
     "1. Check spelling\n"
     "2. Use 'help' to see available commands\n"
     "3. PhantomOS uses different names:\n"
     "   - 'hide' not 'rm/delete'\n"
     "   - 'suspend' not 'kill'\n"
     "   - 'awaken' not 'start'\n\n"
     "Remember: Nothing is ever truly lost in PhantomOS!",
     85, INTENT_ERROR_HELP},

    {{"error", "failed", "problem", "issue", "wrong", NULL},
     "Encountering an error? Let me help diagnose:\n\n"
     "COMMON ISSUES:\n\n"
     "1. GOVERNOR DECLINED\n"
     "   - Using destructive operations?\n"
     "   - Solution: Use 'hide' instead of 'delete'\n\n"
     "2. PERMISSION DENIED\n"
     "   - Check user: 'user info'\n"
     "   - Contact administrator for access\n\n"
     "3. NOT FOUND\n"
     "   - Check path with 'pwd' and 'ls'\n"
     "   - File might be hidden - try 'geo view'\n\n"
     "4. I/O ERROR\n"
     "   - Geology storage issue\n"
     "   - Check: 'geo status'\n\n"
     "What specific error are you seeing?",
     80, INTENT_ERROR_HELP},

    {{NULL}, NULL, 0, INTENT_UNKNOWN}
};

/* General and Conversational */
static const ai_rule_t general_rules[] = {
    {{"hello", "hi", "hey", "greetings", "morning", "evening", NULL},
     "Hello! I'm the PhantomOS AI assistant.\n\n"
     "I'm here to help you:\n"
     "- Learn PhantomOS commands\n"
     "- Understand the Constitution\n"
     "- Find safe alternatives to destructive operations\n"
     "- Navigate the geology (time-travel storage)\n"
     "- Troubleshoot errors\n\n"
     "What would you like to know?",
     50, INTENT_GREETING},

    {{"thank", "thanks", "appreciate", NULL},
     "You're welcome! Remember these key PhantomOS principles:\n\n"
     "- 'hide' instead of 'delete'\n"
     "- 'suspend' instead of 'kill'\n"
     "- 'geo view' to time travel\n"
     "- Nothing is ever lost!\n\n"
     "Feel free to ask more questions anytime.",
     50, INTENT_THANKS},

    {{"what", "can", "you", "do", "help", NULL},
     "I can help you with many things in PhantomOS:\n\n"
     "EXPLAIN:\n"
     "- Constitution and philosophy\n"
     "- Commands and their usage\n"
     "- Why PhantomOS works differently\n\n"
     "GUIDE:\n"
     "- How to accomplish tasks safely\n"
     "- Safe alternatives to dangerous operations\n"
     "- Troubleshoot errors\n\n"
     "COMMANDS:\n"
     "- Suggest the right command for your task\n"
     "- Explain what commands do\n"
     "- Help with geology navigation\n\n"
     "Try asking:\n"
     "- \"How do I delete a file?\" (safe alternative)\n"
     "- \"What is the Governor?\"\n"
     "- \"How do I restore an old version?\"",
     60, INTENT_HELP},

    {{"create", "new", "make", "write", "add", NULL},
     "Creating is what PhantomOS does best!\n\n"
     "CREATE FILES:\n"
     "  touch <name>           - Create empty file\n"
     "  write <file> <text>    - Create with content\n"
     "  echo \"text\" > file     - Write text to file\n\n"
     "CREATE DIRECTORIES:\n"
     "  mkdir <name>           - Create directory\n\n"
     "CREATE LINKS:\n"
     "  ln -s <target> <link>  - Create symbolic link\n\n"
     "CREATE SNAPSHOTS:\n"
     "  geo save <label>       - Create named checkpoint\n\n"
     "Everything you create is preserved forever in geology!",
     65, INTENT_FILE_OPERATION},

    {{"different", "unique", "special", "why phantom", NULL},
     "What makes PhantomOS unique:\n\n"
     "1. NO DELETION\n"
     "   Traditional OS: rm file.txt -> Gone forever\n"
     "   PhantomOS: hide file.txt -> Always recoverable\n\n"
     "2. TIME TRAVEL\n"
     "   Every change creates a geological layer.\n"
     "   You can travel to any point in history!\n\n"
     "3. PROCESS IMMORTALITY\n"
     "   Processes don't die - they sleep.\n"
     "   Every process can be awakened.\n\n"
     "4. GOVERNOR PROTECTION\n"
     "   All code checked before execution.\n"
     "   Destructive operations prevented.\n\n"
     "The result: You can NEVER lose data.\n"
     "Every mistake is recoverable.\n"
     "Every file has complete history.",
     70, INTENT_COMPARISON},

    {{NULL}, NULL, 0, INTENT_UNKNOWN}
};

/* Code and Programming */
static const ai_rule_t code_rules[] = {
    {{"code", "program", "write", "safe", "compliant", NULL},
     "Writing Phantom-compliant code:\n\n"
     "FORBIDDEN (Governor will decline):\n"
     "- unlink(), remove(), rmdir()\n"
     "- kill(), abort(), exit()\n"
     "- truncate(), ftruncate()\n"
     "- DELETE, DROP in SQL\n\n"
     "SAFE ALTERNATIVES:\n"
     "- vfs_hide() instead of unlink()\n"
     "- phantom_process_suspend() instead of kill()\n"
     "- Create new version instead of truncate()\n"
     "- Use UPDATE/INSERT, never DELETE\n\n"
     "TEST YOUR CODE:\n"
     "  governor test '<your_code>'\n\n"
     "The AI can help analyze and fix code:\n"
     "  ai analyze '<code>'",
     75, INTENT_CODE_QUESTION},

    {{"analyze", "check", "review", "code", NULL},
     "To analyze code for Phantom compliance:\n\n"
     "SHELL:\n"
     "  governor test '<code>'    - Quick safety check\n"
     "  ai analyze '<code>'       - Detailed AI analysis\n\n"
     "The analyzer looks for:\n"
     "1. Destructive patterns (delete, kill, etc.)\n"
     "2. Unsafe system calls\n"
     "3. Capability violations\n"
     "4. Potential data loss scenarios\n\n"
     "THREAT LEVELS:\n"
     "  0 = NONE     - Safe code\n"
     "  1 = LOW      - Minor concerns\n"
     "  2 = MEDIUM   - Needs review\n"
     "  3 = HIGH     - Likely declined\n"
     "  4 = CRITICAL - Definitely declined\n\n"
     "Would you like me to explain how to fix specific code?",
     80, INTENT_CODE_QUESTION},

    {{NULL}, NULL, 0, INTENT_UNKNOWN}
};

/* -----------------------------------------------------------------------------
 * Helper Functions
 * ----------------------------------------------------------------------------- */

static void str_to_lower(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

static int match_keywords(const char *query, const char **keywords) {
    char query_lower[1024];
    strncpy(query_lower, query, sizeof(query_lower) - 1);
    query_lower[sizeof(query_lower) - 1] = '\0';
    str_to_lower(query_lower);

    int matches = 0;
    for (int i = 0; keywords[i] != NULL; i++) {
        if (strstr(query_lower, keywords[i])) {
            matches++;
        }
    }
    return matches;
}

/* Detect user intent */
static ai_intent_t detect_intent(const char *query) {
    char query_lower[1024];
    strncpy(query_lower, query, sizeof(query_lower) - 1);
    query_lower[sizeof(query_lower) - 1] = '\0';
    str_to_lower(query_lower);

    ai_intent_t best_intent = INTENT_UNKNOWN;
    int best_matches = 0;

    for (int i = 0; intent_patterns[i].intent != INTENT_UNKNOWN; i++) {
        int matches = 0;
        for (int j = 0; intent_patterns[i].patterns[j] != NULL; j++) {
            if (strstr(query_lower, intent_patterns[i].patterns[j])) {
                matches++;
            }
        }
        if (matches >= intent_patterns[i].min_matches && matches > best_matches) {
            best_matches = matches;
            best_intent = intent_patterns[i].intent;
        }
    }

    return best_intent;
}

/* Find best matching rule from a rule set */
static const ai_rule_t *find_best_rule(const char *query, const ai_rule_t *rules,
                                        ai_intent_t detected_intent) {
    const ai_rule_t *best = NULL;
    int best_score = 0;

    for (int i = 0; rules[i].response != NULL; i++) {
        int matches = match_keywords(query, rules[i].keywords);
        if (matches > 0) {
            int score = matches * 10 + rules[i].priority;
            /* Bonus for matching detected intent */
            if (rules[i].primary_intent == detected_intent) {
                score += 50;
            }
            if (score > best_score) {
                best_score = score;
                best = &rules[i];
            }
        }
    }
    return best;
}

/* Extract entity from query (filename, command name, etc.) */
static int extract_entity(const char *query, const char *prefix, char *entity, size_t max_len) {
    char query_lower[1024];
    strncpy(query_lower, query, sizeof(query_lower) - 1);
    query_lower[sizeof(query_lower) - 1] = '\0';
    str_to_lower(query_lower);

    char *pos = strstr(query_lower, prefix);
    if (!pos) return 0;

    pos += strlen(prefix);
    while (*pos == ' ') pos++;

    /* Find corresponding position in original query */
    size_t offset = pos - query_lower;
    const char *start = query + offset;

    /* Extract word or quoted string */
    size_t len = 0;
    if (*start == '"' || *start == '\'') {
        char quote = *start++;
        while (start[len] && start[len] != quote && len < max_len - 1) len++;
    } else {
        while (start[len] && !isspace(start[len]) && start[len] != '?' && len < max_len - 1) len++;
    }

    if (len > 0) {
        strncpy(entity, start, len);
        entity[len] = '\0';
        return 1;
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * Smart Response Generation
 * ----------------------------------------------------------------------------- */

/* Generate context-aware response for file questions */
static int respond_file_question(const char *query, char *response, size_t response_size) {
    char filename[256] = {0};

    /* Try to extract filename */
    extract_entity(query, "file", filename, sizeof(filename)) ||
    extract_entity(query, "called", filename, sizeof(filename)) ||
    extract_entity(query, "named", filename, sizeof(filename));

    if (filename[0]) {
        snprintf(response, response_size,
            "For the file '%s':\n\n"
            "VIEW: cat %s\n"
            "INFO: stat %s\n"
            "COPY: cp %s %s_copy\n"
            "RENAME: mv %s new_name\n"
            "HIDE: hide %s  (recoverable via geo view)\n"
            "HISTORY: versions %s\n\n"
            "Would you like to know more about any of these operations?",
            filename, filename, filename, filename, filename,
            filename, filename, filename);
        return 1;
    }
    return 0;
}

/* Generate response for command explanation */
static int respond_command_explanation(const char *query, char *response, size_t response_size) {
    char cmd[64] = {0};

    extract_entity(query, "explain", cmd, sizeof(cmd)) ||
    extract_entity(query, "what does", cmd, sizeof(cmd)) ||
    extract_entity(query, "what is", cmd, sizeof(cmd)) ||
    extract_entity(query, "command", cmd, sizeof(cmd));

    if (!cmd[0]) return 0;

    /* Command explanations */
    struct { const char *cmd; const char *explanation; } cmds[] = {
        {"ls", "ls - List directory contents\nUsage: ls [options] [path]\nOptions: -l (long format), -a (show hidden)"},
        {"cd", "cd - Change directory\nUsage: cd <path>\nExample: cd /home"},
        {"pwd", "pwd - Print working directory\nShows your current location in the filesystem"},
        {"cat", "cat - Display file contents\nUsage: cat <filename>\nShows the text content of a file"},
        {"hide", "hide - Make file invisible (not deleted!)\nUsage: hide <filename>\nFile preserved in geology, recoverable via time travel"},
        {"touch", "touch - Create empty file\nUsage: touch <filename>\nCreates a new empty file"},
        {"mkdir", "mkdir - Create directory\nUsage: mkdir <dirname>\nCreates a new directory"},
        {"cp", "cp - Copy file\nUsage: cp <source> <destination>\nBoth files preserved in geology"},
        {"mv", "mv - Move/rename file\nUsage: mv <old> <new>\nOriginal preserved in geology history"},
        {"find", "find - Search for files\nUsage: find [path] <pattern>\nPatterns: * (any), ? (single char)"},
        {"versions", "versions - Show file history\nUsage: versions <filename>\nDisplays all versions in geology"},
        {"restore", "restore - Recover from history\nUsage: restore <file> <view_id> [dest]\nRecovers file from geological view"},
        {"ps", "ps - List processes\nShows all running and suspended processes"},
        {"suspend", "suspend - Put process to sleep\nUsage: suspend <pid>\nProcess can be resumed later"},
        {"resume", "resume - Wake up process\nUsage: resume <pid>\nWakes a suspended process"},
        {"geo", "geo - Geology commands\nUsage: geo list|view|save|current\nTime travel through storage history"},
        {"governor", "governor - Code approval system\nUsage: governor status|mode|test\nEnsures code safety before execution"},
        {NULL, NULL}
    };

    for (int i = 0; cmds[i].cmd; i++) {
        if (strcasecmp(cmd, cmds[i].cmd) == 0) {
            snprintf(response, response_size, "%s", cmds[i].explanation);
            return 1;
        }
    }

    return 0;
}

/* -----------------------------------------------------------------------------
 * Public API Functions
 * ----------------------------------------------------------------------------- */

int phantom_ai_builtin_respond(const char *query, char *response, size_t response_size) {
    if (!query || !response || response_size == 0) return -1;

    response[0] = '\0';

    /* Detect user intent */
    ai_intent_t intent = detect_intent(query);

    /* Try smart response generators first */
    if (intent == INTENT_FILE_OPERATION || intent == INTENT_SEARCH_FILE) {
        if (respond_file_question(query, response, response_size)) {
            return 0;
        }
    }

    if (intent == INTENT_EXPLAIN_CONCEPT || intent == INTENT_HOW_TO) {
        if (respond_command_explanation(query, response, response_size)) {
            return 0;
        }
    }

    /* Search through rule sets with intent awareness */
    const ai_rule_t *best_rule = NULL;
    int best_score = 0;

    /* Check all rule sets */
    struct { const ai_rule_t *rules; } rule_sets[] = {
        {constitution_rules},
        {command_rules},
        {error_rules},
        {code_rules},
        {general_rules},
        {NULL}
    };

    for (int s = 0; rule_sets[s].rules != NULL; s++) {
        const ai_rule_t *r = find_best_rule(query, rule_sets[s].rules, intent);
        if (r) {
            int score = match_keywords(query, r->keywords) * 10 + r->priority;
            if (r->primary_intent == intent) score += 50;
            if (score > best_score) {
                best_score = score;
                best_rule = r;
            }
        }
    }

    if (best_rule) {
        strncpy(response, best_rule->response, response_size - 1);
        response[response_size - 1] = '\0';
        return 0;
    }

    /* Default response with suggestions based on intent */
    const char *suggestion = "";
    switch (intent) {
        case INTENT_FILE_OPERATION:
            suggestion = "For file operations, try: ls, cat, touch, mkdir, hide, cp, mv, find";
            break;
        case INTENT_PROCESS_OPERATION:
            suggestion = "For processes, try: ps, suspend, resume";
            break;
        case INTENT_GEOLOGY_QUERY:
            suggestion = "For history/geology, try: geo list, versions, restore";
            break;
        case INTENT_ERROR_HELP:
            suggestion = "Tell me the specific error message and I'll help troubleshoot.";
            break;
        default:
            suggestion = "Try asking about commands, the constitution, or how to do specific tasks.";
    }

    snprintf(response, response_size,
        "I'm the PhantomOS AI assistant. I can help you with:\n\n"
        "- Understanding the Phantom Constitution\n"
        "- Learning PhantomOS commands\n"
        "- Finding safe alternatives to destructive operations\n"
        "- Navigating geology (time-travel storage)\n"
        "- Troubleshooting errors\n\n"
        "%s\n\n"
        "Example questions:\n"
        "- \"How do I delete a file?\"\n"
        "- \"What is the Governor?\"\n"
        "- \"How do I restore an old version?\"",
        suggestion);

    return 0;
}

int phantom_ai_builtin_is_destructive_request(const char *query) {
    if (!query) return 0;

    char query_lower[1024];
    strncpy(query_lower, query, sizeof(query_lower) - 1);
    query_lower[sizeof(query_lower) - 1] = '\0';
    str_to_lower(query_lower);

    const char *destructive_patterns[] = {
        "delete", "remove", "rm ", "unlink", "erase", "destroy", "purge",
        "kill", "terminate", "abort", "sigkill", "sigterm", "end process",
        "truncate", "wipe", "shred", "obliterate", "clear all",
        "drop table", "delete from", "remove all",
        NULL
    };

    for (int i = 0; destructive_patterns[i]; i++) {
        if (strstr(query_lower, destructive_patterns[i])) {
            return 1;
        }
    }
    return 0;
}

int phantom_ai_builtin_suggest_safe(const char *query, char *response, size_t response_size) {
    if (!query || !response || response_size == 0) return -1;

    char query_lower[1024];
    strncpy(query_lower, query, sizeof(query_lower) - 1);
    query_lower[sizeof(query_lower) - 1] = '\0';
    str_to_lower(query_lower);

    /* Extract potential filename or target */
    char target[256] = "the item";
    extract_entity(query, "delete", target, sizeof(target)) ||
    extract_entity(query, "remove", target, sizeof(target)) ||
    extract_entity(query, "kill", target, sizeof(target)) ||
    extract_entity(query, "file", target, sizeof(target));

    if (strstr(query_lower, "delete") || strstr(query_lower, "remove") ||
        strstr(query_lower, "rm ") || strstr(query_lower, "unlink") ||
        strstr(query_lower, "erase")) {
        snprintf(response, response_size,
            "In PhantomOS, we don't delete - we HIDE instead!\n\n"
            "Instead of deleting %s, use:\n\n"
            "  hide %s\n\n"
            "What happens:\n"
            "1. File becomes invisible in current view\n"
            "2. Content preserved forever in geology\n"
            "3. Recoverable anytime via: geo view <earlier_id>\n"
            "4. Full history with: versions %s\n\n"
            "This way you can NEVER lose data by accident!\n"
            "Want me to explain how to recover hidden files?",
            target, target, target);
        return 0;
    }

    if (strstr(query_lower, "kill") || strstr(query_lower, "terminate") ||
        strstr(query_lower, "stop") || strstr(query_lower, "abort") ||
        strstr(query_lower, "end process")) {
        snprintf(response, response_size,
            "In PhantomOS, we don't kill processes - we SUSPEND them!\n\n"
            "Instead of killing, use:\n\n"
            "  suspend <pid>    - Put process to sleep\n"
            "  resume <pid>     - Wake it back up\n\n"
            "To find the PID:\n"
            "  ps               - List all processes\n\n"
            "Benefits:\n"
            "- Process state fully preserved\n"
            "- Can be resumed anytime\n"
            "- No data loss from terminated processes\n\n"
            "What process are you trying to stop?");
        return 0;
    }

    if (strstr(query_lower, "truncate") || strstr(query_lower, "overwrite") ||
        strstr(query_lower, "wipe") || strstr(query_lower, "clear")) {
        snprintf(response, response_size,
            "In PhantomOS, we don't truncate or wipe - we create NEW versions!\n\n"
            "Instead of overwriting, just write normally:\n"
            "- Every write creates a new version\n"
            "- Old versions preserved in geology\n"
            "- Access history with: versions <filename>\n"
            "- Restore any version: restore <file> <view_id>\n\n"
            "The geology preserves everything automatically!\n"
            "What are you trying to accomplish?");
        return 0;
    }

    /* Generic destructive request */
    snprintf(response, response_size,
        "PhantomOS follows the Prime Directive:\n"
        "\"To Create, Not To Destroy\"\n\n"
        "Destructive operations have safe alternatives:\n\n"
        "- DELETE/RM -> hide (file preserved in geology)\n"
        "- KILL -> suspend (process can be resumed)\n"
        "- TRUNCATE -> write new version (history preserved)\n\n"
        "The benefit: You can NEVER lose data!\n\n"
        "What are you trying to accomplish? I can suggest the right approach.");

    return 0;
}

int phantom_ai_builtin_chat(phantom_ai_t *ai, const char *message,
                            char *response, size_t response_size) {
    if (!message || !response || response_size == 0) return -1;
    (void)ai;

    /* Check for destructive requests first */
    if (phantom_ai_builtin_is_destructive_request(message)) {
        return phantom_ai_builtin_suggest_safe(message, response, response_size);
    }

    /* Get smart response */
    return phantom_ai_builtin_respond(message, response, response_size);
}

void phantom_ai_builtin_greeting(char *response, size_t response_size) {
    if (!response || response_size == 0) return;

    const char *greetings[] = {
        "Hello! I'm the PhantomOS AI assistant. How can I help you today?\n\n"
        "I can explain commands, help troubleshoot, and guide you through "
        "the philosophy of \"To Create, Not To Destroy.\"",

        "Welcome to PhantomOS! I'm here to help you navigate the system.\n\n"
        "Remember: In PhantomOS, nothing is ever deleted - use 'hide' for files "
        "and 'suspend' for processes. What can I help you with?",

        "Greetings! I'm your PhantomOS AI assistant.\n\n"
        "Fun fact: Every change you make is preserved forever in the geology. "
        "You can time-travel to any point! Ask me how.",

        "Hi there! Ready to help you create, not destroy.\n\n"
        "Quick tips:\n- 'hide' instead of 'delete'\n- 'suspend' instead of 'kill'\n"
        "- 'geo view' to time travel\n\nWhat would you like to know?",
    };

    int idx = (int)(time(NULL) % 4);
    strncpy(response, greetings[idx], response_size - 1);
    response[response_size - 1] = '\0';
}

/* Get command suggestion for natural language task */
int phantom_ai_builtin_suggest_command(const char *task, char *command, size_t command_size) {
    if (!task || !command || command_size == 0) return -1;

    char task_lower[512];
    strncpy(task_lower, task, sizeof(task_lower) - 1);
    task_lower[sizeof(task_lower) - 1] = '\0';
    str_to_lower(task_lower);

    /* Task to command mapping */
    struct { const char *patterns[6]; const char *cmd; } mappings[] = {
        {{"list", "show", "files", "directory", NULL}, "ls"},
        {{"where am i", "current", "location", NULL}, "pwd"},
        {{"change", "go to", "navigate", "cd", NULL}, "cd <path>"},
        {{"view", "read", "show", "content", "cat", NULL}, "cat <filename>"},
        {{"create", "new", "file", "empty", NULL}, "touch <filename>"},
        {{"create", "new", "directory", "folder", NULL}, "mkdir <dirname>"},
        {{"hide", "remove", "delete", NULL}, "hide <filename>"},
        {{"copy", "duplicate", NULL}, "cp <source> <destination>"},
        {{"rename", "move", NULL}, "mv <old> <new>"},
        {{"search", "find", "locate", NULL}, "find <pattern>"},
        {{"history", "versions", "old", NULL}, "versions <filename>"},
        {{"restore", "recover", "undo", NULL}, "restore <file> <view_id>"},
        {{"process", "running", "list", NULL}, "ps"},
        {{"sleep", "pause", "suspend", NULL}, "suspend <pid>"},
        {{"wake", "continue", "resume", NULL}, "resume <pid>"},
        {{"snapshot", "save", "checkpoint", NULL}, "geo save <label>"},
        {{"time travel", "go back", "view", NULL}, "geo view <id>"},
        {{NULL}, NULL}
    };

    for (int i = 0; mappings[i].cmd; i++) {
        int matches = 0;
        for (int j = 0; mappings[i].patterns[j]; j++) {
            if (strstr(task_lower, mappings[i].patterns[j])) {
                matches++;
            }
        }
        if (matches >= 1) {
            strncpy(command, mappings[i].cmd, command_size - 1);
            command[command_size - 1] = '\0';
            return 0;
        }
    }

    /* No match */
    strncpy(command, "help", command_size - 1);
    command[command_size - 1] = '\0';
    return -1;
}

/* Analyze code for Phantom compliance (basic) */
int phantom_ai_builtin_analyze_code(const char *code, char *analysis, size_t analysis_size) {
    if (!code || !analysis || analysis_size == 0) return -1;

    char code_lower[4096];
    strncpy(code_lower, code, sizeof(code_lower) - 1);
    code_lower[sizeof(code_lower) - 1] = '\0';
    str_to_lower(code_lower);

    int threat_level = 0;
    char issues[1024] = "";
    char suggestions[1024] = "";

    /* Check for destructive patterns */
    struct { const char *pattern; const char *issue; const char *suggestion; int level; } checks[] = {
        {"unlink", "unlink() - File deletion", "Use vfs_hide() instead", 4},
        {"remove", "remove() - File removal", "Use vfs_hide() instead", 4},
        {"rmdir", "rmdir() - Directory removal", "Use vfs_hide() instead", 4},
        {"kill(", "kill() - Process termination", "Use phantom_process_suspend()", 4},
        {"abort", "abort() - Program abort", "Use phantom_process_suspend()", 3},
        {"exit", "exit() - Program exit", "Consider suspend instead", 2},
        {"truncate", "truncate() - File truncation", "Write new version instead", 3},
        {"ftruncate", "ftruncate() - File truncation", "Write new version instead", 3},
        {"delete from", "DELETE SQL - Data deletion", "Use UPDATE/INSERT patterns", 4},
        {"drop table", "DROP TABLE - Table deletion", "Archive table instead", 4},
        {"sigkill", "SIGKILL - Force kill", "Use SIGSTOP/suspend instead", 4},
        {"sigterm", "SIGTERM - Terminate", "Use SIGSTOP/suspend instead", 3},
        {NULL, NULL, NULL, 0}
    };

    for (int i = 0; checks[i].pattern; i++) {
        if (strstr(code_lower, checks[i].pattern)) {
            if (checks[i].level > threat_level) {
                threat_level = checks[i].level;
            }
            if (strlen(issues) > 0) strcat(issues, "\n");
            strcat(issues, "- ");
            strcat(issues, checks[i].issue);

            if (strlen(suggestions) > 0) strcat(suggestions, "\n");
            strcat(suggestions, "- ");
            strcat(suggestions, checks[i].suggestion);
        }
    }

    const char *verdict;
    switch (threat_level) {
        case 0: verdict = "SAFE - Code appears Phantom-compliant"; break;
        case 1: verdict = "LOW - Minor concerns, likely approvable"; break;
        case 2: verdict = "MEDIUM - Review recommended"; break;
        case 3: verdict = "HIGH - Likely to be declined by Governor"; break;
        case 4: verdict = "CRITICAL - Will be declined by Governor"; break;
        default: verdict = "UNKNOWN"; break;
    }

    if (threat_level == 0) {
        snprintf(analysis, analysis_size,
            "CODE ANALYSIS RESULT\n"
            "====================\n\n"
            "Threat Level: %d (NONE)\n"
            "Verdict: %s\n\n"
            "No destructive patterns detected.\n"
            "This code should be approved by the Governor.",
            threat_level, verdict);
    } else {
        snprintf(analysis, analysis_size,
            "CODE ANALYSIS RESULT\n"
            "====================\n\n"
            "Threat Level: %d\n"
            "Verdict: %s\n\n"
            "ISSUES FOUND:\n%s\n\n"
            "SUGGESTED FIXES:\n%s\n\n"
            "Modify the code to use Phantom-safe alternatives,\n"
            "then resubmit for Governor approval.",
            threat_level, verdict, issues, suggestions);
    }

    return threat_level;
}
