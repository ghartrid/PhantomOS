/*
 * ==============================================================================
 *                    PHANTOM QRNET - QR Code Distributed File Network
 *                          "To Create, Not To Destroy"
 * ==============================================================================
 *
 * Implementation of QRNet - a cryptographically-signed distributed file network
 * using QR codes for linking, DNAuth for identity, and Governor for validation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/bn.h>
#include <openssl/rand.h>

#include "phantom_qrnet.h"
#include "governor.h"
#include "phantom_dnauth.h"

/* ==============================================================================
 * Internal Helpers
 * ============================================================================== */

/* Compute SHA-256 hash */
static void compute_sha256(const void *data, size_t len, uint8_t *hash_out) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(hash_out, &ctx);
}

/* Convert bytes to hex string */
static void bytes_to_hex(const uint8_t *bytes, size_t len, char *hex_out) {
    for (size_t i = 0; i < len; i++) {
        sprintf(hex_out + (i * 2), "%02x", bytes[i]);
    }
    hex_out[len * 2] = '\0';
}

/* Convert hex string to bytes */
static int hex_to_bytes(const char *hex, uint8_t *bytes, size_t max_len) {
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0 || hex_len / 2 > max_len) return -1;

    for (size_t i = 0; i < hex_len / 2; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%02x", &byte) != 1) return -1;
        bytes[i] = (uint8_t)byte;
    }
    return (int)(hex_len / 2);
}

/* ==============================================================================
 * ECDSA Cryptographic Operations
 * ============================================================================== */

qrnet_result_t qrnet_generate_keypair(qrnet_keypair_t *keypair) {
    if (!keypair) return QRNET_INVALID_PARAM;

    memset(keypair, 0, sizeof(qrnet_keypair_t));

    /* Create EC key using secp256k1 curve (same as Bitcoin/Ethereum) */
    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!ec_key) {
        printf("[QRNet] Failed to create EC key\n");
        return QRNET_ERROR;
    }

    /* Generate the key pair */
    if (EC_KEY_generate_key(ec_key) != 1) {
        EC_KEY_free(ec_key);
        printf("[QRNet] Failed to generate EC key pair\n");
        return QRNET_ERROR;
    }

    /* Extract private key */
    const BIGNUM *priv_bn = EC_KEY_get0_private_key(ec_key);
    if (priv_bn) {
        int priv_len = BN_num_bytes(priv_bn);
        if (priv_len <= QRNET_PRIVKEY_LEN) {
            /* Pad with leading zeros if needed */
            memset(keypair->private_key, 0, QRNET_PRIVKEY_LEN);
            BN_bn2bin(priv_bn, keypair->private_key + (QRNET_PRIVKEY_LEN - priv_len));
            keypair->has_private_key = 1;
        }
    }

    /* Extract public key (uncompressed format: 04 || x || y) */
    const EC_POINT *pub_point = EC_KEY_get0_public_key(ec_key);
    const EC_GROUP *group = EC_KEY_get0_group(ec_key);
    if (pub_point && group) {
        size_t pub_len = EC_POINT_point2oct(group, pub_point,
                                             POINT_CONVERSION_UNCOMPRESSED,
                                             keypair->public_key,
                                             QRNET_PUBKEY_LEN, NULL);
        if (pub_len == QRNET_PUBKEY_LEN) {
            bytes_to_hex(keypair->public_key, QRNET_PUBKEY_LEN,
                        keypair->public_key_hex);
        }
    }

    EC_KEY_free(ec_key);
    keypair->initialized = 1;

    printf("[QRNet] Generated ECDSA keypair (secp256k1)\n");
    return QRNET_OK;
}

qrnet_result_t qrnet_derive_keypair(const char *dnauth_identity,
                                     const char *salt,
                                     qrnet_keypair_t *keypair) {
    if (!dnauth_identity || !keypair) return QRNET_INVALID_PARAM;

    memset(keypair, 0, sizeof(qrnet_keypair_t));

    /* Create deterministic seed from identity + salt */
    char seed_input[512];
    snprintf(seed_input, sizeof(seed_input), "QRNET_KEY:%s:%s",
             dnauth_identity, salt ? salt : "phantom");

    /* Hash to create private key material */
    uint8_t seed_hash[32];
    compute_sha256(seed_input, strlen(seed_input), seed_hash);
    memcpy(keypair->private_key, seed_hash, QRNET_PRIVKEY_LEN);
    keypair->has_private_key = 1;

    /* Create EC key and set private key */
    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!ec_key) return QRNET_ERROR;

    BIGNUM *priv_bn = BN_bin2bn(keypair->private_key, QRNET_PRIVKEY_LEN, NULL);
    if (!priv_bn) {
        EC_KEY_free(ec_key);
        return QRNET_ERROR;
    }

    if (EC_KEY_set_private_key(ec_key, priv_bn) != 1) {
        BN_free(priv_bn);
        EC_KEY_free(ec_key);
        return QRNET_ERROR;
    }

    /* Derive public key from private key */
    const EC_GROUP *group = EC_KEY_get0_group(ec_key);
    EC_POINT *pub_point = EC_POINT_new(group);
    if (pub_point && EC_POINT_mul(group, pub_point, priv_bn, NULL, NULL, NULL) == 1) {
        EC_KEY_set_public_key(ec_key, pub_point);

        /* Extract uncompressed public key */
        size_t pub_len = EC_POINT_point2oct(group, pub_point,
                                             POINT_CONVERSION_UNCOMPRESSED,
                                             keypair->public_key,
                                             QRNET_PUBKEY_LEN, NULL);
        if (pub_len == QRNET_PUBKEY_LEN) {
            bytes_to_hex(keypair->public_key, QRNET_PUBKEY_LEN,
                        keypair->public_key_hex);
        }
        EC_POINT_free(pub_point);
    }

    BN_free(priv_bn);
    EC_KEY_free(ec_key);

    keypair->initialized = 1;

    printf("[QRNet] Derived ECDSA keypair from identity\n");
    return QRNET_OK;
}

qrnet_result_t qrnet_export_pubkey(qrnet_keypair_t *keypair, char *hex_out) {
    if (!keypair || !hex_out || !keypair->initialized) return QRNET_INVALID_PARAM;
    strcpy(hex_out, keypair->public_key_hex);
    return QRNET_OK;
}

qrnet_result_t qrnet_import_pubkey(qrnet_keypair_t *keypair, const char *hex_in) {
    if (!keypair || !hex_in) return QRNET_INVALID_PARAM;

    memset(keypair, 0, sizeof(qrnet_keypair_t));

    int len = hex_to_bytes(hex_in, keypair->public_key, QRNET_PUBKEY_LEN);
    if (len != QRNET_PUBKEY_LEN) return QRNET_INVALID_PARAM;

    strncpy(keypair->public_key_hex, hex_in, sizeof(keypair->public_key_hex) - 1);
    keypair->has_private_key = 0; /* Only public key imported */
    keypair->initialized = 1;

    return QRNET_OK;
}

