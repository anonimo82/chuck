# Multimedia roadmap

Six types: raster, vector, 3D, audio-wave, audio-MIDI, video. Eight processes: synthesis, manipulation, input, output, capture, conversion, transmission, automation. Video is both an independent type and a synchronized composition of raster frames and optional audio tracks (and subtitles). Explicitly excluded: text to speech, speech recognition, ASCII art, tracing and OCR.

1. In-memory exchange and creation: typed RGBA and interleaved PCM arrays, video frame export (PR 49); next, incremental video encoding and audio-track exchange with explicit timestamps.
2. Extend synthesis and editing: vector primitives, meshes and weighting, MIDI event composition, raster filters and compositing, audio processing.
3. Cross-type conversions: vector rasterization, 3D rendering, MIDI rendering, audio visualization/sonification, and synchronized audiovisual assembly. Musical audio-to-MIDI transcription and vector-to-3D extrusion are separate algorithmic tasks.
4. Capture and device I/O: reuse native ChucK audio/MIDI and ChuGL where appropriate, add camera/screen and applicable graphical/3D devices, synchronize clocks.
5. Network transmission: explicit formats, framing, buffering, timestamps, and transport-specific safety limits; distinguish offline from realtime.
6. Automation: composable numeric controls, time-based processing and reusable batch workflows.

PR 49 API: RasterImage.create(w,h) creates transparent RGBA8; setPixel(x,y,r,g,b,a) and pixelAt(x,y,channel) use raw bytes. importPixels(w,h,int[]) copies exact row-major RGBA bytes and resets controls. exportPixels(int[]) writes processed RGBA to a preallocated exact-size array. AudioBuffer.create(frames,rate,channels) creates silence; importSamples(rate,channels,float[]) copies normalized interleaved samples and resets controls; exportSamples(float[]) writes processed samples into an exact-size preallocated array. VideoClip.exportPixels(int[]) writes the current processed frame in the same RGBA format, opaque alpha. No temporary files, no shared pointers, no extra dependencies. Arrays are copies; invalid imports preserve previous state. Existing bounds apply. All operations synchronous; no hard realtime guarantee. 40 new checks awaiting Windows execution; previous baseline 597 passed.
