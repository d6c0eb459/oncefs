#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fuse.h>

#include "lib/io.h"
#include "oncefs.h"

/**
 * All c unit tests.
 */

static io_config_t io_config = {
    .path = ":memory:",
    .block_size = 512,
    .max_num_blocks = 100
};

void xxd(uint8_t *buffer, size_t size) {
    int row_width = 16;

    int num_rows = ceil((double) size / row_width);

    for (int i = 0; i < num_rows; i++) {
        for (int j = 0; j < row_width; j++) {
            int t = i * row_width + j;
            if (t > size) {
                printf("  ");
            } else {
                printf("%02x", buffer[t]);
            }
            if (j % 2 == 1) { printf(" "); }
        }

        for (int j = 0; j < row_width; j++) {
            int t = i * row_width + j;
            if (t > size) { break; }

            char c = buffer[t];
            if (c >= 36 && c <= 127) {
                printf("%c", c);
            } else {
                printf(".");
            }
        }

        printf("\n");
    }
}

int _do_test_io(char *path) {
    int r;

    io_config_t config = {
        .path = path,
        .block_size = 16,
        .max_num_blocks = 10
    };

    io_t io;
    r = io_init(&io, &config);
    if (r != 0) { return r; }

    r = io_write2(&io, 0, "One", 3, "Three", 5);
    if (r != 0) { return r; }

    r = io_write(&io, 1, "Two", 3);
    if (r != 0) { return r; }

    // r = io_write(&io, 0, "Three", 5, 3);
    // if(r != 0) { return r; }

    char buffer[100];

    r = io_read(&io, 0, &buffer, 8);
    if (r != 0) { return r; }
    buffer[8] = '\0';
    r = strcmp(buffer, "OneThree");
    if (r != 0) { return -ECANCELED; }

    // r = io_read(&io, 0, &buffer, 5, 3);
    // if(r != 0) { return r; }
    // buffer[5] = '\0';
    // r = strcmp(buffer, "Three");
    // if(r != 0) { return -ECANCELED; }

    r = io_read(&io, 1, &buffer, 3);
    if (r != 0) { return r; }
    buffer[3] = '\0';
    r = strcmp(buffer, "Two");
    if (r != 0) { return -ECANCELED; }

    // r = remove(tmp_file);
    // if(r != 0) { return -ECANCELED; }

    return 0;
}

int _test_io_file() {
    return _do_test_io("test.mfs");
}

int _test_io_memory() {
    return _do_test_io(":memory:");
}

int _test_oncefs_init() {
    int r;

    io_config_t config = {
        .path = ":memory:",
        .block_size = 512,
        .max_num_blocks = 10
    };

    io_t io;
    io_init(&io, &config);

    oncefs_t ofs;
    r = oncefs_init(&ofs, &io, 1);
    if (r != 0) { return r; }

    return 0;
}

int _test_oncefs_format() {
    int r;

    io_config_t config = {
        .path = ":memory:",
        .block_size = 64,
        .max_num_blocks = 10
    };

    io_t io;
    io_init(&io, &config);

    if (((char *) io.buffer)[64] != 0) { return -400; }

    oncefs_t ofs;
    r = oncefs_init(&ofs, &io, 1);
    if (r != 0) { return r; }

    if (((char *) io.buffer)[64] == 0) { return -400; }

    return 0;
}

