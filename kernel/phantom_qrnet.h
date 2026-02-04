/*
 * ==============================================================================
 *                    PHANTOM QRNET - QR Code Distributed File Network
 *                          "To Create, Not To Destroy"
 * ==============================================================================
 *
 * QRNet is a cryptographically-signed, distributed file network that uses QR
 * codes to embed security keys and fingerprints for file destinations. Each
 * node maintains its own keypair derived from DNAuth identity, with all
 * operations validated through the Governor.
 *
 * Key Features:
 * - QR codes embedded in data with destination, hash, signature, metadata
 * - Per-node keypair architecture using DNAuth-derived keys
 * - Governor synchronization for state versioning
 * - Adaptive QR code sizing based on context
 * - Append-only architecture (old codes never destroyed)
 * - Distributed trust model with Governor as authority
 */

#ifndef PHANTOM_QRNET_H
#define PHANTOM_QRNET_H

#include <stdint.h>
#include <time.h>

/* Forward declarations */
struct phantom_governor;
struct dnauth_system;

/* ==============================================================================
 * Constants
 * ============================================================================== */

#define QRNET_MAX_PATH          512
#define QRNET_HASH_LEN          64      /* SHA-256 hex string */
#define QRNET_SIGNATURE_LEN     144     /* Hex-encoded ECDSA signature (72 bytes * 2) */
#define QRNET_NODE_ID_LEN       64
#define QRNET_MAX_NODES         256
#define QRNET_MAX_CODES         4096
#define QRNET_MAX_METADATA      256
#define QRNET_VERSION_MIN       1
#define QRNET_VERSION_MAX       40

/* QR Code capacity by version (approximate alphanumeric capacity) */
#define QRNET_V1_CAPACITY       17
#define QRNET_V10_CAPACITY      174
#define QRNET_V20_CAPACITY      858
#define QRNET_V40_CAPACITY      2953

/* ==============================================================================
 * Enumerations
 * ============================================================================== */

/* QRNet operation results */
typedef enum {
    QRNET_OK = 0,
    QRNET_ERROR,
    QRNET_INVALID_PARAM,
    QRNET_NOT_INITIALIZED,
    QRNET_NODE_NOT_FOUND,
    QRNET_CODE_NOT_FOUND,
    QRNET_SIGNATURE_INVALID,
    QRNET_HASH_MISMATCH,
    QRNET_GOVERNOR_DENIED,
    QRNET_DNAUTH_INVALID,
    QRNET_REVOKED,
    QRNET_EXPIRED,
    QRNET_CAPACITY_EXCEEDED,
    QRNET_ALREADY_EXISTS,
    QRNET_STORAGE_ERROR
} qrnet_result_t;

/* Trust levels */
typedef enum {
    QRNET_TRUST_UNKNOWN = 0,    /* Unknown node - be cautious */
    QRNET_TRUST_MINIMAL,        /* Minimal trust - verify everything */
    QRNET_TRUST_PARTIAL,        /* Some trust - verify signatures */
    QRNET_TRUST_VERIFIED,       /* Verified through Governor */
    QRNET_TRUST_FULL            /* Fully trusted node */
} qrnet_trust_t;

/* File classification for adaptive sizing */
typedef enum {
    QRNET_FILE_USER = 0,        /* User data - minimum version 8 */
    QRNET_FILE_SYSTEM,          /* System files - minimum version 15 */
    QRNET_FILE_CONSTITUTIONAL,  /* Constitutional - full expansion */
    QRNET_FILE_CRITICAL         /* Critical infrastructure - max expansion */
} qrnet_file_class_t;

/* QR code state */
typedef enum {
    QRNET_CODE_ACTIVE = 0,      /* Active and valid */
    QRNET_CODE_SUPERSEDED,      /* Replaced by newer code */
    QRNET_CODE_REVOKED,         /* Creator identity revoked */
    QRNET_CODE_EXPIRED          /* Time-bound validation expired */
} qrnet_code_state_t;

