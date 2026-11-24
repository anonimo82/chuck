# VideoIO (experimental)
Optional native Chugin using FFmpeg libraries. Linux: install libavformat-dev, libavcodec-dev, libavutil-dev, libswscale-dev, pkg-config and a C++17 compiler. macOS: install ffmpeg and pkg-config. Build with make in this directory. Windows x64 build and initial five video smoke checks passed with ChucK 1.5.5.9; see ../WINDOWS-TEST.md.

API:
- VideoIO.width(path), VideoIO.height(path): coded dimensions or -1.
- VideoIO.seconds(path): container duration in seconds or -1.
- VideoIO.fps(path): estimated frame rate or -1 (variable-frame-rate files may differ).
- VideoIO.firstFrame(source, destination): decode first available frame to RGB PPM (.ppm); returns 1 on success, 0 on failure. Output must not exist. Maximum dimensions 8192 x 8192.

Input containers depend on the FFmpeg build (e.g. MP4, AVI, MKV, WebM, MOV, OGV, MPEG). Decoder support depends on codec availability. Native remuxing is available; general-purpose video encoding, seeking, audio extraction and ChuGL texture integration are not yet implemented. The existing ../mediaio.py CLI remains available for offline video conversion. Use trusted local inputs only; FFmpeg may open referenced resources in complex containers. These APIs block and are not real-time safe.

### Native container export

```chuck
@import "VideoIO";
<<< VideoIO.remux("input.mp4", "output.mkv") >>>;
```

Returns 1 on success, 0 on failure. Supported destination extensions (lowercase): .mkv, .mp4, .mov, .avi, .webm. The destination must not exist. Audio/video streams are copied without transcoding; the destination muxer must support their codecs. Other stream types are rejected. Chapters and arbitrary container-specific metadata are not guaranteed to be preserved. Partial outputs are removed on ordinary errors; abrupt process termination may leave a file. Only trusted local inputs; not real-time safe.

New remux checks have not yet been run on Windows or Linux.

### Create video from RGB images

```chuck
@import "VideoIO";
<<< VideoIO.encodeSequence("frame", "animation.mkv", 120, 30) >>>;
```

Reads frame000000.ppm through frame000119.ppm in order and encodes a silent FFV1 video in Matroska. Each input must be a P6 binary RGB PPM with maxval 255, identical dimensions (1–8192 pixels per axis), a single whitespace delimiter before pixel bytes, and no trailing bytes. Count: 1–100000; integer frame rate: 1–120 fps. The output must end in .mkv and must not already exist. Returns 1 on success, 0 on failure; partial outputs are removed on ordinary errors. RGB values are preserved without lossy compression. The encoder must be available in the linked FFmpeg build. This is offline, blocking processing; audio and other output codecs are not yet supported.

Ten new smoke checks and a byte-for-byte decoded RGB comparison are included; execution pending.

### Compressed video export

```chuck
@import "VideoIO";
if(VideoIO.encoderAvailable("h264"))
    <<< VideoIO.encodeSequence("frame", "animation.mp4", 120, 30, "h264", 23) >>>;
if(VideoIO.encoderAvailable("vp9"))
    <<< VideoIO.encodeSequence("frame", "animation.webm", 120, 30, "vp9", 32) >>>;
```

The six-argument overload accepts h264 (libx264/MP4), vp9 (libvpx-vp9/WebM), or ffv1 (MKV, quality 0 only). CRF quality ranges from 0 to 51 for compressed modes; lower values generally produce higher quality and larger files. CRF scales are not directly comparable between encoders. H.264 and VP9 use YUV420P, require even dimensions and do not guarantee exact RGB preservation even at CRF 0. The existing four-argument FFV1 API is unchanged. encoderAvailable reports whether the named encoder exists in the linked FFmpeg build, not whether a particular input can be encoded. All modes remain silent, offline, and blocking. Tests use FFmpeg and ffprobe CLI solely for independent verification. New checks remain unexecuted.

### Combine video and audio

```chuck
@import "VideoIO";
<<< VideoIO.muxAudio("animation.mkv", "sound.wav", "movie.mkv") >>>;
```

