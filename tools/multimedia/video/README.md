# VideoIO (experimental)
Optional native Chugin using FFmpeg libraries. Linux: install libavformat-dev, libavcodec-dev, libavutil-dev, libswscale-dev, pkg-config and a C++17 compiler. macOS: install ffmpeg and pkg-config. Build with make in this directory. Windows build pending.

API:
- VideoIO.width(path), VideoIO.height(path): coded dimensions or -1.
- VideoIO.seconds(path): container duration in seconds or -1.
- VideoIO.fps(path): estimated frame rate or -1 (variable-frame-rate files may differ).
- VideoIO.firstFrame(source, destination): decode first available frame to RGB PPM (.ppm); returns 1 on success, 0 on failure. Output must not exist. Maximum dimensions 8192 x 8192.

Input containers depend on the FFmpeg build (e.g. MP4, AVI, MKV, WebM, MOV, OGV, MPEG). Decoder support depends on codec availability. No video encoding, remuxing, seeking, audio extraction or ChuGL texture integration in this initial version. The existing ../mediaio.py CLI remains available for offline video conversion. Use trusted local inputs only; FFmpeg may open referenced resources in complex containers. These APIs block and are not real-time safe.

Build and runtime testing pending.
