# FreeImage examples; build the library first: make -f Makefile.gnu dist

# libfreeimage.a needs the OpenMP runtime
include ../../Makefile.openmp

# FREEIMAGE_LIB: static link, so the examples call FreeImage_Initialise
CXXFLAGS ?= -O1 -g -Wall
INC       = -I../../Dist -I.
DEFS      = -DFREEIMAGE_LIB
LIB       = ../../Dist/libfreeimage.a -lstdc++ -lm -lpthread $(OPENMP_LDFLAGS)

PROGRAMS = AnimationDelays \
           BatchLoad \
           CloneMultiPage \
           CreateAlpha \
           FIFImportExport \
           LoadFromHandle \
           LoadFromMemory \
           LoadFromMemory-classified \
           ShowMetadata \
           WebPAnimation

# FIIO_Mem is a module, only compiled
MODULES = FIIO_Mem.o

default: all
all: $(PROGRAMS) $(MODULES)

$(PROGRAMS): %: %.cpp ../../Dist/libfreeimage.a
	g++ $(CXXFLAGS) $(DEFS) $(INC) $< -o $@ $(LIB)

$(MODULES): %.o: %.cpp %.h
	g++ $(CXXFLAGS) $(DEFS) $(INC) -c $< -o $@

clean:
	rm -f $(PROGRAMS) $(MODULES)

.PHONY: default all clean
