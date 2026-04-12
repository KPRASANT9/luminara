/*
 * CSOS Page -- Fixed 4096-byte storage unit.
 *
 * Every piece of data in the system lives in a Page.
 * B-Tree nodes are Pages. B+ Tree nodes are Pages.
 * RDMA transfers Pages. WAL logs Pages.
 * ONE read/write function for ALL page types.
 */
#ifndef CSOS_PAGE_H
#define CSOS_PAGE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define CSOS_PAGE_SIZE     4096
#define CSOS_PAGE_MAGIC    0x4353   /* "CS" */
#define CSOS_PAGE_HDR_SIZE 16
#define CSOS_PAGE_PAYLOAD  (CSOS_PAGE_SIZE - CSOS_PAGE_HDR_SIZE)

typedef enum {
    PAGE_FREE           = 0,
    PAGE_BTREE_INTERNAL = 1,
    PAGE_BTREE_LEAF     = 2,
    PAGE_BPLUS_INTERNAL = 3,
    PAGE_BPLUS_LEAF     = 4,
    PAGE_RING_STATE     = 5,
    PAGE_ATOM_DATA      = 6,
    PAGE_PHOTON_BATCH   = 7,
    PAGE_SESSION        = 8,
    PAGE_WAL_ENTRY      = 9,
    PAGE_OVERFLOW       = 10,
} csos_page_type_t;

typedef struct {
    uint16_t magic;          /* CSOS_PAGE_MAGIC */
    uint8_t  version;        /* 1 */
    uint8_t  type;           /* csos_page_type_t */
    uint32_t page_id;
    uint32_t checksum;       /* CRC32C of payload */
    uint16_t record_count;
    uint16_t free_offset;    /* next free byte in payload */
    uint8_t  payload[CSOS_PAGE_PAYLOAD];
} __attribute__((packed)) csos_page_t;

/* Page operations */
static inline void csos_page_init(csos_page_t *p, uint32_t id, csos_page_type_t type) {
    memset(p, 0, sizeof(*p));
    p->magic = CSOS_PAGE_MAGIC;
    p->version = 1;
    p->type = (uint8_t)type;
    p->page_id = id;
    p->free_offset = 0;
}

uint32_t csos_page_checksum(const csos_page_t *p);
int      csos_page_verify(const csos_page_t *p);
int      csos_page_write(int fd, const csos_page_t *p);
int      csos_page_read(int fd, uint32_t page_id, csos_page_t *p);

/* Append record bytes to page payload. Returns offset or -1 if full. */
int      csos_page_append(csos_page_t *p, const void *data, uint16_t len);

#endif /* CSOS_PAGE_H */
