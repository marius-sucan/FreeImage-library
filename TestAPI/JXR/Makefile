# JPEG XR tests; build the library first (make -f Makefile.gnu dist)
# bigfile-run needs ~4.5 GB RAM and ~2.4 GB disk

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = regress edge narrowio meta bigfile
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

HASHSET = tif_hash_set.o

default: all
all: $(TESTS)

$(HASHSET):
	gcc -c -O1 -w -I../../Source/LibTIFF4 -I../../Source -I../../Source/Metadata \
	    ../../Source/LibTIFF4/tif_hash_set.c -o $@

$(TESTS): %: %.c $(HASHSET)
	gcc $(CFLAGS) $(INC) $< $(HASHSET) -o $@ $(LIB)

asan: CFLAGS += -fsanitize=address
asan: clean-bin all

run: all
	@for t in regress edge narrowio meta; do \
	  echo "=== $$t ==="; ./$$t || echo "  ^ FAILED"; done

bigfile-run: bigfile
	./bigfile

clean-bin:
	rm -f $(TESTS)

clean: clean-bin
	rm -f $(HASHSET) PluginJXR.o libfreeimage-narrowstep.a narrowio-step fi_jxr_*.jxr

.PHONY: default all asan run bigfile-run narrowio-step clean clean-bin

# PluginJXR with a 4 KiB seek step, to reach the stepped seek
narrowio-step: narrowio.c $(HASHSET)
	cp ../../Dist/libfreeimage.a libfreeimage-narrowstep.a
	g++ -c -std=c++0x -O1 -g -w -fPIC -fexceptions -D__ANSI__ -DFI_JXR_SEEK_STEP_MAX=4096 $(OPENMP_CFLAGS) \
	    $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's#-I#-I../../#g') \
	    ../../Source/FreeImage/PluginJXR.cpp -o PluginJXR.o
	ar r libfreeimage-narrowstep.a PluginJXR.o
	gcc $(CFLAGS) $(INC) narrowio.c $(HASHSET) -o narrowio-step \
	    libfreeimage-narrowstep.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	./narrowio-step 4096
