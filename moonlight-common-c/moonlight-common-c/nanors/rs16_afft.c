#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "rs16.h"
#include "rs16_afft.h"
#include "deps/obl/oblas16_afft.h"
#include "deps/obl/oblas16.h"
#include "deps/obl/oblas_lite.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
#endif

static int next_power_of_2(int n)
{
    int v = 1;
    while (v < n)
        v <<= 1;
    return v;
}

static int log2_int(int n)
{
    int v = 0;
    while (n > 1) {
        n >>= 1;
        v++;
    }
    return v;
}

static uint16_t omegas[65536];
static int omegas_initialized = 0;

static uint32_t LogWalsh[65536];

#define FWHT_UNROLL_PASS(LEN)                                                                                                      \
    for (int i = 0; i < n; i += 2 * (LEN)) {                                                                                       \
        for (int j = 0; j < (LEN); j++) {                                                                                          \
            uint32_t a = data[i + j];                                                                                              \
            uint32_t b = data[i + (LEN) + j];                                                                                      \
            uint32_t sum = a + b;                                                                                                  \
            uint32_t diff = a + 65535 - b;                                                                                         \
            data[i + j] = sum >= 65535 ? sum - 65535 : sum;                                                                        \
            data[i + (LEN) + j] = diff >= 65535 ? diff - 65535 : diff;                                                             \
        }                                                                                                                          \
    }

static void fwht_mod(uint32_t *data, int n)
{
    if (n < 8)
        return;

    FWHT_UNROLL_PASS(1);
    FWHT_UNROLL_PASS(2);
    FWHT_UNROLL_PASS(4);

    for (int len = 8; len < n; len <<= 1) {
        for (int i = 0; i < n; i += 2 * len) {
            for (int j = 0; j < len; j++) {
                uint32_t a = data[i + j];
                uint32_t b = data[i + len + j];
                uint32_t sum = a + b;
                uint32_t diff = a + 65535 - b;
                data[i + j] = sum >= 65535 ? sum - 65535 : sum;
                data[i + len + j] = diff >= 65535 ? diff - 65535 : diff;
            }
        }
    }
}

void reed_solomon16_afft_init(void)
{
    if (omegas_initialized)
        return;
    reed_solomon16_init();
    oblas16_afft_init();
    uint16_t *buf = (uint16_t *)calloc(65536, sizeof(uint16_t));
    if (buf) {
        buf[1] = 1;
        /* compute omegas using 65536 length fft */
        struct oblas16_impl o16;
        struct oblas16_afft_impl afft;
        oblas16_get_impl(&o16);
        oblas16_afft_get_impl(&afft);
        oblas16_afft_fft(buf, 16, 1, NULL, 0, &o16, &afft);
        memcpy(omegas, buf, 65536 * sizeof(uint16_t));
        free(buf);
    }

    for (int i = 1; i < 65536; i++) {
        LogWalsh[i] = GF16_LOG[omegas[i]];
    }
    LogWalsh[0] = 0;
    fwht_mod(LogWalsh, 65536);
    omegas_initialized = 1;
}

#define BATCH_SIZE 512

/* --- public apis: init, new, release, encode and decode --- */
struct afft_workspace {
    uint16_t *chunk_buf;
    uint8_t *needed_buf;
    uint16_t *acc;
    uint16_t *tmp_buf;
    uint16_t *L_eval;
    uint16_t *inv_Lp_eval;
    uint8_t *is_erased;
    uint32_t *error_locations;
    uint16_t *buf;
    int *erasures;
    int *E;
    void *flat_alloc;
    void (*axpy)(uint16_t *a, const uint16_t *b, uint16_t u, unsigned k);
    void (*axiy)(uint16_t *dst, const uint16_t *src, uint16_t twist, unsigned batch);
    struct oblas16_impl o16;
    struct oblas16_afft_impl afft;
};

