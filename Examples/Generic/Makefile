# The generic FreeImage examples. Build the library first:
#   make -f Makefile.gnu dist
#
#   make            build every example
#   make clean      remove them again
#
# There was no makefile here for twenty years, which is why every one of these
# had stopped compiling: <iostream.h>, <io.h>, _stdcall, itoa. Keep this one
# working and they cannot rot again without somebody noticing.

# The library is built with OpenMP, so libfreeimage.a carries undefined
# GOMP_*/omp_* symbols: linking it needs the OpenMP runtime as well.
include ../../Makefile.openmp

# FREEIMAGE_LIB: these link the static library, so FreeImage_Initialise and
# FreeImage_DeInitialise have to be called, and that is what the guard tests.
CXXFLAGS ?= -O1 -g -Wall
INC       = -I../../Dist -I.
DEFS      = -DFREEIMAGE_LIB
LIB       = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

# every example that is a program of its own
PROGRAMS = BatchLoad \
           CloneMultiPage \
           CreateAlpha \
           FIFImportExport \
           LoadFromHandle \
           LoadFromMemory \
           LoadFromMemory-classified \
           ShowMetadata \
           WebPAnimation

# FIIO_Mem is a module rather than a program - a FreeImageIO over a memory
# buffer that a caller links into its own code - so it is only compiled.
MODULES = FIIO_Mem.o

default: all
all: $(PROGRAMS) $(MODULES)

# the library is a prerequisite so a rebuilt libfreeimage.a relinks the examples
$(PROGRAMS): %: %.cpp ../../Dist/libfreeimage.a
	g++ $(CXXFLAGS) $(DEFS) $(INC) $< -o $@ $(LIB)

$(MODULES): %.o: %.cpp %.h
	g++ $(CXXFLAGS) $(DEFS) $(INC) -c $< -o $@

clean:
	rm -f $(PROGRAMS) $(MODULES)

.PHONY: default all clean
