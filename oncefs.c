#include <errno.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "oncefs.h"

#define TABLE_INDEX_PRIMARY 0
#define TABLE_INDEX_LOOKUP 1

#define NODE_TYPE_DIR 1
#define NODE_TYPE_FILE 2
#define NODE_TYPE_LINK 3
#define NODE_TYPE_LINK_PAYLOAD 4

#define BLOCK_OPERATION_FREE 0
#define BLOCK_OPERATION_NODE 1
#define BLOCK_OPERATION_DATA 2
#define BLOCK_OPERATION_TRUNCATE 3
#define BLOCK_OPERATION_DELETE 4
#define BLOCK_OPERATION_MOVE 5
#define BLOCK_OPERATION_LAST 8

typedef struct oncefs_tagged_block_t {
    uint32_t block;
    oncefs_tag_t tag;
} oncefs_tagged_block_t;

/**
 * Comparison function uniquely identifying a node.
 *
 * Arguments:
 *     raw_a:   A pointer to the first node.
 *     raw_b:   A pointer to the second node.
 *
 * Returns:
 *     Comparison value.
 */
int _oncefs_node_cmp_primary(const void *raw_a, const void *raw_b) {
    oncefs_node_t *a = (oncefs_node_t *) raw_a;
    oncefs_node_t *b = (oncefs_node_t *) raw_b;

    if (a->node < b->node) {
        return -1;
    } else if (a->node > b->node) {
        return 1;
    }

    if (a->type < b->type) {
        return -1;
    } else if (a->type > b->type) {
        return 1;
    }

    return 0;
}

/**
 * Comparison function defining a node's place in a tree-like heirarchy.
 *
 * Arguments:
 *     raw_a:   A pointer to the first node.
 *     raw_b:   A pointer to the second node.
 *
 * Returns:
 *     Comparison value.
 */
int _oncefs_node_cmp_lookup(const void *raw_a, const void *raw_b) {
    oncefs_node_t *a = (oncefs_node_t *) raw_a;
    oncefs_node_t *b = (oncefs_node_t *) raw_b;

    if (a->parent < b->parent) {
        return -1;
    } else if (a->parent > b->parent) {
        return 1;
    }

    int r = strcmp(a->name, b->name);
    if (r != 0) { return r; }

    return 0;
}

/**
 * Comparison function uniquely identifying a block.
 *
 * Arguments:
 *     raw_a:   A pointer to the first block.
 *     raw_b:   A pointer to the second block.
 *
 * Returns:
 *     Comparison value.
 */
int _oncefs_block_cmp_primary(const void *raw_a, const void *raw_b) {
    oncefs_block_t *a = (oncefs_block_t *) raw_a;
    oncefs_block_t *b = (oncefs_block_t *) raw_b;

    if (a->block < b->block) {
        return -1;
    } else if (a->block > b->block) {
        return 1;
    }

    return 0;
}

/**
 * Comparison function to find all blocks for a specific operation and node.
 *
 * Arguments:
 *     raw_a:   A pointer to the first block.
 *     raw_b:   A pointer to the second block.
 *
 * Returns:
 *     Comparison value.
 */
int _oncefs_block_cmp_lookup_fuzzy(const void *raw_a, const void *raw_b) {
    oncefs_block_t *a = (oncefs_block_t *) raw_a;
    oncefs_block_t *b = (oncefs_block_t *) raw_b;

    if (a->tag.operation < b->tag.operation) {
        return -1;
    } else if (a->tag.operation > b->tag.operation) {
        return 1;
    }

    if (a->data.node < b->data.node) {
        return -1;
    } else if (a->data.node > b->data.node) {
        return 1;
    }

    return 0;
};

/**
 * Comparison function that orders blocks by fill and offset; uniquely identifies
 * blocks.
 *
 * Arguments:
 *     raw_a:   A pointer to the first block.
 *     raw_b:   A pointer to the second block.
 *
 * Returns:
 *     Comparison value.
 */
int _oncefs_block_cmp_lookup(const void *raw_a, const void *raw_b) {
    int r;
    r = _oncefs_block_cmp_lookup_fuzzy(raw_a, raw_b);
    if (r != 0) { return r; }

    oncefs_block_t *a = (oncefs_block_t *) raw_a;
    oncefs_block_t *b = (oncefs_block_t *) raw_b;

    if (a->data.offset < b->data.offset) {
        return -1;
    } else if (a->data.offset > b->data.offset) {
        return 1;
    }

    if (a->data.fill < b->data.fill) {
        return -1;
    } else if (a->data.fill > b->data.fill) {
        return 1;
    }

    if (a->block < b->block) {
        return -1;
    } else if (a->block > b->block) {
        return 1;
    }

    return 0;
}

int _oncefs_format(oncefs_t *ofs);
int _oncefs_load(oncefs_t *ofs);

/**
 * Initializer.
 *
 * Arguments:
 *     ofs:     A pointer to the instance. 
 *     io:      A pointer to an input-output instance.
 *     format:  Pass 1 to format the underlying file before reading, 0 otherwise.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int oncefs_init(oncefs_t *ofs, io_t *io, int format) {
    int r;
    ofs->next_node_id = 1;
    ofs->next_seq_id = 1;

    ofs->io = io;
    if (io != NULL) {
        ofs->last_block_id = io_block_last(ofs->io);
        ofs->first_block_id = io_block_first(ofs->io);
        ofs->block_size = io_block_size(ofs->io);
    } else {
        ofs->last_block_id = -2; // second largest possible unsigned
        ofs->first_block_id = 1;
        ofs->block_size = 64;
    }

    ofs->next_block_id = ofs->first_block_id;
    ofs->payload_size = ofs->block_size - sizeof(oncefs_tag_t) - sizeof(oncefs_data_t);
    if (ofs->payload_size < 0) { return -EINVAL; }

    r = table_init(&ofs->nodes, sizeof(oncefs_node_t), _oncefs_node_cmp_primary);
    if (r != 0) { return r; }
    r = table_add_index(&ofs->nodes, _oncefs_node_cmp_lookup);
    if (r != 0) { return r; }

    r = table_init(&ofs->blocks, sizeof(oncefs_block_t), _oncefs_block_cmp_primary);
    if (r != 0) { return r; }
    r = table_add_index(&ofs->blocks, _oncefs_block_cmp_lookup);
    if (r != 0) { return r; }

    if (io != NULL) {
        if (format == 1) {
            r = _oncefs_format(ofs);
        } else {
            r = _oncefs_load(ofs);
        }
        if (r != 0) { return r; }
    }

    ofs->time = time(NULL);

    return 0;
}

/**
 * Teardown function.
 *
 * Arguments:
 *     ofs:     A pointer to the instance. 
 */
