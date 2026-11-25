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

## Remaining work

- Native Chugin bindings for image decoding/encoding, scene import/export and media streaming.
- SVG vector representation and export (currently rasterization only).
- MIDI file writing and editing; existing MidiFileIn is unchanged.
- Automated round-trip tests with fixtures across Linux, Windows and macOS.
- Exact runtime capability enumeration and codec/licensing review before distributing binaries.

## Native Chugins and unified build (experimental)

Six optional modules now exist in raster, vector, model, audio, midi and video. On Linux install build-essential, pkg-config, libmagickwand-dev, librsvg2-dev, libcairo2-dev, libassimp-dev, libsndfile1-dev (>= 1.2.0), libavformat-dev, libavcodec-dev, libavutil-dev, libswscale-dev and ffmpeg. On macOS the equivalent Homebrew libraries are imagemagick, librsvg, cairo, assimp, libsndfile, ffmpeg and pkg-config. Obtain https://github.com/craigsapp/midifile separately (revision not yet pinned).

From the repository root, with a matching ChucK binary built at src/chuck:

    make -C tools/multimedia MIDIFILE=/absolute/path/to/midifile
    make -C tools/multimedia test MIDIFILE=/absolute/path/to/midifile
    make -C tools/multimedia install MIDIFILE=/absolute/path/to/midifile PREFIX=/absolute/path/to/chugins

Use chuck --chugin-path:/absolute/path/to/chugins with your script. The install target copies six binaries and may replace previous copies in the chosen prefix. See each module README for API limitations. Windows build remains pending. Smoke tests are not exhaustive format or security tests. CI and local native compilation results have not yet been verified.

## Automated multimedia CI

The separate Multimedia CI workflow runs on relevant pull requests, pushes to main and manual dispatch. Linux and macOS use the unified CMake build (all eight modules); Windows reuses build-and-test.ps1 with MSYS2 UCRT64 dependencies and a runner-native CMake. Each job uploads its build/test log even on failure. The checks are independent of the existing core workflows; a red job is not a successful validation. The optional graphical ChuGL bridge is not covered by these headless jobs. MIDI dependency is currently fetched from upstream HEAD; the checkout SHA is logged for diagnosis. Runner compatibility and first-run results remain to be verified.

### Native smoke-test summary

The Windows build-and-test script now invokes run_smoke_tests.py, which runs the existing raster, audio and multimedia Python suites and prints one MULTIMEDIA SUMMARY line with expected ChucK checks for batches actually started, observed PASS/FAIL tokens, missing checks within reported batches, unreported checks (started batches that never returned counts), extra checks, completed suites and overall status. Each batch announces its expected count before launching ChucK, so repeated batches and optional encoder checks are counted correctly. On early failure, later batches that never start are not included in expected; incomplete suites still fail overall. Python fixture assertions, FFmpeg errors and other suite errors affect overall status but are not counted as ChucK checks. A compilation/probe failure before the suites start produces no test summary. The full transcript remains available for diagnosis. No additional manual test invocation is required.
