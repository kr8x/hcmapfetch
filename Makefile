# Makefile for hcfetchmaps

# always runs these non-file targets
all: hcmapdemux splitcurl

# build flags common to all options and architectures
CXXFLAGS = -Izlib-hc -I. -g -O2 -Wall -pthread -std=c++17
# CXXFLAGS += -Wextra -pedantic -Werror -Wno-attributes -Wno-unknown-pragmas
CFLAGS = -g -O2

LDXXFLAGS = -Lzlib-hc 
LIBS = -lzlib-hc 
CXX = gcc
CC = cc

# make CXXFLAGS available to sub makes
export CXXFLAGS

# supporting libs
hclibs:
	$(MAKE) -C zlib-hc libzlib-hc.a

hcmapdemux.o: hcmapdemux.c
	$(CXX) $(CXXFLAGS) -c hcmapdemux.c -o hcmapdemux.o

hcmapdemux: hcmapdemux.o hclibs
	$(CXX) $(LDXXFLAGS) hcmapdemux.o -o hcmapdemux $(LIBS)

splitcurl: splitcurl.c 
	$(CC) $(CFLAGS)  splitcurl.c -o splitcurl 

clean clobber:
	$(MAKE) -C zlib-hc clean
	touch x.o 
	rm -rf *.o hcmapdemux splitcurl *.raw *.txt *.bin *.bmp