void oncefs_free(oncefs_t *ofs) {
    table_free(&ofs->nodes);
    table_free(&ofs->blocks);
}

/**
 * Allocate a block, reusing an existing one if possible.
 *
 * Arguments:
 *     ofs:     A pointer to the instance. 
 *     block:   The destination for the newly allocated block id.
 *     node:    (optional) A node id for which any associated block may be reused.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_block_reuse(oncefs_t *ofs, uint32_t *block, uint32_t node) {
    int r;

    int _filter(const void *raw_key, const void *raw_other) {
        oncefs_block_t *k = (oncefs_block_t *) raw_key;
        oncefs_block_t *o = (oncefs_block_t *) raw_other;

        if (k->tag.operation < o->tag.operation) {
            return -1;
        } else if (k->tag.operation > o->tag.operation) {
            return 1;
        }

        return 0;
    };

    oncefs_block_t key;
    key.tag.operation = BLOCK_OPERATION_FREE;

    oncefs_block_t result;

    r = table_query_first(&ofs->blocks, (void *) &key, TABLE_INDEX_LOOKUP, _filter,
                           (void *) &result);
    if (r == 0) {
        *block = result.block;
        return 0;
    }

    // If there are no free blocks then any "delete" block
    // can safely be treated as obsolete
    key.tag.operation = BLOCK_OPERATION_DELETE;
    r = table_query_first(&ofs->blocks, (void *) &key, TABLE_INDEX_LOOKUP, _filter,
                           (void *) &result);
    if (r == 0) {
        *block = result.block;
        return 0;
    }

    if (node < 1) {
        // not a valid node id (disable takeover mode)
        return -ENOSPC;
    }

    // Attempt a same-node takeover
    key.tag.operation = BLOCK_OPERATION_NODE;
    key.data.node = node;
    r = table_query_first(&ofs->blocks, (void *) &key, TABLE_INDEX_LOOKUP,
                           _oncefs_block_cmp_lookup_fuzzy, (void *) &result);
    if (r == 0) {
        *block = result.block;
        return 0;
    }

    return -ENOSPC;
}

/**
 * Initializer for the block type.
 *
 * Arguments:
 *     ofs:         A pointer to the parent instance. 
 *     block:       A pointer to the instance to initialize.
 *     block_id:    The block identifier to assign.
 *     tag:         The tag to assign.
 *     node:        The node identifier to assign.
 *     size:        The size to assign.
 *     offset:      The offset to assign.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_init_block(oncefs_t *ofs, oncefs_block_t *block, uint32_t block_id,
                       oncefs_tag_t tag, uint32_t node, uint16_t size, uint64_t offset) {

    block->block = block_id;

    block->tag = tag;

    block->data.node = node;
    block->data.fill = size;
    block->data.offset = offset;


    if(node >= ofs->next_node_id) {
        ofs->next_node_id = node + 1;
    }

    return 0;
}

/**
 * Helper to allocate and initialize a block.
 *
 * Arguments:
 *     ofs:         A pointer to the parent instance. 
 *     block:       A pointer to the instance to initialize.
 *     operation:   The block operation to associate with this block.
 *     node:        The node identifier to assign.
 *     size:        The size to assign.
 *     offset:      The offset to assign.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_create_block(oncefs_t *ofs, oncefs_block_t *block, char operation,
                         uint32_t node, uint16_t size, uint64_t offset) {
    int r;

    uint32_t block_id;
    if (ofs->next_block_id <= ofs->last_block_id) {
        block_id = ofs->next_block_id++;
    } else {
        r = _oncefs_block_reuse(ofs, &block_id, node);
        if (r != 0) { return r; }
    }

    oncefs_tag_t tag = {.seq = ofs->next_seq_id++, .operation = operation};

    r = _oncefs_init_block(ofs, block, block_id, tag, node, size, offset);
    if (r != 0) { return r; }

    r = table_insert_or_replace(&ofs->blocks, block);
    if (r != 0) { return r; }

    return 0;
}

/**
 * Helper to allocate and initialize a block for a node.
 */
#define _oncefs_create_blockn(o, b, p, n) _oncefs_create_block(o, b, p, n, 0, 0)

int _oncefs_load_block_data(oncefs_t *ofs, oncefs_tagged_block_t *tagged_block,
                            oncefs_data_t *data) {
    int r;

    oncefs_block_t block;
    r = _oncefs_init_block(ofs, &block, tagged_block->block, tagged_block->tag,
                           data->node, data->fill, data->offset);
    if (r != 0) { return r; }
    r = table_insert_or_replace(&ofs->blocks, &block);
    if (r != 0) { return r; }

    return 0;
}

