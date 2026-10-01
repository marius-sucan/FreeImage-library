# JPEG 2000 tests; build the library first: make -f Makefile.gnu dist
# make | make asan | make run | make corpus | make tiles; scratch files in $J2K_TEST_TMP

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = regress robust corpus precision
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

HASHSET = tif_hash_set.o

# Makefile.gnu's C++ flags, relative to here: rebuilt objects have to be built like the library's
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
GNU_CXXDEFS := $(shell sed -n 's/^CXXFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CXXDEFS) $(OPENMP_CFLAGS)
# tiles: every image tiled, in tiles of 256, and JP2 files of 1 MB at most
TILE_DEFS   = -DFI_J2K_TILING_FROM=0 -DFI_J2K_TILE_BYTES=65536 -DFI_JP2_MAX_BYTES=1000000

default: all
all: $(TESTS)

$(HASHSET):
	gcc -c -O1 -w -I../../Source/LibTIFF4 -I../../Source -I../../Source/Metadata \
	    ../../Source/LibTIFF4/tif_hash_set.c -o $@

$(TESTS): %: %.c $(HASHSET) ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< $(HASHSET) -o $@ $(LIB)

# J2KHelper and PluginJP2 rebuilt with TILE_DEFS, linked ahead of the library
tiles: tiles.c $(HASHSET) ../../Dist/libfreeimage.a
	rm -rf tiles-obj && mkdir -p tiles-obj
	for f in J2KHelper PluginJP2; do g++ -c $(LIBCXXFLAGS) $(LIBINC) $(TILE_DEFS) ../../Source/FreeImage/$$f.cpp -o tiles-obj/$$f.o || exit 1; done
	gcc $(CFLAGS) $(INC) $< tiles-obj/*.o $(HASHSET) -o $@ $(LIB)

asan: CFLAGS += -fsanitize=address
asan: clean-bin all

run: all tiles
	@for t in regress robust tiles precision; do \
	  echo "=== $$t ==="; ./$$t || echo "  ^ FAILED"; done

clean-bin:
	rm -f $(TESTS) tiles

clean: clean-bin
	rm -rf $(HASHSET) tiles-obj fi_j2k_*.j2k fi_j2k_*.jp2

.PHONY: default all asan run clean clean-bin
