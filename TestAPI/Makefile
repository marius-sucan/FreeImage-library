# The library is built with OpenMP, so libfreeimage.a carries undefined
# GOMP_*/omp_* symbols: linking it needs the OpenMP runtime as well.
include ../Makefile.openmp

default: all

all:
	g++ -I../Dist/ *.cpp ../Dist/libfreeimage.a -o testAPI $(OPENMP_LDFLAGS)

clean:
	rm -f *.o testAPI *.png *.tif
