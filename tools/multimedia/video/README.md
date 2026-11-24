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

### Extract and convert audio

```chuck
@import "VideoIO";
<<< VideoIO.convertAudio("movie.mp4", "sound.wav", 0) >>>;
```

Decodes the selected zero-based audio track with FFmpeg and writes WAV, FLAC, AIFF (24-bit PCM), MP3 or Ogg Vorbis using libsndfile >= 1.2.0. Output format follows the lowercase extension. Preserves sample rate and channel count (1–8), subject to format/encoder restrictions; no resampling, mixing or explicit channel-layout remapping. Samples outside [-1,1] are clipped; PCM is quantized to 24 bits and compressed formats may be lossy. Decoded samples are written consecutively, without retaining original timestamps, gaps or language metadata. MP3 and Ogg support depends on libsndfile. No overwrite; ordinary failures remove incomplete outputs. Returns 1 on success, 0 on failure. Offline only. VideoIO now links libsndfile in addition to FFmpeg.

Eight new smoke checks including AAC decode and independent PCM comparisons are pending execution.

### Simultaneous video and audio conversion

```chuck
@import "VideoIO";
<<< VideoIO.transcodeAV("original.mkv", "converted.mp4", 1280, 720, "h264", 23, 128) >>>;
```

transcodeAV(source, destination, width, height, videoCodec, CRF, audioKbps) supports H.264/AAC in MP4 and VP9/Opus in WebM, using libx264/native AAC and libvpx-vp9/libopus respectively. All audio tracks are decoded and encoded at 48 kHz; nominal bitrate is 32–256 kbps per track. Only continuous mono/stereo tracks with stable sample format, rate and channel layout are accepted. Timestamp discontinuities or unsupported extra streams cause rejection; first audio timestamps anchor the resampled sample clock. Codec delay, padding and container rounding may affect exact duration. Video dimensions must be even, up to 8192 per axis; scaling stretches to the requested size. The existing transcode API still copies compatible audio unchanged. No overwrite; ordinary failure removes incomplete files. Offline only. libswresample is now required alongside the existing FFmpeg and libsndfile dependencies. Eleven additional smoke checks are planned when both encoder pairs are present, plus independent codec, duration and start-time checks. New code has not yet been compiled or executed.

### Keyframe-aligned trimming

```chuck
@import "VideoIO";
<<< VideoIO.trim("movie.mkv", "excerpt.mkv", 10.0, 25.0) >>>;
```

trim(source, destination, start, end) selects a time interval in seconds relative to the input container start. It copies packets without re-encoding and starts at the first video keyframe at or after start, strictly before end. Video packets with presentation timestamps before end are kept, so the last frame may extend beyond end. Complete audio packets within the actual interval are retained, with timestamps shifted by the common video anchor. Audio packet boundaries can leave short gaps near the edges. The current conservative implementation accepts exactly one video stream and compatible audio streams, requires strictly increasing equal PTS/DTS per stream, and rejects reordered video (e.g. many H.264 streams with B-frames), missing timestamps, empty output tracks and unsupported extra streams. Keyframe flags are not a guarantee of independent decoding in every open-GOP codec. Supported output containers: MKV, MP4, MOV, WebM. No overwrite; ordinary failures remove incomplete output. This is not frame-accurate or sample-accurate editing. Offline trusted local inputs only. Ten new smoke checks and decoded video/audio checks are included, pending execution.

### Concatenate two compatible videos

```chuck
@import "VideoIO";
<<< VideoIO.concat("first.mkv", "second.mkv", "joined.mkv") >>>;
```

concat(first, second, destination) copies packets into a new MKV, MP4, MOV or WebM file without encoding. Inputs must have one video stream and matching audio streams in the same stream order, matching codec parameters and extradata, video dimensions/frame rate and audio sample rate/channel layout. Requires positive packet durations, equal PTS/DTS, strictly increasing output timestamps, a known container start and a keyframe at each video segment start. Rejects explicit audio priming/padding and skip-sample packet side data. This conservative version is intended for compatible independently decodable clips (especially intra-frame video and PCM), not arbitrary compressed videos or B-frame streams. Codec metadata and keyframe flags alone cannot guarantee independent decoding for every codec. The second clip is shifted by the longest actual packet span of the first; shorter streams can leave gaps. Timestamp rounding is possible, and first-clip stream metadata is retained. No transitions, mixing, resizing or normalization. No overwrite; ordinary failures remove incomplete output. Returns 1 on success or 0 on failure. Eight new smoke checks and independent decoded RGB/PCM comparisons are pending execution.

