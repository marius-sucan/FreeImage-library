# I/O layer tests; build the library first (make -f Makefile.gnu dist)
# bigfile writes two sparse BigTIFFs (4.5 GB apparent, a few hundred KB on disk); bigsave a real 2.2 GB file

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = bigfile memstream bigsave
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

default: all
all: $(TESTS)

$(TESTS): %: %.c ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in bigfile memstream; do echo "=== $$t ==="; ./$$t || exit 1; done

# 2.2 GB of RAM and of disk; --full reloads the file too (2.2 GB more RAM)
bigsave-run: bigsave
	./bigsave

memstream-grow: memstream
	./memstream --grow

clean-bin:
	rm -f $(TESTS)

clean: clean-bin
	rm -f fi_io_*.tif

.PHONY: default all run bigsave-run memstream-grow clean clean-bin