reed_solomon16_afft *reed_solomon16_afft_new(int data_shards, int parity_shards)
{
    if (data_shards <= 0 || data_shards > DATA_SHARDS_MAX || parity_shards <= 0 || parity_shards > DATA_SHARDS_MAX)
        return NULL;

    reed_solomon16_afft_init();

    reed_solomon16_afft *rs = (reed_solomon16_afft *)calloc(1, sizeof(reed_solomon16_afft));
    if (!rs)
        return NULL;
    rs->ds = data_shards;
    rs->ps = parity_shards;
    rs->ts = data_shards + parity_shards;

    int K = rs->ds;
    int P = rs->ps;
    int K_prime = next_power_of_2(K);
    int N = next_power_of_2(K_prime + P);
    int log_N = log2_int(N);
    int M = next_power_of_2(P);
    if (M < 2)
        M = 2;

    struct afft_workspace *ws = (struct afft_workspace *)calloc(1, sizeof(struct afft_workspace));
    if (!ws) {
        free(rs);
        return NULL;
    }

    size_t sz_chunk_buf = N * BATCH_SIZE * sizeof(uint16_t);
    size_t sz_needed_buf = log_N * (1 << (log_N - 1)) * sizeof(uint8_t);
    size_t sz_acc = M * BATCH_SIZE * sizeof(uint16_t);
    size_t sz_tmp_buf = M * BATCH_SIZE * sizeof(uint16_t);
    size_t sz_L_eval = N * sizeof(uint16_t);
    size_t sz_inv_Lp_eval = N * sizeof(uint16_t);
    size_t sz_is_erased = N * sizeof(uint8_t);
    size_t sz_error_locations = 65536 * sizeof(uint32_t);
    size_t sz_buf = N * BATCH_SIZE * sizeof(uint16_t);
    size_t sz_erasures = 65536 * sizeof(int);
    size_t sz_E = 65536 * sizeof(int);

    sz_chunk_buf = (sz_chunk_buf + 63) & ~63;
    sz_needed_buf = (sz_needed_buf + 63) & ~63;
    sz_acc = (sz_acc + 63) & ~63;
    sz_tmp_buf = (sz_tmp_buf + 63) & ~63;
    sz_L_eval = (sz_L_eval + 63) & ~63;
    sz_inv_Lp_eval = (sz_inv_Lp_eval + 63) & ~63;
    sz_is_erased = (sz_is_erased + 63) & ~63;
    sz_error_locations = (sz_error_locations + 63) & ~63;
    sz_buf = (sz_buf + 63) & ~63;
    sz_erasures = (sz_erasures + 63) & ~63;
    sz_E = (sz_E + 63) & ~63;

    size_t total_size = sz_chunk_buf + sz_needed_buf + sz_acc + sz_tmp_buf + sz_L_eval + sz_inv_Lp_eval + sz_is_erased +
                        sz_error_locations + sz_buf + sz_erasures + sz_E;

    ws->flat_alloc = obl_alloc(1, total_size, 64);
    if (!ws->flat_alloc) {
        free(ws);
        free(rs);
        return NULL;
    }

    uint8_t *ptr = (uint8_t *)ws->flat_alloc;
    ws->chunk_buf = (uint16_t *)ptr;
    ptr += sz_chunk_buf;
    ws->needed_buf = ptr;
    ptr += sz_needed_buf;
    ws->acc = (uint16_t *)ptr;
    ptr += sz_acc;
    ws->tmp_buf = (uint16_t *)ptr;
    ptr += sz_tmp_buf;
    ws->L_eval = (uint16_t *)ptr;
    ptr += sz_L_eval;
    ws->inv_Lp_eval = (uint16_t *)ptr;
    ptr += sz_inv_Lp_eval;
    ws->is_erased = ptr;
    ptr += sz_is_erased;
    ws->error_locations = (uint32_t *)ptr;
    ptr += sz_error_locations;
    ws->buf = (uint16_t *)ptr;
    ptr += sz_buf;
    ws->erasures = (int *)ptr;
    ptr += sz_erasures;
    ws->E = (int *)ptr;

    oblas16_get_impl(&ws->o16);
    oblas16_afft_get_impl(&ws->afft);
    ws->axpy = ws->o16.axpy;
    ws->axiy = ws->o16.axiy;

    rs->p = (void *)ws;
    return rs;
}

void reed_solomon16_afft_release(reed_solomon16_afft *rs)
{
    if (rs) {
        if (rs->p) {
            struct afft_workspace *ws = (struct afft_workspace *)rs->p;
            if (ws->flat_alloc)
                obl_free(ws->flat_alloc);
            free(ws);
        }
        free(rs);
    }
}

