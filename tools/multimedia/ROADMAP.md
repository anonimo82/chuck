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

## Full validation checkpoint — PR #161

Developer-reported Windows Full validation: expected=4282, PASS=4282, FAIL=0, missing=0, unreported=0, extra=0, suites=3/3, status=PASS. All multimedia smoke tests passed. Eighth text cycle validated (283 new checks). New full baseline 4282; functional cadence resets to 0/20. Next cycle starts at 0/5. Clear the validated local manifest in the checkpoint maintenance PR; retain the global manifest unchanged. This is Windows native validation, not a new Linux/macOS validation claim.


## 7×8 capability audit — baseline #162 (2026-11-25)

The user confirmed seven media types and eight processes and elected to prioritize coverage gaps over further specialized text editing. This static audit inspected the registered native APIs in raster/RasterIO.cpp, vector/VectorIO.cpp, model/ModelIO.cpp, audio/AudioIO.cpp, midi/MidiFileIO.cpp, video/VideoIO.cpp, text/TextIO.cpp, subtitle/SubtitleIO.cpp, their module documentation, CMakeLists.txt, KeyframeIO documentation and relevant ChucK core sources (chuck_io.cpp, ulib_opsc.cpp, ulib_std.cpp, ugen_xxx.cpp, ugen_stk.cpp). It is a provisional planning assessment, not exhaustive hardware or cross-platform certification. Baseline: 4282/4282 Windows Full PASS at #161; #162 maintenance; local manifest empty; functional cadence 0/20.

### Process definitions

Synthesis: create media programmatically. Manipulation: modify existing media. Input: read/import existing content from files or interfaces. Output: save/export or locally reproduce content. Capture: acquire new content directly from a live device/source. Conversion: transform formats or media types. Transmission: exchange media over a communication channel/network. Automation: drive media operations programmatically over time or in reusable workflows. Reuse core ChucK and optional ChuGL rather than duplicate capabilities. Video remains both independent and synchronized raster/audio composition. TTS, speech recognition, ASCII art, tracing and OCR remain excluded.

### Matrix

O = usable foundational capability; P = partial capability; M = specific capability not identified in inspected code. File input/output qualifies without every codec or device; conversion qualifies with useful existing format or cross-type routes, not every possible pairing; automation includes ordinary ChucK scheduling and numerical control. Generic OSC/blob or byte exchange alone is not a complete media transport. Capture does not mean reloading an existing file or generating synthetic media. Text capture means direct text entry/collection, not OCR or speech recognition. Vector and 3D capture are explicitly out of scope (N/A), including point clouds and scanning; existing geometry input, synthesis, editing, conversion and transmission remain in scope. O does not imply full professional feature parity or hard-realtime safety.

| Type | Synthesis | Manipulation | Input | Output | Capture | Conversion | Transmission | Automation |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Raster | O | O | O | O | M | O | M | O |
| Vector | O | O | O | O | N/A | O | M | O |
| 3D | O | O | O | O | N/A | O | M | O |
| Audio Wave | O | O | O | O | O | O | M | O |
| Audio MIDI | O | O | O | O | O | O | P | O |
| Video | O | O | O | O | M | O | M | O |
| Text | O | O | O | O | O | O | P | O |

45 O, 2 P, 7 M, 2 N/A = 56. Weighted coverage over 54 applicable cells (O=1, P=0.5) = 85.19%; this is a new static rubric, not a comparable measured increase over the earlier historical estimate, nor a percentage of engineering effort.

### Evidence and gaps by type

- Raster: RasterImage create/fill/line/composite/crop/resize, load/save, RasterIO.convert, typed pixel/audio/pitch/heightmap export and numeric controls/keyframe animation. No dedicated live screen/camera acquisition or media transport identified.
- Vector: VectorImage.create/addRect/addCircle/addLine/addPolygon/removeShape/load/save/render/exportPixels and cross-type exports; global transforms and numeric automation. Operational foundational manipulation after #169 and Windows local validation: individual geometry, affine transforms, solid paint, structural groups, duplication, stacking order and explicit editable SVG subset loading. Arbitrary SVG DOM editing is not claimed; tracing remains excluded. Vector capture excluded; transport remains missing.
- 3D: Model3D.createMesh/createBoxes/setVertex/transformMesh, materials, skinning and animation controller, load/save, orthographic rendering/projection and sonification. Optional ChuGL geometry/scene bridge documented, graphical runtime not certified by headless baseline. Live geometry capture excluded; no dedicated network scene transport identified.
- Wave: AudioBuffer.create/importSamples, crop/mix/resample/filters, load/save, format conversion and visualization/pitch analysis. Native ChucK adc/dac, SndBuf and WvOut cover foundational local device acquisition/playback/recording and scheduling. No dedicated network waveform streaming/transport identified.
- MIDI: MidiSequence.create/addNote/addCC/addTempo, edit/quantize, load/save, renderSamples and visual/geometry export. Native MidiIn/MidiOut provide live event I/O; foundational capture operational after #171–173 with MidiCapture timestamped channel messages, session controls and SMF export, validated with constructed MidiMsg events. Physical device behavior is not certified. Transmission remains partial because network transport is not established.
- Video: VideoWriter.open/appendFrame/openAV/appendAV, VideoClip.load/next/exportPixels, transcode/trim/concat/transition/composite/subtitles, VideoAudio and synchronized frame/PCM writing. No camera/screen video acquisition or explicit network video transport identified. Offline operations do not guarantee realtime.
- Text: TextDocument/TextFlow creation, formatting, pagination, editing and TXT/native/PNG/SVG/PDF interchange, programmatic controls. Foundational capture operational after #174–175 with TextCapture UTF-8 buffer, terminal ASCII key adapter and document/flow snapshots; committed Unicode strings/codepoints accepted, no IME or global hooks. Physical terminal behavior is not certified. OSC strings give partial transmission; media-aware text transfer remains missing. More specialized grapheme/cursor geometry remains deferred.

