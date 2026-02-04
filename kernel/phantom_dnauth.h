/*
 * ==============================================================================
 *                              DNAuth
 *                 DNA-Based Authentication for PhantomOS
 *                      "Your Code is Your Key"
 * ==============================================================================
 *
 * DNAuth uses DNA sequences as cryptographic keys for authentication.
 * This biologically-inspired system provides:
 *
 * - DNA sequence passwords (ATGC nucleotide strings)
 * - Codon-based key derivation
 * - Mutation-tolerant matching (configurable strictness)
 * - Sequence complexity requirements
 * - GeoFS-backed key storage (immutable audit trail)
 *
 * Philosophy: Just as DNA encodes life, your unique sequence encodes access.
 * The system respects biological variation while maintaining security.
 */

#ifndef PHANTOM_DNAUTH_H
#define PHANTOM_DNAUTH_H

#include <stdint.h>
#include <time.h>

/* ==============================================================================
 * Constants
 * ============================================================================== */

#define DNAUTH_MIN_SEQUENCE_LEN     12      /* Minimum 12 nucleotides (4 codons) */
#define DNAUTH_MAX_SEQUENCE_LEN     4096    /* Maximum sequence length */
#define DNAUTH_HASH_LEN             64      /* SHA-256 hex string length */
#define DNAUTH_SALT_LEN             32      /* Salt length in bytes */
#define DNAUTH_MAX_USERS            1024    /* Maximum registered users */
#define DNAUTH_MAX_MUTATIONS        3       /* Default max allowed mutations */
#define DNAUTH_CODON_TABLE_SIZE     64      /* 4^3 possible codons */
#define DNAUTH_MAX_FAILED_ATTEMPTS  5       /* Lockout threshold */
#define DNAUTH_LOCKOUT_SECS         900     /* 15 minute base lockout */
#define DNAUTH_LOCKOUT_MULTIPLIER   2       /* Exponential backoff multiplier */
#define DNAUTH_MAX_LOCKOUT_SECS     86400   /* Maximum 24 hour lockout */

/* Evolution constants */
#define DNAUTH_MAX_GENERATIONS      100     /* Maximum lineage depth */
#define DNAUTH_EVOLUTION_INTERVAL   604800  /* Default: 1 week in seconds */
#define DNAUTH_MUTATION_RATE        0.02    /* 2% mutation rate per evolution */
#define DNAUTH_MAX_MUTATIONS_PER_GEN 3      /* Max mutations per generation */
#define DNAUTH_ANCESTOR_PENALTY     0.1     /* 10% penalty per generation back */
#define DNAUTH_MAX_ANCESTOR_GENS    5       /* How far back ancestors can auth */
#define DNAUTH_FITNESS_DECAY        0.05    /* Fitness decay per missed evolution */

/* Nucleotide values for bit encoding */
#define DNAUTH_A    0x00    /* Adenine  - 00 */
#define DNAUTH_T    0x01    /* Thymine  - 01 */
#define DNAUTH_G    0x02    /* Guanine  - 10 */
#define DNAUTH_C    0x03    /* Cytosine - 11 */

/* ==============================================================================
 * Types and Enumerations
 * ============================================================================== */

/* Authentication result codes */
typedef enum {
    DNAUTH_OK = 0,                  /* Authentication successful */
    DNAUTH_ERR_INVALID_SEQUENCE,    /* Sequence contains invalid characters */
    DNAUTH_ERR_TOO_SHORT,           /* Sequence below minimum length */
    DNAUTH_ERR_TOO_LONG,            /* Sequence exceeds maximum length */
    DNAUTH_ERR_LOW_COMPLEXITY,      /* Sequence lacks complexity */
    DNAUTH_ERR_NO_MATCH,            /* Sequence doesn't match stored key */
    DNAUTH_ERR_USER_NOT_FOUND,      /* User doesn't exist */
    DNAUTH_ERR_USER_EXISTS,         /* User already registered */
    DNAUTH_ERR_LOCKED_OUT,          /* Too many failed attempts */
    DNAUTH_ERR_EXPIRED,             /* Key has expired */
    DNAUTH_ERR_REVOKED,             /* Key has been revoked */
    DNAUTH_ERR_STORAGE,             /* Storage error */
    DNAUTH_ERR_INTERNAL             /* Internal error */
} dnauth_result_t;

