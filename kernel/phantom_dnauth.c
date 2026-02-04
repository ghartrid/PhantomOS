/*
 * ==============================================================================
 *                              DNAuth
 *                 DNA-Based Authentication for PhantomOS
 *                      "Your Code is Your Key"
 * ==============================================================================
 */

#include "phantom_dnauth.h"
#include "governor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

/* For SHA-256 hashing */
#include <openssl/sha.h>
#include <openssl/rand.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Secure Random Number Generation Wrappers
 * Always use RAND_bytes() from OpenSSL, never fall back to insecure rand()
 * ───────────────────────────────────────────────────────────────────────────── */

static int secure_random_uint32(uint32_t *out) {
    return RAND_bytes((unsigned char *)out, sizeof(uint32_t)) == 1 ? 0 : -1;
}

static uint32_t secure_random_range(uint32_t max) {
    if (max == 0) return 0;
    uint32_t val;
    /* Use rejection sampling to avoid modulo bias */
    uint32_t limit = UINT32_MAX - (UINT32_MAX % max);
    do {
        if (secure_random_uint32(&val) != 0) return 0;
    } while (val >= limit);
    return val % max;
}

static double secure_random_double(void) {
    uint32_t val;
    if (secure_random_uint32(&val) != 0) return 0.0;
    return (double)val / (double)UINT32_MAX;
}

/* Forward declarations for Governor integration functions */
static void dnauth_governor_log(dnauth_system_t *sys,
                                 dnauth_log_type_t log_type,
                                 const char *user_id,
                                 const char *details);
static void dnauth_governor_log_registration(dnauth_system_t *sys,
                                              const char *user_id,
                                              dnauth_mode_t mode);
static void dnauth_governor_log_revocation(dnauth_system_t *sys,
                                            const char *user_id,
                                            const char *reason);
static void dnauth_governor_log_auth(dnauth_system_t *sys,
                                      const char *user_id,
                                      dnauth_result_t result,
                                      int is_ancestor,
                                      int generation_back);
static void dnauth_governor_log_lockout(dnauth_system_t *sys,
                                         const char *user_id,
                                         int failed_attempts);
static void dnauth_governor_log_evolution(dnauth_system_t *sys,
                                           const char *user_id,
                                           int from_gen,
                                           int to_gen,
                                           int mutation_count,
                                           int is_forced);

/* ==============================================================================
 * Standard Genetic Code Table
 * ============================================================================== */

static const struct {
    const char *codon;
    char amino;
    const char *name;
    int is_stop;
} GENETIC_CODE[] = {
    /* Phenylalanine */
    {"TTT", 'F', "Phenylalanine", 0}, {"TTC", 'F', "Phenylalanine", 0},
    /* Leucine */
    {"TTA", 'L', "Leucine", 0}, {"TTG", 'L', "Leucine", 0},
    {"CTT", 'L', "Leucine", 0}, {"CTC", 'L', "Leucine", 0},
    {"CTA", 'L', "Leucine", 0}, {"CTG", 'L', "Leucine", 0},
    /* Isoleucine */
    {"ATT", 'I', "Isoleucine", 0}, {"ATC", 'I', "Isoleucine", 0}, {"ATA", 'I', "Isoleucine", 0},
    /* Methionine (Start) */
    {"ATG", 'M', "Methionine", 0},
    /* Valine */
    {"GTT", 'V', "Valine", 0}, {"GTC", 'V', "Valine", 0},
    {"GTA", 'V', "Valine", 0}, {"GTG", 'V', "Valine", 0},
    /* Serine */
    {"TCT", 'S', "Serine", 0}, {"TCC", 'S', "Serine", 0},
    {"TCA", 'S', "Serine", 0}, {"TCG", 'S', "Serine", 0},
    {"AGT", 'S', "Serine", 0}, {"AGC", 'S', "Serine", 0},
    /* Proline */
    {"CCT", 'P', "Proline", 0}, {"CCC", 'P', "Proline", 0},
    {"CCA", 'P', "Proline", 0}, {"CCG", 'P', "Proline", 0},
    /* Threonine */
    {"ACT", 'T', "Threonine", 0}, {"ACC", 'T', "Threonine", 0},
    {"ACA", 'T', "Threonine", 0}, {"ACG", 'T', "Threonine", 0},
    /* Alanine */
    {"GCT", 'A', "Alanine", 0}, {"GCC", 'A', "Alanine", 0},
    {"GCA", 'A', "Alanine", 0}, {"GCG", 'A', "Alanine", 0},
    /* Tyrosine */
    {"TAT", 'Y', "Tyrosine", 0}, {"TAC", 'Y', "Tyrosine", 0},
    /* Stop codons */
    {"TAA", '*', "Stop", 1}, {"TAG", '*', "Stop", 1}, {"TGA", '*', "Stop", 1},
    /* Histidine */
    {"CAT", 'H', "Histidine", 0}, {"CAC", 'H', "Histidine", 0},
    /* Glutamine */
    {"CAA", 'Q', "Glutamine", 0}, {"CAG", 'Q', "Glutamine", 0},
    /* Asparagine */
    {"AAT", 'N', "Asparagine", 0}, {"AAC", 'N', "Asparagine", 0},
    /* Lysine */
    {"AAA", 'K', "Lysine", 0}, {"AAG", 'K', "Lysine", 0},
    /* Aspartic Acid */
    {"GAT", 'D', "Aspartic Acid", 0}, {"GAC", 'D', "Aspartic Acid", 0},
    /* Glutamic Acid */
    {"GAA", 'E', "Glutamic Acid", 0}, {"GAG", 'E', "Glutamic Acid", 0},
    /* Cysteine */
    {"TGT", 'C', "Cysteine", 0}, {"TGC", 'C', "Cysteine", 0},
    /* Tryptophan */
    {"TGG", 'W', "Tryptophan", 0},
    /* Arginine */
    {"CGT", 'R', "Arginine", 0}, {"CGC", 'R', "Arginine", 0},
    {"CGA", 'R', "Arginine", 0}, {"CGG", 'R', "Arginine", 0},
    {"AGA", 'R', "Arginine", 0}, {"AGG", 'R', "Arginine", 0},
    /* Glycine */
    {"GGT", 'G', "Glycine", 0}, {"GGC", 'G', "Glycine", 0},
    {"GGA", 'G', "Glycine", 0}, {"GGG", 'G', "Glycine", 0},
    {NULL, 0, NULL, 0}
};

/* ==============================================================================
 * Utility Functions
 * ============================================================================== */

int dnauth_is_valid_nucleotide(char c) {
    c = toupper(c);
    return (c == 'A' || c == 'T' || c == 'G' || c == 'C');
}

char dnauth_complement_nucleotide(char c) {
    switch (toupper(c)) {
        case 'A': return 'T';
        case 'T': return 'A';
        case 'G': return 'C';
        case 'C': return 'G';
        default: return c;
    }
}

static uint8_t nucleotide_to_bits(char c) {
    switch (toupper(c)) {
        case 'A': return DNAUTH_A;
        case 'T': return DNAUTH_T;
        case 'G': return DNAUTH_G;
        case 'C': return DNAUTH_C;
        default: return 0;
    }
}

static char bits_to_nucleotide(uint8_t bits) {
    switch (bits & 0x03) {
        case DNAUTH_A: return 'A';
        case DNAUTH_T: return 'T';
        case DNAUTH_G: return 'G';
        case DNAUTH_C: return 'C';
        default: return 'N';
    }
}

const char *dnauth_result_string(dnauth_result_t result) {
    switch (result) {
        case DNAUTH_OK: return "OK";
        case DNAUTH_ERR_INVALID_SEQUENCE: return "Invalid sequence";
        case DNAUTH_ERR_TOO_SHORT: return "Sequence too short";
        case DNAUTH_ERR_TOO_LONG: return "Sequence too long";
        case DNAUTH_ERR_LOW_COMPLEXITY: return "Low complexity";
        case DNAUTH_ERR_NO_MATCH: return "No match";
        case DNAUTH_ERR_USER_NOT_FOUND: return "User not found";
        case DNAUTH_ERR_USER_EXISTS: return "User exists";
        case DNAUTH_ERR_LOCKED_OUT: return "Account locked";
        case DNAUTH_ERR_EXPIRED: return "Key expired";
        case DNAUTH_ERR_REVOKED: return "Key revoked";
        case DNAUTH_ERR_STORAGE: return "Storage error";
        case DNAUTH_ERR_INTERNAL: return "Internal error";
        default: return "Unknown error";
    }
}

const char *dnauth_complexity_string(dnauth_complexity_t complexity) {
    switch (complexity) {
        case DNAUTH_COMPLEXITY_LOW: return "Low";
        case DNAUTH_COMPLEXITY_MEDIUM: return "Medium";
        case DNAUTH_COMPLEXITY_HIGH: return "High";
        case DNAUTH_COMPLEXITY_GENOMIC: return "Genomic";
        default: return "Unknown";
    }
}

const char *dnauth_mode_string(dnauth_mode_t mode) {
    switch (mode) {
        case DNAUTH_MODE_EXACT: return "Exact";
        case DNAUTH_MODE_FUZZY: return "Fuzzy";
        case DNAUTH_MODE_CODON_EXACT: return "Codon Exact";
        case DNAUTH_MODE_PROTEIN: return "Protein";
        default: return "Unknown";
    }
}

/* ==============================================================================
 * Sequence Operations
 * ============================================================================== */

