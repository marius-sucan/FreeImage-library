# JPEG 2000 tests; build the library first: make -f Makefile.gnu dist
# make | make asan | make run | make corpus; scratch files in $J2K_TEST_TMP

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = regress robust corpus
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

HASHSET = tif_hash_set.o

default: all
all: $(TESTS)

$(HASHSET):
	gcc -c -O1 -w -I../../Source/LibTIFF4 -I../../Source -I../../Source/Metadata \
	    ../../Source/LibTIFF4/tif_hash_set.c -o $@

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