/* Sequence complexity level */
typedef enum {
    DNAUTH_COMPLEXITY_LOW,          /* Simple repeating patterns */
    DNAUTH_COMPLEXITY_MEDIUM,       /* Some variation */
    DNAUTH_COMPLEXITY_HIGH,         /* Good nucleotide distribution */
    DNAUTH_COMPLEXITY_GENOMIC       /* Resembles real genomic data */
} dnauth_complexity_t;

/* Mutation types for fuzzy matching */
typedef enum {
    DNAUTH_MUTATION_NONE,           /* Exact match required */
    DNAUTH_MUTATION_SUBSTITUTION,   /* Single nucleotide change */
    DNAUTH_MUTATION_INSERTION,      /* Extra nucleotide inserted */
    DNAUTH_MUTATION_DELETION,       /* Nucleotide deleted */
    DNAUTH_MUTATION_TRANSVERSION,   /* Purine<->Pyrimidine swap */
    DNAUTH_MUTATION_TRANSITION      /* Purine<->Purine or Pyr<->Pyr */
} dnauth_mutation_type_t;

/* Key derivation method */
typedef enum {
    DNAUTH_KDF_CODON,               /* Codon-based (3 nucleotides -> amino acid) */
    DNAUTH_KDF_BINARY,              /* Direct 2-bit encoding */
    DNAUTH_KDF_COMPLEMENT,          /* Include complementary strand */
    DNAUTH_KDF_TRANSCRIPTION        /* DNA -> RNA -> hash */
} dnauth_kdf_t;

/* Evolution event types */
typedef enum {
    DNAUTH_EVO_POINT_MUTATION,      /* Single nucleotide change */
    DNAUTH_EVO_INSERTION,           /* Nucleotide inserted */
    DNAUTH_EVO_DELETION,            /* Nucleotide deleted */
    DNAUTH_EVO_TRANSVERSION,        /* Purine <-> Pyrimidine */
    DNAUTH_EVO_TRANSITION,          /* Purine <-> Purine or Pyr <-> Pyr */
    DNAUTH_EVO_DUPLICATION,         /* Segment duplicated */
    DNAUTH_EVO_INVERSION,           /* Segment inverted */
    DNAUTH_EVO_RECOMBINATION        /* Crossover event */
} dnauth_evolution_type_t;

/* Fitness factors */
typedef enum {
    DNAUTH_FITNESS_USAGE,           /* Based on authentication frequency */
    DNAUTH_FITNESS_AGE,             /* Based on time since creation */
    DNAUTH_FITNESS_COMPLEXITY,      /* Based on sequence complexity */
    DNAUTH_FITNESS_DIVERSITY,       /* Based on codon diversity */
    DNAUTH_FITNESS_ENVIRONMENTAL    /* Based on device/location factors */
} dnauth_fitness_factor_t;

/* Evolution pressure types */
typedef enum {
    DNAUTH_PRESSURE_NONE,           /* No evolution pressure */
    DNAUTH_PRESSURE_TIME,           /* Time-based evolution */
    DNAUTH_PRESSURE_USAGE,          /* Usage-based evolution */
    DNAUTH_PRESSURE_ENVIRONMENTAL,  /* Environmental factors */
    DNAUTH_PRESSURE_ADAPTIVE        /* Adapts to attack patterns */
} dnauth_pressure_t;

/* Authentication mode */
typedef enum {
    DNAUTH_MODE_EXACT,              /* Exact sequence match */
    DNAUTH_MODE_FUZZY,              /* Allow configured mutations */
    DNAUTH_MODE_CODON_EXACT,        /* Exact codon match (synonymous OK) */
    DNAUTH_MODE_PROTEIN             /* Translated protein match */
} dnauth_mode_t;

/* ==============================================================================
 * Data Structures
 * ============================================================================== */

/* Single mutation record */
typedef struct dnauth_mutation {
    dnauth_evolution_type_t type;
    uint32_t position;              /* Position in sequence */
    char original;                  /* Original nucleotide */
    char mutated;                   /* New nucleotide */
    time_t timestamp;               /* When mutation occurred */
    double fitness_impact;          /* Impact on fitness score */
} dnauth_mutation_t;

