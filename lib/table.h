#ifndef _TABLE_H
#define _TABLE_H

/**
 * A table data structure implemented using a sorted array augmented with multiple
 * indexes.
 */

#include "array.h"

typedef int (*comparison_fn_t)(const void *a, const void *b);

typedef struct table {
    array_t rows;
    array_t indexes;
} table_t;

int table_init(table_t *table, int row_size, comparison_fn_t comparator);
void table_free(table_t *table);

int table_add_index(table_t *table, comparison_fn_t comparator);

int table_insert(table_t *table, void *row);
int table_insert_or_replace(table_t *table, void *row);

int table_query_first(table_t *table, void *key, int table_index_id,
                       comparison_fn_t filter, void *result);
int table_query_last(table_t *table, void *key, int table_index_id,
                       comparison_fn_t filter, void *result);
int table_query_all(table_t *table, void *key, int table_index_id,
                    comparison_fn_t comparator, int (*callback)(void *row));
int table_query_count(table_t *table, void *key, int table_index_id,
                      comparison_fn_t comparator, size_t *count);
int table_query_update(table_t *table, void *key, int table_index_id,
                       comparison_fn_t comparator, int (*mutator)(void *row));
int table_query_delete(table_t *table, void *key, int table_index_id,
                       comparison_fn_t comparator);
int table_query_order_by(table_t *table, void *key, int table_index_id,
                         comparison_fn_t comparator, comparison_fn_t order_by,
                         int (*callback)(void *result));

// Debugging
int table_to_array(table_t *table, array_t *result);
int table_to_array_by_index(table_t *table, int table_index_id, array_t *result);

void table_dump(table_t *table, void (*printer)(const void *entry));
void table_dump_by_index(table_t *table, int table_index_id,
                         void (*printer)(const void *entry));

#endif
