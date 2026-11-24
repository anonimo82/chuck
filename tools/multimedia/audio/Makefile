CXX ?= c++
PKG_CONFIG ?= pkg-config
CORE := ../../../src/core
CXXFLAGS += -std=c++17 -O2 -fPIC -D__CHUCK_CHUGIN__ -I$(CORE)
SND_CFLAGS := $(shell $(PKG_CONFIG) --cflags sndfile)
SND_LIBS := $(shell $(PKG_CONFIG) --libs sndfile)
ifeq ($(shell uname -s),Darwin)
SHARED := -dynamiclib
else
SHARED := -shared
endif
all: AudioIO.chug
AudioIO.chug: AudioIO.cpp
	$(PKG_CONFIG) --atleast-version=1.2.0 sndfile
	$(CXX) $(CXXFLAGS) $(SND_CFLAGS) $(SHARED) -o $@ $< $(SND_LIBS)
clean:
	rm -f AudioIO.chug
.PHONY: all clean
