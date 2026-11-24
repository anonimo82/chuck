CXX ?= c++
PKG_CONFIG ?= pkg-config
CORE := ../../../src/core
CXXFLAGS += -std=c++17 -O2 -fPIC -D__CHUCK_CHUGIN__ -I$(CORE)
MAGICK_CFLAGS := $(shell $(PKG_CONFIG) --cflags MagickWand)
MAGICK_LIBS := $(shell $(PKG_CONFIG) --libs MagickWand)
ifeq ($(shell uname -s),Darwin)
SHARED := -dynamiclib
else
SHARED := -shared
endif

all: RasterIO.chug

RasterIO.chug: RasterIO.cpp ../keyframe/keyframe.h ../file_compat.h
	$(CXX) $(CXXFLAGS) $(MAGICK_CFLAGS) $(SHARED) -o $@ $< $(MAGICK_LIBS)

clean:
	rm -f RasterIO.chug

.PHONY: all clean