### Proposed priority order (not an approved PR schedule)

1. Define a bounded shared media exchange protocol and transport requirements (framing, sizes, timestamps, validation, explicit local/trusted endpoints; separate offline transfer from realtime streaming). Reuse native interfaces where appropriate. Start with one small end-to-end media type before extending it.
2. Complete practical capture pathways: camera/screen for raster and video; timestamped MIDI recording; text entry/collection. Vector and 3D capture are excluded, including point-cloud support. Avoid OCR/tracing.
3. Complete vector shape manipulation within a documented supported subset; preserve imported SVG and existing APIs.
4. Extend transmission across types and add integration/validation; do not assume offline transfer equals live streaming.

The next functional PR is proposed to address a matrix gap rather than additional text layout/editor specialization. No implementation or dependency choice is committed by this audit. Preserve global tests and five/twenty functional PR cadence; no GitHub Actions changes.

### Scope decision after #163

User explicitly excluded vector and 3D capture and chose not to support point clouds. Both cells are N/A and excluded from the coverage denominator; the other six processes for these types remain unchanged, as does transmission. The matrix retains 56 positions, 54 applicable. This supersedes any earlier suggestion to implement live vector/3D capture. Documentation only; baseline 4282, local empty, cadence 0/20.

## Vector manipulation cycle 1

User prioritized vector manipulation before transmission and capture. #165: generated-shape inspection and geometry editing. Proposed next milestones: individual transforms, individual style, grouping/order, and persistence/integration. New checks: 57; global: 4339; local: 57; functional cadence: 1/20. Native validation pending. Vector manipulation remains partial until the supported editable SVG scope and integration are complete. The unmerged transmission experiment is deferred.

#166: individual affine transforms, pivoted rotation/scaling and matrix access; 58 new checks, global 4397, local 115; functional cadence 2/20. Native validation pending.

#167: individual fill, stroke, stroke width and opacity; 57 new checks, global 4454, local 172; functional cadence 3/20. Native validation pending.

#168: nested structural groups, duplication and z-order; 65 new checks, global 4519, local 237; functional cadence 4/20. Native validation pending.

#169: explicit editable SVG subset loading and end-to-end regression; 63 new checks, global 4582, local 300; functional cadence 5/20. Local native validation required. Vector manipulation classification remains partial until validation and scope review; arbitrary SVG editing not claimed.

## Validation checkpoint after #169

Developer-reported Windows Local: expected=300, PASS=300, FAIL=0, missing=0, unreported=0, extra=0, suites=1/1, status=PASS. All five vector groups validated. Cumulative validated checks 4582; last Full baseline remains 4282 after #161; cadence 5/20, next local cycle 0/5. Clear the validated local groups, retain global manifest. Promote vector manipulation to O under foundational coverage rubric (not full SVG editor parity). Remaining 11 applicable gaps: raster and video capture missing; MIDI and text capture partial; raster, vector, 3D, waveform and video transmission missing; MIDI and text transmission partial. Next priority to be selected by user; no new implementation choice committed. No workflow changes.

## Capture cycle 1 — MIDI and text

User selected all capture before transmission. Proposed three five-functional-PR cycles: MIDI/text, raster, video/audio synchronization; device backend feasibility must be verified. #171: timestamped MIDI channel message capture buffer, fixed-tempo tick conversion and inspection. 58 new checks, global 4640, local 58; functional cadence 6/20. Native validation pending. MIDI capture remains partial until device bridge, persistence and session integration. Vector/3D capture and point clouds remain excluded.

#172: native MidiMsg field bridge and SMF recording export; 42 new checks, global 4682, local 100; functional cadence 7/20. Hardware validation and native compilation pending.

#173: explicit MIDI session start/pause/resume/stop and active elapsed time; 57 new checks, global 4739, local 157; functional cadence 8/20. Native and device validation pending.

#174: bounded UTF-8 text capture buffer with codepoint insertion, cursor and editing; 64 new checks, global 4803, local 221; functional cadence 9/20. Native validation pending. Interactive device adapter and document/flow integration follow.

#175: native terminal key adapter and capture snapshots into TextDocument/TextFlow; 56 new checks, global 4859, local 277; functional cadence 10/20. Local validation required. MIDI and text capture classification pending validation and scope review. Terminal ASCII keys and caller-supplied committed Unicode text supported; no global keyboard hook or IME implementation. Physical MIDI and terminal device behavior not certified by headless tests.

## Validation checkpoint after #175

User-reported Windows Local: expected=277, PASS=277, FAIL=0, missing=0, unreported=0, extra=0, suites=1/1, status=PASS. Five capture groups validated. Cumulative validated checks 4859; last Full baseline remains 4282 after #161; functional cadence 10/20, local cycle resets to 0/5. Clear validated local groups; retain global manifest unchanged. Promote MIDI and text capture to O under foundational rubric; physical hardware and full Unicode terminal/IME behavior are not certified. Coverage 85.19% (45 O, 2 P, 7 M, 2 N/A). Next functional cycle: raster capture, with device backend feasibility to be assessed before implementation. No workflow changes.

## Capture cycle 2 — Raster

#177: shared bounded RGBA8 frame representation and RasterCapture API foundation; platform detection and capability queries explicitly report no installed capture backends. 43 new checks, global 4902, local 43; cadence 11/20. Native validation pending. Candidate backends for subsequent investigation: Windows Media Foundation and GDI desktop capture, Linux V4L2 and X11/desktop portal, macOS AVFoundation and ScreenCaptureKit. These are candidates, not implemented or verified. Existing FFmpeg build links codecs/format but not libavdevice; do not assume device support. Raster capture remains missing until actual acquisition. No dependency or workflow changes.

