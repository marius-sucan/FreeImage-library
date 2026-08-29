# Entry point for FreeImage makefiles
# Default to 'make -f Makefile.gnu' for Linux and for unknown OS. 
#
# All of the platform makefiles below build with OpenMP where the toolchain
# supports it (see Makefile.openmp); "make OPENMP=0" propagates down to them
# and builds single-threaded.

OS = $(shell uname)
MAKEFILE = gnu

ifeq ($(OS), Darwin)
    MAKEFILE = osx
endif
ifeq ($(OS), Cygwin)
    MAKEFILE = cygwin
endif
ifeq ($(OS), Solaris)
    MAKEFILE = solaris
endif
ifeq ($(OS), windows32)
    MAKEFILE = mingw
endif

default:
	$(MAKE) -f Makefile.$(MAKEFILE) 

all:
	$(MAKE) -f Makefile.$(MAKEFILE) all 

dist:
	$(MAKE) -f Makefile.$(MAKEFILE) dist 

install:
	$(MAKE) -f Makefile.$(MAKEFILE) install 

clean:
	$(MAKE) -f Makefile.$(MAKEFILE) clean 

