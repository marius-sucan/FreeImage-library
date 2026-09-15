# OpenEXR regression tests. Each one is a standalone program that prints a
# report and exits non-zero on failure. Build the library first:
# make -f Makefile.gnu dist
#
#   make            build every test
#   make run        build and run everything
#   make asan       build them with AddressSanitizer (recommended for robust)
#   make data       regenerate data/ - needs a C++17 compiler and the bundled
#                   OpenEXR headers; only do this deliberately, the recorded
#                   checksums in decode.c are tied to these exact files
#
# Scratch files go to $EXR_TEST_TMP, or the current directory.

# The library is built with OpenMP, so libfreeimage.a carries undefined
# GOMP_*/omp_* symbols: linking it needs the OpenMP runtime as well.
include ../../Makefile.openmp

TESTS   = decode regress robust
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

default: all
all: $(TESTS)

# the library is a prerequisite so a rebuilt libfreeimage.a relinks the tests
$(TESTS): %: %.c ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

# --- AddressSanitizer -------------------------------------------------------
# "make asan" instruments only the tests. "make asan-run" is the one that
# matters: it rebuilds the code this upgrade actually introduced - OpenEXRCore,
# the bundled libdeflate, OpenEXR's C++ readers and PluginEXR - with
# AddressSanitizer into a private copy of the library, and runs the tests
# against it. Mixing instrumented and plain objects is fine.
#
# allocator_may_return_null=1 is needed for robust: a corrupted data window
# makes the plugin ask FreeImage for a bitmap of whatever size the header
# claims, tens of gigabytes in some of the mutations. The library handles the
# refusal and rejects the file, which is the path being tested; ASan's default
# is to abort on a huge request instead of letting malloc return NULL, which
# would stop the test at the first such mutation. This is not specific to
# OpenEXR - no FreeImage plugin sanity-checks dimensions before allocating.
asan: CFLAGS += -fsanitize=address
asan: clean-bin all

# the flags Makefile.gnu compiles the library's C and C++ files with
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
LIBCFLAGS   = -std=gnu99 -O1 -g -fPIC -fexceptions -fvisibility=hidden $(OPENMP_CFLAGS)
LIBCXXFLAGS = -std=c++17 -O1 -g -fPIC -fexceptions -fvisibility=hidden $(OPENMP_CFLAGS)
ASAN        = -fsanitize=address -fno-omit-frame-pointer

EXR_C     = $(wildcard ../../Source/OpenEXR/OpenEXRCore/*.c)
EXR_C    += $(addprefix ../../Source/LibDeflate/lib/,adler32.c deflate_compress.c \
              deflate_decompress.c utils.c zlib_compress.c zlib_decompress.c \
              x86/cpu_features.c arm/cpu_features.c)
EXR_CXX   = $(wildcard ../../Source/OpenEXR/OpenEXR/*.cpp ../../Source/OpenEXR/Iex/*.cpp \
              ../../Source/OpenEXR/IlmThread/*.cpp ../../Source/OpenEXR/Imath/*.cpp)
# generators with a main() of their own, never part of the library
EXR_CXX  := $(filter-out %dwaLookups.cpp %b44ExpLogTable.cpp %toFloat.cpp,$(EXR_CXX))
EXR_CXX  += ../../Source/FreeImage/PluginEXR.cpp

libfreeimage-asan.a: ../../Dist/libfreeimage.a $(EXR_C) $(EXR_CXX)
	rm -rf asan-obj && mkdir -p asan-obj
	cp ../../Dist/libfreeimage.a $@
	for f in $(EXR_C); do \
	  gcc -c $(LIBCFLAGS) $(ASAN) $(LIBINC) $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o || exit 1; done
	for f in $(EXR_CXX); do \
	  g++ -c $(LIBCXXFLAGS) $(ASAN) $(LIBINC) $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o || exit 1; done
	ar r $@ asan-obj/*.o

asan-lib: libfreeimage-asan.a

asan-run: libfreeimage-asan.a
	@for t in $(TESTS); do \
	  gcc $(CFLAGS) -fsanitize=address $(INC) $$t.c -o $$t-asan \
	    libfreeimage-asan.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS) || exit 1; done
	@for t in $(TESTS); do echo "=== $$t (asan) ==="; \
	  ASAN_OPTIONS=allocator_may_return_null=1 ./$$t-asan || exit 1; done

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

# --- corpus -----------------------------------------------------------------
# mkdata.cpp writes data/ with the bundled OpenEXR itself, which is the only
# encoder here that can produce RLE, ZIPS, B44A, DWAA, DWAB, tiled, mipmapped
# and luminance/chroma files - FreeImage's own save flags reach none of those.
EXRINC = -I../../Source/OpenEXR -I../../Source/OpenEXR/Iex \
         -I../../Source/OpenEXR/IlmThread -I../../Source/OpenEXR/Imath \
         -I../../Source/OpenEXR/OpenEXR -I../../Source/OpenEXR/OpenEXRCore

data: mkdata.cpp ../../Dist/libfreeimage.a
	g++ -std=c++17 -O1 -w $(EXRINC) $< -o mkdata $(LIB)
	./mkdata data

clean-bin:
	rm -f $(TESTS) mkdata $(addsuffix -asan,$(TESTS))

clean: clean-bin
	rm -rf asan-obj libfreeimage-asan.a fi_exr_rt_*.exr

.PHONY: default all asan asan-lib asan-run run data clean clean-bin
