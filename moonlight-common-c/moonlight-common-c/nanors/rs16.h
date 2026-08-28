#ifndef __RS16_H_
#define __RS16_H_

#include <stdint.h>
#include <stddef.h>
#include "oblas_common.h"

#define DATA_SHARDS_MAX 65535

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _reed_solomon16 {
    int ds;
    int ps;
    int ts;
    void (*axpy)(uint16_t *a, const uint16_t *b, uint16_t u, unsigned k);
    void (*scal)(uint16_t *a, uint16_t u, unsigned k);
    void (*axiy)(uint16_t *a, const uint16_t *b, uint16_t u, unsigned k);
    size_t align_size;
    uint16_t *p;
} reed_solomon16;

void reed_solomon16_init(void);
reed_solomon16 *reed_solomon16_new(int data_shards, int parity_shards);
void reed_solomon16_release(reed_solomon16 *rs);

int reed_solomon16_encode(reed_solomon16 *rs, uint16_t **shards, int nr_shards, int bs);
int reed_solomon16_decode(reed_solomon16 *rs, uint16_t **shards, uint8_t *marks, int nr_shards, int bs);

#ifdef __cplusplus
}
#endif

#endif
