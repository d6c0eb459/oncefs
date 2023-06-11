#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "table.h"

int table_add_index(table_t *table, comparison_fn_t comparator) {
    if(table->rows.fill > 0) {
        return -EINVAL;
    }

    array_t index;
    array_init(&index, sizeof(void *));

    array_set_reference(&index, &table->rows);
    array_sort(&index, comparator);

    array_append(&table->indexes, &index);
    return 0;
}

int table_init(table_t *table, int row_size, comparison_fn_t comparator) {
    array_init(&table->rows, row_size);
    array_init(&table->indexes, sizeof(array_t));

    table_add_index(table, comparator);
    return 0;
}

void table_free(table_t *table) {
    array_free(&table->rows);

    int _callback(void *raw) {
        array_t *index = (array_t *) raw;
        array_free(index);
        return 0;
    }

    array_each(&table->indexes, _callback);
}

int _table_insert(table_t *table, void *row, int replace) {
    int r;

    size_t row_id;

    // Check primary index for match
    array_t index;
    array_get(&table->indexes, 0, &index);

    r = array_sorted_first(&index, index.comparator, row, &row_id);
    if(r == -ENOENT) {
        // No match found; simple insert
        row_id = array_len(&table->rows);

        r = array_append(&table->rows, row);
        if(r != 0) { return r; }

        int _callback(void *raw) {
            array_t *index = (array_t *) raw;
            array_sorted_insert(index, &row_id);
            return 0;
        }

        // Insert into each index
        array_each(&table->indexes, _callback);
        return 0;
    }

    // Error
    if(r != 0) { return r; }

    // A match was found
    if(!replace) {
        return -EEXIST;
    } 

    // Overwrite existing
    void *dest = (void *) array_dereference(&index, (void *) &row_id);
    memcpy(dest, row, table->rows.entry_size);

    // Update indexes
    int _callback(void *raw) {
        array_t *index = (array_t *) raw;
        array_sort(index, NULL);
        return 0;
    }

    // Insert into each index
    array_each(&table->indexes, _callback);
    return 0;
}

int table_insert(table_t *table, void *row) {
    return _table_insert(table, row, 0);
}

int table_insert_or_replace(table_t *table, void *row) {
    return _table_insert(table, row, 1);
}

int table_query_first(table_t *table, void *key, int table_index_id,
                       comparison_fn_t filter, void *result) {
    int r;

    array_t index;
    array_get(&table->indexes, table_index_id, &index);

    size_t row_id;
    r = array_sorted_first(&index, filter, key, &row_id);
    if(r != 0) { return r; }

    if(result != NULL) {
        memcpy(result, array_dereference(&index, &row_id), table->rows.entry_size);
    }

    return 0;
}

int table_query_last(table_t *table, void *key, int table_index_id,
                       comparison_fn_t filter, void *result) {
    int r;

    array_t index;
    r = array_get(&table->indexes, table_index_id, &index);
    if(r != 0) { return r; }

    size_t row_id;
    r = array_sorted_last(&index, filter, key, &row_id);
    if(r != 0) { return r; }

    if(result != NULL) {
        memcpy(result, array_dereference(&index, &row_id), table->rows.entry_size);
    }

    return 0;
}

int table_query_all(table_t *table, void *key, int table_index_id,
                    comparison_fn_t comparator, int (*callback)(void *row)) {
    int r;

    array_t index;
    r = array_get(&table->indexes, table_index_id, &index);
    if(r != 0) { return r; }
    
    int _callback(void *raw) {
        size_t *row_id = (size_t *) raw;
        callback((void *) array_dereference(&index, row_id));
        return 0;
    }

    r = array_sorted_each(&index, comparator, key, _callback);
    if(r != 0) { return r; }

    return 0;
}

int table_query_count(table_t *table, void *key, int table_index_id,
                      comparison_fn_t comparator, size_t *count) {
    int r;

    array_t index;
    r = array_get(&table->indexes, table_index_id, &index);
    if(r != 0) { return r; }

    *count = 0;
    int _callback(void *_unused) {
        *count += 1;
        return 0;
    }

    r = array_sorted_each(&index, comparator, key, _callback);
    if(r != 0 && r != -ENOENT) { return r; }

    return 0;
}

