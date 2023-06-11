#ifndef _MICROFS_IO_H
#define _MICROFS_IO_H

/**
 * Write and read to specific sections of a file.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#define IO_BLOCK_NULL 0
#define IO_BLOCK_FIRST 1

typedef struct {
    char *path;
    int block_size;
    size_t max_num_blocks;
} io_config_t;

typedef struct {
    int fh;
    int block_size;
    size_t last_valid_block;
    void *buffer; // for in-memory operations
} io_t;

int io_init(io_t *io, io_config_t *config);
void io_close(io_t *io);

int io_write3(io_t *io, size_t block, const void *data, int size, const void *data2,
              int size2, const void *data3, int size3);
#define io_write2(i, b, d, s, d2, s2) io_write3(i, b, d, s, d2, s2, NULL, 0)
#define io_write(i, b, d, s) io_write2(i, b, d, s, NULL, 0)

int io_read3(io_t *io, size_t block, void *data, int size, void *data2, int size2,
             void *data3, int size3);
#define io_read2(i, b, d, s, d2, s2) io_read3(i, b, d, s, d2, s2, NULL, 0)
#define io_read(i, b, d, s) io_read2(i, b, d, s, NULL, 0)

int io_sync(io_t *io);

size_t io_block_size(io_t *io);
// Get the first valid block; others before it may be reserved
size_t io_block_first(io_t *io);
// Get the last valid block given the underlying file size
size_t io_block_last(io_t *io);

#endif
