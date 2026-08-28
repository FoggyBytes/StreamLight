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

double now(time_t epoch)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return ((now.tv_sec - epoch) + now.tv_nsec / 1000000000.0);
}

typedef reed_solomon16 rs_t;

void cleanup(uint16_t **buf, uint16_t **cmp, uint8_t *marks, int K, int N)
{
    for (int i = 0; i < K + N; i++) {
        free(buf[i]);
        free(cmp[i]);
    }
    free(buf);
    free(cmp);
    free(marks);
}

int run(int seed, int K, int N, int T, double *et, double *dt)
{
    if (K <= 0 || N <= 0 || T <= 0) {
        printf("invalid dimensions\n");
        return -1;
    }

    uint16_t **buf = calloc(K + N, sizeof(uint16_t *));
    uint16_t **cmp = calloc(K + N, sizeof(uint16_t *));
    uint8_t *marks = calloc(1, K + N);
    int ret = 0;

    if (!buf || !cmp || !marks) {
        printf("out of memory\n");
        free(buf);
        free(cmp);
        free(marks);
        return -1;
    }

    for (int i = 0; i < K + N; i++) {
        buf[i] = aligned_alloc(64, T * sizeof(uint16_t));
        cmp[i] = aligned_alloc(64, T * sizeof(uint16_t));
        if (!buf[i] || !cmp[i]) {
            printf("out of memory\n");
            cleanup(buf, cmp, marks, K, N);
            return -1;
        }
    }

    for (int i = 0; i < K; i++) {
        assert(buf[i] != NULL);
        assert(cmp[i] != NULL);
        for (int j = 0; j < T; j++) {
            buf[i][j] = rand() % 65536;
            cmp[i][j] = buf[i][j];
        }
    }

    reed_solomon16_init();
    rs_t *rs = reed_solomon16_new(K, N);
    if (!rs) {
        cleanup(buf, cmp, marks, K, N);
        printf("failed to init codec\n");
        return -1;
    }

    double t0 = now(0);
    reed_solomon16_encode(rs, buf, K + N, T);
    *et += now(0) - t0;

    for (int i = 0; i < K + N; i++) {
        marks[i] = 0;
    }

    for (int i = 0; i < N; i++) {
        int at = rand() % (K + N);
        assert(buf[at] != NULL);
        memset(buf[at], 0, T * sizeof(uint16_t));
        marks[at] = 1;
    }

    t0 = now(0);
    ret = reed_solomon16_decode(rs, buf, marks, K + N, T);
    *dt += now(0) - t0;
    reed_solomon16_release(rs);

    int failed = 0;
    for (int i = 0; i < K; i++) {
        assert(buf[i] != NULL);
        assert(cmp[i] != NULL);
        for (int j = 0; j < T; j++) {
            if (cmp[i][j] != buf[i][j]) {
                printf("mismatch at row %d col %d\n", i, j);
                failed = 1;
                break;
            }
        }
    }
    assert(failed == 0);

    cleanup(buf, cmp, marks, K, N);
    return ret;
}

int main(int argc, char *argv[])
{
    double t0 = now(0);
    int seed = time(NULL), K, N, T;
    if (argc != 4)
        return -1;

    K = strtol(argv[1], NULL, 10);
    N = strtol(argv[2], NULL, 10);
    T = strtol(argv[3], NULL, 10);

    srand(seed);

    printf("===BEGIN===P SEED: %d K: %d N: %d T: %d\n", seed, K, N, T);

    int Mb = 128;
    int num = Mb * (1024 * 1024) / (K * T * sizeof(uint16_t));
    if (num < 1)
        num = 1;
    double et = 0.0, dt = 0.0;
    for (int i = 0; i < num; i++) {
        if (run(seed, K, N, T, &et, &dt) < 0) {
            printf("run failed\n");
            return -1;
        }
    }
    printf("data shards = %d, repair shards = %d, encoded %d MB in %1.3f secs, "
           "throughput: %.1fMB/s\n",
           K, N, Mb, et, Mb / et);
    printf("data shards = %d, repair shards = %d, decoded %d MB in %1.3f secs, "
           "throughput: %.1fMB/s\n",
           K, N, Mb, dt, Mb / dt);

    printf("total time: %1.3f|%1.3f|%1.3f\n", now(0) - t0, et, dt);
    return 0;
}