int _test_oncefs_set_file() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/bar");
    if (r != 0) { return r; }

    // Cannot set over existing
    r = oncefs_set_file(&ofs, "/foo");
    if (r == 0) { return r; }

    // oncefs_dump(&ofs);

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_set_dir() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_dir(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_dir(&ofs, "/bar");
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/baz");
    if (r != 0) { return r; }

    r = oncefs_set_dir(&ofs, "/foo/bar");
    if (r != 0) { return r; }

    // Correctly select the right "bar"
    r = oncefs_set_file(&ofs, "/foo/bar/baz");
    if (r != 0) { return r; }

    // Cannot set since parent is not a directory
    r = oncefs_set_file(&ofs, "/baz/bar");
    if (r == 0) { return r; }

    // Cannot set since parent doesn't exist
    r = oncefs_set_file(&ofs, "/foo/bork/baz");
    if (r == 0) { return r; }

    // Cannot set over existing
    r = oncefs_set_dir(&ofs, "/foo");
    if (r == 0) { return r; }

    // oncefs_dump(&ofs);

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_set_link() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_link(&ofs, "/foo", "../bar");
    if (r != 0) { return r; }

    // oncefs_dump(&ofs);

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_set_data() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, "Hello world!", 12, 0);
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, "Testing", 7, 0);
    if (r != 0) { return r; }

    // oncefs_dump(&ofs);

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_set_time() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/bar");
    if (r != 0) { return r; }

    r = oncefs_set_time(&ofs, "/foo", 100, 200);
    if (r != 0) { return r; }

    // oncefs_dump(&ofs);

    oncefs_stat_t stat;
    r = oncefs_get_node(&ofs, "/foo", &stat);
    if (r != 0) { return r; }
    if(stat.last_access != 100 || stat.last_modification != 200) { return -ECANCELED; }

    r = oncefs_get_node(&ofs, "/bar", &stat);
    if (r != 0) { return r; }
    if(stat.last_access <= 0 || stat.last_modification <= 0) { return -ECANCELED; }

    oncefs_free(&ofs);

    return 0;
}


int _test_oncefs_get_status() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_dir(&ofs, "/bar");
    if (r != 0) { return r; }

    r = oncefs_set_link(&ofs, "/baz", "foo");
    if (r != 0) { return r; }

    r = oncefs_del_node(&ofs, "/foo");
    if (r != 0) { return r; }

    oncefs_status_t status;
    r = oncefs_get_status(&ofs, &status);
    if (r != 0) { return r; }

    if (status.block_size != 64) { return -400; }

    if (status.total_blocks != -2) { // max value
        return -400;
    }

    int expected_blocks_used = 5;
    int expected_blocks_freed = 2;

    if (status.free_blocks != -2 - expected_blocks_used + expected_blocks_freed) {
        printf("%lu\n", status.free_blocks);
        return -400;
    }

    if (status.name_max_size != ONCEFS_NAME_MAX_SIZE) { return -400; }

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_get_node() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_dir(&ofs, "/bar");
    if (r != 0) { return r; }

    r = oncefs_set_link(&ofs, "/baz", "foo");
    if (r != 0) { return r; }

    oncefs_stat_t stat;
    r = oncefs_get_node(&ofs, "/foo", &stat);
    if (r != 0) { return r; }

    if (!stat.is_file) { return -400; }
    if (stat.node != 1) { return -400; }
    if (stat.size != 0) { return -400; }

    r = oncefs_get_node(&ofs, "/bar", &stat);
    if (r != 0) { return r; }

    r = oncefs_get_node(&ofs, "/baz", &stat);
    if (r != 0) { return r; }

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_get_node_size() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    oncefs_stat_t stat;
    r = oncefs_get_node(&ofs, "/foo", &stat);
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, "Hello", 5, 0);
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, " world", 6, 5);
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, "!", 1, 11);
    if (r != 0) { return r; }

    // oncefs_dump(&ofs);

    r = oncefs_get_node(&ofs, "/foo", &stat);
    if (r != 0) { return r; }
    if (stat.size != 12) { return -400; }

    oncefs_free(&ofs);

    return 0;
}