/* Generation record (one step in lineage) */
typedef struct dnauth_generation {
    uint32_t generation_id;
    uint32_t parent_id;             /* Previous generation (0 = origin) */
    time_t created_at;
    time_t evolved_at;              /* When this gen evolved to next */

    /* Sequence state */
    char *sequence;                 /* Sequence at this generation */
    char sequence_hash[DNAUTH_HASH_LEN + 1];
    uint32_t sequence_length;

    /* Mutations from parent */
    dnauth_mutation_t mutations[DNAUTH_MAX_MUTATIONS_PER_GEN];
    int mutation_count;

    /* Fitness metrics */
    double fitness_score;           /* 0.0 - 1.0 */
    int auth_count;                 /* Successful auths at this gen */
    int failed_count;               /* Failed auths at this gen */

    /* Status */
    int is_active;                  /* Currently active generation */
    int is_extinct;                 /* No longer valid for auth */

    struct dnauth_generation *next;
} dnauth_generation_t;

/* Evolution lineage (full history of a key) */
typedef struct dnauth_lineage {
    char user_id[64];
    uint32_t origin_id;             /* First generation ID */
    uint32_t current_gen;           /* Current active generation */
    int total_generations;

    /* Lineage chain */
    dnauth_generation_t *generations;
    dnauth_generation_t *current;   /* Pointer to active gen */

    /* Evolution settings */
    dnauth_pressure_t pressure;
    double mutation_rate;           /* Probability of mutation */
    int evolution_interval_secs;    /* Time between evolutions */
    time_t next_evolution;          /* Scheduled next evolution */

    /* Fitness tracking */
    double cumulative_fitness;
    int total_auths;
    int total_mutations;

    /* Ancestor authentication */
    int allow_ancestor_auth;        /* Allow auth with older gens */
    int max_ancestor_depth;         /* How many gens back */
    double ancestor_penalty;        /* Penalty per gen back */

    /* Notifications */
    int notify_on_evolution;
    char notification_channel[256]; /* Email, webhook, etc. */

    struct dnauth_lineage *next;
} dnauth_lineage_t;

/* Evolution event (for logging/notification) */
typedef struct dnauth_evolution_event {
    uint32_t event_id;
    char user_id[64];
    uint32_t from_generation;
    uint32_t to_generation;
    time_t timestamp;

    /* Mutations in this evolution */
    dnauth_mutation_t mutations[DNAUTH_MAX_MUTATIONS_PER_GEN];
    int mutation_count;

    /* Fitness change */
    double fitness_before;
    double fitness_after;

    /* User notification */
    int notified;
    char notification_text[512];

    struct dnauth_evolution_event *next;
} dnauth_evolution_event_t;

/* DNA sequence (validated and normalized) */
typedef struct dnauth_sequence {
    char *nucleotides;              /* ATGC string (uppercase, validated) */
    uint32_t length;                /* Sequence length */
    uint8_t *binary;                /* 2-bit encoded form */
    uint32_t binary_len;            /* Binary length in bytes */
    dnauth_complexity_t complexity; /* Computed complexity */

    /* Statistics */
    uint32_t count_a;               /* Adenine count */
    uint32_t count_t;               /* Thymine count */
    uint32_t count_g;               /* Guanine count */
    uint32_t count_c;               /* Cytosine count */
    double gc_content;              /* GC percentage (stability indicator) */
} dnauth_sequence_t;

/* Codon (3 nucleotides) */
typedef struct dnauth_codon {
    char nucleotides[4];            /* 3 chars + null */
    uint8_t value;                  /* 0-63 numeric value */
    char amino_acid;                /* Single letter amino acid code */
    const char *amino_name;         /* Full amino acid name */
    int is_stop;                    /* Is this a stop codon */
} dnauth_codon_t;

