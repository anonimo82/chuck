CXX ?= c++
PKG_CONFIG ?= pkg-config
CORE := ../../../src/core
CXXFLAGS += -std=c++17 -O2 -fPIC -D__CHUCK_CHUGIN__ -I$(CORE)
SVG_CFLAGS := $(shell $(PKG_CONFIG) --cflags librsvg-2.0 cairo)
SVG_LIBS := $(shell $(PKG_CONFIG) --libs librsvg-2.0 cairo)
ifeq ($(shell uname -s),Darwin)
SHARED := -dynamiclib
else
SHARED := -shared
endif

all: VectorIO.chug

VectorIO.chug: VectorIO.cpp
	$(PKG_CONFIG) --atleast-version=2.46 librsvg-2.0
	$(CXX) $(CXXFLAGS) $(SVG_CFLAGS) $(SHARED) -o $@ $< $(SVG_LIBS)

clean:
	rm -f VectorIO.chug

.PHONY: all clean
