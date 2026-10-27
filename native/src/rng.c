/*
 * rng.c - deterministic POSIX lrand48-compatible generator.
 *
 * The original game seeds with srand48() and uses lrand48() through the
 * INTRAND macro.  Reproducing the same 48-bit LCG keeps ported rules
 * deterministic and makes the original random sequence available for later
 * combat and robot work.
 */

#include "dtrek_internal.h"

#define DTREK_LRAND48_MULTIPLIER 0x5DEECE66DULL
#define DTREK_LRAND48_INCREMENT 0x0BULL
#define DTREK_LRAND48_MASK 0xFFFFFFFFFFFFULL

void dtrek_rng_seed(DtrekRng *rng, uint32_t seed)
{
    rng->state = (((uint64_t)seed << 16) | 0x330EULL) & DTREK_LRAND48_MASK;
}

int32_t dtrek_rng_next(DtrekRng *rng)
{
    rng->state = (rng->state * DTREK_LRAND48_MULTIPLIER
                  + DTREK_LRAND48_INCREMENT) & DTREK_LRAND48_MASK;
    return (int32_t)(rng->state >> 17);
}

int32_t dtrek_rng_below(DtrekRng *rng, int32_t limit)
{
    if (limit <= 0) return 0;
    return dtrek_rng_next(rng) % limit;
}
