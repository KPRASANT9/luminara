/*
 * CSOS Record -- Pack/unpack typed records into Pages.
 */
#include "../../lib/record.h"
#include <string.h>
#include <stdlib.h>

int csos_record_pack_photon(csos_record_t *r, uint32_t key_hash,
                            const csos_photon_rec_t *ph) {
    r->type = REC_PHOTON;
    r->length = CSOS_RECORD_HDR_SIZE + sizeof(csos_photon_rec_t);
    r->key_hash = key_hash;
    memcpy(r->data, ph, sizeof(csos_photon_rec_t));
    return 0;
}

int csos_record_unpack_photon(const csos_record_t *r, csos_photon_rec_t *ph) {
    if (r->type != REC_PHOTON) return -1;
    memcpy(ph, r->data, sizeof(csos_photon_rec_t));
    return 0;
}

int csos_record_pack_physics(csos_record_t *r, uint32_t key_hash,
                             const csos_physics_rec_t *ph) {
    r->type = REC_PHYSICS;
    r->length = CSOS_RECORD_HDR_SIZE + sizeof(csos_physics_rec_t);
    r->key_hash = key_hash;
    memcpy(r->data, ph, sizeof(csos_physics_rec_t));
    return 0;
}

int csos_record_pack_session(csos_record_t *r, uint32_t key_hash,
                             const char *key, const char *value) {
    uint16_t klen = (uint16_t)strlen(key);
    uint16_t vlen = (uint16_t)strlen(value);
    csos_session_rec_t *s = (csos_session_rec_t *)r->data;
    s->key_len = klen;
    s->val_len = vlen;
    memcpy(s->data, key, klen);
    memcpy(s->data + klen, value, vlen);
    r->type = REC_SESSION;
    r->length = CSOS_RECORD_HDR_SIZE + 4 + klen + vlen;
    r->key_hash = key_hash;
    return 0;
}