/* Node state */
typedef enum {
    QRNET_NODE_ACTIVE = 0,
    QRNET_NODE_INACTIVE,
    QRNET_NODE_REVOKED,
    QRNET_NODE_SYNCING
} qrnet_node_state_t;

/* Operation types for Governor validation */
typedef enum {
    QRNET_OP_CREATE_CODE,       /* Create new QR code */
    QRNET_OP_VERIFY_CODE,       /* Verify existing code */
    QRNET_OP_LINK_FILE,         /* Link to file destination */
    QRNET_OP_REVOKE_CODE,       /* Revoke a code */
    QRNET_OP_SUPERSEDE_CODE,    /* Replace with new code */
    QRNET_OP_JOIN_NETWORK,      /* Join QRNet network */
    QRNET_OP_SYNC_STATE,        /* Synchronize governor state */
    QRNET_OP_MULTI_SIGN         /* Multi-signature consensus */
} qrnet_operation_t;

/* ==============================================================================
 * Data Structures
 * ============================================================================== */

/* Content hash (SHA-256 or BLAKE3) */
typedef struct qrnet_hash {
    char hex[QRNET_HASH_LEN + 1];       /* Hex string representation */
    uint8_t bytes[32];                   /* Raw bytes */
    int algorithm;                       /* 0=SHA256, 1=BLAKE3 */
} qrnet_hash_t;

/* ECDSA Keypair for cryptographic signatures */
#define QRNET_PUBKEY_LEN    65          /* Uncompressed ECDSA public key */
#define QRNET_PRIVKEY_LEN   32          /* ECDSA private key */
#define QRNET_ECDSA_SIG_LEN 72          /* Max DER-encoded ECDSA signature */

typedef struct qrnet_keypair {
    uint8_t public_key[QRNET_PUBKEY_LEN];   /* Uncompressed public key */
    uint8_t private_key[QRNET_PRIVKEY_LEN]; /* Private key (only for local node) */
    char public_key_hex[QRNET_PUBKEY_LEN * 2 + 1];
    int has_private_key;                     /* 1 if private key is present */
    int initialized;
} qrnet_keypair_t;

/* Compact signature - now with real ECDSA */
typedef struct qrnet_signature {
    uint8_t sig_bytes[QRNET_ECDSA_SIG_LEN]; /* DER-encoded ECDSA signature */
    int sig_len;                             /* Actual signature length */
    char data[QRNET_SIGNATURE_LEN + 1];     /* Hex-encoded for display/storage */
    char signer_id[QRNET_NODE_ID_LEN];      /* DNAuth identity of signer */
    char signer_pubkey[QRNET_PUBKEY_LEN * 2 + 1]; /* Public key of signer */
    time_t timestamp;                        /* Signing timestamp */
    uint32_t governor_state;                 /* Governor state version at signing */
} qrnet_signature_t;

/* QR code data structure */
typedef struct qrnet_code {
    uint32_t code_id;                    /* Unique code identifier */

    /* Core data (minimal QR code) */
    char destination_path[QRNET_MAX_PATH];
    qrnet_hash_t content_hash;
    char dnauth_creator[QRNET_NODE_ID_LEN];
    uint32_t governor_state_version;
    qrnet_signature_t signature;

    /* Metadata */
    qrnet_file_class_t file_class;
    qrnet_code_state_t state;
    int qr_version;                      /* 1-40 */
    time_t created_at;
    time_t expires_at;                   /* 0 = never */
    time_t last_verified;
    uint32_t verification_count;

    /* Expansion data (for larger QR versions) */
    int has_governor_proof;
    char governor_proof[512];            /* Governor validation proof */
    int has_cached_verification;
    char cached_verification[256];       /* Cached verification result */

    /* Multi-signature support */
    int signature_count;
    qrnet_signature_t additional_signatures[8];

    /* Supersession chain */
    uint32_t supersedes_code_id;         /* Code this supersedes (0 = none) */
    uint32_t superseded_by_code_id;      /* Code that supersedes this (0 = none) */

    /* Storage */
    char qr_data[4096];                  /* Encoded QR data */
    int qr_data_len;

    struct qrnet_code *next;
} qrnet_code_t;

