/*
 * DNAuth Test Suite
 * Tests for DNA-based authentication with evolution system
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "phantom_dnauth.h"

#define TEST_PASS "\033[32mPASS\033[0m"
#define TEST_FAIL "\033[31mFAIL\033[0m"

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(test) do { \
    printf("  Testing %s... ", #test); \
    tests_run++; \
    if (test()) { \
        printf("%s\n", TEST_PASS); \
        tests_passed++; \
    } else { \
        printf("%s\n", TEST_FAIL); \
    } \
} while(0)

/* ==============================================================================
 * Basic Sequence Tests
 * ============================================================================== */

static int test_sequence_validation(void) {
    char error[256];

    /* Valid sequences */
    if (!dnauth_sequence_validate("ATGCATGCATGC", error, sizeof(error))) return 0;
    if (!dnauth_sequence_validate("atgcatgcatgc", error, sizeof(error))) return 0;
    if (!dnauth_sequence_validate("ATG CAT GCA TGC", error, sizeof(error))) return 0;

    /* Invalid sequences */
    if (dnauth_sequence_validate("ATGXATGC", error, sizeof(error))) return 0;  /* Invalid char */
    if (dnauth_sequence_validate("ATG", error, sizeof(error))) return 0;       /* Too short */
    if (dnauth_sequence_validate("", error, sizeof(error))) return 0;          /* Empty */

    return 1;
}

static int test_sequence_parsing(void) {
    dnauth_sequence_t *seq = dnauth_sequence_parse("ATGCATGCATGC");
    if (!seq) return 0;

    if (seq->length != 12) { dnauth_sequence_free(seq); return 0; }
    if (seq->count_a != 3) { dnauth_sequence_free(seq); return 0; }
    if (seq->count_t != 3) { dnauth_sequence_free(seq); return 0; }
    if (seq->count_g != 3) { dnauth_sequence_free(seq); return 0; }
    if (seq->count_c != 3) { dnauth_sequence_free(seq); return 0; }

    dnauth_sequence_free(seq);
    return 1;
}

static int test_sequence_complement(void) {
    dnauth_sequence_t *seq = dnauth_sequence_parse("ATGCATGCATGC");
    if (!seq) return 0;

    char *comp = dnauth_sequence_complement(seq);
    if (!comp) { dnauth_sequence_free(seq); return 0; }

    /* A->T, T->A, G->C, C->G */
    if (strcmp(comp, "TACGTACGTACG") != 0) {
        free(comp);
        dnauth_sequence_free(seq);
        return 0;
    }

    free(comp);
    dnauth_sequence_free(seq);
    return 1;
}

static int test_sequence_transcribe(void) {
    dnauth_sequence_t *seq = dnauth_sequence_parse("ATGCATGCATGC");
    if (!seq) return 0;

    char *rna = dnauth_sequence_transcribe(seq);
    if (!rna) { dnauth_sequence_free(seq); return 0; }

    /* T->U for RNA */
    if (strcmp(rna, "AUGCAUGCAUGC") != 0) {
        free(rna);
        dnauth_sequence_free(seq);
        return 0;
    }

    free(rna);
    dnauth_sequence_free(seq);
    return 1;
}

/* ==============================================================================
 * Complexity Tests
 * ============================================================================== */

static int test_complexity_low(void) {
    /* Repeating sequence should be low complexity */
    dnauth_sequence_t *seq = dnauth_sequence_parse("AAAAAAAAAAAAAAAA");
    if (!seq) return 0;

    dnauth_complexity_t complexity = dnauth_compute_complexity(seq);
    dnauth_sequence_free(seq);

    return (complexity == DNAUTH_COMPLEXITY_LOW);
}

static int test_complexity_high(void) {
    /* Well-distributed sequence should be high complexity */
    dnauth_sequence_t *seq = dnauth_sequence_parse("ATGCTAGCATCGATCG");
    if (!seq) return 0;

    dnauth_complexity_t complexity = dnauth_compute_complexity(seq);
    dnauth_sequence_free(seq);

    return (complexity >= DNAUTH_COMPLEXITY_HIGH);
}

static int test_entropy(void) {
    /* Equal distribution should have max entropy (~2.0) */
    dnauth_sequence_t *seq = dnauth_sequence_parse("ATGCATGCATGCATGC");
    if (!seq) return 0;

    double entropy = dnauth_compute_entropy(seq);
    dnauth_sequence_free(seq);

    /* Should be close to 2.0 (max for 4 symbols) */
    return (entropy > 1.9 && entropy <= 2.0);
}

/* ==============================================================================
 * System and Authentication Tests
 * ============================================================================== */

static int test_system_init(void) {
    dnauth_system_t *sys = dnauth_init("/tmp/dnauth_test");
    if (!sys) return 0;
    if (!sys->initialized) { dnauth_cleanup(sys); return 0; }

    dnauth_cleanup(sys);
    return 1;
}