int _test_oncefs_get_dir() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_dir(&ofs, "/bar");
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/bar/bork");
    if (r != 0) { return r; }

    r = oncefs_set_link(&ofs, "/baz", "foo");
    if (r != 0) { return r; }


    char actual[2048];
    int cursor = 0;
    int _aggregator(oncefs_node_t * node) {
        cursor += sprintf(&actual[cursor], "%s\n", node->name);
        return 0;
    }

    r = oncefs_get_dir(&ofs, "/", _aggregator);
    if (r != 0) { return r; }

    char *expected = "bar\nbaz\nfoo\n";
    if (strcmp(expected, actual) != 0) {
        // printf(">%s<\n", actual);
        return -400;
    }

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_get_link() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_dir(&ofs, "/bar");
    if (r != 0) { return r; }

    r = oncefs_set_link(&ofs, "/baz", "../foo");
    if (r != 0) { return r; }

    // oncefs_dump(&ofs);

    oncefs_node_t result;
    r = oncefs_get_link(&ofs, "/baz", &result);
    if (r != 0) { return r; }

    if (strcmp(result.name, "../foo") != 0) { return -400; }

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_del_file() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, "Hello world!", 12, 0);
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/foo");
    if (r == 0) { return r; }

    r = oncefs_del_node(&ofs, "/foo");
    if (r != 0) { return r; }

    // oncefs_dump(&ofs);

    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_del_dir() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_dir(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/foo/bar");
    if (r != 0) { return r; }

    r = oncefs_set_dir(&ofs, "/baz");
    if (r != 0) { return r; }

    r = oncefs_del_node(&ofs, "/foo");
    if (r == 0) { return -400; }

    r = oncefs_del_node(&ofs, "/foo/bar");
    if (r != 0) { return r; }

    r = oncefs_del_node(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    // oncefs_dump(&ofs);

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_del_link() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_link(&ofs, "/foo", "link");
    if (r != 0) { return r; }

    r = oncefs_del_node(&ofs, "link"); // doesn't exist
    if (r == 0) { return r; }

    r = oncefs_del_node(&ofs, "/foo");
    if (r != 0) { return r; }

    // oncefs_dump(&ofs); // both link and payload should be freed

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_del_node_full() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/bar");
    if (r != 0) { return r; }

    // Artificially pretend all blocks are taken
    ofs.next_block_id = ofs.last_block_id + 1;

    // Create should fail
    r = oncefs_set_file(&ofs, "/baz");
    if (r == 0) { return -ECANCELED; }

    // Delete existing should still go through
    r = oncefs_del_node(&ofs, "/foo");
    if (r != 0) { return r; }

    // Create after delete should succeed
    r = oncefs_set_file(&ofs, "/baz");
    if (r == 0) { return -ECANCELED; }

    // oncefs_dump(&ofs);

    oncefs_free(&ofs);

    return 0;
}


int _test_oncefs_del_data() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    // Setup
    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, "abcdefgh", 8, 0);
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, "ijklmnop", 8, 8);
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, "qrstuvwx", 8, 16);
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, "yz", 2, 24);
    if (r != 0) { return r; }

    // Truncate
    r = oncefs_del_data(&ofs, 1, 18);
    if (r != 0) { return r; }

    // oncefs_dump(&ofs);

    // Truncate again
    r = oncefs_del_data(&ofs, 1, 15);
    if (r != 0) { return r; }

    // oncefs_dump(&ofs);

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_del_data_rare() {
    // Test with specially crafted payloads to cause bug when wrong index is used for
    // lookup in table
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    // Setup
    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, "Hello world!", 12, 0);
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/bar");
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 2, "bork bork bork!", 16, 0);
    if (r != 0) { return r; }

    // Truncate
    r = oncefs_del_data(&ofs, 1, 0);
    if (r != 0) { return r; }

    // oncefs_dump(&ofs);

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_move_file() {
    int r;

    oncefs_t ofs;
    r = oncefs_init_default(&ofs);
    if (r != 0) { return r; }

    // Setup
    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }
    r = oncefs_set_file(&ofs, "/bar");
    if (r != 0) { return r; }
    r = oncefs_set_dir(&ofs, "/dir1");
    if (r != 0) { return r; }
    r = oncefs_set_dir(&ofs, "/dir2");
    if (r != 0) { return r; }

    // Move
    r = oncefs_move_node(&ofs, "/foo", "/baz");
    if (r != 0) { return r; }

    // Move into directory
    r = oncefs_move_node(&ofs, "/baz", "/dir1/foo");
    if (r != 0) { return r; }

    // Move into directory shortcut not supported
    r = oncefs_move_node(&ofs, "/dir1/foo", "/dir2");
    if (r == 0) { return r; }

    // Move overwriting existing
    r = oncefs_move_node(&ofs, "/dir1/foo", "/bar");
    if (r != 0) { return r; }

    // Can't move non-existing
    r = oncefs_move_node(&ofs, "/foo", "/baz");
    if (r == 0) { return r; }

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_load_state() {
    int r;

    // Initialize
    io_t io;
    r = io_init(&io, &io_config);
    if (r != 0) { return r; }

    oncefs_t ofs;
    r = oncefs_init(&ofs, &io, 1); // format
    if (r != 0) { return r; }

    // Set up
    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }
    r = oncefs_set_dir(&ofs, "/bar");
    if (r != 0) { return r; }
    r = oncefs_set_link(&ofs, "/baz", "../link");
    if (r != 0) { return r; }

    char expected[2048];
    oncefs_dumps(&ofs, expected);

    oncefs_free(&ofs);

    // Load
    r = oncefs_init(&ofs, &io, 0); // don't format
    if (r != 0) { return r; }

    if(ofs.next_node_id != 4) { return -ECANCELED; }
    if(ofs.next_block_id != 5) { return -ECANCELED; }
    if(ofs.next_seq_id != 5) { return -ECANCELED; }

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_load_get_node() {
    int r;

    // Initialize
    io_t io;
    r = io_init(&io, &io_config);
    if (r != 0) { return r; }

    oncefs_t ofs;
    r = oncefs_init(&ofs, &io, 1); // format
    if (r != 0) { return r; }

    // Set up
    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }
    r = oncefs_set_dir(&ofs, "/bar");
    if (r != 0) { return r; }
    r = oncefs_set_link(&ofs, "/baz", "../link");
    if (r != 0) { return r; }

    char expected[2048];
    oncefs_dumps(&ofs, expected);

    oncefs_free(&ofs);

    // Load
    r = oncefs_init(&ofs, &io, 0); // don't format
    if (r != 0) { return r; }

    char actual[2048];
    oncefs_dumps(&ofs, actual);

    if (strcmp(actual, expected) != 0) { return -400; }

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_load_del_node() {
    int r;

    // Initialize
    io_t io;
    r = io_init(&io, &io_config);
    if (r != 0) { return r; }

    oncefs_t ofs;
    r = oncefs_init(&ofs, &io, 1); // format
    if (r != 0) { return r; }

    // Set up
    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }
    r = oncefs_set_dir(&ofs, "/bar");
    if (r != 0) { return r; }
    r = oncefs_set_link(&ofs, "/baz", "../link");
    if (r != 0) { return r; }

    r = oncefs_del_node(&ofs, "/foo");
    if (r != 0) { return r; }

    char expected[2048];
    oncefs_dumps(&ofs, expected);

    oncefs_free(&ofs);

    // Load
    r = oncefs_init(&ofs, &io, 0); // don't format
    if (r != 0) { return r; }

    char actual[2048];
    oncefs_dumps(&ofs, actual);

    if (strcmp(actual, expected) != 0) { return -400; }

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_load_del_node_partial() {
    // Special test to ensure that a delete is processed even when
    // the relevant node no longer exists
    int r;

    // Initialize
    io_t io;
    r = io_init(&io, &io_config);
    if (r != 0) { return r; }

    oncefs_t ofs;
    r = oncefs_init(&ofs, &io, 1); // format
    if (r != 0) { return r; }

    // Set up
    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_file(&ofs, "/bar");
    if (r != 0) { return r; }

    // Force overwrite block
    ofs.next_block_id = 1;
    r = oncefs_set_file(&ofs, "/baz");
    if (r != 0) { return r; }

    // Re-establish
    ofs.next_block_id = 3;
    r = oncefs_del_node(&ofs, "/foo");
    if (r != 0) { return r; }

    char expected[2048];
    oncefs_dumps(&ofs, expected);

    oncefs_free(&ofs);

    // Load
    r = oncefs_init(&ofs, &io, 0); // don't format
    if (r != 0) { return r; }

    char actual[2048];
    oncefs_dumps(&ofs, actual);

    if (strcmp(actual, expected) != 0) {
        printf("actual %s\n", actual);
        printf("expected %s\n", expected);
        return -ECANCELED;
    }

    oncefs_free(&ofs);

    return 0;
}

