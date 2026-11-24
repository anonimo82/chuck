# AudioIO (experimental)
Optional native Chugin using system libsndfile >= 1.2.0; no changes to the VM or built-in audio classes. Requires C++17, pkg-config and libsndfile development headers. Linux: libsndfile1-dev (check version); macOS: brew install libsndfile pkg-config. Run make here. Windows build not yet provided.

API:
- AudioIO.sampleRate(path): Hz, or -1 on error.
- AudioIO.channels(path): channel count, or -1 on error.
- AudioIO.seconds(path): duration in seconds, or -1 on error.
- AudioIO.convert(source, destination): 1 on success, 0 on failure.

Input uses libsndfile's format detection. Output extensions: WAV, AIF/AIFF, AU/SND, FLAC (24-bit PCM), OGG (Vorbis), OPUS, MP3. Codec support depends on the installed library build; channel and sample-rate restrictions apply. No resampling or channel mixing is performed; unsupported combinations fail. Opus may involve internal codec resampling. Conversion does not preserve metadata or guarantee lossless results, especially for lossy encoders or floating-point inputs converted to PCM. Existing output files are not overwritten. Operations are synchronous and not suitable for real-time processing. Build and runtime validation pending.

## Persistent AudioBuffer

```chuck
@import "AudioIO";
AudioBuffer audio;
if(audio.load("input.wav")) {
    0.5 => audio.gain;
    -0.25 => audio.balance;
    <<< audio.valueAt(0,0) >>>;
    audio.save("output.wav");
}
```

Each object holds independent interleaved double samples, up to 32 Mi samples (256 MiB), 1–8 channels, 1–384000 Hz. load replaces data and resets controls only on success. loaded(), frames(), channels(), sampleRate(), seconds(), clear(), reset(), save(path) are available. gain() [0,16], default 1; balance() [-1,1], default 0, attenuates the opposite stereo channel linearly (no effect on non-stereo). Setters accept ordinary numerical values and reject invalid inputs with NaN, preserving prior settings. sampleAt(frame,channel) returns the stored sample; valueAt returns gain/balance-adjusted sample clipped to [-1,1]. setSample(frame,channel,value) changes the stored sample in [-1,1]; indices are zero-based. Invalid sample reads return NaN, invalid writes return 0. save processes current controls without altering the stored samples, preserves sample rate/channel count, and uses exclusive creation. Output formats match AudioIO.convert; lossy encoders may vary. No UGen playback, resampling or streaming yet. File operations and allocation are offline; no hard real-time guarantee. KeyframeCurve can drive controls without any dependency. 34 new smoke checks plus independent PCM comparisons await execution.