static int test_user_registration(void) {
    dnauth_system_t *sys = dnauth_init("/tmp/dnauth_test");
    if (!sys) return 0;

    /* Set lower complexity requirement for testing */
    sys->min_complexity = DNAUTH_COMPLEXITY_LOW;

    dnauth_result_t result = dnauth_register(sys, "testuser", "ATGCTAGCATCGATCG");
    if (result != DNAUTH_OK) {
        printf("(register failed: %s) ", dnauth_result_string(result));
        dnauth_cleanup(sys);
        return 0;
    }

    /* Duplicate registration should fail */
    result = dnauth_register(sys, "testuser", "ATGCTAGCATCGATCG");
    if (result != DNAUTH_ERR_USER_EXISTS) {
        dnauth_cleanup(sys);
        return 0;
    }

    dnauth_cleanup(sys);
    return 1;
}

static int test_authentication(void) {
    dnauth_system_t *sys = dnauth_init("/tmp/dnauth_test");
    if (!sys) return 0;

    sys->min_complexity = DNAUTH_COMPLEXITY_LOW;

    const char *sequence = "ATGCTAGCATCGATCG";
    dnauth_register(sys, "authuser", sequence);

    /* Correct sequence should authenticate */
    dnauth_result_t result = dnauth_authenticate(sys, "authuser", sequence);
    if (result != DNAUTH_OK) {
        printf("(auth failed: %s) ", dnauth_result_string(result));
        dnauth_cleanup(sys);
        return 0;
    }

    /* Wrong sequence should fail */
    result = dnauth_authenticate(sys, "authuser", "GGGGGGGGGGGGGGGG");
    if (result != DNAUTH_ERR_NO_MATCH) {
        dnauth_cleanup(sys);
        return 0;
    }

    dnauth_cleanup(sys);
    return 1;
}

/* ==============================================================================
 * Evolution System Tests
 * ============================================================================== */

static int test_evolution_init(void) {
    dnauth_system_t *sys = dnauth_init("/tmp/dnauth_test");
    if (!sys) return 0;

    int result = dnauth_evolution_init(sys);
    if (!result) {
        dnauth_cleanup(sys);
        return 0;
    }

    if (sys->default_mutation_rate <= 0) {
        dnauth_cleanup(sys);
        return 0;
    }

    dnauth_evolution_cleanup(sys);
    dnauth_cleanup(sys);
    return 1;
}