int dnauth_sequence_validate(const char *input, char *error, size_t error_len) {
    if (!input || strlen(input) == 0) {
        if (error) snprintf(error, error_len, "Empty sequence");
        return 0;
    }

    size_t len = strlen(input);

    if (len < DNAUTH_MIN_SEQUENCE_LEN) {
        if (error) snprintf(error, error_len, "Sequence too short (min %d)", DNAUTH_MIN_SEQUENCE_LEN);
        return 0;
    }

    if (len > DNAUTH_MAX_SEQUENCE_LEN) {
        if (error) snprintf(error, error_len, "Sequence too long (max %d)", DNAUTH_MAX_SEQUENCE_LEN);
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        char c = input[i];
        if (isspace(c)) continue;  /* Allow whitespace */
        if (!dnauth_is_valid_nucleotide(c)) {
            if (error) snprintf(error, error_len, "Invalid nucleotide '%c' at position %zu", c, i);
            return 0;
        }
    }

    return 1;
}

char *dnauth_sequence_normalize(const char *input) {
    if (!input) return NULL;

    size_t len = strlen(input);
    char *normalized = malloc(len + 1);
    if (!normalized) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (!isspace(input[i])) {
            normalized[j++] = toupper(input[i]);
        }
    }
    normalized[j] = '\0';

    return normalized;
}

dnauth_sequence_t *dnauth_sequence_parse(const char *input) {
    char error[256];
    if (!dnauth_sequence_validate(input, error, sizeof(error))) {
        fprintf(stderr, "[DNAuth] Validation failed: %s\n", error);
        return NULL;
    }

    char *normalized = dnauth_sequence_normalize(input);
    if (!normalized) return NULL;

    dnauth_sequence_t *seq = calloc(1, sizeof(dnauth_sequence_t));
    if (!seq) {
        free(normalized);
        return NULL;
    }

    seq->nucleotides = normalized;
    seq->length = strlen(normalized);

    /* Binary encoding (2 bits per nucleotide) */
    seq->binary_len = (seq->length + 3) / 4;  /* Round up */
    seq->binary = calloc(seq->binary_len, 1);
    if (!seq->binary) {
        free(normalized);
        free(seq);
        return NULL;
    }

    /* Encode and count nucleotides */
    if (!seq->nucleotides) {
        free(seq->binary);
        free(seq);
        return NULL;
    }
    for (uint32_t i = 0; i < seq->length; i++) {
        char c = seq->nucleotides[i];
        uint8_t bits = nucleotide_to_bits(c);

        /* Pack into binary (4 nucleotides per byte) */
        int byte_idx = i / 4;
        int bit_offset = (3 - (i % 4)) * 2;
        seq->binary[byte_idx] |= (bits << bit_offset);

        /* Count */
        switch (c) {
            case 'A': seq->count_a++; break;
            case 'T': seq->count_t++; break;
            case 'G': seq->count_g++; break;
            case 'C': seq->count_c++; break;
        }
    }

    /* Calculate GC content */
    seq->gc_content = (double)(seq->count_g + seq->count_c) / seq->length;

    /* Compute complexity */
    seq->complexity = dnauth_compute_complexity(seq);

    return seq;
}

void dnauth_sequence_free(dnauth_sequence_t *seq) {
    if (!seq) return;
    free(seq->nucleotides);
    free(seq->binary);
    free(seq);
}

char *dnauth_sequence_complement(const dnauth_sequence_t *seq) {
    if (!seq || !seq->nucleotides) return NULL;

    char *comp = malloc(seq->length + 1);
    if (!comp) return NULL;

    for (uint32_t i = 0; i < seq->length; i++) {
        comp[i] = dnauth_complement_nucleotide(seq->nucleotides[i]);
    }
    comp[seq->length] = '\0';

    return comp;
}

char *dnauth_sequence_reverse_complement(const dnauth_sequence_t *seq) {
    if (!seq || !seq->nucleotides) return NULL;

    char *rc = malloc(seq->length + 1);
    if (!rc) return NULL;

    for (uint32_t i = 0; i < seq->length; i++) {
        rc[i] = dnauth_complement_nucleotide(seq->nucleotides[seq->length - 1 - i]);
    }
    rc[seq->length] = '\0';

    return rc;
}

char *dnauth_sequence_transcribe(const dnauth_sequence_t *seq) {
    if (!seq || !seq->nucleotides) return NULL;

    char *rna = malloc(seq->length + 1);
    if (!rna) return NULL;

    for (uint32_t i = 0; i < seq->length; i++) {
        char c = seq->nucleotides[i];
        rna[i] = (c == 'T') ? 'U' : c;  /* T -> U for RNA */
    }
    rna[seq->length] = '\0';

    return rna;
}

/* ==============================================================================
 * Complexity Analysis
 * ============================================================================== */

double dnauth_compute_entropy(const dnauth_sequence_t *seq) {
    if (!seq || seq->length == 0) return 0.0;

    double entropy = 0.0;
    double len = (double)seq->length;

    uint32_t counts[4] = {seq->count_a, seq->count_t, seq->count_g, seq->count_c};

    for (int i = 0; i < 4; i++) {
        if (counts[i] > 0) {
            double p = counts[i] / len;
            entropy -= p * log2(p);
        }
    }

    return entropy;  /* Max entropy for 4 symbols is 2.0 */
}

dnauth_complexity_t dnauth_compute_complexity(const dnauth_sequence_t *seq) {
    if (!seq) return DNAUTH_COMPLEXITY_LOW;

    double entropy = dnauth_compute_entropy(seq);

    /* Check for all nucleotides present */
    int has_all = (seq->count_a > 0 && seq->count_t > 0 &&
                   seq->count_g > 0 && seq->count_c > 0);

    /* Check for repeats */
    int has_long_repeat = dnauth_find_repeats(seq, 6);

    /* Determine complexity */
    if (entropy < 1.0 || has_long_repeat) {
        return DNAUTH_COMPLEXITY_LOW;
    } else if (entropy < 1.5 || !has_all) {
        return DNAUTH_COMPLEXITY_MEDIUM;
    } else if (entropy < 1.9) {
        return DNAUTH_COMPLEXITY_HIGH;
    } else {
        return DNAUTH_COMPLEXITY_GENOMIC;
    }
}

int dnauth_find_repeats(const dnauth_sequence_t *seq, int min_repeat) {
    if (!seq || !seq->nucleotides || seq->length < (uint32_t)min_repeat) return 0;

    /* Check for simple repeats like AAAAAA or ATATAT */
    int max_repeat = 0;
    int current_repeat = 1;

    for (uint32_t i = 1; i < seq->length; i++) {
        if (seq->nucleotides[i] == seq->nucleotides[i - 1]) {
            current_repeat++;
            if (current_repeat > max_repeat) max_repeat = current_repeat;
        } else {
            current_repeat = 1;
        }
    }

    /* Check for dinucleotide repeats */
    if (seq->length >= 4) {
        current_repeat = 1;
        for (uint32_t i = 2; i < seq->length - 1; i += 2) {
            if (seq->nucleotides[i] == seq->nucleotides[i - 2] &&
                seq->nucleotides[i + 1] == seq->nucleotides[i - 1]) {
                current_repeat++;
                if (current_repeat * 2 > max_repeat) max_repeat = current_repeat * 2;
            } else {
                current_repeat = 1;
            }
        }
    }

    return max_repeat >= min_repeat;
}

int dnauth_find_palindromes(const dnauth_sequence_t *seq, int min_length) {
    if (!seq || !seq->nucleotides || seq->length < (uint32_t)min_length) return 0;

    /* A DNA palindrome is where the reverse complement equals the original */
    for (uint32_t start = 0; start <= seq->length - min_length; start++) {
        for (int len = min_length; start + len <= seq->length; len++) {
            int is_palindrome = 1;
            for (int i = 0; i < len / 2; i++) {
                char left = seq->nucleotides[start + i];
                char right = seq->nucleotides[start + len - 1 - i];
                if (dnauth_complement_nucleotide(left) != right) {
                    is_palindrome = 0;
                    break;
                }
            }
            if (is_palindrome && len >= min_length) {
                return 1;
            }
        }
    }

    return 0;
}

dnauth_analysis_t *dnauth_analyze(const dnauth_sequence_t *seq) {
    if (!seq) return NULL;

    dnauth_analysis_t *analysis = calloc(1, sizeof(dnauth_analysis_t));
    if (!analysis) return NULL;

    analysis->complexity = dnauth_compute_complexity(seq);
    analysis->entropy = dnauth_compute_entropy(seq);
    analysis->gc_content = seq->gc_content;
    analysis->has_repeats = dnauth_find_repeats(seq, 6);
    analysis->has_palindrome = dnauth_find_palindromes(seq, 6);

    /* Count unique codons */
    int codon_seen[64] = {0};
    analysis->codon_diversity = 0;
    for (uint32_t i = 0; i + 2 < seq->length; i += 3) {
        int idx = (nucleotide_to_bits(seq->nucleotides[i]) << 4) |
                  (nucleotide_to_bits(seq->nucleotides[i + 1]) << 2) |
                  nucleotide_to_bits(seq->nucleotides[i + 2]);
        if (!codon_seen[idx]) {
            codon_seen[idx] = 1;
            analysis->codon_diversity++;
        }
    }

    /* Generate warnings */
    analysis->warnings[0] = '\0';
    if (analysis->complexity == DNAUTH_COMPLEXITY_LOW) {
        strcat(analysis->warnings, "Low complexity sequence. ");
    }
    if (analysis->has_repeats) {
        strcat(analysis->warnings, "Contains long repeats. ");
    }
    if (analysis->entropy < 1.5) {
        strcat(analysis->warnings, "Low entropy. ");
    }
    if (seq->count_a == 0 || seq->count_t == 0 ||
        seq->count_g == 0 || seq->count_c == 0) {
        strcat(analysis->warnings, "Missing nucleotide types. ");
    }

    /* Determine if acceptable */
    analysis->acceptable = (analysis->complexity >= DNAUTH_COMPLEXITY_MEDIUM &&
                           !analysis->has_repeats &&
                           analysis->entropy >= 1.5);

    return analysis;
}

void dnauth_analysis_free(dnauth_analysis_t *analysis) {
    free(analysis);
}

/* ==============================================================================
 * Codon Table Operations
 * ============================================================================== */

