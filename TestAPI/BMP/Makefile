# BMP tests; build the library first (make -f Makefile.gnu dist)

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = masks icc rle save nopixels
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

# Makefile.gnu's C++ flags, relative to here: the instrumented object has to be built like the library's
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
GNU_CXXDEFS := $(shell sed -n 's/^CXXFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CXXDEFS) $(OPENMP_CFLAGS)

default: all
all: $(TESTS)

$(TESTS): %: %.c ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

# the tests with PluginBMP.cpp rebuilt with AddressSanitizer and UBSan, linked ahead of the library
asan-run: $(TESTS:%=%.c) ../../Dist/libfreeimage.a
	rm -rf asan-obj && mkdir -p asan-obj
	g++ -c $(LIBCXXFLAGS) $(LIBINC) -fsanitize=address,undefined -fno-omit-frame-pointer ../../Source/FreeImage/PluginBMP.cpp -o asan-obj/PluginBMP.o
	@for t in $(TESTS); do gcc $(CFLAGS) -fsanitize=address,undefined $(INC) $$t.c -o $$t-asan asan-obj/PluginBMP.o $(LIB) || exit 1; done
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t-asan || exit 1; done

clean-bin:
	rm -f $(TESTS) $(TESTS:%=%-asan)

clean: clean-bin
	rm -rf asan-obj

.PHONY: default all run asan-run clean clean-bin
