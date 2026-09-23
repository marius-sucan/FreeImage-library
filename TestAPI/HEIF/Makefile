# HEIF tests; needs Dist/libfreeimage.a. Scratch files: $HEIF_TEST_TMP or cwd
# targets: run asan-run narrowio-step thirdparty-run thirdparty-asan-run

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp
include ../../Makefile.heif

TESTS   = decode narrowio sequence
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

# Makefile.gnu's C++ flags, relative to here
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
# Makefile.gnu's -D flags: the instrumented objects have to be built like the library's
GNU_CXXDEFS := $(shell sed -n 's/^CXXFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CXXDEFS) $(OPENMP_CFLAGS)
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
	rm -f $(TESTS) decode-asan narrowio-asan sequence-asan narrowio-step thirdparty thirdparty-asan

clean: clean-bin
	rm -rf asan-obj step-obj libfreeimage-asan.a libfreeimage-narrowstep.a fi_heif_*.png fi_heif_*.bin fi_heif_*.webp

.PHONY: default all run clean clean-bin asan-lib asan-run narrowio-step thirdparty-run thirdparty-asan-run

# --- third-party samples -----------------------------------------------------
thirdparty: thirdparty.c ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) thirdparty.c -o $@ $(LIB)

thirdparty-run: thirdparty
	sh fetch_samples.sh
	./thirdparty

thirdparty-asan-run: asan-obj/.built
	sh fetch_samples.sh
	gcc $(CFLAGS) -fsanitize=address $(INC) thirdparty.c -o thirdparty-asan asan-obj/*.o ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	./thirdparty-asan

# --- AddressSanitizer -------------------------------------------------------
# instruments only the HEIF plugin, libheif and libde265
HEIF_CC  = $(wildcard ../../Source/LibHEIF/libheif/*.cc ../../Source/LibHEIF/libheif/*/*.cc ../../Source/LibHEIF/libheif/*/*/*.cc)
DE265_CC = $(wildcard ../../Source/LibDe265/libde265/*.cc)
ifneq (,$(filter x86_64 amd64 i86pc i386 i486 i586 i686,$(HEIF_MACHINE)))
DE265_CC += $(wildcard ../../Source/LibDe265/libde265/x86/*.cc)
endif

# linked ahead of libfreeimage.a to replace its copies, so only the library's own sources
LIBSRCS := $(addprefix ../../,$(shell grep -oE 'Source/[A-Za-z0-9_./+-]+\.(c|cc|cpp)' ../../Makefile.srcs))
HEIF_CC  := $(filter $(LIBSRCS),$(HEIF_CC))
DE265_CC := $(filter $(LIBSRCS),$(DE265_CC))

asan-obj/.built: Makefile ../../Makefile.gnu ../../Makefile.srcs ../../Dist/libfreeimage.a ../../Source/FreeImage/PluginHEIF.cpp $(HEIF_CC) $(DE265_CC)
	rm -rf asan-obj && mkdir -p asan-obj
	for f in $(HEIF_CC); do \
	  g++ -c $(HEIF_PRE) $(LIBCXXFLAGS) $(LIBINC) $(HEIF_POST) -fsanitize=address -fno-omit-frame-pointer $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o || exit 1; done
	for f in $(DE265_CC); do \
	  case $$f in */x86/sse-*) simd="$(LIBDE265_SSE_FLAGS)";; */transform-avx2.cc) simd=-mavx2;; */transform-avx512.cc) simd="-mavx512f -mavx512bw";; *) simd=;; esac; \
	  g++ -c $(DE265_PRE) $(LIBCXXFLAGS) $(LIBINC) $(DE265_POST) $$simd -fsanitize=address -fno-omit-frame-pointer $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o || exit 1; done
	g++ -c $(LIBCXXFLAGS) $(LIBINC) -fsanitize=address -fno-omit-frame-pointer ../../Source/FreeImage/PluginHEIF.cpp -o asan-obj/PluginHEIF.o
	touch $@

asan-lib: asan-obj/.built

asan-run: asan-obj/.built
	gcc $(CFLAGS) -fsanitize=address $(INC) decode.c -o decode-asan asan-obj/*.o ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	gcc $(CFLAGS) -fsanitize=address $(INC) narrowio.c -o narrowio-asan asan-obj/*.o ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	gcc $(CFLAGS) -fsanitize=address $(INC) sequence.c -o sequence-asan asan-obj/*.o ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	./decode-asan && ./narrowio-asan && ./sequence-asan

# --- stepped seek -----------------------------------------------------------
# runs narrowio against a 4 KB seek step (the Win64-only path)
narrowio-step: narrowio.c ../../Dist/libfreeimage.a ../../Source/FreeImage/PluginHEIF.cpp
	rm -rf step-obj && mkdir -p step-obj
	cp ../../Dist/libfreeimage.a libfreeimage-narrowstep.a
	g++ -c $(LIBCXXFLAGS) -DFI_HEIF_SEEK_STEP_MAX=4096 $(LIBINC) ../../Source/FreeImage/PluginHEIF.cpp -o step-obj/PluginHEIF.o
	ar r libfreeimage-narrowstep.a step-obj/PluginHEIF.o
	gcc $(CFLAGS) $(INC) narrowio.c -o narrowio-step libfreeimage-narrowstep.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	./narrowio-step 4096
