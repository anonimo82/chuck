# VectorIO (experimental)

Optional native Chugin for SVG validation, saving SVG markup and rendering SVG to PNG. Uses librsvg >= 2.46 and Cairo. No VM changes.

Linux (Debian/Ubuntu): install build-essential, pkg-config, librsvg2-dev and libcairo2-dev. macOS: install librsvg, cairo and pkg-config. Run make here, then use a matching ChucK binary (DLL API 10.2):

    chuck --silent --chugin-path:. example.ck

API:

- VectorIO.valid(string path): 1 if librsvg accepts the SVG, 0 on failure.
- VectorIO.save(string svg, string path): validate and save the supplied SVG markup, returning 1 on success or 0 on failure.
- VectorIO.render(string source, string destination, int width, int height): render to PNG, returning 1 on success or 0 on failure. Dimensions must be 1–4096 pixels.

Outputs must not exist. The save operation preserves the supplied SVG text; it does not convert a ChuGL scene to vector graphics. No vector shape editing API, DOM import, EPS/PDF export or SVG animation support is provided. External relative resources may not resolve because SVG is loaded without a base URI. Use trusted local inputs; these operations are synchronous and not intended for real-time audio processing.

Build and runtime validation are pending. Windows build is not provided yet.

## Persistent VectorImage

```chuck
@import "VectorIO";
VectorImage image;
if(image.load("input.svg")) {
    100.0 => image.x;
    1.5 => image.scaleX;
    1.5 => image.scaleY;
    30.0 => image.rotation;
    0.8 => image.opacity;
    <<< image.render("output.png",1280,720) >>>;
}
```

The SVG is parsed once and retained as a native librsvg handle. Each instance has independent controls. load(path) and setSvg(markup) replace the document and reset controls only on success; input limit 16 MiB. loaded(), clear(), reset(), render(path,width,height) operate on the current document. Six float getter/setter pairs: x/y (output-pixel translations, +/-32768, default 0), scaleX/scaleY (0.001–64, default 1), rotation (clockwise degrees, +/-1e9, default 0), opacity (0–1, default 1). Scale and rotation are around the center of the output viewport, then translation is applied. Invalid setters return NaN and preserve previous values. Cairo renders the SVG into a group before applying opacity, preserving correct compositing for overlapping shapes. Output is PNG on a transparent canvas, 1–4096 pixels per axis, clipped to the canvas, exclusive creation. No SVG DOM or individual shape editing yet; transforms are global and non-destructive. No dependency on KeyframeIO; any numeric source may drive setters. Synchronous offline operations, not real-time safe. Thirty new smoke checks plus independent RGBA assertions await execution.

## Procedural vector synthesis

VectorImage.create(width,height) starts an empty editable SVG canvas (1–4096 pixels). addRect(x,y,w,h,rgb,alpha), addCircle(x,y,radius,rgb,alpha), addLine(x,y,x2,y2,strokeWidth,rgb,alpha), addPolygon(float[] xy,rgb,alpha) add shapes in paint order. RGB is packed 0xRRGGBB and alpha is 0–1. Polygon accepts 3–4096 XY pairs. shapes() counts generated shapes; removeShape(index) removes one. Imported SVG supports rendering and raw save, but shapes() returns -1 and structural editing is unavailable. Generated documents are limited to 4096 shapes and 16 MiB; edits rebuild and reparse the SVG transactionally and preserve global controls. save(path) writes the untransformed SVG markup to a new lowercase .svg; render() and exportPixels(width,height,int[]) apply global controls. exportPixels writes straight RGBA8 (unpremultiplied Cairo ARGB32) to exactly width*height*4 preallocated integers, compatible with RasterImage. No new dependencies. 37 smoke checks and independent XML/RGB assertions await Windows testing.

## Waveform import

importWaveform(width,height,float[] bounds,rgb,alpha) creates a new editable canvas containing one filled SVG path with a rectangle for each min/max amplitude pair. Bounds are finite, ordered and within [-1,1]; 1–4096 pairs. Canvas dimensions 1–4096. Positive amplitudes are at the top; each bucket has at least one SVG unit of height, including silence. Transparent background. Success replaces the document and resets controls; failure preserves it. Compatible with AudioBuffer.exportEnvelope. Native validation pending.

## Vector-to-3D silhouette

