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
