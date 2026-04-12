/*
 * CSOS Index -- Sorted key->page_id mapping with tree traversal.
 *
 * One implementation, two modes:
 *   leaf_chain=false -> B-Tree  (data in internal nodes, for storage)
 *   leaf_chain=true  -> B+ Tree (data only in leaves, for indexed queries)
 *
 * Used by: L1 Store (B-Tree for data, B+ for indexes),
 * Forster coupling table, law enforcement patterns.
 */
#ifndef CSOS_INDEX_H
#define CSOS_INDEX_H

#include "page.h"
#include <stdint.h>

#define CSOS_INDEX_ORDER    128   /* Keys per node, fits in 4KB page */
#define CSOS_INDEX_MAX_RESULTS 1024

typedef struct {
    uint32_t key_hash;
    uint32_t page_id;
    uint16_t offset;
} csos_index_entry_t;

typedef struct {
    int       fd;               /* File descriptor for page I/O */
    uint32_t  root_page;        /* Root page ID */
    uint32_t  page_count;       /* Total pages allocated */
    uint32_t  entry_count;      /* Total entries */
    int       leaf_chain;       /* 0=B-Tree, 1=B+ Tree */
} csos_index_t;

/* Iterator for range scans (B+ mode) */
typedef struct {
    csos_index_t *idx;
    uint32_t      current_page;
    uint16_t      current_pos;
    uint32_t      key_hi;       /* Upper bound */
} csos_index_iter_t;

/* Lifecycle */
int  csos_index_open(csos_index_t *idx, const char *path, int leaf_chain);
void csos_index_close(csos_index_t *idx);

/* Point operations */
int  csos_index_insert(csos_index_t *idx, uint32_t key_hash,
                       uint32_t page_id, uint16_t offset);
int  csos_index_lookup(csos_index_t *idx, uint32_t key_hash,
                       uint32_t *page_id, uint16_t *offset);
int  csos_index_delete(csos_index_t *idx, uint32_t key_hash);

/* Range operations (B+ mode, uses leaf chain) */
int  csos_index_range_open(csos_index_t *idx, uint32_t key_lo, uint32_t key_hi,
                           csos_index_iter_t *iter);
int  csos_index_range_next(csos_index_iter_t *iter, csos_index_entry_t *entry);
void csos_index_range_close(csos_index_iter_t *iter);

/* Batch range query (convenience) */
int  csos_index_range(csos_index_t *idx, uint32_t key_lo, uint32_t key_hi,
                      csos_index_entry_t *results, int max_results);

#endif /* CSOS_INDEX_H */