/* Network node */
typedef struct qrnet_node {
    char node_id[QRNET_NODE_ID_LEN];     /* Unique node identifier */
    char dnauth_identity[QRNET_NODE_ID_LEN]; /* DNAuth identity */

    /* ECDSA Keypair for signing */
    qrnet_keypair_t keypair;             /* Node's cryptographic keypair */

    /* Trust */
    qrnet_trust_t trust_level;
    qrnet_node_state_t state;

    /* State synchronization */
    uint32_t governor_state_version;     /* Last synced governor state */
    time_t last_sync;

    /* Statistics */
    uint32_t codes_created;
    uint32_t codes_verified;
    uint32_t verifications_failed;
    time_t joined_at;
    time_t last_active;

    /* Network info */
    char address[256];                   /* Network address if remote */
    int is_local;                        /* Is this the local node */

    struct qrnet_node *next;
} qrnet_node_t;

/* Governor state reference */
typedef struct qrnet_gov_state {
    uint32_t version;
    char hash[QRNET_HASH_LEN + 1];
    time_t timestamp;
    int constitutional_compliant;
} qrnet_gov_state_t;

/* Verification result */
typedef struct qrnet_verification {
    qrnet_result_t result;
    int signature_valid;
    int hash_valid;
    int governor_state_valid;
    int dnauth_valid;
    int not_revoked;
    int not_expired;
    qrnet_trust_t trust_level;
    char details[512];
} qrnet_verification_t;

/* QRNet system */
typedef struct qrnet_system {
    /* Initialization */
    int initialized;
    char data_path[512];

    /* Local node */
    qrnet_node_t *local_node;

    /* Nodes and codes */
    qrnet_node_t *nodes;
    int node_count;
    qrnet_code_t *codes;
    int code_count;

    /* Governor integration */
    struct phantom_governor *governor;
    qrnet_gov_state_t current_gov_state;

    /* DNAuth integration */
    struct dnauth_system *dnauth;

    /* Statistics */
    uint64_t total_codes_created;
    uint64_t total_verifications;
    uint64_t failed_verifications;
    uint64_t revocations;

    /* Adaptive sizing settings */
    int min_version_user;                /* Default: 8 */
    int min_version_system;              /* Default: 15 */
    int min_version_constitutional;      /* Default: 25 */
    int min_version_critical;            /* Default: 40 */

    /* Configuration */
    int auto_expand;                     /* Automatically expand QR versions */
    int cache_verifications;             /* Cache verification results */
    int require_governor_approval;       /* All ops need Governor approval */
    time_t default_expiry;               /* Default expiration (0 = never) */
} qrnet_system_t;

/* ==============================================================================
 * Core Functions
 * ============================================================================== */

/* Initialization and cleanup */
qrnet_system_t *qrnet_init(const char *data_path);
void qrnet_cleanup(qrnet_system_t *sys);

/* Governor and DNAuth integration */
void qrnet_set_governor(qrnet_system_t *sys, struct phantom_governor *gov);
void qrnet_set_dnauth(qrnet_system_t *sys, struct dnauth_system *dnauth);
qrnet_result_t qrnet_sync_governor_state(qrnet_system_t *sys);

/* ==============================================================================
 * QR Code Operations
 * ============================================================================== */

/* Create QR code for file/destination */
qrnet_result_t qrnet_create_code(qrnet_system_t *sys,
                                  const char *destination_path,
                                  const void *content,
                                  size_t content_len,
                                  qrnet_file_class_t file_class,
                                  qrnet_code_t **code_out);

