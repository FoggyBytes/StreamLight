#ifndef RS16_AFFT_H
#define RS16_AFFT_H

#include "rs16.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int ds;
    int ps;
    int ts;
    void *p;
} reed_solomon16_afft;

void reed_solomon16_afft_init(void);
reed_solomon16_afft *reed_solomon16_afft_new(int data_shards, int parity_shards);
void reed_solomon16_afft_release(reed_solomon16_afft *rs);

int reed_solomon16_afft_encode(reed_solomon16_afft *rs, uint16_t **shards, int nr_shards, int bs);
int reed_solomon16_afft_decode(reed_solomon16_afft *rs, uint16_t **shards, uint8_t *marks, int nr_shards, int bs);

#ifdef __cplusplus
}
#endif

#endif /* RS16_AFFT_H */
