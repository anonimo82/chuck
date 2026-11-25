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

## Offline MIDI-to-PCM synthesis

renderFrames(sampleRate) returns the mono sample count or -1. renderSamples(sampleRate,float[] samples) fills an exact-size preallocated PCM array; invalid requests leave it unchanged. Rates 8000–96000 Hz, maximum 32 Mi samples and 100 million summed voice samples. PPQ tempo events across tracks are honored (default 120 BPM); simultaneous tempo events resolved in track/event order. Notes are paired within tracks. Unmatched and overlapping same-pitch/channel notes within a track are rejected. Equal-tempered sine synthesis, fixed 0.25 voice gain, velocity scaling, up to 5 ms edge ramps and final clipping. Transpose and velocityGain apply. Channel 10 percussion and pitches at/above Nyquist are rejected; program changes, sustain, controllers and pitch bend are ignored. Not a General MIDI instrument renderer. Duration includes trailing events. No new dependency. PCM can be passed to AudioBuffer.importSamples(rate,1,samples). Native validation pending.

Simultaneous note boundaries are paired with note-offs (including velocity-zero note-ons) before note-ons at the same tick. Adjacent notes of the same channel and pitch are supported; genuine overlaps and nonpositive durations remain rejected. Regression tests added; native validation pending.

## Fixed-block pitch sequence import

importPitches(ticksPerQuarter,bpm,blockSeconds,channel,velocity,int[] pitches) replaces the sequence with one track and initial tempo. Values -1 (silence) or 0–127; adjacent equal pitches are merged into one note. Tick boundaries are rounded independently from absolute block positions. Requires 1–100000 entries, 1–32767 PPQ, BPM 4–1000, positive block seconds up to 60, at least one tick per block and total ticks <=0x0fffffff. Channel 0–15, velocity 1–127. Trailing silence does not extend the last MIDI event. Success resets transpose/velocity gain, failure preserves previous sequence. All-silent input produces a tempo-only track. Compatible with AudioBuffer.detectPitches; no new native dependency. Native validation pending.

## Animated piano-roll video

exportPianoRoll(sampleRate,cursorFrame,width,height,int[] pixels) renders the entire MIDI sequence into opaque RGBA8, time horizontally and all 128 pitches vertically (highest at top). Uses the existing audio render tempo map, note pairing, transpose and validation, including rejection of percussion channel 10 and pitches above Nyquist. Inactive notes are blue (48,112,192), active notes orange (255,192,64), playhead white, background black. Active intervals are half-open; cursor may equal total rendered frames. Dimensions width 1–4096, height 128–4096, up to 4194304 pixels, exact RGBA output size; bounded rectangle work. Failure preserves output. The example piano_roll_video.ck combines renderSamples with VideoWriter.openAV/appendAV at 48000 Hz and 25 fps, padding final audio block with silence. Sine synthesis, not General MIDI instrument emulation; no new dependency. Native validation pending.

## Static velocity-colored piano roll

exportStaticPianoRoll(width,height,rgb,backgroundRGB,int[] pixels) draws all paired notes across the sequence's tempo-aware duration, high pitches at top, no playhead. Each pixel interpolates background and note color by processed velocity/127; highest velocity wins overlaps. Both RGB values 0–0xffffff, opaque RGBA8, width 1–4096, height 128–4096, max 4194304 pixels, exact output array. Uses the existing pairing and tempo map with 96000 Hz internal time resolution and existing duration/work limits; unlike audio synthesis, accepts percussion and does not require audible pitch. Transpose and velocityGain apply. Invalid inputs preserve output. Existing animated exportPianoRoll method unchanged; static method has a distinct name to avoid identical argument signatures. Example static_piano_roll.ck saves a PNG via RasterImage. No new dependencies. Native validation pending.

## Editable vector piano roll

pianoRollRectCount() returns the paired note count (0–4096), or -1 on invalid sequence or exceeded limits. exportPianoRollRects(width,height,rgb,float[] rects) exports exactly six floats per note: x,y,width,height,packed RGB,opacity (processed velocity/127). Same visual tempo map, transpose, percussion support and resource limits as the static piano roll; fractional coordinates retain subpixel note geometry. Width 1–4096, height 128–4096, RGB 0–0xffffff. Failure preserves output. Use VectorImage.importRects to create editable SVG shapes without rasterization. Overlaps composite by SVG document order, not the raster exporter's maximum velocity rule. Example vector_piano_roll.ck. No new dependencies. Native validation pending.

## MIDI note geometry in 3D

exportNoteBoxes(timeSpan,pitchStep,heightScale,float[] boxes) exports six floats per paired note: xmin,ymin,zmin,xmax,ymax,zmax. Use pianoRollRectCount() to allocate six values per note, 1–4096 notes required. Time is centered across timeSpan using the visual tempo map, pitch centered at MIDI 63.5 with width 0.8*pitchStep, Z from zero to processed velocity/127*heightScale. All three scales finite 0.001–1000000. Same visual timing and resource limits, transpose, velocityGain and percussion support as vector piano roll. Invalid inputs preserve output. Pass boxes to Model3D.createBoxes for a single mesh with independent closed note solids. Example note_geometry.ck. Visualization, not acoustic modeling. No new dependencies; native validation pending.

## Timestamped MIDI capture

MidiCapture.create(ticksPerQuarter,bpm) initializes a fixed-tempo event buffer (TPQ 1–32767, BPM 4–1000). append(seconds,status,data1,data2) accepts explicit nonnegative nondecreasing elapsed seconds and MIDI channel voice statuses 0x80–0xef, with 7-bit data. Program change and channel pressure require data2=0 and store two bytes; all other channel voice messages store three bytes. Equal timestamps retain arrival order. tickAt rounds elapsed seconds * TPQ * BPM / 60 to the nearest tick (half upward), bounded to 0x0fffffff. count(), secondsAt(index), tickAt(index), sizeAt(index), byteAt(index,byteIndex) inspect the recording; invalid index returns -1. clear() empties events while preserving configuration. Invalid create/append preserve state. Maximum one million messages; no system/SysEx or running status. This buffer accepts caller-provided timestamps and does not open a device, save SMF or manage sessions yet. Native validation pending.

## MIDI capture persistence and device bridge

appendMessage(seconds,data1,data2,data3) accepts the three fields of native MidiMsg; for program change and channel pressure the unused third field is ignored. The caller opens MidiIn and drains it using recv(msg); elapsed time can be computed from ChucK now relative to a start time. This is receive-time stamping, not a hardware timestamp guarantee. save(path) exports a single-track Standard MIDI File at the configured TPQ and fixed tempo, preserving arrival order at equal ticks. Existing files are not overwritten. The in-memory recording is unchanged. Example (requires an actual MIDI input):

```chuck
@import "MidiFileIO";
MidiIn input;
MidiMsg msg;
MidiCapture recorder;
if(!input.open(0)) me.exit();
recorder.create(480,120);
now => time started;
while(now-started < 10::second) {
    while(input.recv(msg)) recorder.appendMessage((now-started)/second,msg.data1,msg.data2,msg.data3);
    1::ms => now;
}
recorder.save("recording.mid");
```

Native validation pending; automated tests use constructed MidiMsg objects, not physical devices.
