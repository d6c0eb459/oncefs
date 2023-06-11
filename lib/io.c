#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// for open
#include <fcntl.h>
#include <sys/stat.h>

// for pread, pwrite
#include <unistd.h>
#define _XOPEN_SOURCE 700

#include "io.h"

int _io_init_file(io_t *io, io_config_t *config) {
    // Determine size of underlying file
    FILE *fp = fopen(config->path, "r+b");
    if (fp == NULL) { return -errno; }
    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fclose(fp);

    // Config
    size_t num_blocks = size / config->block_size;
    if(config->max_num_blocks > 0 && config->max_num_blocks < num_blocks) {
        num_blocks = config->max_num_blocks;
    }
    if(num_blocks <= 0) {
        return -ENOSPC;
    }
    io->last_valid_block = num_blocks - 1;

    // Open
    io->fh = open(config->path, O_RDWR);
    if (io->fh == -1) { return -errno; }

    return 0;
}

int _io_init_memory(io_t *io, io_config_t *config) {
    if(config->max_num_blocks <= 0) {
        return -EINVAL;
    }

    // Config
    io->last_valid_block = config->max_num_blocks - 1;

    // Allocate
    size_t size = config->max_num_blocks * config->block_size;
    void *buffer = malloc(size);
    if (buffer == NULL) { return -ENOMEM; }
    io->buffer = buffer;

    return 0;
}

int io_init(io_t *io, io_config_t *config) {
    if(config->block_size <= 0) {
        return -EINVAL;
    }

    io->fh = -1;
    io->buffer = NULL;
    io->block_size = config->block_size;

    int r;
    if (strcmp(config->path, ":memory:") == 0) {
        r = _io_init_memory(io, config);
    } else {
        r = _io_init_file(io, config);
    }

    if (r != 0) { return r; }

    return 0;
}

void io_close(io_t *io) {
    if (io->fh != -1) { close(io->fh); }

    if (io->buffer != NULL) { free(io->buffer); }
}

int io_write3(io_t *io, size_t block, const void *data, int size, const void *data2,
              int size2, const void *data3, int size3) {
    if (block > io->last_valid_block) {
        return -EOVERFLOW; // past underlying file
    }

    if (size + size2 + size3 > io->block_size) {
        return -EINVAL; // past block
    }

    size_t start = block * io->block_size;
    if (start < 0) {
        return -EINVAL; // past block
    }

    char buffer[size + size2 + size3];
    if (data != NULL) { memcpy(buffer, data, size); }
    if (data2 != NULL) { memcpy(buffer + size, data2, size2); }
    if (data3 != NULL) { memcpy(buffer + size + size2, data3, size3); }

    if (io->fh != -1) {
        pwrite(io->fh, buffer, size + size2 + size3, start);
    } else if (io->buffer != NULL) {
        memcpy(io->buffer + start, buffer, size + size2 + size3);
    }

    return 0;
}

int io_read3(io_t *io, size_t block, void *data, int size, void *data2, int size2,
             void *data3, int size3) {
    if (block > io->last_valid_block) {
        return -EOVERFLOW; // past underlying file
    }

    if (size + size2 + size3 > io->block_size) {
        return -EINVAL; // past block
    }

    size_t start = block * io->block_size;
    if (start < 0) {
        return -EINVAL; // past block
    }

    char buffer[size + size2 + size3];

    if (io->fh != -1) {
        pread(io->fh, buffer, size + size2 + size3, start);
    } else if (io->buffer != NULL) {
        memcpy(buffer, io->buffer + start, size + size2 + size3);
    }

    if (data != NULL) { memcpy(data, buffer, size); }
    if (data2 != NULL) { memcpy(data2, buffer + size, size2); }
    if (data3 != NULL) { memcpy(data3, buffer + size + size2, size3); }

    return 0;
}

int io_sync(io_t *io) {
    if(io->fh != -1) {
        return fsync(io->fh);
    }

    return 0;
}

size_t io_block_size(io_t *io) {
    return io->block_size;
}

size_t io_block_first(io_t *io) {
    return IO_BLOCK_FIRST;
}

size_t io_block_last(io_t *io) {
    return io->last_valid_block;
}
