# Portable multimedia build (experimental)

The CMake build is optional and does not rebuild ChucK itself. Use the same architecture and compatible ChucK DLL API (10.2) as the host.

## Windows x64

Use an MSYS2 UCRT64 shell, not the plain MSYS shell. Install the UCRT64 C++ compiler, CMake, Ninja, pkgconf, ImageMagick, librsvg, Cairo, Assimp, libsndfile and FFmpeg development packages. Package names and availability depend on the MSYS2 repository. Obtain a checkout of https://github.com/craigsapp/midifile separately.

From the ChucK repository root:

    cmake -S tools/multimedia -B build/multimedia -G Ninja -DMIDIFILE_SOURCE_DIR=/absolute/path/to/midifile
    cmake --build build/multimedia
    cmake --install build/multimedia --prefix build/multimedia-install

The installed .chug files are under build/multimedia-install/chugins. Run the matching x64 chuck.exe with --chugin-path pointing at that directory. The UCRT64 bin directory must be on PATH for dependent DLLs. Do not mix libraries from different MSYS2 environments. Loading these modules into the upstream MSVC-built host has not yet been verified.

## Linux and macOS

Use the same CMake commands with the dependencies described in README.md and native compiler and pkg-config. The existing Makefiles remain available.

## Status

This is a candidate build configuration, not a tested Windows release. No binaries are provided. Windows Unicode filenames, dependency DLL bundling, ABI compatibility, all native runtime tests, and reproducible dependency versions remain to be verified.
