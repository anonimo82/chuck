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