int _test_oncefs_load_move_node() {
    int r;

    // Initialize
    io_t io;
    r = io_init(&io, &io_config);
    if (r != 0) { return r; }

    oncefs_t ofs;
    r = oncefs_init(&ofs, &io, 1); // format
    if (r != 0) { return r; }

    // Setup
    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }
    r = oncefs_set_file(&ofs, "/bar");
    if (r != 0) { return r; }
    r = oncefs_set_dir(&ofs, "/dir1");
    if (r != 0) { return r; }

    // Move
    r = oncefs_move_node(&ofs, "/foo", "/bar");
    if (r != 0) { return r; }
    r = oncefs_move_node(&ofs, "/bar", "/dir1/foo");
    if (r != 0) { return r; }
    r = oncefs_move_node(&ofs, "/dir1", "/dir2");
    if (r != 0) { return r; }

    // Save state
    char expected[2048];
    oncefs_dumps(&ofs, expected);

    oncefs_free(&ofs);

    // Load
    r = oncefs_init(&ofs, &io, 0); // don't format
    if (r != 0) { return r; }

    char actual[2048];
    oncefs_dumps(&ofs, actual);

    if (strcmp(actual, expected) != 0) {
        printf("actual %s\n", actual);
        printf("expected %s\n", expected);
        return -400;
    }

    oncefs_free(&ofs);

    return 0;
}


