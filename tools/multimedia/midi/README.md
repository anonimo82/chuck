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

## Procedural MIDI synthesis

MidiSequence.create(ticksPerQuarter,bpm) creates a sequence with one track and initial tempo. PPQ 1–32767, tempo 4–1000 BPM. addTrack() returns the zero-based track index or -1. addNote(track,tick,length,channel,pitch,velocity), addChord(track,tick,length,channel,int[] pitches,velocity), addCC(track,tick,channel,controller,value), addProgram(track,tick,channel,program), and addTempo(tick,bpm) return 1 on success, 0 on failure. Absolute ticks are bounded to 0x0fffffff; channels 0–15; pitch/controller/program/value 0–127, note-on velocity 1–127. Chords require unique pitches. Tempo changes are stored on track zero, duplicate ticks rejected. Edits are transactional, sorted after each insertion, and restricted to generated sequences. Limits: 256 tracks and one million events. Existing transpose/velocityGain controls apply at save; no automatic resolution of overlapping notes on the same pitch/channel. Copy-and-sort edits are offline, not realtime. No new dependencies. 34 new smoke checks plus independent SMF assertions await Windows testing.

## Paired-note editing

MidiSequence.notes(track) counts unambiguous note pairs; noteLength(track,index) reports their duration in ticks. Note indices follow note-on order, not raw event indices. setNote(track,index,tick,length,pitch,velocity) edits both paired messages while preserving channel and release velocity. quantize(track,grid,strength) moves note starts toward nearest grid ticks (ties upward), preserving lengths; shiftNotes(track,delta) shifts note pairs; scaleLengths(track,factor) changes durations (1/16–16, minimum one tick); removeNote(track,index) deletes both messages. Edits work on generated and imported PPQ sequences, not SMPTE timing. Unpaired notes, same-channel/pitch overlaps, nonpositive durations and edits causing overlaps are rejected transactionally. Non-note events retain ticks except end-of-track may extend. Notes are paired within a track, not across tracks. Existing transpose and velocityGain apply on save. Offline operations, one million event limit for edits. 40 new checks and independent SMF verification await Windows compilation/testing.
