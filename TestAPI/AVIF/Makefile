# AVIF tests; build the library first (make -f Makefile.gnu dist)
# targets: run, asan-run; scratch files in $AVIF_TEST_TMP

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = decode narrowio
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

# Makefile.gnu's compile flags
LIBINC   = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
# Makefile.gnu's -D flags: the instrumented objects have to be built like the library's
GNU_CDEFS   := $(shell sed -n 's/^CFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
GNU_CXXDEFS := $(shell sed -n 's/^CXXFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
LIBCFLAGS   = -std=gnu99 -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CDEFS) $(OPENMP_CFLAGS)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CXXDEFS) $(OPENMP_CFLAGS)

default: all
all: $(TESTS)

$(TESTS): %: %.c
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

clean-bin:
	rm -f $(TESTS) decode-asan narrowio-asan

clean: clean-bin
	rm -rf asan-obj libfreeimage-asan.a fi_avif_*

.PHONY: default all run clean clean-bin asan-lib asan-run

# --- AddressSanitizer -------------------------------------------------------
# only the plugin, libavif and dav1d are instrumented
AVIF_C   = $(wildcard ../../Source/LibAVIF/src/*.c ../../Source/LibAVIF/third_party/libyuv/source/*.c)
DAV1D_C  = $(wildcard ../../Source/LibDav1d/src/*.c ../../Source/LibDav1d/src/win32/*.c ../../Source/LibDav1d/bitdepth/*.c)
DAV1D_C := $(filter-out %_tmpl.c,$(DAV1D_C))

# linked ahead of libfreeimage.a to replace its copies, so only the library's own sources
LIBSRCS := $(addprefix ../../,$(shell grep -oE 'Source/[A-Za-z0-9_./+-]+\.(c|cc|cpp)' ../../Makefile.srcs))
AVIF_C  := $(filter $(LIBSRCS),$(AVIF_C))
DAV1D_C := $(filter $(LIBSRCS),$(DAV1D_C))

asan-obj/.built: Makefile ../../Makefile.gnu ../../Makefile.srcs ../../Dist/libfreeimage.a ../../Source/FreeImage/PluginAVIF.cpp $(AVIF_C) $(DAV1D_C)
	rm -rf asan-obj && mkdir -p asan-obj
	for f in $(AVIF_C) $(DAV1D_C); do \
	  gcc -c $(LIBCFLAGS) -fsanitize=address -fno-omit-frame-pointer $(LIBINC) $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o || exit 1; done
	g++ -c $(LIBCXXFLAGS) -fsanitize=address -fno-omit-frame-pointer $(LIBINC) ../../Source/FreeImage/PluginAVIF.cpp -o asan-obj/PluginAVIF.o
	touch $@

asan-lib: asan-obj/.built

asan-run: asan-obj/.built
	gcc $(CFLAGS) -fsanitize=address $(INC) decode.c -o decode-asan asan-obj/*.o ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	gcc $(CFLAGS) -fsanitize=address $(INC) narrowio.c -o narrowio-asan asan-obj/*.o ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	./decode-asan && ./narrowio-asan