int _test_oncefs_load_get_data() {
    int r;

    // Initialize
    io_t io;
    r = io_init(&io, &io_config);
    if (r != 0) { return r; }

    oncefs_t ofs;
    r = oncefs_init(&ofs, &io, 1); // format
    if (r != 0) { return r; }

    // Setup

    size_t amount = oncefs_set_data(&ofs, 1, "Hello world!", 12, 0);
    if (amount != 12) { return amount; }

    char expected[2048];
    oncefs_dumps(&ofs, expected);

    oncefs_free(&ofs);

    // Load
    r = oncefs_init(&ofs, &io, 0); // don't format
    if (r != 0) { return r; }

    char actual[2048];
    oncefs_dumps(&ofs, actual);

    if (strcmp(actual, expected) != 0) {
        printf("actual %s\n", actual);
        printf("expected %s\n", expected);
        return -400;
    }

    // Check contents

    strcpy(expected, "Hello world!");
    int size = strlen(expected);

    r = oncefs_get_data(&ofs, 1, actual, size, 0);
    if (r != 0) { return r; }

    // Inject 0
    actual[size] = '\0';

    if (strncmp(actual, expected, size) != 0) {
        printf("actual %s\n", actual);
        printf("expected %s\n", expected);
        return -400;
    }

    return 0;
}

int _test_oncefs_load_get_data_overlay() {
    int r;

    // Initialize
    io_t io;
    r = io_init(&io, &io_config);
    if (r != 0) { return r; }

    oncefs_t ofs;
    r = oncefs_init(&ofs, &io, 1); // format
    if (r != 0) { return r; }

    // Setup
    r = oncefs_set_data(&ofs, 1, "xxxxxxxxxxxxxxxx", 16, 0);
    if (r != 0) { return r; }
    r = oncefs_set_data(&ofs, 1, "aaaaaaaaaaaaa", 12, 2);
    if (r != 0) { return r; }
    r = oncefs_set_data(&ofs, 1, "ddd", 3, 9 + 2);
    if (r != 0) { return r; }
    r = oncefs_set_data(&ofs, 1, "cccccc", 6, 3 + 2);
    if (r != 0) { return r; }
    r = oncefs_set_data(&ofs, 1, "bbb", 3, 3 + 2);
    if (r != 0) { return r; }

    char expected[2048];
    oncefs_dumps(&ofs, expected);

    oncefs_free(&ofs);

    // Load
    r = oncefs_init(&ofs, &io, 0); // don't format
    if (r != 0) { return r; }

    char actual[2048];
    oncefs_dumps(&ofs, actual);

    if (strcmp(actual, expected) != 0) { return -400; }

    // Check contents
    strcpy(expected, "xaaabbbcccdddx");
    int size = strlen(expected);

    size_t amount = oncefs_get_data(&ofs, 1, actual, size, 1);
    if (amount != size) { return r; }

    // Inject 0
    actual[size] = '\0';

    if (strncmp(actual, expected, size) != 0) {
        printf("actual %s\n", actual);
        printf("expected %s\n", expected);
        return -400;
    }

    return 0;
}