#178: optional libavdevice camera snapshot, backend capability and best-effort device enumeration (DirectShow, V4L2, AVFoundation). 17 new headless checks, global 4919, local 60; cadence 12/20. Existing foundation assertions adapted to backend availability without changing count. Native compilation and real camera validation pending; enumeration can be unsupported, timeout is cooperative and not guaranteed. No workflow changes.

#179: primary desktop region snapshot backend (Windows GDI, optional Linux X11 TrueColor), screen dimensions and capability queries. 22 new headless checks, global 4941, local 82; cadence 13/20. Three previous assertions adapted without changing counts. macOS and native Wayland screen capture not implemented. Native and live desktop validation pending. No workflow changes.

#180: direct RasterImage.importCapture snapshot bridge, independent pixel ownership, neutral rendering controls and PNG/edit roundtrip checks. 52 new checks, global 4993, local 134; cadence 14/20. Native and device validation pending. No workflow changes.

#181: direct snapshot save, end-to-end preservation/PNG checks and opt-in device example. 40 new checks, global 5033, local 174; cadence 15/20. Windows Local validation required before closing cycle. Actual camera and desktop capture must be tested separately with consent; unsupported macOS/Wayland desktop backends remain explicit limitations. No workflow changes.

## Validation checkpoint after #181

User-reported Windows Local: expected=174, PASS=174, FAIL=0, missing=0, unreported=0, extra=0, suites=1/1, status=PASS. Five raster capture groups validated. Cumulative validated checks 5033; last Full baseline remains 4282 after #161; functional cadence 15/20, local cycle resets to 0/5. Clear validated local groups; retain global manifest unchanged. Promote raster capture to O under foundational rubric; physical camera/desktop validation remains pending and macOS/native Wayland desktop backends are not implemented. Coverage 87.04% (46 O, 2 P, 6 M, 2 N/A). Next five functional milestones: sequential camera capture, screen recording, synchronized audio acquisition, VideoWriter audiovisual integration, capture consolidation and Full validation. No workflow changes.

## Capture cycle 3 — Video and synchronized audio

#183: persistent optional camera session in VideoIO, bounded RGBA8 sequential frames, monotonic host timestamps, explicit synthetic input and lifecycle, 65 headless checks; global 5098, local 65, cadence 16/20. Native compilation and live camera validation pending. Blocking/cooperative timeout; no guaranteed real-time scheduling, audio or recording integration yet. No workflow changes.

#184: sequential primary desktop region acquisition and explicit constant-frame-rate FFV1/MKV recording from capture snapshots; 56 new headless checks, global 5154, local 121, cadence 17/20. Native and physical validation pending. Caller controls sampling and recording cadence; no automatic scheduling, timestamp resampling, cursor capture guarantee, native Wayland or macOS screen backend. No workflow changes.

#185: session-relative synchronized PCM ingestion from ChucK audio sources (including adc via explicit caller sampling), contiguous sample-grid validation and bounded readback; 60 new headless checks, global 5214, local 181, cadence 18/20. No independent native microphone backend or hardware clock drift correction; caller aligns audio origin to video timeline. Native and physical validation pending. No workflow changes.

#186: explicit audiovisual capture recording using shared VideoWriterState FFV1/PCM Matroska encoder, session-origin alignment, nearest frame-slot validation and sample-grid audio slicing; 60 new headless checks, global 5274, local 241, cadence 19/20. Native and physical validation pending. No automatic missing-frame repair, audio padding or hardware drift correction. No workflow changes.

#187: capture lifecycle inspection and explicit cleanup, bounded rolling PCM via caller-directed discard protecting unwritten AV samples, sample-grid continuity after complete discard, end-to-end recorded video/audio readback and opt-in physical device example. 71 new headless checks, global 5345, local 312, cadence 20/20. Full native validation required before graphics cycle or coverage promotion. Physical device checks remain separate. No workflow changes.

## Full validation checkpoint after #187

User-reported Windows Full: expected=5345, PASS=5345, FAIL=0, missing=0, unreported=0, extra=0, suites=3/3, status=PASS. Global baseline and cumulative validated checks 5345. Original 15-functional-PR capture roadmap implemented and software validated. Clear the five validated local groups; preserve the global manifest unchanged. Reset functional cadence to 0/20 and local cycle to 0/5. Promote video capture to O under the foundational matrix rubric; coverage 88.89% (47 O, 2 P, 5 M, 2 N/A). Real camera, desktop, MIDI and audio-device behavior and hardware clock drift remain separately unverified. Graphics capture extension remains planned and is next, before transmission. No workflow changes.

## Planned capture extension — graphics output

User approved adding direct graphics capture before multimedia transmission, after the current five-PR video cycle and global validation. Plan five functional milestones: (1) OpenGL framebuffer readback with explicit context ownership and canonical RGBA8 orientation, (2) ChuGL integration after verifying its actual rendering backend and readback API, (3) offscreen rendering/capture, (4) deterministic audiovisual recording, (5) consolidation and local validation. Exact PR numbering and supported backend scope depend on feasibility and checkpoint/maintenance PRs. Do not assume ChuGL uses OpenGL. Keep existing capture matrix rubric and report extension progress separately until validated. No workflow changes.

#189: optional desktop OpenGL 2.1+ current-thread framebuffer readback, explicit timestamps and viewport bounds, pixel-pack restoration and PBO rejection, canonical top-down opaque RGBA8 adapter and recording roundtrip. 50 headless checks, global 5395, local 50, cadence 1/20. Native GL context readback not exercised by headless tests; caller owns current context and framebuffer, clean GL error state required. No context creation or ChuGL integration yet. No workflow changes.

#190: ChuGL public texture readback adapter, sourceMode 5, normalized top-down RGBA float ingestion with finite range validation and opaque RGBA8 conversion, recording roundtrip and opt-in bridge example. 53 headless checks, global 5448, local 103, cadence 2/20. Public Texture.read()/data() asynchronous readback verified against ChuGL documentation; no direct dependency or renderer changes. GPU example and actual texture layout/readback timing require separate live ChuGL validation. No workflow changes.

