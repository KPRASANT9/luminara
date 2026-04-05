/*
 * CSOS Store -- L1 persistence layer.
 * B-Tree storage + B+ Index + WAL for crash recovery.
 * Replaces Core._save()/_load() and all JSON file I/O.
 */
#include "../../lib/page.h"
#include "../../lib/record.h"
#include "../../lib/index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

typedef struct {
    char          base_path[256];
    int           data_fd;       /* .csos/data.db */
    int           wal_fd;        /* .csos/wal.log */
    uint32_t      next_page_id;
    /* In-memory page cache (simple direct-mapped) */
    #define STORE_CACHE_SIZE 256
    csos_page_t   cache[STORE_CACHE_SIZE];
    uint32_t      cache_ids[STORE_CACHE_SIZE];
    uint8_t       cache_dirty[STORE_CACHE_SIZE];
    uint8_t       cache_valid[STORE_CACHE_SIZE];
} csos_store_t;

/* Forward declarations */
static int csos_store_wal_recover(csos_store_t *store);

static void store_ensure_dir(const char *base) {
    char path[512];
    snprintf(path, sizeof(path), "%s/.csos", base);
    mkdir(path, 0755);
}

int csos_store_open(csos_store_t *store, const char *base_path) {
    memset(store, 0, sizeof(*store));
    strncpy(store->base_path, base_path, sizeof(store->base_path) - 1);
    store_ensure_dir(base_path);

    char path[512];
    snprintf(path, sizeof(path), "%s/.csos/data.db", base_path);
    store->data_fd = open(path, O_RDWR | O_CREAT, 0644);
    if (store->data_fd < 0) return -1;

    snprintf(path, sizeof(path), "%s/.csos/wal.log", base_path);
    store->wal_fd = open(path, O_RDWR | O_CREAT, 0644);
    if (store->wal_fd < 0) { close(store->data_fd); return -1; }

    /* Determine next page ID from file size */
    off_t sz = lseek(store->data_fd, 0, SEEK_END);
    store->next_page_id = (sz > 0) ? (uint32_t)(sz / CSOS_PAGE_SIZE) : 0;

    /* Recover from WAL if needed */
    csos_store_wal_recover(store);
    return 0;
}

void csos_store_close(csos_store_t *store) {
    /* Flush dirty cache pages */
    for (int i = 0; i < STORE_CACHE_SIZE; i++) {
        if (store->cache_valid[i] && store->cache_dirty[i]) {
            csos_page_write(store->data_fd, &store->cache[i]);
        }
    }
    if (store->data_fd >= 0) { fsync(store->data_fd); close(store->data_fd); }
    if (store->wal_fd >= 0) { close(store->wal_fd); }
}

/* WAL: write-ahead log for crash safety */
#define WAL_MAGIC 0x57414C31   /* "WAL1" */

typedef struct {
    uint32_t magic;
    uint32_t page_id;
    uint32_t committed;   /* 0=pending, 1=committed */
    uint32_t reserved;
} wal_header_t;

int csos_store_wal_begin(csos_store_t *store) {
    /* Truncate WAL to mark new transaction */
    ftruncate(store->wal_fd, 0);
    lseek(store->wal_fd, 0, SEEK_SET);
    return 0;
}

int csos_store_wal_write_page(csos_store_t *store, const csos_page_t *p) {
    /* Write page image to WAL before modifying data file */
    wal_header_t hdr = { WAL_MAGIC, p->page_id, 0, 0 };
    lseek(store->wal_fd, 0, SEEK_END);
    write(store->wal_fd, &hdr, sizeof(hdr));
    write(store->wal_fd, p, CSOS_PAGE_SIZE);
    fsync(store->wal_fd);
    return 0;
}