int table_query_update(table_t *table, void *key, int table_index_id,
                       comparison_fn_t comparator, int (*mutator)(void *row)) {
    int r;

    array_t index;
    r = array_get(&table->indexes, table_index_id, &index);
    if(r != 0) { return r; }

    int _mutator(void *raw) {
        size_t *row_id = (size_t *) raw;
        mutator((void *) array_dereference(&index, row_id));
        return 0;
    }

    r = array_sorted_each(&index, comparator, key, _mutator);
    if(r != 0 && r != -ENOENT) { return r; }

    int _callback(void *raw) {
        array_t *index = (array_t *) raw;
        array_sort(index, NULL);
        return 0;
    }

    array_each(&table->indexes, _callback);

    return 0;

}

const int _cmp_size(const void *raw_a, const void *raw_b) {
    size_t *a = (size_t *) raw_a;
    size_t *b = (size_t *) raw_b;

    if(*a < *b) { return -1; }
    else if(*a > *b) { return 1; }

    return 0;
}

int table_query_delete(table_t *table, void *key, int table_index_id,
                       comparison_fn_t comparator) {
    int r;

    array_t index;
    r = array_get(&table->indexes, table_index_id, &index);
    if(r != 0) { return r; }

    // Collect matching row ids
    array_t row_ids;
    array_init(&row_ids, sizeof(size_t));
    array_sort(&row_ids, _cmp_size);

    int _callback(void *row_id) {
        array_sorted_insert(&row_ids, row_id);
        return 0;
    }

    r = array_sorted_each(&index, comparator, key, _callback);
    if(r != 0) { return r; }

    // void _printer(const void *raw) {
    //     printf("%lu\n", *(size_t *) raw);
    // }
    // array_dump(&row_ids, _printer);

    // Delete from all indexes
    int _callback2(void *raw) {
        array_t *index = (array_t *) raw;

        int _filter(const void *_unused, const void *row_id) {
            int r;
            r = array_sorted_first(&row_ids, _cmp_size, (void *) row_id, NULL);
            return r;
        }

        array_delete(index, _filter, NULL);
        return 0;
    }

    r = array_each(&table->indexes, _callback2);
    if(r != 0) { return r; }

    return 0;
}

int table_query_order_by(table_t *table, void *key, int table_index_id,
                         comparison_fn_t comparator, comparison_fn_t order_by,
                         int (*callback)(void *result)) {
    int r;

    array_t results;
    array_init(&results, table->rows.entry_size);
    array_sort(&results, order_by);

    int _callback(void *raw) {
        array_sorted_insert(&results, raw);
        return 0;
    }
    
    r = table_query_all(table, key, table_index_id, comparator, _callback);
    if(r != 0) { return r; }

    array_each(&results, callback);

    return 0;
}

int table_to_array(table_t *table, array_t *result) {
    int _callback(void *raw) {
        array_append(result, raw);
        return 0;
    }

    array_each(&table->rows, _callback);
    return 0;
}

int table_to_array_by_index(table_t *table, int table_index_id, array_t *result) {
    array_t index;
    array_get(&table->indexes, table_index_id, &index);

    int _callback(void *row) {
        row = (void *) array_dereference(&index, row);
        array_append(result, row);
        return 0;
    }

    array_each(&index, _callback);

    return 0;
}

void table_dump(table_t *table, void (*printer)(const void *entry)) {
    int _callback(void *row) {
        printer(row);
        return 0;
    }
    array_each(&table->rows, _callback);
}

void table_dump_by_index(table_t *table, int table_index_id,
                         void (*printer)(const void *entry)) {
    array_t index;
    array_get(&table->indexes, table_index_id, &index);

    int _callback(void *row) {
        row = (void *) array_dereference(&index, row);
        printer(row);
        return 0;
    }

    array_each(&index, _callback);
}
