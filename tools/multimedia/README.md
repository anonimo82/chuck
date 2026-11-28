# Optional multimedia I/O backend

This initial offline conversion utility does not change the ChucK VM or require new core dependencies. It is not a native Chugin. Install Python 3 and the relevant executable on PATH: FFmpeg, Assimp CLI, librsvg (rsvg-convert). Availability of codecs and exporters depends on the installed build.

From the repository root:

    python3 tools/multimedia/mediaio.py input.mp3 output.wav
    python3 tools/multimedia/mediaio.py input.jpg output.png
    python3 tools/multimedia/mediaio.py input.svg output.png
    python3 tools/multimedia/mediaio.py input.fbx output.obj
    python3 tools/multimedia/mediaio.py input.mp4 output.mpg

Then load WAV with SndBuf, PNG with ChuGL Texture, OBJ with ChuGL GModel. For ChuGL video, use FFmpeg explicitly with MPEG-1 video and MP2 audio in an MPEG program stream; not every .mpg is compatible.

The tool refuses existing primary output files. Assimp can create sidecar files, so use a fresh output directory for models. Conversion can lose metadata, animation, materials, transparency, audio channels or quality. Only conversions supported by the installed backend will succeed. Use offline, not on the real-time audio thread.

## Native Chugins and unified build (experimental)

The native multimedia implementation is optional and separate from the offline CLI above. The unified CMake build defines ten modules: RasterIO, VectorIO, ModelIO, AudioIO, MidiFileIO, VideoIO, SubtitleIO, KeyframeIO, TextIO and MediaTransport. See the module documentation for API and format limitations. Native application project formats such as .blend are not supported inputs to ModelIO; actual codec and importer availability still depends on installed libraries.

On Windows x64, use the MSYS2 UCRT64 environment and the provided setup and build scripts. Install dependencies once (or when required):

    powershell -ExecutionPolicy Bypass -File tools/multimedia/setup-windows.ps1

The build-and-test script configures CMake and Ninja, builds and installs all ten Chugins, stages runtime DLLs, probes the modules with ChucK, and invokes the selected smoke suites. Its local build directory is build/multimedia-windows. Supply -ChuckExe if ChucK is not discoverable; it must be compatible with the Chugin API. MIDI dependency checkout SHA is logged, but the upstream revision is not pinned.

On Linux and macOS, the unified CMake build requires a C++17 compiler, CMake 3.20+, pkg-config, ImageMagick MagickWand, librsvg 2.46+, Cairo, Assimp, libsndfile 1.2+, PangoCairo, GLib, FFmpeg libraries (including libswresample), libpng, libjpeg and libwebp, and a checkout of https://github.com/craigsapp/midifile. FFmpeg CLI and Python 3 are needed for smoke tests. Optional camera, screen and OpenGL capture depend on the platform and available libraries.

    cmake -S tools/multimedia -B build/multimedia -DMIDIFILE_SOURCE_DIR=/absolute/path/to/midifile
    cmake --build build/multimedia
    cmake --install build/multimedia --prefix /absolute/path/to/install

The installed Chugins are in the chugins subdirectory. The older Makefile covers only six modules and is not the complete build path for the current smoke suites. The Windows local validation results do not establish equivalent Linux or macOS runtime coverage. Smoke tests are not exhaustive format or security tests.

## Validation policy

GitHub Actions are not used for the current development cycle; run validation locally. No workflow is required to execute these scripts.

### Native smoke-test summary

The Windows build-and-test script now invokes run_smoke_tests.py, which runs the existing raster, audio and multimedia Python suites and prints one MULTIMEDIA SUMMARY line with expected ChucK checks for batches actually started, observed PASS/FAIL tokens, missing checks within reported batches, unreported checks (started batches that never returned counts), extra checks, completed suites and overall status. Each batch announces its expected count before launching ChucK, so repeated batches and optional encoder checks are counted correctly. On early failure, later batches that never start are not included in expected; incomplete suites still fail overall. Python fixture assertions, FFmpeg errors and other suite errors affect overall status but are not counted as ChucK checks. A compilation/probe failure before the suites start produces no test summary. The full transcript remains available for diagnosis. No additional manual test invocation is required.

### Local and global validation manifests

The repository maintains two versioned lists: global-tests.json (all registered groups) and local-tests.json (current cycle's new groups and explicitly selected regression groups). A group is an independently executable collection of tests and its fixture dependencies. Every new group must be registered in the runner and test dispatcher, added to both manifests in the same PR, and retained in global-tests.json permanently. Existing tests must not be copied or removed to change selection.

During the expansion cycle, use -Mode Local (or its compatibility alias -Mode Incremental) after every ten added functional families, and -Mode Full when each source application’s approved functional scope is complete, resetting the family counter for the next cycle. Run additional targeted or Full validation after corrective or shared structural changes when necessary. The cadence is a development policy, not an automatic counter. The default remains Full for compatibility. The old -Since option remains accepted with Incremental and conservatively selects Full rather than inferring tests from a Git diff.

Use -Mode Failed to retry the previously attempted group selection plus the current local manifest. A diagnostic -Group selection does not certify the cycle. The runner records attempts and successful clean commits in the existing local build state file. An empty local manifest reports SKIP (zero tests), not PASS. The runner never edits tracked manifests: after a successful cycle validation is reported and checked against the selected commit and expected count, the next infrastructure/functional PR clears the local list; on failure it remains intact. This keeps the working tree clean and avoids silently clearing tests on other developers' machines. Full validation can also close the current local cycle.

Historical baseline at manifest introduction: 3330 native checks; current counts depend on the checked-out revision and selected groups. Verify manifest selection without building or launching ChucK:

    python tools/multimedia/run_smoke_tests.py --mode Full --list
    python tools/multimedia/run_smoke_tests.py --mode Local --list

Windows execution:

    powershell -ExecutionPolicy Bypass -File tools/multimedia/build-and-test.ps1 -Mode Local
    powershell -ExecutionPolicy Bypass -File tools/multimedia/build-and-test.ps1 -Mode Full

Build and chugin probes are unchanged. Full mode runs all three suites. The summary counts selected started batches and selected suites only. No new native tests are introduced by the manifest infrastructure.