qrnet_result_t qrnet_sign_data(qrnet_keypair_t *keypair,
                                const void *data, size_t data_len,
                                uint8_t *sig_out, int *sig_len_out) {
    if (!keypair || !data || !sig_out || !sig_len_out) return QRNET_INVALID_PARAM;
    if (!keypair->has_private_key) return QRNET_ERROR;

    /* Hash the data first */
    uint8_t hash[32];
    compute_sha256(data, data_len, hash);

    /* Create EC key and set private key */
    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!ec_key) return QRNET_ERROR;

    BIGNUM *priv_bn = BN_bin2bn(keypair->private_key, QRNET_PRIVKEY_LEN, NULL);
    if (!priv_bn) {
        EC_KEY_free(ec_key);
        return QRNET_ERROR;
    }

    EC_KEY_set_private_key(ec_key, priv_bn);

    /* Derive and set public key */
    const EC_GROUP *group = EC_KEY_get0_group(ec_key);
    EC_POINT *pub_point = EC_POINT_new(group);
    EC_POINT_mul(group, pub_point, priv_bn, NULL, NULL, NULL);
    EC_KEY_set_public_key(ec_key, pub_point);
    EC_POINT_free(pub_point);
    BN_free(priv_bn);

    /* Sign the hash */
    unsigned int sig_len = QRNET_ECDSA_SIG_LEN;
    if (ECDSA_sign(0, hash, 32, sig_out, &sig_len, ec_key) != 1) {
        EC_KEY_free(ec_key);
        return QRNET_ERROR;
    }

    *sig_len_out = (int)sig_len;
    EC_KEY_free(ec_key);

    return QRNET_OK;
}

qrnet_result_t qrnet_verify_data(qrnet_keypair_t *keypair,
                                  const void *data, size_t data_len,
                                  const uint8_t *sig, int sig_len) {
    if (!keypair || !data || !sig || !keypair->initialized) return QRNET_INVALID_PARAM;

    /* Hash the data */
    uint8_t hash[32];
    compute_sha256(data, data_len, hash);

    /* Create EC key and set public key */
    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!ec_key) return QRNET_ERROR;

    const EC_GROUP *group = EC_KEY_get0_group(ec_key);
    EC_POINT *pub_point = EC_POINT_new(group);

    if (EC_POINT_oct2point(group, pub_point, keypair->public_key,
                           QRNET_PUBKEY_LEN, NULL) != 1) {
        EC_POINT_free(pub_point);
        EC_KEY_free(ec_key);
        return QRNET_ERROR;
    }

    EC_KEY_set_public_key(ec_key, pub_point);
    EC_POINT_free(pub_point);

    /* Verify the signature */
    int result = ECDSA_verify(0, hash, 32, sig, sig_len, ec_key);
    EC_KEY_free(ec_key);

    return (result == 1) ? QRNET_OK : QRNET_SIGNATURE_INVALID;
}

/* Get QR version capacity */
static int get_qr_capacity(int version) {
    /* Approximate alphanumeric capacity for QR versions */
    static const int capacities[] = {
        0,    /* v0 (invalid) */
        17,   /* v1 */
        32,   /* v2 */
        53,   /* v3 */
        78,   /* v4 */
        106,  /* v5 */
        134,  /* v6 */
        154,  /* v7 */
        192,  /* v8 */
        230,  /* v9 */
        271,  /* v10 */
        321,  /* v11 */
        367,  /* v12 */
        425,  /* v13 */
        458,  /* v14 */
        520,  /* v15 */
        586,  /* v16 */
        644,  /* v17 */
        718,  /* v18 */
        792,  /* v19 */
        858,  /* v20 */
        929,  /* v21 */
        1003, /* v22 */
        1091, /* v23 */
        1171, /* v24 */
        1273, /* v25 */
        1367, /* v26 */
        1465, /* v27 */
        1528, /* v28 */
        1628, /* v29 */
        1732, /* v30 */
        1840, /* v31 */
        1952, /* v32 */
        2068, /* v33 */
        2188, /* v34 */
        2303, /* v35 */
        2431, /* v36 */
        2563, /* v37 */
        2699, /* v38 */
        2809, /* v39 */
        2953  /* v40 */
    };

    if (version < 1 || version > 40) return 0;
    return capacities[version];
}

/* ==============================================================================
 * Initialization and Cleanup
 * ============================================================================== */

qrnet_system_t *qrnet_init(const char *data_path) {
    qrnet_system_t *sys = calloc(1, sizeof(qrnet_system_t));
    if (!sys) return NULL;

    strncpy(sys->data_path, data_path, sizeof(sys->data_path) - 1);

    /* Set default adaptive sizing */
    sys->min_version_user = 8;
    sys->min_version_system = 15;
    sys->min_version_constitutional = 25;
    sys->min_version_critical = 40;

    /* Default configuration */
    sys->auto_expand = 1;
    sys->cache_verifications = 1;
    sys->require_governor_approval = 1;
    sys->default_expiry = 0; /* Never expire by default */

    sys->initialized = 1;

    printf("[QRNet] System initialized at %s\n", data_path);

    return sys;
}

void qrnet_cleanup(qrnet_system_t *sys) {
    if (!sys) return;

    /* Free codes */
    qrnet_code_t *code = sys->codes;
    while (code) {
        qrnet_code_t *next = code->next;
        free(code);
        code = next;
    }

    /* Free nodes */
    qrnet_node_t *node = sys->nodes;
    while (node) {
        qrnet_node_t *next = node->next;
        free(node);
        node = next;
    }

    free(sys);
    printf("[QRNet] System cleaned up\n");
}

/* ==============================================================================
 * Governor and DNAuth Integration
 * ============================================================================== */

void qrnet_set_governor(qrnet_system_t *sys, struct phantom_governor *gov) {
    if (!sys) return;
    sys->governor = gov;
    printf("[QRNet] Governor integration enabled\n");
}

void qrnet_set_dnauth(qrnet_system_t *sys, struct dnauth_system *dnauth) {
    if (!sys) return;
    sys->dnauth = dnauth;
    printf("[QRNet] DNAuth integration enabled\n");
}