void dnauth_init_codon_table(dnauth_system_t *sys) {
    if (!sys) return;

    for (int i = 0; GENETIC_CODE[i].codon != NULL; i++) {
        /* Calculate codon index from nucleotides */
        const char *codon = GENETIC_CODE[i].codon;
        int idx = (nucleotide_to_bits(codon[0]) << 4) |
                  (nucleotide_to_bits(codon[1]) << 2) |
                  nucleotide_to_bits(codon[2]);

        if (idx < DNAUTH_CODON_TABLE_SIZE) {
            strncpy(sys->codon_table[idx].nucleotides, codon, 3);
            sys->codon_table[idx].nucleotides[3] = '\0';
            sys->codon_table[idx].value = idx;
            sys->codon_table[idx].amino_acid = GENETIC_CODE[i].amino;
            sys->codon_table[idx].amino_name = GENETIC_CODE[i].name;
            sys->codon_table[idx].is_stop = GENETIC_CODE[i].is_stop;
        }
    }
}

char dnauth_codon_to_amino(dnauth_system_t *sys, const char *triplet) {
    if (!sys || !triplet || strlen(triplet) < 3) return '?';

    int idx = (nucleotide_to_bits(triplet[0]) << 4) |
              (nucleotide_to_bits(triplet[1]) << 2) |
              nucleotide_to_bits(triplet[2]);

    if (idx < DNAUTH_CODON_TABLE_SIZE) {
        return sys->codon_table[idx].amino_acid;
    }
    return '?';
}

int dnauth_codons_synonymous(dnauth_system_t *sys, const char *c1, const char *c2) {
    return dnauth_codon_to_amino(sys, c1) == dnauth_codon_to_amino(sys, c2);
}

char *dnauth_sequence_translate(const dnauth_sequence_t *seq) {
    if (!seq || !seq->nucleotides) return NULL;

    /* Create temporary system for codon table */
    dnauth_system_t temp_sys = {0};
    dnauth_init_codon_table(&temp_sys);

    int protein_len = seq->length / 3;
    char *protein = malloc(protein_len + 1);
    if (!protein) return NULL;

    for (int i = 0; i < protein_len; i++) {
        char codon[4] = {
            seq->nucleotides[i * 3],
            seq->nucleotides[i * 3 + 1],
            seq->nucleotides[i * 3 + 2],
            '\0'
        };
        protein[i] = dnauth_codon_to_amino(&temp_sys, codon);
    }
    protein[protein_len] = '\0';

    return protein;
}

/* ==============================================================================
 * Hashing and Key Derivation
 * ============================================================================== */

void dnauth_generate_salt(uint8_t *salt, size_t len) {
    if (!salt || len == 0) return;

    if (RAND_bytes(salt, len) != 1) {
        /* SECURITY: No fallback to insecure rand() - fail securely instead */
        fprintf(stderr, "[DNAuth] CRITICAL: Failed to generate secure random salt\n");
        memset(salt, 0, len);  /* Zero out to indicate failure */
    }
}

char *dnauth_derive_key_binary(const dnauth_sequence_t *seq, const uint8_t *salt) {
    if (!seq || !salt) return NULL;

    /* Concatenate salt + binary sequence */
    size_t total_len = DNAUTH_SALT_LEN + seq->binary_len;
    uint8_t *data = malloc(total_len);
    if (!data) return NULL;

    memcpy(data, salt, DNAUTH_SALT_LEN);
    memcpy(data + DNAUTH_SALT_LEN, seq->binary, seq->binary_len);

    /* SHA-256 hash */
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data, total_len, hash);
    free(data);

    /* Convert to hex string */
    char *hex = malloc(DNAUTH_HASH_LEN + 1);
    if (!hex) return NULL;

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hex + (i * 2), "%02x", hash[i]);
    }
    hex[DNAUTH_HASH_LEN] = '\0';

    return hex;
}

char *dnauth_derive_key_codon(const dnauth_sequence_t *seq, const uint8_t *salt) {
    if (!seq || !salt) return NULL;

    /* Translate to protein first */
    char *protein = dnauth_sequence_translate(seq);
    if (!protein) return NULL;

    /* Concatenate salt + protein */
    size_t protein_len = strlen(protein);
    size_t total_len = DNAUTH_SALT_LEN + protein_len;
    uint8_t *data = malloc(total_len);
    if (!data) {
        free(protein);
        return NULL;
    }

    memcpy(data, salt, DNAUTH_SALT_LEN);
    memcpy(data + DNAUTH_SALT_LEN, protein, protein_len);
    free(protein);

    /* SHA-256 hash */
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data, total_len, hash);
    free(data);

    /* Convert to hex string */
    char *hex = malloc(DNAUTH_HASH_LEN + 1);
    if (!hex) return NULL;

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hex + (i * 2), "%02x", hash[i]);
    }
    hex[DNAUTH_HASH_LEN] = '\0';

    return hex;
}

char *dnauth_hash_sequence(const dnauth_sequence_t *seq,
                           const uint8_t *salt, size_t salt_len,
                           dnauth_kdf_t method) {
    if (!seq || !salt || salt_len == 0) return NULL;

    switch (method) {
        case DNAUTH_KDF_CODON:
            return dnauth_derive_key_codon(seq, salt);
        case DNAUTH_KDF_BINARY:
        default:
            return dnauth_derive_key_binary(seq, salt);
    }
}

/* ==============================================================================
 * Sequence Matching
 * ============================================================================== */

int dnauth_levenshtein_distance(const char *s1, const char *s2) {
    if (!s1 || !s2) return -1;

    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);

    /* SECURITY: Prevent integer overflow in allocation size calculation.
     * Limit each string to 50000 chars to prevent (len1+1)*(len2+1)*sizeof(int) overflow.
     * 50001 * 50001 * 4 = ~10GB which is too large anyway. */
    if (len1 > 50000 || len2 > 50000) {
        return -1;  /* Strings too long for safe comparison */
    }

    /* Use dynamic programming */
    int *matrix = malloc((len1 + 1) * (len2 + 1) * sizeof(int));
    if (!matrix) return -1;

    #define MAT(i, j) matrix[(i) * (len2 + 1) + (j)]

    for (size_t i = 0; i <= len1; i++) MAT(i, 0) = i;
    for (size_t j = 0; j <= len2; j++) MAT(0, j) = j;

    for (size_t i = 1; i <= len1; i++) {
        for (size_t j = 1; j <= len2; j++) {
            int cost = (toupper(s1[i-1]) == toupper(s2[j-1])) ? 0 : 1;
            int del = MAT(i-1, j) + 1;
            int ins = MAT(i, j-1) + 1;
            int sub = MAT(i-1, j-1) + cost;

            MAT(i, j) = del;
            if (ins < MAT(i, j)) MAT(i, j) = ins;
            if (sub < MAT(i, j)) MAT(i, j) = sub;
        }
    }

    int distance = MAT(len1, len2);
    free(matrix);
    #undef MAT

    return distance;
}

dnauth_match_t *dnauth_match_sequences(const dnauth_sequence_t *seq1,
                                        const dnauth_sequence_t *seq2,
                                        int max_mutations) {
    if (!seq1 || !seq2) return NULL;

    dnauth_match_t *match = calloc(1, sizeof(dnauth_match_t));
    if (!match) return NULL;

    /* Calculate Levenshtein distance */
    int distance = dnauth_levenshtein_distance(seq1->nucleotides, seq2->nucleotides);

    match->mutations = distance;
    match->exact = (distance == 0);
    match->matched = (distance <= max_mutations);

    /* Calculate similarity */
    int max_len = (seq1->length > seq2->length) ? seq1->length : seq2->length;
    match->similarity = 1.0 - ((double)distance / max_len);

    /* Count mutation types (simplified) */
    if (seq1->length == seq2->length) {
        /* Same length: count substitutions */
        for (uint32_t i = 0; i < seq1->length; i++) {
            if (seq1->nucleotides[i] != seq2->nucleotides[i]) {
                match->substitutions++;
            }
        }
    } else if (seq1->length > seq2->length) {
        match->deletions = seq1->length - seq2->length;
        match->substitutions = distance - match->deletions;
    } else {
        match->insertions = seq2->length - seq1->length;
        match->substitutions = distance - match->insertions;
    }

    if (match->substitutions < 0) match->substitutions = 0;

    return match;
}

void dnauth_match_free(dnauth_match_t *match) {
    free(match);
}

/* ==============================================================================
 * System Lifecycle
 * ============================================================================== */

dnauth_system_t *dnauth_init(const char *storage_path) {
    dnauth_system_t *sys = calloc(1, sizeof(dnauth_system_t));
    if (!sys) return NULL;

    /* Set defaults */
    sys->default_mode = DNAUTH_MODE_EXACT;
    sys->default_kdf = DNAUTH_KDF_BINARY;
    sys->default_max_mutations = DNAUTH_MAX_MUTATIONS;
    sys->min_sequence_length = DNAUTH_MIN_SEQUENCE_LEN;
    sys->max_sequence_length = DNAUTH_MAX_SEQUENCE_LEN;
    sys->min_complexity = DNAUTH_COMPLEXITY_MEDIUM;
    sys->require_all_nucleotides = 1;
    sys->max_attempts_log = 1000;

    if (storage_path) {
        strncpy(sys->storage_path, storage_path, sizeof(sys->storage_path) - 1);
    }

    /* Initialize codon table */
    dnauth_init_codon_table(sys);

    /* OpenSSL RAND is auto-seeded, no need to manually seed */

    sys->initialized = 1;
    printf("[DNAuth] System initialized\n");

    return sys;
}

void dnauth_cleanup(dnauth_system_t *sys) {
    if (!sys) return;

    /* Free keys */
    dnauth_key_t *key = sys->keys;
    while (key) {
        dnauth_key_t *next = key->next;
        free(key);
        key = next;
    }

    /* Free attempts */
    dnauth_attempt_t *attempt = sys->attempts;
    while (attempt) {
        dnauth_attempt_t *next = attempt->next;
        free(attempt);
        attempt = next;
    }

    free(sys);
    printf("[DNAuth] System cleaned up\n");
}

