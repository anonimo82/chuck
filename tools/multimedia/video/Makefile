CXX ?= c++
PKG_CONFIG ?= pkg-config
CORE := ../../../src/core
CXXFLAGS += -std=c++17 -O2 -fPIC -D__CHUCK_CHUGIN__ -I$(CORE)
AV_PACKAGES := libavformat libavcodec libavutil libswscale libswresample sndfile pangocairo
AV_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(AV_PACKAGES))
AV_LIBS := $(shell $(PKG_CONFIG) --libs $(AV_PACKAGES))
ifeq ($(shell uname -s),Darwin)
SHARED := -dynamiclib
else
SHARED := -shared
endif
all: VideoIO.chug
VideoIO.chug: VideoIO.cpp ../subtitle/subtitle_io.h ../file_compat.h
	$(PKG_CONFIG) --exists $(AV_PACKAGES)
	$(CXX) $(CXXFLAGS) $(AV_CFLAGS) $(SHARED) -o $@ $< $(AV_LIBS)
clean:
	rm -f VideoIO.chug
.PHONY: all clean
