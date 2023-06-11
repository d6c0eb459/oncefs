# OnceFS

Oncefs is a small experimental filesystem built to minimize disk writes.

The filesystem is based on top of a log based database:
- Every create, read, update, or delete is written as a new log entry.
- All log entries are written strictly append only.
- An in-memory index provides fast search and directory listing.
- If there is no space to append a new log entries, the filesystem can optionally switch into a
  second mode where obsolete entries can be re-used.

The entire database is stored as a single file.

# Installation

This repository only depends on libfuse (aside from gcc, make, etc.)

On Debian based systems run `make install`, otherwise install an equivalent to libfuse-dev manually.

# Tests

- For c unit tests run `make test && ./test`
- For Python based testing of fuse functionality run `make test.unit`

# Usage

```bash
make install

# Create a container to hold the filesystem.
dd if=/dev/zero of=test.ofs bs=1M count=100

# Create a mount point
mkdir -p mountpoint

# Format the container and mount it
./fuse --format test.ofs mountpoint

# Use the file system, ex.
echo "Hello world!" > mountpoint/test.txt

# Unmount
fusermount -u mountpoin

# Remount
./fuse test.ofs mountpoint

# Read
cat mountpoint/test.txt
```

©️ Derek Cheung 2023