int _test_oncefs_load_get_data_large() {
    int r;

    // Initialize
    io_t io;
    r = io_init(&io, &io_config);
    if (r != 0) { return r; }

    oncefs_t ofs;
    r = oncefs_init(&ofs, &io, 1); // format
    if (r != 0) { return r; }

    // Setup
    size_t count = 1024 * 20; // must be greater than 16k
    char data[count];
    for (int i = 0; i < count; i++) {
        data[i] = (char) i;
    }

    r = oncefs_set_data(&ofs, 1, data, count, 0);
    if (r != 0) { return r; }

    char expected[5120];
    oncefs_dumps(&ofs, expected);

    oncefs_free(&ofs);

    // Load
    r = oncefs_init(&ofs, &io, 0); // don't format
    if (r != 0) { return r; }

    char actual[5120];
    oncefs_dumps(&ofs, actual);

    if (strcmp(actual, expected) != 0) { return -400; }

    // Read
    memset(data, 0, count);
    size_t amount = oncefs_get_data(&ofs, 1, data, count, 0);
    if (amount != count) { return r; }

    // Verify
    for (int i = 0; i < count; i++) {
        if (data[i] != (char) i) { return -400; }
    }

    return 0;
}


int _test_oncefs_load_del_data() {
    int r;

    // Initialize
    io_t io;
    r = io_init(&io, &io_config);
    if (r != 0) { return r; }

    oncefs_t ofs;
    r = oncefs_init(&ofs, &io, 1); // format
    if (r != 0) { return r; }

    // Setup
    int count = 1024 * 10;
    char data[count];
    for (int i = 0; i < count; i++) {
        data[i] = (char) i;
    }

    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, data, count, 0);
    if (r != 0) { return r; }

    // Unrelated
    r = oncefs_set_file(&ofs, "/bar");
    if (r != 0) { return r; }
    r = oncefs_set_data(&ofs, 2, "bork bork bork", 14, 0);
    if (r != 0) { return r; }

    // Truncate
    int cutoff = 5000;

    r = oncefs_del_data(&ofs, 1, cutoff);
    if (r != 0) { return r; }

    char expected[4096];
    oncefs_dumps(&ofs, expected);

    oncefs_free(&ofs);

    // Load
    r = oncefs_init(&ofs, &io, 0); // don't format
    if (r != 0) { return r; }

    char actual[4096];
    oncefs_dumps(&ofs, actual);

    // oncefs_dump(&ofs);

    if (strcmp(actual, expected) != 0) {
        printf("expected %s\n", expected);
        printf("actual %s\n", actual);
        return -400;
    }

    // Read
    memset(data, 0, count);
    int size = oncefs_get_data(&ofs, 1, data, count, 0);
    if (size < 0) { return size; }

    // oncefs_dump(&ofs);

    if(size != cutoff) { return -ECANCELED; }

    // Verify
    for (int i = 0; i < count; i++) {
        if (i >= cutoff) {
            // This data should have been truncated
            if (data[i] != 0) { return -400; }
        } else {
            // This data should exist
            if (data[i] != (char) i) { return -400; }
        }
    }

    return 0;
}