exportAlpha(width,height,int[] alpha) renders the current SVG with all global controls and writes row-major alpha values 0–255 into an exactly sized width*height array. Dimensions 1–4096; failure preserves output. For a 3D extrusion pass the mask (at most 256 per axis) to Model3D.createSilhouette. Works for imported and generated SVG, including holes and disconnected shapes. No new native dependency. This is raster-based approximation, not exact SVG curve tessellation.

## Raster-to-vector conversion

importPixels(width,height,int[] pixels) converts straight row-major RGBA8 pixels to editable SVG rectangles, merging horizontally adjacent pixels of identical RGBA and omitting fully transparent runs. Dimensions 1–4096, at most 4194304 pixels and 4096 nontransparent runs; exact array size width*height*4, components 0–255. Invalid input or excess complexity preserves the previous document and controls. Success replaces the document and resets controls. This is pixel-art vectorization, not contour tracing or photographic simplification. Compatible with RasterImage.exportPixels, including its current color controls. Rendering at original dimensions preserves opaque colors and alpha, subject to Cairo premultiplication rounding for translucent colors. No new dependency; native validation pending.

## Projected triangle import

importTriangles(width,height,float[] triangles) replaces the current document with editable filled polygons in supplied order. Seven floats per triangle: three XY pairs and integral packed RGB 0–0xffffff. Dimensions 1–4096, 1–4096 input triangles; finite coordinates within existing SVG limits. Zero-area triangles are omitted, and canvas clips out-of-bounds geometry. Success resets controls; failure preserves document and controls. Compatible with Model3D.projectTriangles; no new native dependency. Native validation pending.

## SVG-to-MIDI sonification

exportPitches(width,height,lowPitch,highPitch,alphaThreshold,scan,int[] pitches) renders the current SVG, including transforms and opacity, at the requested sampling resolution in memory. Straight RGBA8 is recovered using the same unpremultiplication as exportPixels. Weighted sRGB brightness maps to the inclusive MIDI pitch range 0–127; alpha below threshold 1–255 yields -1 (rest). Scan 0 row-major, 1 column-major, 2 serpentine. Dimensions 1–4096, at most 100000 samples, exact output array length width*height. Failure preserves output; SVG is unchanged. Compatible with MidiSequence.importPitches and RasterImage.exportPitches conventions. Sampling resolution and antialiasing affect results; this is creative sonification, not optical music recognition or direct path traversal. No new dependency. Native validation pending.

## SVG animation to video

exportFrame(width,height,backgroundRGB,int[] pixels) renders the current SVG with translation, scale, rotation and opacity, then composites Cairo premultiplied RGBA over an opaque RGB background. Output is row-major RGBA8 with alpha 255, suitable for VideoWriter.appendFrame. Dimensions 1–4096, at most 4194304 pixels, exact array length width*height*4, background 0–0xffffff. Invalid inputs leave output unchanged; document and controls are unchanged. Example animate_video.ck animates the existing controls at 25 fps and writes lossless MKV without intermediate files. This animates global transforms, not SVG path morphing or SMIL playback. No new dependencies. Native validation pending.

## Batch editable rectangle import

importRects(width,height,float[] rects) replaces the document with independent SVG rectangles from groups of six values x,y,width,height,packed RGB,opacity. Canvas dimensions 1–4096; up to 4096 rectangles, finite coordinates within +/-1000000, positive extents, integral RGB 0–0xffffff, opacity 0–1. Out-of-canvas geometry is clipped by the viewport. Empty array creates an empty canvas. Success resets controls; failure preserves document and controls. Compatible with MidiSequence.exportPianoRollRects. No new dependency. Native validation pending.

## SVG-to-audio sonification

exportAudioSamples(width,height,channels,float[] samples) renders the current SVG in memory including global transforms and opacity, recovers straight RGBA8 from Cairo premultiplied pixels, and maps one row-major pixel to one PCM frame. Mono is (2*weighted RGB luminance-1)*alpha; stereo is (R+G-1)*alpha and (B+G-1)*alpha, components normalized to 0–1. Transparent pixels are silent. Same mapping as RasterImage.exportAudioSamples. Dimensions 1–4096, max 4194304 pixels, channels 1 or 2, exact array length width*height*channels. Invalid input preserves output; document and controls unchanged. Pass samples to AudioBuffer.importSamples at a chosen sample rate. Example sonify_svg.ck uses stereo at 44100 Hz and attenuates before WAV export. This is creative pixel sonification, not reconstruction of encoded audio. No new dependencies. Native validation pending.
