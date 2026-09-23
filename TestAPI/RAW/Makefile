# RAW tests; build the library first (make -f Makefile.gnu dist)

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = decode regress robust
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

default: all
all: $(TESTS)

$(TESTS): %: %.c ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

# deterministic; decode.c's TABLE is the oracle
data:
	python3 mkdata.py data

# --- AddressSanitizer -------------------------------------------------------
# asan-run also instruments LibRaw and PluginRAW
asan: CFLAGS += -fsanitize=address
asan: clean-bin all

# as Makefile.gnu compiles the library
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
# Makefile.gnu's -D flags: the instrumented objects have to be built like the library's
GNU_CXXDEFS := $(shell sed -n 's/^CXXFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CXXDEFS) $(OPENMP_CFLAGS)
ASAN        = -fsanitize=address -fno-omit-frame-pointer

# linked ahead of libfreeimage.a to replace its copies, so only the library's own sources
LIBSRCS := $(addprefix ../../,$(shell grep -oE 'Source/[A-Za-z0-9_./+-]+\.(c|cc|cpp)' ../../Makefile.srcs))

RAW_CXX  = $(filter $(LIBSRCS),$(shell find ../../Source/LibRawLite/src -name '*.cpp') \
           ../../Source/FreeImage/PluginRAW.cpp)

asan-obj/.built: Makefile ../../Makefile.gnu ../../Makefile.srcs ../../Dist/libfreeimage.a $(RAW_CXX)
	rm -rf asan-obj && mkdir -p asan-obj
	for f in $(RAW_CXX); do \
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
	rm -f $(TESTS) $(addsuffix -asan,$(TESTS))

clean: clean-bin
	rm -rf asan-obj libfreeimage-asan.a

.PHONY: default all run data asan asan-lib asan-run clean clean-bin
