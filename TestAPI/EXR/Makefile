# OpenEXR tests; build the library first (make -f Makefile.gnu dist)
# make data rewrites data/, which decode.c's checksums are tied to

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = decode regress robust lc bombs
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

default: all
all: $(TESTS)

$(TESTS): %: %.c ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

# --- AddressSanitizer -------------------------------------------------------
# asan-run also instruments OpenEXR; bombs must abort on huge allocs
asan: CFLAGS += -fsanitize=address
asan: clean-bin all

# as Makefile.gnu compiles the library
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
# generator programs, not library code
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
	  if [ $$t = bombs ]; then \
	    ./$$t-asan || exit 1; \
	  else \
	    ASAN_OPTIONS=allocator_may_return_null=1 ./$$t-asan || exit 1; \
	  fi; done

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

# --- corpus -----------------------------------------------------------------
EXRINC = -I../../Source/OpenEXR -I../../Source/OpenEXR/Iex \
         -I../../Source/OpenEXR/IlmThread -I../../Source/OpenEXR/Imath \
         -I../../Source/OpenEXR/OpenEXR -I../../Source/OpenEXR/OpenEXRCore

data: mkdata.cpp ../../Dist/libfreeimage.a
	g++ -std=c++17 -O1 -w $(EXRINC) $< -o mkdata $(LIB)
	./mkdata data

clean-bin:
	rm -f $(TESTS) mkdata $(addsuffix -asan,$(TESTS))

clean: clean-bin
	rm -rf asan-obj libfreeimage-asan.a fi_exr_rt_*.exr fi_exr_lc_*.exr

.PHONY: default all asan asan-lib asan-run run data clean clean-bin
