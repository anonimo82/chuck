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
