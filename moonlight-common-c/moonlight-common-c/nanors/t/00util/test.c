#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "rs.h"
#include "oblas_lite.h"
static uint8_t GF2_8_MUL[65536];

#define MAP(x, max_x, m, n) (m + x / (max_x / (n - m) + 1))

typedef reed_solomon rs_t;

// Helper to check if two buffers are identical
static int verify_buffers(uint8_t **orig, uint8_t **reconstructed, int K, int T)
{
    for (int i = 0; i < K; i++) {
        assert(orig[i] != NULL);
        assert(reconstructed[i] != NULL);
        for (int j = 0; j < T; j++) {
            if (orig[i][j] != reconstructed[i][j]) {
                return 0;
            }
        }
    }
    return 1;
}

/* Reference implementations for verification */
static void ref_axpy(uint8_t *a, uint8_t *b, uint8_t u, unsigned k)
{
    const uint8_t *u_row = &GF2_8_MUL[u << 8];
    for (unsigned i = 0; i < k; i++) {
        a[i] ^= u_row[b[i]];
    }
}

static void ref_scal(uint8_t *a, uint8_t u, unsigned k)
{
    const uint8_t *u_row = &GF2_8_MUL[u << 8];
    for (unsigned i = 0; i < k; i++) {
        a[i] = u_row[a[i]];
    }
}

static void ref_axiy(uint8_t *a, uint8_t *b, uint8_t u, unsigned k)
{
    const uint8_t *u_row = &GF2_8_MUL[u << 8];
    for (unsigned i = 0; i < k; i++) {
        a[i] = u_row[b[i]];
    }
}

static void ref_axpyb32(uint8_t *a, uint32_t *b, uint8_t u, unsigned k)
{
    unsigned idx = 0, p = 0;
    unsigned k_fast = k & ~31;
    for (; idx < k_fast; idx += 32, p++) {
        uint32_t tmp = b[p];
        while (tmp > 0) {
            unsigned tz = __builtin_ctz(tmp);
            tmp = tmp & (tmp - 1);
            a[tz + idx] ^= u;
        }
    }
    if (idx < k) {
        uint32_t tmp = b[p];
        tmp &= (1U << (k - idx)) - 1;
        while (tmp > 0) {
            unsigned tz = __builtin_ctz(tmp);
            tmp = tmp & (tmp - 1);
            a[tz + idx] ^= u;
        }
    }
}

