CXX ?= c++
CORE := ../../../src/core
MIDIFILE ?= ../../../../midifile
CXXFLAGS += -std=c++17 -O2 -fPIC -D__CHUCK_CHUGIN__ -I$(CORE) -I$(MIDIFILE)/include
SOURCES := $(wildcard $(MIDIFILE)/src/*.cpp)
ifeq ($(shell uname -s),Darwin)
SHARED := -dynamiclib
else
SHARED := -shared
endif
all: MidiFileIO.chug
MidiFileIO.chug: MidiFileIO.cpp $(SOURCES)
	test -f $(MIDIFILE)/include/MidiFile.h
	$(CXX) $(CXXFLAGS) $(SHARED) -o $@ $^
clean:
	rm -f MidiFileIO.chug
.PHONY: all clean
