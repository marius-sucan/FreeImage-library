# MNG tests. Each one is a standalone program that prints a report and exits
# non-zero on failure. Build the library first:
# make -f Makefile.gnu dist
#
#   make            build every test
#   make run        build and run everything
#   make asan       build them with AddressSanitizer
#   make asan-run   rebuild PluginMNG.cpp, MNGHelper.cpp, PluginPNG.cpp and the
#                   bundled libpng with AddressSanitizer into a private copy of
#                   the library and run the tests against that
#
# Scratch files go to $MNG_TEST_TMP, or the current directory.
#
# There is no data directory: MNG has no writer, so there is nothing to make a
# corpus with, and the tests build their datastreams themselves from the chunk
# layouts in the specification (see mngbuild.h). The frames inside them are
# made with FreeImage's own PNG writer and stripped of their signature, which
# is exactly what a MNG encoder does with them - the tests are about the
# container, not about PNG.

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
$(TESTS): %: %.c mngbuild.h ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

# --- AddressSanitizer -------------------------------------------------------
# "make asan" instruments only the tests. "make asan-run" is the one that
# matters for robust: the MNG plugin parses the container by hand - chunk
# headers, lengths, the extent of each embedded datastream, the variable-length
# bodies of FRAM and DEFI - and a sanitized run over deliberately damaged input
# is what catches a mistake in that.
asan: CFLAGS += -fsanitize=address
asan: clean-bin all

# the flags Makefile.gnu compiles the library's C and C++ files with
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
LIBCFLAGS   = -std=gnu99 -O1 -g -fPIC -fexceptions -fvisibility=hidden $(OPENMP_CFLAGS)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(OPENMP_CFLAGS)
ASAN        = -fsanitize=address -fno-omit-frame-pointer

MNG_C   = $(wildcard ../../Source/LibPNG/*.c)
MNG_CXX = ../../Source/FreeImage/PluginMNG.cpp ../../Source/FreeImage/MNGHelper.cpp \
          ../../Source/FreeImage/PluginJNG.cpp ../../Source/FreeImage/PluginPNG.cpp \
          ../../Source/FreeImage/MultiPage.cpp

libfreeimage-asan.a: ../../Dist/libfreeimage.a $(MNG_C) $(MNG_CXX)
	rm -rf asan-obj && mkdir -p asan-obj
	cp ../../Dist/libfreeimage.a $@
	for f in $(MNG_C); do \
	  gcc -c $(LIBCFLAGS) $(ASAN) $(LIBINC) $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o || exit 1; done
	for f in $(MNG_CXX); do \
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
	rm -rf asan-obj libfreeimage-asan.a mng_*.mng

.PHONY: default all run asan asan-lib asan-run clean clean-bin