### Exact frame-range trimming

```chuck
@import "VideoIO";
<<< VideoIO.trimFrames("movie.mkv", "excerpt.mkv", 120, 60) >>>;
```

trimFrames(source, destination, firstFrame, frameCount) decodes in presentation order and encodes exactly frameCount frames starting at the zero-based firstFrame index, irrespective of keyframe boundaries. It reuses the native transcoding pipeline and writes FFV1/BGR0 in MKV at the source dimensions, with the first selected timestamp shifted to zero and subsequent relative presentation timestamps retained. FFV1 is lossless relative to the converted BGR0 frames, not necessarily the source color representation. A complete requested range must exist; short inputs cause failure and cleanup. Only single-stream video inputs are accepted; audio and other streams are rejected rather than discarded. Missing or non-increasing decoded timestamps are rejected. No audio trimming, time-based frame selection, or exact last-frame duration guarantee. No overwrite. Offline only. Twelve new unconditional smoke checks plus one conditional H.264 reordered-frame check and independent decoded comparisons are pending execution.

### Synchronized frame and PCM audio trimming

```chuck
@import "VideoIO";
<<< VideoIO.trimAV("movie.mkv", "excerpt.mkv", 120, 60) >>>;
```

trimAV uses a first decoding pass to locate the presentation timestamps of the first selected frame and the next frame after the requested range (or the known final frame duration at EOF). It then re-encodes the exact video range as FFV1/BGR0 at source dimensions, copying the corresponding complete PCM S16LE sample frames from every audio track into MKV. Audio boundaries are rounded upward to the source sample grid. Original sample rates and channel counts (1–8) are preserved; timestamps are rebased, subject to muxer rounding. A continuous PCM sample clock is required, allowing one source timestamp tick of rounding. Tracks must cover the entire selected interval; gaps, unsupported codecs, subtitles or extra video streams cause failure rather than silent loss. The original PCM-only implementation is extended below. Video-only files are also supported. No overwrite; ordinary failures clean up incomplete output. The source must remain unchanged during both passes. Offline only. Nine new smoke checks and independent decoded RGB/PCM comparisons are pending execution.

### Compressed audio in synchronized trimming

The same trimAV API now attempts native decoding of non-S16LE audio, converting decoded samples to interleaved signed 16-bit PCM with libswresample, without changing the sample rate. PCM S16LE retains its existing direct packet path. All compatible audio tracks and their metadata are retained. Decoder output must have stable sample rate, sample format, channel layout and continuous presentation timestamps; the entire selected interval must exist. Codec priming, timestamp rounding, discontinuities, unsupported decoders or dynamic format changes may cause failure. Conversion to 16-bit PCM can quantize higher-precision audio. Six conditional smoke checks (AAC, MP3, Opus encoder fixtures) and independent PCM/RGB assertions are added; this extension is not yet compiled or tested.

### Normalized concatenation of heterogeneous video clips

```chuck
@import "VideoIO";
<<< VideoIO.concatNormalized("first.mp4", "second.webm", "joined.mkv", 1920, 1080, 30) >>>;
```

concatNormalized(firstClip, secondClip, destination, width, height, fps) decodes two single-stream video files, scales each to the requested dimensions (1–8192), and emits one continuous FFV1/BGR0 MKV at integer 1–120 fps. Each clip is independently anchored at its first decoded presentation timestamp. Frames are selected by holding the most recent input frame on the target time grid (duplication or dropping, not interpolation); each clip duration rounds upward to a whole output frame. Requires strictly increasing decoded timestamps and a positive final frame duration. Maximum combined output: one million frames. Aspect ratio is stretched, with square output pixels; rotation and color metadata are not normalized. Additional streams, including audio, are rejected rather than discarded. No intermediate files or external commands, no overwrite, incomplete output removed on ordinary failure. Eleven new smoke checks plus RGB and timestamp comparisons are pending execution. Audio normalization is a separate future extension.

### Normalized audiovisual concatenation

```chuck
@import "VideoIO";
<<< VideoIO.concatAV("first.mp4", "second.webm", "joined.mkv", 1920, 1080, 30, 48000, 2) >>>;
```