#191: optional OSMesa software offscreen context and explicit solid-color frame rendering, sourceMode 6, recording roundtrip; 34 checks in either backend branch, global 5482, local 137, cadence 3/20. OSMesa opt-in mutually exclusive with desktop GL linkage to avoid symbol collisions; default build tests unavailable-backend behavior and texture bridge. General scene/shader rendering and ChuGL offscreen scene integration not implemented here. Native OSMesa validation pending. No workflow changes.

#192: deterministic indexed audiovisual recording for synthetic/GL/texture/offscreen sources, integer sample origin and ingestion, strict one-new-frame-per-index contract, exact rational cumulative sample counts and rolling PCM integration. 80 headless checks, global 5562, local 217, cadence 4/20. Native validation pending; caller renders and supplies audio, no automatic scheduling or shader/scene integration. No workflow changes.

#193: indexed pending/ready/lost diagnostics, cross-source graphics pixel conversion and audiovisual roundtrip, explicit sequence-loss and alpha rejection checks, deterministic no-GPU example. 68 headless checks, global 5630, local 285, cadence 5/20. Local validation required before next cycle. Actual desktop GL context, ChuGL GPU readback and opt-in OSMesa build remain separately unverified; arbitrary offscreen scene/shader rendering not implemented. No workflow changes.

## Validation checkpoint after #193

User-reported Windows Local: expected=285, PASS=285, FAIL=0, missing=0, unreported=0, extra=0, suites=1/1, status=PASS. Five graphics capture groups validated. Cumulative validated checks 5630; last Full baseline remains 5345 after #187. Extended capture implementation cadence 20/20; functional global cadence 5/20, local cycle resets to 0/5. Clear validated local groups and retain global manifest unchanged. Matrix coverage remains 88.89% (47 O, 2 P, 5 M, 2 N/A); graphics extension does not add a separate matrix cell. Physical devices, actual desktop GL context, ChuGL GPU integration and optional OSMesa build remain separately unverified; arbitrary offscreen scene/shader rendering is not implemented. No workflow changes.

## Planned transmission and reception roadmap

Working estimate accepted by user: 25 functional PRs in five cycles, subject to protocol/backend feasibility and scope review. Cycle 1: shared infrastructure, protocols, connections and sessions. Cycle 2: waveform audio and MIDI. Cycle 3: raster, vector and 3D models. Cycle 4: video and synchronized audiovisual streaming. Cycle 5: text, integrated multimedia transmission/reception and consolidation. Specific transport choices, security model, performance guarantees and interoperability are not yet committed. Preserve existing APIs; local validation every five functional PRs, Full every twenty across cycles, excluding maintenance/fix PRs. Final project consolidation follows transmission; estimated 10–15 functional PRs, provisional. No workflow changes.

#195: dependency-free shared binary media framing v1, seven type IDs, bounded 1 MiB payload, stream and sequence IDs, microsecond timestamps, big-endian serialization and CRC-32 integrity, MediaTransport Chugin. 92 new checks, global 5722, local 92, cadence 6/20. Native validation pending. No network connections, media payload semantics, authentication, or realtime guarantees yet. No workflow changes.

#196: bounded single-peer bidirectional IPv4 loopback TCP transport, nonblocking accept/poll, explicit connection lifecycle, partial I/O, framing validation and receive backpressure. 66 new checks, global 5788, local 158, cadence 7/20. Native validation pending. Local connect may block; no remote endpoints, authentication, encryption, session negotiation, or hard-realtime guarantees. No workflow changes.

#197: symmetric CKHS v1 session handshake, common media mask and payload limit, explicit state/errors, cooperative monotonic timeout, reserved control stream and session policy enforcement. 66 new checks, global 5854, local 224, cadence 8/20. Native validation pending. No remote endpoints, authentication, encryption or hard-realtime guarantees. No workflow changes.

#198: explicit numeric IPv4 endpoints, opt-in remote access, exact source IPv4 filter, bounded connect timeout and peer inspection for raw and negotiated sessions. 58 new checks, global 5912, local 282, cadence 9/20. Native and cross-host validation pending; no authentication, encryption or hard-realtime guarantee. No workflow changes.

#199: transport counters and error diagnostics, seven-type bidirectional integration and reconnect checks, avoidance of redundant queued frame validation. 125 new checks, global 6037, local 407, cadence 10/20. Local validation required before starting audio/MIDI transmission. Cross-host tests and authentication/encryption remain outside this checkpoint. No workflow changes.

## Validation checkpoint after #199

User-reported Windows Local: expected=407, PASS=407, FAIL=0, missing=0, unreported=0, extra=0, suites=1/1, status=PASS. All five shared transport groups validated. Cumulative validated checks 6037; last Full baseline remains 5345 after #187. Transmission cycle 1 complete (5/25 functional PRs), global functional cadence 10/20, local cadence resets to 0/5. Clear validated local groups; retain global manifest unchanged. Matrix coverage remains 88.89% (47 O, 2 P, 5 M, 2 N/A): generic framing and transport do not yet establish media-specific transmission. Actual cross-host operation, authentication, encryption and hard-realtime behavior are not certified. Next functional cycle: waveform audio and MIDI transmission/reception. No workflow changes.

## Transmission cycle 2 — Waveform and MIDI

#201: CKAP v1 PCM16 waveform packets, interleaved 1–8 channels, 8–192 kHz, bounded sample validation and metadata inspection, bidirectional negotiated session integration. 82 new checks, global 6119, local 82, cadence 11/20. Native validation pending; quantization is lossy. Sequential streaming, buffering, synchronization and device integration remain pending. No workflow changes.

#202: sequential PCM blocks with rational cumulative sample timeline, strict stream/format/sequence/timestamp checks, state and error inspection, variable block lengths and negotiated session integration. 103 new checks, global 6222, local 185, cadence 12/20. Native validation pending. Buffering, gap repair, automatic scheduling and device synchronization remain pending. No workflow changes.