/* ==============================================================================
 * Key Management
 * ============================================================================== */

dnauth_key_t *dnauth_get_key(dnauth_system_t *sys, const char *user_id) {
    if (!sys || !user_id) return NULL;

    dnauth_key_t *key = sys->keys;
    while (key) {
        if (strcmp(key->user_id, user_id) == 0) {
            return key;
        }
        key = key->next;
    }
    return NULL;
}

int dnauth_key_exists(dnauth_system_t *sys, const char *user_id) {
    return dnauth_get_key(sys, user_id) != NULL;
}

dnauth_result_t dnauth_register(dnauth_system_t *sys, const char *user_id,
                                 const char *sequence) {
    return dnauth_register_with_options(sys, user_id, sequence,
                                        sys->default_mode,
                                        sys->default_kdf,
                                        sys->default_max_mutations,
                                        0);  /* No expiration */
}

dnauth_result_t dnauth_register_with_options(dnauth_system_t *sys,
                                              const char *user_id,
                                              const char *sequence,
                                              dnauth_mode_t mode,
                                              dnauth_kdf_t kdf,
                                              int max_mutations,
                                              time_t expires) {
    if (!sys || !user_id || !sequence) return DNAUTH_ERR_INTERNAL;

    /* Check if user exists */
    if (dnauth_key_exists(sys, user_id)) {
        return DNAUTH_ERR_USER_EXISTS;
    }

    /* Parse and validate sequence */
    dnauth_sequence_t *seq = dnauth_sequence_parse(sequence);
    if (!seq) {
        return DNAUTH_ERR_INVALID_SEQUENCE;
    }

    /* Check complexity */
    if (seq->complexity < sys->min_complexity) {
        dnauth_sequence_free(seq);
        return DNAUTH_ERR_LOW_COMPLEXITY;
    }

    /* Create key */
    dnauth_key_t *key = calloc(1, sizeof(dnauth_key_t));
    if (!key) {
        dnauth_sequence_free(seq);
        return DNAUTH_ERR_INTERNAL;
    }

    key->key_id = sys->key_count + 1;
    strncpy(key->user_id, user_id, sizeof(key->user_id) - 1);
    key->kdf_method = kdf;
    key->auth_mode = mode;
    key->max_mutations = max_mutations;
    key->min_length = seq->length;
    key->created_at = time(NULL);
    key->expires_at = expires;

    /* Generate salt and hash */
    dnauth_generate_salt(key->salt, DNAUTH_SALT_LEN);
    char *hash = dnauth_hash_sequence(seq, key->salt, DNAUTH_SALT_LEN, kdf);
    if (!hash) {
        free(key);
        dnauth_sequence_free(seq);
        return DNAUTH_ERR_INTERNAL;
    }
    strncpy(key->key_hash, hash, DNAUTH_HASH_LEN);
    free(hash);

    /* Add to list */
    key->next = sys->keys;
    sys->keys = key;
    sys->key_count++;

    dnauth_sequence_free(seq);

    printf("[DNAuth] Registered user '%s' with %s mode\n", user_id, dnauth_mode_string(mode));

    /* Log to Governor for audit trail */
    dnauth_governor_log_registration(sys, user_id, mode);

    return DNAUTH_OK;
}

dnauth_result_t dnauth_revoke(dnauth_system_t *sys, const char *user_id,
                               const char *reason) {
    if (!sys || !user_id) return DNAUTH_ERR_INTERNAL;

    dnauth_key_t *key = dnauth_get_key(sys, user_id);
    if (!key) return DNAUTH_ERR_USER_NOT_FOUND;

    key->revoked = 1;
    if (reason) {
        strncpy(key->revoke_reason, reason, sizeof(key->revoke_reason) - 1);
    }

    printf("[DNAuth] Revoked key for user '%s': %s\n", user_id, reason ? reason : "No reason");

    /* Log to Governor for audit trail (revocation requires justification) */
    dnauth_governor_log_revocation(sys, user_id, reason);

    return DNAUTH_OK;
}

/* ==============================================================================
 * Authentication
 * ============================================================================== */

dnauth_result_t dnauth_authenticate(dnauth_system_t *sys, const char *user_id,
                                     const char *sequence) {
    return dnauth_authenticate_fuzzy(sys, user_id, sequence, 0, NULL);
}

dnauth_result_t dnauth_authenticate_fuzzy(dnauth_system_t *sys, const char *user_id,
                                           const char *sequence, int max_mutations,
                                           dnauth_match_t *match_result) {
    if (!sys || !user_id || !sequence) return DNAUTH_ERR_INTERNAL;

    /* Get key */
    dnauth_key_t *key = dnauth_get_key(sys, user_id);
    if (!key) {
        dnauth_log_attempt(sys, user_id, DNAUTH_ERR_USER_NOT_FOUND, "unknown");
        return DNAUTH_ERR_USER_NOT_FOUND;
    }

    /* Check if revoked */
    if (key->revoked) {
        dnauth_log_attempt(sys, user_id, DNAUTH_ERR_REVOKED, "unknown");
        return DNAUTH_ERR_REVOKED;
    }

    /* Check if expired */
    if (key->expires_at > 0 && time(NULL) > key->expires_at) {
        dnauth_log_attempt(sys, user_id, DNAUTH_ERR_EXPIRED, "unknown");
        return DNAUTH_ERR_EXPIRED;
    }

    /* Check lockout */
    if (key->lockout_until > 0 && time(NULL) < key->lockout_until) {
        return DNAUTH_ERR_LOCKED_OUT;
    }

    /* Parse input sequence */
    dnauth_sequence_t *seq = dnauth_sequence_parse(sequence);
    if (!seq) {
        key->failed_attempts++;
        if (key->failed_attempts >= DNAUTH_MAX_FAILED_ATTEMPTS) {
            key->lockout_until = time(NULL) + DNAUTH_LOCKOUT_SECS;
            if (sys->on_lockout) sys->on_lockout(user_id, sys->callback_data);
        }
        dnauth_log_attempt(sys, user_id, DNAUTH_ERR_INVALID_SEQUENCE, "unknown");
        return DNAUTH_ERR_INVALID_SEQUENCE;
    }

    /* Hash input with stored salt */
    char *input_hash = dnauth_hash_sequence(seq, key->salt, DNAUTH_SALT_LEN, key->kdf_method);
    dnauth_sequence_free(seq);

    if (!input_hash) {
        return DNAUTH_ERR_INTERNAL;
    }

    /* Compare hashes */
    int matched = (strcmp(input_hash, key->key_hash) == 0);
    free(input_hash);

    if (matched) {
        /* Success */
        key->failed_attempts = 0;
        key->lockout_until = 0;
        key->last_used = time(NULL);
        key->auth_count++;
        sys->total_auths++;
        sys->successful_auths++;

        if (match_result) {
            match_result->matched = 1;
            match_result->exact = 1;
            match_result->mutations = 0;
            match_result->similarity = 1.0;
        }

        dnauth_log_attempt(sys, user_id, DNAUTH_OK, "unknown");
        if (sys->on_auth_success) sys->on_auth_success(user_id, sys->callback_data);

        /* Log to Governor for audit trail */
        dnauth_governor_log_auth(sys, user_id, DNAUTH_OK, 0, 0);

        printf("[DNAuth] Authentication successful for '%s'\n", user_id);
        return DNAUTH_OK;
    }

    /* Failed */
    key->failed_attempts++;
    sys->total_auths++;
    sys->failed_auths++;

    if (key->failed_attempts >= DNAUTH_MAX_FAILED_ATTEMPTS) {
        key->lockout_until = time(NULL) + DNAUTH_LOCKOUT_SECS;
        if (sys->on_lockout) sys->on_lockout(user_id, sys->callback_data);

        /* Log lockout to Governor (rate limiting visibility) */
        dnauth_governor_log_lockout(sys, user_id, key->failed_attempts);

        printf("[DNAuth] User '%s' locked out after %d failed attempts\n",
               user_id, key->failed_attempts);
    }

    dnauth_log_attempt(sys, user_id, DNAUTH_ERR_NO_MATCH, "unknown");
    if (sys->on_auth_failure) sys->on_auth_failure(user_id, DNAUTH_ERR_NO_MATCH, sys->callback_data);

    /* Log failure to Governor for audit trail */
    dnauth_governor_log_auth(sys, user_id, DNAUTH_ERR_NO_MATCH, 0, 0);

    return DNAUTH_ERR_NO_MATCH;
}

/* ==============================================================================
 * Audit Logging
 * ============================================================================== */

void dnauth_log_attempt(dnauth_system_t *sys, const char *user_id,
                        dnauth_result_t result, const char *source) {
    if (!sys || !user_id) return;

    dnauth_attempt_t *attempt = calloc(1, sizeof(dnauth_attempt_t));
    if (!attempt) return;

    attempt->attempt_id = sys->attempt_count + 1;
    strncpy(attempt->user_id, user_id, sizeof(attempt->user_id) - 1);
    attempt->timestamp = time(NULL);
    attempt->result = result;
    if (source) strncpy(attempt->source_ip, source, sizeof(attempt->source_ip) - 1);

    attempt->next = sys->attempts;
    sys->attempts = attempt;
    sys->attempt_count++;

    /* Trim old attempts if needed */
    if (sys->attempt_count > sys->max_attempts_log) {
        /* Find and remove oldest */
        dnauth_attempt_t *prev = NULL;
        dnauth_attempt_t *curr = sys->attempts;
        while (curr && curr->next) {
            prev = curr;
            curr = curr->next;
        }
        if (prev) {
            prev->next = NULL;
            free(curr);
            sys->attempt_count--;
        }
    }
}

