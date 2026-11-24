# MidiFileIO (experimental)
Optional native Chugin using https://github.com/craigsapp/midifile . Obtain that dependency separately and set MIDIFILE to its absolute source directory. Build with make MIDIFILE=/path/to/midifile using a C++17 compiler on Linux or macOS. Dependency revision is not yet pinned. Windows build pending.

- MidiFileIO.tracks(path): track count or -1.
- MidiFileIO.events(path, track): event count (including metadata), or -1. Tracks are zero-based.
- MidiFileIO.copy(source, destination): parse and write a MIDI file, 1 on success, 0 on failure; not guaranteed byte-identical.
- MidiFileIO.writeNote(path, pitch, velocity, ticks): create a single-note MIDI file at 120 BPM, 480 ticks per quarter note, MIDI channel 1. Returns 1 or 0.

Existing outputs are not overwritten. Supports Standard MIDI File input/output (.mid/.midi); not audio synthesis or live MIDI. Arbitrary event editing, multi-note composition API, event data access, and build/runtime testing remain pending. Use offline with trusted files.

## Persistent MidiSequence

```chuck
@import "MidiFileIO";
MidiSequence sequence;
if(sequence.load("input.mid")) {
    12 => sequence.transpose;
    0.8 => sequence.velocityGain;
    <<< sequence.save("output.mid") >>>;
}
```

Each object retains an independent parsed Standard MIDI File. load(path) replaces data and resets controls only on success. loaded(), tracks(), events(track), ticksPerQuarter(), tickAt(track,index), sizeAt(track,index), byteAt(track,index,byteIndex), setVelocity(track,index,value), save(path), reset(), clear() are available. Indices are zero-based and event ticks are absolute. Invalid event reads return -1. setVelocity edits a stored nonzero note-on velocity (1–127); invalid edits return 0. transpose() is an integer getter/setter in [-127,127], default 0; invalid setters preserve and return the previous value. velocityGain() is a float getter/setter in (0,16], default 1; invalid setters return NaN. Export operates on a copy: transposes note-on, note-off and polyphonic key pressure pitches, rejects out-of-range pitches, and rounds/clamps nonzero note-on velocities to 1–127. Note-on with zero velocity stays zero; note-off velocities and other messages are unchanged. These global transforms include percussion channels. Existing files are never overwritten. No live MIDI, tempo editing, arbitrary insertion, or per-track transformations yet. File operations are synchronous offline. KeyframeCurve values may be passed directly (cast to int for transpose). 35 smoke checks and independent SMF event comparisons await execution.