#203: bounded PCM receive ring, configurable prebuffer threshold, atomic overflow/underrun handling, sample-clock playback and buffered-duration diagnostics, stereo ring wrap and negotiated-session integration. 92 new checks, global 6314, local 277, cadence 13/20. Native validation pending. Caller-paced consumption; automatic scheduling, hardware clock synchronization, resampling and UGen integration remain pending. No workflow changes.

#204: CKMI v1 MIDI 1.0 channel voice packet codec, strict sequence and monotonic event timeline, all 16 channels, message validation and bidirectional PCM/MIDI session integration. 97 new checks, global 6411, local 374, cadence 14/20. Native validation pending. SysEx, system messages, hardware scheduling and shared playback synchronization remain pending. No workflow changes.

#205: combined PCM/MIDI receiver with shared caller-paced sample timeline, bounded MIDI queue, due-event release, late-event rejection, prebuffer and underrun gating, shared session integration. 92 new checks, global 6503, local 466, cadence 15/20. Local validation required before next cycle. Hardware synchronization, automatic scheduling, SysEx/system MIDI and cross-host validation remain pending. No workflow changes.

## Validation checkpoint after #205 and maintenance fix #206

User-reported Windows Local: expected=466, PASS=466, FAIL=0, missing=0, unreported=0, extra=0, suites=1/1, status=PASS. All five waveform/MIDI transport groups validated after the reserved identifier correction in #206. Cumulative validated checks 6503; last Full baseline remains 5345 after #187. Transmission cycle 2 complete (10/25 functional PRs), global functional cadence 15/20, local cadence resets to 0/5. Clear validated local groups; retain global manifest unchanged. Matrix coverage retained at 88.89% pending explicit matrix reassessment of media-specific transmission and remaining limitations. Actual cross-host operation, device clock synchronization, automatic playback scheduling, system MIDI/SysEx, authentication, encryption and hard-realtime behavior remain uncertified or unimplemented. Next functional cycle: raster, vector and 3D model transmission/reception. No workflow changes.

## Transmission cycle 3 — Raster, vector and 3D models

#208: CKRA v1 lossless RGBA8 raster packet codec, bounded dimensions, metadata validation, RasterImage array interoperability and bidirectional negotiated session tests. 93 new checks, global 6596, local 93, cadence 16/20. Native validation pending. Larger images require fragmentation/compression; sequential images and display scheduling remain pending. No workflow changes.

#209: CKRF v1 contiguous raster fragments, bounded 16 MiB image reassembly, strict sequence/image/offset validation, complete-image backpressure and session integration. 71 new checks, global 6667, local 164, cadence 17/20. Native validation pending; caller slices fragments, no compression or automatic display scheduling. No workflow changes.

#210: CKVG v1 UTF-8 SVG source transport, exact source preservation, bounded length and encoding validation, VectorImage interoperability and negotiated session roundtrip. 49 new checks, global 6716, local 213, cadence 18/20. Native validation pending. SVG semantic validation/sanitization, external resource bundling and fragmentation are not implemented in the transport. No workflow changes.

#211: CKMD v1 indexed triangle mesh transport, finite binary32 XYZ and bounded indices, Model3D array interoperability, negotiated session roundtrip. 71 new checks, global 6787, local 284, cadence 19/20. Native validation pending; geometry only, materials, textures, scene graphs, animation and fragmentation not included. No workflow changes.

#212: unified validated graphics packet dispatch, mixed raster/fragment/vector/model bidirectional session integration, raster counter exhaustion diagnostics and portable model integer type deduction. 78 new checks, global 6865, local 362, cadence 20/20. Full validation required before next cycle. No workflow changes. Graphics transmission remains limited to the documented codecs and caller-controlled scheduling; no cross-host certification.

## Full validation checkpoint after #212 and maintenance #213–#217

User-reported Windows Full on 2026-11-25: expected=6865, PASS=6865, FAIL=0, missing=0, unreported=0, extra=0, suites=3/3, status=PASS. All 362 new graphics transport checks validated, including uint32 counter exhaustion after #217. Raster timeout diagnostics #213–#215 identified MagickSetFormat as the last native stage in a timed-out run; independent UCRT64 format enumeration completed in 49.339 seconds, and #216 introduced a bounded configurable smoke timeout (default 180 seconds). Raster Local 8/8 and graphics Local 78/78 passed before Full. Exact environmental cause of slow module loading remains unconfirmed. Global validated baseline 6865; transmission cycle 3 complete (15/25 functional PRs), global cadence resets to 0/20, local cadence resets to 0/5. Clear validated local groups; global manifest unchanged. Matrix coverage remains 88.89% pending explicit reassessment. Cross-host operation, device synchronization, automatic scheduling, authentication, encryption and hard-realtime behavior remain uncertified or unimplemented; graphics limitations documented above remain. Next functional cycle: video transmission and audiovisual synchronization. No workflow changes.

## Transmission cycle 4 — Video and audiovisual synchronization

#219: CKVF v1 timestamped raw RGBA8 video frame codec (CKMP type 6), strict dimensions, byte and metadata validation, checksum and malformed payload rejection, negotiated bidirectional video-only loopback. 102 new checks, global 6967, local 102, cadence 1/20; native validation pending. Single-packet frames limited to 262140 pixels; larger resolutions require fragmentation or compression, sequential reception, buffering, synchronized presentation, device integration and cross-host validation remain pending. No workflow changes.

#220: CKVS v1 sequential fragmented RGBA8 video with bounded 16 MiB frame reassembly, strict contiguous packet/frame/offset counters and strictly increasing completed-frame timestamps, complete-frame backpressure, exhaustion handling and video session integration. 109 new checks, global 7076, local 211, cadence 2/20; native validation pending. CKVF remains a separate single-frame codec; MediaVideoStream consumes CKVS only. Caller slices packets to negotiated limits; compression, multi-frame buffering, automatic presentation, device integration and cross-host validation remain pending. No workflow changes.