/* ==============================================================================
 * Utility Functions
 * ============================================================================== */

char *dnauth_generate_random_sequence(uint32_t length) {
    if (length < DNAUTH_MIN_SEQUENCE_LEN) length = DNAUTH_MIN_SEQUENCE_LEN;
    if (length > DNAUTH_MAX_SEQUENCE_LEN) length = DNAUTH_MAX_SEQUENCE_LEN;

    char *seq = malloc(length + 1);
    if (!seq) return NULL;

    const char nucleotides[] = "ATGC";

    /* Use secure random for DNA sequence generation */
    for (uint32_t i = 0; i < length; i++) {
        seq[i] = nucleotides[secure_random_range(4)];
    }
    seq[length] = '\0';

    return seq;
}

/* ==============================================================================
 * Evolution System - Utility Strings
 * ============================================================================== */

const char *dnauth_evolution_type_string(dnauth_evolution_type_t type) {
    switch (type) {
        case DNAUTH_EVO_POINT_MUTATION: return "Point Mutation";
        case DNAUTH_EVO_INSERTION: return "Insertion";
        case DNAUTH_EVO_DELETION: return "Deletion";
        case DNAUTH_EVO_TRANSVERSION: return "Transversion";
        case DNAUTH_EVO_TRANSITION: return "Transition";
        case DNAUTH_EVO_DUPLICATION: return "Duplication";
        case DNAUTH_EVO_INVERSION: return "Inversion";
        case DNAUTH_EVO_RECOMBINATION: return "Recombination";
        default: return "Unknown";
    }
}

const char *dnauth_pressure_string(dnauth_pressure_t pressure) {
    switch (pressure) {
        case DNAUTH_PRESSURE_NONE: return "None";
        case DNAUTH_PRESSURE_TIME: return "Time-based";
        case DNAUTH_PRESSURE_USAGE: return "Usage-based";
        case DNAUTH_PRESSURE_ENVIRONMENTAL: return "Environmental";
        case DNAUTH_PRESSURE_ADAPTIVE: return "Adaptive";
        default: return "Unknown";
    }
}

/* ==============================================================================
 * Evolution System - Mutation Engine
 * ============================================================================== */

/* Get a random nucleotide different from the given one */
static char get_different_nucleotide(char original) {
    const char nucleotides[] = "ATGC";
    char result;
    do {
        result = nucleotides[secure_random_range(4)];
    } while (result == toupper(original));
    return result;
}

/* Get transition partner (purine<->purine or pyrimidine<->pyrimidine) */
static char get_transition_partner(char c) {
    switch (toupper(c)) {
        case 'A': return 'G';  /* Purine -> Purine */
        case 'G': return 'A';
        case 'T': return 'C';  /* Pyrimidine -> Pyrimidine */
        case 'C': return 'T';
        default: return c;
    }
}

/* Get transversion partner (purine<->pyrimidine) */
static char get_transversion_partner(char c) {
    switch (toupper(c)) {
        case 'A': return secure_random_range(2) ? 'T' : 'C';  /* Purine -> Pyrimidine */
        case 'G': return secure_random_range(2) ? 'T' : 'C';
        case 'T': return secure_random_range(2) ? 'A' : 'G';  /* Pyrimidine -> Purine */
        case 'C': return secure_random_range(2) ? 'A' : 'G';
        default: return c;
    }
}

dnauth_mutation_t dnauth_generate_mutation(const char *sequence, uint32_t length,
                                            dnauth_evolution_type_t type) {
    dnauth_mutation_t mut = {0};
    mut.type = type;
    mut.timestamp = time(NULL);
    mut.position = secure_random_range(length);
    mut.original = sequence[mut.position];

    switch (type) {
        case DNAUTH_EVO_POINT_MUTATION:
            /* Random change to any different nucleotide */
            mut.mutated = get_different_nucleotide(mut.original);
            mut.fitness_impact = -0.05;  /* Slight fitness penalty */
            break;

        case DNAUTH_EVO_TRANSITION:
            /* Purine<->Purine or Pyrimidine<->Pyrimidine (less disruptive) */
            mut.mutated = get_transition_partner(mut.original);
            mut.fitness_impact = -0.02;  /* Minor penalty */
            break;

        case DNAUTH_EVO_TRANSVERSION:
            /* Purine<->Pyrimidine (more disruptive) */
            mut.mutated = get_transversion_partner(mut.original);
            mut.fitness_impact = -0.08;  /* Larger penalty */
            break;

        case DNAUTH_EVO_INSERTION:
            /* Mark as insertion - position indicates where to insert */
            mut.mutated = "ATGC"[secure_random_range(4)];
            mut.original = '-';  /* No original (insertion) */
            mut.fitness_impact = -0.10;
            break;

        case DNAUTH_EVO_DELETION:
            /* Mark as deletion */
            mut.mutated = '-';  /* Deleted */
            mut.fitness_impact = -0.10;
            break;

        case DNAUTH_EVO_DUPLICATION:
        case DNAUTH_EVO_INVERSION:
        case DNAUTH_EVO_RECOMBINATION:
            /* These are segment-level mutations - simplified to point for now */
            mut.mutated = get_different_nucleotide(mut.original);
            mut.fitness_impact = -0.15;
            break;

        default:
            mut.mutated = mut.original;
            mut.fitness_impact = 0;
            break;
    }

    return mut;
}

char *dnauth_apply_mutation(const char *sequence, dnauth_mutation_t *mutation) {
    if (!sequence || !mutation) return NULL;

    size_t len = strlen(sequence);
    char *result;

    switch (mutation->type) {
        case DNAUTH_EVO_INSERTION: {
            /* Insert a new nucleotide */
            result = malloc(len + 2);
            if (!result) return NULL;

            memcpy(result, sequence, mutation->position);
            result[mutation->position] = mutation->mutated;
            memcpy(result + mutation->position + 1,
                   sequence + mutation->position,
                   len - mutation->position + 1);
            break;
        }

        case DNAUTH_EVO_DELETION: {
            /* Delete a nucleotide */
            if (len <= DNAUTH_MIN_SEQUENCE_LEN) {
                /* Can't delete if at minimum length - do substitution instead */
                result = strdup(sequence);
                if (!result) return NULL;
                result[mutation->position] = get_different_nucleotide(sequence[mutation->position]);
            } else {
                result = malloc(len);
                if (!result) return NULL;
                memcpy(result, sequence, mutation->position);
                memcpy(result + mutation->position,
                       sequence + mutation->position + 1,
                       len - mutation->position);
            }
            break;
        }

        default: {
            /* Point mutation / substitution */
            result = strdup(sequence);
            if (!result) return NULL;
            if (mutation->position < len) {
                result[mutation->position] = mutation->mutated;
            }
            break;
        }
    }

    return result;
}

char *dnauth_apply_mutations(const char *sequence, dnauth_mutation_t *mutations, int count) {
    if (!sequence || !mutations || count <= 0) return strdup(sequence);

    char *current = strdup(sequence);
    if (!current) return NULL;

    for (int i = 0; i < count; i++) {
        char *next = dnauth_apply_mutation(current, &mutations[i]);
        free(current);
        if (!next) return NULL;
        current = next;
    }

    return current;
}

int dnauth_count_differences(const char *seq1, const char *seq2) {
    if (!seq1 || !seq2) return -1;
    return dnauth_levenshtein_distance(seq1, seq2);
}

/* ==============================================================================
 * Evolution System - Lineage Management
 * ============================================================================== */

dnauth_lineage_t *dnauth_lineage_create(dnauth_system_t *sys, const char *user_id,
                                         const char *initial_sequence) {
    if (!sys || !user_id || !initial_sequence) return NULL;

    /* Validate sequence */
    char error[256];
    if (!dnauth_sequence_validate(initial_sequence, error, sizeof(error))) {
        fprintf(stderr, "[DNAuth Evolution] Invalid sequence: %s\n", error);
        return NULL;
    }

    char *normalized = dnauth_sequence_normalize(initial_sequence);
    if (!normalized) return NULL;

    /* Create lineage */
    dnauth_lineage_t *lineage = calloc(1, sizeof(dnauth_lineage_t));
    if (!lineage) {
        free(normalized);
        return NULL;
    }

    strncpy(lineage->user_id, user_id, sizeof(lineage->user_id) - 1);
    lineage->origin_id = 1;
    lineage->current_gen = 1;
    lineage->total_generations = 1;

    /* Set evolution defaults */
    lineage->pressure = sys->default_pressure;
    lineage->mutation_rate = sys->default_mutation_rate > 0 ?
                             sys->default_mutation_rate : DNAUTH_MUTATION_RATE;
    lineage->evolution_interval_secs = sys->default_evolution_interval > 0 ?
                                       sys->default_evolution_interval : DNAUTH_EVOLUTION_INTERVAL;
    lineage->next_evolution = time(NULL) + lineage->evolution_interval_secs;

    /* Ancestor auth settings */
    lineage->allow_ancestor_auth = sys->default_allow_ancestors;
    lineage->max_ancestor_depth = sys->default_max_ancestor_depth > 0 ?
                                  sys->default_max_ancestor_depth : DNAUTH_MAX_ANCESTOR_GENS;
    lineage->ancestor_penalty = DNAUTH_ANCESTOR_PENALTY;

    /* Create first generation */
    dnauth_generation_t *gen = calloc(1, sizeof(dnauth_generation_t));
    if (!gen) {
        free(normalized);
        free(lineage);
        return NULL;
    }

    gen->generation_id = 1;
    gen->parent_id = 0;  /* No parent - origin */
    gen->created_at = time(NULL);
    gen->sequence = normalized;
    gen->sequence_length = strlen(normalized);
    gen->is_active = 1;
    gen->fitness_score = 1.0;  /* Start with full fitness */

    /* Hash the sequence */
    dnauth_sequence_t *seq = dnauth_sequence_parse(normalized);
    if (seq) {
        uint8_t salt[DNAUTH_SALT_LEN];
        dnauth_generate_salt(salt, DNAUTH_SALT_LEN);
        char *hash = dnauth_hash_sequence(seq, salt, DNAUTH_SALT_LEN, DNAUTH_KDF_BINARY);
        if (hash) {
            strncpy(gen->sequence_hash, hash, DNAUTH_HASH_LEN);
            free(hash);
        }
        dnauth_sequence_free(seq);
    }

    lineage->generations = gen;
    lineage->current = gen;
    lineage->cumulative_fitness = 1.0;

    /* Add to system */
    lineage->next = sys->lineages;
    sys->lineages = lineage;
    sys->lineage_count++;

    printf("[DNAuth Evolution] Created lineage for user '%s' with %u nucleotides\n",
           user_id, gen->sequence_length);

    return lineage;
}

