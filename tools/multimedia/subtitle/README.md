# SubtitleIO

Optional offline Chugin for plain-text SRT and WebVTT.

```chuck
@import "SubtitleIO";
<<< SubtitleIO.count("captions.srt") >>>;
<<< SubtitleIO.startTime("captions.srt", 0) >>>;
<<< SubtitleIO.endTime("captions.srt", 0) >>>;
<<< SubtitleIO.text("captions.srt", 0) >>>;
<<< SubtitleIO.convert("captions.srt", "captions.vtt") >>>;
<<< SubtitleIO.writeCue("intro.srt", "Welcome", 0, 3000) >>>;
```

Indices are zero-based; startTime/endTime return seconds, writeCue takes integer milliseconds. count and time queries return -1 on error; text returns an empty string. convert and writeCue return 1 on success, 0 otherwise. No overwrite. UTF-8 with optional BOM, LF or CRLF, multiline text, millisecond precision and overlapping cues in nondecreasing start-time order. Conversion renumbers SRT cues and emits canonical timestamps. Limits: 16 MiB, 100000 cues, 16384 bytes per cue, timestamps under 100 hours. Lowercase .srt/.vtt extensions only. This is intentionally a strict plain-text subset: no markup, entities, cue IDs/settings in VTT, NOTE/STYLE blocks, or empty cues. Unsupported input is rejected rather than silently losing formatting. writeCue creates a single-cue file; multi-cue output via convert. Queries reparse the file each call. Embedded subtitle tracks and burn-in are future work.

Build via the multimedia CMake project (GLib already used by the other modules). Twenty smoke checks and independent byte comparisons await execution.

## Persistent SubtitleTrack

```chuck
@import "SubtitleIO";
SubtitleTrack captions;
captions.load("input.srt");
0.5 => captions.offset;
1.1 => captions.timeScale;
<<< captions.textAt(2.0) >>>;
captions.save("output.vtt");
```

Each instance retains independent plain UTF-8 cues. load(path) replaces data and resets controls only on success. count(), startTime(index), endTime(index), text(index), add(beginMs,endMs,content), set(index,beginMs,endMs,content), remove(index), clear(), reset(), save(path) are available. Original cue queries return seconds; editing accepts integer milliseconds. Cues must have positive duration, nondecreasing start times, plain UTF-8 text, at most 16384 bytes each and at most 100000 cues. Overlaps are allowed. Invalid edits return 0. offset() is seconds, default 0, range +/-360000; timeScale() is a positive duration multiplier, default 1, range 0.001–1000. Transformed endpoints are round(originalMs * timeScale + offset * 1000). Invalid setters preserve previous values and return NaN. save and textAt(timeSeconds) apply these controls without changing stored cues; textAt joins simultaneous active cues with newlines, using half-open intervals. Invalid transformed intervals cause save failure or an empty textAt result. No automatic clipping of negative times. Output SRT or plain WebVTT uses exclusive creation. No styling or embedded subtitle streams; synchronous offline operations. No KeyframeIO dependency. 44 new smoke checks and exact independent file comparisons await execution.
