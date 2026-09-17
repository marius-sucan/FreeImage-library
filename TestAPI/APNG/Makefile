# APNG regression tests. Each one is a standalone program that prints a report
# and exits non-zero on failure. Build the library first:
# make -f Makefile.gnu dist
#
#   make            build every test
#   make run        build and run everything
#   make asan       build them with AddressSanitizer
#   make asan-run   rebuild PluginAPNG.cpp, PluginPNG.cpp and the bundled
#                   libpng with AddressSanitizer into a private copy of the
#                   library and run the tests against that
#
# Scratch files go to $APNG_TEST_TMP, or the current directory.

# The library is built with OpenMP, so libfreeimage.a carries undefined
# GOMP_*/omp_* symbols: linking it needs the OpenMP runtime as well.
include ../../Makefile.openmp

TESTS   = regress robust
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

# --- AddressSanitizer -------------------------------------------------------
# "make asan" instruments only the tests. "make asan-run" is the one that
# matters for robust: the APNG plugin parses the container by hand - chunk
# headers, lengths, sequence numbers, frame rectangles - and a sanitized run
# over deliberately damaged input is what catches a mistake in that.
asan: CFLAGS += -fsanitize=address
asan: clean-bin all

# the flags Makefile.gnu compiles the library's C and C++ files with
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
LIBCFLAGS   = -std=gnu99 -O1 -g -fPIC -fexceptions -fvisibility=hidden $(OPENMP_CFLAGS)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(OPENMP_CFLAGS)
ASAN        = -fsanitize=address -fno-omit-frame-pointer

PNG_C   = $(wildcard ../../Source/LibPNG/*.c)
PNG_CXX = ../../Source/FreeImage/PluginAPNG.cpp ../../Source/FreeImage/PluginPNG.cpp

libfreeimage-asan.a: ../../Dist/libfreeimage.a $(PNG_C) $(PNG_CXX)
	rm -rf asan-obj && mkdir -p asan-obj
	cp ../../Dist/libfreeimage.a $@
	for f in $(PNG_C); do \
	  gcc -c $(LIBCFLAGS) $(ASAN) $(LIBINC) $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o || exit 1; done
	for f in $(PNG_CXX); do \
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
	rm -rf asan-obj libfreeimage-asan.a apng_*.png

.PHONY: default all run asan asan-lib asan-run clean clean-bin
