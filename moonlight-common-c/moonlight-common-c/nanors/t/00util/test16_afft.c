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

#include "rs16.h"
#include "rs16_afft.h"

#define MAP(x, max_x, m, n) (m + x / (max_x / (n - m) + 1))

typedef reed_solomon16_afft rs_t;

static int verify_buffers(uint16_t **orig, uint16_t **reconstructed, int K, int T)
{
    for (int i = 0; i < K; i++) {
        assert(orig[i] != NULL);
        assert(reconstructed[i] != NULL);
        for (int j = 0; j < T; j++) {
            if (orig[i][j] != reconstructed[i][j]) {
                printf("Mismatch at shard %d, byte %d: expected %04x, got %04x\n", i, j, orig[i][j], reconstructed[i][j]);
                return 0;
            }
        }
    }
    return 1;
}

int test_codec_run(int K, int N, int T, int erasures_count, int seed)
{
    if (K <= 0 || N <= 0 || T <= 0 || erasures_count < 0) {
        printf("invalid dimensions\n");
        return -1;
    }

    uint16_t **buf = calloc(K + N, sizeof(uint16_t *));
    uint16_t **cmp = calloc(K + N, sizeof(uint16_t *));
    uint8_t *marks = calloc(1, K + N);
    int status = 0;

    if (!buf || !cmp || !marks) {
        free(buf);
        free(cmp);
        free(marks);
        return -1;
    }

    for (int i = 0; i < K + N; i++) {
        buf[i] = malloc(T * sizeof(uint16_t));
        cmp[i] = malloc(T * sizeof(uint16_t));
        if (!buf[i] || !cmp[i]) {
            status = -1;
            goto cleanup;
        }
    }

    for (int i = 0; i < K; i++) {
        for (int j = 0; j < T; j++) {
            buf[i][j] = rand() % 65536;
            cmp[i][j] = buf[i][j];
        }
    }

    rs_t *rs = reed_solomon16_afft_new(K, N);
    if (!rs) {
        status = -2;
        goto cleanup;
    }

    reed_solomon16_afft_encode(rs, buf, K + N, T);

    int erased = 0;
    while (erased < erasures_count) {
        int at = rand() % (K + N);
        if (marks[at] == 0) {
            memset(buf[at], 0, T * sizeof(uint16_t));
            marks[at] = 1;
            erased++;
        }
    }

    int decode_res = reed_solomon16_afft_decode(rs, buf, marks, K + N, T);
    if (decode_res < 0)
        printf("reed_solomon16_afft_decode returned %d\n", decode_res);
    reed_solomon16_afft_release(rs);

    if (decode_res < 0) {
        status = -1;
    } else {
        status = verify_buffers(cmp, buf, K, T) ? 0 : 1;
    }

cleanup:
    for (int i = 0; i < K + N; i++) {
        free(buf[i]);
        free(cmp[i]);
    }
    free(buf);
    free(cmp);
    free(marks);
    return status;
}

int test_codec_explicit_erasures(int K, int N, int T, uint32_t erasure_mask)
{
    if (K <= 0 || N <= 0 || T <= 0)
        return -1;

    uint16_t **buf = calloc(K + N, sizeof(uint16_t *));
    uint16_t **cmp = calloc(K + N, sizeof(uint16_t *));
    uint8_t *marks = calloc(1, K + N);
    int status = 0;

    for (int i = 0; i < K + N; i++) {
        buf[i] = malloc(T * sizeof(uint16_t));
        cmp[i] = malloc(T * sizeof(uint16_t));
    }

    for (int i = 0; i < K; i++) {
        for (int j = 0; j < T; j++) {
            buf[i][j] = rand() % 65536;
            cmp[i][j] = buf[i][j];
        }
    }

    rs_t *rs = reed_solomon16_afft_new(K, N);
    if (!rs) {
        status = -2;
        goto cleanup;
    }

    reed_solomon16_afft_encode(rs, buf, K + N, T);

    for (int i = 0; i < K + N; i++) {
        if ((erasure_mask >> i) & 1) {
            memset(buf[i], 0, T * sizeof(uint16_t));
            marks[i] = 1;
        }
    }

    int decode_res = reed_solomon16_afft_decode(rs, buf, marks, K + N, T);
    if (decode_res < 0)
        printf("reed_solomon16_afft_decode returned %d\n", decode_res);
    reed_solomon16_afft_release(rs);

    if (decode_res < 0)
        status = -1;
    else
        status = verify_buffers(cmp, buf, K, T) ? 0 : 1;

cleanup:
    for (int i = 0; i < K + N; i++) {
        free(buf[i]);
        free(cmp[i]);
    }
    free(buf);
    free(cmp);
    free(marks);
    return status;
}

