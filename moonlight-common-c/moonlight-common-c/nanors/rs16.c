#include "rs16.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "deps/obl/oblas16.h"

static int invert_mat_gf16(uint16_t *src, uint16_t *dst, int ds)
{
    for (int i = 0; i < ds; i++) {
        for (int j = 0; j < ds; j++) {
            dst[i * ds + j] = (i == j) ? 1 : 0;
        }
    }

    for (int i = 0; i < ds; i++) {
        if (src[i * ds + i] == 0) {
            int pivot = -1;
            for (int j = i + 1; j < ds; j++) {
                if (src[j * ds + i] != 0) {
                    pivot = j;
                    break;
                }
            }
            if (pivot == -1)
                return -1;
            for (int k = 0; k < ds; k++) {
                uint16_t tmp = src[i * ds + k];
                src[i * ds + k] = src[pivot * ds + k];
                src[pivot * ds + k] = tmp;

                tmp = dst[i * ds + k];
                dst[i * ds + k] = dst[pivot * ds + k];
                dst[pivot * ds + k] = tmp;
            }
        }

        uint16_t pivot_val = src[i * ds + i];
        uint16_t inv_p = gf16_inv(pivot_val);
        for (int k = 0; k < ds; k++) {
            src[i * ds + k] = gf16_mul(src[i * ds + k], inv_p);
            dst[i * ds + k] = gf16_mul(dst[i * ds + k], inv_p);
        }

        for (int j = 0; j < ds; j++) {
            if (i == j)
                continue;
            uint16_t factor = src[j * ds + i];
            if (factor != 0) {
                for (int k = 0; k < ds; k++) {
                    src[j * ds + k] ^= gf16_mul(factor, src[i * ds + k]);
                    dst[j * ds + k] ^= gf16_mul(factor, dst[i * ds + k]);
                }
            }
        }
    }
    return 0;
}

void reed_solomon16_init(void)
{
    oblas16_init();
}

reed_solomon16 *reed_solomon16_new(int data_shards, int parity_shards)
{
    if (data_shards <= 0 || data_shards > DATA_SHARDS_MAX || parity_shards <= 0 || parity_shards > DATA_SHARDS_MAX)
        return NULL;

    reed_solomon16_init();

    struct oblas16_impl impl;
    oblas16_get_impl(&impl);

    reed_solomon16 *rs = (reed_solomon16 *)calloc(1, sizeof(reed_solomon16));
    rs->ds = data_shards;
    rs->ps = parity_shards;
    rs->ts = data_shards + parity_shards;
    rs->axpy = impl.axpy;
    rs->scal = impl.scal;
    rs->axiy = impl.axiy;
    rs->align_size = impl.align_size;

    rs->p = (uint16_t *)calloc(rs->ps * rs->ds, sizeof(uint16_t));
    for (int j = 0; j < rs->ps; j++) {
        for (int i = 0; i < rs->ds; i++) {
            rs->p[j * rs->ds + i] = gf16_inv((rs->ps + i) ^ j);
        }
    }

    return rs;
}

void reed_solomon16_release(reed_solomon16 *rs)
{
    if (rs) {
        if (rs->p)
            free(rs->p);
        free(rs);
    }
}

int reed_solomon16_encode(reed_solomon16 *rs, uint16_t **shards, int nr_shards, int bs)
{
    if (!rs || !shards || nr_shards < rs->ts || bs <= 0)
        return -1;

    for (int i = 0; i < rs->ps; i++) {
        memset(shards[rs->ds + i], 0, bs * sizeof(uint16_t));
        for (int j = 0; j < rs->ds; j++) {
            uint16_t coef = rs->p[i * rs->ds + j];
            if (coef == 0)
                continue;
            uint16_t *dst = shards[rs->ds + i];
            uint16_t *src = shards[j];
            rs->axpy(dst, src, coef, bs);
        }
    }
    return 0;
}

int reed_solomon16_decode(reed_solomon16 *rs, uint16_t **shards, uint8_t *marks, int nr_shards, int bs)
{
    if (!rs || !shards || !marks || nr_shards < rs->ts || bs <= 0) {
        return -1;
    }

    int gaps = 0;
    uint16_t erasures[DATA_SHARDS_MAX];
    uint16_t surviving[DATA_SHARDS_MAX];

    for (int i = 0; i < rs->ds; i++) {
        if (marks[i]) {
            erasures[gaps++] = i;
        }
    }

    if (gaps == 0)
        return 0;

    int surv_idx = 0;
    for (int i = 0; i < rs->ts; i++) {
        if (!marks[i]) {
            surviving[surv_idx++] = i;
        }
    }

    if (surv_idx < rs->ds) {
        return -1;
    }

    uint16_t *decode_mat = (uint16_t *)calloc(rs->ds * rs->ds, sizeof(uint16_t));
    for (int i = 0; i < rs->ds; i++) {
        int r = surviving[i];
        if (r < rs->ds) {
            decode_mat[i * rs->ds + r] = 1;
        } else {
            int p_idx = r - rs->ds;
            for (int j = 0; j < rs->ds; j++) {
                decode_mat[i * rs->ds + j] = rs->p[p_idx * rs->ds + j];
            }
        }
    }

    uint16_t *decode_mat_inv = (uint16_t *)calloc(rs->ds * rs->ds, sizeof(uint16_t));
    if (invert_mat_gf16(decode_mat, decode_mat_inv, rs->ds) != 0) {
        free(decode_mat);
        free(decode_mat_inv);
        return -1;
    }

    for (int i = 0; i < gaps; i++) {
        memset(shards[erasures[i]], 0, bs * sizeof(uint16_t));
    }

    for (int i = 0; i < rs->ds; i++) {
        for (int j = 0; j < rs->ds; j++) {
            uint16_t coef = decode_mat_inv[i * rs->ds + j];
            if (coef == 0)
                continue;
            uint16_t *src = shards[surviving[j]];
            int e_idx = -1;
            for (int k = 0; k < gaps; k++)
                if (erasures[k] == i)
                    e_idx = k;
            if (e_idx != -1) {
                uint16_t *dst = shards[erasures[e_idx]];
                rs->axpy(dst, src, coef, bs);
            }
        }
    }

    free(decode_mat);
    free(decode_mat_inv);

    for (int i = 0; i < rs->ps; i++) {
        if (marks[rs->ds + i]) {
            memset(shards[rs->ds + i], 0, bs * sizeof(uint16_t));
            for (int j = 0; j < rs->ds; j++) {
                uint16_t coef = rs->p[i * rs->ds + j];
                if (coef == 0)
                    continue;
                uint16_t *dst = shards[rs->ds + i];
                uint16_t *src = shards[j];
                rs->axpy(dst, src, coef, bs);
            }
        }
    }

    return 0;
}