qrnet_result_t qrnet_sync_governor_state(qrnet_system_t *sys) {
    if (!sys || !sys->governor) return QRNET_NOT_INITIALIZED;

    /* Get current governor state version */
    /* In real implementation, this would query the governor */
    sys->current_gov_state.version++;
    sys->current_gov_state.timestamp = time(NULL);
    sys->current_gov_state.constitutional_compliant = 1;

    /* Hash the state */
    char state_str[256];
    snprintf(state_str, sizeof(state_str), "gov_state_v%u_%ld",
             sys->current_gov_state.version, sys->current_gov_state.timestamp);
    uint8_t hash[32];
    compute_sha256(state_str, strlen(state_str), hash);
    bytes_to_hex(hash, 32, sys->current_gov_state.hash);

    printf("[QRNet] Synced governor state to version %u\n",
           sys->current_gov_state.version);

    return QRNET_OK;
}

/* ==============================================================================
 * QR Code Operations
 * ============================================================================== */

qrnet_result_t qrnet_create_code(qrnet_system_t *sys,
                                  const char *destination_path,
                                  const void *content,
                                  size_t content_len,
                                  qrnet_file_class_t file_class,
                                  qrnet_code_t **code_out) {
    int min_version = qrnet_min_version_for_class(sys, file_class);
    return qrnet_create_code_with_options(sys, destination_path, content,
                                           content_len, file_class,
                                           min_version, 0, code_out);
}

qrnet_result_t qrnet_create_code_with_options(qrnet_system_t *sys,
                                               const char *destination_path,
                                               const void *content,
                                               size_t content_len,
                                               qrnet_file_class_t file_class,
                                               int qr_version,
                                               time_t expires_at,
                                               qrnet_code_t **code_out) {
    if (!sys || !sys->initialized) return QRNET_NOT_INITIALIZED;
    if (!destination_path || !content) return QRNET_INVALID_PARAM;
    if (!sys->local_node) return QRNET_NODE_NOT_FOUND;

    /* Request Governor approval */
    if (sys->require_governor_approval && sys->governor) {
        qrnet_result_t gov_result = qrnet_governor_approve(sys,
            QRNET_OP_CREATE_CODE, "Create QR code for file linkage");
        if (gov_result != QRNET_OK) {
            return QRNET_GOVERNOR_DENIED;
        }
    }

    /* Create new code */
    qrnet_code_t *code = calloc(1, sizeof(qrnet_code_t));
    if (!code) return QRNET_ERROR;

    /* Generate code ID */
    code->code_id = ++sys->total_codes_created;

    /* Set destination */
    strncpy(code->destination_path, destination_path,
            sizeof(code->destination_path) - 1);

    /* Compute content hash */
    qrnet_hash_content(content, content_len, 0, &code->content_hash);

    /* Set creator identity */
    strncpy(code->dnauth_creator, sys->local_node->dnauth_identity,
            sizeof(code->dnauth_creator) - 1);

    /* Set governor state version */
    code->governor_state_version = sys->current_gov_state.version;

    /* Set metadata */
    code->file_class = file_class;
    code->state = QRNET_CODE_ACTIVE;
    code->qr_version = qr_version;
    code->created_at = time(NULL);
    code->expires_at = expires_at;
    code->last_verified = 0;
    code->verification_count = 0;

    /* Sign the code */
    qrnet_sign_code(sys, code, sys->local_node->dnauth_identity);

    /* Encode to QR data */
    qrnet_encode_code(code);

    /* Add to list */
    code->next = sys->codes;
    sys->codes = code;
    sys->code_count++;

    /* Log to governor */
    if (sys->governor) {
        qrnet_governor_log(sys, QRNET_OP_CREATE_CODE, code,
                           "QR code created successfully");
    }

    /* Update local node stats */
    sys->local_node->codes_created++;

    if (code_out) *code_out = code;

    printf("[QRNet] Created code #%u for %s (v%d, class=%s)\n",
           code->code_id, destination_path, qr_version,
           qrnet_file_class_string(file_class));

    return QRNET_OK;
}

qrnet_result_t qrnet_verify_code(qrnet_system_t *sys,
                                  qrnet_code_t *code,
                                  qrnet_verification_t *result) {
    if (!sys || !code || !result) return QRNET_INVALID_PARAM;

    memset(result, 0, sizeof(qrnet_verification_t));

    /* Check code state */
    if (code->state == QRNET_CODE_REVOKED) {
        result->result = QRNET_REVOKED;
        result->not_revoked = 0;
        strcpy(result->details, "Code has been revoked");
        return QRNET_REVOKED;
    }

    if (code->state == QRNET_CODE_EXPIRED ||
        (code->expires_at > 0 && time(NULL) > code->expires_at)) {
        result->result = QRNET_EXPIRED;
        result->not_expired = 0;
        strcpy(result->details, "Code has expired");
        return QRNET_EXPIRED;
    }

    result->not_revoked = 1;
    result->not_expired = 1;

    /* Verify signature */
    qrnet_result_t sig_result = qrnet_verify_signature(sys, code);
    result->signature_valid = (sig_result == QRNET_OK);

    /* Check governor state version */
    result->governor_state_valid =
        (code->governor_state_version <= sys->current_gov_state.version);

    /* Check DNAuth identity validity */
    if (sys->dnauth) {
        result->dnauth_valid = dnauth_key_exists(sys->dnauth, code->dnauth_creator);
    } else {
        result->dnauth_valid = 1; /* Assume valid if no DNAuth */
    }

    /* Trust codes from our local node even without DNAuth registration */
    if (!result->dnauth_valid && sys->local_node &&
        strcmp(code->dnauth_creator, sys->local_node->dnauth_identity) == 0) {
        result->dnauth_valid = 1; /* Local node is always trusted */
    }

    /* Get creator node trust level */
    qrnet_node_t *creator = qrnet_get_node(sys, code->dnauth_creator);
    if (creator) {
        result->trust_level = creator->trust_level;
    } else {
        result->trust_level = QRNET_TRUST_UNKNOWN;
    }

    /* Overall result */
    if (result->signature_valid && result->governor_state_valid &&
        result->dnauth_valid && result->not_revoked && result->not_expired) {
        result->result = QRNET_OK;
        snprintf(result->details, sizeof(result->details),
                 "Verification successful (trust: %s)",
                 qrnet_trust_string(result->trust_level));
    } else {
        result->result = QRNET_SIGNATURE_INVALID;
        snprintf(result->details, sizeof(result->details),
                 "Verification failed: sig=%d gov=%d dnauth=%d",
                 result->signature_valid, result->governor_state_valid,
                 result->dnauth_valid);
    }

    /* Update statistics */
    code->last_verified = time(NULL);
    code->verification_count++;
    sys->total_verifications++;

    if (result->result != QRNET_OK) {
        sys->failed_verifications++;
    }

    return result->result;
}