#221: bounded caller-paced CKVS video FIFO with frame-slot and byte budgets (including incomplete frame reservation), configurable prebuffer threshold, retryable overflow and underrun/rebuffer gating, frame metadata and counters, session integration. 106 new checks, global 7182, local 317, cadence 3/20; native validation pending. Up to 256 slots and 64 MiB retained pixel budget, temporary decode memory additional; caller-driven presentation, no compression or device synchronization. No workflow changes.

#222: caller-paced common audio/video sample clock, PCM and CKVS routing, dual initial prebuffers, sample-boundary audio pull limit, due video presentation and lateness diagnostics, frozen clock on audio underrun and audio continuation during video starvation. 93 new checks, global 7275, local 410, cadence 4/20; native validation pending. Late frames remain deliverable, no automatic dropping, hardware clock recovery, resampling or real-time presentation. No workflow changes.

#223: validated PCM/CKVF/CKVS audiovisual packet metadata dispatch, mixed session delivery, synchronized presentation and boundary-at-end-of-buffer correction; correct six-sample PCM fixture size in #222 (60 bytes). 80 new checks, global 7355, local 490, cadence 5/20; Local validation required before next cycle. No workflow changes. Caller-paced raw video and PCM, no automatic late-frame dropping, hardware clock recovery, compression, device presentation or cross-host certification.

## Validation checkpoint after #223 and maintenance fix #224

User-reported Windows Local: expected=490, PASS=490, FAIL=0, missing=0, unreported=0, extra=0, suites=1/1, status=PASS. All five video and audiovisual transport groups validated after correcting the positive PCM16 quantization expectation in #224. Cumulative validated checks 7355 across the last Full baseline and this Local run; last Full baseline remains 6865 after #217. Transmission cycle 4 complete (20/25 functional PRs), global functional cadence 5/20, local cadence resets to 0/5. Clear validated local groups; retain global manifest unchanged. Matrix coverage remains 88.89% pending explicit reassessment. Caller-paced raw video and PCM, compression, automatic presentation, hardware clock recovery, resampling, cross-host certification, authentication and encryption limitations remain as documented above. Next functional cycle: text transmission and integrated multimedia consolidation. No workflow changes.

## Transmission cycle 5 — Text and integrated multimedia consolidation

#226: CKTX v1 plain Unicode UTF-8 text transport (CKMP type 7, excluding reserved control stream), exact byte preservation, strict Unicode scalar encoding validation, nonempty NUL-free payload, metadata and bidirectional negotiated text-only session. 63 new checks, global 7418, local 63, cadence 6/20; native validation pending. Text allows Unicode scalar values other than NUL, including ASCII controls; no normalization, rendering, sanitization, formatting or fragmentation. Max 1048564 text bytes per packet. No workflow changes.

#227: CKTX v2 ordered text fragments (CKMP type 7), 20-byte payload header with total bytes, offset and first sequence; same stream and timestamp, consecutive packet sequences without wrap, exact byte reassembly including split UTF-8 scalars, final strict UTF-8 validation, explicit reset between messages. MediaTextFragment and MediaTextAssembler; push returns 0 rejected, 1 partial, 2 complete. Maximum message 16 MiB, fragment data 1048556 bytes; caller chooses offsets and sizes within negotiated payload limit. No retransmission, interleaved messages within one assembler or automatic timing. CKTX v1 unchanged. 52 new smoke checks, global expected 7470, local 115, transmission 22/25, local cadence 2/5, global cadence 7/20; native validation pending; validated cumulative remains 7355 and matrix 88.89%. No workflow changes.

#228: MediaTextBuffer accepts CKTX v1 and ordered CKTX v2 on one configured stream, strict consecutive packet sequences without wrap, nondecreasing timestamps, FIFO completed messages, caller-supplied monotonic microsecond clock and fixed presentation delay. Full message byte and slot reservation during fragmentation; maximum 256 messages and 64 MiB logical payload budget, including reservations. No wall-clock recovery, clock-domain conversion, reordering, retransmission or automatic scheduling; late complete messages become immediately eligible. 65 new checks, global expected 7535, local 180, transmission 23/25, local cadence 3/5, global cadence 8/20; native validation pending, validated cumulative 7355, matrix 88.89%. No workflow changes.

#229: heterogeneous CKMP transport integration through MediaPacket.inspect and MediaRouter. Stable kind identifiers 1 raster, 2 raster fragment, 3 vector, 4 model, 5 PCM, 6 MIDI, 7 video frame, 8 video fragment, 9 text, 10 text fragment; metadata kind/type/stream/sequence/timestamp/payload bytes. Validates media-specific packet structure, rejects reserved control stream, explicit mask and up to 256 registered streams with independent consecutive packet sequences (no wrap) and nondecreasing timestamps. Mixed seven-type session loopback with text buffer integration. Router does not reassemble media, schedule playback or automatically dispatch to decoders; caller retains those responsibilities. 129 new smoke checks, global expected 7664, local 309, transmission 24/25, local cadence 4/5, global cadence 9/20; native validation pending, validated cumulative 7355, matrix 88.89%. No workflow changes.

## Extended transmission roadmap (user approved)

Expand from 25 to 40 functional PRs. Move the final consolidation after three additional five-PR scope extensions; PR numbers indicative, maintenance excluded from cadence. Raster: lossless RLE, PNG, JPEG/WebP, compressed fragmentation, consolidation. Video: automatic fragmentation, higher resolutions, intraframe compression, interframe/video codecs, consolidation. 3D: mesh attributes, materials/PBR/textures, scenes/transforms, skeletons/animation/morph targets, fragmentation/integration. Codec/backend feasibility remains to be verified. Preserve native validation cadence every five functional PRs locally and twenty globally. No workflow changes.

