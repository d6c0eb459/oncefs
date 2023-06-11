#define FUSE_USE_VERSION 31

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fuse.h>

#include "oncefs.h"

io_t io;
oncefs_t ofs;

static void *do_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    (void) conn;
    cfg->kernel_cache = 1;
    // cfg->kernel_cache = 0;

    // Disable async read
    conn->want &= ~(FUSE_CAP_ASYNC_READ);
    return NULL;
}

static int do_access(const char *path, int mask) {
    int r;

    oncefs_stat_t result;
    r = oncefs_get_node(&ofs, path, &result);
    if (r != 0) { return r; }

    return 0;
}

static int do_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    int r;

    oncefs_stat_t result;
    r = oncefs_get_node(&ofs, path, &result);
    if (r != 0) { return r; }

    oncefs_status_t status;
    r = oncefs_get_status(&ofs, &status);
    if (r != 0) { return r; }

    memset(stbuf, 0, sizeof(struct stat)); // clear
    if (result.is_file == 1) {
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
        stbuf->st_size = result.size;
        stbuf->st_blocks = ceil(result.size / 512); // number of 512 blocks by definition
    } else if (result.is_dir) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_blocks = ceil(status.block_size / 512);
    } else if (result.is_link) {
        stbuf->st_mode = S_IFLNK | 0777;
        stbuf->st_blocks = ceil(status.block_size / 512);
    }

    stbuf->st_ino = (ino_t) result.node;

    stbuf->st_atim.tv_sec = result.last_access;
    stbuf->st_mtim.tv_sec = result.last_modification;
    stbuf->st_ctim.tv_sec = result.last_access;

    return 0;
}

static int do_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                      struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    int _callback(oncefs_node_t * result) {
        filler(buf, result->name, NULL, 0, 0);
        return 0;
    }

    int r;
    r = oncefs_get_dir(&ofs, path, _callback);
    if (r != 0) {
        filler(buf, ".", NULL, 0, 0);
        filler(buf, "..", NULL, 0, 0);
    }

    return r;
}

static int do_mkdir(const char *path, mode_t mode) {
    return oncefs_set_dir(&ofs, path);
}

static int do_open(const char *path, struct fuse_file_info *fi) {
    int r;

    oncefs_stat_t stat;

    int mode = fi->flags & O_ACCMODE;
    if (mode == O_RDONLY) {
        r = oncefs_get_node(&ofs, path, &stat);
        if (r != 0) { return r; }

        if (!stat.is_file) { return -EINVAL; }

        fi->fh = stat.node;

        return 0;
    } else if (mode == O_WRONLY || mode == O_RDWR) {
        // Fetch existing node id
        r = oncefs_get_node(&ofs, path, &stat);
        if (r == -ENOENT) {
            if((fi->flags & O_CREAT) == 0) {
                return -ENOENT;
            }

            // Create a new node TODO assuming is file
            r = oncefs_set_file(&ofs, path);
            if (r != 0) { return r; }

            // Fetch the new node id
            r = oncefs_get_node(&ofs, path, &stat);
            if (r != 0) { return r; }
        } else if (stat.is_file != 1) {
            return -EINVAL;
        } else if ((fi->flags & O_APPEND) == 0) {
            // Delete existing and re-use node id
            r = oncefs_del_data(&ofs, stat.node, 0);
            if (r != 0) { return r; }
        }

        fi->fh = stat.node;

        return 0;
    }

    return -ENOSYS;
}

static int do_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    int r = do_open(path, fi);
    if (r < 0) { return r; }

    return 0;
}

static int do_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    if (fi->fh <= 0) { return -EINVAL; }

    int r;
    r = oncefs_get_data(&ofs, fi->fh, buf, size, offset);
    if (r != 0) { return r; }

    return size;
}

static int do_write(const char *path, const char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) {
    if (fi->fh <= 0) { return -EINVAL; }

    int r;
    r = oncefs_set_data(&ofs, fi->fh, buf, size, offset);
    if (r != 0) { return r; }

    return size;
}

static int do_symlink(const char *to, const char *from) {
    return oncefs_set_link(&ofs, from, to);
}

static int do_readlink(const char *path, char *buf, size_t size) {
    int r;
    oncefs_node_t result;
    r = oncefs_get_link(&ofs, path, &result);
    if (r != 0) { return r; }

    strncpy(buf, result.name, size);

    return 0;
}

static int do_unlink(const char *path) {
    return oncefs_del_node(&ofs, path);
}