int _test_oncefs_load_del_data_missing() {
    int r;

    // Initialize
    io_t io;
    r = io_init(&io, &io_config);
    if (r != 0) { return r; }

    oncefs_t ofs;
    r = oncefs_init(&ofs, &io, 1); // format
    if (r != 0) { return r; }

    // Setup
    r = oncefs_set_file(&ofs, "/foo");
    if (r != 0) { return r; }

    r = oncefs_set_data(&ofs, 1, "Hello world!", 12, 0);
    if (r != 0) { return r; }

    // Truncating a non-existing node should error
    r = oncefs_del_data(&ofs, 2, 5);
    if (r == 0) { return r; }

    // Force overwriting node declaration
    ofs.next_block_id = 1;
    r = oncefs_del_data(&ofs, 1, 5);
    if (r != 0) { return r; }

    // oncefs_dump(&ofs);

    oncefs_free(&ofs);

    // When loading, truncating a non-existing node should not error
    r = oncefs_init(&ofs, &io, 0); // don't format
    if (r != 0) { return r; }

    oncefs_free(&ofs);

    return 0;
}
// Framework code

void _runner(const char *name, const int (*func)()) {
    printf("%s ...", name);

    int r = func();
    if (r == 0) {
        printf("[OK]\n");
    } else {
        if (r < -200) {
            printf("[ER] %i\n", -r);
        } else {
            printf("[ER] %s\n", strerror(-r));
        }
    }
}

void test_unit() {
    //_runner("_test_io_file", &_test_io_file); // !! requires manual setup
    _runner("_test_io_memory", &_test_io_memory);
    _runner("_test_oncefs_init", &_test_oncefs_init);
    _runner("_test_oncefs_format", &_test_oncefs_format);
    _runner("_test_oncefs_set_file", &_test_oncefs_set_file);
    _runner("_test_oncefs_set_dir", &_test_oncefs_set_dir);
    _runner("_test_oncefs_set_link", &_test_oncefs_set_link);
    _runner("_test_oncefs_set_data", &_test_oncefs_set_data);
    _runner("_test_oncefs_set_time", &_test_oncefs_set_time);
    _runner("_test_oncefs_get_status", &_test_oncefs_get_status);
    _runner("_test_oncefs_get_node", &_test_oncefs_get_node);
    _runner("_test_oncefs_get_node_size", &_test_oncefs_get_node);
    _runner("_test_oncefs_get_dir", &_test_oncefs_get_dir);
    _runner("_test_oncefs_get_link", &_test_oncefs_get_link);
    _runner("_test_oncefs_del_file", &_test_oncefs_del_file);
    _runner("_test_oncefs_del_dir", &_test_oncefs_del_dir);
    _runner("_test_oncefs_del_link", &_test_oncefs_del_link);
    _runner("_test_oncefs_del_node_full", &_test_oncefs_del_node_full);
    _runner("_test_oncefs_del_data", &_test_oncefs_del_data);
    _runner("_test_oncefs_del_data_rare", &_test_oncefs_del_data_rare);
    _runner("_test_oncefs_move_file", &_test_oncefs_move_file);
    _runner("_test_oncefs_load_state", &_test_oncefs_load_state);
    _runner("_test_oncefs_load_get_node", &_test_oncefs_load_get_node);
    _runner("_test_oncefs_load_del_node", &_test_oncefs_load_del_node);
    _runner("_test_oncefs_load_del_node_partial", &_test_oncefs_load_del_node_partial);
    _runner("_test_oncefs_load_del_data", &_test_oncefs_load_del_data);
    _runner("_test_oncefs_load_del_data_missing", &_test_oncefs_load_del_data_missing);
    _runner("_test_oncefs_load_move_node", &_test_oncefs_load_move_node);
    _runner("_test_oncefs_load_get_data", &_test_oncefs_load_get_data);
    _runner("_test_oncefs_load_get_data_overlay", &_test_oncefs_load_get_data_overlay);
    _runner("_test_oncefs_load_get_data_large", &_test_oncefs_load_get_data_large);
}

int main(int argc, char **argv) {
    test_unit();
}
