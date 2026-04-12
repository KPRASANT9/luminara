/*
 * CSOS Ring -- Circular buffer with atomic positions.
 *
 * Used by: Physics rings (photon storage), message bus (IPC),
 * transport (RDMA queue pair), WAL (crash recovery log),
 * f_history (rolling F values), observation buffer (agent).
 *
 * ring_depth() IS gradient. ring_speed() IS speed.
 * The data structure IS the physics.
 */
#ifndef CSOS_RING_H
#define CSOS_RING_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __STDC_NO_ATOMICS__
  /* Fallback for compilers without C11 atomics */
  #define _Atomic volatile
#else
  #include <stdatomic.h>
#endif

typedef struct {
    _Atomic uint64_t write_pos;
    _Atomic uint64_t read_pos;
    uint64_t         capacity;       /* Must be power of 2 */
    uint8_t         *buffer;         /* Backing memory */
    uint16_t         slot_size;      /* Fixed slot size (0 = variable) */
} csos_ring_t;

/* Lifecycle */
int  csos_ring_init(csos_ring_t *r, uint64_t capacity, uint16_t slot_size);
int  csos_ring_init_buf(csos_ring_t *r, uint64_t capacity, uint16_t slot_size,
                        uint8_t *buf);
void csos_ring_destroy(csos_ring_t *r);

/* Operations */
int      csos_ring_push(csos_ring_t *r, const void *data, uint16_t len);
int      csos_ring_pop(csos_ring_t *r, void *data, uint16_t *len);
void    *csos_ring_peek(csos_ring_t *r, uint64_t offset);

/* Metrics (these ARE the physics properties) */
static inline uint64_t csos_ring_depth(const csos_ring_t *r) {
    return r->write_pos - r->read_pos;
}

static inline int csos_ring_full(const csos_ring_t *r) {
    return csos_ring_depth(r) >= r->capacity;
}

static inline int csos_ring_empty(const csos_ring_t *r) {
    return r->write_pos == r->read_pos;
}

#endif /* CSOS_RING_H */
