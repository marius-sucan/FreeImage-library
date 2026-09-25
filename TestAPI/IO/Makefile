# I/O layer tests; build the library first (make -f Makefile.gnu dist)
# bigfile writes two sparse BigTIFFs (4.5 GB apparent, a few hundred KB on disk); bigsave a real 2.2 GB file

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = bigfile memstream bigsave streams
CFLAGS ?= -O1 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

# Makefile.gnu's C++ flags, relative to here: the instrumented objects have to be built like the library's
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
GNU_CXXDEFS := $(shell sed -n 's/^CXXFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CXXDEFS) $(OPENMP_CFLAGS)
# the sources streams exercises
ASAN_SRCS   = PluginTARGA PluginICO PluginGIF FreeImageIO MultiPage MemoryIO

default: all
all: $(TESTS)

$(TESTS): %: %.c ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in bigfile memstream streams; do echo "=== $$t ==="; ./$$t || exit 1; done

# streams with those sources rebuilt with AddressSanitizer, linked ahead of the library
asan-run: streams.c ../../Dist/libfreeimage.a
	rm -rf asan-obj && mkdir -p asan-obj
	for f in $(ASAN_SRCS); do g++ -c $(LIBCXXFLAGS) $(LIBINC) -fsanitize=address -fno-omit-frame-pointer ../../Source/FreeImage/$$f.cpp -o asan-obj/$$f.o || exit 1; done
	gcc $(CFLAGS) -fsanitize=address $(INC) streams.c -o streams-asan asan-obj/*.o $(LIB)
	ASAN_OPTIONS=detect_leaks=0 ./streams-asan

# 2.2 GB of RAM and of disk; --full reloads the file too (2.2 GB more RAM)
bigsave-run: bigsave
	./bigsave

memstream-grow: memstream
	./memstream --grow

clean-bin:
	rm -f $(TESTS) streams-asan

clean: clean-bin
	rm -rf fi_io_* asan-obj

.PHONY: default all run asan-run bigsave-run memstream-grow clean clean-bin
