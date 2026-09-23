# JPEG tests; build the library first (make -f Makefile.gnu dist)
# make [run|asan|asan-run]; scratch files go to $JPEG_TEST_TMP

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = decode regress robust
# oracle compares two builds; it is not a test
TOOLS   = oracle
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

default: all
all: $(TESTS)
tools: $(TOOLS)

$(TESTS) $(TOOLS): %: %.c ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

# --- AddressSanitizer -------------------------------------------------------
# asan-run instruments LibJPEG, PluginJPEG and JPEGTransform too
asan: CFLAGS += -fsanitize=address
asan: clean-bin all

# the library's own compile flags
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
# Makefile.gnu's -D flags: the instrumented objects have to be built like the library's
GNU_CDEFS   := $(shell sed -n 's/^CFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
GNU_CXXDEFS := $(shell sed -n 's/^CXXFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
LIBCFLAGS   = -std=gnu99 -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CDEFS) $(OPENMP_CFLAGS)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CXXDEFS) $(OPENMP_CFLAGS)
ASAN        = -fsanitize=address -fno-omit-frame-pointer

# only the LibJPEG files in Makefile.srcs (not the sample programs)
JPEG_C   = $(patsubst %,../../%,$(shell grep -o 'Source/LibJPEG/[A-Za-z0-9_]*\.c' ../../Makefile.srcs | sort -u))
JPEG_CXX = ../../Source/FreeImage/PluginJPEG.cpp ../../Source/FreeImageToolkit/JPEGTransform.cpp

asan-obj/.built: Makefile ../../Makefile.gnu ../../Makefile.srcs ../../Dist/libfreeimage.a $(JPEG_C) $(JPEG_CXX)
	rm -rf asan-obj && mkdir -p asan-obj
	for f in $(JPEG_C); do \
	  gcc -c $(LIBCFLAGS) $(ASAN) $(LIBINC) $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o || exit 1; done
	for f in $(JPEG_CXX); do \
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
	rm -f $(TESTS) $(TOOLS) $(addsuffix -asan,$(TESTS))

clean: clean-bin
	rm -rf asan-obj libfreeimage-asan.a fi_jpeg_rt_*.jpg fi_jpeg_xf_*.jpg

.PHONY: default all tools run asan asan-lib asan-run clean clean-bin