dnauth_lineage_t *dnauth_lineage_get(dnauth_system_t *sys, const char *user_id) {
    if (!sys || !user_id) return NULL;

    dnauth_lineage_t *lineage = sys->lineages;
    while (lineage) {
        if (strcmp(lineage->user_id, user_id) == 0) {
            return lineage;
        }
        lineage = lineage->next;
    }
    return NULL;
}

void dnauth_lineage_free(dnauth_lineage_t *lineage) {
    if (!lineage) return;

    /* Free all generations */
    dnauth_generation_t *gen = lineage->generations;
    while (gen) {
        dnauth_generation_t *next = gen->next;
        free(gen->sequence);
        free(gen);
        gen = next;
    }

    free(lineage);
}

int dnauth_lineage_get_depth(dnauth_lineage_t *lineage) {
    if (!lineage) return 0;
    return lineage->total_generations;
}

/* ==============================================================================
 * Evolution System - Core Evolution Operations
 * ============================================================================== */

int dnauth_check_evolution_due(dnauth_system_t *sys, const char *user_id) {
    if (!sys || !user_id) return 0;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (!lineage) return 0;

    return (time(NULL) >= lineage->next_evolution);
}

void dnauth_schedule_evolution(dnauth_system_t *sys, const char *user_id, time_t when) {
    if (!sys || !user_id) return;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (!lineage) return;

    lineage->next_evolution = when;
}

dnauth_evolution_event_t *dnauth_evolve(dnauth_system_t *sys, const char *user_id) {
    if (!sys || !user_id) return NULL;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (!lineage || !lineage->current || !lineage->current->sequence) {
        return NULL;
    }

    /* Check if we've hit max generations */
    if (lineage->total_generations >= DNAUTH_MAX_GENERATIONS) {
        printf("[DNAuth Evolution] User '%s' at max generations (%d)\n",
               user_id, DNAUTH_MAX_GENERATIONS);
        return NULL;
    }

    /* Determine number of mutations based on rate */
    int num_mutations = 0;
    for (int i = 0; i < DNAUTH_MAX_MUTATIONS_PER_GEN; i++) {
        if (secure_random_double() < lineage->mutation_rate) {
            num_mutations++;
        }
    }
    if (num_mutations == 0) num_mutations = 1;  /* At least one mutation per evolution */

    return dnauth_evolve_forced(sys, user_id, num_mutations);
}

dnauth_evolution_event_t *dnauth_evolve_forced(dnauth_system_t *sys, const char *user_id,
                                                int num_mutations) {
    if (!sys || !user_id || num_mutations <= 0) return NULL;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (!lineage || !lineage->current || !lineage->current->sequence) {
        return NULL;
    }

    dnauth_generation_t *current_gen = lineage->current;
    char *current_seq = current_gen->sequence;
    uint32_t seq_len = current_gen->sequence_length;

    /* Create evolution event */
    dnauth_evolution_event_t *event = calloc(1, sizeof(dnauth_evolution_event_t));
    if (!event) return NULL;

    event->event_id = sys->evolution_event_count + 1;
    strncpy(event->user_id, user_id, sizeof(event->user_id) - 1);
    event->from_generation = current_gen->generation_id;
    event->timestamp = time(NULL);
    event->fitness_before = current_gen->fitness_score;

    /* Generate mutations */
    dnauth_evolution_type_t types[] = {
        DNAUTH_EVO_POINT_MUTATION,
        DNAUTH_EVO_TRANSITION,
        DNAUTH_EVO_TRANSVERSION,
        DNAUTH_EVO_INSERTION,
        DNAUTH_EVO_DELETION
    };
    int num_types = sizeof(types) / sizeof(types[0]);

    if (num_mutations > DNAUTH_MAX_MUTATIONS_PER_GEN) {
        num_mutations = DNAUTH_MAX_MUTATIONS_PER_GEN;
    }

    for (int i = 0; i < num_mutations; i++) {
        dnauth_evolution_type_t type = types[secure_random_range(num_types)];
        event->mutations[i] = dnauth_generate_mutation(current_seq, seq_len, type);
        event->mutation_count++;
    }

    /* Apply mutations to create new sequence */
    char *new_sequence = dnauth_apply_mutations(current_seq, event->mutations, event->mutation_count);
    if (!new_sequence) {
        free(event);
        return NULL;
    }

    /* Create new generation */
    dnauth_generation_t *new_gen = calloc(1, sizeof(dnauth_generation_t));
    if (!new_gen) {
        free(new_sequence);
        free(event);
        return NULL;
    }

    new_gen->generation_id = lineage->total_generations + 1;
    new_gen->parent_id = current_gen->generation_id;
    new_gen->created_at = time(NULL);
    new_gen->sequence = new_sequence;
    new_gen->sequence_length = strlen(new_sequence);
    new_gen->is_active = 1;

    /* Copy mutations to generation record */
    memcpy(new_gen->mutations, event->mutations, sizeof(event->mutations));
    new_gen->mutation_count = event->mutation_count;

    /* Calculate new fitness */
    double fitness_change = 0;
    for (int i = 0; i < event->mutation_count; i++) {
        fitness_change += event->mutations[i].fitness_impact;
    }
    new_gen->fitness_score = current_gen->fitness_score + fitness_change;
    if (new_gen->fitness_score < 0.1) new_gen->fitness_score = 0.1;  /* Minimum fitness */
    if (new_gen->fitness_score > 1.0) new_gen->fitness_score = 1.0;

    /* Hash new sequence */
    dnauth_sequence_t *seq = dnauth_sequence_parse(new_sequence);
    if (seq) {
        uint8_t salt[DNAUTH_SALT_LEN];
        dnauth_generate_salt(salt, DNAUTH_SALT_LEN);
        char *hash = dnauth_hash_sequence(seq, salt, DNAUTH_SALT_LEN, DNAUTH_KDF_BINARY);
        if (hash) {
            strncpy(new_gen->sequence_hash, hash, DNAUTH_HASH_LEN);
            free(hash);
        }
        dnauth_sequence_free(seq);
    }

    /* Update lineage */
    current_gen->is_active = 0;
    current_gen->evolved_at = time(NULL);
    new_gen->next = lineage->generations;
    lineage->generations = new_gen;
    lineage->current = new_gen;
    lineage->current_gen = new_gen->generation_id;
    lineage->total_generations++;
    lineage->total_mutations += event->mutation_count;
    lineage->cumulative_fitness = new_gen->fitness_score;
    lineage->next_evolution = time(NULL) + lineage->evolution_interval_secs;

    /* Complete event */
    event->to_generation = new_gen->generation_id;
    event->fitness_after = new_gen->fitness_score;

    /* Add to event log */
    event->next = sys->evolution_events;
    sys->evolution_events = event;
    sys->evolution_event_count++;

    /* Trigger callback */
    if (sys->on_evolution) {
        sys->on_evolution(user_id, event, sys->callback_data);
    }

    printf("[DNAuth Evolution] User '%s' evolved: Gen %u -> Gen %u (%d mutations, fitness %.2f -> %.2f)\n",
           user_id, event->from_generation, event->to_generation,
           event->mutation_count, event->fitness_before, event->fitness_after);

    /* Log evolution to Governor (forced evolution = admin action) */
    dnauth_governor_log_evolution(sys, user_id,
                                   event->from_generation, event->to_generation,
                                   event->mutation_count, (num_mutations > 0));

    return event;
}

/* ==============================================================================
 * Evolution System - Ancestor Authentication
 * ============================================================================== */

dnauth_generation_t *dnauth_get_ancestor(dnauth_lineage_t *lineage, int generations_back) {
    if (!lineage || generations_back < 0) return NULL;
    if (generations_back == 0) return lineage->current;

    /* Walk back through generations */
    dnauth_generation_t *gen = lineage->current;
    int steps = 0;
    while (gen && steps < generations_back) {
        /* Find parent generation */
        uint32_t parent_id = gen->parent_id;
        if (parent_id == 0) return gen;  /* At origin */

        dnauth_generation_t *parent = lineage->generations;
        while (parent) {
            if (parent->generation_id == parent_id) {
                gen = parent;
                break;
            }
            parent = parent->next;
        }
        steps++;
    }
    return gen;
}

double dnauth_ancestor_penalty(dnauth_system_t *sys, int generations_back) {
    if (!sys || generations_back <= 0) return 0.0;
    return generations_back * DNAUTH_ANCESTOR_PENALTY;
}