static void run_oblas_lite_tests(void)
{
    printf("[OBLAS LITE] Running oblas_lite SIMD vs Reference verification...\n");
    struct oblas_impl impl;
    oblas_get_impl(&impl);

    /* Test a wide range of sizes to cover SIMD registers (16, 32, 64) and all tails/alignments */
    for (int T = 1; T <= 256; T++) {
        uint8_t *buf_ref = reed_solomon_aligned_alloc(T);
        uint8_t *buf_opt = reed_solomon_aligned_alloc(T);
        uint8_t *buf_b = reed_solomon_aligned_alloc(T);
        int b_u32_len = (T + 31) / 32;
        uint32_t *buf_b32 = calloc(b_u32_len, sizeof(uint32_t));

        assert(buf_ref && buf_opt && buf_b && buf_b32);

        /* Test with different u values (including 0, 1, and random values) */
        uint8_t u_values[] = {0, 1, 2, 17, 128, 255};
        int num_u = sizeof(u_values) / sizeof(u_values[0]);

        for (int ui = 0; ui < num_u; ui++) {
            uint8_t u = u_values[ui];

            /* 1. Test axpy */
            for (int i = 0; i < T; i++) {
                buf_ref[i] = buf_opt[i] = rand() % 256;
                buf_b[i] = rand() % 256;
            }
            ref_axpy(buf_ref, buf_b, u, T);
            impl.axpy(buf_opt, buf_b, u, T);
            for (int i = 0; i < T; i++) {
                if (buf_ref[i] != buf_opt[i]) {
                    printf("[OBLAS LITE] axpy mismatch at T=%d, u=%d, index=%d\n", T, u, i);
                    exit(1);
                }
            }

            /* 2. Test scal */
            for (int i = 0; i < T; i++) {
                buf_ref[i] = buf_opt[i] = rand() % 256;
            }
            ref_scal(buf_ref, u, T);
            impl.scal(buf_opt, u, T);
            for (int i = 0; i < T; i++) {
                if (buf_ref[i] != buf_opt[i]) {
                    printf("[OBLAS LITE] scal mismatch at T=%d, u=%d, index=%d\n", T, u, i);
                    exit(1);
                }
            }

            /* 3. Test axiy */
            for (int i = 0; i < T; i++) {
                buf_ref[i] = buf_opt[i] = rand() % 256;
                buf_b[i] = rand() % 256;
            }
            ref_axiy(buf_ref, buf_b, u, T);
            impl.axiy(buf_opt, buf_b, u, T);
            for (int i = 0; i < T; i++) {
                if (buf_ref[i] != buf_opt[i]) {
                    printf("[OBLAS LITE] axiy mismatch at T=%d, u=%d, index=%d\n", T, u, i);
                    exit(1);
                }
            }

            /* 4. Test axpyb32 */
            for (int i = 0; i < T; i++) {
                buf_ref[i] = buf_opt[i] = rand() % 256;
            }
            for (int i = 0; i < b_u32_len; i++) {
                buf_b32[i] = ((uint32_t)rand() << 16) | (uint32_t)rand();
            }
            ref_axpyb32(buf_ref, buf_b32, u, T);
            impl.axpyb32(buf_opt, buf_b32, u, T);
            for (int i = 0; i < T; i++) {
                if (buf_ref[i] != buf_opt[i]) {
                    printf("[OBLAS LITE] axpyb32 mismatch at T=%d, u=%d, index=%d\n", T, u, i);
                    exit(1);
                }
            }
        }

        reed_solomon_free(buf_ref);
        reed_solomon_free(buf_opt);
        reed_solomon_free(buf_b);
        free(buf_b32);
    }
    printf("[OBLAS LITE] OK\n");
}

// Runs a single RS encode/decode verification
int test_codec_run(int K, int N, int T, int erasures_count, int seed)
{
    if (K <= 0 || N <= 0 || T <= 0 || erasures_count < 0) {
        printf("invalid dimensions\n");
        return -1;
    }

    uint8_t **buf = calloc(K + N, sizeof(uint8_t *));
    uint8_t **cmp = calloc(K + N, sizeof(uint8_t *));
    uint8_t *marks = calloc(1, K + N);
    int status = 0;

    if (!buf || !cmp || !marks) {
        printf("out of memory\n");
        free(buf);
        free(cmp);
        free(marks);
        return -1;
    }

    for (int i = 0; i < K + N; i++) {
        buf[i] = reed_solomon_aligned_alloc(T);
        cmp[i] = reed_solomon_aligned_alloc(T);
        if (!buf[i] || !cmp[i]) {
            printf("out of memory\n");
            status = -1;
            goto cleanup;
        }
    }

    // Populate data shards with deterministic random data
    for (int i = 0; i < K; i++) {
        assert(buf[i] != NULL);
        assert(cmp[i] != NULL);
        for (int j = 0; j < T; j++) {
            buf[i][j] = MAP(rand(), RAND_MAX, 0, 256);
            cmp[i][j] = buf[i][j];
        }
    }

    reed_solomon_init();
    rs_t *rs = reed_solomon_new(K, N);
    if (!rs) {
        status = -2; // Initialization failed
        goto cleanup;
    }

    reed_solomon_encode(rs, buf, K + N, T);

    // Erase a specific number of shards
    int erased = 0;
    while (erased < erasures_count) {
        int at = rand() % (K + N);
        if (marks[at] == 0) {
            assert(buf[at] != NULL);
            memset(buf[at], 0, T);
            marks[at] = 1;
            erased++;
        }
    }

    int decode_res = reed_solomon_reconstruct(rs, buf, marks, K + N, T);
    reed_solomon_release(rs);

    if (decode_res < 0) {
        status = -1; // Decode returned error
    } else {
        status = verify_buffers(cmp, buf, K, T) ? 0 : 1; // 0 = SUCCESS, 1 = MISMATCH
    }

cleanup:
    for (int i = 0; i < K + N; i++) {
        reed_solomon_free(buf[i]);
        reed_solomon_free(cmp[i]);
    }
    free(buf);
    free(cmp);
    free(marks);
    return status;
}

