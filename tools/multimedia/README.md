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