dnauth_result_t dnauth_authenticate_ancestor(dnauth_system_t *sys, const char *user_id,
                                              const char *sequence, int max_generations_back,
                                              int *generation_matched) {
    if (!sys || !user_id || !sequence) return DNAUTH_ERR_INTERNAL;
    if (generation_matched) *generation_matched = -1;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (!lineage) {
        return DNAUTH_ERR_USER_NOT_FOUND;
    }

    if (!lineage->allow_ancestor_auth) {
        return DNAUTH_ERR_EXPIRED;  /* Ancestor auth not allowed */
    }

    /* Limit how far back we check */
    if (max_generations_back > lineage->max_ancestor_depth) {
        max_generations_back = lineage->max_ancestor_depth;
    }

    /* Normalize input */
    char *input_normalized = dnauth_sequence_normalize(sequence);
    if (!input_normalized) return DNAUTH_ERR_INVALID_SEQUENCE;

    /* Try each generation from current back */
    for (int back = 0; back <= max_generations_back; back++) {
        dnauth_generation_t *gen = dnauth_get_ancestor(lineage, back);
        if (!gen || gen->is_extinct) continue;

        /* Compare sequences */
        if (strcmp(input_normalized, gen->sequence) == 0) {
            free(input_normalized);
            if (generation_matched) *generation_matched = back;

            /* Update stats */
            gen->auth_count++;
            lineage->total_auths++;

            if (back == 0) {
                printf("[DNAuth] Ancestor auth for '%s': matched current generation\n", user_id);
            } else {
                printf("[DNAuth] Ancestor auth for '%s': matched %d generation(s) back (penalty: %.0f%%)\n",
                       user_id, back, dnauth_ancestor_penalty(sys, back) * 100);
            }

            /* Log ancestor auth to Governor */
            dnauth_governor_log_auth(sys, user_id, DNAUTH_OK, 1, back);

            return DNAUTH_OK;
        }
    }

    free(input_normalized);

    /* Update failed count */
    if (lineage->current) {
        lineage->current->failed_count++;
    }

    /* Log failure to Governor */
    dnauth_governor_log_auth(sys, user_id, DNAUTH_ERR_NO_MATCH, 1, -1);

    return DNAUTH_ERR_NO_MATCH;
}

/* ==============================================================================
 * Evolution System - Fitness Calculation
 * ============================================================================== */

double dnauth_calculate_fitness(dnauth_system_t *sys, dnauth_lineage_t *lineage) {
    if (!sys || !lineage || !lineage->current) return 0.0;

    double fitness = lineage->current->fitness_score;

    /* Apply pressure adjustments */
    switch (lineage->pressure) {
        case DNAUTH_PRESSURE_USAGE: {
            /* Fitness increases with authentication frequency */
            if (lineage->total_auths > 0) {
                double usage_bonus = (double)lineage->current->auth_count / 100.0;
                if (usage_bonus > 0.2) usage_bonus = 0.2;
                fitness += usage_bonus;
            }
            break;
        }
        case DNAUTH_PRESSURE_TIME: {
            /* Check if evolution is overdue */
            if (time(NULL) > lineage->next_evolution) {
                time_t overdue = time(NULL) - lineage->next_evolution;
                int periods_overdue = overdue / lineage->evolution_interval_secs;
                fitness -= periods_overdue * DNAUTH_FITNESS_DECAY;
            }
            break;
        }
        case DNAUTH_PRESSURE_ADAPTIVE: {
            /* Penalize for failed auth attempts */
            if (lineage->current->failed_count > 0) {
                fitness -= lineage->current->failed_count * 0.02;
            }
            break;
        }
        default:
            break;
    }

    /* Clamp fitness */
    if (fitness < 0.0) fitness = 0.0;
    if (fitness > 1.0) fitness = 1.0;

    return fitness;
}

void dnauth_update_fitness(dnauth_system_t *sys, const char *user_id) {
    if (!sys || !user_id) return;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (!lineage) return;

    double new_fitness = dnauth_calculate_fitness(sys, lineage);
    if (lineage->current) {
        lineage->current->fitness_score = new_fitness;
    }
    lineage->cumulative_fitness = new_fitness;

    /* Trigger warning callback if fitness is low */
    if (new_fitness < 0.3 && sys->on_fitness_warning) {
        sys->on_fitness_warning(user_id, new_fitness, sys->callback_data);
    }
}

double dnauth_get_fitness(dnauth_system_t *sys, const char *user_id) {
    if (!sys || !user_id) return 0.0;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (!lineage) return 0.0;

    return dnauth_calculate_fitness(sys, lineage);
}

int dnauth_is_fit(dnauth_system_t *sys, const char *user_id, double threshold) {
    return dnauth_get_fitness(sys, user_id) >= threshold;
}

/* ==============================================================================
 * Evolution System - Generation Queries
 * ============================================================================== */

dnauth_generation_t *dnauth_get_current_generation(dnauth_system_t *sys, const char *user_id) {
    if (!sys || !user_id) return NULL;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (!lineage) return NULL;

    return lineage->current;
}

dnauth_generation_t *dnauth_get_generation(dnauth_lineage_t *lineage, uint32_t gen_id) {
    if (!lineage) return NULL;

    dnauth_generation_t *gen = lineage->generations;
    while (gen) {
        if (gen->generation_id == gen_id) {
            return gen;
        }
        gen = gen->next;
    }
    return NULL;
}

char *dnauth_get_current_sequence(dnauth_system_t *sys, const char *user_id) {
    dnauth_generation_t *gen = dnauth_get_current_generation(sys, user_id);
    if (!gen || !gen->sequence) return NULL;
    return strdup(gen->sequence);
}

int dnauth_get_generation_number(dnauth_system_t *sys, const char *user_id) {
    if (!sys || !user_id) return 0;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (!lineage) return 0;

    return lineage->current_gen;
}

/* ==============================================================================
 * Evolution System - History and Notification
 * ============================================================================== */

dnauth_evolution_event_t *dnauth_get_evolution_history(dnauth_system_t *sys,
                                                        const char *user_id, int limit) {
    if (!sys || !user_id) return NULL;

    /* Find events for this user */
    dnauth_evolution_event_t *result = NULL;
    dnauth_evolution_event_t *last = NULL;
    int count = 0;

    dnauth_evolution_event_t *event = sys->evolution_events;
    while (event && (limit <= 0 || count < limit)) {
        if (strcmp(event->user_id, user_id) == 0) {
            /* Copy event to result list */
            dnauth_evolution_event_t *copy = malloc(sizeof(dnauth_evolution_event_t));
            if (copy) {
                memcpy(copy, event, sizeof(dnauth_evolution_event_t));
                copy->next = NULL;
                if (last) {
                    last->next = copy;
                } else {
                    result = copy;
                }
                last = copy;
                count++;
            }
        }
        event = event->next;
    }

    return result;
}

char *dnauth_format_mutation_notice(dnauth_mutation_t *mutation) {
    if (!mutation) return NULL;

    char *notice = malloc(256);
    if (!notice) return NULL;

    snprintf(notice, 256, "%s at position %u: %c -> %c (fitness impact: %.2f)",
             dnauth_evolution_type_string(mutation->type),
             mutation->position,
             mutation->original,
             mutation->mutated,
             mutation->fitness_impact);

    return notice;
}

char *dnauth_format_evolution_notice(dnauth_evolution_event_t *event) {
    if (!event) return NULL;

    char *notice = malloc(1024);
    if (!notice) return NULL;

    char mutations_desc[512] = "";
    for (int i = 0; i < event->mutation_count; i++) {
        char *mut_str = dnauth_format_mutation_notice(&event->mutations[i]);
        if (mut_str) {
            if (i > 0) strcat(mutations_desc, "; ");
            strncat(mutations_desc, mut_str, sizeof(mutations_desc) - strlen(mutations_desc) - 1);
            free(mut_str);
        }
    }

    snprintf(notice, 1024,
             "Evolution Event: Generation %u -> %u\n"
             "Mutations: %d\n"
             "Details: %s\n"
             "Fitness: %.2f -> %.2f",
             event->from_generation, event->to_generation,
             event->mutation_count,
             mutations_desc,
             event->fitness_before, event->fitness_after);

    return notice;
}

char *dnauth_describe_evolution(dnauth_evolution_event_t *event) {
    return dnauth_format_evolution_notice(event);
}

void dnauth_send_evolution_notification(dnauth_system_t *sys, const char *user_id,
                                         dnauth_evolution_event_t *event) {
    if (!sys || !user_id || !event) return;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (!lineage || !lineage->notify_on_evolution) return;

    char *notice = dnauth_format_evolution_notice(event);
    if (notice) {
        strncpy(event->notification_text, notice, sizeof(event->notification_text) - 1);
        event->notified = 1;
        printf("[DNAuth Notification] %s: %s\n", user_id, notice);
        free(notice);
    }
}

/* ==============================================================================
 * Evolution System - Evolution Daemon
 * ============================================================================== */

int dnauth_evolution_init(dnauth_system_t *sys) {
    if (!sys) return 0;

    /* Set default evolution settings if not configured */
    if (sys->default_mutation_rate <= 0) {
        sys->default_mutation_rate = DNAUTH_MUTATION_RATE;
    }
    if (sys->default_evolution_interval <= 0) {
        sys->default_evolution_interval = DNAUTH_EVOLUTION_INTERVAL;
    }
    if (sys->evolution_check_interval <= 0) {
        sys->evolution_check_interval = 3600;  /* Check hourly */
    }
    if (sys->max_evolution_events <= 0) {
        sys->max_evolution_events = 10000;
    }

    sys->default_pressure = DNAUTH_PRESSURE_TIME;
    sys->default_allow_ancestors = 1;
    sys->default_max_ancestor_depth = DNAUTH_MAX_ANCESTOR_GENS;

    printf("[DNAuth Evolution] Evolution system initialized\n");
    printf("  - Mutation rate: %.1f%%\n", sys->default_mutation_rate * 100);
    printf("  - Evolution interval: %d seconds\n", sys->default_evolution_interval);
    printf("  - Max ancestor depth: %d generations\n", sys->default_max_ancestor_depth);

    return 1;
}

