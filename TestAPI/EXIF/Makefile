# Exif orientation tests; build the library first (make -f Makefile.gnu dist)
# make [run]; LIBFI=<path to libfreeimage.a> runs them against another build

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = orientation
CFLAGS ?= -O1 -g -w
LIBFI  ?= ../../Dist/libfreeimage.a
INC     = -I../../Dist
LIB     = $(LIBFI) -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

default: all
all: $(TESTS)

$(TESTS): %: %.c $(LIBFI)
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

clean:
	rm -f $(TESTS)

.PHONY: default all run clean