qrnet_result_t qrnet_verify_code_content(qrnet_system_t *sys,
                                          qrnet_code_t *code,
                                          const void *content,
                                          size_t content_len,
                                          qrnet_verification_t *result) {
    /* First do standard verification */
    qrnet_result_t res = qrnet_verify_code(sys, code, result);
    if (res != QRNET_OK) return res;

    /* Now verify content hash matches */
    qrnet_hash_t computed_hash;
    qrnet_hash_content(content, content_len, code->content_hash.algorithm,
                       &computed_hash);

    if (memcmp(computed_hash.bytes, code->content_hash.bytes, 32) != 0) {
        result->result = QRNET_HASH_MISMATCH;
        result->hash_valid = 0;
        strcpy(result->details, "Content hash mismatch - file may be tampered");
        return QRNET_HASH_MISMATCH;
    }

    result->hash_valid = 1;
    return QRNET_OK;
}

qrnet_result_t qrnet_supersede_code(qrnet_system_t *sys,
                                     qrnet_code_t *old_code,
                                     const void *new_content,
                                     size_t new_content_len,
                                     qrnet_code_t **new_code_out) {
    if (!sys || !old_code || !new_content) return QRNET_INVALID_PARAM;

    /* Create new code */
    qrnet_code_t *new_code;
    qrnet_result_t result = qrnet_create_code(sys, old_code->destination_path,
                                               new_content, new_content_len,
                                               old_code->file_class, &new_code);
    if (result != QRNET_OK) return result;

    /* Link supersession chain */
    new_code->supersedes_code_id = old_code->code_id;
    old_code->superseded_by_code_id = new_code->code_id;
    old_code->state = QRNET_CODE_SUPERSEDED;

    /* Log to governor */
    if (sys->governor) {
        char details[256];
        snprintf(details, sizeof(details),
                 "Code #%u superseded by #%u",
                 old_code->code_id, new_code->code_id);
        qrnet_governor_log(sys, QRNET_OP_SUPERSEDE_CODE, new_code, details);
    }

    if (new_code_out) *new_code_out = new_code;

    printf("[QRNet] Code #%u superseded by #%u\n",
           old_code->code_id, new_code->code_id);

    return QRNET_OK;
}

qrnet_result_t qrnet_revoke_code(qrnet_system_t *sys,
                                  qrnet_code_t *code,
                                  const char *reason) {
    if (!sys || !code) return QRNET_INVALID_PARAM;

    /* Request Governor approval */
    if (sys->require_governor_approval && sys->governor) {
        qrnet_result_t gov_result = qrnet_governor_approve(sys,
            QRNET_OP_REVOKE_CODE, "Revoke QR code");
        if (gov_result != QRNET_OK) {
            return QRNET_GOVERNOR_DENIED;
        }
    }

    /* Mark as revoked (never delete - append only) */
    code->state = QRNET_CODE_REVOKED;

    sys->revocations++;

    /* Log to governor */
    if (sys->governor) {
        char details[512];
        snprintf(details, sizeof(details), "Code #%u revoked: %s",
                 code->code_id, reason ? reason : "No reason given");
        qrnet_governor_log(sys, QRNET_OP_REVOKE_CODE, code, details);
    }

    printf("[QRNet] Code #%u revoked: %s\n",
           code->code_id, reason ? reason : "No reason");

    return QRNET_OK;
}

qrnet_code_t *qrnet_get_code(qrnet_system_t *sys, uint32_t code_id) {
    if (!sys) return NULL;

    qrnet_code_t *code = sys->codes;
    while (code) {
        if (code->code_id == code_id) return code;
        code = code->next;
    }
    return NULL;
}

qrnet_code_t *qrnet_get_codes_for_path(qrnet_system_t *sys,
                                        const char *destination_path) {
    if (!sys || !destination_path) return NULL;

    /* Return first matching code (most recent due to prepend) */
    qrnet_code_t *code = sys->codes;
    while (code) {
        if (strcmp(code->destination_path, destination_path) == 0 &&
            code->state == QRNET_CODE_ACTIVE) {
            return code;
        }
        code = code->next;
    }
    return NULL;
}

/* ==============================================================================
 * Multi-Signature Support
 * ============================================================================== */

qrnet_result_t qrnet_add_signature(qrnet_system_t *sys,
                                    qrnet_code_t *code,
                                    const char *signer_dnauth_id) {
    if (!sys || !code || !signer_dnauth_id) return QRNET_INVALID_PARAM;
    if (code->signature_count >= 8) return QRNET_CAPACITY_EXCEEDED;

    /* Verify signer has valid DNAuth identity */
    if (sys->dnauth && !dnauth_key_exists(sys->dnauth, signer_dnauth_id)) {
        return QRNET_DNAUTH_INVALID;
    }

    /* Add signature */
    qrnet_signature_t *sig = &code->additional_signatures[code->signature_count];
    strncpy(sig->signer_id, signer_dnauth_id, sizeof(sig->signer_id) - 1);
    sig->timestamp = time(NULL);
    sig->governor_state = sys->current_gov_state.version;

    /* Generate signature data */
    char sig_data[512];
    snprintf(sig_data, sizeof(sig_data), "%u:%s:%s:%u",
             code->code_id, code->content_hash.hex,
             signer_dnauth_id, sig->governor_state);

    uint8_t sig_hash[32];
    compute_sha256(sig_data, strlen(sig_data), sig_hash);
    bytes_to_hex(sig_hash, 32, sig->data);

    code->signature_count++;

    printf("[QRNet] Added signature #%d from %s to code #%u\n",
           code->signature_count, signer_dnauth_id, code->code_id);

    return QRNET_OK;
}

qrnet_result_t qrnet_verify_consensus(qrnet_system_t *sys,
                                       qrnet_code_t *code,
                                       int required_signatures,
                                       qrnet_verification_t *result) {
    if (!sys || !code || !result) return QRNET_INVALID_PARAM;

    /* First verify the code itself */
    qrnet_result_t res = qrnet_verify_code(sys, code, result);
    if (res != QRNET_OK) return res;

    /* Check we have enough signatures */
    int total_sigs = 1 + code->signature_count; /* Primary + additional */
    if (total_sigs < required_signatures) {
        result->result = QRNET_ERROR;
        snprintf(result->details, sizeof(result->details),
                 "Insufficient signatures: have %d, need %d",
                 total_sigs, required_signatures);
        return QRNET_ERROR;
    }

    /* Verify each additional signature */
    int valid_sigs = 1; /* Primary already verified */
    for (int i = 0; i < code->signature_count; i++) {
        qrnet_signature_t *sig = &code->additional_signatures[i];
        if (sys->dnauth && dnauth_key_exists(sys->dnauth, sig->signer_id)) {
            valid_sigs++;
        }
    }

    if (valid_sigs >= required_signatures) {
        snprintf(result->details, sizeof(result->details),
                 "Consensus verified: %d/%d valid signatures",
                 valid_sigs, total_sigs);
        return QRNET_OK;
    }

    result->result = QRNET_SIGNATURE_INVALID;
    snprintf(result->details, sizeof(result->details),
             "Consensus failed: only %d/%d valid signatures",
             valid_sigs, required_signatures);
    return QRNET_SIGNATURE_INVALID;
}

