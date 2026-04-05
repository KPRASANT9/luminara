/*
 * CSOS Record -- Variable-size typed data within a Page.
 *
 * A Photon, an Atom header, a session field, an IORequest, an agent state --
 * all Records. Same pack/unpack functions. Same header format.
 */
#ifndef CSOS_RECORD_H
#define CSOS_RECORD_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    REC_PHOTON    = 1,   /* Signal observation: 29 bytes fixed */
    REC_ATOM      = 2,   /* Atom metadata: variable */
    REC_RING_HDR  = 3,   /* Ring header: cycles, signals, F */
    REC_SESSION   = 4,   /* Human key-value data */
    REC_PHYSICS   = 5,   /* Physics result: decision, delta, speed, rw */
    REC_AGENT     = 6,   /* Agent state: mode, steps, consecutive_zero */
    REC_IO_REQ    = 7,   /* I/O request */
    REC_IO_RESP   = 8,   /* I/O response */
    REC_MESSAGE   = 9,   /* Transport message */
} csos_record_type_t;

typedef struct {
    uint16_t type;       /* csos_record_type_t */
    uint16_t length;     /* Total length including this header */
    uint32_t key_hash;   /* FNV-1a hash for indexing */
    uint8_t  data[];     /* Variable payload */
} __attribute__((packed)) csos_record_t;

#define CSOS_RECORD_HDR_SIZE 8

/* Photon record layout: 29 bytes in data[] */
typedef struct {
    uint32_t cycle;
    double   predicted;
    double   actual;
    double   error;
    uint8_t  resonated;
} __attribute__((packed)) csos_photon_rec_t;

/* Physics result layout: 24 bytes */
typedef struct {
    uint8_t  decision;       /* 0=EXPLORE, 1=EXECUTE */
    int32_t  delta;
    float    speed;
    float    rw;
    float    F;
    float    action_ratio;
} __attribute__((packed)) csos_physics_rec_t;

/* Session record layout: variable */
typedef struct {
    uint16_t key_len;
    uint16_t val_len;
    uint8_t  data[];    /* key bytes then value bytes */
} __attribute__((packed)) csos_session_rec_t;

/* FNV-1a hash */
static inline uint32_t csos_fnv1a(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0x811c9dc5;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x01000193;
    }
    return h;
}

/* Composite key hash: ring + atom + cycle */
static inline uint32_t csos_photon_key(const char *ring, const char *atom, uint32_t cycle) {
    uint32_t h = 0x811c9dc5;
    for (const char *p = ring; *p; p++) { h ^= (uint8_t)*p; h *= 0x01000193; }
    h ^= ':'; h *= 0x01000193;
    for (const char *p = atom; *p; p++) { h ^= (uint8_t)*p; h *= 0x01000193; }
    h ^= ':'; h *= 0x01000193;
    h ^= (cycle & 0xFF); h *= 0x01000193;
    h ^= ((cycle >> 8) & 0xFF); h *= 0x01000193;
    h ^= ((cycle >> 16) & 0xFF); h *= 0x01000193;
    h ^= ((cycle >> 24) & 0xFF); h *= 0x01000193;
    return h;
}

#endif /* CSOS_RECORD_H */