/* Stored authentication key */
typedef struct dnauth_key {
    uint32_t key_id;                /* Unique key identifier */
    char user_id[64];               /* Associated user ID */
    char key_hash[DNAUTH_HASH_LEN + 1];  /* Hashed sequence */
    uint8_t salt[DNAUTH_SALT_LEN];  /* Salt for hashing */

    /* Key properties */
    dnauth_kdf_t kdf_method;        /* Key derivation method used */
    dnauth_mode_t auth_mode;        /* Required authentication mode */
    int max_mutations;              /* Max allowed mutations (fuzzy mode) */
    uint32_t min_length;            /* Minimum sequence length */

    /* Metadata */
    time_t created_at;              /* Key creation time */
    time_t expires_at;              /* Expiration (0 = never) */
    time_t last_used;               /* Last successful auth */
    int revoked;                    /* Key has been revoked */
    char revoke_reason[256];        /* Why key was revoked */

    /* Security */
    int failed_attempts;            /* Consecutive failed attempts */
    time_t lockout_until;           /* Lockout expiration */

    /* Audit */
    uint64_t auth_count;            /* Total successful auths */
    char last_auth_ip[64];          /* Last auth source */

    struct dnauth_key *next;        /* Linked list */
} dnauth_key_t;

/* Authentication attempt record */
typedef struct dnauth_attempt {
    uint32_t attempt_id;
    char user_id[64];
    time_t timestamp;
    dnauth_result_t result;
    char source_ip[64];
    int mutations_found;            /* Number of mutations detected */
    dnauth_mutation_type_t mutation_types[DNAUTH_MAX_MUTATIONS];
    struct dnauth_attempt *next;
} dnauth_attempt_t;

/* Sequence analysis result */
typedef struct dnauth_analysis {
    dnauth_complexity_t complexity;
    double entropy;                 /* Shannon entropy */
    double gc_content;
    int has_repeats;                /* Contains long repeats */
    int repeat_length;              /* Longest repeat */
    int has_palindrome;             /* Contains palindromic sequences */
    int codon_diversity;            /* Unique codons used */
    char warnings[512];             /* Security warnings */
    int acceptable;                 /* Meets requirements */
} dnauth_analysis_t;

/* Match result for fuzzy authentication */
typedef struct dnauth_match {
    int matched;                    /* Did it match */
    int exact;                      /* Was it exact match */
    int mutations;                  /* Number of mutations found */
    int substitutions;
    int insertions;
    int deletions;
    double similarity;              /* 0.0 - 1.0 similarity score */
    int alignment_score;            /* Sequence alignment score */
} dnauth_match_t;

/* Forward declaration for Governor integration */
struct phantom_governor;

/* DNAuth system state */
typedef struct dnauth_system {
    int initialized;

    /* Governor integration (partial - audit logging only) */
    struct phantom_governor *governor;  /* For audit logging to GeoFS */

    /* Configuration */
    dnauth_mode_t default_mode;
    dnauth_kdf_t default_kdf;
    int default_max_mutations;
    uint32_t min_sequence_length;
    uint32_t max_sequence_length;
    dnauth_complexity_t min_complexity;
    int require_all_nucleotides;    /* Require A, T, G, and C */

    /* Key storage */
    dnauth_key_t *keys;
    int key_count;
    char storage_path[4096];        /* GeoFS path for key storage */

    /* Audit log */
    dnauth_attempt_t *attempts;
    int attempt_count;
    int max_attempts_log;           /* Max attempts to keep */

    /* Codon table */
    dnauth_codon_t codon_table[DNAUTH_CODON_TABLE_SIZE];

    /* Statistics */
    uint64_t total_auths;
    uint64_t successful_auths;
    uint64_t failed_auths;
    uint64_t fuzzy_matches;

    /* Callbacks */
    void (*on_auth_success)(const char *user_id, void *data);
    void (*on_auth_failure)(const char *user_id, dnauth_result_t reason, void *data);
    void (*on_lockout)(const char *user_id, void *data);
    void *callback_data;

    /* === EVOLUTION SYSTEM === */

    /* Lineage tracking */
    dnauth_lineage_t *lineages;
    int lineage_count;

    /* Evolution events log */
    dnauth_evolution_event_t *evolution_events;
    int evolution_event_count;
    int max_evolution_events;

    /* Evolution daemon */
    int evolution_enabled;
    int evolution_daemon_running;
    int evolution_check_interval;   /* Seconds between checks */
    time_t last_evolution_check;

    /* Default evolution settings */
    dnauth_pressure_t default_pressure;
    double default_mutation_rate;
    int default_evolution_interval;
    int default_allow_ancestors;
    int default_max_ancestor_depth;

    /* Evolution callbacks */
    void (*on_evolution)(const char *user_id, dnauth_evolution_event_t *event, void *data);
    void (*on_extinction)(const char *user_id, uint32_t generation, void *data);
    void (*on_fitness_warning)(const char *user_id, double fitness, void *data);
} dnauth_system_t;