// Runs a single RS encode/decode verification with explicit erasures mask
int test_codec_explicit_erasures(int K, int N, int T, uint32_t erasure_mask)
{
    if (K <= 0 || N <= 0 || T <= 0) {
        printf("invalid dimensions\n");
        return -1;
    }

    uint8_t **buf = calloc(K + N, sizeof(uint8_t *));
    uint8_t **cmp = calloc(K + N, sizeof(uint8_t *));
    uint8_t *marks = calloc(1, K + N);
    int status = 0;

    if (!buf || !cmp || !marks) {
        printf("out of memory\n");
        free(buf);
        free(cmp);
        free(marks);
        return -1;
    }

    for (int i = 0; i < K + N; i++) {
        buf[i] = reed_solomon_aligned_alloc(T);
        cmp[i] = reed_solomon_aligned_alloc(T);
        if (!buf[i] || !cmp[i]) {
            printf("out of memory\n");
            status = -1;
            goto cleanup;
        }
    }

    // Populate data shards with deterministic random data
    for (int i = 0; i < K; i++) {
        assert(buf[i] != NULL);
        assert(cmp[i] != NULL);
        for (int j = 0; j < T; j++) {
            buf[i][j] = MAP(rand(), RAND_MAX, 0, 256);
            cmp[i][j] = buf[i][j];
        }
    }

    reed_solomon_init();
    rs_t *rs = reed_solomon_new(K, N);
    if (!rs) {
        status = -2; // Initialization failed
        goto cleanup;
    }

    reed_solomon_encode(rs, buf, K + N, T);

    for (int i = 0; i < K + N; i++) {
        if ((erasure_mask >> i) & 1) {
            assert(buf[i] != NULL);
            memset(buf[i], 0, T);
            marks[i] = 1;
        }
    }

    int decode_res = reed_solomon_reconstruct(rs, buf, marks, K + N, T);
    reed_solomon_release(rs);

    if (decode_res < 0) {
        status = -1; // Decode returned error
    } else {
        status = verify_buffers(cmp, buf, K, T) ? 0 : 1; // 0 = SUCCESS, 1 = MISMATCH
    }

cleanup:
    for (int i = 0; i < K + N; i++) {
        reed_solomon_free(buf[i]);
        reed_solomon_free(cmp[i]);
    }
    free(buf);
    free(cmp);
    free(marks);
    return status;
}

// 1. API Parameter Safety Tests
static void run_api_safety_tests(void)
{
    printf("[API SAFETY] Running parameter checks...\n");

    // ds + ps > 255
    rs_t *rs1 = reed_solomon_new(250, 10);
    assert(rs1 == NULL);

    // ds <= 0
    rs_t *rs2 = reed_solomon_new(0, 5);
    assert(rs2 == NULL);

    // ps <= 0
    rs_t *rs3 = reed_solomon_new(5, 0);
    assert(rs3 == NULL);

    // Valid instantiation
    rs_t *rs4 = reed_solomon_new(5, 3);
    assert(rs4 != NULL);
    assert(rs4->ds == 5);
    assert(rs4->ps == 3);
    assert(rs4->ts == 8);
    reed_solomon_release(rs4);

    printf("[API SAFETY] OK\n");
}

// 2. Decoder Failure Recovery Tests
static void run_decoder_failure_tests(void)
{
    printf("[DECODER FAILURE] Testing too many erasures...\n");
    // K = 5, N = 3 (Parity = 3, so we can correct at most 3 erasures)
    // Erase 4 shards (should fail)
    int res = test_codec_run(5, 3, 64, 4, 12345);
    assert(res == -1); // Must fail decode
    printf("[DECODER FAILURE] OK\n");
}