int reed_solomon16_afft_encode(reed_solomon16_afft *rs, uint16_t **shards, int nr_shards, int bs)
{
    if (!rs || !shards || nr_shards < rs->ts || bs <= 0)
        return -1;

    struct afft_workspace *ws = (struct afft_workspace *)rs->p;
    int K = rs->ds;
    int P = rs->ps;
    int K_prime = next_power_of_2(K);
    int N = next_power_of_2(K_prime + P);

    int log_K_prime = log2_int(K_prime);
    int log_N = log2_int(N);

    uint16_t *chunk_buf = ws->chunk_buf;
    uint8_t *needed_buf = ws->needed_buf;
    memset(needed_buf, 0, log_N * (1 << (log_N - 1)) * sizeof(uint8_t));

    uint8_t *needed[16];
    int offset = 0;
    for (int k = 0; k < log_N; k++) {
        needed[k] = needed_buf + offset;
        offset += 1 << (log_N - 1 - k);
    }
    for (int i = 0; i < P; i++) {
        int idx = K_prime + i;
        for (int k = 0; k < log_N; k++) {
            needed[k][idx >> (k + 1)] = 1;
        }
    }

    /* high rate encoder optimization */
    int M = next_power_of_2(P);
    if (M < 2)
        M = 2;
    int log_M = log2_int(M);

    if (M <= K_prime / 2) {
        uint16_t *acc = ws->acc;
        uint16_t *tmp_buf = ws->tmp_buf;

        uint16_t *gamma_arr = (uint16_t *)calloc(K / M + 1, sizeof(uint16_t));
        for (int c = 0; c * M < K; c++) {
            gamma_arr[c] = oblas16_afft_compute_gamma(c, log_M, log_K_prime, log_N, K_prime / M);
        }

        for (int start = 0; start < bs; start += BATCH_SIZE) {
            int current_chunk = (bs - start < BATCH_SIZE) ? (bs - start) : BATCH_SIZE;

            memset(acc, 0, M * BATCH_SIZE * sizeof(uint16_t));

            for (int c = 0; c * M < K; c++) {
                int chunk_items = (K - c * M > M) ? M : (K - c * M);
                memset(chunk_buf, 0, M * BATCH_SIZE * sizeof(uint16_t));

                for (int i = 0; i < chunk_items; i++) {
                    memcpy(&chunk_buf[i * BATCH_SIZE], &shards[c * M + i][start], current_chunk * sizeof(uint16_t));
                }

                oblas16_afft_ifft(chunk_buf, log_M, BATCH_SIZE, chunk_items, c, &ws->o16, &ws->afft);

                uint16_t gamma = gamma_arr[c];
                if (gamma == 1) {
                    ws->axpy(acc, chunk_buf, 1, M * BATCH_SIZE);
                } else if (gamma != 0) {
                    ws->axiy(tmp_buf, chunk_buf, gamma, M * BATCH_SIZE);
                    ws->axpy(acc, tmp_buf, 1, M * BATCH_SIZE);
                }
            }

            /* apply fft to accumulator */
            int c_out = K_prime / M;
            oblas16_afft_fft(acc, log_M, BATCH_SIZE, NULL, c_out, &ws->o16, &ws->afft);

            /* extract parity */
            for (int i = 0; i < P; i++) {
                memcpy(&shards[K + i][start], &acc[i * BATCH_SIZE], current_chunk * sizeof(uint16_t));
            }
        }
        free(gamma_arr);
    } else {
        /* original encoder for low rate / small n */
        for (int i = 0; i < P; i++) {
            memset(shards[K + i], 0, bs * sizeof(uint16_t));
        }

        for (int start = 0; start < bs; start += BATCH_SIZE) {
            int current_chunk = (bs - start < BATCH_SIZE) ? (bs - start) : BATCH_SIZE;

            /* load data into chunk_buf */
            memset(chunk_buf, 0, N * BATCH_SIZE * sizeof(uint16_t));

            for (int i = 0; i < K; i++) {
                memcpy(&chunk_buf[i * BATCH_SIZE], &shards[i][start], current_chunk * sizeof(uint16_t));
            }

            /* apply ifft of length k_prime on the data */
            oblas16_afft_ifft(chunk_buf, log_K_prime, BATCH_SIZE, K, 0, &ws->o16, &ws->afft);

            /* pad to n and fft */
            oblas16_afft_fft(chunk_buf, log_N, BATCH_SIZE, needed, 0, &ws->o16, &ws->afft);

            /* extract parity */
            for (int i = 0; i < P; i++) {
                memcpy(&shards[K + i][start], &chunk_buf[(K_prime + i) * BATCH_SIZE], current_chunk * sizeof(uint16_t));
            }
        }
    }
    return 0;
}