static int test_lineage_creation(void) {
    dnauth_system_t *sys = dnauth_init("/tmp/dnauth_test");
    if (!sys) return 0;

    dnauth_evolution_init(sys);

    const char *sequence = "ATGCTAGCATCGATCG";
    dnauth_lineage_t *lineage = dnauth_lineage_create(sys, "evouser", sequence);
    if (!lineage) {
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    /* Check lineage properties */
    if (lineage->total_generations != 1) {
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    if (!lineage->current || !lineage->current->sequence) {
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    if (strcmp(lineage->current->sequence, sequence) != 0) {
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    dnauth_evolution_cleanup(sys);
    dnauth_cleanup(sys);
    return 1;
}

static int test_mutation_generation(void) {
    const char *sequence = "ATGCTAGCATCGATCG";
    uint32_t len = strlen(sequence);

    /* Test point mutation */
    dnauth_mutation_t mut = dnauth_generate_mutation(sequence, len, DNAUTH_EVO_POINT_MUTATION);
    if (mut.type != DNAUTH_EVO_POINT_MUTATION) return 0;
    if (mut.position >= len) return 0;
    if (mut.mutated == mut.original) return 0;  /* Should be different */

    /* Test transition */
    mut = dnauth_generate_mutation(sequence, len, DNAUTH_EVO_TRANSITION);
    if (mut.type != DNAUTH_EVO_TRANSITION) return 0;

    return 1;
}

static int test_mutation_application(void) {
    const char *sequence = "ATGCTAGCATCGATCG";

    /* Create a mutation at position 0 */
    dnauth_mutation_t mut = {0};
    mut.type = DNAUTH_EVO_POINT_MUTATION;
    mut.position = 0;
    mut.original = 'A';
    mut.mutated = 'G';

    char *result = dnauth_apply_mutation(sequence, &mut);
    if (!result) return 0;

    if (result[0] != 'G') {
        free(result);
        return 0;
    }

    /* Rest should be unchanged */
    if (strncmp(result + 1, sequence + 1, strlen(sequence) - 1) != 0) {
        free(result);
        return 0;
    }

    free(result);
    return 1;
}

static int test_evolution_event(void) {
    dnauth_system_t *sys = dnauth_init("/tmp/dnauth_test");
    if (!sys) return 0;

    dnauth_evolution_init(sys);

    const char *sequence = "ATGCTAGCATCGATCG";
    dnauth_lineage_create(sys, "evolveuser", sequence);

    /* Force an evolution */
    dnauth_evolution_event_t *event = dnauth_evolve_forced(sys, "evolveuser", 1);
    if (!event) {
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    /* Check evolution occurred */
    if (event->mutation_count < 1) {
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    /* Check generation advanced */
    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, "evolveuser");
    if (!lineage || lineage->total_generations != 2) {
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    /* New sequence should differ from original */
    if (strcmp(lineage->current->sequence, sequence) == 0) {
        printf("(sequence didn't change) ");
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    dnauth_evolution_cleanup(sys);
    dnauth_cleanup(sys);
    return 1;
}

static int test_ancestor_authentication(void) {
    dnauth_system_t *sys = dnauth_init("/tmp/dnauth_test");
    if (!sys) return 0;

    dnauth_evolution_init(sys);

    const char *original_sequence = "ATGCTAGCATCGATCG";
    dnauth_lineage_create(sys, "ancestoruser", original_sequence);

    /* Evolve a few times */
    for (int i = 0; i < 3; i++) {
        dnauth_evolve_forced(sys, "ancestoruser", 1);
    }

    /* Should now be at generation 4 */
    dnauth_lineage_t *lineage = dnauth_lineage_get(sys, "ancestoruser");
    if (!lineage || lineage->total_generations != 4) {
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    /* Ancestor auth with original sequence should work */
    int gen_matched = -1;
    dnauth_result_t result = dnauth_authenticate_ancestor(sys, "ancestoruser",
                                                          original_sequence, 5, &gen_matched);
    if (result != DNAUTH_OK) {
        printf("(ancestor auth failed: %s) ", dnauth_result_string(result));
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    /* Should have matched 3 generations back */
    if (gen_matched != 3) {
        printf("(expected gen 3, got %d) ", gen_matched);
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    dnauth_evolution_cleanup(sys);
    dnauth_cleanup(sys);
    return 1;
}

static int test_fitness_calculation(void) {
    dnauth_system_t *sys = dnauth_init("/tmp/dnauth_test");
    if (!sys) return 0;

    dnauth_evolution_init(sys);

    dnauth_lineage_create(sys, "fitnessuser", "ATGCTAGCATCGATCG");

    /* Initial fitness should be 1.0 */
    double fitness = dnauth_get_fitness(sys, "fitnessuser");
    if (fitness != 1.0) {
        printf("(initial fitness %.2f != 1.0) ", fitness);
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    /* After mutations, fitness should decrease */
    dnauth_evolve_forced(sys, "fitnessuser", 2);
    fitness = dnauth_get_fitness(sys, "fitnessuser");
    if (fitness >= 1.0) {
        printf("(fitness didn't decrease) ");
        dnauth_evolution_cleanup(sys);
        dnauth_cleanup(sys);
        return 0;
    }

    dnauth_evolution_cleanup(sys);
    dnauth_cleanup(sys);
    return 1;
}

static int test_levenshtein_distance(void) {
    /* Same strings */
    if (dnauth_levenshtein_distance("ATGC", "ATGC") != 0) return 0;

    /* One substitution */
    if (dnauth_levenshtein_distance("ATGC", "ATGG") != 1) return 0;

    /* One insertion */
    if (dnauth_levenshtein_distance("ATGC", "ATGCA") != 1) return 0;

    /* One deletion */
    if (dnauth_levenshtein_distance("ATGC", "ATG") != 1) return 0;

    /* Multiple differences */
    if (dnauth_levenshtein_distance("ATGC", "GGGG") != 3) return 0;

    return 1;
}

/* ==============================================================================
 * Main Test Runner
 * ============================================================================== */

int main(void) {
    printf("\n=== DNAuth Test Suite ===\n\n");

    printf("Sequence Operations:\n");
    RUN_TEST(test_sequence_validation);
    RUN_TEST(test_sequence_parsing);
    RUN_TEST(test_sequence_complement);
    RUN_TEST(test_sequence_transcribe);

    printf("\nComplexity Analysis:\n");
    RUN_TEST(test_complexity_low);
    RUN_TEST(test_complexity_high);
    RUN_TEST(test_entropy);

    printf("\nSystem & Authentication:\n");
    RUN_TEST(test_system_init);
    RUN_TEST(test_user_registration);
    RUN_TEST(test_authentication);
    RUN_TEST(test_levenshtein_distance);

    printf("\nEvolution System:\n");
    RUN_TEST(test_evolution_init);
    RUN_TEST(test_lineage_creation);
    RUN_TEST(test_mutation_generation);
    RUN_TEST(test_mutation_application);
    RUN_TEST(test_evolution_event);
    RUN_TEST(test_ancestor_authentication);
    RUN_TEST(test_fitness_calculation);

    printf("\n=== Results: %d/%d tests passed ===\n\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
