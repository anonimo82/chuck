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
