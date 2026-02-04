/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM USER SYSTEM
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * A user and permission system that embodies the Phantom philosophy:
 * - Users are NEVER deleted, only deactivated (become dormant)
 * - All user actions are logged permanently to geology
 * - Permissions integrate with the Governor capability system
 * - Password changes create new versions, old passwords preserved in history
 *
 * Key Principles:
 * 1. ACCOUNTABILITY: Every action is traceable to a user
 * 2. PERSISTENCE: User accounts exist forever (dormant when "deleted")
 * 3. TRANSPARENCY: All permission grants/revocations are logged
 * 4. INTEGRATION: Works with Governor for capability-based access
 */

#ifndef PHANTOM_USER_H
#define PHANTOM_USER_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "phantom.h"
#include "governor.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────── */

#define PHANTOM_MAX_USERS       256
#define PHANTOM_MAX_GROUPS      64
#define PHANTOM_MAX_USERNAME    64
#define PHANTOM_MAX_GROUPNAME   64
#define PHANTOM_HASH_LEN        64      /* SHA-256 hex string */
#define PHANTOM_SALT_LEN        32

/* Special user IDs */
#define PHANTOM_UID_ROOT        0
#define PHANTOM_UID_SYSTEM      1
#define PHANTOM_UID_NOBODY      65534
#define PHANTOM_UID_FIRST_USER  1000

/* Special group IDs */
#define PHANTOM_GID_ROOT        0
#define PHANTOM_GID_WHEEL       10      /* Admin group */
#define PHANTOM_GID_USERS       100

/* ─────────────────────────────────────────────────────────────────────────────
 * User States
 *
 * Users are never deleted - they transition between states.
 * Even "deleted" users remain as DORMANT for accountability.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    USER_STATE_ACTIVE = 0,      /* Normal active user */
    USER_STATE_LOCKED,          /* Temporarily locked (too many failed logins) */
    USER_STATE_SUSPENDED,       /* Admin suspended (can be reactivated) */
    USER_STATE_DORMANT,         /* "Deleted" - preserved but inactive */
} phantom_user_state_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Permission Flags
 *
 * These extend Governor capabilities for user-level permissions.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PERM_LOGIN          = (1 << 0),   /* Can log in */
    PERM_SUDO           = (1 << 1),   /* Can elevate privileges */
    PERM_CREATE_USER    = (1 << 2),   /* Can create new users */
    PERM_MANAGE_USER    = (1 << 3),   /* Can modify other users */
    PERM_CREATE_GROUP   = (1 << 4),   /* Can create groups */
    PERM_MANAGE_GROUP   = (1 << 5),   /* Can modify groups */
    PERM_INSTALL_PKG    = (1 << 6),   /* Can install packages */
    PERM_SYSTEM_CONFIG  = (1 << 7),   /* Can modify system config */
    PERM_VIEW_LOGS      = (1 << 8),   /* Can view system logs */
    PERM_NETWORK_ADMIN  = (1 << 9),   /* Can manage network */
    PERM_GOVERNOR_ADMIN = (1 << 10),  /* Can configure Governor */
} phantom_permission_t;

/* Permission sets */
#define PERM_NONE           0
#define PERM_BASIC          (PERM_LOGIN)
#define PERM_STANDARD       (PERM_LOGIN | PERM_VIEW_LOGS)
#define PERM_ADMIN          (0xFFFFFFFF)  /* All permissions */

/* ─────────────────────────────────────────────────────────────────────────────
 * User Structure
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_user {
    uint32_t uid;                       /* Unique user ID (never reused) */
    uint32_t primary_gid;               /* Primary group */
    phantom_user_state_t state;

    char username[PHANTOM_MAX_USERNAME];
    char full_name[128];
    char home_dir[256];
    char shell[128];

    /* Authentication (hashed, never plaintext) */
    char password_hash[PHANTOM_HASH_LEN + 1];
    char password_salt[PHANTOM_SALT_LEN + 1];
    uint32_t password_version;          /* Increments on each change */

    /* Permissions */
    uint32_t permissions;               /* User-level permissions */
    uint32_t capabilities;              /* Governor capabilities granted */

    /* Group memberships */
    uint32_t groups[16];                /* Secondary group IDs */
    int group_count;

    /* Timestamps */
    time_t created_at;
    time_t last_login;
    time_t last_password_change;
    time_t state_changed_at;

    /* Security */
    uint32_t failed_logins;             /* Since last successful login */
    uint32_t total_logins;              /* Lifetime count */
    char last_login_ip[64];

    /* Creator accountability */
    uint32_t created_by_uid;            /* Who created this user */

} phantom_user_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Group Structure
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_group {
    uint32_t gid;                       /* Unique group ID */
    phantom_user_state_t state;         /* Groups can also be dormant */

    char name[PHANTOM_MAX_GROUPNAME];
    char description[256];

    /* Permissions granted to group members */
    uint32_t permissions;
    uint32_t capabilities;

    /* Timestamps */
    time_t created_at;
    time_t modified_at;

    /* Creator accountability */
    uint32_t created_by_uid;

} phantom_group_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Session Structure
 *
 * Represents an active login session.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_session {
    uint64_t session_id;                /* Unique session identifier */
    uint32_t uid;                       /* User this session belongs to */

    time_t started_at;
    time_t last_activity;
    time_t expires_at;                  /* 0 = no expiry */

    char source_ip[64];                 /* Where they logged in from */
    char terminal[64];                  /* TTY or pseudo-terminal */

    int is_elevated;                    /* Running with sudo? */
    uint32_t effective_uid;             /* Effective UID (for sudo) */

} phantom_session_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * User System Context
 * ───────────────────────────────────────────────────────────────────────────── */

