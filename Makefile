# Makefile for hcfetchmaps

# always runs these non-file targets
all: hcmapdemux 

# build flags common to all options and architectures
CXXFLAGS = -Izlib-hc -I. -g -O2 -Wall -pthread -std=c++17
# CXXFLAGS += -Wextra -pedantic -Werror -Wno-attributes -Wno-unknown-pragmas


LDXXFLAGS = -Lzlib-hc 
LIBS = -lzlib-hc 
CXX = gcc

# make CXXFLAGS available to sub makes
export CXXFLAGS


OBJS = \
	hcmapdemux.o


# supporting libs
hclibs:
	$(MAKE) -C zlib-hc libzlib-hc.a

hcmapdemux: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o hcmapdemux $(LIBS)

clean clobber:
	$(MAKE) -C zlib-hc clean
	touch x.o 
	rm -rf *.o hcmapdemux *.bmp
