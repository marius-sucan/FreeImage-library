CC = gcc
CPP = g++

# libfreeimage may be the static library, which carries undefined GOMP_*/omp_*
# symbols when FreeImage was built with OpenMP (the default).
include ../../Makefile.openmp

COMPILERFLAGS = -O3
INCLUDE = -I../../Dist
VGALIBRARIES = -lfreeimage -lvga
VGAINCLUDE = -I/usr/include/asm
GTKLIBRARIES = -lfreeimage `pkg-config --libs gtk+-2.0`
GTKINCLUDE = `pkg-config --cflags gtk+-2.0`
CFLAGS = $(COMPILERFLAGS) $(INCLUDE)

all: default

default: linux-svgalib linux-gtk

linux-svgalib: linux-svgalib.c
	$(CC) $(CFLAGS) $< -o $@ $(VGALIBRARIES) $(VGAINCLUDE) $(OPENMP_LDFLAGS)
	strip $@

linux-gtk: linux-gtk.c
	$(CC) $(CFLAGS) $< -o $@ $(GTKLIBRARIES) $(GTKINCLUDE) $(OPENMP_LDFLAGS)
	strip $@

clean:
	rm -f core linux-svgalib linux-gtk