/* ==============================================================================
 * Function Prototypes
 * ============================================================================== */

/* System lifecycle */
dnauth_system_t *dnauth_init(const char *storage_path);
void dnauth_cleanup(dnauth_system_t *sys);
int dnauth_save(dnauth_system_t *sys);
int dnauth_load(dnauth_system_t *sys);

/* Configuration */
void dnauth_set_mode(dnauth_system_t *sys, dnauth_mode_t mode);
void dnauth_set_kdf(dnauth_system_t *sys, dnauth_kdf_t kdf);
void dnauth_set_max_mutations(dnauth_system_t *sys, int max);
void dnauth_set_min_length(dnauth_system_t *sys, uint32_t len);
void dnauth_set_min_complexity(dnauth_system_t *sys, dnauth_complexity_t complexity);

/* Sequence operations */
dnauth_sequence_t *dnauth_sequence_parse(const char *input);
void dnauth_sequence_free(dnauth_sequence_t *seq);
int dnauth_sequence_validate(const char *input, char *error, size_t error_len);
char *dnauth_sequence_normalize(const char *input);
char *dnauth_sequence_complement(const dnauth_sequence_t *seq);
char *dnauth_sequence_reverse_complement(const dnauth_sequence_t *seq);
char *dnauth_sequence_transcribe(const dnauth_sequence_t *seq);  /* DNA -> RNA */
char *dnauth_sequence_translate(const dnauth_sequence_t *seq);   /* DNA -> Protein */

/* Sequence analysis */
dnauth_analysis_t *dnauth_analyze(const dnauth_sequence_t *seq);
void dnauth_analysis_free(dnauth_analysis_t *analysis);
dnauth_complexity_t dnauth_compute_complexity(const dnauth_sequence_t *seq);
double dnauth_compute_entropy(const dnauth_sequence_t *seq);
int dnauth_find_repeats(const dnauth_sequence_t *seq, int min_repeat);
int dnauth_find_palindromes(const dnauth_sequence_t *seq, int min_length);

/* Codon operations */
void dnauth_init_codon_table(dnauth_system_t *sys);
dnauth_codon_t *dnauth_get_codon(dnauth_system_t *sys, const char *triplet);
char dnauth_codon_to_amino(dnauth_system_t *sys, const char *triplet);
int dnauth_codons_synonymous(dnauth_system_t *sys, const char *c1, const char *c2);

/* Key management */
dnauth_result_t dnauth_register(dnauth_system_t *sys, const char *user_id,
                                 const char *sequence);
dnauth_result_t dnauth_register_with_options(dnauth_system_t *sys,
                                              const char *user_id,
                                              const char *sequence,
                                              dnauth_mode_t mode,
                                              dnauth_kdf_t kdf,
                                              int max_mutations,
                                              time_t expires);
dnauth_result_t dnauth_revoke(dnauth_system_t *sys, const char *user_id,
                               const char *reason);
dnauth_result_t dnauth_change_key(dnauth_system_t *sys, const char *user_id,
                                   const char *old_sequence,
                                   const char *new_sequence);
dnauth_key_t *dnauth_get_key(dnauth_system_t *sys, const char *user_id);
int dnauth_key_exists(dnauth_system_t *sys, const char *user_id);

/* Authentication */
dnauth_result_t dnauth_authenticate(dnauth_system_t *sys, const char *user_id,
                                     const char *sequence);
dnauth_result_t dnauth_authenticate_fuzzy(dnauth_system_t *sys, const char *user_id,
                                           const char *sequence, int max_mutations,
                                           dnauth_match_t *match_result);

