# BMP tests; build the library first (make -f Makefile.gnu dist)

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = masks
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

# masks with PluginBMP.cpp rebuilt with AddressSanitizer and UBSan, linked ahead of the library
asan-run: masks.c ../../Dist/libfreeimage.a
	rm -rf asan-obj && mkdir -p asan-obj
	g++ -c $(LIBCXXFLAGS) $(LIBINC) -fsanitize=address,undefined -fno-omit-frame-pointer ../../Source/FreeImage/PluginBMP.cpp -o asan-obj/PluginBMP.o
	gcc $(CFLAGS) -fsanitize=address,undefined $(INC) masks.c -o masks-asan asan-obj/PluginBMP.o $(LIB)
	./masks-asan

clean-bin:
	rm -f $(TESTS) masks-asan

clean: clean-bin
	rm -rf asan-obj

.PHONY: default all run asan-run clean clean-bin
