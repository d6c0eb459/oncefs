# Makefile

BINARY      = test
OBJS     	= lib/array.o lib/table.o lib/io.o oncefs.o
MAIN		= test.c

CC          = gcc
CFLAGS		= -Wall -O3
LIBS        = -lm
LDFLAGS     = -Wimplicit-function-declaration -D_FILE_OFFSET_BITS=64
INCLUDES	=

all: $(BINARY)

.PHONY: deps
deps:
	sudo apt-get install pkg-config libfuse3-dev fuse3

.PHONY: install
install: deps fuse

.PHONY: test.unit
test.unit: fuse mountpoint
	python3 test_unit.py
	python3 test_reuse.py

.PHONY: test.load
test.load: fuse mountpoint
	python3 test_load.py

.PHONY: run.fuse-reformat
run.fuse-reformat: fuse mountpoint test.ofs
	./fuse -d --format test.ofs mountpoint || fusermount3 -u mountpoint

.PHONY: run.fuse
run.fuse: fuse mountpoint test.ofs
	./fuse -d test.ofs mountpoint || fusermount3 -u mountpoint

.PHONY: fuse
fuse: $(OBJS)
	gcc -Wall -o fuse $(LIBS) $(CFLAGS) $(LDFLAGS) $(OBJS) fuse.c `pkg-config fuse3 --cflags --libs` 

$(BINARY): $(OBJS) $(MAIN)
	$(CC) $(LDFLAGS) -o $(BINARY) $(OBJS) $(MAIN) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJS) $(BINARY) *~ 

test.ofs:
	dd if=/dev/zero of=test.ofs bs=1M count=100

mountpoint:
	mkdir mountpoint