/* Sequence matching */
dnauth_match_t *dnauth_match_sequences(const dnauth_sequence_t *seq1,
                                        const dnauth_sequence_t *seq2,
                                        int max_mutations);
void dnauth_match_free(dnauth_match_t *match);
int dnauth_levenshtein_distance(const char *s1, const char *s2);
int dnauth_needleman_wunsch(const char *s1, const char *s2, int *score);

/* Hashing */
char *dnauth_hash_sequence(const dnauth_sequence_t *seq,
                           const uint8_t *salt, size_t salt_len,
                           dnauth_kdf_t method);
void dnauth_generate_salt(uint8_t *salt, size_t len);
char *dnauth_derive_key_codon(const dnauth_sequence_t *seq, const uint8_t *salt);
char *dnauth_derive_key_binary(const dnauth_sequence_t *seq, const uint8_t *salt);

/* Audit */
void dnauth_log_attempt(dnauth_system_t *sys, const char *user_id,
                        dnauth_result_t result, const char *source);
dnauth_attempt_t *dnauth_get_attempts(dnauth_system_t *sys, const char *user_id,
                                       int limit);
void dnauth_clear_attempts(dnauth_system_t *sys, const char *user_id);

/* Utility */
const char *dnauth_result_string(dnauth_result_t result);
const char *dnauth_complexity_string(dnauth_complexity_t complexity);
const char *dnauth_mode_string(dnauth_mode_t mode);
char *dnauth_generate_random_sequence(uint32_t length);
int dnauth_is_valid_nucleotide(char c);
char dnauth_complement_nucleotide(char c);

/* ==============================================================================
 * Evolution System
 * ============================================================================== */

/* Evolution lifecycle */
int dnauth_evolution_init(dnauth_system_t *sys);
void dnauth_evolution_cleanup(dnauth_system_t *sys);
void dnauth_evolution_enable(dnauth_system_t *sys, int enable);

/* Lineage management */
dnauth_lineage_t *dnauth_lineage_create(dnauth_system_t *sys, const char *user_id,
                                         const char *initial_sequence);
dnauth_lineage_t *dnauth_lineage_get(dnauth_system_t *sys, const char *user_id);
void dnauth_lineage_free(dnauth_lineage_t *lineage);
int dnauth_lineage_get_depth(dnauth_lineage_t *lineage);

/* Evolution operations */
dnauth_evolution_event_t *dnauth_evolve(dnauth_system_t *sys, const char *user_id);
dnauth_evolution_event_t *dnauth_evolve_forced(dnauth_system_t *sys, const char *user_id,
                                                int num_mutations);
int dnauth_check_evolution_due(dnauth_system_t *sys, const char *user_id);
void dnauth_schedule_evolution(dnauth_system_t *sys, const char *user_id, time_t when);

/* Mutation operations */
dnauth_mutation_t dnauth_generate_mutation(const char *sequence, uint32_t length,
                                            dnauth_evolution_type_t type);
char *dnauth_apply_mutation(const char *sequence, dnauth_mutation_t *mutation);
char *dnauth_apply_mutations(const char *sequence, dnauth_mutation_t *mutations, int count);
int dnauth_count_differences(const char *seq1, const char *seq2);

/* Ancestor authentication */
dnauth_result_t dnauth_authenticate_ancestor(dnauth_system_t *sys, const char *user_id,
                                              const char *sequence, int max_generations_back,
                                              int *generation_matched);
dnauth_generation_t *dnauth_get_ancestor(dnauth_lineage_t *lineage, int generations_back);
double dnauth_ancestor_penalty(dnauth_system_t *sys, int generations_back);

/* Fitness calculation */
double dnauth_calculate_fitness(dnauth_system_t *sys, dnauth_lineage_t *lineage);
void dnauth_update_fitness(dnauth_system_t *sys, const char *user_id);
double dnauth_get_fitness(dnauth_system_t *sys, const char *user_id);
int dnauth_is_fit(dnauth_system_t *sys, const char *user_id, double threshold);