Selects the best video stream from the first input and best audio stream from the second input using FFmpeg. Existing audio in the video input is not copied; additional streams are ignored. Copies compressed packets without re-encoding. Supported output containers: MKV, MP4, MOV and WebM, subject to codec compatibility (e.g. FFV1 and PCM in MKV; H.264 and AAC in MP4; VP9 and Opus in WebM). Returns 1 on success, 0 on failure. Requires a new destination file. Ordinary failures remove incomplete outputs. Packet timestamps are preserved subject to muxer time-base rounding and container rules; no independent start-time normalization, trimming, padding, resampling or synchronization correction is performed. If tracks differ in duration, both are retained in full. Intended for trusted local files and offline processing.

Eight new smoke checks and independent exact RGB/PCM decode comparisons are included; not yet executed.

### Transcode and resize an existing video

```chuck
@import "VideoIO";
<<< VideoIO.transcode("animation.mkv", "small.mp4", 640, 360, "h264", 23) >>>;
```

The six arguments select source, new destination, width, height, codec, and CRF quality. Codec/container pairs match encodeSequence: FFV1/MKV (quality 0), H.264/MP4 and VP9/WebM (quality 0–51). Dimensions must be 1–8192, and even for H.264 and VP9. The implementation decodes and scales frames directly in memory, preserving strictly increasing presentation timestamps subject to output muxer rounding. It stretches to the requested dimensions and outputs square pixels; no aspect-ratio correction, rotation handling, color management, or frame rate conversion is provided. FFV1 is lossless relative to its encoded BGR0 frames, not necessarily the original source pixels after scaling or color conversion. One video stream and any number of compatible audio streams are accepted. Audio is copied without re-encoding; incompatible audio, subtitles and additional video or data streams cause rejection rather than silent loss. No overwrite; partial outputs are removed on ordinary failures. Returns 1 on success, 0 on failure. Offline only.

Fourteen added smoke checks and independent decoded pixel checks are pending execution.

### Extract a sequence of frames

```chuck
@import "VideoIO";
VideoIO.extractSequence("movie.mkv", "frame", 0, 120) => int count;
if(count > 0)
    <<< VideoIO.encodeSequence("frame", "rebuilt.mkv", count, 30) >>>;
```

extractSequence(source, prefix, start, limit) decodes the selected video stream sequentially, skips the first start decoded frames (zero-based), then writes up to limit RGB8 P6 PPM images named prefix000000.ppm, prefix000001.ppm, etc. Existing files are never overwritten. Returns the actual number written (possibly zero at end of stream), or -1 on failure. On ordinary failure, it attempts to remove only files created by that invocation; a filesystem cleanup failure or process interruption can leave files behind. Limit: 1–100000; start: 0–2147483647; frame dimensions: up to 8192 per axis. Output directories must already exist. Audio and original timing metadata are not exported; reconstruction uses the caller's chosen constant frame rate. RGB conversion can reduce precision or discard alpha from the source. Offline only, with trusted local inputs.

Nine additional smoke checks, byte-level frame checks and lossless reconstruction comparison are included; execution pending.

### Transcoding with audio preservation

The existing transcode API now copies all compatible audio streams while decoding, resizing and encoding the video. Audio codec parameters, language metadata, dispositions and packet timestamps are copied, subject to destination container rules and timestamp rounding. Audio is not resampled, re-encoded, trimmed or independently synchronized. For example, FFV1/PCM into MKV is supported; PCM into WebM is rejected rather than discarded. Compatibility is checked conservatively using avformat_query_codec. Chapter and arbitrary container metadata preservation is not guaranteed. Tests for two independent PCM tracks and language tags are included but have not yet been executed.

### Extract individual audio tracks

```chuck
@import "VideoIO";
<<< VideoIO.audioTracks("movie.mkv") >>>;
<<< VideoIO.extractAudio("movie.mkv", "sound.mka", 0) >>>;
```

audioTracks returns the number of audio streams in a readable video, zero for a silent video, or -1 on failure. extractAudio selects a zero-based audio ordinal (not a container stream ID), copies its packets into a new audio-only file and returns 1 on success or 0 on failure. Supported lowercase extensions: .mka (Matroska audio), .wav, .flac, .mp3, .m4a, .ogg and .opus, provided the existing codec is compatible with the target muxer. M4A is limited to AAC/ALAC, and FLAC, MP3 and Opus extensions require their corresponding codecs. No decoding, mixing, resampling or audio transcoding is performed. Stream metadata and timestamps are copied subject to container limitations; WAV and other formats cannot necessarily preserve original timing or language metadata. The destination must not exist. Ordinary failures remove incomplete outputs. Offline trusted local files only.

Twelve new smoke checks and independent sample/metadata comparisons are included, pending execution.