/**
 * Helper to load a block containing a node.
 *
 * Arguments:
 *     ofs:             A pointer to the parent instance. 
 *     tagged_block:    A pointer to a block with tag.
 *     node:            The destination for the resulting node.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_load_block_node(oncefs_t *ofs, oncefs_tagged_block_t *tagged_block,
                            oncefs_node_t *node) {
    int r;

    oncefs_block_t block;
    r = _oncefs_init_block(ofs, &block, tagged_block->block, tagged_block->tag,
                           node->node, 0, 0);
    if (r != 0) { return r; }
    r = table_insert_or_replace(&ofs->blocks, &block);
    if (r != 0) { return r; }

    return 0;
}

/**
 * Helper to reserve a node identifier.
 *
 * Arguments:
 *     ofs:             A pointer to the parent instance. 
 *     node:            The destination for the resulting node identifier.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_reserve_node_id(oncefs_t *ofs, uint32_t *node) {
    *node = ofs->next_node_id++;
    return 0;
}

/**
 * Helper to find the node associated with a path.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     path:    A string path.
 *     result:  The destination for the associated node.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_resolve_node(oncefs_t *ofs, const char *path, oncefs_node_t *result) {
    int r;

    if (strcmp(path, "/") == 0) {
        // Root always exists
        result->node = 0;
        result->last_access = ofs->time;
        result->last_modification = ofs->time;
        result->type = NODE_TYPE_DIR;
        return 0;
    }

    char *str = strdup(path);
    char *saveptr;
    const char *delim = "/";

    oncefs_node_t key = {.type = NODE_TYPE_DIR, .parent = 0};

    oncefs_node_t tmp;

    char *name = strtok_r(str, delim, &saveptr);
    while (name != NULL) {
        if (key.type != NODE_TYPE_DIR) {
            r = -EINVAL;
            break;
        }

        if (strlen(name) > ONCEFS_NAME_MAX_SIZE) {
            r = -EINVAL;
            break;
        }

        strcpy(key.name, name);

        r = table_query_first(&ofs->nodes, (void *) &key, TABLE_INDEX_LOOKUP, NULL,
                               (void *) &tmp);
        if (r != 0) { break; }

        key.parent = tmp.node;
        key.type = tmp.type;
        name = strtok_r(NULL, delim, &saveptr);
    }

    free(str);

    if (result != NULL) { memcpy(result, &tmp, sizeof(*result)); }

    return r;
}

/**
 * Initializer for a node.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     path:    A string path.
 *     type:    The type of the node.
 *     result:  A pointer to the node to initialize.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_init_node(oncefs_t *ofs, const char *path, char type,
                      oncefs_node_t *result) {
    int r;

    // Check if node with this name already exists
    r = _oncefs_resolve_node(ofs, path, NULL);
    if (r == 0) {
        return -EEXIST; // a node with this name already exists
    } else if (r != -ENOENT) {
        return r; // other error
    }             // else a node with this name does not exist yet


    // Check if parent is directory
    char *tmp = strdup(path);
    char *dir = dirname(tmp);
    oncefs_node_t parent;
    r = _oncefs_resolve_node(ofs, dir, &parent);
    free(tmp);

    if (r != 0) { return r; }
    if (parent.type != NODE_TYPE_DIR) { return -EINVAL; }

    // Populate

    result->parent = parent.node;
    result->type = type;
    result->last_access = (uint64_t) time(NULL);
    result->last_modification = (uint64_t) time(NULL);
    result->mode = 0;

    // Copy name
    tmp = strdup(path);
    char *name = basename(tmp);
    strncpy(result->name, name, ONCEFS_NAME_MAX_SIZE);
    free(tmp);

    if (strlen(result->name) > ONCEFS_NAME_MAX_SIZE) { return -EINVAL; }

    // Assign node id
    r = _oncefs_reserve_node_id(ofs, &result->node);
    if (r != 0) { return r; }

    return 0;
}

/**
 * Filesystem operation to create a file.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     path:    A string path.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int oncefs_set_file(oncefs_t *ofs, const char *path) {
    int r;

    oncefs_node_t entry;
    r = _oncefs_init_node(ofs, path, NODE_TYPE_FILE, &entry);
    if (r != 0) { return r; }

    r = table_insert_or_replace(&ofs->nodes, &entry);
    if (r != 0) { return r; }

    oncefs_block_t block;
    r = _oncefs_create_blockn(ofs, &block, BLOCK_OPERATION_NODE, entry.node);
    if (r != 0) { return r; }

    if (ofs->io != NULL) {
        r = io_write2(ofs->io, block.block, &block.tag, sizeof(block.tag), &entry,
                      sizeof(entry));
        if (r != 0) { return r; }
    }

    return 0;
}

/**
 * Filesystem operation to create a directory.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     path:    A string path.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int oncefs_set_dir(oncefs_t *ofs, const char *path) {
    int r;

    oncefs_node_t entry;
    r = _oncefs_init_node(ofs, path, NODE_TYPE_DIR, &entry);
    if (r != 0) { return r; }

    r = table_insert_or_replace(&ofs->nodes, &entry);
    if (r != 0) { return r; }

    oncefs_block_t block;
    r = _oncefs_create_blockn(ofs, &block, BLOCK_OPERATION_NODE, entry.node);
    if (r != 0) { return r; }

    if (ofs->io != NULL) {
        r = io_write2(ofs->io, block.block, &block.tag, sizeof(block.tag), &entry,
                      sizeof(entry));
        if (r != 0) { return r; }
    }

    return 0;
}

/**
 * Filesystem operation to create a link.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     from:    The path where the link will exist.
 *     to:      The path that the link will point to.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int oncefs_set_link(oncefs_t *ofs, const char *from, const char *to) {
    int r;

    if (strlen(to) > ONCEFS_NAME_MAX_SIZE) { return -EINVAL; }

    // Entry in filesystem
    oncefs_node_t entry;
    r = _oncefs_init_node(ofs, from, NODE_TYPE_LINK, &entry);
    if (r != 0) { return r; }

    r = table_insert_or_replace(&ofs->nodes, &entry);
    if (r != 0) { return r; }

    // Link content
    oncefs_node_t entry_payload = {
        .node = entry.node,
        .parent = entry.node,
        .last_access = 0, // Not used
        .last_modification = 0, // Not used
        .type = NODE_TYPE_LINK_PAYLOAD,
    };
    strncpy(entry_payload.name, to, ONCEFS_NAME_MAX_SIZE);

    r = table_insert_or_replace(&ofs->nodes, &entry_payload);
    if (r != 0) { return r; }

    // Persist
    oncefs_block_t block;
    r = _oncefs_create_blockn(ofs, &block, BLOCK_OPERATION_NODE, entry.node);
    if (r != 0) { return r; }

    if (ofs->io != NULL) {
        r = io_write2(ofs->io, block.block, &block.tag, sizeof(block.tag), &entry,
                      sizeof(entry));
        if (r != 0) { return r; }
    }

    r = _oncefs_create_blockn(ofs, &block, BLOCK_OPERATION_NODE, entry_payload.node);
    if (r != 0) { return r; }

    if (ofs->io != NULL) {
        r = io_write2(ofs->io, block.block, &block.tag, sizeof(block.tag),
                      &entry_payload, sizeof(entry_payload));
        if (r != 0) { return r; }
    }

    return 0;
}

int oncefs_set_time(oncefs_t *ofs, const char *path, time_t last_access, time_t last_modification) {
    int r;

    oncefs_node_t node;
    r = _oncefs_resolve_node(ofs, path, &node);
    if (r != 0) { return r; }

    if(last_modification == node.last_modification) {
        // Updating access is expensive, so skip this
        return 0;
    }

    node.last_access = last_access;
    node.last_modification = last_modification;

    r = table_insert_or_replace(&ofs->nodes, &node);
    if (r != 0) { return r; }

    oncefs_block_t block;
    r = _oncefs_create_blockn(ofs, &block, BLOCK_OPERATION_NODE, node.node);
    if (r != 0) { return r; }

    if (ofs->io != NULL) {
        r = io_write2(ofs->io, block.block, &block.tag, sizeof(block.tag), &node,
                      sizeof(node));
        if (r != 0) { return r; }
    }

    return 0;
}

/**
 * Filesystem operation to get the status of the system.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     result:  A pointer to the resulting status. 
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int oncefs_get_status(oncefs_t *ofs, oncefs_status_t *result) {
    int r;

    size_t last_valid_block = ofs->last_block_id;
    size_t next_valid_block = ofs->next_block_id;
    size_t first_valid_block = ofs->first_block_id;

    result->total_blocks = last_valid_block - first_valid_block + 1;
    result->block_size = ofs->block_size;
    result->name_max_size = ONCEFS_NAME_MAX_SIZE;

    // Unused blocks
    size_t unused_blocks = last_valid_block - next_valid_block + 1;

    // Count free blocks
    size_t free_blocks;
    size_t delete_blocks;

    int _filter(const void *raw_key, const void *raw_other) {
        oncefs_block_t *k = (oncefs_block_t *) raw_key;
        oncefs_block_t *o = (oncefs_block_t *) raw_other;

        if (k->tag.operation < o->tag.operation) {
            return -1;
        } else if (k->tag.operation > o->tag.operation) {
            return 1;
        }

        return 0;
    };

    oncefs_block_t key;

    key.tag.operation = BLOCK_OPERATION_FREE;
    r = table_query_count(&ofs->blocks, (void *) &key, TABLE_INDEX_LOOKUP, _filter,
                          &free_blocks);
    if (r != 0 && r != -ENOENT) { return r; }

    key.tag.operation = BLOCK_OPERATION_DELETE;
    r = table_query_count(&ofs->blocks, (void *) &key, TABLE_INDEX_LOOKUP, _filter,
                          &delete_blocks);
    if (r != 0 && r != -ENOENT) { return r; }

    result->free_blocks = unused_blocks + free_blocks + delete_blocks;

    return 0;
}

/**
 * Filesystem operation to fetch the node at a given path.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     path:    A string path.
 *     result:  The destination for the associated node.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int oncefs_get_node(oncefs_t *ofs, const char *path, oncefs_stat_t *result) {
    int r;

    oncefs_node_t node;
    r = _oncefs_resolve_node(ofs, path, &node);
    if (r != 0) { return r; }

    result->node = node.node;
    result->mode = node.mode;
    result->is_dir = (node.type == NODE_TYPE_DIR) ? 1 : 0;
    result->is_file = (node.type == NODE_TYPE_FILE) ? 1 : 0;
    result->is_link = (node.type == NODE_TYPE_LINK) ? 1 : 0;

    oncefs_block_t key;

    key.tag.operation = BLOCK_OPERATION_DATA;
    key.data.node = node.node;

    oncefs_block_t tmp;

    // Resolve size
    result->size = 0;
    if (result->is_file == 1) {
        r = table_query_last(&ofs->blocks, (void *) &key, TABLE_INDEX_LOOKUP,
                               _oncefs_block_cmp_lookup_fuzzy, (void *) &tmp);
        if (r == 0) { result->size = tmp.data.offset + tmp.data.fill; }
    } else if (result->is_link == 1) {
        // TODO fetch size of link
    }

    result->last_access = node.last_access;
    result->last_modification = node.last_modification;

    return 0;
}

/**
 * Filesystem operation to read a directory.
 *
 * Arguments:
 *     ofs:         A pointer to the parent instance. 
 *     path:        A string path of the directory.
 *     callback:    A function to be called once per node in the directory.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int oncefs_get_dir(oncefs_t *ofs, const char *path,
                   int (*callback)(oncefs_node_t *entry)) {
    int r;

    oncefs_node_t key;
    r = _oncefs_resolve_node(ofs, path, &key);
    if (r != 0) { return r; }

    int _filter(const void *raw_a, const void *raw_b) {
        oncefs_node_t *a = (oncefs_node_t *) raw_a;
        oncefs_node_t *b = (oncefs_node_t *) raw_b;

        if (a->node < b->parent) {
            return -1;
        } else if (a->node > b->parent) {
            return 1;
        }

        return 0;
    };

    int _callback(void *raw) {
        return callback((oncefs_node_t *) raw);
    }

    r = table_query_all(&ofs->nodes, &key, TABLE_INDEX_LOOKUP, _filter, _callback);
    if(r != 0 && r != -ENOENT) { return r; }

    return 0;
}

/**
 * Filesystem operation to read a link.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     path:    A string path.
 *     result:  The destination dummy node; only the name will be filled.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int oncefs_get_link(oncefs_t *ofs, const char *path, oncefs_node_t *result) {
    int r;
    r = _oncefs_resolve_node(ofs, path, result);
    if (r != 0) { return r; }

    if (result->type != NODE_TYPE_LINK) { return -EINVAL; }

    oncefs_node_t key = {
        .node = result->node, .parent = result->node, .type = NODE_TYPE_LINK_PAYLOAD};

    oncefs_node_t tmp;

    r = table_query_first(&ofs->nodes, (void *) &key, TABLE_INDEX_PRIMARY, NULL, 
                           (void *) &tmp);
    if (r != 0) { return r; }

    memcpy(result, &tmp, sizeof(*result));

    return r;
}

/**
 * Filesystem operation to write data associated with a node.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     node:    The node identifier.
 *     data:    The data to write.
 *     size:    The size of data in bytes.
 *     offset:  The byte location of the data within the node.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
size_t oncefs_set_data(oncefs_t *ofs, uint32_t node, const char *data, size_t size,
                    uint64_t offset) {
    int r;

    size_t written = 0;
    size_t amount;
    while (written < size) {
        // write in blocks
        amount = size - written;
        if (amount > ofs->payload_size) { amount = ofs->payload_size; }

        oncefs_block_t block;
        r = _oncefs_create_block(ofs, &block, BLOCK_OPERATION_DATA, node, amount,
                                 offset + written);
        if (r != 0) { return r; }

        oncefs_data_t data_entry = {
            .node = node, .fill = amount, .offset = offset + written};

        if (ofs->io != NULL) {
            r = io_write3(ofs->io, block.block, &block.tag, sizeof(block.tag),
                          &data_entry, sizeof(data_entry), data + written, amount);
            if (r != 0) { return r; }
        }

        written += amount;
    }

    return 0;
}

/**
 * Helper to read data asssociated with a node.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     node:    The node identifier.
 *     data:    A buffer to hold the resulting data.
 *     size:    The size of data buffer in bytes.
 *     offset:  The byte location of the data within the node.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_get_data(oncefs_t *ofs, uint32_t node, char *data, uint16_t size,
                    uint64_t offset) {
    int r;

    int _filter(const void *raw_key, const void *raw_other) {
        int r;

        r = _oncefs_block_cmp_lookup_fuzzy(raw_key, raw_other);
        if (r != 0) { return r; }

        oncefs_block_t *k = (oncefs_block_t *) raw_key;
        oncefs_block_t *o = (oncefs_block_t *) raw_other;

        // stretch to include any blocks that could potentially contain
        // overlapping data
        int window = k->data.fill + ofs->payload_size;

        if (k->data.offset + window < o->data.offset) { return -1; }
        if (k->data.offset > o->data.offset + window) { return 1; }

        return 0;
    };

    int _order_by(const void *raw_a, const void *raw_b) {
        oncefs_block_t *a = (oncefs_block_t *) raw_a;
        oncefs_block_t *b = (oncefs_block_t *) raw_b;

        if (a->tag.seq < b->tag.seq) {
            return -1;
        } else if (a->tag.seq > b->tag.seq) {
            return 1;
        }

        return 0;
    };

    oncefs_block_t key = {.tag = {.operation = BLOCK_OPERATION_DATA},
                          .data = {.node = node, .fill = size, .offset = offset}};

    // Relative to file
    off_t tgt_start = offset;
    off_t tgt_end = offset + size;

    int fill = 0;

    int _callback(void *raw) {
        int r;
        oncefs_block_t *result = (oncefs_block_t *) raw;

        // Relative to file
        off_t src_start = result->data.offset;
        off_t src_end = src_start + result->data.fill;

        if (src_start > tgt_end || src_end < tgt_start) {
            return 0; // no bytes overlap
        }

        int skip = 0;
        int seek = 0;
        if (src_start < tgt_start) {
            skip = tgt_start - src_start;
            src_start = tgt_start;
        } else if (src_start > tgt_start) {
            seek = src_start - tgt_start;
        }

        if (src_end > tgt_end) { src_end = tgt_end; }

        int amount = src_end - src_start;

        // printf("skip seek amount %i %i %i\n", skip, seek, amount);

        if (amount <= 0) { return 0; }

        r = io_read3(ofs->io, result->block, NULL, sizeof(oncefs_tag_t), NULL,
                     sizeof(oncefs_data_t) + skip, data + seek, amount);
        if (r != 0) { return r; }

        if (skip + amount > fill) {
            fill = skip + amount;
        }

        return 0;
    };

    r = table_query_order_by(&ofs->blocks, &key, TABLE_INDEX_LOOKUP, _filter, _order_by,
                             _callback);
    if (r != 0) { return r; }

    return fill;
}

/**
 * Filesystem operation to read data asssociated with a node.
 *
 * Breaks up read operations into block sized chunks.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     node:    The node identifier.
 *     data:    A buffer to hold the resulting data.
 *     size:    The size of data buffer in bytes.
 *     offset:  The byte location of the data within the node.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
size_t oncefs_get_data(oncefs_t *ofs, uint32_t node, char *data, size_t size,
                    uint64_t offset) {
    size_t total = 0; // total amount read
    int limit; // max amount to read
    int read; // amount read
    while (total < size) {
        // total in blocks
        limit = (size - total > ofs->payload_size) ? ofs->payload_size : size - total;

        read = _oncefs_get_data(ofs, node, data + total, limit, offset + total);
        if(read == -ENOENT || read  == 0) { break; }
        if(read < 0) { return read ; }

        total += read ;
    }

    return total;
}

/**
 * Helper to rename (move) the name of an existing node.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     node:    A pointer to a dummy instance with existing node id and
 *              new parent id and/or name.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_move_node(oncefs_t *ofs, oncefs_node_t *node) {
    int r;

    // Check to see if adding this node will conflict with an existing file
    oncefs_node_t existing;
    r = table_query_first(&ofs->nodes, (void *) node, TABLE_INDEX_LOOKUP, NULL,
                           &existing);
    if (r == 0) {
        // A node exists
        if (existing.type == NODE_TYPE_FILE) {
            return -EEXIST; // conflict
        } else {
            return -EINVAL; // moving into dir or symlink not supported
        }
    }

    // All good to update
    r = table_insert_or_replace(&ofs->nodes, node);
    if (r != 0) { return r; }

    return 0;
}

/**
 * Filesystem operation to rename or move a node.
 *
 * Breaks up read operations into block sized chunks.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     from:    The node's current path. 
 *     to:      The node's desired new path. 
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int oncefs_move_node(oncefs_t *ofs, const char *from, const char *to) {
    int r;

    // Find the node
    oncefs_node_t result;
    r = _oncefs_resolve_node(ofs, from, &result);
    if (r != 0) { return r; }

    // Check if target parent is directory
    char *tmp = strdup(to);
    char *dir = dirname(tmp);

    oncefs_node_t parent;
    r = _oncefs_resolve_node(ofs, dir, &parent);

    free(tmp);

    if (r != 0) { return r; }
    if (parent.type != NODE_TYPE_DIR) { return -EINVAL; }

    // Set new parent
    result.parent = parent.node;

    // Set new name
    tmp = strdup(to);
    char *name = basename(tmp);
    strncpy(result.name, name, ONCEFS_NAME_MAX_SIZE);
    free(tmp);
    if (strlen(result.name) > ONCEFS_NAME_MAX_SIZE) { return -EINVAL; }

    // Perform move
    r = _oncefs_move_node(ofs, &result);
    if (r == -EEXIST) {
        // Conflict
        r = oncefs_del_node(ofs, to);
        if (r != 0) { return r; }

        // try again
        r = _oncefs_move_node(ofs, &result);
    }

    if (r != 0) { return r; }

    // Register the move block
    oncefs_block_t block;
    r = _oncefs_create_block(ofs, &block, BLOCK_OPERATION_MOVE, result.node, 0, 0);
    if (r != 0) { return r; }

    if (ofs->io != NULL) {
        r = io_write2(ofs->io, block.block, &block.tag, sizeof(block.tag), &result,
                      sizeof(result));
        if (r != 0) { return r; }
    }

    return 0;
}

/**
 * Helper to delete a node and associated data.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     node:    A pointer to a description of the node to delete.
 *     check_for_children:     
 *              If 1, deleting a folder that contains children will be blocked.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_del_node(oncefs_t *ofs, oncefs_node_t *node, int check_for_children) {
    int r;

    if (check_for_children == 1 && node->type == NODE_TYPE_DIR) {
        // Check for children
        int _filter0(const void *raw_key, const void *raw_other) {
            oncefs_node_t *k = (oncefs_node_t *) raw_key;
            oncefs_node_t *o = (oncefs_node_t *) raw_other;

            if (k->node < o->parent) {
                return -1;
            } else if (k->node > o->parent) {
                return 1;
            }

            return 0;
        };

        r = table_query_first(&ofs->nodes, (void *) node, TABLE_INDEX_LOOKUP, _filter0,
                               NULL);
        if (r == 0) { 
            // This can happen naturally if history is rewritten such that a node that
            // will be deleted in the future is no longer moved out of a folder that
            // will be deleted in the near future. This is a non-issue since the
            // unreachable node will be removed in the future.

            // printf("HERE\n");
            // oncefs_dump(ofs);
            return -EINVAL;
        }
    }

    // Delete node entries

    // Set the node to be free
    int _filter(const void *raw_key, const void *raw_other) {
        oncefs_node_t *k = (oncefs_node_t *) raw_key;
        oncefs_node_t *o = (oncefs_node_t *) raw_other;

        if (k->node < o->node) {
            return -1;
        } else if (k->node > o->node) {
            return 1;
        }

        return 0;
    };

    r = table_query_delete(&ofs->nodes, node, TABLE_INDEX_PRIMARY, _filter);
    if (r != 0 && r != -ENOENT) { return r; } // ignore non existing

    // Set all blocks to be free
    int _mutator2(void *raw) {
        oncefs_block_t *block = (oncefs_block_t *) raw;
        block->tag.operation = BLOCK_OPERATION_FREE;
        return 0;
    }

    oncefs_block_t key;
    key.data.node = node->node;

    for(int i=1;i<BLOCK_OPERATION_LAST;i++) {
        key.tag.operation = i;
        r = table_query_update(&ofs->blocks, &key, TABLE_INDEX_LOOKUP, _oncefs_block_cmp_lookup_fuzzy, _mutator2);
        if (r != 0 && r != -ENOENT) { return r; } // ignore non existing
    }

    return 0;
}

/**
 * Filesystem operation to delete a node and associated data.
 *
 * Arguments:
 *     ofs:     A pointer to the parent instance. 
 *     path:    A string path.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int oncefs_del_node(oncefs_t *ofs, const char *path) {
    int r;

    oncefs_node_t result;
    r = _oncefs_resolve_node(ofs, path, &result);
    if (r != 0) { return r; }

    r = _oncefs_del_node(ofs, &result, 1);
    if (r != 0) { return r; }

    // Register the delete block
    oncefs_block_t block;
    r = _oncefs_create_block(ofs, &block, BLOCK_OPERATION_DELETE, result.node, 0, 0);
    if (r != 0) { return r; }

    if (ofs->io != NULL) {
        r = io_write2(ofs->io, block.block, &block.tag, sizeof(block.tag), &result,
                      sizeof(result));
        if (r != 0) { return r; }
    }

    return 0;
}

/**
 * Helper to delete a range of data.
 *
 * Arguments:
 *     ofs:         A pointer to the parent instance. 
 *     node:        The node identifier to delete from.
 *     new_size:    The desired size in bytes of the node after the delete has been
 *                  performed.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_del_data(oncefs_t *ofs, uint32_t node, uint64_t new_size) {
    int r;

    int _filter(const void *raw_key, const void *raw_other) {
        int r;

        r = _oncefs_block_cmp_lookup_fuzzy(raw_key, raw_other);
        if (r != 0) { return r; }

        oncefs_block_t *k = (oncefs_block_t *) raw_key;
        oncefs_block_t *o = (oncefs_block_t *) raw_other;

        // All potentially matching blocks
        // impossible to be to the left
        if (k->data.offset > o->data.offset + ofs->payload_size) { return 1; }

        return 0;
    };

    int _mutator(void *raw) {
        oncefs_block_t *block = (oncefs_block_t *) raw;

        off_t start = block->data.offset;
        if (start >= new_size) {
            // No bytes to keep
            block->tag.operation = BLOCK_OPERATION_FREE;
            return 0;
        }

        off_t end = start + block->data.fill;
        if (end > new_size) { end = new_size; }

        block->data.fill = end - start;

        return 0;
    }

    oncefs_block_t key;
    key.tag.operation = BLOCK_OPERATION_DATA;
    key.data.node = node;
    key.data.offset = new_size;

    r = table_query_update(&ofs->blocks, &key, TABLE_INDEX_LOOKUP, _filter, _mutator);
    if (r != 0 && r != -ENOENT) { return r; }

    // Clear existing "truncate" blocks
    int _mutator2(void *raw) {
        oncefs_block_t *block = (oncefs_block_t *) raw;
        block->tag.operation = BLOCK_OPERATION_FREE;
        return 0;
    }

    key.tag.operation = BLOCK_OPERATION_TRUNCATE;

    r = table_query_update(&ofs->blocks, &key, TABLE_INDEX_LOOKUP,
                           _oncefs_block_cmp_lookup_fuzzy, _mutator2);
    if (r != 0 && r != -ENOENT) { return r; }

    return 0;
}

/**
 * Filesystem operation to delete or truncate a range of data.
 *
 * Arguments:
 *     ofs:         A pointer to the parent instance. 
 *     node:        The node identifier to delete from.
 *     new_size:    The desired size in bytes of the node after the delete has been
 *                  performed.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int oncefs_del_data(oncefs_t *ofs, uint32_t node, uint64_t new_size) {
    int r;

    // Check that the node exists in the first place?
    oncefs_node_t key = {.node = node, .type = NODE_TYPE_FILE};
    oncefs_node_t result;
    r = table_query_first(&ofs->nodes, (void *) &key, TABLE_INDEX_PRIMARY, NULL,
                           (void *) &result);
    if (r != 0) { return r; }

    r = _oncefs_del_data(ofs, result.node, new_size);
    if (r != 0) { return r; }

    // Register the truncate block
    oncefs_block_t block;
    r = _oncefs_create_block(ofs, &block, BLOCK_OPERATION_TRUNCATE, result.node, 0,
                             new_size);
    if (r != 0) { return r; }

    if (ofs->io != NULL) {
        r = io_write2(ofs->io, block.block, &block.tag, sizeof(block.tag), &block.data,
                      sizeof(block.data));
        if (r != 0) { return r; }
    }

    return 0;
}

/**
 * Helper to format all data in a container.
 *
 * The tag portion of all blocks will be overwritten to invalid values.
 *
 * Arguments:
 *     ofs:         A pointer to the parent instance. 
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_format(oncefs_t *ofs) {
    int r;

    size_t start = io_block_first(ofs->io);
    size_t end = io_block_last(ofs->io);

    oncefs_tag_t tag;
    size_t tag_size = sizeof(tag);

    for (size_t i = start; i <= end; i++) {
        // Overwrite all tag operations to invalid values
        tag.seq = random();
        do {
            tag.operation = random();
        } while (tag.operation <= BLOCK_OPERATION_LAST);

        r = io_write(ofs->io, i, &tag, tag_size);
        if (r != 0) { return r; }
    }

    return 0;
}

/**
 * Helper to load data from a container.
 *
 * Blocks are processed in order of their sequence number.
 *
 * Arguments:
 *     ofs:         A pointer to the parent instance. 
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
int _oncefs_load(oncefs_t *ofs) {
    int r;

    size_t start = io_block_first(ofs->io);
    size_t end = io_block_last(ofs->io);
    size_t num_blocks = end - start + 1; // +1 since end is a valid block

    size_t count = 0;

    // Read all tags
    oncefs_tagged_block_t tags[num_blocks];
    oncefs_tagged_block_t *cursor;

    size_t item_size = sizeof(oncefs_tagged_block_t);
    for (size_t i = start; i <= end; i++) {
        cursor = &tags[count];
        cursor->block = i;

        r = io_read(ofs->io, i, &cursor->tag, item_size);
        if (r != 0) { return r; }

        if (cursor->tag.operation >= BLOCK_OPERATION_LAST) { break; }

        count += 1;
    }

    // Sort tags by sequence id
    int cmp(const void *raw_a, const void *raw_b) {
        oncefs_tagged_block_t *a = (oncefs_tagged_block_t *) raw_a;
        oncefs_tagged_block_t *b = (oncefs_tagged_block_t *) raw_b;
        if (a->tag.seq < b->tag.seq) {
            return -1;
        } else if (a->tag.seq > b->tag.seq) {
            return 1;
        }
        return 0;
    }

    qsort(&tags, count, item_size, &cmp);

    // Process tags
    oncefs_node_t node_entry;
    oncefs_data_t data_entry;

    // Load blocks one by one

    int operation;
    for (size_t i = 0; i < count; i++) {
        cursor = &tags[i];

        // printf("Loading seq block op: %lu %i %i\n", cursor->tag.seq, cursor->block, cursor->tag.operation);

        operation = cursor->tag.operation;
        if (operation == BLOCK_OPERATION_DATA) {
            r = io_read2(ofs->io, cursor->block, &cursor->tag, sizeof(cursor->tag),
                         &data_entry, sizeof(data_entry));

            // printf(" %i: truncate size %i offset %lu\n", data_entry.node, data_entry.fill, data_entry.offset);

            r = _oncefs_load_block_data(ofs, cursor, &data_entry);
            if (r != 0) { return r; }
        } else if (operation == BLOCK_OPERATION_NODE) {
            r = io_read2(ofs->io, cursor->block, &cursor->tag, sizeof(cursor->tag),
                         &node_entry, sizeof(node_entry));
            if (r != 0) { return r; }

            // printf(" %i: node %s parent %i\n", node_entry.node, node_entry.name, node_entry.parent);

            r = table_insert_or_replace(&ofs->nodes, &node_entry);
            if (r != 0) { return r; }

            r = _oncefs_load_block_node(ofs, cursor, &node_entry);
            if (r != 0) { return r; }

        } else if (operation == BLOCK_OPERATION_MOVE) {
            r = io_read2(ofs->io, cursor->block, &cursor->tag, sizeof(cursor->tag),
                         &node_entry, sizeof(node_entry));
            if (r != 0) { return r; }

            // printf(" %i: move %s parent %i\n", node_entry.node, node_entry.name, node_entry.parent);

            r = _oncefs_move_node(ofs, &node_entry);
            if (r != 0) { return r; }

            r = _oncefs_load_block_node(ofs, cursor, &node_entry);
            if (r != 0) { return r; }
        } else if (operation == BLOCK_OPERATION_DELETE) {
            r = io_read2(ofs->io, cursor->block, &cursor->tag, sizeof(cursor->tag),
                         &node_entry, sizeof(node_entry));
            if (r != 0) { return r; }

            // printf(" %i: delete %s parent %i\n", node_entry.node, node_entry.name, node_entry.parent);

            r = _oncefs_del_node(ofs, &node_entry, 0); // do not check for children
            if (r != 0 && r != -ENOENT) { return r; }

            r = _oncefs_load_block_node(ofs, cursor, &node_entry);
            if (r != 0) { return r; }
        } else if (operation == BLOCK_OPERATION_TRUNCATE) {
            r = io_read2(ofs->io, cursor->block, &cursor->tag, sizeof(cursor->tag),
                         &data_entry, sizeof(data_entry));

            // printf(" %i: truncate offset %lu\n", data_entry.node, data_entry.offset);

            r = _oncefs_del_data(ofs, data_entry.node, data_entry.offset);
            if (r != 0) { return r; }

            r = _oncefs_load_block_data(ofs, cursor, &data_entry);
            if (r != 0) { return r; }
        } else {
            return -ENOSYS; // TODO not implemented
        }

        if(cursor->block >= ofs->next_block_id) {
            ofs->next_block_id = cursor->block + 1;
        }
    }

    if(count > 0) {
        ofs->next_seq_id = tags[count - 1].tag.seq + 1;
    }

    return 0;
}

int oncefs_sync(oncefs_t *ofs) {
    return io_sync(ofs->io);
}

/**
 * Debugging helper to dump the contents of the filesystem to a table.
 *
 * Arguments:
 *     ofs:     A pointer to the instance. 
 *     buffer:  The buffer to dump string data to.
 *
 * Returns:
 *     0 on success, otherwise an errno code.
 */