void dnauth_evolution_cleanup(dnauth_system_t *sys) {
    if (!sys) return;

    /* Stop daemon if running */
    dnauth_evolution_daemon_stop(sys);

    /* Free lineages */
    dnauth_lineage_t *lineage = sys->lineages;
    while (lineage) {
        dnauth_lineage_t *next = lineage->next;
        dnauth_lineage_free(lineage);
        lineage = next;
    }
    sys->lineages = NULL;
    sys->lineage_count = 0;

    /* Free evolution events */
    dnauth_evolution_event_t *event = sys->evolution_events;
    while (event) {
        dnauth_evolution_event_t *next = event->next;
        free(event);
        event = next;
    }
    sys->evolution_events = NULL;
    sys->evolution_event_count = 0;

    printf("[DNAuth Evolution] Evolution system cleaned up\n");
}

void dnauth_evolution_enable(dnauth_system_t *sys, int enable) {
    if (!sys) return;
    sys->evolution_enabled = enable;
    printf("[DNAuth Evolution] Evolution %s\n", enable ? "enabled" : "disabled");
}

int dnauth_evolution_daemon_start(dnauth_system_t *sys) {
    if (!sys) return 0;

    if (!sys->evolution_enabled) {
        printf("[DNAuth Evolution] Cannot start daemon - evolution not enabled\n");
        return 0;
    }

    sys->evolution_daemon_running = 1;
    sys->last_evolution_check = time(NULL);
    printf("[DNAuth Evolution] Daemon started\n");
    return 1;
}

void dnauth_evolution_daemon_stop(dnauth_system_t *sys) {
    if (!sys) return;

    sys->evolution_daemon_running = 0;
    printf("[DNAuth Evolution] Daemon stopped\n");
}

void dnauth_evolution_daemon_tick(dnauth_system_t *sys) {
    if (!sys || !sys->evolution_enabled || !sys->evolution_daemon_running) return;

    time_t now = time(NULL);

    /* Only check at configured interval */
    if (now - sys->last_evolution_check < sys->evolution_check_interval) {
        return;
    }
    sys->last_evolution_check = now;

    /* Check each lineage for due evolutions */
    dnauth_lineage_t *lineage = sys->lineages;
    while (lineage) {
        if (lineage->pressure != DNAUTH_PRESSURE_NONE &&
            dnauth_check_evolution_due(sys, lineage->user_id)) {

            /* Trigger evolution */
            dnauth_evolution_event_t *event = dnauth_evolve(sys, lineage->user_id);
            if (event && lineage->notify_on_evolution) {
                dnauth_send_evolution_notification(sys, lineage->user_id, event);
            }
        }

        /* Update fitness */
        dnauth_update_fitness(sys, lineage->user_id);

        lineage = lineage->next;
    }
}

/* ==============================================================================
 * Evolution System - Configuration
 * ============================================================================== */

void dnauth_set_evolution_interval(dnauth_system_t *sys, const char *user_id, int seconds) {
    if (!sys || !user_id || seconds <= 0) return;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (lineage) {
        lineage->evolution_interval_secs = seconds;
        lineage->next_evolution = time(NULL) + seconds;
    }
}

void dnauth_set_mutation_rate(dnauth_system_t *sys, const char *user_id, double rate) {
    if (!sys || !user_id || rate < 0 || rate > 1.0) return;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (lineage) {
        lineage->mutation_rate = rate;
    }
}

void dnauth_set_pressure(dnauth_system_t *sys, const char *user_id, dnauth_pressure_t pressure) {
    if (!sys || !user_id) return;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (lineage) {
        lineage->pressure = pressure;
    }
}

void dnauth_set_ancestor_policy(dnauth_system_t *sys, const char *user_id,
                                 int allow, int max_depth, double penalty) {
    if (!sys || !user_id) return;

    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, user_id);
    if (lineage) {
        lineage->allow_ancestor_auth = allow;
        if (max_depth > 0) lineage->max_ancestor_depth = max_depth;
        if (penalty >= 0) lineage->ancestor_penalty = penalty;
    }
}

/* ==============================================================================
 * Governor Integration (Partial - Audit Logging Only)
 * ==============================================================================
 *
 * The Governor integration for DNAuth is PARTIAL by design.
 * DNA sequences are NOT code - they are credentials.
 * We use the Governor for:
 *   - Immutable audit logging to GeoFS
 *   - Rate limiting visibility
 *   - Policy enforcement transparency
 *
 * We do NOT use the Governor to:
 *   - Evaluate DNA sequences as "code"
 *   - Block compliant authentication
 *   - Analyze sequences for "destructive patterns"
 */

/* String representation of log types */
const char *dnauth_log_type_string(dnauth_log_type_t type) {
    switch (type) {
        case DNAUTH_LOG_REGISTRATION:    return "REGISTRATION";
        case DNAUTH_LOG_REVOCATION:      return "REVOCATION";
        case DNAUTH_LOG_AUTH_SUCCESS:    return "AUTH_SUCCESS";
        case DNAUTH_LOG_AUTH_FAILURE:    return "AUTH_FAILURE";
        case DNAUTH_LOG_LOCKOUT:         return "LOCKOUT";
        case DNAUTH_LOG_EVOLUTION:       return "EVOLUTION";
        case DNAUTH_LOG_FORCED_EVOLUTION: return "FORCED_EVOLUTION";
        case DNAUTH_LOG_ANCESTOR_AUTH:   return "ANCESTOR_AUTH";
        case DNAUTH_LOG_KEY_CHANGE:      return "KEY_CHANGE";
        default:                         return "UNKNOWN";
    }
}

/* Set Governor for audit logging */
void dnauth_set_governor(dnauth_system_t *sys, struct phantom_governor *gov) {
    if (!sys) return;
    sys->governor = gov;

    if (gov) {
        printf("[DNAuth] Governor integration enabled for audit logging\n");
    } else {
        printf("[DNAuth] Governor integration disabled\n");
    }
}

/*
 * Internal: Log DNAuth event to Governor
 *
 * This creates a Governor log entry for the DNAuth event.
 * The Governor logs to GeoFS, providing immutable audit trail.
 */
static void dnauth_governor_log(dnauth_system_t *sys,
                                 dnauth_log_type_t log_type,
                                 const char *user_id,
                                 const char *details) {
    if (!sys || !sys->governor) return;  /* No governor = no logging */

    /* Create a minimal eval request/response for logging */
    governor_eval_request_t request = {0};
    governor_eval_response_t response = {0};

    /* Set up request with DNAuth event info */
    snprintf(request.name, sizeof(request.name), "DNAuth:%s",
             dnauth_log_type_string(log_type));
    snprintf(request.description, sizeof(request.description),
             "DNAuth event for user '%s': %s",
             user_id ? user_id : "unknown",
             details ? details : "no details");

    /* For DNAuth events, we always "approve" logging (not code evaluation) */
    response.decision = GOVERNOR_APPROVE;
    snprintf(response.summary, sizeof(response.summary),
             "[DNAuth Audit] %s", dnauth_log_type_string(log_type));
    snprintf(response.reasoning, sizeof(response.reasoning),
             "DNAuth audit log entry - credential operation, not code evaluation");
    snprintf(response.decision_by, sizeof(response.decision_by), "dnauth");
    response.approved_at = time(NULL);

    /* Log to Governor (which logs to GeoFS) */
    governor_log_decision(sys->governor, &request, &response);
}

/*
 * Log registration event to Governor
 */
static void dnauth_governor_log_registration(dnauth_system_t *sys,
                                              const char *user_id,
                                              dnauth_mode_t mode) {
    char details[512];
    snprintf(details, sizeof(details),
             "New key registered with mode '%s'",
             dnauth_mode_string(mode));
    dnauth_governor_log(sys, DNAUTH_LOG_REGISTRATION, user_id, details);
}

/*
 * Log revocation event to Governor
 */
static void dnauth_governor_log_revocation(dnauth_system_t *sys,
                                            const char *user_id,
                                            const char *reason) {
    char details[512];
    snprintf(details, sizeof(details),
             "Key revoked - Reason: %s",
             reason ? reason : "No reason provided");
    dnauth_governor_log(sys, DNAUTH_LOG_REVOCATION, user_id, details);
}

/*
 * Log authentication event to Governor
 */
static void dnauth_governor_log_auth(dnauth_system_t *sys,
                                      const char *user_id,
                                      dnauth_result_t result,
                                      int is_ancestor,
                                      int generation_back) {
    char details[512];

    if (result == DNAUTH_OK) {
        if (is_ancestor && generation_back > 0) {
            snprintf(details, sizeof(details),
                     "Ancestor authentication successful (%d generation(s) back)",
                     generation_back);
            dnauth_governor_log(sys, DNAUTH_LOG_ANCESTOR_AUTH, user_id, details);
        } else {
            snprintf(details, sizeof(details),
                     "Authentication successful");
            dnauth_governor_log(sys, DNAUTH_LOG_AUTH_SUCCESS, user_id, details);
        }
    } else {
        snprintf(details, sizeof(details),
                 "Authentication failed - %s",
                 dnauth_result_string(result));
        dnauth_governor_log(sys, DNAUTH_LOG_AUTH_FAILURE, user_id, details);
    }
}

/*
 * Log lockout event to Governor
 */
static void dnauth_governor_log_lockout(dnauth_system_t *sys,
                                         const char *user_id,
                                         int failed_attempts) {
    char details[512];
    snprintf(details, sizeof(details),
             "Account locked after %d failed attempts",
             failed_attempts);
    dnauth_governor_log(sys, DNAUTH_LOG_LOCKOUT, user_id, details);
}

/*
 * Log evolution event to Governor
 */
static void dnauth_governor_log_evolution(dnauth_system_t *sys,
                                           const char *user_id,
                                           int from_gen,
                                           int to_gen,
                                           int mutation_count,
                                           int is_forced) {
    char details[512];
    snprintf(details, sizeof(details),
             "%s Gen %d -> Gen %d (%d mutations)",
             is_forced ? "Forced evolution:" : "Natural evolution:",
             from_gen, to_gen, mutation_count);
    dnauth_governor_log(sys,
                        is_forced ? DNAUTH_LOG_FORCED_EVOLUTION : DNAUTH_LOG_EVOLUTION,
                        user_id, details);
}
