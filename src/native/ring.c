/*
 * CSOS Ring -- Lock-free single-producer single-consumer circular buffer.
 *
 * Works for: physics rings, message bus, transport, WAL, f_history.
 * Same implementation, different backing memory (heap, mmap, RDMA MR).
 */
#include "../../lib/ring.h"
#include <stdlib.h>
#include <string.h>

/* Round up to next power of 2 */
static uint64_t next_pow2(uint64_t v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4;
    v |= v >> 8; v |= v >> 16; v |= v >> 32;
    return v + 1;
}

int csos_ring_init(csos_ring_t *r, uint64_t capacity, uint16_t slot_size) {
    memset(r, 0, sizeof(*r));
    r->capacity = next_pow2(capacity);
    r->slot_size = slot_size;
    uint64_t buf_size = r->capacity * (slot_size ? slot_size : 256);
    r->buffer = (uint8_t *)calloc(1, buf_size);
    if (!r->buffer) return -1;
    return 0;
}

int csos_ring_init_buf(csos_ring_t *r, uint64_t capacity, uint16_t slot_size,
                       uint8_t *buf) {
    memset(r, 0, sizeof(*r));
    r->capacity = next_pow2(capacity);
    r->slot_size = slot_size;
    r->buffer = buf;
    return 0;
}

void csos_ring_destroy(csos_ring_t *r) {
    if (r->buffer) {
        free(r->buffer);
        r->buffer = NULL;
    }
}

int csos_ring_push(csos_ring_t *r, const void *data, uint16_t len) {
    if (csos_ring_full(r)) return -1;
    uint16_t actual = r->slot_size ? r->slot_size : len;
    uint64_t pos = r->write_pos & (r->capacity - 1);
    uint64_t byte_pos = pos * (r->slot_size ? r->slot_size : 256);
    if (!r->slot_size) {
        /* Variable: store length prefix */
        memcpy(r->buffer + byte_pos, &len, 2);
        memcpy(r->buffer + byte_pos + 2, data, len);
    } else {
        uint16_t copy = len < actual ? len : actual;
        memcpy(r->buffer + byte_pos, data, copy);
    }
    r->write_pos++;
    return 0;
}

int csos_ring_pop(csos_ring_t *r, void *data, uint16_t *len) {
    if (csos_ring_empty(r)) return -1;
    uint64_t pos = r->read_pos & (r->capacity - 1);
    uint64_t byte_pos = pos * (r->slot_size ? r->slot_size : 256);
    if (!r->slot_size) {
        uint16_t slen;
        memcpy(&slen, r->buffer + byte_pos, 2);
        if (len) *len = slen;
        if (data) memcpy(data, r->buffer + byte_pos + 2, slen);
    } else {
        if (len) *len = r->slot_size;
        if (data) memcpy(data, r->buffer + byte_pos, r->slot_size);
    }
    r->read_pos++;
    return 0;
}

void *csos_ring_peek(csos_ring_t *r, uint64_t offset) {
    if (offset >= csos_ring_depth(r)) return NULL;
    uint64_t pos = (r->read_pos + offset) & (r->capacity - 1);
    uint64_t byte_pos = pos * (r->slot_size ? r->slot_size : 256);
    if (!r->slot_size) return r->buffer + byte_pos + 2; /* skip length prefix */
    return r->buffer + byte_pos;
}
