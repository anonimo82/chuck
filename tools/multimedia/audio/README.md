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

## In-memory audio editing

AudioBuffer.crop(firstFrame,frameCount) keeps a nonempty range; remove(firstFrame,frameCount) deletes a range without leaving an empty buffer. appendSamples(float[]) concatenates interleaved PCM at the existing rate and channel count. reverse() reverses frame order, preserving channels. peak() returns maximum absolute raw sample. normalize(target) sets raw peak to [0,1], leaving silence unchanged. fade(firstFrame,frameCount,fromGain,toGain) applies an inclusive linear envelope over at least two frames, gains in [0,1]. mixSamples(firstFrame,float[],mixGain) adds samples with gain [0,16] and clamps to [-1,1]. readSamples(firstFrame,float[]) exports processed samples to a preallocated array. No resampling or channel conversion. Edits preserve gain/balance and the existing 32 Mi sample limit. Synchronous offline operations; no new dependencies. 41 new checks await Windows testing.

## Offline DSP

resample(sampleRate) changes the rate and rounds the frame count to preserve duration approximately. changeSpeed(factor) retains the sample rate and changes both pitch and duration; factor 0.0625–16. Both use normalized Hann-windowed sinc interpolation with reduced cutoff on downsampling and constant endpoint extension. Ratio limited to 1/16–16; existing sample limits and a one-billion tap/channel operation bound apply. convertChannels(1 or 2) averages stereo to mono or duplicates mono to stereo, rejects multichannel sources. lowPass(frequency) and highPass(frequency) use complementary first-order exponential filters with independent zero-initialized channel state; frequency must be positive and below Nyquist. These are simple filters, not exact digital -3 dB cutoff designs. Processing edits raw samples, preserves gain/balance, clips resampling/filter overshoots, and is transactional. No pitch-preserving time stretch, no realtime guarantee, no new dependencies. 50 new checks await Windows compilation/testing.

## Audio-to-raster visualization

exportWaveform(channel,width,height,int[] pixels) exports an opaque grayscale min/max envelope, one column per time bucket, white waveform on black. Positive amplitudes are at the top. When width exceeds frame count, samples repeat. exportSpectrogram(channel,width,fftSize,floorDb,int[] pixels) outputs width by fftSize/2+1 pixels, linear frequency with DC at bottom and Nyquist at top; time centers are uniformly spaced over the clip. Uses radix-2 FFT, symmetric Hann window, zero padding, coherent-gain-corrected one-sided amplitude and grayscale dBFS mapping from floorDb to 0 dBFS. FFT sizes 32–4096 powers of two; floor -160 to -10 dB. Both use current gain/balance and a selected zero-based channel, straight row-major RGBA8, exact preallocated array size, dimensions <=4096 and <=4194304 pixels. FFT work limited to width*fftSize <=8388608. Failures leave arrays unchanged; audio data is not modified. No additional dependency or realtime guarantee. RasterImage.importPixels can consume these arrays directly. Native validation pending.

## Audio-to-vector waveform

exportEnvelope(channel,count,float[] bounds) writes count interleaved minimum/maximum amplitude pairs, using the same time buckets and gain/balance as exportWaveform. Count 1–4096; exact preallocated size 2*count. Repeats samples when count exceeds frames. Failure leaves output unchanged. Pass bounds to VectorImage.importWaveform; no cross-Chugin native dependency.

## Monophonic pitch detection

detectPitches(channel,blockFrames,rmsGate,confidence,int[] pitches) analyzes nonoverlapping complete blocks using current gain/balance, DC removal, RMS gate and normalized autocorrelation. First local peak above confidence is refined by parabolic interpolation and rounded to nearest MIDI pitch. Unvoiced blocks are -1. Exact output length floor(frames/blockFrames), 1–100000 blocks; block size 256–16384, sample rate 8000–96000, gate (0,1], confidence [0.5,1]. Nominal frequency range 65–2000 Hz, constrained by block length; work capped at 200 million sample-lag comparisons (plus one boundary lag). Invalid input preserves output. No polyphonic transcription, onset refinement, trailing partial blocks or guaranteed accuracy on complex sounds. Compatible with MidiSequence.importPitches. Native validation pending.