// 3. Edge Case Bounds & Alignment Tests
static void run_bounds_and_alignment_tests(void)
{
    printf("[ALIGNMENT & TAILS] Testing non-aligned buffer sizes and SIMD boundary tails...\n");

    // Test sizes from 1 to 130 bytes to comprehensively cover all SIMD tail paths (NEON, AVX2, AVX-512)
    for (int T = 1; T <= 130; T++) {
        // Test varying erasure counts
        for (int e = 0; e <= 2; e++) {
            int res = test_codec_run(6, 2, T, e, T * 7 + e);
            if (res != 0) {
                printf("[ALIGNMENT & TAILS] FAILED at T = %d, erasures = %d, res = %d\n", T, e, res);
                exit(1);
            }
        }
    }

    // Extreme configurations
    // K = 1, N = 1 (Minimal dimensions)
    assert(test_codec_run(1, 1, 64, 1, 999) == 0);
    assert(test_codec_run(1, 1, 1, 1, 999) == 0);

    // K = 254, N = 1 (Max data shards)
    assert(test_codec_run(254, 1, 16, 1, 888) == 0);

    // K = 1, N = 254 (Max parity shards)
    assert(test_codec_run(1, 254, 16, 50, 777) == 0);

    printf("[ALIGNMENT & TAILS] OK\n");
}

// 4. MDS Property Verification
static void run_mds_property_tests(void)
{
    printf("[MDS PROPERTY] Verifying MDS property (any K shards can decode) for small K, N...\n");
    // Test pairs like (K=3, N=2), (K=4, N=3), (K=5, N=4)
    int test_pairs[][2] = {{3, 2}, {4, 3}, {5, 4}, {6, 2}, {2, 6}, {4, 4}};
    int num_pairs = sizeof(test_pairs) / sizeof(test_pairs[0]);
    int T = 64; // arbitrary size

    for (int p = 0; p < num_pairs; p++) {
        int K = test_pairs[p][0];
        int N = test_pairs[p][1];
        int total = K + N;
        uint32_t max_mask = (1U << total);

        for (uint32_t mask = 0; mask < max_mask; mask++) {
            if (__builtin_popcount(mask) <= N) {
                int res = test_codec_explicit_erasures(K, N, T, mask);
                if (res != 0) {
                    printf("[MDS PROPERTY] FAILED at K=%d, N=%d, mask=0x%x\n", K, N, mask);
                    exit(1);
                }
            }
        }
    }
    printf("[MDS PROPERTY] OK\n");
}

int main(int argc, char *argv[])
{
    for (int i = 0; i < 256; i++) {
        for (int j = 0; j < 256; j++) {
            if (i == 0 || j == 0) {
                GF2_8_MUL[(i << 8) + j] = 0;
            } else {
                GF2_8_MUL[(i << 8) + j] = GF2_8_EXP[GF2_8_LOG[i] + GF2_8_LOG[j]];
            }
        }
    }
    int seed = time(NULL);
    if (argc == 2) {
        seed = strtol(argv[1], NULL, 10);
    }
    srand(seed);

    printf("=== RUNNING ROBUST CODEC TEST BATTERY ===\n");
    printf("Seed: %d\n", seed);

    /* Execute oblas_lite unit tests */
    run_oblas_lite_tests();

    /* Execute structured unit tests */
    run_api_safety_tests();
    run_decoder_failure_tests();
    run_bounds_and_alignment_tests();
    run_mds_property_tests();

    // Execute random tests for broader coverage
    printf("[RANDOM SEARCH] Running randomized codec iterations...\n");
    int num_iterations = 200;
    if (getenv("CI_EMULATION")) {
        num_iterations = 20;
    }
    for (int i = 0; i < num_iterations; i++) {
        int K = MAP(rand(), RAND_MAX, 1, 50);
        int N = MAP(rand(), RAND_MAX, 1, 20);
        if ((K + N) > 255) {
            K = 255 - N;
        }
        int T = MAP(rand(), RAND_MAX, 1, 1024);
        int e = rand() % (N + 1); // Random erasures <= N

        int res = test_codec_run(K, N, T, e, seed + i);
        if (res != 0) {
            printf("[RANDOM SEARCH] Mismatch or failure at K = %d, N = %d, T = %d, e = %d\n", K, N, T, e);
            exit(1);
        }
    }
    printf("[RANDOM SEARCH] OK\n");

    printf("===ALL TESTS PASSED SUCCESSFULLY=== \n===OK===\n");
    return 0;
}