#230: CKRC v1 lossless deterministic RGBA8 pixel RLE, repeated and literal runs of up to 128 pixels, exact alpha preservation, bounded decoded image (16 MiB) and CKMP payload (1 MiB); malformed runs rejected, incompressible images may exceed packet limit and require raw fragmentation or later compressed fragmentation. MediaRasterCompressed packetSize/encode/inspect/decode, MediaGraphics kind 5 and MediaPacket kind 11; existing kind IDs and wire formats unchanged. 49 new checks, global expected 7713, local 358, transmission 25/40, local cadence 5/5, global cadence 10/20; native Local validation required before further functional PRs. Cumulative validated 7355, matrix 88.89%. No workflow changes.

Maintenance after #230: Windows native build failed before smoke execution due to CKTX and CKRF sharing mediawire::fragmentHeader and fragmentPacketSize. Rename only CKTX internal symbols to textFragmentHeader and textFragmentPacketSize and update its native bindings. No wire format, public API, test counts or workflow changes. Local 358 and global 7713 expected, cumulative validated 7355, transmission 25/40, local cadence 5/5, global 10/20 unchanged; rerun native Local after merge.

Maintenance after #231: user-reported Windows compilation succeeded, CKTX v1 63/63 passed; CKTX fragment smoke script failed to compile because MediaSession exposes sendPending(), not busy(). Correct both affected test groups (transport-text-fragments and transport-heterogeneous). Partial summary expected 115, PASS 63, missing 52, suite failed; do not advance validation baseline or functional cadence. Local manifest still contains five groups, expected complete Local 358, global 7713. No native API, manifest or workflow changes. Rerun complete Local after merge.

Validation checkpoint after #230 and maintenance #231–#232: user-reported Windows Local expected=358, PASS=358, FAIL=0, missing=0, unreported=0, extra=0, suites=1/1, status=PASS. All five text, heterogeneous and raster lossless groups validated. Cumulative validated checks 7713; last Full baseline remains 6865 after #217. Transmission 25/40 functional PRs; reset local cadence to 0/5, retain global cadence 10/20 and global manifest unchanged. Clear validated local groups. Matrix coverage 88.89% pending explicit reassessment. Next functional milestone PNG raster transport; advanced compression and 3D scope as approved above. No workflow changes.

#234: CKPN v1 bounded single-packet PNG RGBA8 transport via libpng simplified memory API (CMake PNG::PNG dependency), preserves straight alpha and pixel bytes for encoder-generated PNG profile, validates dimensions, chunk framing and compressed data, rejects trailing bytes and unsupported profiles. MediaRasterPng packetSize/encode/inspect/decode; MediaGraphics kind 6, MediaPacket kind 12; old kinds unchanged. Maximum decoded image 16 MiB, CKMP payload 1 MiB; larger encoded images require later fragmentation. 57 new checks, global expected 7770, local 57, transmission 26/40, local cadence 1/5, global cadence 11/20; native validation pending, cumulative validated 7713, matrix 88.89%. No workflow changes. PNG development library availability in Windows build still to be confirmed.

#235: CKJP v1 baseline JPEG RGB (opaque RGBA input required, decoded alpha 255) via libjpeg; CKWP v1 WebP lossy/lossless via libwebp, alpha quality 100, exact transparent RGB requested in lossless mode. Quality 1–100, single packet, bounded dimensions and decoded payload, strict JPEG warnings and WebP RIFF/mode checks. MediaRasterJpeg and MediaRasterWebp, MediaGraphics kinds 7/8, MediaPacket kinds 13/14. Existing kinds unchanged. CMake links JPEG::JPEG and PkgConfig::WEBP, native availability not yet verified. 88 new checks, global expected 7858, local 145, transmission 27/40, local cadence 2/5, global 12/20; cumulative validated 7713, matrix 88.89%, native validation pending. Compressed fragmentation follows. No workflow changes.

#236: CKCF v1 compressed raster horizontal strip transport, codec IDs 1 RLE, 2 PNG, 3 JPEG, 4 WebP lossy, 5 WebP lossless. Each strip is independently encoded, bounded by negotiated CKMP payload, with full image dimensions, image identity, row offset/count and codec. Explicit caller slicing, ordered contiguous reassembly, stable stream/timestamp/codec and consecutive sequence validation, 16 MiB decoded image bound. Not fragmentation of one existing compressed file; lossy strips may show boundary artifacts. MediaRasterCompressedFragment and MediaRasterCompressedAssembler; MediaGraphics kind 9, MediaPacket kind 15. User-approved MediaRasterJpeg.flatten(width,height,red,green,blue,source,destination) performs deterministic straight-alpha compositing in encoded RGB byte space over an explicit opaque color; separate source/destination arrays preserve input, JPEG still rejects nonopaque input. 129 new checks, global expected 7987, local 274, transmission 28/40, local cadence 3/5, global 13/20; cumulative validated 7713, matrix 88.89%, native validation pending. No workflow changes.

#237: MediaRasterAuto selects the smallest fitting lossless single-packet RGBA8/RLE/PNG/WebP lossless encoding, deterministic tie priority in that order, never silently selects lossy encoding. MediaRasterStripSender uses explicit codec/quality and caller-provided negotiated payload limit; tries remaining rows and halves until a strip fits, peek/commit lifecycle for retryable transport enqueue, validates JPEG opacity, one image at a time. Not guaranteed optimal strip packing; a single row too large cannot be subdivided by this sender, and subsequent prepare can fail on incompressible rows or sequence exhaustion. 78 new checks, global expected 8065, local 352, transmission 29/40, local cadence 4/5, global 14/20; cumulative validated 7713, matrix 88.89%, native validation pending. Raster extension five milestones published, validation at next scheduled checkpoint. Next functional milestone automatic video fragmentation. No workflow changes.