concatAV extends normalized video concatenation with a single PCM S16LE audio output at a requested sample rate (8000–96000 Hz) and mono or stereo channel count. Each input may contain one video and zero or one audio stream; additional streams are rejected. Audio is decoded and resampled with libswresample, anchored to the first video timestamp, trimmed or silence-padded to the rounded video duration. Missing audio becomes silence. Stable decoded formats and continuous timestamps are required. Audio is processed in a separate pass over each source, so inputs must remain unchanged and the muxer can buffer substantial video data (not suitable for unbounded clips or real-time use). The existing video-only API is unchanged. Seven smoke checks plus independent PCM and RGB checks are pending compilation and execution.

### Linear audiovisual crossfade

```chuck
@import "VideoIO";
<<< VideoIO.crossfade("first.mp4", "second.webm", "transition.mkv",
                     1920, 1080, 30, 48000, 2, 2.0) >>>;
```

crossfade uses the same normalization as concatAV, overlapping the tail of the first clip and the head of the second clip. The requested duration in seconds rounds to the nearest whole output frame; at least two and at most 1200 frames are required. Both clips must contain at least that many normalized frames. The target audio sample rate must be divisible by the target FPS for exact sample-grid alignment. Linear BGR byte blending and signed PCM sample blending use endpoint-inclusive ramps; no gamma correction or equal-power curve. Output duration is the sum of normalized clip durations minus the overlap. Missing audio is silence; only zero or one audio track per source is supported. The retained video tail is capped at 256 MiB and captured normalized audio at 128 MiB per clip; the muxer may additionally buffer encoded video. Offline only, no overwrite, ordinary failure removes incomplete output. Eight new smoke checks and independent PCM/RGB comparisons await execution.

### Selectable audiovisual transitions

```chuck
@import "VideoIO";
<<< VideoIO.transition("first.mp4", "second.webm", "transition.mkv",
                      1280, 720, 30, 48000, 2, 1.0, "wipeLeft", "equalPower") >>>;
```

transition has the same parameters and constraints as crossfade, followed by a visual mode and an audio curve. Visual modes: "linear" (existing dissolve), "wipeLeft" (new image revealed from the left edge toward the right), "wipeDown" (top toward bottom), and "black" (first image fades to black, then second fades in; exact black midpoint when the overlap contains an odd number of frames). Audio curves: "linear" and "equalPower" (cosine/sine gains with saturation to signed 16-bit PCM to avoid overflow; correlated loud signals can clip). Unknown modes are rejected. Existing crossfade behavior is unchanged. Eight additional smoke checks and independent pixel/sample comparisons are pending execution, along with the eight checks from the preceding crossfade change.

### Picture-in-picture and opacity compositing

```chuck
@import "VideoIO";
<<< VideoIO.overlay("main.mp4", "inset.webm", "composite.mkv",
                    1280, 720, 30, 900, 20, 360, 202, 0.75, 48000, 2) >>>;
```

overlay(baseClip, layerClip, destination, width, height, fps, x, y, layerWidth, layerHeight, opacity, sampleRate, channels) normalizes each video independently to the requested pixel dimensions and samples both on the output time grid. Coordinates are measured in pixels from the upper-left corner. The layer rectangle must fit within the canvas. Global opacity is 0–1; RGB byte blending is linear without gamma correction or source alpha. Both clips begin at their first decoded presentation timestamp; the inset disappears at its end and the base controls output duration. Output is FFV1/BGR0 and PCM S16LE in MKV. Only base audio is normalized (8000–96000 Hz, mono/stereo); layer audio is deliberately ignored and missing base audio produces silence. Each input accepts one video plus at most one audio track. Video reading uses one-frame lookahead rather than storing whole decoded clips, but the separate audio pass can still cause substantial muxer buffering. Maximum one million output frames; known positive final frame durations and increasing timestamps required. No overwrite; ordinary failure removes incomplete output. Nine smoke checks plus independent RGB/PCM comparisons await execution.

### Chroma key and audio mixing

```chuck
@import "VideoIO";
<<< VideoIO.composite("main.mp4", "green-screen.webm", "composite.mkv",
                      1280, 720, 30, 0, 0, 1280, 720, 1.0, 48000, 2,
                      65280, 0.15, 0.1, 1.0, 0.5) >>>;
```

composite extends overlay with five parameters: keyRGB (packed 0xRRGGBB integer, or -1 to disable), tolerance, softness, baseGain and layerGain. The normalized Euclidean RGB distance is measured after scaling. Pixels within tolerance become transparent; the next softness interval linearly restores opacity. Tolerance and softness are nonnegative and their sum must not exceed one. This is a basic RGB key, without spill suppression or source alpha. Audio gains are linear amplitudes in [0,4]; normalized signed PCM samples are summed, rounded and saturated. The layer audio is limited to its visible video duration; missing audio is silence. The base controls output duration. Each captured PCM buffer is limited to 128 MiB; muxer buffering remains additional. The original overlay API is unchanged. Nine new smoke checks plus independent pixel and PCM comparisons await execution.

