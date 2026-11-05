# Quantizer tests; build the library first (make -f Makefile.gnu dist)
# targets: run, asan-run

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = reference
CFLAGS ?= -O2 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

default: all
all: $(TESTS)

# relink when libfreeimage.a changes
$(TESTS): %: %.c ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

clean-bin:
	rm -f $(TESTS) $(addsuffix -asan,$(TESTS))

clean: clean-bin
	rm -rf asan-obj

.PHONY: default all run clean clean-bin asan-run

# --- AddressSanitizer and UndefinedBehaviorSanitizer -----------------------
# MedianCutQuantizer.cpp instrumented, linked ahead of the library

SAN         = -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
# Makefile.gnu's -D flags: the instrumented object has to be built like the library's
GNU_CXXDEFS := $(shell sed -n 's/^CXXFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CXXDEFS) $(OPENMP_CFLAGS)

asan-obj/.built: Makefile ../../Makefile.gnu ../../Makefile.srcs ../../Dist/libfreeimage.a ../../Source/FreeImage/MedianCutQuantizer.cpp
	rm -rf asan-obj && mkdir -p asan-obj
	g++ -c $(LIBCXXFLAGS) $(SAN) $(LIBINC) ../../Source/FreeImage/MedianCutQuantizer.cpp -o asan-obj/MedianCutQuantizer.o
	touch $@

asan-run: asan-obj/.built
	for t in $(TESTS); do \
	  gcc $(CFLAGS) $(SAN) $(INC) $$t.c -o $$t-asan asan-obj/*.o ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS) || exit 1; done
	for t in $(TESTS); do echo "=== $$t (asan) ==="; ASAN_OPTIONS=detect_leaks=1 ./$$t-asan || exit 1; done