/* Create code with specific options */
qrnet_result_t qrnet_create_code_with_options(qrnet_system_t *sys,
                                               const char *destination_path,
                                               const void *content,
                                               size_t content_len,
                                               qrnet_file_class_t file_class,
                                               int qr_version,
                                               time_t expires_at,
                                               qrnet_code_t **code_out);

/* Verify QR code */
qrnet_result_t qrnet_verify_code(qrnet_system_t *sys,
                                  qrnet_code_t *code,
                                  qrnet_verification_t *result);

/* Verify code against content */
qrnet_result_t qrnet_verify_code_content(qrnet_system_t *sys,
                                          qrnet_code_t *code,
                                          const void *content,
                                          size_t content_len,
                                          qrnet_verification_t *result);

/* Parse QR code data */
qrnet_result_t qrnet_parse_code(qrnet_system_t *sys,
                                 const char *qr_data,
                                 int qr_data_len,
                                 qrnet_code_t **code_out);

/* Supersede code (create new code that replaces old) */
qrnet_result_t qrnet_supersede_code(qrnet_system_t *sys,
                                     qrnet_code_t *old_code,
                                     const void *new_content,
                                     size_t new_content_len,
                                     qrnet_code_t **new_code_out);

/* Revoke code */
qrnet_result_t qrnet_revoke_code(qrnet_system_t *sys,
                                  qrnet_code_t *code,
                                  const char *reason);

/* Get code by ID */
qrnet_code_t *qrnet_get_code(qrnet_system_t *sys, uint32_t code_id);

/* Get codes for destination */
qrnet_code_t *qrnet_get_codes_for_path(qrnet_system_t *sys,
                                        const char *destination_path);

/* ==============================================================================
 * Multi-Signature Support
 * ============================================================================== */

/* Add signature from another node */
qrnet_result_t qrnet_add_signature(qrnet_system_t *sys,
                                    qrnet_code_t *code,
                                    const char *signer_dnauth_id);

/* Verify multi-signature consensus */
qrnet_result_t qrnet_verify_consensus(qrnet_system_t *sys,
                                       qrnet_code_t *code,
                                       int required_signatures,
                                       qrnet_verification_t *result);

/* ==============================================================================
 * Node Management
 * ============================================================================== */

/* Create local node */
qrnet_result_t qrnet_create_local_node(qrnet_system_t *sys,
                                        const char *dnauth_identity);

/* Add remote node */
qrnet_result_t qrnet_add_node(qrnet_system_t *sys,
                               const char *node_id,
                               const char *dnauth_identity,
                               const char *address);

/* Set node trust level */
qrnet_result_t qrnet_set_node_trust(qrnet_system_t *sys,
                                     const char *node_id,
                                     qrnet_trust_t trust_level);

/* Revoke node */
qrnet_result_t qrnet_revoke_node(qrnet_system_t *sys,
                                  const char *node_id,
                                  const char *reason);

/* Get node by ID */
qrnet_node_t *qrnet_get_node(qrnet_system_t *sys, const char *node_id);

/* Sync node with governor state */
qrnet_result_t qrnet_sync_node(qrnet_system_t *sys, const char *node_id);

/* ==============================================================================
 * Adaptive Sizing
 * ============================================================================== */

/* Calculate minimum QR version for file class */
int qrnet_min_version_for_class(qrnet_system_t *sys, qrnet_file_class_t file_class);

/* Calculate required version for data size */
int qrnet_version_for_data(int data_size);

/* Expand QR code to larger version */
qrnet_result_t qrnet_expand_code(qrnet_system_t *sys,
                                  qrnet_code_t *code,
                                  int new_version);

/* Add governor proof to code */
qrnet_result_t qrnet_add_governor_proof(qrnet_system_t *sys,
                                         qrnet_code_t *code);

/* Cache verification result in code */
qrnet_result_t qrnet_cache_verification(qrnet_system_t *sys,
                                         qrnet_code_t *code,
                                         qrnet_verification_t *verification);

