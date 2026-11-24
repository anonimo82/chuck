MODULES := raster vector model audio midi video
MIDIFILE ?= $(abspath ../../../midifile)
PREFIX ?= $(HOME)/.local/lib/chuck
CHUGINS := raster/RasterIO.chug vector/VectorIO.chug model/ModelIO.chug audio/AudioIO.chug midi/MidiFileIO.chug video/VideoIO.chug

all: $(MODULES)
$(MODULES):
	$(MAKE) -C $@ MIDIFILE="$(MIDIFILE)"

test: all
	python3 raster/test_raster.py
	python3 audio/test_audio.py
	python3 test_multimedia.py

install: all
	install -d "$(DESTDIR)$(PREFIX)"
	install -m 755 $(CHUGINS) "$(DESTDIR)$(PREFIX)/"

clean:
	@set -e; for module in $(MODULES); do $(MAKE) -C $$module clean; done

.PHONY: all test install clean $(MODULES)