int reed_solomon16_afft_decode(reed_solomon16_afft *rs, uint16_t **shards, uint8_t *marks, int nr_shards, int bs)
{
    if (!rs || !shards || !marks || nr_shards < rs->ts || bs <= 0)
        return -1;

    reed_solomon16_afft_init();

    struct afft_workspace *ws = (struct afft_workspace *)rs->p;
    int K = rs->ds;
    int P = rs->ps;
    int K_prime = next_power_of_2(K);
    int N = next_power_of_2(K_prime + P);

    int log_N = log2_int(N);

    /* find erasures */
    int *erasures = ws->erasures;
    int num_erasures = 0;
    for (int i = 0; i < K; i++) {
        if (marks[i])
            erasures[num_erasures++] = i;
    }
    if (num_erasures == 0)
        return 0; /* nothing to decode! */

    int num_surviving_parities = 0;
    for (int i = 0; i < P; i++) {
        if (!marks[K + i])
            num_surviving_parities++;
    }

    if (num_surviving_parities < num_erasures)
        return -1; /* not enough shards */

    /* determine the complete set of evaluation points e that are considered erased. */
    int *E = ws->E;
    int num_E = 0;

    /* add missing data points */
    for (int i = 0; i < K; i++) {
        if (marks[i])
            E[num_E++] = i;
    }
    /* add missing parity points */
    for (int i = 0; i < P; i++) {
        if (marks[K + i])
            E[num_E++] = K_prime + i;
    }
    /* add unused trailing points */
    for (int i = K_prime + P; i < N; i++) {
        E[num_E++] = i;
    }

    uint16_t *L_eval = ws->L_eval;
    uint16_t *inv_Lp_eval = ws->inv_Lp_eval;
    uint8_t *is_erased = ws->is_erased;
    uint32_t *error_locations = ws->error_locations;

    memset(is_erased, 0, N * sizeof(uint8_t));
    memset(error_locations, 0, 65536 * sizeof(uint32_t));

    for (int i = 0; i < num_E; i++) {
        is_erased[E[i]] = 1;
        error_locations[E[i]] = 1;
    }

    fwht_mod(error_locations, 65536);

    for (int i = 0; i < 65536; i++) {
        uint32_t x = error_locations[i] * LogWalsh[i];
        uint32_t s = (x >> 16) + (x & 65535);
        s = (s >> 16) + (s & 65535);
        error_locations[i] = (s >= 65535) ? s - 65535 : s;
    }

    fwht_mod(error_locations, 65536);

    for (int i = 0; i < N; i++) {
        if (!is_erased[i]) {
            L_eval[i] = GF16_EXP[error_locations[i]];
        } else {
            inv_Lp_eval[i] = GF16_EXP[65535 - error_locations[i]];
        }
    }

    uint8_t *needed_buf = ws->needed_buf;
    memset(needed_buf, 0, log_N * (1 << (log_N - 1)) * sizeof(uint8_t));

    uint8_t *needed[16];
    int offset = 0;
    for (int k = 0; k < log_N; k++) {
        needed[k] = needed_buf + offset;
        offset += 1 << (log_N - 1 - k);
    }
    for (int i = 0; i < num_erasures; i++) {
        int idx = erasures[i];
        for (int k = 0; k < log_N; k++) {
            needed[k][idx >> (k + 1)] = 1;
        }
    }

    /* decoding main loop */
    uint16_t *buf = ws->buf;

    int num_words = bs;
    for (int byte_idx = 0; byte_idx < num_words; byte_idx += BATCH_SIZE) {
        int batch = (num_words - byte_idx < BATCH_SIZE) ? (num_words - byte_idx) : BATCH_SIZE;

        memset(buf, 0, N * BATCH_SIZE * sizeof(uint16_t));

        /* populate non-erased data */
        for (int i = 0; i < K; i++) {
            if (!marks[i]) {
                uint16_t L = L_eval[i];
                uint16_t *shard = shards[i];
                ws->axiy(&buf[i * BATCH_SIZE], &shard[byte_idx], L, batch);
            }
        }

        /* populate non-erased parities */
        for (int i = 0; i < P; i++) {
            if (!marks[K + i]) {
                uint16_t L = L_eval[K_prime + i];
                uint16_t *shard = shards[K + i];
                ws->axiy(&buf[(K_prime + i) * BATCH_SIZE], &shard[byte_idx], L, batch);
            }
        }

        /* apply ifft */
        oblas16_afft_ifft(buf, log_N, BATCH_SIZE, K_prime + P, 0, &ws->o16, &ws->afft);

        /* compute formal derivative in-place on buf */
        for (int i = 1; i < K_prime + P; i++) {
            int width = ((i ^ (i - 1)) + 1) >> 1;
            int dst_idx = (i - width) * BATCH_SIZE;
            int src_idx = i * BATCH_SIZE;
            ws->axpy(&buf[dst_idx], &buf[src_idx], 1, width * BATCH_SIZE);
        }

        /* apply fft in-place on buf */
        oblas16_afft_fft(buf, log_N, BATCH_SIZE, needed, 0, &ws->o16, &ws->afft);

        /* reconstruct erased data shards */
        for (int i = 0; i < num_erasures; i++) {
            int idx = erasures[i];
            uint16_t inv_L = inv_Lp_eval[idx];
            uint16_t *shard = shards[idx];
            memset(&shard[byte_idx], 0, batch * sizeof(uint16_t));
            ws->axiy(&shard[byte_idx], &buf[idx * BATCH_SIZE], inv_L, batch);
        }
    }

    return 0;
}