### Static images, logos and watermarks

```chuck
@import "VideoIO";
<<< VideoIO.watermark("main.mp4", "logo.png", "branded.mkv",
                      1280, 720, 30, 1000, 20, 240, 120, 0.8, 48000, 2) >>>;
```

watermark uses the same positional arguments as overlay, but the second input is a single static image decoded using FFmpeg (PNG, JPEG or BMP codec). The image is scaled once to the specified rectangle and displayed for the entire base video. Per-pixel alpha is multiplied by global opacity, preserving transparency in RGBA PNGs. Alpha blending uses straight RGB values and bilinear scaling, without gamma correction or premultiplied-alpha edge filtering. The base video controls duration; its audio is normalized to the requested sample rate and channel count, or silence if absent. Output is FFV1/BGR0 and PCM S16LE in MKV. Animated or multi-frame inputs are rejected. Existing overlay and composite APIs are unchanged. Nine smoke checks and independent RGB/PCM assertions await execution.

### Ordered multilayer compositing

```chuck
@import "VideoIO";
<<< VideoIO.multilayer("main.mp4", "layers.tsv", "composite.mkv",
                       1280, 720, 30, 48000, 2) >>>;
```

The UTF-8 text manifest has seven tab-separated fields per line: type (image or video), local path, x, y, width, height, opacity. Paths are resolved relative to the process working directory, not the manifest; spaces are allowed, tabs/newlines are not. Empty lines and lines beginning with # are ignored. CRLF and LF are supported; use no UTF-8 BOM. First row is drawn first, last row on top. Maximum eight layers and 64 KiB manifest; the sum of target layer pixel buffers is limited to 256 MiB, excluding decoder and muxer buffers. Image layers retain alpha and are displayed throughout; video layers start at their first presentation frame and disappear at EOF. The base controls duration and audio, with other audio ignored. One output encoding pass and no temporary files. No overwrite. Output FFV1/PCM MKV. Chroma key, per-layer audio mixing, and temporal keyframes are not yet exposed in the multilayer API. Eight smoke checks and independent RGB/PCM assertions await execution.

### Timed text titles

```chuck
@import "VideoIO";
<<< VideoIO.title("main.mp4", "titled.mkv", "ChucK Multimedia",
                  "Sans Bold", 48, 16777215, 100, 100, 0.0, 5.0) >>>;
```

title renders plain UTF-8 text once using Pango/Cairo, at the given pixel size, packed RGB color (0xRRGGBB), and top-left pixel coordinates. Text wraps at the remaining canvas width and clips at the bottom edge. The title is shown at normalized frame timestamps in [beginSeconds,endSeconds); negative or empty intervals are rejected. Text is antialiased, with no background or markup. The source dimensions are retained and the guessed source frame rate rounded to an integer in 1–120 FPS. Output is FFV1/PCM MKV; audio is normalized to the source sample rate if 8–96 kHz (otherwise 48 kHz), mono for mono sources, stereo otherwise, and silence if absent. Existing single-video/at-most-one-audio source constraints apply. Fonts depend on the installed system fonts and Pango fallback. No overwrite. Nine smoke checks and independent timing, color and PCM comparisons await execution. PangoCairo is now an explicit VideoIO build dependency.

### Burn-in subtitles

```chuck
@import "VideoIO";
<<< VideoIO.burnSubtitles("film.mp4", "captions.srt", "subtitled.mkv",
                          "Sans Bold", 36, 16777215, 40) >>>;
```

Uses the same strict plain UTF-8 SRT/WebVTT parser as SubtitleIO. The final argument is the bottom margin in pixels. Cues are active on the half-open millisecond interval [start,end), evaluated on the normalized output frame grid. Simultaneous cues are joined by newlines in file order, horizontally centered, bottom aligned in the available region, with a black outline and antialiasing. Text wraps and clips when it exceeds the available canvas. Combined active text is limited to 16384 bytes. Glyphs are rendered again only when the visible text changes. Output resolution, integer FPS selection and audio normalization follow title(); one FFV1/PCM MKV encoding pass, no temporary images, no overwrite. Nine additional smoke checks and independent frame/audio comparisons await execution.

### Animated image overlays

