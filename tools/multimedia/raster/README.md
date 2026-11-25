# RasterIO (experimental)

Optional native Chugin using ImageMagick MagickWand, called directly from ChucK. No changes to the VM or existing classes. Requires ChucK DLL API 10.2 and a C++17 compiler.

On Debian/Ubuntu install build-essential, pkg-config and libmagickwand-dev. On macOS install imagemagick and pkg-config using Homebrew. Build from this directory with make. Windows build is not yet provided.

Run with a matching ChucK binary:

    chuck --chugin-path:. example.ck:input.jpg:output.png

API:

- RasterIO.width(string path): first image width, or -1 on error.
- RasterIO.height(string path): first image height, or -1 on error.
- RasterIO.convert(string source, string destination): 1 on success, 0 on failure. Destination must not exist.

Allowed extensions: PNG, JPG/JPEG, BMP, TIFF/TIF, WebP, GIF, TGA. Actual codec availability depends on ImageMagick build and security policy. Only the first frame is exported; animated GIF, multi-page TIFF and metadata preservation are not guaranteed. Use trusted local inputs, and configure ImageMagick resource limits. Operations are synchronous and must not be used in real-time audio processing. Pixel arrays and ChuGL texture integration are not yet implemented.

Smoke test: use a known 2x3 PNG fixture, verify width and height, convert to BMP, verify dimensions again, verify attempting to overwrite the BMP fails. Compile and run checks are pending.

### Keyframed raster animation

```chuck
@import "RasterIO";
<<< RasterIO.animate("input.png","animation.tsv","frame",120,30) >>>;
```

Writes frame000000.png through frame000119.png at 30 FPS. All frames keep the source dimensions. The shared keyframe engine samples each curve at frame index / FPS. Manifest rows are parameter, time in seconds, value, outgoing interpolation, for example:

```text
red 0 1 linear
red 4 0.5 smooth
opacity 0 1 linear
opacity 4 0 linear
brightness 0 0 linear
brightness 4 0.2 linear
```

Optional parameters: red/green/blue multipliers [0,4] (default 1), opacity multiplier [0,1] (default 1), brightness additive offset [-1,1] (default 0). At least one curve required. RGB is converted to sRGB 8-bit, multiplied and offset, clamped and rounded; alpha is multiplied separately. No geometric animation or animated GIF output yet. Up to 10000 frames, 1–120 FPS, source up to 8192 per axis and 16 megapixels. Existing outputs are never overwritten; newly created frames are removed on failure. No extra libraries. Nine new smoke checks and independent FFmpeg RGBA comparisons await execution.

## Persistent RasterImage

```chuck
@import "RasterIO";
RasterImage image;
if(image.load("input.png")) {
    0.5 => image.opacity;
    1.2 => image.red;
    <<< image.save("output.png") >>>;
}
```

