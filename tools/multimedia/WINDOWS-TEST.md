# Windows x64 diagnostic build

Download this branch as ZIP from GitHub, extract it to a writable local folder, and open PowerShell in the extracted repository root. Review the scripts first. Run:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\multimedia\run-windows.ps1

The execution policy option applies only to this process. The script installs or updates MSYS2 and its UCRT64 dependencies, builds the six Chugins with CMake, and runs the multimedia smoke checks if a compatible x64 ChucK executable is available. It may require an installer approval. If MSYS2 requests a restart, rerun the same command. Existing MSYS2 installations will be updated.

If no ChucK binary is found, the script tries the installed Visual Studio MSBuild toolchain (v142 and Windows SDK required). Otherwise supply a compatible existing binary using -ChuckExe "C:\path\to\chuck.exe". Visual Studio is not installed automatically. No GitHub credentials or uploads are required.

Send setup-windows.log and build-and-test.log from tools/multimedia if present (review for personal paths before sharing). Output binaries are in build/multimedia-windows/install/chugins. This procedure is experimental; the build-and-test stage has passed on Windows x64 (see below). Dependency revisions are not pinned; the MIDI commit is recorded in the log.

## Verified Windows result — 2026-11-24

User-executed build-and-test.ps1 against source revision e4726eb28f6f9036a2414f572fa7a1825d84e651 (PR #18): ChucK 1.5.5.9 x64, host API 10.2, MSYS2 UCRT64, ImageMagick 7.1.2, FFmpeg libavcodec/libavformat major 63, libvpx 1.17.0, midifile revision 98917df5b1bf0d6e8d4c0e5fff86d6b05343e793. All six modules loaded. All 99 smoke checks passed: RasterIO 8, AudioIO 7, VectorIO 4, ModelIO 6, MidiFileIO 5, VideoIO 69. H.264 and VP9 encoders actually ran, and independent binary/pixel/audio/metadata assertions completed. Final script message: All multimedia smoke tests passed. This is a smoke-test result, not exhaustive format compatibility or Linux/macOS verification.

## Extended Windows result — 2026-11-24

User-executed build-and-test.ps1 against source revision 9ab46a4d759c0223de273ac4b969ad16e4945258 (PRs #19–#23 and reserved parameter name fix). All six Chugins loaded on ChucK 1.5.5.9 x64, API 10.2, MSYS2 UCRT64. FFmpeg libavformat 63.16.100, libavcodec 63.24.100, libavutil 61.19.100, libswscale 10.5.100, libswresample 7.6.100. All 148 smoke checks passed: RasterIO 8, VectorIO 4, ModelIO 6, AudioIO 7, MidiFileIO 5, VideoIO 118. Both H.264/AAC and VP9/Opus audiovisual conversion paths executed; independent sample, pixel, metadata, duration and start-time assertions completed. Audio extraction/conversion, keyframe trimming and compatible concatenation passed their fixtures. An AAC discarded-sample timestamp warning was nonfatal. Final script message: All multimedia smoke tests passed. This is not exhaustive codec, timing, platform or production-readiness validation; Linux/macOS and additional formats remain to be tested.

## Frame-accurate trimming Windows result — 2026-11-24

User-executed build-and-test.ps1 against source revision ad9fa456ce67c5a03603f4cdec0427ea6719170e (PR #24). ChucK 1.5.5.9 x64, API 10.2, MSYS2 UCRT64. All six Chugins loaded and all 161 smoke checks passed: RasterIO 8, VectorIO 4, ModelIO 6, AudioIO 7, MidiFileIO 5, VideoIO 131. All thirteen new frame-range checks ran, including the conditional H.264 reordered-frame fixture. Independent RGB and timestamp comparisons completed. Final script message: All multimedia smoke tests passed. Frame-accurate trimming currently accepts video-only inputs and outputs FFV1/MKV; sample-accurate audiovisual trimming and broader platform/codec validation remain outstanding.

## Synchronized audiovisual trimming Windows result — 2026-11-24

User-executed build-and-test.ps1 against source revision ee6474e8d8f134b936cd739a0809c25d4a45b1c9 (PRs #25–#26). ChucK 1.5.5.9 x64, API 10.2, MSYS2 UCRT64. All six Chugins loaded and all 176 smoke checks passed: RasterIO 8, VectorIO 4, ModelIO 6, AudioIO 7, MidiFileIO 5, VideoIO 146. All 15 new checks ran, including synchronized PCM trimming and AAC, MP3 and Opus decoding fixtures. Independent RGB, PCM sample and metadata comparisons completed. Final script message: All multimedia smoke tests passed. This validates the tested continuous audio fixtures; arbitrary codec priming, discontinuities, channel layouts, timestamp rounding and other platforms still require broader validation.

## Heterogeneous audiovisual concatenation Windows result — 2026-11-24

User-executed build-and-test.ps1 against source revision 9f9f3efe337b734cc7d098f6629a026f7abbe1d2 (PRs #27–#28). ChucK 1.5.5.9 x64, API 10.2, MSYS2 UCRT64. All six Chugins loaded and all 194 smoke checks passed: RasterIO 8, VectorIO 4, ModelIO 6, AudioIO 7, MidiFileIO 5, VideoIO 164. All 18 new checks ran (11 normalized video concatenation and 7 audiovisual concatenation). Independent RGB, timestamp, PCM, sample-rate/channel-count and silence-padding assertions completed. Final script message: All multimedia smoke tests passed. Validation is limited to tested fixtures; multiple audio tracks, long-clip memory usage, broader codec combinations and other platforms remain outstanding.

## Audiovisual transitions Windows result — 2026-11-24

User-executed build-and-test.ps1 against source revision d32d24cb389d10d6b3da85c964ab7467ba8b5640 (PRs #29–#30). ChucK 1.5.5.9 x64, API 10.2, MSYS2 UCRT64. All six Chugins loaded and all 210 smoke checks passed: RasterIO 8, VectorIO 4, ModelIO 6, AudioIO 7, MidiFileIO 5, VideoIO 180. All 16 new checks ran (8 linear crossfade and 8 selectable transitions), including independent RGB and PCM comparisons and linear-mode compatibility. Final script message: All multimedia smoke tests passed. Validation remains limited to tested fixtures; memory limits, integer audio/video grid constraints, broader media combinations and other platforms remain outstanding.

## Picture-in-picture and chroma-key compositing Windows result — 2026-11-24

User-executed build-and-test.ps1 against source revision 20850643135db803ebc46e920b4bc8634f2d9659 (PRs #31–#32). ChucK 1.5.5.9 x64, API 10.2, MSYS2 UCRT64. All six Chugins loaded and all 228 smoke checks passed: RasterIO 8, VectorIO 4, ModelIO 6, AudioIO 7, MidiFileIO 5, VideoIO 198. All 18 new checks ran (9 picture-in-picture and 9 chroma-key/audio mixing), including independent RGB and PCM assertions. Final script message: All multimedia smoke tests passed. Validation remains limited to tested fixtures; soft-key edges, more varied media, long-clip memory usage, multiple tracks and other platforms require further validation.

## Static watermarks and multilayer compositing Windows result — 2026-11-24

User-executed build-and-test.ps1 against source revision b3741d10ad6f7a33cf4e779696a3991e991fce8a (PRs #33–#34). ChucK 1.5.5.9 x64, API 10.2, MSYS2 UCRT64. All six Chugins loaded and all 245 smoke checks passed: RasterIO 8, VectorIO 4, ModelIO 6, AudioIO 7, MidiFileIO 5, VideoIO 215. All 17 new checks ran (9 static watermark and 8 ordered multilayer checks), including independent RGB and PCM assertions. Final script message: All multimedia smoke tests passed. Validation remains limited to tested fixtures; JPEG/BMP watermark inputs, alpha-edge quality, long-clip memory usage, maximum layer count in successful compositions, and other platforms require further testing.

## Timed titles and SubtitleIO Windows result — 2026-11-24

User-executed build-and-test.ps1 with multimedia files from ca10688264462817bb12a87006e596bcfbbdcdae (PRs #35–#36), test runner corrected by e3672f54a5c438e0c6e9797da271aed934eb9d6e and 3dc7e5bdf030ca9f514922579dd2b3dcdf1e4335. ChucK 1.5.5.9 x64, API 10.2, MSYS2 UCRT64, PangoCairo 1.58.2, GLib 2.90.1. All seven Chugins loaded; all 274 smoke checks passed: RasterIO 8, VectorIO 4, ModelIO 6, AudioIO 7, MidiFileIO 5, VideoIO 224, SubtitleIO 20. Nine title checks and twenty subtitle checks passed, including independent pixel/timing/PCM and canonical subtitle byte comparisons. The two test-runner fixes explicitly write UTF-8 scripts and compare native 2 FPS title output to the four-frame fixture rather than the eight-frame 4 FPS overlay reference. No C++ changes were needed for these fixes. Final script message: All multimedia smoke tests passed. Validation is limited to tested fixtures and Windows; richer subtitle syntax, additional fonts and platforms remain unverified.

## Subtitle burn-in and shared keyframe engine Windows result — 2026-11-24

User-executed build-and-test.ps1 with multimedia source revision 4dfa25c0f4f386d1301bc5071ba2b07943ab6f16 (PRs #37–#38), ChucK 1.5.5.9 x64, API 10.2, MSYS2 UCRT64. All eight Chugins loaded successfully. All 307 smoke checks passed: RasterIO 8, VectorIO 4, ModelIO 6, AudioIO 7, MidiFileIO 5, VideoIO 233, SubtitleIO 20, KeyframeIO 24. All nine new subtitle burn-in checks and independent pixel/timing/margin/PCM assertions passed, as did all 24 keyframe interpolation and validation checks. Final script message: All multimedia smoke tests passed. No fixes were needed. Validation is limited to tested fixtures and Windows; additional platforms, complex text layouts, large curves and media-specific keyframe integrations remain unverified.

## Animated overlays, raster sequences and persistent objects — Windows result, 2026-11-24

User-executed build-and-test.ps1 with multimedia source revision babf802453e616ea2beaac8025478c2e207632e4 (PRs #39–#42), ChucK 1.5.5.9 x64, API 10.2, MSYS2 UCRT64. All eight Chugins loaded successfully. All 389 smoke checks passed: RasterIO 50, VectorIO 4, ModelIO 6, AudioIO 7, MidiFileIO 5, VideoIO 244, SubtitleIO 20, KeyframeIO 53. All 82 added checks passed (animated video overlays 11, raster sequences 9, persistent KeyframeCurve 29, persistent RasterImage 33 including direct numeric composition with KeyframeCurve). Independent RGB/RGBA/PCM assertions and output rollback checks completed successfully. Final script message: All multimedia smoke tests passed. No fixes required. Results cover tested Windows fixtures; other platforms, long-running memory/lifetime stress tests and real-time safety remain unverified.

## Complete persistent multimedia object suite — Windows result, 2026-11-24

User-executed build-and-test.ps1 on ChucK 1.5.5.9 Windows x64, API 10.2, MSYS2 UCRT64. Multimedia C++ source revision 99feba34023abe4fe3482bb688aeef991dab32b4 (PRs #43–#48), test runner revision 9570560d163a35b9bbf0db7e391848d6177513bd. All eight Chugins compiled and loaded successfully. All 597 smoke checks passed: KeyframeIO 53, RasterIO 50, VectorIO 34, ModelIO 36, AudioIO 40, MidiFileIO 40, VideoIO 280, SubtitleIO 64. All 208 additional persistent-object checks passed, including independent RGBA, OBJ world-space geometry, PCM, SMF event, PPM and subtitle file assertions and KeyframeCurve composition. Two test-runner-only fixes corrected expected check counts and the missing AudioIO mapping; no C++ changes were required. Final script message: All multimedia smoke tests passed. This validates the tested Windows fixtures, not other platforms, exhaustive format compatibility, long-running memory stress or hard real-time safety.

## 2026-11-25 — cumulative PR #49–58 validation

User-provided Windows transcript confirms 986 PASS checks, zero FAIL, and `All multimedia smoke tests passed.` All eight multimedia chugins compiled, installed and probed OK (API 10.2). Source/test snapshot `684784c3d293e55926afed170ba32ea1aa4de749`; build script fix `ce68f03b44a587bf37a6ec559a05cb9d21f7f5d9`. Standalone CMake 4.5.1 at `C:\Program Files\CMake\bin\cmake.exe`, GNU 16.2.0 UCRT64, ChucK 1.5.6.0-dev (chai), Windows 64-bit. MSYS2 CMake had terminated with STATUS_ILLEGAL_INSTRUCTION (0xC000001D); clean configuration using standalone CMake succeeded. Previous 597 checks plus 389 new checks cover memory exchange, incremental video writing, synchronized AV writing, audio extraction, raster/vector/3D/MIDI synthesis and audio editing/DSP. Independent output assertions completed. This is a smoke-test baseline, not exhaustive format, stress, realtime or cross-platform validation.