```chuck
@import "VideoIO";
<<< VideoIO.animateOverlay("base.mp4","overlay.png","animation.tsv","result.mkv",1280,720,30) >>>;
```

The whitespace-separated animation manifest has four columns: parameter, time in seconds, value, outgoing interpolation. All six scalar curves are required; rows may be interleaved but times within each curve must strictly increase. For example:

```text
x 0 100 smooth
x 5 800 linear
y 0 200 linear
width 0 320 linear
height 0 180 linear
rotation 0 0 linear
rotation 5 360 linear
opacity 0 0 linear
opacity 1 1 step
opacity 4 1 linear
opacity 5 0 linear
```

Uses the shared KeyframeIO engine, including endpoint holds and five interpolation modes. X/Y describe the unrotated rectangle's top-left in output pixels; width/height are in [1,8192], x/y in [-32768,32768], rotation is clockwise degrees around rectangle center (absolute limit 1e9), opacity in [0,1]. Pixel-center inverse mapping uses nearest-neighbor sampling of the original static PNG/JPEG/BMP, preserving per-pixel alpha; off-canvas pixels are clipped. No video layer animation or high-quality resampling yet. The manifest is limited to 8 MiB and 100000 keys per curve. Output FFV1/PCM MKV, base determines duration, audio normalized as for title(), no overwrite. Eleven smoke checks plus independent exact pixel/PCM comparisons await execution.

## Persistent VideoClip

```chuck
@import "VideoIO";
VideoClip clip;
if(clip.load("input.mkv")) {
    do {
        <<< clip.frameIndex(), clip.timeSeconds() >>>;
        0.8 => clip.red;
        clip.saveFrame("frame" + clip.frameIndex() + ".ppm");
    } while(clip.next() == 1);
}
```

VideoClip retains a native decoder and two decoded frames (current and lookahead), rather than loading the full video into memory. load(path) selects the first decoded frame, resets controls, and replaces existing state only on success. next() returns 1 for a new frame, 0 at end (last frame remains available), -1 on error. A decoder error invalidates the loaded state. eof() means the current frame is the last decoded frame. frameIndex() is zero-based; timeSeconds() is the current presentation timestamp relative to the first frame. Both return -1 when unloaded. width()/height() report current decoded dimensions, or zero when unloaded. clear() releases the decoder, reset() resets controls without rewinding. Reload to rewind. One video stream and at most one audio stream are accepted, with strictly increasing known presentation timestamps. Audio is not exposed by this class.

red/green/blue float getter/setters [0,4] default 1 and brightness [-1,1] default 0 affect only exported pixels. Invalid setters return NaN and preserve previous values. saveFrame(path) writes RGB8 P6 PPM at source dimensions; saveFrame(path,width,height) stretches with bilinear resampling. Dimensions 1–8192, maximum 16 megapixels. Each frame uses original decoded pixels; color adjustments do not accumulate. Output is always PPM regardless of filename extension, exclusive creation. No random seek, playback UGen, live display, or incremental video encoder yet. Synchronous offline operations, no real-time guarantee. Numeric setters can be driven by KeyframeCurve without a native dependency. 37 new smoke checks plus exact PPM pixel comparisons await execution.

## Incremental VideoWriter

```chuck
@import "VideoIO";
VideoWriter writer;
int pixels[16*16*4];
for(0 => int i; i<256; i++) 255 => pixels[i*4+3];
if(writer.open("generated.mkv",16,16,30)) {
    writer.appendFrame(pixels);
    writer.finish();
}
```

VideoWriter.open(path,width,height,fps) reserves a new lowercase .mkv file and starts a FFV1/BGR0 encoder. Dimensions 1–8192, at most 16777216 pixels, integer fps 1–120. appendFrame(int[]) accepts exactly width*height*4 row-major RGBA8 bytes; alpha must be 255 (no silent loss of transparency). Appends at the next constant-frame-rate timestamp, with a limit of one million frames. Invalid input returns 0 without consuming a frame; fatal encoding errors close the writer and remove incomplete output. frames() reports appended frames while active, active() reports open state. finish() flushes and finalizes; requires at least one frame, closes even on failure. abort() or destruction before finish deletes incomplete output. Existing files are never overwritten. No audio track or variable timestamps yet; explicit finish is mandatory. Multiple independent writers can coexist. Compatible with RasterImage.exportPixels and VideoClip.exportPixels. Synchronous offline operations, no hard realtime guarantee. New smoke checks and independent decoded RGB comparisons await Windows testing.