void oncefs_dumps(oncefs_t *ofs, char *buffer) {
    int printer(const char *format, ...) {
        int r;
        va_list args;
        va_start(args, format);
        if (buffer != NULL) {
            r = vsprintf(buffer, format, args);
            buffer += r;
        } else {
            r = vprintf(format, args);
        }
        va_end(args);
        return r;
    }

    printer("\n info \n");
    printer("+--------------+-------+\n");
    printer("| name         | value |\n");
    printer("+--------------+-------+\n");
    printer("| next node    | %5lu |\n", ofs->next_node_id);
    printer("| next seq     | %5lu |\n", ofs->next_seq_id);
    printer("+--------------+-------+\n");
    printer("| block size   | %5i |\n", ofs->block_size);
    printer("| payload size | %5i |\n", ofs->payload_size);
    printer("+-------------+-------+\n");
    printer("| first block  | %5lu |\n", ofs->first_block_id);
    printer("| next block   | %5lu |\n", ofs->next_block_id);
    printer("| last block   | %5lu |\n", ofs->last_block_id);
    printer("| total_blocks | %5lu |\n", ofs->last_block_id - ofs->first_block_id + 1);
    printer("+--------------+-------+\n");

    oncefs_status_t status;
    int r = oncefs_get_status(ofs, &status);
    if(r == 0) {
        printer("| free blocks  | %5lu |\n", status.free_blocks);
        printer("+--------------+-------+\n");
    }

    int index = TABLE_INDEX_LOOKUP;
    printer("\n  nodes (index %i)\n", index);
    printer("+------+--------+------+------+------------+------------+----------+\n");
    printer("| node | parent | type | mode | access     | modify     |     name |\n");
    printer("+------+--------+------+------+------------+------------+----------+\n");
    void printer1(const void *raw) {
        oncefs_node_t *entry = (oncefs_node_t *) raw;
        printer("| %4i | %6i | %4i | %4i | %10lu | %10lu | %8s |\n", entry->node, entry->parent,
                entry->type, entry->mode, entry->last_access, entry->last_modification, entry->name);
    }
    table_dump_by_index(&ofs->nodes, index, printer1);
    printer("+------+--------+------+------+------------+------------+----------+\n");
    printer("\n");

    index = TABLE_INDEX_LOOKUP;
    printer("\n  blocks (index %i)\n", index);
    printer("+-------+-----+------+------+------+--------+\n");
    printer("| block | seq | op   | node | fill | offset |\n");
    printer("+-------+-----+------+------+------+--------+\n");
    void printer2(const void *raw) {
        oncefs_block_t *b = (oncefs_block_t *) raw;
        printer("| %5i | %3lu | %4i | %4i | %4i | %6lu |\n", b->block, b->tag.seq,
                b->tag.operation, b->data.node, b->data.fill, b->data.offset);
    }
    table_dump_by_index(&ofs->blocks, index, printer2);
    printer("+-------+-----+------+------+------+--------+\n");
}