/* Generation queries */
dnauth_generation_t *dnauth_get_current_generation(dnauth_system_t *sys, const char *user_id);
dnauth_generation_t *dnauth_get_generation(dnauth_lineage_t *lineage, uint32_t gen_id);
char *dnauth_get_current_sequence(dnauth_system_t *sys, const char *user_id);
int dnauth_get_generation_number(dnauth_system_t *sys, const char *user_id);

/* Evolution history */
dnauth_evolution_event_t *dnauth_get_evolution_history(dnauth_system_t *sys,
                                                        const char *user_id, int limit);
dnauth_mutation_t *dnauth_get_mutation_history(dnauth_lineage_t *lineage, int *count);
char *dnauth_describe_evolution(dnauth_evolution_event_t *event);

/* Evolution daemon */
int dnauth_evolution_daemon_start(dnauth_system_t *sys);
void dnauth_evolution_daemon_stop(dnauth_system_t *sys);
void dnauth_evolution_daemon_tick(dnauth_system_t *sys);

/* Notification helpers */
char *dnauth_format_mutation_notice(dnauth_mutation_t *mutation);
char *dnauth_format_evolution_notice(dnauth_evolution_event_t *event);
void dnauth_send_evolution_notification(dnauth_system_t *sys, const char *user_id,
                                         dnauth_evolution_event_t *event);

/* Evolution configuration */
void dnauth_set_evolution_interval(dnauth_system_t *sys, const char *user_id, int seconds);
void dnauth_set_mutation_rate(dnauth_system_t *sys, const char *user_id, double rate);
void dnauth_set_pressure(dnauth_system_t *sys, const char *user_id, dnauth_pressure_t pressure);
void dnauth_set_ancestor_policy(dnauth_system_t *sys, const char *user_id,
                                 int allow, int max_depth, double penalty);

/* Utility strings */
const char *dnauth_evolution_type_string(dnauth_evolution_type_t type);
const char *dnauth_pressure_string(dnauth_pressure_t pressure);

/* ==============================================================================
 * Governor Integration (Partial - Audit Logging)
 * ============================================================================== */

/*
 * The Governor integration for DNAuth is PARTIAL by design.
 *
 * The Governor IS used for:
 * - Audit logging of all key operations to GeoFS
 * - Rate limiting visibility on failed auth attempts
 * - Policy enforcement logging (admin actions)
 * - Revocation justification recording
 *
 * The Governor is NOT used for:
 * - Treating DNA sequences as "code" requiring execution approval
 * - Blocking compliant authentication operations
 * - Evaluating sequences for "destructive" patterns
 *
 * This respects Constitutional Article III while maintaining accountability.
 */

/* Set Governor for audit logging (optional - DNAuth works without it) */
void dnauth_set_governor(dnauth_system_t *sys, struct phantom_governor *gov);

/* Log types for DNAuth events */
typedef enum {
    DNAUTH_LOG_REGISTRATION,        /* New key registered */
    DNAUTH_LOG_REVOCATION,          /* Key revoked */
    DNAUTH_LOG_AUTH_SUCCESS,        /* Successful authentication */
    DNAUTH_LOG_AUTH_FAILURE,        /* Failed authentication */
    DNAUTH_LOG_LOCKOUT,             /* User locked out */
    DNAUTH_LOG_EVOLUTION,           /* Key evolved */
    DNAUTH_LOG_FORCED_EVOLUTION,    /* Admin forced evolution */
    DNAUTH_LOG_ANCESTOR_AUTH,       /* Ancestor key used */
    DNAUTH_LOG_KEY_CHANGE           /* Key changed by user */
} dnauth_log_type_t;

/* Get string representation of log type */
const char *dnauth_log_type_string(dnauth_log_type_t type);

/* ==============================================================================
 * Amino Acid Codes (for codon translation)
 * ============================================================================== */

/*
 * Standard genetic code:
 * A = Alanine       G = Glycine       M = Methionine    S = Serine
 * C = Cysteine      H = Histidine     N = Asparagine    T = Threonine
 * D = Aspartic Acid I = Isoleucine    P = Proline       V = Valine
 * E = Glutamic Acid K = Lysine        Q = Glutamine     W = Tryptophan
 * F = Phenylalanine L = Leucine       R = Arginine      Y = Tyrosine
 * * = Stop codon
 */

#endif /* PHANTOM_DNAUTH_H */
