# FreeImage_MustTonemap() test; build the library first (make -f Makefile.gnu dist)

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

TESTS   = musttonemap
CFLAGS ?= -O2 -g -w
INC     = -I../../Dist
LIB     = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread -ldl $(OPENMP_LDFLAGS)

default: all
all: $(TESTS)

# relink when libfreeimage.a changes
$(TESTS): %: %.c ../../Dist/libfreeimage.a
	gcc $(CFLAGS) $(INC) $< -o $@ $(LIB)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done

clean:
	rm -f $(TESTS)

.PHONY: default all run clean
