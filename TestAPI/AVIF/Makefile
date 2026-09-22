# AVIF tests; build the library first (make -f Makefile.gnu dist)
# targets: run, asan-run, narrowio-step; scratch files in $AVIF_TEST_TMP

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = decode narrowio
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

# Makefile.gnu's compile flags
LIBINC   = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
LIBCFLAGS   = -std=c99 -O1 -g -fPIC -fexceptions -fvisibility=hidden -DAVIF_CODEC_DAV1D=1 $(OPENMP_CFLAGS)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden -D__ANSI__ $(OPENMP_CFLAGS)

default: all
all: $(TESTS)

$(TESTS): %: %.c
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

clean-bin:
	rm -f $(TESTS) decode-asan narrowio-asan narrowio-step

clean: clean-bin
	rm -rf asan-obj step-obj libfreeimage-asan.a libfreeimage-narrowstep.a fi_avif_*

.PHONY: default all run clean clean-bin asan-lib asan-run narrowio-step

# --- AddressSanitizer -------------------------------------------------------
# only the plugin, libavif and dav1d are instrumented
AVIF_C   = $(wildcard ../../Source/LibAVIF/src/*.c ../../Source/LibAVIF/third_party/libyuv/source/*.c)
DAV1D_C  = $(wildcard ../../Source/LibDav1d/src/*.c ../../Source/LibDav1d/src/win32/*.c ../../Source/LibDav1d/bitdepth/*.c)
DAV1D_C := $(filter-out %_tmpl.c,$(DAV1D_C))

libfreeimage-asan.a: ../../Dist/libfreeimage.a ../../Source/FreeImage/PluginAVIF.cpp $(AVIF_C) $(DAV1D_C)
	rm -rf asan-obj && mkdir -p asan-obj
	cp ../../Dist/libfreeimage.a $@
	for f in $(AVIF_C) $(DAV1D_C); do \
	  gcc -c $(LIBCFLAGS) -fsanitize=address -fno-omit-frame-pointer $(LIBINC) $$f -o asan-obj/$$(echo $$f | tr / _).o || exit 1; done
	g++ -c $(LIBCXXFLAGS) -fsanitize=address -fno-omit-frame-pointer $(LIBINC) ../../Source/FreeImage/PluginAVIF.cpp -o asan-obj/PluginAVIF.o
	ar r $@ asan-obj/*.o

asan-lib: libfreeimage-asan.a

asan-run: libfreeimage-asan.a
	gcc $(CFLAGS) -fsanitize=address $(INC) decode.c -o decode-asan libfreeimage-asan.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	gcc $(CFLAGS) -fsanitize=address $(INC) narrowio.c -o narrowio-asan libfreeimage-asan.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	./decode-asan && ./narrowio-asan

# --- stepped seek -----------------------------------------------------------
# rebuild the plugin with a tiny seek step (the Win64 32-bit long path)
narrowio-step: narrowio.c ../../Dist/libfreeimage.a ../../Source/FreeImage/PluginAVIF.cpp
	rm -rf step-obj && mkdir -p step-obj
	cp ../../Dist/libfreeimage.a libfreeimage-narrowstep.a
	g++ -c $(LIBCXXFLAGS) -DFI_AVIF_SEEK_STEP_MAX=4096 $(LIBINC) ../../Source/FreeImage/PluginAVIF.cpp -o step-obj/PluginAVIF.o
	ar r libfreeimage-narrowstep.a step-obj/PluginAVIF.o
	gcc $(CFLAGS) $(INC) narrowio.c -o narrowio-step libfreeimage-narrowstep.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)
	./narrowio-step 4096
