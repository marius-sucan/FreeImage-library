# JPEG 2000 (J2K / JP2) regression tests. Each one is a standalone program that
# prints a report and exits non-zero on failure. Build the library first:
# make -f Makefile.gnu dist
#
#   make            build every test
#   make asan       build them with AddressSanitizer (recommended for robust)
#   make run        build and run regress and robust
#   make corpus     build the corpus decoder; see README.md for what to feed it
#
# Scratch files go to $J2K_TEST_TMP, or the current directory.

# The library is built with OpenMP, so libfreeimage.a carries undefined
# GOMP_*/omp_* symbols: linking it needs the OpenMP runtime as well.
include ../../Makefile.openmp

TESTS   = regress robust corpus
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

# Makefile.srcs omits this one, so libfreeimage.a is missing TIFFHashSet*
HASHSET = tif_hash_set.o

default: all
all: $(TESTS)

$(HASHSET):
	gcc -c -O1 -w -I../../Source/LibTIFF4 -I../../Source -I../../Source/Metadata \
	    ../../Source/LibTIFF4/tif_hash_set.c -o $@

# the library is a prerequisite so a rebuilt libfreeimage.a relinks the tests
$(TESTS): %: %.c $(HASHSET) ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< $(HASHSET) -o $@ $(LIB)

asan: CFLAGS += -fsanitize=address
asan: clean-bin all

run: all
	@for t in regress robust; do \
	  echo "=== $$t ==="; ./$$t || echo "  ^ FAILED"; done

clean-bin:
	rm -f $(TESTS)

clean: clean-bin
	rm -f $(HASHSET) fi_j2k_*.j2k fi_j2k_*.jp2

.PHONY: default all asan run clean clean-bin
