# HEIF regression tests. Each one is a standalone program that prints a report
# and exits non-zero on failure. Build the library first: make -f Makefile.gnu dist
#
#   make            build every test
#   make run        build and run everything
#   make asan-run   rebuild the plugin, libheif and libde265 with AddressSanitizer into
#                   a private copy of the library and run the tests against it
#   make narrowio-step
#                   run narrowio against a plugin built with a 4 KB seek step, which
#                   exercises the stepped seek that only Win64's 32-bit 'long' reaches
#
# Scratch files go to $HEIF_TEST_TMP, or the current directory.

# The library is built with OpenMP, so libfreeimage.a carries undefined
# GOMP_*/omp_* symbols: linking it needs the OpenMP runtime as well.
include ../../Makefile.openmp
# the per-library compiler flags of the two HEIF libraries (C++17/20, SIMD, defines)
include ../../Makefile.heif

TESTS   = decode narrowio
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

# the flags Makefile.gnu compiles the library's C++ files with, seen from this directory
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden -D__ANSI__ $(OPENMP_CFLAGS)
HEIF_PRE    = $(subst -ISource,-I../../Source,$(LIBHEIF_CXXFLAGS_PRE))
HEIF_POST   = $(LIBHEIF_CXXFLAGS_POST)
DE265_PRE   = $(subst -ISource,-I../../Source,$(LIBDE265_CXXFLAGS_PRE))
DE265_POST  = $(LIBDE265_CXXFLAGS_POST)

default: all
all: $(TESTS)

$(TESTS): %: %.c
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

clean-bin:
	rm -f $(TESTS) decode-asan narrowio-asan narrowio-step

clean: clean-bin
	rm -rf asan-obj step-obj libfreeimage-asan.a libfreeimage-narrowstep.a fi_heif_*.png fi_heif_*.bin

.PHONY: default all run clean clean-bin asan-lib asan-run narrowio-step

# --- AddressSanitizer -------------------------------------------------------
# Only the HEIF plugin, libheif and libde265 are instrumented; the rest of the
# library is taken as built. Mixing instrumented and plain objects is fine.
# libde265's x86 kernels get the same per-file SIMD flags as in Makefile.heif.
HEIF_CC  = $(wildcard ../../Source/LibHEIF/libheif/*.cc ../../Source/LibHEIF/libheif/*/*.cc ../../Source/LibHEIF/libheif/*/*/*.cc)
DE265_CC = $(wildcard ../../Source/LibDe265/libde265/*.cc)
ifneq (,$(filter x86_64 amd64 i86pc i386 i486 i586 i686,$(HEIF_MACHINE)))
DE265_CC += $(wildcard ../../Source/LibDe265/libde265/x86/*.cc)
endif

libfreeimage-asan.a: ../../Dist/libfreeimage.a ../../Source/FreeImage/PluginHEIF.cpp $(HEIF_CC) $(DE265_CC)
	rm -rf asan-obj && mkdir -p asan-obj
	cp ../../Dist/libfreeimage.a $@
	for f in $(HEIF_CC); do \
	  g++ -c $(HEIF_PRE) $(LIBCXXFLAGS) $(LIBINC) $(HEIF_POST) -fsanitize=address -fno-omit-frame-pointer $$f -o asan-obj/$$(echo $$f | tr / _).o || exit 1; done
	for f in $(DE265_CC); do \
	  case $$f in */x86/sse-*) simd="$(LIBDE265_SSE_FLAGS)";; */transform-avx2.cc) simd=-mavx2;; */transform-avx512.cc) simd="-mavx512f -mavx512bw";; *) simd=;; esac; \
	  g++ -c $(DE265_PRE) $(LIBCXXFLAGS) $(LIBINC) $(DE265_POST) $$simd -fsanitize=address -fno-omit-frame-pointer $$f -o asan-obj/$$(echo $$f | tr / _).o || exit 1; done
	g++ -c $(LIBCXXFLAGS) $(LIBINC) -fsanitize=address -fno-omit-frame-pointer ../../Source/FreeImage/PluginHEIF.cpp -o asan-obj/PluginHEIF.o
	ar r $@ asan-obj/*.o

asan-lib: libfreeimage-asan.a

asan-run: libfreeimage-asan.a
	gcc $(CFLAGS) -fsanitize=address $(INC) decode.c -o decode-asan libfreeimage-asan.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	gcc $(CFLAGS) -fsanitize=address $(INC) narrowio.c -o narrowio-asan libfreeimage-asan.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	./decode-asan && ./narrowio-asan

# --- stepped seek -----------------------------------------------------------
# HEIF_SeekTo reaches offsets beyond FI_HEIF_SEEK_STEP_MAX by rewinding and
# stepping forward. Where 'long' is 64-bit that branch is unreachable, so it
# would otherwise ship to Windows untested. Rebuild the plugin with a tiny step
# into a private copy of the library and run narrowio with a matching cap.
narrowio-step: narrowio.c ../../Dist/libfreeimage.a ../../Source/FreeImage/PluginHEIF.cpp
	rm -rf step-obj && mkdir -p step-obj
	cp ../../Dist/libfreeimage.a libfreeimage-narrowstep.a
	g++ -c $(LIBCXXFLAGS) -DFI_HEIF_SEEK_STEP_MAX=4096 $(LIBINC) ../../Source/FreeImage/PluginHEIF.cpp -o step-obj/PluginHEIF.o
	ar r libfreeimage-narrowstep.a step-obj/PluginHEIF.o
	gcc $(CFLAGS) $(INC) narrowio.c -o narrowio-step libfreeimage-narrowstep.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	./narrowio-step 4096
