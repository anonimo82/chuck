# AudioIO (experimental)
Optional native Chugin using system libsndfile >= 1.2.0; no changes to the VM or built-in audio classes. Requires C++17, pkg-config and libsndfile development headers. Linux: libsndfile1-dev (check version); macOS: brew install libsndfile pkg-config. Run make here. Windows build not yet provided.

API:
- AudioIO.sampleRate(path): Hz, or -1 on error.
- AudioIO.channels(path): channel count, or -1 on error.
- AudioIO.seconds(path): duration in seconds, or -1 on error.
- AudioIO.convert(source, destination): 1 on success, 0 on failure.

Input uses libsndfile's format detection. Output extensions: WAV, AIF/AIFF, AU/SND, FLAC (24-bit PCM), OGG (Vorbis), OPUS, MP3. Codec support depends on the installed library build; channel and sample-rate restrictions apply. No resampling or channel mixing is performed; unsupported combinations fail. Opus may involve internal codec resampling. Conversion does not preserve metadata or guarantee lossless results, especially for lossy encoders or floating-point inputs converted to PCM. Existing output files are not overwritten. Operations are synchronous and not suitable for real-time processing. Build and runtime validation pending.
