# Color management tests; build the library first (make -f Makefile.gnu dist)
# targets: run, threads, asan-run, pillow; scratch files in $ICC_TEST_TMP

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = convert profiles robust
CFLAGS ?= -O2 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

default: all
all: $(TESTS)

# relink when libfreeimage.a changes
$(TESTS): %: %.c common.h ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

# the same results on one thread and on eight
threads: convert
	OMP_NUM_THREADS=1 ./convert -digest > digest-1.txt
	OMP_NUM_THREADS=8 ./convert -digest > digest-8.txt
	cmp digest-1.txt digest-8.txt && echo "one thread and eight agree: `cat digest-1.txt`"

# Pillow's ImageCms, a Little CMS of another version, as the oracle (needs Pillow and colord's profiles)
pillow:
	python3 pillow.py

clean-bin:
	rm -f $(TESTS) $(addsuffix -asan,$(TESTS)) digest-1.txt digest-8.txt

clean: clean-bin
	rm -rf asan-obj

.PHONY: default all run threads pillow clean clean-bin asan-run

# --- AddressSanitizer and UndefinedBehaviorSanitizer -----------------------
# the toolkit file and Little CMS are instrumented, linked ahead of the library

SAN         = -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
# Little CMS's fixed-point interpolation relies on signed wraparound (cmsintrp.c, cmsopt.c)
LCMS_SAN    = $(SAN) -fno-sanitize=signed-integer-overflow
LIBINC      = $$(sed -n 's/^INCLUDE *= *//p' ../../Makefile.srcs | head -1 | sed 's\#-I\#-I../../\#g')
# Makefile.gnu's -D flags: the instrumented objects have to be built like the library's
GNU_CDEFS   := $(shell sed -n 's/^CFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
GNU_CXXDEFS := $(shell sed -n 's/^CXXFLAGS += \(-D.*\)/\1/p' ../../Makefile.gnu)
LIBCFLAGS   = -std=gnu99 -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CDEFS) $(OPENMP_CFLAGS)
LIBCXXFLAGS = -std=c++0x -O1 -g -fPIC -fexceptions -fvisibility=hidden $(GNU_CXXDEFS) $(OPENMP_CFLAGS)
LCMS_C      = $(wildcard ../../Source/LibLCMS2/src/*.c)

asan-obj/.built: Makefile ../../Makefile.gnu ../../Makefile.srcs ../../Dist/libfreeimage.a ../../Source/FreeImageToolkit/ColorManagement.cpp $(LCMS_C)
	rm -rf asan-obj && mkdir -p asan-obj
	for f in $(LCMS_C); do \
	  gcc -c $(LIBCFLAGS) $(LCMS_SAN) $(LIBINC) $$f -o asan-obj/$$(basename $$f .c).o || exit 1; done
	g++ -c $(LIBCXXFLAGS) $(SAN) $(LIBINC) ../../Source/FreeImageToolkit/ColorManagement.cpp -o asan-obj/ColorManagement.o
	touch $@

asan-run: asan-obj/.built
	for t in $(TESTS); do \
	  gcc $(CFLAGS) $(SAN) $(INC) $$t.c -o $$t-asan asan-obj/*.o ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS) || exit 1; done
	ASAN_OPTIONS=detect_leaks=1 ./convert-asan -quick && ./profiles-asan && ./robust-asan 30
