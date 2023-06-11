#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "array.h"

void array_init(array_t *array, int entry_size) {
    array->entries = NULL;
    array->entry_size = entry_size;
    array->fill = 0;
    array->capacity = 0;
    array->reference = NULL;
}

int array_set_reference(array_t *array, array_t *reference) {
    if(array->entry_size != sizeof(size_t)) {
        return -EINVAL;
    }

    array->reference = reference;
    return 0;
}

void array_free(array_t *array) {
    if(array->entries != NULL) {
        free(array->entries);
    }
}

const void *array_dereference(array_t *array, const void *entry) {
    if(array->reference == NULL) {
        return entry;
    }

    size_t *index = (size_t *) entry;
    return &array->reference->entries[*index * array->reference->entry_size];
}

int _array_resize(array_t *array, size_t size) {
    if(size < array->fill || size < array->capacity) {
        return -EINVAL;
    }

    if(size == array->capacity) {
        return 0; // noop
    }

    void *ptr = realloc(array->entries, size * array->entry_size);
    if(ptr == NULL) { return -ENOMEM; }

    array->entries = ptr;
    array->capacity = size;

    return 0;
}

int array_set(array_t *array, size_t index, const void *entry) {
    int r;
    if(index >= array->capacity) {
        r = _array_resize(array, index + 1);
        if(r != 0) { return r; }
    }

    if(entry != NULL) {
        memcpy(&array->entries[index * array->entry_size], entry, array->entry_size);
    }
    
    if(index >= array->fill) {
        array->fill = index + 1;
    }

    array->comparator = NULL;

    return 0;
}

int array_append(array_t *array, const void *entry) {
    return array_set(array, array->fill, entry);
}

int array_get(array_t *array, size_t index, void *result) {
    if(index >= array->fill) {
        return -EINVAL;
    }

    memcpy(result, &array->entries[index * array->entry_size], array->entry_size);
    
    return 0;
}

int array_each(array_t *array, int (*callback)(void *result)) {
    for(size_t i=0;i<array->fill;i++) {
        callback(&array->entries[i * array->entry_size]);
    }

    return 0;
}

int array_delete(array_t *array, comparison_fn_t filter, void *key) {
    size_t cursor = 0;
    for(size_t i=0;i<array->fill;i++) {
        if(filter(key, &array->entries[i * array->entry_size]) == 0) {
            continue;
        }

        if(cursor != i) {
            memcpy(&array->entries[cursor * array->entry_size],
                    &array->entries[i * array->entry_size],
                    array->entry_size);

        }

        cursor++;
    }

    array->fill = cursor;
    return 0;
}

int array_sort(array_t *array, comparison_fn_t comparator) {
    if(comparator != NULL) {
        array->comparator = comparator;
    } else if(array->comparator == NULL) {
        return -EINVAL;
    }

    int _comparator(const void *a, const void *b) {
        a = array_dereference(array, a);
        b = array_dereference(array, b);
        return array->comparator(a, b);
    }
    qsort(array->entries, array->fill, array->entry_size, _comparator);
    return 0;
}

int array_sorted_insert(array_t *array, const void *entry) {
    int r;

    if(array->comparator == NULL) {
        return -EINVAL;
    }

    const void *entry_dereferenced = array_dereference(array, entry);

    // Binary search
    size_t low = 0;
    size_t high = array->fill;

    size_t mid = 0;

    const void *other;
    while(low < high) {
        mid = low + ((high - low) / 2);

        other = &array->entries[mid * array->entry_size];
        other = array_dereference(array, other);

        r = array->comparator(entry_dereferenced, other);

        // printf("low %lu mid %lu high %lu r %i\n", low, mid, high, r);

        if(r < 0) {
            high = mid;
        } else if(r > 0) {
            low = mid + 1;
        } else {
            // Match
            low = mid;
            break;
        }
    }

    size_t dest = low;
    if(array->fill >= array->capacity) {
        r = _array_resize(array, array->fill + 1);
        if(r != 0) { return r; }
    }

    if(dest < array->fill) {
        // move
        memmove(&array->entries[(dest + 1) * array->entry_size],
                &array->entries[dest * array->entry_size],
                (array->fill - dest) * array->entry_size);
    }

    // set
    memcpy(&array->entries[dest * array->entry_size], entry, array->entry_size);

    array->fill += 1;

    // void _printer(const void *value) {
    //     printf("%i ", *((int *) value));
    // }
    // array_dump(array, _printer);

    return 0;
}

int _array_sorted_find_index(array_t *array, comparison_fn_t filter, const void *key,
        size_t *index, int reverse) {
    int r;

    if(array->comparator == NULL) {
        return -EINVAL; // must have been sorted first
    }

    if(filter == NULL) {
        filter = array->comparator; // default comparator
    }

    size_t low = 0;
    size_t high = array->fill;
    size_t mid;

    int is_valid = 0;

    const void *other;
    while(low < high) {
        mid = low + ((high - low) / 2);

        if(reverse) {
            other = &array->entries[(array->fill - 1 - mid) * array->entry_size];
            other = array_dereference(array, other);
            r = -filter(key, other);
        } else {
            other = &array->entries[mid * array->entry_size];
            other = array_dereference(array, other);
            r = filter(key, other);
        }

        if(r == 0) {
            high = mid;
            is_valid = 1;
        } else if(r < 0) {
            high = mid;
            is_valid = 0;
        } else if(r > 0) {
            low = mid + 1;
        }
    }

    if(!is_valid) {
        return -ENOENT;
    }

    *index = (reverse) ? (array->fill - 1 - high) : high;
    return 0;
}

int _array_sorted_find_value(array_t *array, comparison_fn_t filter, void *key, void *result,
        int reverse) {
    int r;
    size_t index;

    r = _array_sorted_find_index(array, filter, key, &index, reverse);
    if(r != 0) { return r; }

    if(result != NULL) {
        memcpy(result, &array->entries[index * array->entry_size], array->entry_size);
    }
    return 0;
}

int array_sorted_first(array_t *array, comparison_fn_t filter, void *key, void *result) {
    return _array_sorted_find_value(array, filter, key, result, 0);
}

int array_sorted_last(array_t *array, comparison_fn_t filter, void *key, void *result) {
    return _array_sorted_find_value(array, filter, key, result, 1);
}

int array_sorted_each(array_t *array, comparison_fn_t filter, void *key,
        int (*callback)(void *result)) {
    int r;

    size_t first;
    size_t last;

    r = _array_sorted_find_index(array, filter, key, &first, 0);
    if(r != 0) { return r; }
    r = _array_sorted_find_index(array, filter, key, &last, 1);
    if(r != 0) { return r; }

    for(size_t i=first;i<=last;i++) {
        callback(&array->entries[i * array->entry_size]);
    }

    return 0;
}

int array_sorted_extract(array_t *array, comparison_fn_t filter, void *key,
        array_t *results) {

    int _callback(void *result) {
        array_append(results, result);
        return 0;
    }

    return array_sorted_each(array, filter, key, _callback);
}

size_t array_len(array_t *array) {
    return array->fill;
}

void array_dump(array_t *array, void (*printer)(const void *entry)) {
    for (int i = 0; i < array->fill; i++) {
        printer(&array->entries[i * array->entry_size]);
    }
}
