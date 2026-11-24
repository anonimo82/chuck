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