static void run_api_safety_tests(void)
{
    printf("[API SAFETY] Running parameter checks...\n");
    rs_t *rs4 = reed_solomon16_afft_new(5, 3);
    assert(rs4 != NULL);
    assert(rs4->ds == 5);
    assert(rs4->ps == 3);
    assert(rs4->ts == 8);
    reed_solomon16_afft_release(rs4);
    printf("[API SAFETY] OK\n");
}

static void run_decoder_failure_tests(void)
{
    printf("[DECODER FAILURE] Testing too many erasures...\n");
    int res = test_codec_run(5, 3, 1280, 4, 12345);
    assert(res == -1);
    printf("[DECODER FAILURE] OK\n");
}

static void run_bounds_and_alignment_tests(void)
{
    printf("[ALIGNMENT & TAILS] Testing non-aligned buffer sizes...\n");
    for (int T = 1; T <= 130; T++) {
        for (int e = 0; e <= 2; e++) {
            int res = test_codec_run(6, 2, T, e, T * 7 + e);
            if (res != 0) {
                printf("[ALIGNMENT & TAILS] FAILED at T = %d, erasures = %d, res = %d\n", T, e, res);
                exit(1);
            }
        }
    }
    assert(test_codec_run(1, 1, 1280, 1, 999) == 0);
    assert(test_codec_run(1, 1, 1, 1, 999) == 0);
    assert(test_codec_run(254, 1, 16, 1, 888) == 0);
    assert(test_codec_run(1, 254, 16, 50, 777) == 0);
    printf("[ALIGNMENT & TAILS] OK\n");
}

static void run_mds_property_tests(void)
{
    printf("[MDS PROPERTY] Verifying MDS property (any K shards can decode) for small K, N...\n");
    int test_pairs[][2] = {{3, 2}, {4, 3}, {5, 4}, {6, 2}, {2, 6}, {4, 4}};
    int num_pairs = sizeof(test_pairs) / sizeof(test_pairs[0]);
    int T = 1280;

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
    int seed = time(NULL);
    if (argc == 2)
        seed = strtol(argv[1], NULL, 10);
    srand(seed);

    printf("=== RUNNING RS16 AFFT CODEC TEST BATTERY ===\n");
    printf("Seed: %d\n", seed);

    run_api_safety_tests();
    run_decoder_failure_tests();
    run_bounds_and_alignment_tests();
    run_mds_property_tests();

    printf("[RANDOM SEARCH] Running randomized codec iterations...\n");
    int num_iterations = 200;
    if (getenv("CI_EMULATION")) {
        num_iterations = 20;
    }
    for (int i = 0; i < num_iterations; i++) {
        int K = MAP(rand(), RAND_MAX, 1, 50);
        int N = MAP(rand(), RAND_MAX, 1, 20);
        if ((K + N) > 255)
            K = 255 - N;
        int T = MAP(rand(), RAND_MAX, 1, 1280);
        int e = rand() % (N + 1);

        int res = test_codec_run(K, N, T, e, seed + i);
        if (res != 0) {
            printf("[RANDOM SEARCH] Mismatch or failure at K = %d, N = %d, T = %d, e = %d\n", K, N, T, e);
            exit(1);
        }
    }
    printf("[RANDOM SEARCH] OK\n");

    printf("[LARGE K] Running tests for big K...\n");
    int big_ks[] = {500, 1000, 2000, 5000, 10000};
    int losses[] = {5, 10};
    int num_big_ks = sizeof(big_ks) / sizeof(big_ks[0]);
    if (getenv("CI_EMULATION")) {
        num_big_ks = 2; /* Only test up to K=1000 under emulation */
    }
    for (int i = 0; i < num_big_ks; i++) {
        for (int j = 0; j < sizeof(losses) / sizeof(losses[0]); j++) {
            int K = big_ks[i];
            int N = (K * losses[j]) / 100;
            if (N == 0)
                N = 1;
            int T = 1280; // Reasonable block size
            int e = N;    // Max erasures
            int res = test_codec_run(K, N, T, e, seed + i * 10 + j);
            if (res != 0) {
                printf("[LARGE K] FAILED at K = %d, N = %d\n", K, N);
                exit(1);
            }
        }
    }
    printf("[LARGE K] OK\n");

    printf("===ALL AFFT TESTS PASSED SUCCESSFULLY=== \n===OK===\n");
    return 0;
}