/* ==============================================================================
 * Cryptographic Key Management
 * ============================================================================== */

/* Generate ECDSA keypair for a node */
qrnet_result_t qrnet_generate_keypair(qrnet_keypair_t *keypair);

/* Export public key to hex string */
qrnet_result_t qrnet_export_pubkey(qrnet_keypair_t *keypair, char *hex_out);

/* Import public key from hex string */
qrnet_result_t qrnet_import_pubkey(qrnet_keypair_t *keypair, const char *hex_in);

/* Derive keypair from DNAuth identity (deterministic) */
qrnet_result_t qrnet_derive_keypair(const char *dnauth_identity,
                                     const char *salt,
                                     qrnet_keypair_t *keypair);

/* ==============================================================================
 * Hashing and Signing
 * ============================================================================== */

/* Hash content */
qrnet_result_t qrnet_hash_content(const void *content, size_t len,
                                   int algorithm, qrnet_hash_t *hash_out);

/* Sign code with ECDSA using node's keypair */
qrnet_result_t qrnet_sign_code(qrnet_system_t *sys,
                                qrnet_code_t *code,
                                const char *dnauth_identity);

/* Verify ECDSA signature on code */
qrnet_result_t qrnet_verify_signature(qrnet_system_t *sys,
                                       qrnet_code_t *code);

/* Sign arbitrary data with node's keypair */
qrnet_result_t qrnet_sign_data(qrnet_keypair_t *keypair,
                                const void *data, size_t data_len,
                                uint8_t *sig_out, int *sig_len_out);

/* Verify signature on arbitrary data */
qrnet_result_t qrnet_verify_data(qrnet_keypair_t *keypair,
                                  const void *data, size_t data_len,
                                  const uint8_t *sig, int sig_len);

/* ==============================================================================
 * Governor Integration
 * ============================================================================== */

/* Request Governor approval for operation */
qrnet_result_t qrnet_governor_approve(qrnet_system_t *sys,
                                       qrnet_operation_t operation,
                                       const char *description);

/* Log operation to Governor */
qrnet_result_t qrnet_governor_log(qrnet_system_t *sys,
                                   qrnet_operation_t operation,
                                   qrnet_code_t *code,
                                   const char *details);

/* Get current governor state version */
uint32_t qrnet_get_governor_state_version(qrnet_system_t *sys);

/* ==============================================================================
 * Utility Functions
 * ============================================================================== */

/* Result to string */
const char *qrnet_result_string(qrnet_result_t result);

/* Trust level to string */
const char *qrnet_trust_string(qrnet_trust_t trust);

/* File class to string */
const char *qrnet_file_class_string(qrnet_file_class_t file_class);

/* Code state to string */
const char *qrnet_code_state_string(qrnet_code_state_t state);

/* Node state to string */
const char *qrnet_node_state_string(qrnet_node_state_t state);

/* Generate node ID from DNAuth identity */
qrnet_result_t qrnet_generate_node_id(const char *dnauth_identity,
                                       char *node_id_out);

/* Encode code to QR data string */
qrnet_result_t qrnet_encode_code(qrnet_code_t *code);

/* Decode QR data string to code fields */
qrnet_result_t qrnet_decode_code(const char *qr_data, int len,
                                  qrnet_code_t *code_out);

/* ==============================================================================
 * Persistence
 * ============================================================================== */

/* Save system state */
qrnet_result_t qrnet_save(qrnet_system_t *sys);

/* Load system state */
qrnet_result_t qrnet_load(qrnet_system_t *sys);

/* Export code to file */
qrnet_result_t qrnet_export_code(qrnet_code_t *code, const char *filepath);

/* Import code from file */
qrnet_result_t qrnet_import_code(qrnet_system_t *sys,
                                  const char *filepath,
                                  qrnet_code_t **code_out);

#endif /* PHANTOM_QRNET_H */