int csos_store_wal_commit(csos_store_t *store) {
    /* Mark all WAL entries as committed */
    lseek(store->wal_fd, 0, SEEK_SET);
    wal_header_t hdr;
    while (read(store->wal_fd, &hdr, sizeof(hdr)) == sizeof(hdr)) {
        if (hdr.magic == WAL_MAGIC && !hdr.committed) {
            hdr.committed = 1;
            lseek(store->wal_fd, -(off_t)sizeof(hdr), SEEK_CUR);
            write(store->wal_fd, &hdr, sizeof(hdr));
        }
        lseek(store->wal_fd, CSOS_PAGE_SIZE, SEEK_CUR);
    }
    fsync(store->wal_fd);
    /* Now write pages to data file */
    lseek(store->wal_fd, 0, SEEK_SET);
    csos_page_t page;
    while (read(store->wal_fd, &hdr, sizeof(hdr)) == sizeof(hdr)) {
        if (read(store->wal_fd, &page, CSOS_PAGE_SIZE) == CSOS_PAGE_SIZE) {
            if (hdr.committed) {
                csos_page_write(store->data_fd, &page);
            }
        }
    }
    fsync(store->data_fd);
    ftruncate(store->wal_fd, 0);
    return 0;
}

int csos_store_wal_recover(csos_store_t *store) {
    /* Replay committed WAL entries that didn't make it to data file */
    off_t sz = lseek(store->wal_fd, 0, SEEK_END);
    if (sz <= 0) return 0;
    lseek(store->wal_fd, 0, SEEK_SET);
    wal_header_t hdr;
    csos_page_t page;
    int recovered = 0;
    while (read(store->wal_fd, &hdr, sizeof(hdr)) == sizeof(hdr)) {
        if (read(store->wal_fd, &page, CSOS_PAGE_SIZE) == CSOS_PAGE_SIZE) {
            if (hdr.magic == WAL_MAGIC && hdr.committed) {
                csos_page_write(store->data_fd, &page);
                recovered++;
            }
        }
    }
    fsync(store->data_fd);
    ftruncate(store->wal_fd, 0);
    return recovered;
}

/* Allocate a new page */
uint32_t csos_store_alloc_page(csos_store_t *store) {
    return store->next_page_id++;
}

/* Cache-backed page read */
int csos_store_get_page(csos_store_t *store, uint32_t page_id, csos_page_t **out) {
    int slot = page_id & (STORE_CACHE_SIZE - 1);
    if (store->cache_valid[slot] && store->cache_ids[slot] == page_id) {
        *out = &store->cache[slot];
        return 0;
    }
    /* Evict if dirty */
    if (store->cache_valid[slot] && store->cache_dirty[slot]) {
        csos_page_write(store->data_fd, &store->cache[slot]);
        store->cache_dirty[slot] = 0;
    }
    /* Read from disk */
    int rc = csos_page_read(store->data_fd, page_id, &store->cache[slot]);
    if (rc == 0) {
        store->cache_ids[slot] = page_id;
        store->cache_valid[slot] = 1;
        store->cache_dirty[slot] = 0;
        *out = &store->cache[slot];
    }
    return rc;
}

/* Write a page through cache */
int csos_store_put_page(csos_store_t *store, csos_page_t *p) {
    int slot = p->page_id & (STORE_CACHE_SIZE - 1);
    /* WAL first */
    csos_store_wal_write_page(store, p);
    /* Update cache */
    memcpy(&store->cache[slot], p, sizeof(csos_page_t));
    store->cache_ids[slot] = p->page_id;
    store->cache_valid[slot] = 1;
    store->cache_dirty[slot] = 1;
    return 0;
}

/* Flush all dirty pages and commit WAL */
int csos_store_flush(csos_store_t *store) {
    for (int i = 0; i < STORE_CACHE_SIZE; i++) {
        if (store->cache_valid[i] && store->cache_dirty[i]) {
            csos_page_write(store->data_fd, &store->cache[i]);
            store->cache_dirty[i] = 0;
        }
    }
    fsync(store->data_fd);
    ftruncate(store->wal_fd, 0);
    return 0;
}