#238: MediaVideoFragmentSender automatic sequential CKVS v1 pixel fragmentation from an immutable frame copy, negotiated payload limit supplied by caller, exact maximum fragment pixel count, consecutive sequence/frame counters, strict timestamp progression, preflight sequence exhaustion and prepare/peek/commit retry lifecycle. Compatible with existing MediaVideoStream and buffering; caller drives transport and commits after enqueue acceptance, no automatic scheduling or delivery acknowledgement. Existing 16 MiB frame bound unchanged. 102 new checks, global expected 8167, local 454, transmission 30/40, local cadence 5/5, global 15/20; cumulative validated 7713, matrix 88.89%, native Local validation required before further functional development. No workflow changes.

Maintenance after #238: Windows native CMake configuration failed in FindPNG/FindZLIB with UCRT64 compiler and standalone Windows CMake. Use the existing configured UCRT64 pkg-config discovery for libpng and libjpeg, consistent with libwebp and other multimedia dependencies; no separate FindZLIB lookup required for shared libpng. Native verification pending; local 454, global 8167, validated 7713, functional transmission 30/40, local cadence 5/5, global 15/20 unchanged. No workflow changes.

Maintenance after #239: user-reported native configuration succeeded but C++ compilation failed because the original raster and new automatic raster bindings both defined ra_size. Rename only the automatic bindings to raster_auto_size and raster_auto_select, preserve public registration names and original raster binding. Static scan of 332 CK_DLL function definitions found no remaining duplicate names. Restore MediaSession.h as the first include so Winsock precedes codec headers on Windows. Native validation pending; local 454, global 8167, cumulative validated 7713, transmission 30/40, local 5/5, global 15/20 unchanged. No workflow or manifest changes.

Maintenance after #240: native Local reported 451 PASS / 3 FAIL of 454, video automatic fragmentation checks 98, 99 and 101. ChucK decimal integer lexer uses atoi (src/core/chuck.lex); out-of-range decimal literals in the counter exhaustion test are replaced with runtime int arithmetic from 65536, retaining the uint32 maximum and exhausted sentinel checks. No production code or test counts changed. Native rerun required; cumulative validated remains 7713, global expected 8167, local expected 454, functional transmission 30/40, local cadence 5/5, global 15/20.

Validation checkpoint after #238 and maintenance #239–#241: user-reported Windows Local expected=454, PASS=454, FAIL=0, missing=0, unreported=0, extra=0, suites=1/1, status=PASS. All five PNG, JPEG/WebP, compressed raster fragmentation and compositing, raster consolidation and automatic video fragmentation groups validated. Cumulative validated checks 8167; global expected 8167; last Full baseline remains 6865 after #217. Transmission 30/40 functional PRs, local cadence reset to 0/5, global cadence remains 15/20. Clear validated local groups, retain global manifest unchanged. Matrix coverage 88.89% pending explicit reassessment. Next functional milestone higher video resolutions (including 4K). No workflow changes. Windows native validation confirms the corrected dependency discovery, native bindings and counter tests on the user's configuration.

#243: extend CKVS fragmented RGBA8 frame limit from 4,194,304 to 16,777,216 pixels (64 MiB), covering 3840x2160 UHD and 4096x2160 DCI 4K, not 8K. Shared videoPixels bound applies to fragment encoder, automatic sender, stream reassembly, buffer and AV integration; existing 64 MiB VideoBuffer byte budget and 1 MiB CKMP payload limit unchanged. Old 16 MiB receiver implementations may reject larger frames; no capability negotiation change. ChucK arrays store ints rather than packed bytes, so caller-side memory exceeds native RGBA byte counts. Update obsolete bound assertion in transport-video-stream without changing count. 256 new checks including full UHD automatic sender-to-buffer reassembly, exact representative pixel checks, budget reservation and DCI metadata/partial reassembly, global expected 8423, local 256, cumulative validated 8167, transmission 31/40, local cadence 1/5, global 16/20, matrix 88.89%. Native validation pending; no workflow changes.

#244: CKVC v1 independent compressed horizontal RGBA video strips, CKMP type 6, codecs 1 RLE, 2 PNG, 3 JPEG (opaque), 4 WebP lossy, 5 WebP lossless. MediaVideoCompressedFragment.packetSize/encode/inspect; caller chooses strips and codec/quality, decoded strip pixel count bounded by CKVS maximum so existing sequence exhaustion checks remain valid. Reuse existing codec validation; full frame up to 64 MiB, payload up to 1 MiB. Existing VideoStream/VideoBuffer/AudioVideo accept CKVC and CKVS strips including mixed representations; MediaAVPacket kind 4 and MediaPacket kind 16, old IDs unchanged. No automatic compressed strip sender yet, no codec capability negotiation, lossy strip boundaries may show artifacts. 122 new checks, global expected 8545, local 378, cumulative validated 8167, transmission 32/40, local cadence 2/5, global 17/20, matrix 88.89%. Native validation pending; no workflow changes.

#245: CKVD v1 lossless interframe XOR residual with pixel RLE, CKMP type 6, immediately previous decoded frame reference, CRC32 over every reference slice byte and explicit reference index validation. MediaVideoDeltaFragment.packetSize/encode/inspect/reconstruct converts a validated delta to a normal CKVS packet for existing stream/buffer/AV receivers. MediaAVPacket kind 5 and MediaPacket kind 17, existing IDs unchanged. Caller retains reference slices and selects keyframes/fragment boundaries; no automatic reference cache, standard H.264/HEVC/AV1 bitstreams, motion estimation or codec negotiation. CRC is accidental mismatch detection, not authentication. Incompressible residuals may exceed packet limit; use raw CKVS fallback. 94 new checks, global expected 8639, local 472, cumulative validated 8167, transmission 33/40, local cadence 3/5, global 18/20, matrix 88.89%. Native validation pending; no workflow changes.
