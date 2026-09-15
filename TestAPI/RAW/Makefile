# RAW regression tests. Each one is a standalone program that prints a report
# and exits non-zero on failure. Build the library first:
# make -f Makefile.gnu dist
#
#   make            build every test
#   make run        build and run everything
#   make data       regenerate data/ from mkdata.py (deterministic)
#   make asan       build them with AddressSanitizer (recommended for robust)
#   make asan-run   rebuild the bundled LibRaw and the plugin with
#                   AddressSanitizer into a private copy of the library and run
#                   the tests against that

# The library is built with OpenMP - LibRaw's demosaicing uses it - so
# libfreeimage.a carries undefined GOMP_*/omp_* symbols and linking it needs
# the OpenMP runtime as well.
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

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

# data/ is generated, not kept: LibRaw only decodes, so unlike the WebP corpus
# nothing in these files depends on the library version. The table in decode.c
# is the oracle; this target only has to reproduce the same bytes.
data:
	python3 mkdata.py data

# --- AddressSanitizer -------------------------------------------------------
# "make asan" instruments only the tests. "make asan-run" is the one that
# matters for robust: it rebuilds what this upgrade actually replaced - the
# whole of Source/LibRawLite and PluginRAW.cpp - with AddressSanitizer into a
# private copy of the library. Mixing instrumented and plain objects is fine.
#
# RAW files are parsed metadata first and decoded second, across some seventy
# vendor-specific paths, which is where LibRaw's own release notes spend most
# of their bug fixes. A sanitized run over damaged input is the shape of test
# that catches their like.
asan: CFLAGS += -fsanitize=address
asan: clean-bin all

# the flags Makefile.gnu compiles the library's C++ files with
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(OPENMP_CFLAGS)
ASAN        = -fsanitize=address -fno-omit-frame-pointer

RAW_CXX  = $(shell find ../../Source/LibRawLite/src -name '*.cpp') \
           ../../Source/FreeImage/PluginRAW.cpp

libfreeimage-asan.a: ../../Dist/libfreeimage.a $(RAW_CXX)
	rm -rf asan-obj && mkdir -p asan-obj
	cp ../../Dist/libfreeimage.a $@
	for f in $(RAW_CXX); do \
	  g++ -c $(LIBCXXFLAGS) $(ASAN) $(LIBINC) $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o || exit 1; done
	ar r $@ asan-obj/*.o

asan-lib: libfreeimage-asan.a

asan-run: libfreeimage-asan.a
	@for t in $(TESTS); do \
	  gcc $(CFLAGS) -fsanitize=address $(INC) $$t.c -o $$t-asan \
	    libfreeimage-asan.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS) || exit 1; done
	@for t in $(TESTS); do echo "=== $$t (asan) ==="; \
	  ASAN_OPTIONS=allocator_may_return_null=1 ./$$t-asan || exit 1; done

clean-bin:
	rm -f $(TESTS) $(addsuffix -asan,$(TESTS))

clean: clean-bin
	rm -rf asan-obj libfreeimage-asan.a

.PHONY: default all run data asan asan-lib asan-run clean clean-bin
