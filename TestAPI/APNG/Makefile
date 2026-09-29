# APNG regression tests; build the library first: make -f Makefile.gnu dist
# targets: all, run, chunks, asan, asan-run; scratch files go to $APNG_TEST_TMP

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = regress robust
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

default: all
all: $(TESTS)

# relink the tests when libfreeimage.a changes
$(TESTS): %: %.c ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all chunks
	@for t in $(TESTS) chunks; do echo "=== $$t ==="; ./$$t || exit 1; done

# PluginAPNG rebuilt to split image data at 1000 bytes, linked ahead of the library
chunks: chunks.c ../../Dist/libfreeimage.a ../../Source/FreeImage/PluginAPNG.cpp
	rm -rf chunks-obj && mkdir -p chunks-obj
	g++ -c $(LIBCXXFLAGS) $(LIBINC) -DAPNG_MAX_DATA_CHUNK=1000UL ../../Source/FreeImage/PluginAPNG.cpp -o chunks-obj/PluginAPNG.o
	gcc $(CFLAGS) $(INC) $< chunks-obj/PluginAPNG.o -o $@ $(LIB)

# --- AddressSanitizer -------------------------------------------------------
asan: CFLAGS += -fsanitize=address
asan: clean-bin all

# the library's own compile flags (Makefile.gnu)
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
# Makefile.gnu's -D flags: the instrumented objects have to be built like the library's
GNU_CDEFS   := $(shell sed -n 's/^CFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
GNU_CXXDEFS := $(shell sed -n 's/^CXXFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
LIBCFLAGS   = -std=gnu99 -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CDEFS) $(OPENMP_CFLAGS)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CXXDEFS) $(OPENMP_CFLAGS)
ASAN        = -fsanitize=address -fno-omit-frame-pointer

# linked ahead of libfreeimage.a to replace its copies, so only the library's own sources (pngtest.c has a main)
LIBSRCS := $(addprefix ../../,$(shell grep -oE 'Source/[A-Za-z0-9_./+-]+\.(c|cc|cpp)' ../../Makefile.srcs))

PNG_C   = $(filter $(LIBSRCS),$(wildcard ../../Source/LibPNG/*.c))
PNG_CXX = ../../Source/FreeImage/PluginAPNG.cpp ../../Source/FreeImage/PluginPNG.cpp

asan-obj/.built: Makefile ../../Makefile.gnu ../../Makefile.srcs ../../Dist/libfreeimage.a $(PNG_C) $(PNG_CXX)
	rm -rf asan-obj && mkdir -p asan-obj
	for f in $(PNG_C); do \
	  gcc -c $(LIBCFLAGS) $(ASAN) $(LIBINC) $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o || exit 1; done
	for f in $(PNG_CXX); do \
	  g++ -c $(LIBCXXFLAGS) $(ASAN) $(LIBINC) $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o || exit 1; done
	touch $@

asan-lib: asan-obj/.built

asan-run: asan-obj/.built
	@for t in $(TESTS); do \
	  gcc $(CFLAGS) -fsanitize=address $(INC) $$t.c -o $$t-asan \
	    asan-obj/*.o ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS) || exit 1; done
	@for t in $(TESTS); do echo "=== $$t (asan) ==="; \
	  ASAN_OPTIONS=allocator_may_return_null=1 ./$$t-asan || exit 1; done

clean-bin:
	rm -f $(TESTS) $(addsuffix -asan,$(TESTS)) chunks

clean: clean-bin
	rm -rf asan-obj chunks-obj libfreeimage-asan.a apng_*.png

.PHONY: default all run asan asan-lib asan-run clean clean-bin
