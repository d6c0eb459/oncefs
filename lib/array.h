#ifndef _ARRAY_H  
#define _ARRAY_H  

/**
 * An array with sort capabilities.
 */

typedef int (*comparison_fn_t)(const void *a, const void *b);

typedef struct array {
    char *entries;
    int entry_size;

    size_t fill;
    size_t capacity;

    comparison_fn_t comparator;

    struct array *reference;
} array_t;

void array_init(array_t *array, int entry_size);
int array_set_reference(array_t *array, array_t *reference);
const void *array_dereference(array_t *array, const void *entry);

void array_free(array_t *array);

int array_set(array_t *array, size_t index, const void *entry);
int array_append(array_t *array, const void *entry);
int array_get(array_t *array, size_t index, void *result);
int array_each(array_t *array, int (*callback)(void *result));
int array_delete(array_t *array, comparison_fn_t filter, void *key);

// Sorting
int array_sort(array_t *array, comparison_fn_t comparator); // must be called first
int array_sorted_insert(array_t *array, const void *entry);

// Filtering; filters must be compatible with sort comparator
int array_sorted_first(array_t *array, comparison_fn_t filter, void *key, void *result);
int array_sorted_last(array_t *array, comparison_fn_t filter, void *key, void *result);
int array_sorted_each(array_t *array, comparison_fn_t filter, void *key,
        int (*callback)(void *result));
int array_sorted_extract(array_t *array, comparison_fn_t filter, void *key,
        array_t *results);

// Stats
size_t array_len(array_t *array);
void array_dump(array_t *array, void (*printer)(const void *entry));

#endif
