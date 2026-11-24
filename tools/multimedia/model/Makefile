CXX ?= c++
PKG_CONFIG ?= pkg-config
CORE := ../../../src/core
CXXFLAGS += -std=c++17 -O2 -fPIC -D__CHUCK_CHUGIN__ -I$(CORE)
ASSIMP_CFLAGS := $(shell $(PKG_CONFIG) --cflags assimp)
ASSIMP_LIBS := $(shell $(PKG_CONFIG) --libs assimp)
ifeq ($(shell uname -s),Darwin)
SHARED := -dynamiclib
else
SHARED := -shared
endif
all: ModelIO.chug
ModelIO.chug: ModelIO.cpp
	$(PKG_CONFIG) --exists assimp
	$(CXX) $(CXXFLAGS) $(ASSIMP_CFLAGS) $(SHARED) -o $@ $< $(ASSIMP_LIBS)
clean:
	rm -f ModelIO.chug
.PHONY: all clean
