# libfreeimage.a needs the OpenMP runtime
include ../Makefile.openmp

default: all

all:
	g++ -I../Dist/ *.cpp ../Dist/libfreeimage.a -o testAPI $(OPENMP_LDFLAGS)

clean:
	rm -f *.o testAPI *.png *.tif