#define PHANTOM_MAX_SESSIONS    128

typedef struct phantom_user_system {
    /* User database */
    phantom_user_t users[PHANTOM_MAX_USERS];
    int user_count;
    uint32_t next_uid;

    /* Group database */
    phantom_group_t groups[PHANTOM_MAX_GROUPS];
    int group_count;
    uint32_t next_gid;

    /* Active sessions */
    phantom_session_t sessions[PHANTOM_MAX_SESSIONS];
    int session_count;
    uint64_t next_session_id;

    /* Current session (for shell context) */
    phantom_session_t *current_session;

    /* Statistics */
    uint64_t total_logins;
    uint64_t failed_logins;
    uint64_t users_created;
    uint64_t users_dormant;

    /* References */
    struct phantom_kernel *kernel;
    struct phantom_governor *governor;

    /* Configuration */
    int require_strong_passwords;
    int max_failed_logins;              /* Before lockout */
    int lockout_duration_sec;
    int session_timeout_sec;

    /* State */
    int initialized;

} phantom_user_system_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Result Codes
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    USER_OK = 0,
    USER_ERR_INVALID = -1,
    USER_ERR_EXISTS = -2,
    USER_ERR_NOT_FOUND = -3,
    USER_ERR_DENIED = -4,
    USER_ERR_LOCKED = -5,
    USER_ERR_DORMANT = -6,
    USER_ERR_BAD_PASSWORD = -7,
    USER_ERR_WEAK_PASSWORD = -8,
    USER_ERR_SESSION_EXPIRED = -9,
    USER_ERR_NO_SESSION = -10,
    USER_ERR_FULL = -11,
} phantom_user_result_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * User System API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Lifecycle */
int phantom_user_system_init(phantom_user_system_t *sys, struct phantom_kernel *kernel);
void phantom_user_system_shutdown(phantom_user_system_t *sys);

/* User Management */
int phantom_user_create(phantom_user_system_t *sys, const char *username,
                        const char *password, const char *full_name,
                        uint32_t creator_uid, uint32_t *uid_out);
int phantom_user_set_password(phantom_user_system_t *sys, uint32_t uid,
                              const char *new_password, uint32_t actor_uid);
int phantom_user_set_state(phantom_user_system_t *sys, uint32_t uid,
                           phantom_user_state_t state, uint32_t actor_uid);
int phantom_user_grant_permission(phantom_user_system_t *sys, uint32_t uid,
                                  uint32_t permission, uint32_t actor_uid);
int phantom_user_revoke_permission(phantom_user_system_t *sys, uint32_t uid,
                                   uint32_t permission, uint32_t actor_uid);
int phantom_user_grant_capability(phantom_user_system_t *sys, uint32_t uid,
                                  uint32_t capability, uint32_t actor_uid);

/* User Lookup */
phantom_user_t *phantom_user_find_by_uid(phantom_user_system_t *sys, uint32_t uid);
phantom_user_t *phantom_user_find_by_name(phantom_user_system_t *sys, const char *username);

/* Group Management */
int phantom_group_create(phantom_user_system_t *sys, const char *name,
                         const char *description, uint32_t creator_uid,
                         uint32_t *gid_out);
int phantom_group_add_user(phantom_user_system_t *sys, uint32_t gid,
                           uint32_t uid, uint32_t actor_uid);
int phantom_group_remove_user(phantom_user_system_t *sys, uint32_t gid,
                              uint32_t uid, uint32_t actor_uid);
phantom_group_t *phantom_group_find_by_gid(phantom_user_system_t *sys, uint32_t gid);
phantom_group_t *phantom_group_find_by_name(phantom_user_system_t *sys, const char *name);

/* Authentication */
int phantom_user_authenticate(phantom_user_system_t *sys, const char *username,
                              const char *password, phantom_session_t **session_out);
int phantom_user_authenticate_dna(phantom_user_system_t *sys, const char *username,
                                  const char *dna_sequence, void *dnauth_system,
                                  phantom_session_t **session_out);
int phantom_user_logout(phantom_user_system_t *sys, uint64_t session_id);
int phantom_user_elevate(phantom_user_system_t *sys, uint64_t session_id,
                         const char *password);

/* Session Management */
phantom_session_t *phantom_session_get(phantom_user_system_t *sys, uint64_t session_id);
int phantom_session_refresh(phantom_user_system_t *sys, uint64_t session_id);
int phantom_session_check(phantom_user_system_t *sys, uint64_t session_id);

/* Permission Checking */
int phantom_user_has_permission(phantom_user_system_t *sys, uint32_t uid,
                                uint32_t permission);
int phantom_user_has_capability(phantom_user_system_t *sys, uint32_t uid,
                                uint32_t capability);
int phantom_user_can_access(phantom_user_system_t *sys, uint32_t uid,
                            const char *path, int mode);

/* Utility */
const char *phantom_user_state_string(phantom_user_state_t state);
const char *phantom_user_result_string(phantom_user_result_t result);
void phantom_user_print_info(phantom_user_t *user);
void phantom_user_system_print_stats(phantom_user_system_t *sys);

/* Password Utilities */
int phantom_password_hash(const char *password, const char *salt,
                          char *hash_out, size_t hash_len);
int phantom_password_generate_salt(char *salt_out, size_t salt_len);
int phantom_password_check_strength(const char *password);

#endif /* PHANTOM_USER_H */