static int do_rename(const char *from, const char *to, unsigned int flags) {
    if (flags) { return -EINVAL; }
    return oncefs_move_node(&ofs, from, to);
}

int do_sync(const char *path, int isdatasync, struct fuse_file_info *fi) {
    return oncefs_sync(&ofs);
}

int do_flush(const char *path, struct fuse_file_info *fi) {
    return oncefs_sync(&ofs);
}

static int do_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
    return 0;
}

static int do_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi) {
    return 0;
}

static int do_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    if (fi->fh <= 0) { return -EINVAL; }
    return oncefs_del_data(&ofs, fi->fh, size);
}

static int do_utimens(const char *path, const struct timespec ts[2], struct fuse_file_info *fi) {
    int r;
    r = oncefs_set_time(&ofs, path, ts[0].tv_sec, ts[1].tv_sec);
    if(r != 0) { return r; }

	return 0;
}

static int do_statfs(const char *path, struct statvfs *stbuf) {
    int r;
    oncefs_status_t status;
    r = oncefs_get_status(&ofs, &status);
    if(r != 0) { return r; }

    memset(stbuf, 0, sizeof(struct statvfs));
    stbuf->f_bsize = (__fsword_t) status.block_size;
    stbuf->f_blocks = (fsblkcnt_t) status.total_blocks;
    stbuf->f_bfree = (fsblkcnt_t) status.free_blocks;
    stbuf->f_bavail = (fsblkcnt_t) status.free_blocks;
    stbuf->f_namemax = status.name_max_size;

    return 0;
}

static int do_setxattr(const char *path, const char *name, const char *value,
                       size_t size, int flags) {
    return 0;
}

static int do_getxattr(const char *path, const char *name, char *value, size_t size) {
    return 0;
}

static int do_listxattr(const char *path, char *list, size_t size) {
    return 0;
}

static int do_removexattr(const char *path, const char *name) {
    return 0;
}

static const struct fuse_operations do_oper = {
    .init = do_init,
    .access = do_access,
    .getattr = do_getattr,
    .readdir = do_readdir,
    .mkdir = do_mkdir,
    .open = do_open,
    .create = do_create,
    .read = do_read,
    .write = do_write,
    .symlink = do_symlink,
    .readlink = do_readlink,
    .truncate = do_truncate,
    .unlink = do_unlink,
    .rmdir = do_unlink,
    .rename = do_rename,
    //
    .fsync = do_sync,
    .flush = do_flush,
    //
    // For setattr
    .chmod = do_chmod,
    .chown = do_chown,
    .truncate = do_truncate,
    .utimens = do_utimens,
    // End for setattr
    .statfs = do_statfs,
    // .release	= xmp_release,
    .setxattr = do_setxattr,
    .getxattr = do_getxattr,
    .listxattr = do_listxattr,
    .removexattr = do_removexattr,
};


int do_help(const char *name) {
    printf("Usage: %s [options] <file> <directory>\n\n", name);
    printf("Pass \":memory:\" in place of a file path to use RAM instead.\n\n");
    printf("Options:\n"
           "    --help    Show this info.\n"
           "    --format  Format (wipe) container.\n"
           "\n");

    return 1;
}

int main(int argc, char *argv[]) {
    int r;

    // Custom config
    int format = 0;
    char *container = NULL;

    // Parse to filter out custom args
    int argc_new = 0; // skip command
    char *argv_new[argc];
    for(int i=0;i<argc;i++) { // skip command
        if(i != 0) {
            // All args but first
            if(strcmp(argv[i], "--format") == 0) {
                format = 1;
                continue;
            } else if(strcmp(argv[i], "--help") == 0) {
                return do_help(argv[0]);
            }

            if(container == NULL && argv[i][0] != '-') {
                // First non-flag argument
                container = argv[i];
                continue;
            }
        }

        argv_new[argc_new++] = argv[i];
    }

    if(container == NULL) {
        return do_help(argv[0]);
    }

    // Startup

    io_config_t config = {
        .path = container,
        .block_size = 1024 + ONCEFS_OVERHEAD_SIZE
    };

    r = io_init(&io, &config);
    if(r != 0) {
        printf("Error %i: %s\n", -r, strerror(-r));
        return -r;
    }

    r = oncefs_init(&ofs, &io, format);
    if (r != 0) {
        printf("Error %i: %s\n", -r, strerror(-r));
        return -r;
    }

    // Pass to fuse

    // oncefs_dump(&ofs);

    r = fuse_main(argc_new, argv_new, &do_oper, NULL);
    if(r != 0) {
        return do_help(argv[0]);
    }

    return 0;
}
