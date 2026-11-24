# Windows x64 diagnostic build

Download this branch as ZIP from GitHub, extract it to a writable local folder, and open PowerShell in the extracted repository root. Review the scripts first. Run:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\multimedia\run-windows.ps1

The execution policy option applies only to this process. The script installs or updates MSYS2 and its UCRT64 dependencies, builds the six Chugins with CMake, and runs the multimedia smoke checks if a compatible x64 ChucK executable is available. It may require an installer approval. If MSYS2 requests a restart, rerun the same command. Existing MSYS2 installations will be updated.

If no ChucK binary is found, the script tries the installed Visual Studio MSBuild toolchain (v142 and Windows SDK required). Otherwise supply a compatible existing binary using -ChuckExe "C:\path\to\chuck.exe". Visual Studio is not installed automatically. No GitHub credentials or uploads are required.

Send setup-windows.log and build-and-test.log from tools/multimedia if present (review for personal paths before sharing). Output binaries are in build/multimedia-windows/install/chugins. This procedure is experimental; the build-and-test stage has passed on Windows x64 (see below). Dependency revisions are not pinned; the MIDI commit is recorded in the log.

## Verified Windows result — 2026-11-24

User-executed build-and-test.ps1 against source revision e4726eb28f6f9036a2414f572fa7a1825d84e651 (PR #18): ChucK 1.5.5.9 x64, host API 10.2, MSYS2 UCRT64, ImageMagick 7.1.2, FFmpeg libavcodec/libavformat major 63, libvpx 1.17.0, midifile revision 98917df5b1bf0d6e8d4c0e5fff86d6b05343e793. All six modules loaded. All 99 smoke checks passed: RasterIO 8, AudioIO 7, VectorIO 4, ModelIO 6, MidiFileIO 5, VideoIO 69. H.264 and VP9 encoders actually ran, and independent binary/pixel/audio/metadata assertions completed. Final script message: All multimedia smoke tests passed. This is a smoke-test result, not exhaustive format compatibility or Linux/macOS verification.
