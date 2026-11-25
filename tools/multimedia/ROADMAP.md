# Multimedia roadmap

Six types: raster, vector, 3D, audio-wave, audio-MIDI, video. Eight processes: synthesis, manipulation, input, output, capture, conversion, transmission, automation. Video is both an independent type and a synchronized composition of raster frames and optional audio tracks (and subtitles). Explicitly excluded: text to speech, speech recognition, ASCII art, tracing and OCR.

1. In-memory exchange and creation: typed RGBA and interleaved PCM arrays, video frame export (PR 49); next, incremental video encoding and audio-track exchange with explicit timestamps.
2. Extend synthesis and editing: vector primitives, meshes and weighting, MIDI event composition, raster filters and compositing, audio processing.
3. Cross-type conversions: vector rasterization, 3D rendering, MIDI rendering, audio visualization/sonification, and synchronized audiovisual assembly. Musical audio-to-MIDI transcription and vector-to-3D extrusion are separate algorithmic tasks.
4. Capture and device I/O: reuse native ChucK audio/MIDI and ChuGL where appropriate, add camera/screen and applicable graphical/3D devices, synchronize clocks.
5. Network transmission: explicit formats, framing, buffering, timestamps, and transport-specific safety limits; distinguish offline from realtime.
6. Automation: composable numeric controls, time-based processing and reusable batch workflows.

PR 49 API: RasterImage.create(w,h) creates transparent RGBA8; setPixel(x,y,r,g,b,a) and pixelAt(x,y,channel) use raw bytes. importPixels(w,h,int[]) copies exact row-major RGBA bytes and resets controls. exportPixels(int[]) writes processed RGBA to a preallocated exact-size array. AudioBuffer.create(frames,rate,channels) creates silence; importSamples(rate,channels,float[]) copies normalized interleaved samples and resets controls; exportSamples(float[]) writes processed samples into an exact-size preallocated array. VideoClip.exportPixels(int[]) writes the current processed frame in the same RGBA format, opaque alpha. No temporary files, no shared pointers, no extra dependencies. Arrays are copies; invalid imports preserve previous state. Existing bounds apply. All operations synchronous; no hard realtime guarantee. 40 new checks awaiting Windows execution; previous baseline 597 passed.


## Current checkpoint — 2026-11-25, PR #155

This section supersedes historical status statements above, not the original scope or exclusions.

- Last reported full native validation: PR #135, 3330/3330 checks, three suites. PR #136 introduced the two manifests.
- Subsequent functional PRs: #137–141 (pagination), #143–147 (linked frames), #149–153 (reading order, interchange, optimization and consolidation). Corrective PRs #142, #148, #154 and #155 are excluded from the cadence.
- Latest local validation reported by the developer: 249/249 PASS, zero FAIL/missing/unreported/extra, one suite, after #155. Cumulative validated checks: 3999; outstanding: zero. This is not a fresh full-suite result.
- Global validation cadence: **15/20 functional PRs** since #135; five functional PRs remain before the next Full run. The previous 7/20 at #144 was a cadence checkpoint, not project completion.
- Seventh text cycle: complete. Next text cycle: 0/5; next functional PR number depends on intervening maintenance PRs.
- Local manifest still contains the five validated groups until the next functional/infrastructure change resets it; global manifest is never pruned. No GitHub Actions changes.

### Recovered scope and progress

The original six media types are raster, vector, 3D, waveform audio, MIDI audio and video; procedural text was subsequently added as a seventh type. Eight processes remain synthesis, manipulation, input, output, capture, conversion, transmission and automation. The initial roadmap above remains the basis for planning. Earlier conversation records report a historical 7×8 assessment of 32 operational, 13 partial and 11 unimplemented cells (68.75% if partial cells count as half). The cell-level assignments and current assessment have not been recovered; do not present this historical estimate as current completion. The validation cadence is not a percentage of project completion.

| Text milestone | Functional PRs | Status |
| --- | --- | --- |
| Pagination and continuation | #137–141 | Locally validated |
| Linked frames, mixed runs, rendering and native persistence | #143–147 | Locally validated |
| Logical reading order, TXT exchange, document/flow exchange, range optimization, integration | #149–153 | Locally validated |
| Eighth text cycle | TBD | Not yet defined in recovered records |

### Remaining roadmap themes

Recovered themes, not a claim that every item is entirely absent: advanced cross-type interoperability, realtime constraints, capture/device I/O, transmission and automation; cross-platform validation and API documentation. Reassess existing implementations before assigning new PRs. Preserve the original exclusions (text-to-speech, speech recognition, ASCII art, tracing, OCR). Do not change GitHub Actions under the current development policy.

### Maintenance protocol

Update this checkpoint at each validated cycle, with the last full baseline, functional PR count, local/global check totals, exact scope and validation limitations. Use Local after five functional PRs and Full after twenty instead of Local, or after shared structural changes as appropriate. Corrective PRs do not advance the cadence. Keep pending groups until successful validation. Preserve API and native-format compatibility. Future milestones must be marked proposed until agreed; do not invent an original roadmap or a completion percentage.

### Eighth text cycle — proposed implementation sequence

#157 frame-local hit testing and insertion offsets; #158 selection and ranges; #159 text insertion/deletion/replacement; #160 style preservation during edits; #161 integration and documentation. PR numbering is indicative; corrective PRs do not count. Full validation at the end (20 functional PRs since the last baseline). First functional group: 35 new checks, global 4034, local 35; native validation pending.

#158: directional UTF-8 source selection implemented; 57 new checks, global 4091, local 92; cadence 17/20, native validation pending.

#159: transactional insertion/deletion/replacement; 60 new checks, global 4151, local 152; cadence 18/20, native validation pending.

#160: explicit styled insertion/replacement, range formatting and style lookup; 75 new checks, global 4226, local 227; cadence 19/20, native validation pending.

#161: integrated editing regression and documentation; 56 new checks, global 4282, local 283; eighth text cycle implementation 5/5, cadence 20/20. Full native validation required before next functional cycle. Last confirmed cumulative total 3999. Keep all pending groups until success.
