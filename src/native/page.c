/*
 * CSOS Page -- Fixed 4096-byte storage operations.
 * CRC32 checksumming, page-aligned I/O, append records.
 */
#include "../../lib/page.h"
#include <unistd.h>
#include <errno.h>

/* CRC32C lookup table (Castagnoli polynomial) */
static uint32_t crc32c_table[256];
static int crc32c_init = 0;

static void crc32c_build_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ ((c & 1) ? 0x82F63B78 : 0);
        crc32c_table[i] = c;
    }
    crc32c_init = 1;
}

uint32_t csos_page_checksum(const csos_page_t *p) {
    if (!crc32c_init) crc32c_build_table();
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *buf = p->payload;
    for (uint16_t i = 0; i < p->free_offset; i++)
        crc = crc32c_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

int csos_page_verify(const csos_page_t *p) {
    if (p->magic != CSOS_PAGE_MAGIC) return -1;
    if (p->version != 1) return -2;
    if (p->free_offset > CSOS_PAGE_PAYLOAD) return -3;
    if (p->checksum != csos_page_checksum(p)) return -4;
    return 0;
}

int csos_page_write(int fd, const csos_page_t *p) {
    off_t offset = (off_t)p->page_id * CSOS_PAGE_SIZE;
    ssize_t n = pwrite(fd, p, CSOS_PAGE_SIZE, offset);
    return (n == CSOS_PAGE_SIZE) ? 0 : -1;
}

int csos_page_read(int fd, uint32_t page_id, csos_page_t *p) {
    off_t offset = (off_t)page_id * CSOS_PAGE_SIZE;
    ssize_t n = pread(fd, p, CSOS_PAGE_SIZE, offset);
    if (n != CSOS_PAGE_SIZE) return -1;
    return 0;
}

int csos_page_append(csos_page_t *p, const void *data, uint16_t len) {
    if (p->free_offset + len > CSOS_PAGE_PAYLOAD) return -1;
    int offset = p->free_offset;
    memcpy(p->payload + p->free_offset, data, len);
    p->free_offset += len;
    p->record_count++;
    p->checksum = csos_page_checksum(p);
    return offset;
}