An image is decoded once and held as straight-alpha sRGB RGBA8 pixels. Each object owns independent data and controls. Methods: load(path), save(path), loaded(), width(), height(), clear(), reset(). Width/height are zero when unloaded. load returns 1 on success and resets controls; a failed load leaves the object unchanged. clear releases pixels and resets controls. reset restores controls without reloading. red(), green(), blue(), opacity(), brightness() are getters and accept a float argument as setters (also usable with ChucK's chuck operator). Ranges and defaults match RasterIO.animate. Invalid setters return NaN without changing the old value. save applies current controls to the original pixels, not the previous output, and uses exclusive file creation. All existing raster output extensions are recognized; lossy formats and formats without alpha may lose precision/transparency. PNG is recommended for exact RGBA output. Source metadata is not retained. Source image limits: 8192 per axis, 16 megapixels. Loading/saving is offline, not real-time audio safe.

```chuck
@import "RasterIO";
@import "KeyframeIO";
RasterImage image;
KeyframeCurve gain;
image.load("input.png");
gain.add(0.0, 1.0, "linear");
gain.add(2.0, 2.0, "smooth");
gain.valueAt(1.0) => image.red;
image.save("frame.png");
```

RasterImage has no knowledge of curves; it accepts ordinary numbers. The legacy RasterIO.animate uses the same rendering helper. Thirty-three new object and integration smoke checks plus independent RGBA comparisons await execution.

## In-memory drawing and editing

RasterImage.fill(r,g,b,a), fillRect(x,y,w,h,r,g,b,a), line(x,y,x2,y2,r,g,b,a), and fillCircle(x,y,radius,r,g,b,a) replace raw RGBA8 pixels. Coordinates use a top-left origin; rectangle and line endpoints must fit; circles are clipped to the canvas, with center inside. Drawing is non-antialiased. crop(x,y,w,h) extracts an in-bounds rectangle and resize(w,h) uses nearest-neighbor sampling. Both are transactional, preserve numeric controls and existing image bounds. composite(x,y,w,h,int[] pixels,float opacity) performs straight-alpha source-over blending with clipping, row-major RGBA8, opacity [0,1]. It validates all source values before writing. Compositing uses raw destination pixels; existing color controls apply afterward on export/save. All methods return 1 on success and 0 for invalid input. No extra dependencies or temporary files. 37 new smoke checks plus independent RGB assertions; Windows execution pending.

## Raster-to-audio sonification

exportAudioSamples(channels,float[] samples) maps each pixel to one PCM frame in row-major order (left to right, top to bottom). Mono uses weighted sRGB components (0.2126 R + 0.7152 G + 0.0722 B); stereo uses (R+G)/2 and (B+G)/2 respectively. Components are normalized to [0,1], mapped to [-1,1] and multiplied by processed alpha, so transparent pixels are silent. Current RGBA controls apply with the same rounding as exportPixels. Exact preallocated size width*height*channels, channels 1 or 2 only. Failure leaves output unchanged; source is not modified. Feed the resulting samples to AudioBuffer.importSamples(rate,channels,samples). Rate is chosen by the caller, with one frame per pixel. This is direct sonification, not recovery of original audio from a waveform or spectrogram. Constant regions can produce DC; use offline highPass and fade for listening. No new dependencies; native validation pending.

## Heightmap export

exportHeightmap(float[] heights) writes width*height normalized heights, in row-major order, using weighted sRGB brightness multiplied by alpha. The current color and opacity controls apply, with the same component rounding as exportPixels. Transparent pixels map to zero height. Exact preallocated array size required; failure preserves the output and the image. Compatible with Model3D.createHeightmap. Native validation pending.

## Raster-to-MIDI sonification

exportPitches(lowPitch,highPitch,alphaThreshold,scan,int[] pitches) maps each pixel to a MIDI pitch by rounded weighted sRGB brightness across the inclusive pitch range 0–127. Alpha below threshold (1–255) maps to -1 (rest). Current color/opacity controls apply with RGBA8 rounding. Scan 0 is row-major, 1 column-major, 2 alternating horizontal rows (serpentine). Exact array size width*height, maximum 100000 pixels. Invalid input preserves output. Compatible with MidiSequence.importPitches; each pixel is a fixed-duration slot, adjacent equal pitches merge, velocity is configured by the MIDI importer. This is creative mapping, not optical music recognition. No new dependencies. Native validation pending.

## Raster-to-video crossfade

exportBlend(int[] targetPixels,float mix,int backgroundRGB,int[] pixels) exports an opaque RGBA8 frame. Both source (with current controls) and target (already processed RGBA8) are flattened over the selected RGB background, then interpolated linearly in encoded RGB with mix 0–1. Arrays must have exactly width*height*4 entries, maximum 4194304 pixels; target values 0–255. Output may alias target; invalid input leaves output unchanged. Source image is not modified. Use VideoWriter.appendFrame for lossless MKV output. Example dissolve_video.ck resizes both images to a common canvas, then writes a two-second dissolve. This is a crossfade, not motion-compensated interpolation. No new dependency; native validation pending.

## Raster capture foundation

RasterCapture.platform() returns 0 unknown, 1 Windows, 2 Linux, 3 macOS. Source IDs 1 camera and 2 screen; supported(source) reports whether a backend is implemented, deviceCount(source) returns detected count or -1 on invalid source/enumeration error; see backend-specific sections below. The initial foundation did not open devices; subsequent sections describe native backends. ingestFrame(width,height,seconds,int[] pixels) accepts caller-supplied top-down tightly packed straight RGBA8, dimensions <=8192 per side and <=4194304 pixels total, finite nonnegative timestamp, exact array size and channels 0–255. ready(), width(), height(), timestamp(), exportPixels(int[]) and clear() expose the last snapshot. Failed ingestion preserves the previous frame; failed export preserves destination. This is a testable shared frame contract, not live device capture. Native validation pending.

## Camera snapshots

Optional CMake MULTIMEDIA_CAMERA_CAPTURE (default ON) detects libavdevice using pkg-config; if absent, camera support is disabled and raster functionality remains available. supported(1) checks registered FFmpeg input format (not permission or device presence). deviceCount(1) returns count, 0 when backend absent, -1 when enumeration fails or is not supported. deviceName(1,index) returns an enumerated name or empty string; list may change between calls. captureCamera(name,width,height) explicitly opens a video input, decodes a frame into RGBA8, closes it and replaces the last snapshot only on success. Windows uses DirectShow video friendly name (without video= prefix); Linux uses /dev/videoN; macOS uses explicit AVFoundation camera name/index. No audio acquisition. Requested dimensions are negotiated by the backend; inspect actual dimensions. Timestamp is elapsed monotonic seconds since the snapshot request began, not device exposure time. Synchronous operation can block; a cooperative five-second interrupt deadline and packet limit are attempted, but driver calls may not honor interruption. Not suitable for audio realtime threads. Camera permission and driver errors return 0; no automatic permission bypass. Tests exercise validation and failure preservation without opening physical devices; native compile and device tests remain pending.

## Screen snapshots

MULTIMEDIA_SCREEN_CAPTURE (default ON) enables Windows GDI (system libraries) or optional Linux X11 via pkg-config. macOS and native Wayland screen capture are not implemented. supported(2) reports compiled backend; deviceCount(2) is 1 if primary/default screen dimensions are accessible, otherwise 0. deviceName(2,0) returns primary when accessible. screenWidth()/screenHeight() return dimensions or -1. captureScreen(x,y,width,height) captures an explicitly selected rectangle in primary/default screen coordinates, top-left origin, wholly inside the screen and within the existing frame size limits. RGBA8 alpha is opaque; timestamp measures monotonic request duration. Failed requests preserve the previous snapshot. No cursor compositing, window capture, secondary monitor selection, protected-content bypass or permission escalation. X11 is limited to TrueColor; the X server may fail/disconnect independently. Windows DPI virtualization may affect coordinates. Synchronous calls are unsuitable for real-time audio threads. Headless tests exercise invalid requests and state preservation; actual desktop acquisition requires manual validation.

## Importing captured images

RasterImage.importCapture(RasterCapture capture) copies the latest valid RGBA8 frame directly into the image, without a temporary file or ChucK pixel array. Dimensions and alpha are preserved; rendering controls reset to neutral, consistent with load/importPixels. Capture ownership and timestamp remain unchanged. Later edits or capture.clear() do not affect the other object. Invalid or empty capture returns 0 without changing image state. The imported image can use existing crop, resize, drawing, save, pixel export and conversion pathways. Native validation pending; headless tests use a programmatically ingested frame.

## Direct saving and manual device check

RasterCapture.save(path) saves the current frame with neutral rendering controls using existing raster formats and exclusive file creation. Empty/invalid capture, unsupported extension or existing file returns 0 without modifying the frame. No automatic capture or overwrite. The opt-in capture_snapshot.ck example takes a screen or camera selection and saves original and resized preview PNGs in the working directory. Use a non-sensitive desktop region or a camera you are authorized to use; no device capture is performed by automatic tests. For example, with the installed chugin path configured, run chuck --silent --chugin-path:<installed-chugins> capture_snapshot.ck:screen in a fresh working directory (use the actual script path). For a camera, pass two script arguments, e.g. quote the entire shell argument "capture_snapshot.ck:camera:USB Camera". Device names containing colons are not supported by this example. Device enumeration and actual capture success remain dependent on permissions, backend and environment. Native validation pending.
