# MidiFileIO (experimental)
Optional native Chugin using https://github.com/craigsapp/midifile . Obtain that dependency separately and set MIDIFILE to its absolute source directory. Build with make MIDIFILE=/path/to/midifile using a C++17 compiler on Linux or macOS. Dependency revision is not yet pinned. Windows build pending.

- MidiFileIO.tracks(path): track count or -1.
- MidiFileIO.events(path, track): event count (including metadata), or -1. Tracks are zero-based.
- MidiFileIO.copy(source, destination): parse and write a MIDI file, 1 on success, 0 on failure; not guaranteed byte-identical.
- MidiFileIO.writeNote(path, pitch, velocity, ticks): create a single-note MIDI file at 120 BPM, 480 ticks per quarter note, MIDI channel 1. Returns 1 or 0.

Existing outputs are not overwritten. Supports Standard MIDI File input/output (.mid/.midi); not audio synthesis or live MIDI. Arbitrary event editing, multi-note composition API, event data access, and build/runtime testing remain pending. Use offline with trusted files.