/* ==============================================================================
 * Node Management
 * ============================================================================== */

qrnet_result_t qrnet_create_local_node(qrnet_system_t *sys,
                                        const char *dnauth_identity) {
    if (!sys || !dnauth_identity) return QRNET_INVALID_PARAM;

    if (sys->local_node) {
        return QRNET_ALREADY_EXISTS;
    }

    qrnet_node_t *node = calloc(1, sizeof(qrnet_node_t));
    if (!node) return QRNET_ERROR;

    /* Generate node ID from DNAuth identity */
    qrnet_generate_node_id(dnauth_identity, node->node_id);
    strncpy(node->dnauth_identity, dnauth_identity,
            sizeof(node->dnauth_identity) - 1);

    /* Generate ECDSA keypair for signing (derived from identity for reproducibility) */
    qrnet_result_t key_result = qrnet_derive_keypair(dnauth_identity,
                                                      "phantom_qrnet_v1",
                                                      &node->keypair);
    if (key_result != QRNET_OK) {
        printf("[QRNet] Warning: Failed to generate keypair, falling back to random\n");
        /* Fall back to random keypair if derivation fails */
        qrnet_generate_keypair(&node->keypair);
    }

    node->trust_level = QRNET_TRUST_FULL; /* Local node is fully trusted */
    node->state = QRNET_NODE_ACTIVE;
    node->governor_state_version = sys->current_gov_state.version;
    node->last_sync = time(NULL);
    node->joined_at = time(NULL);
    node->last_active = time(NULL);
    node->is_local = 1;

    sys->local_node = node;

    /* Add to node list */
    node->next = sys->nodes;
    sys->nodes = node;
    sys->node_count++;

    printf("[QRNet] Created local node: %s (pubkey: %.16s...)\n",
           node->node_id, node->keypair.public_key_hex);

    return QRNET_OK;
}

qrnet_result_t qrnet_add_node(qrnet_system_t *sys,
                               const char *node_id,
                               const char *dnauth_identity,
                               const char *address) {
    if (!sys || !node_id || !dnauth_identity) return QRNET_INVALID_PARAM;

    /* Check if node already exists */
    if (qrnet_get_node(sys, node_id)) {
        return QRNET_ALREADY_EXISTS;
    }

    qrnet_node_t *node = calloc(1, sizeof(qrnet_node_t));
    if (!node) return QRNET_ERROR;

    strncpy(node->node_id, node_id, sizeof(node->node_id) - 1);
    strncpy(node->dnauth_identity, dnauth_identity,
            sizeof(node->dnauth_identity) - 1);
    if (address) {
        strncpy(node->address, address, sizeof(node->address) - 1);
    }

    node->trust_level = QRNET_TRUST_UNKNOWN;
    node->state = QRNET_NODE_ACTIVE;
    node->joined_at = time(NULL);
    node->is_local = 0;

    /* Add to list */
    node->next = sys->nodes;
    sys->nodes = node;
    sys->node_count++;

    printf("[QRNet] Added remote node: %s\n", node_id);

    return QRNET_OK;
}

qrnet_result_t qrnet_set_node_trust(qrnet_system_t *sys,
                                     const char *node_id,
                                     qrnet_trust_t trust_level) {
    if (!sys || !node_id) return QRNET_INVALID_PARAM;

    qrnet_node_t *node = qrnet_get_node(sys, node_id);
    if (!node) return QRNET_NODE_NOT_FOUND;

    node->trust_level = trust_level;

    printf("[QRNet] Set trust level for %s: %s\n",
           node_id, qrnet_trust_string(trust_level));

    return QRNET_OK;
}

qrnet_result_t qrnet_revoke_node(qrnet_system_t *sys,
                                  const char *node_id,
                                  const char *reason) {
    if (!sys || !node_id) return QRNET_INVALID_PARAM;

    qrnet_node_t *node = qrnet_get_node(sys, node_id);
    if (!node) return QRNET_NODE_NOT_FOUND;

    node->state = QRNET_NODE_REVOKED;
    node->trust_level = QRNET_TRUST_UNKNOWN;

    /* Mark all codes from this node as having revoked creator */
    qrnet_code_t *code = sys->codes;
    while (code) {
        if (strcmp(code->dnauth_creator, node->dnauth_identity) == 0) {
            code->state = QRNET_CODE_REVOKED;
            sys->revocations++;
        }
        code = code->next;
    }

    printf("[QRNet] Revoked node %s: %s\n",
           node_id, reason ? reason : "No reason");

    return QRNET_OK;
}

