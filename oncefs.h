#ifndef _ONCEFS_H
#define _ONCEFS_H

#include <stdint.h>

#include "lib/io.h"
#include "lib/table.h"

#define ONCEFS_NAME_MAX_SIZE 256

typedef struct oncefs_tag {
    uint64_t seq;
    char operation;
} oncefs_tag_t;

typedef struct oncefs_node {
    uint32_t node;
    uint32_t parent;
    char type;
    uint64_t last_access;
    uint64_t last_modification;
    uint16_t mode;
    char name[ONCEFS_NAME_MAX_SIZE + 1]; // +1 to append \0
} oncefs_node_t;

typedef struct oncefs_data {
    uint32_t node;
    uint16_t fill;
    uint64_t offset;
} oncefs_data_t;

typedef struct oncefs_block {
    uint32_t block;
    oncefs_tag_t tag;
    oncefs_data_t data;
} oncefs_block_t;

typedef struct oncefs_status_t {
    int block_size;
    size_t total_blocks;
    size_t free_blocks;
    int name_max_size;
} oncefs_status_t;

typedef struct oncefs_stat_t {
    size_t node;
    char is_dir;
    char is_file;
    char is_link;
    size_t size;
    off_t time;
    int mode;
    time_t last_access;
    time_t last_modification;
} oncefs_stat_t;

typedef struct oncefs {
    unsigned long next_node_id;
    unsigned long first_block_id;
    unsigned long last_block_id;
    unsigned long next_block_id;
    unsigned long next_seq_id;
    time_t time;
    table_t nodes;
    table_t blocks;
    io_t *io;
    int payload_size;
    int block_size;
} oncefs_t;

#define ONCEFS_OVERHEAD_SIZE (sizeof(oncefs_tag_t) + sizeof(oncefs_data_t))

int oncefs_init(oncefs_t *ofs, io_t *io, int format);
#define oncefs_init_default(ofs) oncefs_init(ofs, NULL, 0)
void oncefs_free(oncefs_t *ofs);

int oncefs_set_file(oncefs_t *ofs, const char *path);
int oncefs_set_dir(oncefs_t *ofs, const char *path);
int oncefs_set_link(oncefs_t *ofs, const char *from, const char *to);
int oncefs_set_time(oncefs_t *ofs, const char *path, time_t last_access, time_t last_modification);
size_t oncefs_set_data(oncefs_t *ofs, uint32_t node, const char *data, size_t size,
                    uint64_t offset);

int oncefs_get_status(oncefs_t *ofs, oncefs_status_t *result);
int oncefs_get_node(oncefs_t *ofs, const char *path, oncefs_stat_t *result);
int oncefs_get_dir(oncefs_t *ofs, const char *path,
                   int (*callback)(oncefs_node_t *entry));
int oncefs_get_link(oncefs_t *ofs, const char *path, oncefs_node_t *result);
size_t oncefs_get_data(oncefs_t *ofs, uint32_t node, char *data, size_t size,
                    uint64_t offset);

int oncefs_move_node(oncefs_t *ofs, const char *from, const char *to);

int oncefs_del_node(oncefs_t *ofs, const char *path);
int oncefs_del_data(oncefs_t *ofs, uint32_t node, uint64_t from);

int oncefs_sync(oncefs_t *ofs);

void oncefs_dumps(oncefs_t *ofs, char *buffer);
#define oncefs_dump(ofs) oncefs_dumps(ofs, NULL)

#endif
