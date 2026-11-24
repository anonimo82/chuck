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