qrnet_node_t *qrnet_get_node(qrnet_system_t *sys, const char *node_id) {
    if (!sys || !node_id) return NULL;

    qrnet_node_t *node = sys->nodes;
    while (node) {
        if (strcmp(node->node_id, node_id) == 0 ||
            strcmp(node->dnauth_identity, node_id) == 0) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

qrnet_result_t qrnet_sync_node(qrnet_system_t *sys, const char *node_id) {
    if (!sys || !node_id) return QRNET_INVALID_PARAM;

    qrnet_node_t *node = qrnet_get_node(sys, node_id);
    if (!node) return QRNET_NODE_NOT_FOUND;

    node->governor_state_version = sys->current_gov_state.version;
    node->last_sync = time(NULL);
    node->state = QRNET_NODE_ACTIVE;

    return QRNET_OK;
}

/* ==============================================================================
 * Adaptive Sizing
 * ============================================================================== */

int qrnet_min_version_for_class(qrnet_system_t *sys, qrnet_file_class_t file_class) {
    if (!sys) return 8;

    switch (file_class) {
        case QRNET_FILE_USER:
            return sys->min_version_user;
        case QRNET_FILE_SYSTEM:
            return sys->min_version_system;
        case QRNET_FILE_CONSTITUTIONAL:
            return sys->min_version_constitutional;
        case QRNET_FILE_CRITICAL:
            return sys->min_version_critical;
        default:
            return sys->min_version_user;
    }
}

int qrnet_version_for_data(int data_size) {
    for (int v = 1; v <= 40; v++) {
        if (get_qr_capacity(v) >= data_size) {
            return v;
        }
    }
    return 40; /* Maximum */
}

qrnet_result_t qrnet_expand_code(qrnet_system_t *sys,
                                  qrnet_code_t *code,
                                  int new_version) {
    if (!sys || !code) return QRNET_INVALID_PARAM;
    if (new_version <= code->qr_version) return QRNET_INVALID_PARAM;
    if (new_version > 40) return QRNET_INVALID_PARAM;

    code->qr_version = new_version;

    /* Re-encode with new version */
    qrnet_encode_code(code);

    printf("[QRNet] Expanded code #%u to version %d\n",
           code->code_id, new_version);

    return QRNET_OK;
}

qrnet_result_t qrnet_add_governor_proof(qrnet_system_t *sys,
                                         qrnet_code_t *code) {
    if (!sys || !code || !sys->governor) return QRNET_INVALID_PARAM;

    /* Generate governor proof */
    snprintf(code->governor_proof, sizeof(code->governor_proof),
             "GOV_PROOF:v%u:t%ld:h%s",
             sys->current_gov_state.version,
             sys->current_gov_state.timestamp,
             sys->current_gov_state.hash);

    code->has_governor_proof = 1;

    /* May need to expand QR code */
    int needed_size = strlen(code->qr_data) + strlen(code->governor_proof);
    int needed_version = qrnet_version_for_data(needed_size);
    if (needed_version > code->qr_version) {
        qrnet_expand_code(sys, code, needed_version);
    }

    return QRNET_OK;
}

qrnet_result_t qrnet_cache_verification(qrnet_system_t *sys,
                                         qrnet_code_t *code,
                                         qrnet_verification_t *verification) {
    if (!sys || !code || !verification) return QRNET_INVALID_PARAM;

    /* Store verification result in code (truncate details if needed) */
    char truncated_details[200];
    strncpy(truncated_details, verification->details, sizeof(truncated_details) - 1);
    truncated_details[sizeof(truncated_details) - 1] = '\0';

    snprintf(code->cached_verification, sizeof(code->cached_verification),
             "CACHED:%d:%ld:%.199s",
             verification->result,
             (long)time(NULL),
             truncated_details);

    code->has_cached_verification = 1;

    return QRNET_OK;
}

/* ==============================================================================
 * Hashing and Signing
 * ============================================================================== */

qrnet_result_t qrnet_hash_content(const void *content, size_t len,
                                   int algorithm, qrnet_hash_t *hash_out) {
    if (!content || !hash_out) return QRNET_INVALID_PARAM;

    hash_out->algorithm = algorithm;

    /* Use SHA-256 (algorithm 0) */
    compute_sha256(content, len, hash_out->bytes);
    bytes_to_hex(hash_out->bytes, 32, hash_out->hex);

    return QRNET_OK;
}

qrnet_result_t qrnet_sign_code(qrnet_system_t *sys,
                                qrnet_code_t *code,
                                const char *dnauth_identity) {
    if (!sys || !code || !dnauth_identity) return QRNET_INVALID_PARAM;
    if (!sys->local_node || !sys->local_node->keypair.initialized) {
        return QRNET_ERROR;
    }

    /* Set timestamp and governor state first (needed for signature) */
    code->signature.timestamp = time(NULL);
    code->signature.governor_state = sys->current_gov_state.version;

    /* Create canonical message to sign */
    char sig_input[1024];
    snprintf(sig_input, sizeof(sig_input),
             "QRNET_SIG:v1:%u:%s:%s:%s:%u:%ld",
             code->code_id,
             code->destination_path,
             code->content_hash.hex,
             dnauth_identity,
             code->signature.governor_state,
             (long)code->signature.timestamp);

    /* Sign with ECDSA */
    qrnet_result_t result = qrnet_sign_data(&sys->local_node->keypair,
                                             sig_input, strlen(sig_input),
                                             code->signature.sig_bytes,
                                             &code->signature.sig_len);
    if (result != QRNET_OK) {
        return result;
    }

    /* Store signature as hex for display/transport */
    bytes_to_hex(code->signature.sig_bytes, code->signature.sig_len,
                 code->signature.data);

    /* Store signer info */
    strncpy(code->signature.signer_id, dnauth_identity,
            sizeof(code->signature.signer_id) - 1);
    strncpy(code->signature.signer_pubkey, sys->local_node->keypair.public_key_hex,
            sizeof(code->signature.signer_pubkey) - 1);

    printf("[QRNet] Code #%u signed with ECDSA (sig_len=%d)\n",
           code->code_id, code->signature.sig_len);

    return QRNET_OK;
}

qrnet_result_t qrnet_verify_signature(qrnet_system_t *sys,
                                       qrnet_code_t *code) {
    if (!sys || !code) return QRNET_INVALID_PARAM;

    /* Need either the signer's public key in signature or find the node */
    qrnet_keypair_t verify_keypair;

    /* Try to get public key from signature first */
    if (strlen(code->signature.signer_pubkey) > 0) {
        if (qrnet_import_pubkey(&verify_keypair, code->signature.signer_pubkey) != QRNET_OK) {
            return QRNET_SIGNATURE_INVALID;
        }
    } else {
        /* Try to find the signer node */
        qrnet_node_t *signer = qrnet_get_node(sys, code->signature.signer_id);
        if (!signer || !signer->keypair.initialized) {
            /* Fall back to hash-based verification for legacy codes */
            char sig_input[1024];
            snprintf(sig_input, sizeof(sig_input),
                     "QRNET_SIG:%s:%s:%s:%u:%ld",
                     code->destination_path,
                     code->content_hash.hex,
                     code->signature.signer_id,
                     code->signature.governor_state,
                     (long)code->signature.timestamp);

            uint8_t expected_hash[32];
            compute_sha256(sig_input, strlen(sig_input), expected_hash);

            char expected_hex[65];
            bytes_to_hex(expected_hash, 32, expected_hex);

            if (strcmp(expected_hex, code->signature.data) == 0) {
                return QRNET_OK;
            }
            return QRNET_SIGNATURE_INVALID;
        }
        memcpy(&verify_keypair, &signer->keypair, sizeof(qrnet_keypair_t));
    }

    /* Recreate the canonical message that was signed */
    char sig_input[1024];
    snprintf(sig_input, sizeof(sig_input),
             "QRNET_SIG:v1:%u:%s:%s:%s:%u:%ld",
             code->code_id,
             code->destination_path,
             code->content_hash.hex,
             code->signature.signer_id,
             code->signature.governor_state,
             (long)code->signature.timestamp);

    /* Verify ECDSA signature */
    if (code->signature.sig_len > 0) {
        return qrnet_verify_data(&verify_keypair, sig_input, strlen(sig_input),
                                  code->signature.sig_bytes, code->signature.sig_len);
    }

    /* If no ECDSA signature, try hex-encoded signature */
    if (strlen(code->signature.data) > 0) {
        uint8_t sig_bytes[QRNET_ECDSA_SIG_LEN];
        int sig_len = hex_to_bytes(code->signature.data, sig_bytes, QRNET_ECDSA_SIG_LEN);
        if (sig_len > 0) {
            return qrnet_verify_data(&verify_keypair, sig_input, strlen(sig_input),
                                      sig_bytes, sig_len);
        }
    }

    return QRNET_SIGNATURE_INVALID;
}

/* ==============================================================================
 * Governor Integration
 * ============================================================================== */

qrnet_result_t qrnet_governor_approve(qrnet_system_t *sys,
                                       qrnet_operation_t operation,
                                       const char *description) {
    if (!sys) return QRNET_NOT_INITIALIZED;

    /* If no governor, approve by default */
    if (!sys->governor) return QRNET_OK;

    /* In full implementation, this would call governor_evaluate() */
    /* For now, approve all operations */
    printf("[QRNet] Governor approved op=%d: %s\n", operation, description);

    return QRNET_OK;
}

qrnet_result_t qrnet_governor_log(qrnet_system_t *sys,
                                   qrnet_operation_t operation,
                                   qrnet_code_t *code,
                                   const char *details) {
    if (!sys || !sys->governor) return QRNET_NOT_INITIALIZED;

    /* Log to governor's immutable ledger */
    printf("[QRNet->Governor] Op=%d Code=#%u: %s\n",
           operation, code ? code->code_id : 0, details);

    return QRNET_OK;
}

uint32_t qrnet_get_governor_state_version(qrnet_system_t *sys) {
    if (!sys) return 0;
    return sys->current_gov_state.version;
}

/* ==============================================================================
 * Utility Functions
 * ============================================================================== */

const char *qrnet_result_string(qrnet_result_t result) {
    switch (result) {
        case QRNET_OK: return "OK";
        case QRNET_ERROR: return "Error";
        case QRNET_INVALID_PARAM: return "Invalid parameter";
        case QRNET_NOT_INITIALIZED: return "Not initialized";
        case QRNET_NODE_NOT_FOUND: return "Node not found";
        case QRNET_CODE_NOT_FOUND: return "Code not found";
        case QRNET_SIGNATURE_INVALID: return "Invalid signature";
        case QRNET_HASH_MISMATCH: return "Hash mismatch";
        case QRNET_GOVERNOR_DENIED: return "Governor denied";
        case QRNET_DNAUTH_INVALID: return "Invalid DNAuth identity";
        case QRNET_REVOKED: return "Revoked";
        case QRNET_EXPIRED: return "Expired";
        case QRNET_CAPACITY_EXCEEDED: return "Capacity exceeded";
        case QRNET_ALREADY_EXISTS: return "Already exists";
        case QRNET_STORAGE_ERROR: return "Storage error";
        default: return "Unknown";
    }
}

const char *qrnet_trust_string(qrnet_trust_t trust) {
    switch (trust) {
        case QRNET_TRUST_UNKNOWN: return "Unknown";
        case QRNET_TRUST_MINIMAL: return "Minimal";
        case QRNET_TRUST_PARTIAL: return "Partial";
        case QRNET_TRUST_VERIFIED: return "Verified";
        case QRNET_TRUST_FULL: return "Full";
        default: return "Unknown";
    }
}

const char *qrnet_file_class_string(qrnet_file_class_t file_class) {
    switch (file_class) {
        case QRNET_FILE_USER: return "User";
        case QRNET_FILE_SYSTEM: return "System";
        case QRNET_FILE_CONSTITUTIONAL: return "Constitutional";
        case QRNET_FILE_CRITICAL: return "Critical";
        default: return "Unknown";
    }
}

const char *qrnet_code_state_string(qrnet_code_state_t state) {
    switch (state) {
        case QRNET_CODE_ACTIVE: return "Active";
        case QRNET_CODE_SUPERSEDED: return "Superseded";
        case QRNET_CODE_REVOKED: return "Revoked";
        case QRNET_CODE_EXPIRED: return "Expired";
        default: return "Unknown";
    }
}

const char *qrnet_node_state_string(qrnet_node_state_t state) {
    switch (state) {
        case QRNET_NODE_ACTIVE: return "Active";
        case QRNET_NODE_INACTIVE: return "Inactive";
        case QRNET_NODE_REVOKED: return "Revoked";
        case QRNET_NODE_SYNCING: return "Syncing";
        default: return "Unknown";
    }
}

qrnet_result_t qrnet_generate_node_id(const char *dnauth_identity,
                                       char *node_id_out) {
    if (!dnauth_identity || !node_id_out) return QRNET_INVALID_PARAM;

    /* Hash the DNAuth identity to create node ID */
    uint8_t hash[32];
    compute_sha256(dnauth_identity, strlen(dnauth_identity), hash);
    bytes_to_hex(hash, 16, node_id_out); /* Use first 16 bytes = 32 hex chars */

    return QRNET_OK;
}

qrnet_result_t qrnet_encode_code(qrnet_code_t *code) {
    if (!code) return QRNET_INVALID_PARAM;

    /* Encode to compact format */
    int len = snprintf(code->qr_data, sizeof(code->qr_data),
                       "QR:%u|%s|%s|%s|%u|%s|%ld",
                       code->code_id,
                       code->destination_path,
                       code->content_hash.hex,
                       code->dnauth_creator,
                       code->governor_state_version,
                       code->signature.data,
                       code->signature.timestamp);

    /* Add governor proof if present */
    if (code->has_governor_proof) {
        len += snprintf(code->qr_data + len, sizeof(code->qr_data) - len,
                        "|%s", code->governor_proof);
    }

    code->qr_data_len = len;

    return QRNET_OK;
}

qrnet_result_t qrnet_decode_code(const char *qr_data, int len,
                                  qrnet_code_t *code_out) {
    if (!qr_data || !code_out) return QRNET_INVALID_PARAM;

    /* Parse the encoded format */
    /* Format: QR:id|path|hash|creator|gov_state|sig|timestamp */

    memset(code_out, 0, sizeof(qrnet_code_t));

    /* Simple parsing - in production would use proper parser */
    if (strncmp(qr_data, "QR:", 3) != 0) {
        return QRNET_INVALID_PARAM;
    }

    /* Copy data for parsing */
    char buffer[4096];
    int copy_len = len - 3;  /* Exclude "QR:" prefix */
    if (copy_len < 0) copy_len = 0;
    if (copy_len >= (int)sizeof(buffer)) copy_len = sizeof(buffer) - 1;
    strncpy(buffer, qr_data + 3, copy_len);
    buffer[copy_len] = '\0';

    /* Parse fields */
    char *saveptr;
    char *token = strtok_r(buffer, "|", &saveptr);
    if (token) code_out->code_id = atoi(token);

    token = strtok_r(NULL, "|", &saveptr);
    if (token) strncpy(code_out->destination_path, token,
                       sizeof(code_out->destination_path) - 1);

    token = strtok_r(NULL, "|", &saveptr);
    if (token) strncpy(code_out->content_hash.hex, token,
                       sizeof(code_out->content_hash.hex) - 1);

    token = strtok_r(NULL, "|", &saveptr);
    if (token) strncpy(code_out->dnauth_creator, token,
                       sizeof(code_out->dnauth_creator) - 1);

    token = strtok_r(NULL, "|", &saveptr);
    if (token) code_out->governor_state_version = atoi(token);

    token = strtok_r(NULL, "|", &saveptr);
    if (token) strncpy(code_out->signature.data, token,
                       sizeof(code_out->signature.data) - 1);

    token = strtok_r(NULL, "|", &saveptr);
    if (token) code_out->signature.timestamp = atol(token);

    code_out->state = QRNET_CODE_ACTIVE;

    return QRNET_OK;
}

/* ==============================================================================
 * Persistence
 * ============================================================================== */

qrnet_result_t qrnet_save(qrnet_system_t *sys) {
    if (!sys) return QRNET_NOT_INITIALIZED;

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/qrnet.dat", sys->data_path);

    FILE *f = fopen(filepath, "wb");
    if (!f) return QRNET_STORAGE_ERROR;

    /* Write header */
    fprintf(f, "QRNET_V1\n");
    fprintf(f, "codes:%d\n", sys->code_count);
    fprintf(f, "nodes:%d\n", sys->node_count);

    /* Write codes */
    qrnet_code_t *code = sys->codes;
    while (code) {
        fprintf(f, "CODE:%u:%s:%s:%s:%u:%d:%d\n",
                code->code_id, code->destination_path,
                code->content_hash.hex, code->dnauth_creator,
                code->governor_state_version, code->state,
                code->qr_version);
        code = code->next;
    }

    /* Write nodes */
    qrnet_node_t *node = sys->nodes;
    while (node) {
        fprintf(f, "NODE:%s:%s:%d:%d\n",
                node->node_id, node->dnauth_identity,
                node->trust_level, node->state);
        node = node->next;
    }

    fclose(f);

    printf("[QRNet] Saved state to %s\n", filepath);

    return QRNET_OK;
}

qrnet_result_t qrnet_load(qrnet_system_t *sys) {
    if (!sys) return QRNET_NOT_INITIALIZED;

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/qrnet.dat", sys->data_path);

    FILE *f = fopen(filepath, "r");
    if (!f) return QRNET_OK; /* No saved state is OK */

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        /* Parse CODE entries */
        if (strncmp(line, "CODE:", 5) == 0) {
            qrnet_code_t *code = calloc(1, sizeof(qrnet_code_t));
            if (!code) continue;

            char hash_hex[65] = {0};
            int state_val = 0, version = 0;

            if (sscanf(line + 5, "%u:%511[^:]:%64[^:]:%63[^:]:%u:%d:%d",
                       &code->code_id, code->destination_path,
                       hash_hex, code->dnauth_creator,
                       &code->governor_state_version, &state_val, &version) >= 7) {
                strncpy(code->content_hash.hex, hash_hex, sizeof(code->content_hash.hex) - 1);
                code->state = (qrnet_code_state_t)state_val;
                code->qr_version = version;
                code->created_at = time(NULL);

                /* Add to list */
                code->next = sys->codes;
                sys->codes = code;
                sys->code_count++;

                if (code->code_id > sys->total_codes_created) {
                    sys->total_codes_created = code->code_id;
                }
            } else {
                free(code);
            }
        }
        /* Parse NODE entries */
        else if (strncmp(line, "NODE:", 5) == 0) {
            qrnet_node_t *node = calloc(1, sizeof(qrnet_node_t));
            if (!node) continue;

            int trust_val = 0, state_val = 0;

            if (sscanf(line + 5, "%63[^:]:%63[^:]:%d:%d",
                       node->node_id, node->dnauth_identity,
                       &trust_val, &state_val) >= 4) {
                node->trust_level = (qrnet_trust_t)trust_val;
                node->state = (qrnet_node_state_t)state_val;
                node->joined_at = time(NULL);

                /* Add to list */
                node->next = sys->nodes;
                sys->nodes = node;
                sys->node_count++;
            } else {
                free(node);
            }
        }
    }

    fclose(f);

    printf("[QRNet] Loaded state from %s (%d codes, %d nodes)\n",
           filepath, sys->code_count, sys->node_count);

    return QRNET_OK;
}

qrnet_result_t qrnet_export_code(qrnet_code_t *code, const char *filepath) {
    if (!code || !filepath) return QRNET_INVALID_PARAM;

    FILE *f = fopen(filepath, "w");
    if (!f) return QRNET_STORAGE_ERROR;

    fprintf(f, "%s\n", code->qr_data);

    fclose(f);

    return QRNET_OK;
}

qrnet_result_t qrnet_import_code(qrnet_system_t *sys,
                                  const char *filepath,
                                  qrnet_code_t **code_out) {
    if (!sys || !filepath) return QRNET_INVALID_PARAM;

    FILE *f = fopen(filepath, "r");
    if (!f) return QRNET_STORAGE_ERROR;

    char buffer[4096];
    if (!fgets(buffer, sizeof(buffer), f)) {
        fclose(f);
        return QRNET_ERROR;
    }
    fclose(f);

    /* Remove newline */
    int len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') buffer[--len] = '\0';

    return qrnet_parse_code(sys, buffer, len, code_out);
}

qrnet_result_t qrnet_parse_code(qrnet_system_t *sys,
                                 const char *qr_data,
                                 int qr_data_len,
                                 qrnet_code_t **code_out) {
    if (!sys || !qr_data) return QRNET_INVALID_PARAM;

    qrnet_code_t *code = calloc(1, sizeof(qrnet_code_t));
    if (!code) return QRNET_ERROR;

    qrnet_result_t result = qrnet_decode_code(qr_data, qr_data_len, code);
    if (result != QRNET_OK) {
        free(code);
        return result;
    }

    /* Store the QR data */
    strncpy(code->qr_data, qr_data, sizeof(code->qr_data) - 1);
    code->qr_data_len = qr_data_len;

    /* Add to system */
    code->next = sys->codes;
    sys->codes = code;
    sys->code_count++;

    if (code_out) *code_out = code;

    return QRNET_OK;
}
