# JPEG XL tests; build the library first (make -f Makefile.gnu dist)
# targets: run, asan-run, conformance; scratch files in $JXL_TEST_TMP

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = decode regress robust
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread -ldl $(OPENMP_LDFLAGS)

default: all
all: $(TESTS)

# relink when libfreeimage.a changes
$(TESTS): %: %.c pattern.h ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

# wrote data/, which is kept: see README.md
mkdata: mkdata.c pattern.h ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) -I../../Source/LibJXL/lib/include $< -o $@ $(LIB)

# the JPEG XL conformance suite, fetched into conformance/ by fetch_conformance.py
conformance:
	python3 conformance.py ../../Dist/libfreeimage-3.20.0.so conformance

# --- AddressSanitizer -------------------------------------------------------
# asan-run instruments the plugin, libjxl, highway and brotli

# Makefile.gnu's and Makefile.jxl's compile flags
LIBINC      := $(shell sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's|-I|-I../../|g')
GNU_CDEFS   := $(shell sed -n 's/^CFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
GNU_CXXDEFS := $(shell sed -n 's/^CXXFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
JXL_PRE     := $(shell sed -n 's/^LIBJXL_CXXFLAGS_PRE = //p' ../../Makefile.jxl | sed 's|-I|-I../../|g')
JXL_POST    := $(shell sed -n 's/^LIBJXL_CXXFLAGS_POST = //p' ../../Makefile.jxl)
LIBCFLAGS   = -std=gnu99 -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CDEFS) $(OPENMP_CFLAGS)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CXXDEFS) $(OPENMP_CFLAGS)
ASAN        = -fsanitize=address -fno-omit-frame-pointer
JOBS       ?= 4

# linked ahead of libfreeimage.a to replace its copies, so only the library's own sources
LIBSRCS := $(addprefix ../../,$(shell grep -oE 'Source/[A-Za-z0-9_./+-]+\.(c|cc|cpp)' ../../Makefile.srcs))
JXL_CC   = $(filter ../../Source/LibJXL/%,$(LIBSRCS))
BROTLI_C = $(filter ../../Source/LibBrotli/%,$(LIBSRCS))
PLUGIN   = ../../Source/FreeImage/PluginJXL.cpp

asan-obj/.built: Makefile ../../Makefile.gnu ../../Makefile.srcs ../../Makefile.jxl ../../Dist/libfreeimage.a $(PLUGIN)
	rm -rf asan-obj && mkdir -p asan-obj
	for f in $(JXL_CC); do echo "g++ -c $(JXL_PRE) $(LIBCXXFLAGS) $(ASAN) $(LIBINC) $(JXL_POST) $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o"; done > asan-obj/commands
	for f in $(BROTLI_C); do echo "gcc -c $(LIBCFLAGS) $(ASAN) $(LIBINC) $$f -o asan-obj/$$(echo $$f | sed 's#^\.\./\.\./##; s#/#_#g').o"; done >> asan-obj/commands
	echo "g++ -c $(LIBCXXFLAGS) $(ASAN) $(LIBINC) $(PLUGIN) -o asan-obj/PluginJXL.o" >> asan-obj/commands
	xargs -P $(JOBS) -d '\n' -I{} sh -c '{}' < asan-obj/commands
	test $$(ls asan-obj/*.o | wc -l) -eq $$(wc -l < asan-obj/commands)
	touch $@

asan-lib: asan-obj/.built

asan-run: asan-obj/.built
	@for t in $(TESTS); do \
	  gcc $(CFLAGS) -fsanitize=address $(INC) $$t.c -o $$t-asan \
	    asan-obj/*.o ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread -ldl $(OPENMP_LDFLAGS) || exit 1; done
	@for t in $(TESTS); do echo "=== $$t (asan) ==="; \
	  ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=1 ./$$t-asan || exit 1; done

clean-bin:
	rm -f $(TESTS) mkdata $(addsuffix -asan,$(TESTS))

clean: clean-bin
	rm -rf asan-obj fi_jxl_*

.PHONY: default all run mkdata conformance asan-lib asan-run clean clean-bin
